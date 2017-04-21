/* This program converts <cr> delimited files to Windoze <cr><lf>

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
#define LAST_ANY	0
#define LAST_CR		1
#define LAST_LF		2
#define MD_WIN		0
#define MD_UNIX		1
#define MD_MAC		2

void puteol (int mode, FILE *of)
{
if (mode != MD_UNIX) putc ('\r', of);
if (mode != MD_MAC) putc ('\n', of);
return;
}

int main (int argc, char *argv[])
{
int i, k, mc, lastc;
int mode;
char *s, *ppos, oname[256];
FILE *ifile, *ofile;

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: asc -muw file [file...]\n");
	exit (0);  }

s = argv[1];
if ((s != NULL) && (*s++ == '-')) {
	++argv; --argc;
	switch (*s) {
 	case 'm': case 'M':
	    mode = MD_MAC;  break;
	case 'u': case 'U':
	    mode = MD_UNIX; break;
        case 'w': case 'W':
	    mode = MD_WIN; break;
	default:
	    fprintf (stderr, "Bad option %c\n", *s);
	return 0;  }
	}
else mode = MD_WIN;

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
	for (lastc = LAST_ANY;;) {
	    k = getc (ifile);
	    if (k == EOF) break;
	    mc = k & 0177;
	    if (mc && (mc != 0177)) {
		if (mc == 015) {
		    if (lastc == LAST_CR) puteol (mode, ofile);
		    lastc = LAST_CR;  }
		else if (mc == 012) {
		    puteol (mode, ofile);
		    lastc = LAST_LF;  }
		else {
		    if (lastc == LAST_CR) puteol (mode, ofile);
		    putc (mc, ofile);
		    lastc = LAST_ANY;  }
		}
	    }
	if (lastc == LAST_CR) puteol (mode, ofile);
	fclose (ifile);
	fclose (ofile);
	}
return 0;
}
