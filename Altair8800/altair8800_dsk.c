/* altair8800_dsk.c: Soft Sector Disk Library

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

#include "sim_defs.h"
#include "altair8800_dsk.h"

static void calc_offset(DSK_INFO *d);

/*
 * INTERLEAVED disk images are structured as follows:
 *
 * +------------------+
 * | TRACK 0 / HEAD 0 |
 * +------------------+
 * | TRACK 0 / HEAD 1 |
 * +------------------+
 * | TRACK 1 / HEAD 0 |
 * +------------------+
 * | TRACK 1 / HEAD 1 |
 * +------------------+
 * | TRACK n / HEAD 0 |
 * +------------------+
 * | TRACK n / HEAD 1 |
 * +------------------+
 *
 * NON-INTERLEAVED disk images are structured as follows:
 *
 * +------------------+
 * | TRACK 0 / HEAD 0 |
 * +------------------+
 * | TRACK 1 / HEAD 0 |
 * +------------------+
 * | TRACK n / HEAD 0 |
 * +------------------+
 * | TRACK 0 / HEAD 1 |
 * +------------------+
 * | TRACK 1 / HEAD 1 |
 * +------------------+
 * | TRACK n / HEAD 1 |
 * +------------------+
 *
 */

t_stat dsk_init(DSK_INFO *d, UNIT *unit, int tracks, int heads, int interleaved)
{
    if (d == NULL) {
        return SCPE_ARG;
    }
    if (tracks < 1 || tracks > DSK_MAX_TRACKS) {
        return SCPE_ARG;
    }
    if (heads < 1 || heads > DSK_MAX_HEADS) {
        return SCPE_ARG;
    }

    d->unit = unit;
    d->fmt.tracks = tracks;
    d->fmt.heads = heads;
    d->fmt.interleaved = interleaved;

    return SCPE_OK;
}

t_stat dsk_init_format(DSK_INFO *d, int strack, int etrack, int shead, int ehead, int den, int secs, int secsize, int stsec)
{
    int tr, hd;

    if (d == NULL) {
        return SCPE_ARG;
    }
    if (strack < 0 || strack > d->fmt.tracks - 1) {
        return SCPE_ARG;
    }
    if (shead < 0 || shead > d->fmt.heads - 1) {
        return SCPE_ARG;
    }
    if (strack > etrack || shead > ehead) {
        return SCPE_ARG;
    }

    if (d->fmt.tracks < (etrack - strack + 1)) {
        d->fmt.tracks = (etrack - strack + 1);
    }

    if (d->fmt.heads < (ehead - shead + 1)) {
        d->fmt.heads = ehead - shead + 1;
    }

    if (d->fmt.interleaved && d->fmt.heads > 1) {
        for (tr = strack; tr <= etrack; tr++) {
            for (hd = shead; hd <= ehead ; hd++) {
                d->fmt.track[tr][hd].density = den;
                d->fmt.track[tr][hd].sectors = secs;
                d->fmt.track[tr][hd].sectorsize = secsize;
                d->fmt.track[tr][hd].startsector = stsec;
            }
        }
    }
    else {
        for (hd = shead; hd <= ehead; hd++) {
            for (tr = strack; tr <= etrack; tr++) {
                d->fmt.track[tr][hd].density = den;
                d->fmt.track[tr][hd].sectors = secs;
                d->fmt.track[tr][hd].sectorsize = secsize;
                d->fmt.track[tr][hd].startsector = stsec;
            }
        }
    }

    calc_offset(d);

    return SCPE_OK;
}

static void calc_offset(DSK_INFO *d) {
    int t, h, offset = 0;

    if (d->fmt.interleaved && d->fmt.heads > 1) {
        for (t = 0; t < d->fmt.tracks; t++) {
            for (h = 0; h < d->fmt.heads; h++) {
                sim_debug(d->dbg_verbose, d->unit->dptr, "T:%02d H:%d O:%d\n", t, h, offset);
                d->fmt.track[t][h].offset = offset;

                /* Set offset to start of next track */
                offset += d->fmt.track[t][h].sectors * d->fmt.track[t][h].sectorsize;
            }
        }
    }
    else {
        for (h = 0; h < d->fmt.heads; h++) {
            for (t = 0; t < d->fmt.tracks; t++) {
                sim_debug(d->dbg_verbose, d->unit->dptr, "T:%02d H:%d O:%d\n", t, h, offset);
                d->fmt.track[t][h].offset = offset;

                /* Set offset to start of next track */
                offset += d->fmt.track[t][h].sectors * d->fmt.track[t][h].sectorsize;
            }
        }
    }
}

t_stat dsk_validate(DSK_INFO *d, int track, int head, int sector)
{
    if (track < 0 || track > d->fmt.tracks - 1) {
        sim_printf("DSK: ** Invalid track number %d\n", track);
        return SCPE_IOERR;
    }
    if (head < 0 || head > (d->fmt.heads - 1)) {
        sim_printf("DSK: ** Invalid head number %d\n", head);
        return SCPE_IOERR;
    }
    if (sector < d->fmt.track[track][head].startsector || sector > (d->fmt.track[track][head].sectors - ((d->fmt.track[track][head].startsector) ? 0 : 1))) {
        sim_printf("DSK: ** Invalid sector number. track/head %d/%d has %d sectors. %d requested.\n", track, head, d->fmt.track[track][head].sectors, sector);
        return SCPE_IOERR;
    }

    return SCPE_OK;
}

int32 dsk_size(DSK_INFO *d)
{
    if (d != NULL && d->unit != NULL && d->unit->fileref != NULL) {
        return sim_fsize(d->unit->fileref);
    }

    return 0;
}

int32 dsk_tracks(DSK_INFO *d)
{
    if (d != NULL) {
        return d->fmt.tracks;
    }

    return 0;
}

int32 dsk_track_size(DSK_INFO *d, int32 track, int32 head)
{
    if (d != NULL) {
        return d->fmt.track[track][head].sectors * d->fmt.track[track][head].sectorsize;
    }

    return 0;
}

int32 dsk_sectors(DSK_INFO *d, int32 track, int32 head)
{
    if (d != NULL) {
        return d->fmt.track[track][head].sectors;
    }

    return 0;
}

int32 dsk_sector_size(DSK_INFO *d, int32 track, int32 head)
{
    if (d != NULL) {
        return d->fmt.track[track][head].sectorsize;
    }

    return 0;
}

int32 dsk_start_sector(DSK_INFO *d, int32 track, int32 head)
{
    if (d != NULL) {
        return d->fmt.track[track][head].startsector;
    }

    return 0;
}

int32 dsk_sector_offset(DSK_INFO *d, int32 track, int32 head, int32 sector)
{
    if (d != NULL) {
        return d->fmt.track[track][head].offset + (dsk_sector_size(d, track, head) * (sector - dsk_start_sector(d, track, head)));
    }

    return 0;
}

t_stat dsk_read_sector(DSK_INFO *d, int32 track, int32 head, int32 sector, uint8 *buf, int32 *bytesread)
{
    int32 b, ssize;
    t_stat r = SCPE_OK;

    if (d == NULL || d->unit == NULL || d->unit->fileref == NULL) {
        return SCPE_ARG;
    }

    if ((r = dsk_validate(d, track, head, sector)) != 0) {
        return SCPE_ARG;
    }

    ssize = dsk_sector_size(d, track, head);

    fseek(d->unit->fileref, dsk_sector_offset(d, track, head, sector), SEEK_SET);

    if ((b = fread(buf, 1, ssize, d->unit->fileref)) != ssize) {
        r = SCPE_IOERR;
    }

    if (bytesread != NULL) {
        *bytesread = b;
        sim_debug(d->dbg_verbose, d->unit->dptr, "DSK RD SEC: T:%d H:%d S:%d SS:%d READ:%d\n", track, head, sector, ssize, *bytesread);
    }


//    dsk_dump_buf(buf, ssize);

    return r;
}

t_stat dsk_write_sector(DSK_INFO *d, int32 track, int32 head, int32 sector, const uint8 *buf, int32 *byteswritten)
{
    int b, ssize, offset;
    int r = SCPE_OK;

    if (d == NULL || d->unit == NULL || d->unit->fileref == NULL) {
        return SCPE_ARG;
    }

    if ((r = dsk_validate(d, track, head, sector)) != 0) {
        return r;
    }

    ssize = dsk_sector_size(d, track, head);
    offset = dsk_sector_offset(d, track,head, sector);

    fseek(d->unit->fileref, offset, SEEK_SET);

    b = fwrite(buf, 1, ssize, d->unit->fileref);

    if (byteswritten != NULL) {
        *byteswritten = b;
        sim_debug(d->dbg_verbose, d->unit->dptr, "DSK WR SEC: T:%d H:%d S:%d SS:%d O:%d WRITTEN:%d\n", track, head, sector, ssize, offset, *byteswritten);
    }


//    dsk_dump_buf(buf, ssize);

    return r;
}

t_stat dsk_read_track(DSK_INFO *d, int32 track, int32 head, uint8 *buf)
{
    if (d == NULL || d->unit == NULL || d->unit->dptr == NULL) {
        return SCPE_ARG;
    }

    sim_debug(d->dbg_verbose, d->unit->dptr, "DSK RD TRK: T:%d H:%d\n", track, head);

    return SCPE_OK;
}

t_stat dsk_write_track(DSK_INFO *d, int32 track, int32 head, uint8 fill)
{
    int s, ssize, start;
    unsigned char *b;

    if (d == NULL) {
        return SCPE_ARG;
    }

    ssize = dsk_sector_size(d, track, head);
    start = dsk_start_sector(d, track, head);

    if ((b = malloc(ssize)) == NULL) {
        return 0;
    }

    memset(b, fill, ssize);

    sim_debug(d->dbg_verbose, d->unit->dptr, "DSK WR TRK: T:%d H:%d SS:%d F:%02X\n", track, head, ssize, fill);

    for (s = 0; s < dsk_sectors(d, track, head); s++) {
        dsk_write_sector(d, track, head, s + start, b, NULL);
    }

    free(b);

    return 0;
}

t_stat dsk_format(DSK_INFO *d, uint8 fill)
{
    int t, h;

    if (d == NULL) {
        return SCPE_ARG;
    }

    for (t = 0; t < d->fmt.tracks; t++) {
        for (h = 0; h < d->fmt.heads; h++) {
            dsk_write_track(d, t, h, fill);
        }
    }

    return SCPE_OK;
}

void dsk_dump_buf(const uint8 *b, int32 size)
{
    int i;

    if (b == NULL) {
        return;
    }

    for (i = 0; i < size; i++) {
        if ((i & 0x0f) == 0x00) {
            sim_printf("%04X: ", i);
        }
        sim_printf("%02X%c", b[i], ((i & 0x0f) == 0x0f) ? '\n' : ' ');
    }
}

void dsk_show(DSK_INFO *d)
{
    int t, h;

    if (d != NULL) {
        sim_printf("\n");
        sim_printf("fmt.tracks = %d\n", d->fmt.tracks);
        sim_printf("fmt.heads = %d\n", d->fmt.heads);

        for (t = 0; t < d->fmt.tracks; t++) {
            for (h = 0; h < d->fmt.heads; h++) {
                sim_printf("T:%02d H:%d D:%s SECS:%02d SECSIZE:%04d OFFSET:%05X\n", t, h,
                    d->fmt.track[t][h].density == DSK_DENSITY_SD ? "SD" : "DD",
                    d->fmt.track[t][h].sectors,
                    d->fmt.track[t][h].sectorsize,
                    d->fmt.track[t][h].offset);
            }
        }
    }
}

void dsk_set_verbose_flag(DSK_INFO *d, uint32 flag)
{
    if (d != NULL) {
        d->dbg_verbose = flag;
    }
}

t_stat dsk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    uint32 i;

    for (i=0; i < dptr->numunits; i++) {
        if ((dptr->units[i].flags & UNIT_ATTABLE) &&
            !(dptr->units[i].flags & UNIT_DIS)) {
            fprintf (st, "  sim> ATTACH {switches} %s diskfile\n", sim_uname(&dptr->units[i]));
        }
    }

    fprintf (st, "\n%s attach command switches\n", dptr->name);
    fprintf (st, "    -E          Must Exist (if not specified an attempt to create the indicated\n");
    fprintf (st, "                disk container will be attempted).\n");
    fprintf (st, "    -N          New file. Existing file is overwritten.\n");
    fprintf (st, "    -R          Attach Read Only.\n");

    fprintf (st, "\n\n");

    return SCPE_OK;
}

