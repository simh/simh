/* This program converts a gt7 magtape dump to a SIMH magtape

   Copyright (c) 2002, Robert M. Supnik

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
unsigned char fzero[4] = { 0 };

int dump_rec (FILE *of, int bc, char *buf)
{
unsigned char buc[4];

if (((bc == 1) && (buf[0] == 0xF)) ||
    ((bc == 2) && (buf[0] == 0xF) && (buf[1] == 0xF))) {
	fwrite (fzero, sizeof (char), 4, of);
	return 1;  }
buc[0] = bc & 0xFF;
buc[1] = (bc >> 8) & 0xFF;
buc[2] = (bc >> 16) & 0xFF;
buc[3] = (bc >> 24) & 0xFF;
fwrite (buc, sizeof (char), 4, of);
fwrite (buf, sizeof (char), (bc + 1) & ~1, of);
fwrite (buc, sizeof (char), 4, of);
return 0;
}

int main (int argc, char *argv[])
{
int i, ch, bc, rc, fc;
unsigned char buf[FLPSIZ];
char *ppos, oname[256];
FILE *ifile, *ofile;

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: verb file [file...]\n");
	exit (0);  }

for (i = 1; i < argc; i++) {
	strcpy (oname, argv[i]);
        if (ppos = strrchr (oname, '.')) strcpy (ppos, ".tap");
            else strcat (oname, ".tap");
	ifile = fopen (argv[i], "rb");
	if (ifile == NULL) {
	    printf ("Error opening file: %s\n", argv[i]);
	    exit (0);  }
	ofile = fopen (oname, "wb");
	if (ofile == NULL) {
	    printf ("Error opening file: %s\n", oname);
	    exit (0);  }

	printf ("Processing file %s\n", argv[i]);
	for (bc = rc = fc = 0;;) {
	    ch = fgetc (ifile);
	    if (ch == EOF) break;
	    if (ch & 0x80) {
		if (bc) {
		    if (dump_rec (ofile, bc, buf))
			printf ("End of file %d\n", ++fc);
		    else printf ("Record %d size %d\n", ++rc, bc);
		    }
		bc = 0;  }
	    buf[bc++] = ch & 0x3F;
	    }
	fclose (ifile);
	if (bc) dump_rec (ofile, bc, buf);
	fwrite (fzero, sizeof (char), 4, ofile);
	printf ("End of file %d\n", ++fc);
	fclose (ofile);	
	}

return 0;
}
