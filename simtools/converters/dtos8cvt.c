/* This program converts an OS8 DECtape image to simulator format

   Copyright (c) 1993-2001, Robert M. Supnik

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
#define BLKSIZ 129
int main (int argc, char *argv[])
{
int i, k;
unsigned short buf[BLKSIZ];
char *ppos, oname[256];
FILE *ifile, *ofile;

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: verb file [file...]\n");
	exit (0);  }

for (i = 1; i < argc; i++) {
	strcpy (oname, argv[i]);
        if (ppos = strrchr (oname, '.')) strcpy (ppos, ".dt8");
                else strcat (oname, ".dt8");
	ifile = fopen (argv[i], "rb");
	if (ifile == NULL) {
		printf ("Error opening file: %s\n", argv[i]);
		exit (0);  }
	ofile = fopen (oname, "wb");
	if (ofile == NULL) {
		printf ("Error opening file: %s\n", oname);
		exit (0);  }

	printf ("Processing file %s\n", argv[i]);
	for (;;) {
		k = fread (buf, sizeof (short), BLKSIZ - 1, ifile);
		if (k == 0) break;
		for ( ; k < BLKSIZ; k++) buf[k] = 0;
		fwrite (buf, sizeof (short), BLKSIZ, ofile);
		}
	fclose (ifile);
	fclose (ofile);
	}

return 0;
}
