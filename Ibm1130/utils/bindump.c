/*
 * (C) Copyright 2002, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 *
 * This is not a supported product, but I welcome bug reports and fixes.
 * Mail to sim@ibm1130.org
 */

// ---------------------------------------------------------------------------------
// BINDUMP - dumps card deck files in assembler object format
//
// Usage:
///			bindump    deckfile			 lists object header info & sector break cards
//			bindump -v deckfile			 lists object data records as well
//			bindump -p deckfile			 for system program, lists phase IDs in the deck
//			bindump -s deckfile >outfile for system program, sorts the phases & writes to stdout

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif

#include "util_io.h"

#ifndef TRUE
    #define BOOL  int
    #define TRUE  1
    #define FALSE 0
#endif

typedef enum {R_ABSOLUTE = 0, R_RELATIVE = 1, R_LIBF = 2, R_CALL = 3} RELOC;

BOOL verbose = FALSE;
BOOL phid    = FALSE;
BOOL sort    = FALSE;
unsigned short card[80], buf[54], cardtype;

// bindump - dump a binary (card format) deck to verify sbrks, etc

void bail        (char *msg);
void dump        (char *fname);
void dump_data   (char *fname);
void dump_phids  (char *fname);
char *getname    (unsigned short *ptr);
char *getseq     (void);
int  hollerith_to_ascii (unsigned short h);
void process     (char *fname);
void show_raw    (char *name);
void show_data   (void);
void show_core   (void);
void show_endc   (void);
void show_81     (void);
void show_main   (void);
void show_sub    (void);
void show_ils    (void);
void show_iss    (void);
void show_end    (void);
void sort_phases (char *fname);
void trim        (char *s);
void unpack      (unsigned short *card, unsigned short *buf);
void verify_checksum(unsigned short *buf);

int main (int argc, char **argv)
{
	char *arg;
	static char usestr[] = "Usage: bindump [-psv] filename...";
	int i;

	for (i = 1; i < argc; i++) {
		arg = argv[i];
		if (*arg == '-') {
			arg++;
			while (*arg) {
				switch (*arg++) {
					case 'v':
						verbose = TRUE;
						break;
					case 'p':
						phid = TRUE;		// print only phase ID's
						break;
					case 's':
						sort = TRUE;		// sort deck by phases, writing to stdout
						break;
					default:
						bail(usestr);
				}
			}
		}
	}

	for (i = 1; i < argc; i++) {
		arg = argv[i];
		if (*arg != '-')
			process(arg);
	}
	return 0;
}

void process (char *nm)
{
#ifdef _WIN32
	WIN32_FIND_DATA fd;
	HANDLE hFind;
	char *c, buf[256];

	if (strchr(nm, '*') == NULL && strchr(nm, '?') == NULL)
		dump(nm);

	else if ((hFind = FindFirstFile(nm, &fd)) == INVALID_HANDLE_VALUE)
		fprintf(stderr, "No files matching '%s'\n", nm);

	else {
		if ((c = strrchr(nm, '\\')) == NULL)
			c = strrchr(nm, ':');

		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;

			if (c == NULL)
				dump(fd.cFileName);
			else {
				strcpy(buf, nm);
				strcpy(buf + (c-nm+1), fd.cFileName);
				dump(buf);
			}
						
		} while (FindNextFile(hFind, &fd));

		FindClose(hFind);
	}
#else
    dump(nm);					// on unices, sh globs for us
#endif
}

void dump (char *fname)
{
	if (sort)
		sort_phases(fname);
	else if (phid)
		dump_phids(fname);
	else
		dump_data(fname);
}

struct tag_card {
	int phid, seq;
	unsigned short card[80];
};

int cardcomp (const void *a, const void *b)
{
	short diff;

	diff = ((struct tag_card *) a)->phid - ((struct tag_card *) b)->phid;

	return diff ? diff : (((struct tag_card *) a)->seq - ((struct tag_card *) b)->seq);
}

void sort_phases (char *fname)
{
	int i, ncards, cardtype, len, seq = 0, phid;
	struct tag_card *deck;
	FILE *fd;
	BOOL saw_sbrk = TRUE;

	if ((fd = fopen(fname, "rb")) == NULL) {
		perror(fname);
		return;
	}

	fseek(fd, 0, SEEK_END);
	len = ftell(fd);				// get length of file
	fseek(fd, 0, SEEK_SET);

	if (len <= 0 || (len % 160) != 0) {
		fprintf(stderr, "%s is not a binard deck image\n");
		fclose(fd);
		return;
	}

	ncards = len / 160;

	if ((deck = (struct tag_card *) malloc(ncards*sizeof(struct tag_card))) == NULL) {
		fprintf(stderr, "%s: can't sort, insufficient memory\n");
		fclose(fd);
		return;
	}

	phid = 0;
	for (i = 0; i < ncards; i++) {
		if (fxread(deck[i].card, sizeof(card[0]), 80, fd) != 80) {
			free(deck);
			fprintf(stderr, "%s: error reading deck\n");
			fclose(fd);
			return;
		}

		unpack(deck[i].card, buf);
		deck[i].seq = seq++;
		deck[i].phid = phid;

		verify_checksum(buf);

		cardtype = (buf[2] >> 8) & 0xFF;

		if (cardtype == 1 || cardtype == 2) {		// start of deck is same as sector break
			saw_sbrk = TRUE;
		}
		else if (cardtype == 0) {
			fprintf(stderr, "%s is a core image deck\n");
			free(deck);
			fclose(fd);
			return;
		}
		else if (cardtype == 0x0A && saw_sbrk) {
			phid = (int) (signed short) buf[10];
			if (phid < 0)
				phid = -phid;

			deck[i].phid = phid;					// this belongs to the new phase
			deck[i-1].phid = phid;					// as does previous card
			saw_sbrk = FALSE;
		}
	}
	fclose(fd);

	qsort(deck, ncards, sizeof(struct tag_card), cardcomp);	// sort the deck

#ifdef _WIN32
	_setmode(_fileno(stdout), _O_BINARY);			// set standard output to binary mode
#endif

	for (i = 0; i < ncards; i++) 					// write to stdout
		fxwrite(deck[i].card, sizeof(card[0]), 80, stdout);

	free(deck);
}

void dump_phids (char *fname)
{
	FILE *fp;
	BOOL first = TRUE;
	BOOL saw_sbrk = TRUE, neg;
	short id;

	if ((fp = fopen(fname, "rb")) == NULL) {
		perror(fname);
		return;
	}
	
	printf("\n%s:\n", fname);

	while (fxread(card, sizeof(card[0]), 80, fp) > 0) {
		unpack(card, buf);
		verify_checksum(buf);

		cardtype = (buf[2] >> 8) & 0xFF;

		if (cardtype == 1 && ! first) {			// sector break
			saw_sbrk = TRUE;
			continue;
		}
		else {
			switch (cardtype) {
				case 0x00:
					printf("   This is a core image deck\n");
					goto done;
					break;
				case 0x01:
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
				case 0x06:
				case 0x07:
				case 0x0F:
					break;

				case 0x0A:
					if (saw_sbrk) {
						id = buf[10];
						if (id < 0)
							id = -id, neg = TRUE;
						else
							neg = FALSE;
						printf("   : %3d / %02x%s\n", id, id, neg ? " (neg)" : "");
						saw_sbrk = FALSE;
					}
					break;

				default:
					show_raw("??? ");
			}
		}
done:
		first = FALSE;
	}

	fclose(fp);
}

void dump_data (char *fname)
{
	FILE *fp;
	BOOL first = TRUE;
	char str[80];
	int i;

	if ((fp = fopen(fname, "rb")) == NULL) {
		perror(fname);
		return;
	}
	
	printf("\n%s:\n", fname);

	while (fxread(card, sizeof(card[0]), 80, fp) > 0) {
		unpack(card, buf);
		verify_checksum(buf);

		cardtype = (buf[2] >> 8) & 0xFF;

		if (cardtype == 1 && ! first) {			// sector break
			for (i = 4; i < 72; i++)
				str[i] = hollerith_to_ascii(card[i]);

			str[i] = '\0';
			trim(str+4);
			printf("*SBRK %s\n", str+4);
			continue;
		}
		else {
			switch (cardtype) {
				case 0x00:
					if (first)
						show_raw("CORE");
					if (verbose)
						show_core();
					break;

				case 0x01:
					show_raw("ABS ");
					show_main();
					break;
				case 0x02:
					show_raw("REL ");
					show_main();
					break;
				case 0x03:
					show_raw("LIB ");
					show_sub();
					break;
				case 0x04:
					show_raw("SUB ");
					show_sub();
					break;
				case 0x05:
					show_raw("ISSL");
					show_iss();
					break;
				case 0x06:
					show_raw("ISSC");
					show_iss();
					break;
				case 0x07:
					show_raw("ILS ");
					show_ils();
					break;
				case 0x0F:
					show_raw("END ");
					show_end();
					break;
				case 0x80:
					show_raw("ENDC");
					show_endc();
					break;
				case 0x81:
					show_raw("81  ");
					show_81();
					break;
				case 0x0A:
					if (verbose)
						show_data();
					break;
				default:
					show_raw("??? ");
			}
		}

		first = FALSE;
	}

	fclose(fp);
}

void show_data (void)
{
	int i, n, jrel, rflag, nout, ch, reloc;
	BOOL first = TRUE;

	n = buf[2] & 0x00FF;

	printf("%04x: ", buf[0]);

	jrel = 3;
	nout = 0;
	rflag = buf[jrel++];
	for (i = 0; i < n; i++) {
		if (nout >= 8) {
			rflag = buf[jrel++];
			if (first) {
				printf(" %s", getseq());
				first = FALSE;
			}
			printf("\n      ");
			nout = 0;
		}
		reloc = (rflag >> 14) & 0x03;
		ch = (reloc == R_ABSOLUTE) ? ' ' :
			 (reloc == R_RELATIVE) ? 'R' :
			 (reloc == R_LIBF)     ? 'L' : '@';

		printf("%04x%c ", buf[9+i], ch);
		rflag <<= 2;
		nout++;
	}
	putchar('\n');
}

void show_core (void)
{
	int i, n, nout;
	BOOL first = TRUE;

	n = buf[2] & 0x00FF;

	printf("%04x: ", buf[0]);

	nout = 0;
	for (i = 0; i < n; i++) {
		if (nout >= 8) {
			if (first) {
				printf(" %s", getseq());
				first = FALSE;
			}
			printf("\n      ");
			nout = 0;
		}
		printf("%04x ", buf[9+i]);
		nout++;
	}
	putchar('\n');
}

void info (int i, char *nm, char type)
{
	if (nm)
		printf("%s ", nm);

	switch (type) {
		case 'd':
			printf("%d ", buf[i]);
			break;

		case 'x':
			printf("%04x ", buf[i]);
			break;

		case 'b':
			printf("%02x ", buf[i] & 0xFF);
			break;

		case 'n':
			printf("%s ", getname(buf+i));
			break;

		default:
			bail("BAD TYPE");
	}
}

void show_main (void)
{
	printf("      ");
	info(2, "prec",   'b');
	info(4, "common", 'd');
	info(6, "work",   'd');
	info(8, "files",  'd');
	info(9, "name",   'n');
	info(11, "pta",    'x');
	putchar('\n');
}

void show_sub (void)
{
	int i, n;

	printf("      ");
	info( 2, "prec",   'b');

	n = buf[5] / 3;
	for (i = 0; i < n; i++) {
		info( 9+3*i, "ent", 'n');
		info(11+3*i, NULL, 'x');
	}

	putchar('\n');
}

void show_iss (void)
{
	printf("      ");
	info(12, "level",  'd');
	putchar('\n');
}

void show_ils (void)
{
	printf("      ");
	info( 2, "prec",   'b');
	info( 5, "nint6",  'd');
	info( 9, "ent",    'n');
	info(11, NULL,     'x');
	info(14, "nint",   'd');
	info(15, "il1",    'd');
	info(16, "il2",    'd');
	putchar('\n');
}

void show_end (void)
{
	printf("      ");
	info(0, "size", 'd');
	info(3, "pta",  'x');
	putchar('\n');
}

void show_endc(void)
{
	printf("      ");
	info(52, "IX3", 'x');
	info(53, "pta", 'x');
	putchar('\n');
}

void show_81(void)
{
}

void show_raw (char *name)
{
	int i;
	printf("*%s", name);

	for (i = 0; i < 12; i++)
		printf(" %04x", buf[i]);

	printf(" %s\n", getseq());
}

char * getseq (void)
{
	static char seq[10];
	int i;

	for (i = 0; i < 8; i++)
		seq[i] = hollerith_to_ascii(card[72+i]);

    seq[i] = '\0';
	return seq;
}


void bail (char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

void unpack (unsigned short *icard, unsigned short *obuf)
{
	int i, j;
	unsigned short wd1, wd2, wd3, wd4;

	for (i = j = 0; i < 54; i += 3, j += 4) {
		wd1 = icard[j];
		wd2 = icard[j+1];
		wd3 = icard[j+2];
		wd4 = icard[j+3];

		obuf[i  ] = (wd1        & 0xFFF0) | ((wd2 >> 12) & 0x000F);
		obuf[i+1] = ((wd2 << 4) & 0xFF00) | ((wd3 >>  8) & 0x00FF);
		obuf[i+2] = ((wd3 << 8) & 0xF000) | ((wd4 >>  4) & 0x0FFF);
	}
}

void verify_checksum (unsigned short *obuf)
{
//	unsigned short sum;

	if (obuf[1] == 0)			// no checksum
		return;

//	if (sum != card[1])
//		printf("Checksum %04x doesn't match card %04x\n", sum, card[1]);
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

// ---------------------------------------------------------------------------------
// trim - remove trailing whitespace from string s
// ---------------------------------------------------------------------------------

void trim (char *s)
{
	char *nb;

	for (nb = s-1; *s; s++)
		if (*s > ' ')
			nb = s;

	nb[1] = '\0';
}

int ascii_to_ebcdic_table[128] = 
{
	0x00,0x01,0x02,0x03,0x37,0x2d,0x2e,0x2f, 0x16,0x05,0x25,0x0b,0x0c,0x0d,0x0e,0x0f,
	0x10,0x11,0x12,0x13,0x3c,0x3d,0x32,0x26, 0x18,0x19,0x3f,0x27,0x1c,0x1d,0x1e,0x1f,
	0x40,0x5a,0x7f,0x7b,0x5b,0x6c,0x50,0x7d, 0x4d,0x5d,0x5c,0x4e,0x6b,0x60,0x4b,0x61,
	0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7, 0xf8,0xf9,0x7a,0x5e,0x4c,0x7e,0x6e,0x6f,

	0x7c,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7, 0xc8,0xc9,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,
	0xd7,0xd8,0xd9,0xe2,0xe3,0xe4,0xe5,0xe6, 0xe7,0xe8,0xe9,0xba,0xe0,0xbb,0xb0,0x6d,
	0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87, 0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
	0x97,0x98,0x99,0xa2,0xa3,0xa4,0xa5,0xa6, 0xa7,0xa8,0xa9,0xc0,0x4f,0xd0,0xa1,0x07,
};

char *getname (unsigned short *ptr)
{
	static char str[6];
	int i, j, ch;
	long v;

	v = (ptr[0] << 16L) | ptr[1];

	for (i = 0; i < 5; i++) {
		ch = ((v >> 24) & 0x3F) | 0xC0;		// recover those lost two bits
		v <<= 6;

		str[i] = ' ';

		for (j = 0; j < (sizeof(ascii_to_ebcdic_table)/sizeof(ascii_to_ebcdic_table[0])); j++) {
			if (ascii_to_ebcdic_table[j] == ch) {
				str[i] = j;
				break;
			}
		}
	}

	str[5] = '\0';
	return str;
}


