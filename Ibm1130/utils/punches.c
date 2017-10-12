/*
 * punches - convert beteen IBM1130 simulator binary card image format and ascii text lists of punch numbers
 *
 * Usage:
 *      punches -b [infile [outfile]]
 *          Converts from ascii to binary. Reads stdin/writes stdout if infile/outfile not specified
 *
 *      punches -a [infile [outfile]]
 *          Converts from binary to ascii.
 *
 * The ASCII format consists of an arbitrary number of card images. Each card image consists of
 * a line with the word "start", followed by 80 lines each containing the punch data for one card
 * column, followe by a line with the word "end".
 *
 * A column specification line consists of the word "blank", for a column with no punches,
 * or an arbitrary number of integer row names separated by hyphens. The row names are 12, 11, 0, 1, 2, ..., 9.
 *
 * The character #, * or ; terminates an input line and the remainder of the line is ignored as a comment.
 * Blank lines are ignored and may occur at any place in the input file.
 *
 * A typical card specification might look like this:
 
   start
   * This is a comment line
   12-1-2
   blank
   5         # this is a comment after the data for column 3
   4
   2
   blank
   blank
   11-5
   2-6-3
.                     \
.                      | not all lines shown. Exactly 80 data lines are required
.                     /
blank
12-0-2-6-7-8
end

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util_io.h"
#ifdef WIN32                    // for Windows binary file mode setting
#  include <io.h>
#  include <fcntl.h>
#endif

#define TRUE  1
#define FALSE 0
typedef int BOOL;

#define BETWEEN(v,a,b) (((v) >= (a)) && ((v) <= (b)))

BOOL failed = FALSE;
int ncards = 0;

void tobinary (char *fnin, char *fnout);
void toascii (char *fnin, char *fnout);
void bail (char *msg);

int main (int argc, char **argv)
{
    enum {MODE_UNKNOWN, MODE_TOBINARY, MODE_TOASCII} mode = MODE_UNKNOWN;
    int i;
    char *arg, *fnin = NULL, *fnout = NULL;
    static char usestr[] = "Usage: punches -b|-a [infile [outfile]]";

    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if (*arg == '-') {
            arg++;
            while (*arg) {
                switch (*arg++) {
                    case 'b':
                        mode = MODE_TOBINARY;
                        break;

                    case 'a':
                        mode = MODE_TOASCII;
                        break;

                    default:
                        bail(usestr);
                }
            }
        }
        else if (fnin == NULL)
            fnin = arg;
        else if (fnout == NULL)
            fnout = arg;
        else
            bail(usestr);
    }

    util_io_init();                             // check CPU for big/little endianness

    if (mode == MODE_TOBINARY)
        tobinary(fnin, fnout);
    else if (mode == MODE_TOASCII)
        toascii(fnin, fnout);
    else
        bail(usestr);

    if (failed) {
        if (fnin != NULL) {                     // if there was an error, delete output file if possible
            unlink(fnout);
            fprintf(stderr, "Output file \"%s\" deleted\n", fnout);
            exit(1);
        }
        else
            bail("Output file is incorrect");
    }
    else                                        // if no error, tell how many cards we converted
        fprintf(stderr, "* %d card%s converted\n", ncards, (ncards == 1) ? "" : "s");

    return 0;
}

// alltrim - remove string's leading and trailing whitespace

char *alltrim (char *str)
{
    char *c, *e;

    for (c = str; *c && *c <= ' '; c++)         // skip over leading whitespace
        ;

    if (c > str)                                // if there was some, copy string down over it
        strcpy(str, c);

    for (e = str-1, c = str; *c; c++)           // find last non-white character
        if (*c > ' ')
            e = c;

    e[1] = '\0';                                // terminate string immediately after last nonwhite character
    return str;                                 // return pointer to string
}

void tobinary (char *fnin, char *fnout)
{
    FILE *fin, *fout;
    BOOL gotnum;
    int col, v, lineno = 0;
    char str[256], *c;
    unsigned short buf[80], punches;
    static unsigned short punchval[13] = {
        0x2000, 0x1000, 0x0800, 0x0400, 0x0200,         // 0, 1, 2, 3, 4
        0x0100, 0x0080, 0x0040, 0x0020, 0x0010,         // 5, 6, 7, 8, 9
        0x0000,                                         // there is no 10 punch
        0x4000, 0x8000};                                // 11 and 12.

    if (fnin == NULL) {
        fin = stdin;
    }
    else if ((fin = fopen(fnin, "r")) == NULL) {
        perror(fnin);
        exit(1);
    }

    if (fnout == NULL) {
        fout = stdout;
#ifdef WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }
    else if ((fout = fopen(fnout, "wb")) == NULL) {
        perror(fnout);
        exit(1);
    }

    col = 0;                                        // we are starting between cards, expect start as first data line

    while (fgets(str, sizeof(str), fin) != NULL && ! failed) {
        alltrim(str);                               // trim leading/trailing blanks (including newline)
        lineno++;                                   // count input line

        if (*str == ';' || *str == '#'|| *str == '*' || ! *str)
            continue;                               // ignore comment or blank line

        if (strnicmp(str, "start", 5) == 0) {       // start marks new card, proceed to column 1 (strnicmp so trailing comment is ignored)
            if (col == 0)
                col = 1;
            else {
                fprintf(stderr, "\"start\" encountered where column %d was expected, at line %d\n", lineno);
                failed = TRUE;
            }
        }
        else if (strnicmp(str, "end", 3) == 0) {    // end is expected as 81'st data line
            if (col == 81) {
                fxwrite(buf, 2, 80, fout);          // write binary card image to output file
                ncards++;                           // increment card count
                col = 0;                            // reset, expect start next
            }
            else {
                fprintf(stderr, "\"end\" encountered where ");

                if (col == 0)
                    fprintf(stderr, "\"start\"");
                else
                    fprintf(stderr, "column %d", col);

                fprintf(stderr, " was expected, at line %d\n", lineno);
                failed = TRUE;
            }
        }
        else if (BETWEEN(col, 1, 80)) {             // for column 1 to 80, we expect a data line
            if (strnicmp(str, "blank", 5) == 0) {   // blank indicates an unpunched column
                buf[col-1] = 0;
                col++;
            }
            else {
                punches = 0;                        // prepare to parse a data line. Punches is output binary value for column

                v = 0;                              // v is current punch number
                gotnum = FALSE;                     // gotnum indicates we've seen a punch number

                for (c = str; ! failed; c++) {
                    if (BETWEEN(*c, '0', '9')) {    // this is a digit, accumulate into current punch number
                        v = v*10 + *c - '0';
                        gotnum = TRUE;              // note that we've seen a value
                    }
                    else if (*c == '-' || *c == '\0') {                 // at - separator or at end of string
                        if (gotnum && BETWEEN(v, 0, 12) && v != 10)
                            punches |= punchval[v];                     // add correct bit to column binary value
                        else {                                          // error if number not seen or punch number not 0..9, 11, or 12
                            fprintf(stderr, "Invalid punch value %d at line %d\n", v, lineno);
                            failed = TRUE;
                            break;
                        }

                        if (*c == '\0') {           // at end of string store value and advance column count
                            buf[col-1] = punches;
                            col++;
                            break;
                        }
                        else {
                            v = 0;                  // at separator, reset for next punch value
                            gotnum = FALSE;
                        }
                    }
                    else if (*c == '#' || *c == ';' || *c == '*') {
                        break;                      // terminate line parsing at comment character
                    }
                    else {                          // invalid character
                        fprintf(stderr, "Unexpected character '%c' at line %d\n", *c, lineno);
                        failed = TRUE;
                        break;
                    }
                }
            }
        }
        else {                                      // we expected start or end when not expecting column data
            fprintf(stderr, "\"%s\" encountered where \"%s\" was expected, at line %d\n", str,
                (col == 0) ? "start" : "end", lineno);
            failed = TRUE;
        }
    }

    fclose(fin);
    fclose(fout);
}

void toascii (char *fnin, char *fnout)
{
    FILE *fin, *fout;
    unsigned short buf[80], mask;
    int nread, col, row;
    BOOL first;
    static char *punchname[] = {"12", "11", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

    if (fnin == NULL) {
        fin = stdin;                                    // no input file named, read from stdin
#ifdef WIN32
        _setmode(_fileno(stdin), _O_BINARY);            // (on Windows, must set binary mode)
#endif
    }
    else if ((fin = fopen(fnin, "rb")) == NULL) {       // open named input file
        perror(fnin);
        exit(1);
    }

    if (fnout == NULL) {                                // no output file named, write to stdout
        fout = stdout;
    }
    else if ((fout = fopen(fnout, "wb")) == NULL) {     // open named output file
        perror(fnout);
        exit(1);
    }
                                                        // write comment with input file name
    fprintf(fout, "* converted from %s\n", (fnin == NULL) ? "<stdin>" : fnin);

    while ((nread = fxread(buf, 2, 80, fin)) == 80) {   // pull cards from binary file
        ncards++;                                       // increment card count
        fprintf(fout, "**** card %d\nstart\n", ncards); // write comment with card number and start statement

        for (col = 0; col < 80; col++) {                // dump 80 columns
            if (buf[col] == 0) {
                fprintf(fout, "blank\n");               // no punches this column
            }
            else if (buf[col] & 0x000F) {               // if low bits are set it is not a valid IBM1130 card image
                fprintf(stderr, "Input file is not an IBM 1130 card image, low bits set found at card image %d\n", ncards);
                failed = TRUE;
                break;
            }
            else {
                first = TRUE;                           // scan the 12 punch bits
                for (mask = 0x8000, row = 0; row < 12; row++, mask >>= 1) {
                    if (buf[col] & mask) {              // output name of punch row for each bit set (12, 10, 0, ..., 9)
                        fprintf(fout, "%s%s", first ? "" : "-", punchname[row]);
                        first = FALSE;                  // next punch will need a hyphen
                    }
                }
                putc('\n', fout);
            }
        }

        fprintf(fout, "end\n");
    }

    if (nread != 0) {                           // oops, file wasn't a multiple of 160 bytes in length
        fprintf(stderr, "Input file invalid or contained a partial card image\n");
        failed = TRUE;
    }

    fclose(fin);
    fclose(fout);
}

void bail (char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}
