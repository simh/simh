/* This program converts a Motorola S format PROM dump to a binary file

   Copyright (c) 1993-2003, Robert M. Supnik

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
#define HEX(a)	((a) >= 'A'? (a) - 'A' + 10: (a) - '0')
#define MAXA	(2 << 14)
#define MAXR	4

int main (int argc, char *argv[])
{
int i, d, d1, j, k, astrt, dstrt, addr, maxaddr[MAXR];
int numr, numf;
unsigned char data[MAXR][MAXA];
char *s, *ppos, *cptr, line[256], oname[256];
FILE *ifile, *ofile;

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: verb file [file...]\n");
	exit (0);  }

s = argv[1];
if ((s != NULL) && (*s++ == '-')) {
	++argv; --argc;
	switch (*s) {
 	case '1':
	    numr = 1; break;
	case '2':
	    numr = 2; break;
	case '4':
	    numr = 4; break;
	default:
	    fprintf (stderr, "Bad option %c\n", *s);
	return 0;  }
	}
else numr = 1;

for (i = 1, numf = 0; i < argc; i++) {
	ifile = fopen (argv[i], "r");
	if (ifile == NULL) {
		printf ("Error opening file: %s\n", argv[i]);
		exit (0);  }

	printf ("Processing file %s\n", argv[i]);
	astrt = 4;
	maxaddr[numf] = 0;
	for (;;) {
		cptr = fgets (line, 256, ifile);
		if (cptr == NULL) break;
		if (line[0] != 'S') continue;
		if (line[1] == '1') dstrt = 8;
		else if (line[1] == '2') dstrt = 10;
		else continue;
		for (k = astrt, addr = 0; k < dstrt; k++) {
			d = HEX (line[k]);
			addr = (addr << 4) + d;  }
		if (addr >= MAXA) {
			printf ("Address %o out of range\n", addr);
			break;  }
		for (k = dstrt; k < (dstrt + 32); k = k + 2, addr++) {
			d = HEX (line[k]);
			d1 = HEX (line[k+1]);
			data[numf][addr] = (d << 4) + d1;  }
		if (addr > maxaddr[numf]) maxaddr[numf] = addr;
		}
	fclose (ifile);
	numf++;
	if (numf >= numr) {
	    for (k = 0; k < numr; k++) {
		if (maxaddr[k] != maxaddr[0]) {
		    printf ("Rom lengths don't match, file 1 = %d, file %d = %d\n",
			maxaddr[0], k, maxaddr[k]);
		    return 0;  }  }
	    strcpy (oname, argv[i]);
            if (ppos = strrchr (oname, '.')) strcpy (ppos, ".bin");
            else strcat (oname, ".bin");
	    ofile = fopen (oname, "wb");
	    if (ofile == NULL) {
		printf ("Error opening file: %s\n", oname);
		exit (0);  }
	    printf ("Output file: %s, ROM size is %d\n", oname, maxaddr[0]);
	    for (k = 0; k < maxaddr[0]; k++) {
		for (j = numr - 1; j >= 0; j--) {
		    fwrite (&data[j][k], 1, 1, ofile);  }  }
	    fclose (ofile);
	    numf = 0;
	    }
	}
if (numf) printf ("Unprocessed files\n");
return 0;
}
