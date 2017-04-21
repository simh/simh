/* Dump and interpret an object file. */

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

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "rad50.h"

#include "util.h"

#define WORD(cp) ((*(cp) & 0xff) + ((*((cp)+1) & 0xff) << 8))

int psectid = 0;
char *psects[256];
FILE *bin = NULL;
int badbin = 0;
int xferad = 1;

char *readrec(FILE *fp, int *len)
{
	int c, i;
	int chksum;
	char *buf;

	chksum = 0;
	
	while(c = fgetc(fp), c != EOF && c == 0)
		;

	if(c == EOF)
		return NULL;

	if(c != 1)
	{
		fprintf(stderr, "Improperly formatted OBJ file (1)\n");
		return NULL;		// Not a properly formatted file.
	}

	chksum -= c;

	c = fgetc(fp);
	if(c != 0)
	{
		fprintf(stderr, "Improperly formatted OBJ file (2)\n");
		return NULL;		// Not properly formatted
	}

	chksum -= c;			// even though for 0 the checksum isn't changed...

	c = fgetc(fp);
	if(c == EOF)
	{
		fprintf(stderr, "Improperly formatted OBJ file (3)\n");
		return NULL;
	}
	*len = c;

	chksum -= c;

	c = fgetc(fp);
	if(c == EOF)
	{
		fprintf(stderr, "Improperly formatted OBJ file (4)\n");
		return NULL;
	}

	*len += (c << 8);

	chksum -= c;

	*len -= 4;				// Subtract header and length bytes from length
	if(*len < 0)
	{
		fprintf(stderr, "Improperly formatted OBJ file (5)\n");
		return NULL;
	}
	
	buf = malloc(*len);
	if(buf == NULL)
	{
		fprintf(stderr, "Out of memory allocating %d bytes\n", *len);
		return NULL;		// Bad alloc
	}

	i = fread(buf, 1, *len, fp);
	if(i < *len)
	{
		free(buf);
		fprintf(stderr, "Improperly formatted OBJ file (6)\n");
		return NULL;
	}

	for(i = 0; i < *len; i++)
	{
		chksum -= (buf[i] & 0xff);
	}

	c = fgetc(fp);
	c &= 0xff;
	chksum &= 0xff;

	if(c != chksum)
	{
		free(buf);
		fprintf(stderr, "Bad record checksum, "
				"calculated=%d, recorded=%d\n", chksum, c);
		return NULL;
	}

	return buf;
}

void dump_bytes(char *buf, int len)
{
	int i, j;

	for(i = 0; i < len; i += 8)
	{
		printf("\t%3.3o: ", i);
		for(j = i; j < len && j < i+8; j++)
		{
			printf("%3.3o ", buf[j] & 0xff);
		}

		printf("%*s", (i+8 - j) * 4, "");

		for(j = i; j < len && j < i+8; j++)
		{
			int c = buf[j] & 0xff;
			if(!isprint(c))
				c = '.';
			putchar(c);
		}

		putchar('\n');
	}
}

void dump_words(unsigned addr, char *buf, int len)
{
	int i, j;

	for(i = 0; i < len; i += 8)
	{
		printf("\t%6.6o: ", addr);

		for(j = i; j < len && j < i+8; j += 2)
		{
			if(len - j >= 2)
			{
				unsigned word = WORD(buf + j);
				printf("%6.6o ", word);
			}
			else
				printf("%3.3o    ", buf[j] & 0xff);
		}

		printf("%*s", (i+8 - j) * 7 / 2, "");

		for(j = i; j < len && j < i+8; j++)
		{
			int c = buf[j] & 0xff;
			if(!isprint(c))
				c = '.';
			putchar(c);
		}

		putchar('\n');
		addr += 8;
	}
}

void dump_bin(unsigned addr, char *buf, int len)
{
	int chksum;					/* Checksum is negative sum of all
								bytes including header and length */
	int FBR_LEAD1 = 1, FBR_LEAD2 = 0;
	int i;
	unsigned hdrlen = len + 6;

	for(i = 0; i < 8; i++) fputc (0, bin);
	chksum = 0;
	if(fputc(FBR_LEAD1, bin) == EOF) return;	/* All recs begin with 1,0 */
	chksum -= FBR_LEAD1;
	if(fputc(FBR_LEAD2, bin) == EOF) return;
	chksum -= FBR_LEAD2;
	
	i = hdrlen & 0xff;				/* length, lsb */
	chksum -= i;
	if(fputc(i, bin) == EOF) return;

	i = (hdrlen >> 8) & 0xff;		/* length, msb */
	chksum -= i;
	if(fputc(i, bin) == EOF) return;

	i = addr & 0xff;				/* origin, msb */
	chksum -= i;
	if(fputc(i, bin) == EOF) return;

	i = (addr >> 8) & 0xff;			/* origin, lsb */
	chksum -= i;
	if(fputc(i, bin) == EOF) return;

	if ((len == 0) || (buf == NULL)) return;	/* end of tape block */

	i = fwrite(buf, 1, len, bin);
	if(i < len) return;

	while(len > 0)				/* All the data bytes */
	{
		chksum -= *buf++ & 0xff;
		len--;
	}

	chksum &= 0xff;

	fputc(chksum, bin);			/* Followed by the checksum byte */

	return;					/* Worked okay. */
}

void trim(char *buf)
{
	char *cp;

	for(cp = buf + strlen(buf); cp > buf; cp--)
	{
		if(cp[-1] != ' ')
			break;
	}
	*cp = 0;
}

char **all_gsds = NULL;
int nr_gsds = 0;
int gsdsize = 0;

void add_gsdline(char *line)
{
	if(nr_gsds >= gsdsize || all_gsds == NULL)
	{
		gsdsize += 128;
		all_gsds = realloc(all_gsds, gsdsize * sizeof(char *));
		if(all_gsds == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}
	}

	all_gsds[nr_gsds++] = line;
}

void got_gsd(char *cp, int len)
{
	int i;
	char *gsdline;

	for(i = 2; i < len; i += 8)
	{
		char name[8];
		unsigned value;
		unsigned flags;

		gsdline = malloc(256);
		if(gsdline == NULL)
		{
			fprintf(stderr, "Out of memory\n");
			exit(EXIT_FAILURE);
		}

		unrad50(WORD(cp+i), name);
		unrad50(WORD(cp+i+2), name+3);
		name[6] = 0;

		value = WORD(cp+i+6);
		flags = cp[i+4] & 0xff;

		switch(cp[i+5] & 0xff)
		{
		case 0:
			sprintf(gsdline,
					 "\tMODNAME %s=%o flags=%o\n", name, value, flags);
			break;
		case 1:
			sprintf(gsdline,
					 "\tCSECT %s=%o flags=%o\n", name, value, flags);
			break;
		case 2:
			sprintf(gsdline,
					 "\tISD %s=%o flags=%o\n", name, value, flags);
			break;
		case 3:
			sprintf(gsdline,
					 "\tXFER %s=%o flags=%o\n", name, value, flags);
			xferad = value;
			break;
		case 4:
			sprintf(gsdline,
					 "\tGLOBAL %s=%o %s flags=%o\n",
					 name, value, cp[i+4] & 8 ? "DEF" : "REF", flags);
			break;
		case 5:
			sprintf(gsdline,
					 "\tPSECT %s=%o flags=%o\n", name, value, flags);
			psects[psectid] = strdup(name);
			trim(psects[psectid++]);
			break;
		case 6:
			sprintf(gsdline,
					 "\tIDENT %s=%o flags=%o\n", name, value, flags);
			break;
		case 7:
			sprintf(gsdline,
					 "\tVSECT %s=%o flags=%o\n", name, value, flags);
			break;
		default:
			sprintf(gsdline,
					 "\t***Unknown GSD entry type %d flags=%o\n",
					 cp[i+5] & 0xff, flags);
			break;
		}

		gsdline = realloc(gsdline, strlen(gsdline)+1);
		add_gsdline(gsdline);
	}

}

int compare_gsdlines(const void *p1, const void *p2)
{
	const char * const *l1 = p1, * const *l2 = p2;

	return strcmp(*l1, *l2);
}

void got_endgsd(char *cp, int len)
{
	int i;

	qsort(all_gsds, nr_gsds, sizeof(char *), compare_gsdlines);

	printf("GSD:\n");
	
	for(i = 0; i < nr_gsds; i++)
	{
		fputs(all_gsds[i], stdout);
		free(all_gsds[i]);
	}

	printf("ENDGSD\n");

	free(all_gsds);
}

unsigned last_text_addr = 0;

void got_text(char *cp, int len)
{
	unsigned addr = WORD(cp+2);

	last_text_addr = addr;

	printf("TEXT ADDR=%o LEN=%o\n", last_text_addr, len-4);

	dump_words(last_text_addr, cp+4, len-4);

	if (bin) dump_bin(last_text_addr, cp+4, len-4);
}

void rad50name(char *cp, char *name)
{
	unrad50(WORD(cp), name);
	unrad50(WORD(cp+2), name+3);
	name[6] = 0;
	trim(name);
}

void got_rld(char *cp, int len)
{
	int i;
	printf("RLD\n");

	for(i = 2; i < len;)
	{
		unsigned addr;
		unsigned word;
		unsigned disp = cp[i+1] & 0xff;
		char name[8];
		char *byte;

		addr = last_text_addr + disp - 4;

		byte = "";
		if(cp[i] & 0200)
			byte = " byte";

		switch(cp[i] & 0x7f)
		{
		case 01:
			printf("\tInternal%s %o=%o\n", byte, addr, WORD(cp+i+2));
			i += 4;
			break;
		case 02:
			rad50name(cp+i+2, name);
			printf("\tGlobal%s %o=%s\n", byte, addr, name);
			i += 6;
			break;
		case 03:
			printf("\tInternal displaced%s %o=%o\n", byte, addr, WORD(cp+i+2));
			i += 4;
			badbin = 1;
			break;
		case 04:
			rad50name(cp+i+2, name);
			printf("\tGlobal displaced%s %o=%s\n", byte, addr, name);
			i += 6;
			badbin = 1;
			break;
		case 05:
			rad50name(cp+i+2, name);
			word = WORD(cp+i+6);
			printf("\tGlobal plus offset%s %o=%s+%o\n",
				byte, addr, name, word);
			i += 8;
			badbin = 1;
			break;
		case 06:
			rad50name(cp+i+2, name);
			word = WORD(cp+i+6);
			printf("\tGlobal plus offset displaced%s %o=%s+%o\n",
				byte, addr, name, word);
			i += 8;
			badbin = 1;
			break;
		case 07:
			rad50name(cp+i+2, name);
			word = WORD(cp+i+6);
			printf("\tLocation counter definition %s+%o\n",
				name, word);
			i += 8;

			last_text_addr = word;
			break;
		case 010:
			word = WORD(cp+i+2);
			printf("\tLocation counter modification %o\n", word);
			i += 4;

			last_text_addr = word;
			break;
		case 011:
			printf("\t.LIMIT %o\n", addr);
			i += 2;
			break;

		case 012:
			rad50name(cp+i+2, name);
			printf("\tPSECT%s %o=%s\n", byte, addr, name);
			i += 6;
			badbin = 1;
			break;
		case 014:
			rad50name(cp+i+2, name);

			printf("\tPSECT displaced%s %o=%s+%o\n", byte, addr, name, word);
			i += 6;
			badbin = 1;
			break;
		case 015:
			rad50name(cp+i+2, name);
			word = WORD(cp+i+6);
			printf("\tPSECT plus offset%s %o=%s+%o\n",
				byte, addr, name, word);
			i += 8;
			badbin = 1;
			break;
		case 016:
			rad50name(cp+i+2, name);
			word = WORD(cp+i+6);
			printf("\tPSECT plus offset displaced%s %o=%s+%o\n",
				byte, addr, name, word);
			i += 8;
			badbin = 1;
			break;

		case 017:
			badbin = 1;
			printf("\tComplex%s %o=", byte, addr);
			i += 2;
			{
				char *xp = cp + i;
				int size;
				for(;;)
				{
					size = 1;
					switch(*xp)
					{
					case 000:
						fputs("nop ", stdout); break;
					case 001:
						fputs("+ ", stdout); break;
					case 002:
						fputs("- ", stdout); break;
					case 003:
						fputs("* ", stdout); break;
					case 004:
						fputs("/ ", stdout); break;
					case 005:
						fputs("& ", stdout); break;
					case 006:
						fputs("! ", stdout); break;
					case 010:
						fputs("neg ", stdout); break;
					case 011:
						fputs("^C ", stdout); break;
					case 012:
						fputs("store ", stdout); break;
					case 013:
						fputs("store{disp} ", stdout); break;

					case 016:
						rad50name(xp+1, name);
						printf("%s ", name);
						size = 5;
						break;

					case 017:
						assert((xp[1] & 0377) < psectid);
						printf("%s:%o ",
							psects[xp[1] & 0377],
							WORD(xp+2));
						size = 4;
						break;

					case 020:
						printf("%o ", WORD(xp+1));
						size = 3;
						break;
					default:
						printf("**UNKNOWN COMPLEX CODE** %o\n", *xp & 0377);
						return;
					}
					i += size;
					if(*xp == 012 || *xp == 013)
						break;
					xp += size;
				}
				fputc('\n', stdout);
				break;
			}

		default:
			printf("\t***Unknown RLD code %o\n", cp[i] & 0xff);
			return;
		}
	}

}

void got_isd(char *cp, int len)
{
	printf("ISD len=%o\n");
}

void got_endmod(char *cp, int len)
{
	printf("ENDMOD\n");
}

void got_libhdr(char *cp, int len)
{
	printf("LIBHDR\n");
}

void got_libend(char *cp, int len)
{
	printf("LIBEND\n");
}

int main(int argc, char *argv[])
{
	int len;
	FILE *fp;
	char *cp;

	fp = fopen(argv[1], "rb");
	if(fp == NULL)
		return EXIT_FAILURE;
	if(argv[2])
	{
		bin = fopen(argv[2], "wb");
		if(bin == NULL) return EXIT_FAILURE;
	}

	while((cp = readrec(fp, &len)) != NULL)
	{
		switch(cp[0] & 0xff)
		{
		case 1:
			got_gsd(cp, len);
			break;
		case 2:
			got_endgsd(cp, len);
			break;
		case 3:
			got_text(cp, len);
			break;
		case 4:
			got_rld(cp, len);
			break;
		case 5:
			got_isd(cp, len);
			break;
		case 6:
			got_endmod(cp, len);
			break;
		case 7:
			got_libhdr(cp, len);
			break;
		case 8:
			got_libend(cp, len);
			break;
		default:
			printf("Unknown record type %d\n", cp[0] & 0xff);
			break;
		}

		free(cp);
	}

	if (bin)
	{	dump_bin (xferad, NULL, 0);
		fclose (bin);
		if (badbin) fprintf (stderr, "Probable errors in binary file\n");
	}

	fclose (fp);
	return EXIT_SUCCESS;
}
