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
#include "util_io.h"

#define TRUE  1
#define FALSE 0
typedef int BOOL;

int  hollerith_to_ascii (unsigned short h);
void bail (char *msg);
void format_coldstart (unsigned short *buf);

int main (int argc, char **argv)
{
    FILE *fd;
    char *fname = NULL, line[82], *arg;
    BOOL coldstart = FALSE;
    unsigned short buf[80];
    int i, lastnb;
    static char usestr[] =
        "Usage: viewdeck [-c] deckfile\n"
        "\n"
        "-c: convert cold start card to 16-bit format as a C array initializer\n";

    for (i = 1; i < argc; i++) {                // process command line arguments
        arg = argv[i];

        if (*arg == '-') {
            arg++;
            while (*arg) {
                switch (*arg++) {
                    case 'c':
                        coldstart = TRUE;
                        break;
                    default:
                        bail(usestr);
                }
            }
        }
        else if (fname == NULL)                 // first non-switch arg is file name
            fname = arg;
        else
            bail(usestr);                       // there can be only one name
    }

    if (fname == NULL)                          // there must be a name
        bail(usestr);

    if ((fd = fopen(fname, "rb")) == NULL) {
        perror(fname);
        return 1;
    }

    while (fxread(buf, sizeof(short), 80, fd) == 80) {
        if (coldstart) {
            format_coldstart(buf);
            break;
        }

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

    if (coldstart) {
        if (fxread(buf, sizeof(short), 1, fd) == 1)
            bail("Coldstart deck has more than one card");
    }

    fclose(fd);

    return 0;
}

void format_coldstart (unsigned short *buf)
{
    int i, nout = 0;
    unsigned short word;

    for (i = 0; i < 80; i++) {
        word = buf[i];                          // expand 12-bit card data to 16-bit instruction
        word = (word & 0xF800) | ((word & 0x0400) ? 0x00C0 : 0x0000) | ((word & 0x03F0) >> 4);

        if (nout >= 8) {
            fputs(",\n", stdout);
            nout = 0;
        }
        else if (i > 0)
            fputs(", ", stdout);

        printf("0x%04x", word);
        nout++;
    }

    putchar('\n');
}

typedef struct {
    unsigned short hollerith;
    char    ascii;
} CPCODE;

static CPCODE cardcode_029[] =
{
    0x0000,     ' ',
    0x8000,     '&',            // + in 026 Fortran
    0x4000,     '-',
    0x2000,     '0',
    0x1000,     '1',
    0x0800,     '2',
    0x0400,     '3',
    0x0200,     '4',
    0x0100,     '5',
    0x0080,     '6',
    0x0040,     '7',
    0x0020,     '8',
    0x0010,     '9',
    0x9000,     'A',
    0x8800,     'B',
    0x8400,     'C',
    0x8200,     'D',
    0x8100,     'E',
    0x8080,     'F',
    0x8040,     'G',
    0x8020,     'H',
    0x8010,     'I',
    0x5000,     'J',
    0x4800,     'K',
    0x4400,     'L',
    0x4200,     'M',
    0x4100,     'N',
    0x4080,     'O',
    0x4040,     'P',
    0x4020,     'Q',
    0x4010,     'R',
    0x3000,     '/',
    0x2800,     'S',
    0x2400,     'T',
    0x2200,     'U',
    0x2100,     'V',
    0x2080,     'W',
    0x2040,     'X',
    0x2020,     'Y',
    0x2010,     'Z',
    0x0820,     ':',
    0x0420,     '#',        // = in 026 Fortran
    0x0220,     '@',        // ' in 026 Fortran
    0x0120,     '\'',
    0x00A0,     '=',
    0x0060,     '"',
    0x8820,     '\xA2',     // cent, in MS-DOS encoding
    0x8420,     '.',
    0x8220,     '<',        // ) in 026 Fortran
    0x8120,     '(',
    0x80A0,     '+',
    0x8060,     '|',
    0x4820,     '!',
    0x4420,     '$',
    0x4220,     '*',
    0x4120,     ')',
    0x40A0,     ';',
    0x4060,     '\xAC',     // not, in MS-DOS encoding
    0x2420,     ',',
    0x2220,     '%',        // ( in 026 Fortran
    0x2120,     '_',
    0x20A0,     '>',
    0xB000,     'a',
    0xA800,     'b',
    0xA400,     'c',
    0xA200,     'd',
    0xA100,     'e',
    0xA080,     'f',
    0xA040,     'g',
    0xA020,     'h',
    0xA010,     'i',
    0xD000,     'j',
    0xC800,     'k',
    0xC400,     'l',
    0xC200,     'm',
    0xC100,     'n',
    0xC080,     'o',
    0xC040,     'p',
    0xC020,     'q',
    0xC010,     'r',
    0x6800,     's',
    0x6400,     't',
    0x6200,     'u',
    0x6100,     'v',
    0x6080,     'w',
    0x6040,     'x',
    0x6020,     'y',
    0x6010,     'z',                // these odd punch codes are used by APL:
    0x1010,     '\001',             // no corresponding ASCII   using ^A
    0x0810,     '\002',             // SYN                      using ^B
    0x0410,     '\003',             // no corresponding ASCII   using ^C
    0x0210,     '\004',             // PUNCH ON                 using ^D
    0x0110,     '\005',             // READER STOP              using ^E
    0x0090,     '\006',             // UPPER CASE               using ^F
    0x0050,     '\013',             // EOT                      using ^K
    0x0030,     '\016',             // no corresponding ASCII   using ^N
    0x1030,     '\017',             // no corresponding ASCII   using ^O
    0x0830,     '\020',             // no corresponding ASCII   using ^P
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

