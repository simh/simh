/* Functions to convert RAD50 to or from ASCII. */

/*
Copyright (c) 2001, Richard Krehbiel
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

o Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

o Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

o Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rad50.h"

static char radtbl[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$. 0123456789";

/* rad50 converts from 0 to 3 ASCII (or EBCDIC, if your compiler is so
   inclined) characters into a RAD50 word. */

unsigned rad50(char *cp, char **endp)
{
	unsigned long acc = 0;
	char *rp;

	if(endp)
		*endp = cp;

	if(!*cp)					/* Got to check for end-of-string
								   manually, because strchr will call
								   it a hit.  :-/ */
		return acc;

	rp = strchr(radtbl, toupper(*cp));
	if(rp == NULL)				/* Not a RAD50 character */
		return acc;
	acc = (rp - radtbl) * 03100; /* Convert */
	cp++;

	/* Now, do the same thing two more times... */

	if(endp)
		*endp = cp;
	if(!*cp)
		return acc;
	rp = strchr(radtbl, toupper(*cp));
	if(rp == NULL)
		return acc;
	acc += (rp - radtbl) * 050;

	cp++;
	if(endp)
		*endp = cp;
	if(!*cp)
		return acc;
	rp = strchr(radtbl, toupper(*cp));
	if(rp == NULL)
		return acc;
	acc += (rp - radtbl);

	cp++;
	if(endp)
		*endp = cp;

	return acc;					/* Done. */
}

/* rad50x2 - converts from 0 to 6 characters into two words of RAD50. */

void rad50x2(char *cp, unsigned *rp)
{
	*rp++ = rad50(cp, &cp);
	*rp = 0;
	if(*cp)
		*rp = rad50(cp, &cp);
}

/* unrad50 - converts a RAD50 word to three characters of ASCII. */

void unrad50(unsigned word, char *cp)
{
	if(word < 0175000)			/* Is it legal RAD50? */
	{
		cp[0] = radtbl[word / 03100];
		cp[1] = radtbl[(word / 050) % 050];
		cp[2] = radtbl[word % 050];
	}
	else
		cp[0] = cp[1] = cp[2] = ' ';
}
