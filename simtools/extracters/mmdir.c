/* This program dumps the directory of a simulated Interdata MDM tape

   Copyright (c) 1993-2002, Robert M. Supnik

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
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

int main (int argc, char *argv[])
{
int obj, i, k, fc, rc, tpos, sa, ea, fr, fq;
unsigned char b[53];
unsigned char bca[4];
unsigned int bc;
int preveof;
FILE *ifile;
#define MAXRLNT 65536

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: verb file [file...]\n");
	exit (0);  }

for (i = 1; i < argc; i++) {
	ifile = fopen (argv[i], "rb");
	if (ifile == NULL) {
	    printf ("Error opening file: %s\n", argv[i]);
	    return 0;  }
	printf ("Processing input file %s\n", argv[i]);
	tpos = 0; rc = 1; fc = 0; preveof = 0;
	for (;;) {
	    fseek (ifile, tpos, SEEK_SET);
	    k = fread (bca, 1, 4, ifile);
	    bc = (((unsigned int) bca[3]) << 24) | (((unsigned int) bca[2]) << 16) |
		(((unsigned int) bca[1]) << 8) | ((unsigned int) bca[0]);
	    if ((k == 0) || (bc == 0xFFFFFFFF)) {
		printf ("End of physical tape\n");
		break;  }
	    if (bc & 0x80000000) {
		printf ("Error marker at record %d\n", rc);
		bc = bc & ~0x80000000;  }
	    if (bc == 0) {
		if (preveof) {
		    printf ("End of logical tape\n");
		    break;  }
		preveof = 1;
		fc++; obj++;
		rc = 1;
		tpos = tpos + 4;  }
	    else if (bc > MAXRLNT) {
		printf ("Invalid record length %d, terminating\n", bc);
		break;  }
	    else {
		tpos = tpos + 8 + ((bc + 1) & ~1);
		preveof = 0;
		if (fc && (rc == 1)) {
		    if (bc != 52) {
			printf ("Invalid record length %d, terminating\n", bc);
			break;  }
		    fread (b, 1, 52, ifile);
		    sa = (((unsigned int) b[18]) << 16) |
			(((unsigned int) b[19]) << 8) | ((unsigned int) b[20]);
		    ea = (((unsigned int) b[21]) << 16) |
			(((unsigned int) b[22]) << 8) | ((unsigned int) b[23]);
		    fr = b[27] >> 4;
		    fq = b[27] & 0xF;
		    printf ("%3d %c%c%c 06-%c%c%c", fc,
			b[0], b[1], b[2], b[3], b[4], b[5]);
		    if (fr) printf ("F0%X", fr);
		    else printf ("   ");
		    printf ("R%c%c%c%c %c%c  ",
			b[6], b[7], b[25], b[26], b[28], b[29]);
		    b[18] = b[51] = b[52] = 0;
		    printf ("%s%s  %06X %06X %X\n",
			&b[8], &b[30], sa, ea, fq);
		    }
		rc++;  }
	    }
	fclose (ifile);
	}
return 0;
}
