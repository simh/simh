/* This program dumps the format of a simulated magtape

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
#include <errno.h>
#include <limits.h>

#define F_STD	0
#define F_E11	1
#define F_TPC	2
#define F_TPF	3

int main (int argc, char *argv[])
{
int obj, i, k, fc, rc, tpos;
unsigned int bc, fmt, rlntsiz;
unsigned char *s, bca[4];
int preveof;
FILE *ifile;
#define MAXRLNT 65536

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: mtdump {-secf} file [file...]\n");
	exit (0);  }

s = argv[1];
if ((s != NULL) && (*s++ == '-')) {
	++argv; --argc;
	switch (*s) {
 	case 'S': case 's':
	    fmt = F_STD; rlntsiz = 4; break;
	case 'E': case 'e':
	    fmt = F_E11; rlntsiz = 4; break;
        case 'C': case 'c':
	    fmt = F_TPC; rlntsiz = 2; break;
/*	case 'F': case 'f':
/*	    fmt = F_TPF; break; */
	default:
	    fprintf (stderr, "Bad option %c\n", *s);
	    return 0;
	    }
	}
else {	fmt = F_STD;
	rlntsiz = 4;  }

for (i = 1; i < argc; i++) {
	ifile = fopen (argv[i], "rb");
	if (ifile == NULL) {
	    printf ("Error opening file: %s\n", argv[i]);
	    exit (0);  }
	printf ("Processing input file %s\n", argv[i]);
	tpos = 0; rc = 1; fc = 1; preveof = 0;
	printf ("Processing tape file %d\n", fc);
	for (obj = 1;;) {
	    fseek (ifile, tpos, SEEK_SET);
	    k = fread (bca, 1, rlntsiz, ifile);
	    switch (fmt) {
	    case F_STD: case F_E11:
		bc = (((unsigned int) bca[3]) << 24) | (((unsigned int) bca[2]) << 16) |
		    (((unsigned int) bca[1]) << 8) | ((unsigned int) bca[0]);
		break;
	    case F_TPC:
		bc = (((unsigned int) bca[1]) << 8) | ((unsigned int) bca[0]);
	        break;
		}
	    if ((k == 0) || (bc == 0xFFFFFFFF)) {
		printf ("End of physical tape\n");
		break;  }
	    if (bc & 0x80000000) {
		printf ("Error marker at record %d\n", rc);
		bc = bc & ~0x80000000;  }
	    if (bc == 0) {
		if (preveof) {
		    printf ("Obj %d, position %d, end of logical tape\n", obj, tpos);
		    break;  }
		preveof = 1;
		printf ("Obj %d, position %d, end of tape file %d\n", obj, tpos, fc);
		fc++; obj++;
		rc = 1;
		tpos = tpos + rlntsiz;  }
	    else if (bc > MAXRLNT) {
		printf ("Invalid record length %d, terminating dump\n", bc);
		break;  }
	    else {
		if (preveof) printf ("Processing tape file %d\n", fc);
		preveof = 0;
		printf ("Obj %d, position %d, record %d, length = %d (0x%X)\n",
		    obj, tpos, rc, bc, bc);
		rc++; obj++;
		switch (fmt) {
		case F_STD:
		    tpos = tpos + 8 + ((bc + 1) & ~1); break;
		case F_E11:
		    tpos = tpos + 8 + bc; break;
		case F_TPC:
		    tpos = tpos + 2 + ((bc + 1) & ~1); break;
		    }
		}
	    }
	fclose (ifile);
	}

return 0;
}
