/* Simple program to display a binary card-image file in ASCII.
 * We assume the deck was written with one card per 16-bit word, left-justified,
 * and written in PC little-endian order
 *
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

#include <stdio.h>
#include <stdlib.h>

int  hollerith_to_ascii (unsigned short h);
void bail (char *msg);

int main (int argc, char **argv)
{
	FILE *fd;
	char line[82];
	unsigned short buf[80];
	int i, lastnb;

	if (argc != 2)
		bail("Usage: viewdeck deckfile");

	if ((fd = fopen(argv[1], "rb")) == NULL) {
		perror(argv[1]);
		return 1;
	}

	while (fread(buf, sizeof(short), 80, fd) == 80) {
		lastnb = -1;
		for (i = 0; i < 80; i++) {
			line[i] = hollerith_to_ascii(buf[i]);
			if (line[i] > ' ')
				lastnb = i;
		}
		line[++lastnb] = '\n';
		line[++lastnb] = '\0';
		fputs(line, stdout);
	}

	fclose(fd);

	return 0;
}

typedef struct {
	unsigned short hollerith;
	char	ascii;
} CPCODE;

static CPCODE cardcode_029[] =
{
	0x0000,		' ',
	0x8000, 	'&',	 		// + in 026 Fortran
	0x4000,		'-',
	0x2000,		'0',
	0x1000,		'1',
	0x0800,		'2',
	0x0400,		'3',
	0x0200,		'4',
	0x0100,		'5',
	0x0080,		'6',
	0x0040,		'7',
	0x0020,		'8',
	0x0010,		'9',
	0x9000, 	'A',
	0x8800,		'B',
	0x8400,		'C',
	0x8200,		'D',
	0x8100,		'E',
	0x8080,		'F',
	0x8040,		'G',
	0x8020,		'H',
	0x8010,		'I',
	0x5000, 	'J',
	0x4800,		'K',
	0x4400,		'L',
	0x4200,		'M',
	0x4100,		'N',
	0x4080,		'O',
	0x4040,		'P',
	0x4020,		'Q',
	0x4010,		'R',
	0x3000, 	'/',
	0x2800,		'S',
	0x2400,		'T',
	0x2200,		'U',
	0x2100,		'V',
	0x2080,		'W',
	0x2040,		'X',
	0x2020,		'Y',
	0x2010,		'Z',
	0x0820,		':',
	0x0420,		'#',		// = in 026 Fortran
	0x0220,		'@',		// ' in 026 Fortran
	0x0120,		'\'',
	0x00A0,		'=',
	0x0060,		'"',
	0x8820,		'c',		// cent
	0x8420,		'.',
	0x8220,		'<',		// ) in 026 Fortran
	0x8120,		'(',
	0x80A0,		'+',
	0x8060,		'|',
	0x4820,		'!',
	0x4420,		'$',
	0x4220,		'*',
	0x4120,		')',
	0x40A0,		';',
	0x4060,		'n',		// not
	0x2820,		'x',		// what?
	0x2420,		',',
	0x2220,		'%',		// ( in 026 Fortran
	0x2120,		'_',
	0x20A0,		'>',
	0x2060,		'>',
};

int hollerith_to_ascii (unsigned short h)
{
	int i;

 	h &= 0xFFF0;

	for (i = 0; i < sizeof(cardcode_029) / sizeof(CPCODE); i++)
		if (cardcode_029[i].hollerith == h)
			return cardcode_029[i].ascii;

	return '?';
}

void bail (char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

