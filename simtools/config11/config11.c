/* Program to configure the floating address space of a PDP-11 or VAX

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

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define RANK_LNT	34	

int csr, i, j;
unsigned int rank, num;
char *cp, *ocp, inp[100];
unsigned char numctl[RANK_LNT];
unsigned char modtab[RANK_LNT] = {
 0X07, 0X0f, 0X07, 0X07, 0X07, 0X07, 0X07, 0X07,
 0X07, 0X07, 0X07, 0X0f, 0X07, 0X07, 0X0f, 0X07,
 0X07, 0X07, 0X07, 0X07, 0X07, 0X07, 0X07, 0X0f,
 0X07, 0X03, 0X1f, 0X0f, 0X0f, 0X03, 0X0F, 0x0F,
 0x1F, 0X1F };
unsigned int fixtab[RANK_LNT] = {
 0, 0, 0, 0, 0, 0, 0, 0,
 0, 0, 0, 0, 0, 0774400, 0770460, 0,
 0, 0777170, 0, 0772410, 0, 0, 0, 0,
 0774440, 0772150, 0, 0, 0, 0774500, 0, 0,
 0, 0 };
char *namtab[RANK_LNT] = {
 "DJ11", "DH11", "DQ11", "DU11", "DUP11", "LK11A", "DMC11", "DZ11",
 "KMC11", "LPP11", "VMV21", "VMV31", "DWR70", "RL11", "LPA11K", "KW11C",
 "rsvd", "RX11", "DR11W", "DR11B", "DMP11", "DPV11", "ISB11", "DMV11",
 "DEUNA", "UDA50", "DMF32", "KMS11", "VS100", "TK50", "KMV11", "DHV11",
 "DMZ32", "CP132" };

int main (int argc, char *argv[])
{
for ( ;; ) {
    for (i = 0; i < RANK_LNT; i++) numctl[i] = 0;
    printf ("Enter configuration data\n");
    for ( ;; ) {
	printf ("Name:\t");
	if (gets (inp) == NULL) return 0;
	if (*inp == 0) break;
	for (cp = inp; *cp != 0; cp++) *cp = toupper (*cp);
	for (rank = 0; rank < RANK_LNT; rank++) {
	    if (strcmp (inp, namtab[rank]) == 0) break;  }
	if (rank >= RANK_LNT) {
	    printf ("Unknown controller, valid names are:");
	    for (i = 0; i < RANK_LNT; i++) {
	       if ((i & 07) == 0) printf ("\n");
	       printf (" %s", namtab[i]);  }
	    printf ("\n");
	    continue;  }
	printf ("Number:\t");
	gets (inp);
	errno = 0;
	num = strtoul (inp, &ocp, 10);
	if (errno || (inp == ocp)) {
	    printf ("Input error\n");
	    continue;  }
	if (num > 8) {
	    printf ("Too many controllers\n");
	    continue;  }
	numctl[rank] = num;
	}

    printf ("\nRank\tName\tCtrl#\t CSR\n\n");
    csr = 0760010;
    for (i = 0; i < RANK_LNT; i++) {
	if (numctl[i] == 0) {
	    printf (" %02d\t%s\tgap\t%06o\n", i+1, namtab[i], csr);  }
	else {
	    if (fixtab[i])
		printf (" %02d\t%s\t  1\t%06o*\n", i+1, namtab[i], fixtab[i]);
	    else {
		printf (" %02d\t%s\t  1\t%06o\n", i+1, namtab[i], csr);
		csr = (csr + modtab[i] + 1) & ~modtab[i];  }
	    for (j = 1; j < numctl[i]; j++) {
		printf ("\t\t  %d\t%06o\n", j + 1, csr);
		csr = (csr + modtab[i] + 1) & ~modtab[i];  }
	    printf (" %\t\tgap\t%06o\n", csr);
	    }
	if ((i + 1) < RANK_LNT) csr = (csr + modtab[i+1] + 1) & ~modtab[i+1];
	}
    printf ("\n\n");
    }
return 0;
}
