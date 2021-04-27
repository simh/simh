/* kx10_disk.h: Disk translator.

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


/* Flags in the unit flags word */

#define UNIT_V_FMT      (UNIT_V_UF + 8)
#define UNIT_M_FMT      7
#define GET_FMT(x)      (((x) >> UNIT_V_FMT) & UNIT_M_FMT)
#define SET_FMT(x)      (((x) & UNIT_M_FMT) << UNIT_V_FMT)
#define UNIT_FMT        (UNIT_M_FMT << UNIT_V_FMT)

#define SIMH            0                /* Default raw uint64 word format */
#define DBD9            1                /* KLH10 Disb Big End Double */
#define DLD9            2                /* KLH10 Disb Little End Double */

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


t_stat disk_read(UNIT *uptr, uint64 *buffer, int sector, int wps);
t_stat disk_write(UNIT *uptr, uint64 *buffer, int sector, int wps);
/* Set disk format */
t_stat disk_set_fmt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
/* Show disk format */
t_stat disk_show_fmt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
/* Device attach */
t_stat disk_attach (UNIT *uptr, CONST char *cptr);
/* Device detach */
t_stat disk_detach (UNIT *uptr);
/* Print attach help */
t_stat disk_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
