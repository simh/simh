/* This program dumps the format of an SDS paper tape

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

int getfc (FILE *ifile)
{
int k;
unsigned char by;

k = fread (&by, sizeof (char), 1, ifile);
if (k == 0) {
	printf ("End of physical tape\n");
	return -1;  }
return by;
}

int main (int argc, char *argv[])
{
int i, k, wc, pos, cc, inrec, wd, op, tag, addr;
FILE *ifile;
char *opstr[] = {
	"HLT", "BRU", "EOM", NULL,  NULL,  NULL,  "EOD", NULL,
	"MIY", "BRI", "MIW", "POT", "ETR", NULL,  "MRG", "EOR",
	"NOP", NULL,  "OVF", "EXU", NULL,  NULL,  NULL,  NULL,
	"YIM", NULL,  "WIM", "PIN", NULL,  "STA", "STB", "STX",
	"SKS", "BRX", NULL,  "BRM", NULL,  NULL,  "CPY", NULL,
	"SKE", "BRR", "SKB", "SKN", "SUB", "ADD", "SUC", "ADC",
	"SKR", "MIN", "XMA", "ADM", "MUL", "DIV", "RSH", "LSH",
	"SKM", "LDX", "SKA", "SKG", "SKD", "LDB", "LDA", "EAX" };

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: verb file [file...]\n");
	exit (0);  }

for (i = 1; i < argc; i++) {
	ifile = fopen (argv[i], "rb");
	if (ifile == NULL) {
		printf ("Error opening file: %s\n", argv[i]);
		exit (0);  }
	printf ("Processing input file %s\n", argv[i]);
	inrec = 0; wc = 1;
	for (pos = wd = cc = 0; ;pos++) {
		if ((k = getfc (ifile)) < 0) {
			if (inrec) printf ("Format error\n");
			break;  }
		if (k == 0) {
			if (inrec && cc) printf ("Incomplete word\n");
			inrec = 0;
			continue;  }
		wd = (wd << 6) | (k & 077);
		cc++;
		if (cc >= 4) {
			printf ("Pos = %d, cnt = %d: %08o", pos, wc, wd);
			tag = (wd >> 21) & 07;
			op = (wd >> 15) & 077;
			addr = wd & 037777;
			if (opstr[op] && ((op != 0) || (wd == 0))) {
				if (wd & 040000) printf (" [%s* %o", opstr[op], addr);
				else printf (" [%s %o", opstr[op], addr);
				if (tag) printf (",%o", tag);
				printf ("]");  }
			printf ("\n");
			cc = wd = 0;
			wc++;  }
		}
	fclose (ifile);
	}
return 0;
}
