/* This program fixes a SIMH magtape containing a misread EOF

   Copyright (c) 2003, Robert M. Supnik

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
#define FLPSIZ 65536
int main (int argc, char *argv[])
{
int i, fc, rc;
unsigned int k, tbc;
unsigned char bc[4] = { 0 };
unsigned char bceof[4] = { 0 };
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
	fc = 1; rc = 0;
	for (;;) {
	    k = fread (bc, sizeof (char), 4, ifile);
	    if (k == 0) break;
	    tbc = ((unsigned int) bc[3] << 24) | ((unsigned int) bc[2] << 16) |
		((unsigned int) bc[1] << 8) | (unsigned int) bc[0];
	    if (tbc) {
		printf ("Record size = %d\n", tbc);
		if (tbc > FLPSIZ) {
		    printf ("Record too big\n");
		    return 0;  }
		k = fread (buf, sizeof (char), tbc, ifile);
		for ( ; k < tbc; k++) buf[k] = 0;
		fread (bc, sizeof (char), 4, ifile);
		if (tbc > 1) {
		    fwrite (bc, sizeof (char), 4, ofile);
		    fwrite (buf, sizeof (char), tbc, ofile);
		    fwrite (bc, sizeof (char), 4, ofile);
		    rc++;  }
		else {
		    printf ("Record length = 1, ignored\n");
		    }  }
	    else {
		fwrite (bceof, sizeof (char), 4, ofile);
		if (rc) printf ("End of file %d, record count = %d\n", fc, rc);
		else printf ("End of tape\n");
		fc++;
		rc = 0;  }
	    }
	fclose (ifile);
	fclose (ofile);
	}

return 0;
}
