/*
 * formatdisk.c - A utility to produce blank BESM-6 disk images.
 *
 * Copyright (c) 2014 Leonid Broukhis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * SERGE VAKULENKO OR LEONID BROUKHIS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE 
 * OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Leonid Broukhis or
 * Serge Vakulenko shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from Leonid Broukhis and Serge Vakulenko.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "besm6_defs.h"

#define TOTBLK 01767

int main(int argc, char ** argv) {
    t_value control[4];  /* block (zone) number, key, userid, checksum */
    int diskno, blkno, word;

    if (argc != 2 || (diskno = atoi(argv[1])) < 2048 || diskno > 4095) {
        fprintf(stderr, "Usage: formatdisk NNNN > diskNNNN.bin, where 2048 <= NNNN <= 4095\n");
        exit(1);
    }

    control[1] = SET_CONVOL(0, CONVOL_NUMBER);
    control[2] = SET_CONVOL(0, CONVOL_NUMBER);
    control[3] = SET_CONVOL(0, CONVOL_NUMBER);

    control[1] |= 01370707LL << 24;    /* Magic */
    control[1] |= diskno << 12;

    for (blkno = 0; blkno < TOTBLK; ++blkno) {
        control[0] = SET_CONVOL((t_value)(2*blkno) << 36, CONVOL_NUMBER);
        fwrite(control, sizeof(t_value), 4, stdout);
        control[0] = SET_CONVOL((t_value)(2*blkno+1) << 36, CONVOL_NUMBER);
        fwrite(control, sizeof(t_value), 4, stdout);
        for (word = 0; word < 02000; ++word) {
            fwrite(control+2, sizeof(t_value), 1, stdout);
        }
    }
    exit(0);
}
