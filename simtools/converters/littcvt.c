/* This program removes the 4 header bytes from a Litt tape

   Copyright (c) 1993-1999, Robert M. Supnik, Compaq Computer Corporation

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
   ROBERT M SUPNIK, OR COMPAQ COMPUTER CORPORATION, BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik, or Compaq
   Computer Corporation, shall not be used in advertising or otherwise to
   promote the sale, use or other dealings in this Software without prior
   written authorization from both the author and Compaq Computer Corporation.
*/

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define FLPSIZ 65536
int main (int argc, char *argv[])
{
int i, k, bc;
unsigned char buf[FLPSIZ];
char *ppos, oname[256];
FILE *ifile, *ofile;

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: verb file [file...]\n");
	exit (0);  }

for (i = 1; i < argc; i++) {
	strcpy (oname, argv[i]);
        if (ppos = strrchr (oname, '.')) strcpy (ppos, ".new");
                else strcat (oname, ".new");
	ifile = fopen (argv[i], "rb");
	if (ifile == NULL) {
		printf ("Error opening file: %s\n", argv[i]);
		exit (0);  }
	ofile = fopen (oname, "wb");
	if (ofile == NULL) {
		printf ("Error opening file: %s\n", oname);
		exit (0);  }

	printf ("Processing file %s\n", argv[i]);
	fread (&bc, sizeof (int), 1, ifile);
	for (;;) {
		k = fread (buf, sizeof (char), FLPSIZ, ifile);
		if (k == 0) break;
		fwrite (buf, sizeof (char), k, ofile);  }
	fclose (ifile);
	fclose (ofile);
}

exit (0);
}
