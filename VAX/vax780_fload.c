/* vax780_fload.c: VAX780 FLOAD command

   Copyright (c) 2006-2008, Robert M Supnik

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

   This code is based on the CP/M RT11 utility, which bears the following
   copyrights:

    copyright (c) 1980, William C. Colley, III

    Rev. 1.2 -- Craig Davenport - Incitec Ltd      (Feb 1984)
                                  P O Box 140
                                  Morningside,
                                  Qld 4170,
                                  Australia.
             -- Modified for Digital Research C compiler under CP/M-86
             -- Assebmbly language routines added for BIOS calls etc.

   Thanks to Phil Budne for the original adaptation of RT11 to SimH.

   28-May-08    RMS     Inlined physical memory routines
*/

#include "vax_defs.h"
#include <ctype.h>

#define BLK_SIZE        256                             /* RT11 block size */

/* Floppy disk parameters */

#define BPT             26                              /* blocks/track */
#define NTRACKS         77
#define SECTOR_SKEW     2
#define TRACK_SKEW      6
#define TRACK_OFFSET    1                               /* track 0 unused */

/* RT11 directory segment (2 blocks = 512 16b words) */

#define DS_TOTAL        0                               /* segments available */
#define  DS_MAX         31                              /* segment max */
#define DS_NEXT         1                               /* zero for last segment */
#define DS_HIGHEST      2                               /* only in 1st segment */
#define DS_EXTRA        3                               /* extra bytes/entry */
#define DS_FIRST        4                               /* first block */
#define DS_ENTRIES      5                               /* start of entries */
#define DS_SIZE         (2 * BLK_SIZE)                  /* segment size, words */

/* RT11 directory entry offsets */

#define DE_STATUS       0                               /* status (odd byte) */
#define  TENTAT          001                            /* tentative */
#define  EMPTY           002
#define  PERM            004
#define  ENDSEG          010                            /* end of segment */
#define DE_NAME         1                               /* file name */
#define DE_FLNT         4                               /* file length */
#define DE_SIZE         7                               /* entry size in words */
#define DE_GET_STAT(x)  (((x) >> 8) & 0377)

extern UNIT fl_unit;

t_bool rtfile_parse (char *pntr, uint16 *file_name);
uint32 rtfile_lookup (uint16 *file_name, uint32 *start);
uint32 rtfile_ator50 (uint32 ascii);
t_bool rtfile_read (uint32 block, uint32 count, uint16 *buffer);
uint32 rtfile_find (uint32 block, uint32 sector);

/* FLOAD file_name {file_origin} */

t_stat vax780_fload (int32 flag, CONST char *cptr)
{
char gbuf[CBUFSIZE];
uint16 file_name[3], blkbuf[BLK_SIZE];
t_stat r;
uint32 i, j, start, size, origin;

if ((fl_unit.flags & UNIT_ATT) == 0)                    /* floppy attached? */
    return SCPE_UNATT;
if (*cptr == 0)
    return SCPE_2FARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get file name */
if (!rtfile_parse (gbuf, file_name))                    /* legal file name? */
    return SCPE_ARG;
if ((size = rtfile_lookup (file_name, &start)) == 0)    /* file on floppy? */
    return SCPE_ARG;
if (*cptr) {                                            /* origin? */
    origin = (uint32) get_uint (cptr, 16, MEMSIZE, &r);
    if ((r != SCPE_OK) || (origin & 1))                 /* must be even */
        return SCPE_ARG;
    }
else origin = 512;                                      /* no, use default */

for (i = 0; i < size; i++) {                            /* loop thru blocks */
    if (!rtfile_read (start + i, 1, blkbuf))            /* read block */
        return SCPE_FMT;
    for (j = 0; j < BLK_SIZE; j++) {                    /* loop thru words */
        if (ADDR_IS_MEM (origin))
            WriteW (origin, blkbuf[j]);
        else return SCPE_NXM;
        origin = origin + 2;
        }
    }
return SCPE_OK;
}

/* Parse an RT11 file name and convert it to radix-50 */

t_bool rtfile_parse (char *pntr, uint16 *file_name)
{
char c;
uint16 d;
uint32 i, j;

file_name[0] = file_name[1] = file_name[2] = 0;         /* zero file name */
for (i = 0; i < 2; i++) {                               /* 6 characters */
    for (j = 0; j < 3; j++) {
        c = *pntr;
        if ((c == '.') || (c == 0))                     /* fill if . or end */
            d = 0;
        else {
            if ((d = rtfile_ator50 (c)) == 0)
                return FALSE;
            pntr++;
            }
        file_name[i] = (file_name[i] * 050) + d;        /* merge into name */
        }
    }
if (file_name[0] == 0)                                  /* no name? lose */
    return FALSE;
while ((c = *pntr++) != '.') {                          /* scan for . */
    if (c == 0)                                         /* end? done */
        return TRUE;
    }
for (i = 0; i < 3; i++) {                               /* 3 characters */
    c = *pntr;
    if (c == 0)                                         /* fill if end */
        d = 0;
    else {
        if ((d = rtfile_ator50 (c)) == 0)
            return FALSE;
        pntr++;
        }
    file_name[2] = (file_name[2] * 050) + d;            /* merge into ext */
    }
return TRUE;
}

/* ASCII to radix-50 conversion */

uint32 rtfile_ator50 (uint32 ascii)
{
static const char *r50 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$._0123456789";
const char *fptr;

ascii = toupper (ascii);
if ((fptr = strchr (r50, toupper (ascii))) != NULL)
    return ((uint32) (fptr - r50));
else return 0;
}

/* Lookup an RT11 file name in the directory */

uint32 rtfile_lookup (uint16 *file_name, uint32 *start)
{
uint16 dirseg[DS_SIZE];
uint32 segnum, dirent;

for (segnum = 1; (segnum != 0) && (segnum <= DS_MAX);   /* loop thru segments */
     segnum = dirseg[DS_NEXT]) {
    if (!rtfile_read ((segnum * 2) + 4, 2, dirseg))     /* read segment */
        return 0;                                       /* error? */
    *start = dirseg[DS_FIRST];                          /* init file start */
    for (dirent = DS_ENTRIES;                           /* loop thru entries */
         (dirent < DS_SIZE) &&
         (DE_GET_STAT (dirseg[dirent + DE_STATUS]) != ENDSEG);
         dirent += DE_SIZE + (dirseg[DS_EXTRA] / 2)) {
        if ((DE_GET_STAT (dirseg[dirent + DE_STATUS]) == PERM) &&
            (dirseg[dirent + DE_NAME + 0] == file_name[0]) &&
            (dirseg[dirent + DE_NAME + 1] == file_name[1]) &&
            (dirseg[dirent + DE_NAME + 2] == file_name[2]))
            return dirseg[dirent + DE_FLNT];
        *start += dirseg[dirent + DE_FLNT];             /* incr file start */
        }
    }
return 0;
}

/* Read blocks */

t_stat rtfile_read (uint32 block, uint32 count, uint16 *buffer)
{
uint32 i, j;
uint32 pos;
uint8 *fbuf = (uint8 *)fl_unit.filebuf;

for (; count > 0; count--, block++) {
    for (i = 0; i < 4; i++) {                           /* 4 sectors/block */
        pos = rtfile_find (block, i);                   /* position */
        if ((pos + 128) >= (uint32) fl_unit.capac)      /* off end of disk? */
            return FALSE;
        for (j = 0; j < 128; j = j + 2)                 /* copy 128 bytes */
            *buffer++ = (((uint16) fbuf[pos + j + 1]) << 8) |
                ((uint16) fbuf[pos + j]);
        }
    }
return TRUE;
}

/* Map an RT-11 block number to a physical byte number */

uint32 rtfile_find (uint32 block, uint32 sector)
{
uint32 ls, lt, pt, ps;
uint32 off, bb;

/* get logical block, track & sector */

bb = (block * 4) + sector;

lt = bb / BPT;
ls = bb % BPT;

/* logic from 4.3BSD rx.c
 * calculate phys track & sector
 * 2:1 skew, 6 sector skew for each track
 */

pt = lt + TRACK_OFFSET;
ps = ((ls * SECTOR_SKEW) + (ls / (BPT / SECTOR_SKEW)) + (TRACK_SKEW * lt)) % BPT;

/* byte offset in logical disk */

off = (pt * BPT + ps) * 128;
return off;
}
