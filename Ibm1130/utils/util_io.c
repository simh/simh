// util_io - I/O routines from simh package -- "endian-independent"
// borrowed from scp.c, with this copyright notice:

// use fxread and fxwrite instead of fread and fwrite

/*
   Copyright (c) 1993-2002, Robert M Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.
*/

/* Endian independent binary I/O package

   For consistency, all binary data read and written by the simulator
   is stored in little endian data order.  That is, in a multi-byte
   data item, the bytes are written out right to left, low order byte
   to high order byte.  On a big endian host, data is read and written
   from high byte to low byte.  Consequently, data written on a little
   endian system must be byte reversed to be usable on a big endian
   system, and vice versa.

   These routines are analogs of the standard C runtime routines
   fread and fwrite.  If the host is little endian, or the data items
   are size char, then the calls are passed directly to fread or
   fwrite.  Otherwise, these routines perform the necessary byte swaps
   using an intermediate buffer.
*/

#include <stdio.h>
#include "util_io.h"

#define int32       int
#define FLIP_SIZE   1024            /* make the flip buffer smaller for these utilities */

static int sim_end = 1;         /* 1 = little-endian */
static unsigned char sim_flip[FLIP_SIZE];
static int end_tested = 0;

void util_io_init (void)
{
    union {int32 i; char c[sizeof (int32)]; } end_test;

    end_test.i = 1;                     /* test endian-ness */
    sim_end    = end_test.c[0];
    end_tested = 1;
}

size_t fxread (void *bptr, size_t size, size_t count, FILE *fptr)
{
    size_t c, j, nelem, nbuf, lcnt, total;
    int32 i, k;
    unsigned char *sptr, *dptr;

    if (! end_tested)
        util_io_init();

    if (sim_end || (size == sizeof(char)))
        return fread(bptr, size, count, fptr);

    if ((size == 0) || (count == 0))
        return 0;

    nelem = FLIP_SIZE / size;               /* elements in buffer */
    nbuf  = count / nelem;                  /* number buffers */
    lcnt  = count % nelem;                  /* count in last buf */

    if (lcnt)
        ++nbuf;
    else
        lcnt = nelem;

    total = 0;
    dptr  = bptr;                       /* init output ptr */

    for (i = nbuf; i > 0; i--) {
        if ((c = fread(sim_flip, size, (i == 1) ? lcnt : nelem, fptr)) == 0)
            return total;

        total += c;

        for (j = 0, sptr = sim_flip; j < c; j++) {
            for (k = size - 1; k >= 0; k--)
                *(dptr + k) = *sptr++;

            dptr += size;
        }
    }

    return total;
}

size_t fxwrite (void *bptr, size_t size, size_t count, FILE *fptr)
{
    size_t c, j, nelem, nbuf, lcnt, total;
    int32 i, k;
    unsigned char *sptr, *dptr;

    if (! end_tested)
        util_io_init();

    if (sim_end || (size == sizeof(char)))
        return fwrite(bptr, size, count, fptr);

    if ((size == 0) || (count == 0))
        return 0;

    nelem = FLIP_SIZE / size;               /* elements in buffer */
    nbuf  = count / nelem;                  /* number buffers */
    lcnt  = count % nelem;                  /* count in last buf */

    if (lcnt)
        ++nbuf;
    else
        lcnt = nelem;

    total = 0;
    sptr  = bptr;                           /* init input ptr */

    for (i = nbuf; i > 0; i--) {
        c = (i == 1) ? lcnt : nelem;

        for (j = 0, dptr = sim_flip; j < c; j++) {
            for (k = size - 1; k >= 0; k--)
                *(dptr + k) = *sptr++;

            dptr += size;
        }

        if ((c = fwrite(sim_flip, size, c, fptr)) == 0)
            return total;

        total += c;
    }

    return total;
}
