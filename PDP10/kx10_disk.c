/* kx10_disk.c: Disk translator.

   Copyright (c) 2020, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "kx10_defs.h"
#include "kx10_disk.h"

/*
 *  SIMH format is number words per sector stored as a 64 bit word.
 *
 *  DBD9 format is:   9 character per pair of words.
 *
 *      0 - B0  1  2  3  4  5  6  7
 *      0 -  8  9 10 11 12 13 14 15
 *      0 - 16 17 18 19 20 21 22 23
 *      0 - 24 25 26 27 28 29 30 31
 *      0 - 32 33 34 35 B0  1  2  3
 *      1 -  4  5  6  7  8  9 10 11
 *      1 - 12 13 14 15 16 17 18 19
 *      1 - 20 21 22 23 24 25 26 27
 *      1 - 28 29 30 31 32 33 34 35
 *
 *
 *  DLD9 format is:   9 character per pair of words.
 *
 *      0 - 28 29 30 31 32 33 34 35
 *      0 - 20 21 22 23 24 25 26 27
 *      0 - 12 13 14 15 16 17 18 19
 *      0 -  4  5  6  7  8  9 10 11
 *      0 - 32 33 34 35 B0  1  2  3
 *      1 - 24 25 26 27 28 29 30 31
 *      1 - 16 17 18 19 20 21 22 23
 *      1 -  8  9 10 11 12 13 14 15
 *      1 - B0  1  2  3  4  5  6  7
 */


struct disk_formats {
    int         mode;
    const char  *name;
};

static struct disk_formats fmts[] = {
    {SIMH,  "SIMH"},
    {DBD9,  "DBD9"},
    {DLD9,  "DLD9"},
    {0, 0},
};

t_stat 
disk_read(UNIT *uptr, uint64 *buffer, int sector, int wps)
{
    int      da;
    int      wc;
    int      bc;
    int      wp;
    uint64   temp;
    uint8    conv_buff[2048];
    switch(GET_FMT(uptr->flags)) {
    case SIMH:
            da = sector * wps;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            wc = sim_fread (buffer, sizeof(uint64), wps, uptr->fileref);
            while (wc < wps)
                buffer[wc++] = 0;
            break;
    case DBD9:
            bc = (wps / 2) * 9;
            da = sector * bc;
            (void)sim_fseek(uptr->fileref, da, SEEK_SET);
            wc = sim_fread (&conv_buff, 1, bc, uptr->fileref);
            while (wc < bc)
                 conv_buff[wc++] = 0;
            for (wp = wc = 0; wp < wps;) {
                temp = ((uint64)conv_buff[wc++]) << 28;
                temp |= ((uint64)conv_buff[wc++]) << 20;
                temp |= ((uint64)conv_buff[wc++]) << 12;
                temp |= ((uint64)conv_buff[wc++]) << 4;
                temp |= ((uint64)conv_buff[wc]) >> 4;
                buffer[wp++] = temp;
                temp = ((uint64)conv_buff[wc++] & 0xf) << 32;
                temp |= ((uint64)conv_buff[wc++]) << 24;
                temp |= ((uint64)conv_buff[wc++]) << 16;
                temp |= ((uint64)conv_buff[wc++]) << 8;
                temp |= ((uint64)conv_buff[wc++]);
                buffer[wp++] = temp;
            }
            break;

    case DLD9:
            bc = (wps / 2) * 9;
            da = sector * bc;
            (void)sim_fseek(uptr->fileref, da, SEEK_SET);
            wc = sim_fread (&conv_buff, 1, bc, uptr->fileref);
            while (wc < bc)
                 conv_buff[wc++] = 0;
            for (wp = wc = 0; wp < wps;) {
                temp = ((uint64)conv_buff[wc++]);
                temp |= ((uint64)conv_buff[wc++]) << 8;
                temp |= ((uint64)conv_buff[wc++]) << 16;
                temp |= ((uint64)conv_buff[wc++]) << 24;
                temp |= ((uint64)conv_buff[wc] & 0xf) << 32;
                buffer[wp++] = temp;
                temp = ((uint64)conv_buff[wc++] & 0xf0) >> 4;
                temp |= ((uint64)conv_buff[wc++]) << 4;
                temp |= ((uint64)conv_buff[wc++]) << 12;
                temp |= ((uint64)conv_buff[wc++]) << 20;
                temp |= ((uint64)conv_buff[wc++]) << 28;
                buffer[wp++] = temp;
            }
            break;
     }
     return SCPE_OK;
}

t_stat
disk_write(UNIT *uptr, uint64 *buffer, int sector, int wps)
{
    int      da;
    int      wc;
    int      bc;
    int      wp;
    uint64   temp;
    uint8    conv_buff[2048];
    switch(GET_FMT(uptr->flags)) {
    case SIMH:
            da = sector * wps;
            (void)sim_fseek(uptr->fileref, da * sizeof(uint64), SEEK_SET);
            wc = sim_fwrite (buffer, sizeof(uint64), wps, uptr->fileref);
            break;
    case DBD9:
            bc = (wps / 2) * 9;
            for (wp = wc = 0; wp < wps;) {
                temp = buffer[wp++];
                conv_buff[wc++] = (uint8)((temp >> 28) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 20) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 12) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 4) & 0xff);
                conv_buff[wc] = (uint8)((temp & 0xf) << 4);
                temp = buffer[wp++];
                conv_buff[wc++] |= (uint8)((temp >> 32) & 0xf);
                conv_buff[wc++] = (uint8)((temp >> 24) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 16) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 8) & 0xff);
                conv_buff[wc++] = (uint8)(temp & 0xff);
            }
            da = sector * bc;
            (void)sim_fseek(uptr->fileref, da, SEEK_SET);
            wc = sim_fwrite (&conv_buff, 1, bc, uptr->fileref);
            return SCPE_OK;
    case DLD9:
            bc = (wps / 2) * 9;
            for (wp = wc = 0; wp < wps;) {
                temp = buffer[wp++];
                conv_buff[wc++] = (uint8)(temp & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 8) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 16) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 24) & 0xff);
                conv_buff[wc] = (uint8)((temp >> 32)  & 0xf);
                temp = buffer[wp++];
                conv_buff[wc++] |= (uint8)((temp << 4) & 0xf0);
                conv_buff[wc++] = (uint8)((temp >> 4) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 12) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 20) & 0xff);
                conv_buff[wc++] = (uint8)((temp >> 28) & 0xff);
            }
            da = sector * bc;
            (void)sim_fseek(uptr->fileref, da, SEEK_SET);
            wc = sim_fwrite (&conv_buff, 1, bc, uptr->fileref);
            return SCPE_OK;
    }
    return SCPE_OK;
}


/* Set disk format */
t_stat disk_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int f;

    if (uptr == NULL) return SCPE_IERR;
    if (cptr == NULL) return SCPE_ARG;
    for (f = 0; fmts[f].name != 0; f++) {
        if (strcmp (cptr, fmts[f].name) == 0) {
            uptr->flags &= ~UNIT_FMT;
            uptr->flags |= SET_FMT(fmts[f].mode);
            return SCPE_OK;
            }
        }
    return SCPE_ARG;
}

/* Show disk format */

t_stat disk_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int fmt = GET_FMT(uptr->flags);
    int f;

    for (f = 0; fmts[f].name != 0; f++) {
        if (fmt == fmts[f].mode) {
            fprintf (st, "%s format", fmts[f].name);
            return SCPE_OK;
        }
    }
    fprintf (st, "invalid format");
    return SCPE_OK;
}


/* Device attach */
t_stat disk_attach (UNIT *uptr, CONST char *cptr)
{
    t_stat r;
    char                 gbuf[30];

    /* Reset to SIMH format on attach */
    uptr->flags &= ~UNIT_FMT;
    /* Pickup optional format specifier during RESTORE */
    cptr = get_sim_sw (cptr);                 
    if (sim_switches & SWMASK ('F')) {        /* format spec? */
        cptr = get_glyph (cptr, gbuf, 0);     /* get spec */
        if (*cptr == 0) return SCPE_2FARG;    /* must be more */
        if (disk_set_fmt (uptr, 0, gbuf, NULL) != SCPE_OK)
            return SCPE_ARG;
    }

    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK)
        return r;
    return SCPE_OK;
}

/* Device detach */

t_stat disk_detach (UNIT *uptr)
{
    return detach_unit (uptr);
}

t_stat disk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "%s Disk Attach Help\n\n", dptr->name);
    
    fprintf (st, "Disk container files can be one of 3 different types:\n\n");
    fprintf (st, "    SIMH   A disk is an unstructured binary file of 64bit integers\n");
    fprintf (st, "    DBD9   Compatible with KLH10 is a packed big endian word\n");
    fprintf (st, "    DLD9   Compatible with KLH10 is a packed little endian word\n");
    
    if (dptr->numunits > 1) {
        uint32 i;
    
        for (i=0; (i < dptr->numunits); ++i)
            if ((dptr->units[i].flags & UNIT_ATTABLE) &&
                !(dptr->units[i].flags & UNIT_DIS)) {
                fprintf (st, "  sim> ATTACH {switches} %s%d diskfile\n", dptr->name, i);
                }
        }
    else
        fprintf (st, "  sim> ATTACH {switches} %s diskfile\n", dptr->name);
    fprintf (st, "\n%s attach command switches\n", dptr->name);
    fprintf (st, "    -R          Attach Read Only.\n");
    fprintf (st, "    -E          Must Exist (if not specified an attempt to create the indicated\n");
    fprintf (st, "                disk container will be attempted).\n");
    fprintf (st, "    -F          Open the indicated disk container in a specific format (default\n");
    fprintf (st, "                is SIMH), other options are DBD9 and DLD9\n");
    fprintf (st, "    -Y          Answer Yes to prompt to overwrite last track (on disk create)\n");
    fprintf (st, "    -N          Answer No to prompt to overwrite last track (on disk create)\n");
    return SCPE_OK;
}
