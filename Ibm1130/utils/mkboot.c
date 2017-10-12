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
// MKBOOT - reads card loader format cards and produces an absolute core image that
// can then be dumped out in 1130 IPL, 1800 IPL or Core Image loader formats.
//
// Usage: mkboot [-v] binfile outfile [1130|1800|core [loaddr [hiaddr [ident]]]]"
//
// Arguments:
//          binfile - name of assembler output file (card loader format, absolute output)
//          outfile - name of output file to create
//          mode    - output mode, default is 1130 IPL format
//          loaddr  - low address to dump. Default is lowest address loaded from binfile
//          hiaddr  - high address to dump. Defult is highest address loaded from binfile
//          ident   - ident string to write in last 8 columns. Omit when when writing an
//                    1130 IPL card that requires all 80 columns of data.
//
// Examples:
//      mkboot somefile.bin somefile.ipl 1130
//
//          loads somefile.bin, writes object in 1130 IPL format to somefile.ipl
//          Up to 80 columns will be written depending on what the object actually uses
//
//      mkboot somefile.bin somefile.ipl 1130 /0 /47 SOMEF
//
//          loads somefile.bin. Writes 72 columns (hex 0 to hex 47), with ident columns 73-80 = SOMEF001
//
//      mkboot somefile.bin somefile.dat core 0 0 SOMEF001
//
//          loads somefile.bin and writes a core image format deck with ident SOMEF001, SOMEF002, etc
//
//      For other examples of usage, see MKDMS.BAT
//
//         1.00 - 2002Apr18 - first release. Tested only under Win32. The core image
//                            loader format is almost certainly wrong. Cannot handle
//                            relocatable input decks, but it works well enough to
//                            load DSYSLDR1 which is what we are after here.
// ---------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "util_io.h"

#ifndef TRUE
    #define BOOL  int
    #define TRUE  1
    #define FALSE 0
#endif

#ifndef _WIN32
    int strnicmp (char *a, char *b, int n);
    int strcmpi (char *a, char *b);
#endif

#define BETWEEN(v,a,b) (((v) >= (a)) && ((v) <= (b)))
#define MIN(a,b)       (((a) <= (b)) ? (a) : (b))
#define MAX(a,b)       (((a) >= (b)) ? (a) : (b))

#define MAXADDR 4096

typedef enum {R_ABSOLUTE = 0, R_RELATIVE = 1, R_LIBF = 2, R_CALL = 3} RELOC;

typedef enum {B_1130, B_1800, B_CORE} BOOTMODE;

BOOL verbose = FALSE;
char *infile = NULL, *outfile = NULL;
BOOTMODE mode = B_1130;
int addr_from = 0, addr_to = 79;
int  outcols = 0;                               // columns written in using card output
int maxiplcols = 80;
char cardid[9];                                 // characters used for IPL card ID
int pta = 0;
int load_low = 0x7FFFFFF;
int load_high = 0;
unsigned short mem[MAXADDR];            // small core!

// mkboot - load a binary object deck into core and dump requested bytes as a boot card

void bail (char *msg);
void verify_checksum(unsigned short *card);
char *upcase (char *str);
void unpack (unsigned short *card, unsigned short *buf);
void dump (char *fname);
void loaddata (char *fname);
void write_1130 (void);
void write_1800 (void);
void write_core (void);
void flushcard(void);
int  ascii_to_hollerith (int ch);
void corecard_init (void);
void corecard_writecard (char *sbrk_text);
void corecard_writedata (void);
void corecard_flush (void);
void corecard_setorg (int neworg);
void corecard_writew (int word, RELOC relative);
void corecard_endcard (void);

char *fname = NULL;
FILE *fout;

int main (int argc, char **argv)
{
    char *arg;
    static char usestr[] = "Usage: mkboot [-v] binfile outfile [1130|1800|core [loaddr [hiaddr [ident]]]]";
    int i, ano = 0, ok;

    for (i = 1; i < argc; i++) {
        arg = argv[i];
        if (*arg == '-') {
            arg++;
            while (*arg) {
                switch (*arg++) {
                    case 'v':
                        verbose = TRUE;
                        break;
                    default:
                        bail(usestr);
                }
            }
        }
        else {
            switch (ano++) {
                case 0:
                    infile = arg;
                    break;

                case 1:
                    outfile = arg;
                    break;

                case 2:
                    if       (strcmp(arg, "1130")  == 0) mode = B_1130;
                    else if  (strcmp(arg, "1800")  == 0) mode = B_1800;
                    else if  (strcmpi(arg, "core") == 0) mode = B_CORE;
                    else bail(usestr);
                    break;

                case 3:
                    if (strnicmp(arg, "0x", 2) == 0) ok = sscanf(arg+2, "%x", &addr_from);
                    else if (arg[0] == '/')          ok = sscanf(arg+1, "%x", &addr_from);
                    else                             ok = sscanf(arg,   "%d", &addr_from);
                    if (ok != 1) bail(usestr);
                    break;

                case 4:
                    if (strnicmp(arg, "0x", 2) == 0) ok = sscanf(arg+2, "%x", &addr_to);
                    else if (arg[0] == '/')          ok = sscanf(arg+1, "%x", &addr_to);
                    else                             ok = sscanf(arg,   "%d", &addr_to);
                    if (ok != 1) bail(usestr);
                    break;

                case 5:
                    strncpy(cardid, arg, 9);
                    cardid[8] = '\0';
                    upcase(cardid);
                    break;

                default:
                    bail(usestr);
            }
        }
    }

    if (*cardid == '\0')
        maxiplcols = (mode == B_1130) ? 80 : 72;
    else {
        while (strlen(cardid) < 8)
            strcat(cardid, "0");
        maxiplcols = 72;
    }

    loaddata(infile);

    if (mode == B_1800)
        write_1800();
    else if (mode == B_CORE)
        write_core();
    else
        write_1130();

    return 0;
}

void write_1130 (void)
{
    int addr;
    unsigned short word;

    if ((fout = fopen(outfile, "wb")) == NULL) {
        perror(outfile);
        exit(1);
    }

    for (addr = addr_from; addr <= addr_to; addr++) {
        if (outcols >= maxiplcols)
            flushcard();

        word = mem[addr];

        // if F or L bits are set, or if high 2 bits of displacement are unequal, it's bad
        if ((word & 0x0700) || ! (((word & 0x00C0) == 0) || ((word & 0x00C0) == 0x00C0)))
            printf("Warning: word %04x @ %04x may not IPL properly\n", word & 0xFFFF, addr);

        word = ((word & 0xF800) >> 4) | (word & 0x7F);  // convert to 1130 IPL format

        putc((word & 0x000F) << 4,  fout);              // write the 12 bits in little-endian binary AABBCC00 as CC00 AABB
        putc((word & 0x0FF0) >> 4, fout);
        outcols++;
    }
    flushcard();
    fclose(fout);
}

void write_1800 (void)
{
    int addr;
    unsigned short word;

    if ((fout = fopen(outfile, "wb")) == NULL) {
        perror(outfile);
        exit(1);
    }

    for (addr = addr_from; addr <= addr_to; addr++) {
        word = mem[addr];

        if (outcols >= maxiplcols)
            flushcard();

        putc(0, fout);
        putc(word & 0xFF, fout);        // write the low 8 bits in little-endian binary
        outcols++;

        putc(0, fout);
        putc((word >> 8) & 0xFF, fout);     // write the high 8 bits in little-endian binary
        outcols++;
    }
    flushcard();
    fclose(fout);
}

void write_core (void)
{
    int addr;

    if ((fout = fopen(outfile, "wb")) == NULL) {
        perror(outfile);
        exit(1);
    }

    addr_from = load_low;
    addr_to   = load_high;

    maxiplcols = 72;
    corecard_init();
    corecard_setorg(addr_from);

    for (addr = addr_from; addr <= addr_to; addr++) {
        corecard_writew(mem[addr], 0);
    }

    corecard_flush();
    corecard_endcard();
    fclose(fout);
}

void flushcard (void)
{
    int i, hol, ndig;
    char fmt[20], newdig[20];

    if (outcols <= 0)
        return;                         // nothing to flush

    while (outcols < maxiplcols) {      // pad to required number of columns with blanks (no punches)
        putc(0, fout);
        putc(0, fout);
        outcols++;
    }

    if (*cardid) {                      // add label
        for (i = 0; i < 8; i++) {       // write label as specified
            hol = ascii_to_hollerith(cardid[i] & 0x7F);
            putc(hol & 0xFF, fout);
            putc((hol >> 8) & 0xFF, fout);
        }

        ndig = 0;                       // count trailing digits in the label
        for (i = 8; --i >= 0; ndig++)
            if (! isdigit(cardid[i]))
                break;

        i++;                            // index of first digit in trailing sequence

        if (ndig > 0) {                     // if any, increment them
            sprintf(fmt, "%%0%dd", ndig);   // make, e.g. %03d
            sprintf(newdig, fmt, atoi(cardid+i)+1);
            newdig[ndig] = '\0';            // clip if necessary
            strcpy(cardid+i, newdig);       // replace for next card's sequence number
        }
    }

    outcols = 0;
}

void show_data (unsigned short *buf)
{
    int i, n, jrel, rflag, nout, ch, reloc;

    n = buf[2] & 0x00FF;

    printf("%04x: ", buf[0]);

    jrel = 3;
    nout = 0;
    rflag = buf[jrel++];
    for (i = 0; i < n; i++) {
        if (nout >= 8) {
            rflag = buf[jrel++];
            putchar('\n');
            printf("      ");
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

void loadcard (unsigned short *buf)
{
    int addr, n, i;
    
    addr = buf[0];
    n = buf[2] & 0x00FF;

    for (i = 0; i < n; i++) {
        if (addr >= MAXADDR)
            bail("Program doesn't fit into 4K");
        mem[addr] = buf[9+i];

        load_low  = MIN(addr, load_low);
        load_high = MAX(addr, load_high);
        addr++;
    }
}

void loaddata (char *fname)
{
    FILE *fp;
    BOOL first = TRUE;
    unsigned short card[80], buf[54], cardtype;

    if ((fp = fopen(fname, "rb")) == NULL) {
        perror(fname);
        exit(1);
    }

    if (verbose)    
        printf("\n%s:\n", fname);

    while (fxread(card, sizeof(card[0]), 80, fp) > 0) {
        unpack(card, buf);
        verify_checksum(card);

        cardtype = (buf[2] >> 8) & 0xFF;

        if (cardtype == 1 && ! first) {         // sector break
            if (verbose)
                printf("*SBRK\n");
            continue;
        }
        else {
            switch (cardtype) {
                case 0x01:
                    if (verbose)
                        printf("*ABS\n");
                    break;
                case 0x02:
                case 0x03:
                case 0x04:
                case 0x05:
                case 0x06:
                case 0x07:
                    bail("Data must be in absolute format");
                    break;

                case 0x0F:
                    pta = buf[3];           // save program transfer address
                    if (verbose)
                        printf("*END\n");
                    break;

                case 0x0A:
                    if (verbose)
                        show_data(buf);
                    loadcard(buf);
                    break;
                default:
                    bail("Unexpected card type");
            }
        }
        first = FALSE;
    }

    fclose(fp);
}

void bail (char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

void unpack (unsigned short *card, unsigned short *buf)
{
    int i, j;
    unsigned short wd1, wd2, wd3, wd4;

    for (i = j = 0; i < 54; i += 3, j += 4) {
        wd1 = card[j];
        wd2 = card[j+1];
        wd3 = card[j+2];
        wd4 = card[j+3];

        buf[i  ] = (wd1        & 0xFFF0) | ((wd2 >> 12) & 0x000F);
        buf[i+1] = ((wd2 << 4) & 0xFF00) | ((wd3 >>  8) & 0x00FF);
        buf[i+2] = ((wd3 << 8) & 0xF000) | ((wd4 >>  4) & 0x0FFF);
    }
}

void verify_checksum (unsigned short *card)
{
//  unsigned short sum;

    if (card[1] == 0)           // no checksum
        return;

//  if (sum != card[1])
//      printf("Checksum %04x doesn't match card %04x\n", sum, card[1]);
}

typedef struct {
    int     hollerith;
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
    0x8820,     'c',        // cent
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
    0x4060,     'n',        // not
    0x2820,     'x',        // what?
    0x2420,     ',',
    0x2220,     '%',        // ( in 026 Fortran
    0x2120,     '_',
    0x20A0,     '>',
    0x2060,     '>',
};

int ascii_to_hollerith (int ch)
{
    int i;

    for (i = 0; i < sizeof(cardcode_029) / sizeof(CPCODE); i++)
        if (cardcode_029[i].ascii == ch)
            return cardcode_029[i].hollerith;

    return 0;
}

// ---------------------------------------------------------------------------------
// corecard - routines to write IBM 1130 Card object format
// ---------------------------------------------------------------------------------

unsigned short corecard[54];    // the 54 data words that can fit on a binary format card
int corecard_n   = 0;           // number of object words stored in corecard (0-45)
int corecard_seq = 1;           // card output sequence number
int corecard_org = 0;           // origin of current card-full
int corecard_maxaddr = 0;
BOOL corecard_first = TRUE;     // TRUE when we're to write the program type card

// corecard_init - prepare a new object data output card

void corecard_init (void)
{
    memset(corecard, 0, sizeof(corecard));      // clear card data
    corecard_n = 0;                             // no data
    corecard[0] = corecard_org;                 // store load address
    corecard_maxaddr = MAX(corecard_maxaddr, corecard_org-1);   // save highest address written-to (this may be a BSS)
}

// binard_writecard - emit a card. sbrk_text = NULL for normal data cards, points to comment text for sbrk card

void corecard_writecard (char *sbrk_text)
{
    unsigned short binout[80];
    int i, j;

    for (i = j = 0; i < 54; i += 3, j += 4) {
        binout[j  ] = ( corecard[i]          & 0xFFF0);
        binout[j+1] = ((corecard[i]   << 12) & 0xF000)  | ((corecard[i+1] >> 4) & 0x0FF0);
        binout[j+2] = ((corecard[i+1] <<  8) & 0xFF00)  | ((corecard[i+2] >> 8) & 0x00F0);
        binout[j+3] = ((corecard[i+2] <<  4) & 0xFFF0);
    }

    for (i = 0; i < 72; i++) {
        putc(binout[i] & 0xFF, fout);
        putc((binout[i] >> 8) & 0xFF, fout);
    }

    outcols = 72;                               // add the ident
    flushcard();
}

// binard_writedata - emit an object data card

void corecard_writedata (void)
{
    corecard[1] = 0;                            // checksum
    corecard[2] = 0x0000 | corecard_n;          // data card type + word count
    corecard_writecard(FALSE);                  // emit the card
}

// corecard_flush - flush any pending binary data

void corecard_flush (void)
{
    if (corecard_n > 0)
        corecard_writedata();

    corecard_init();
}

// corecard_setorg - set the origin

void corecard_setorg (int neworg)
{
    corecard_org = neworg;          // set origin for next card
    corecard_flush();               // flush any current data & store origin
}

// corecard_writew - write a word to the current output card.

void corecard_writew (int word, RELOC relative)
{
    if (corecard_n >= 50)           // flush full card buffer (must be even)
        corecard_flush();

    corecard[3+corecard_n++] = word;
    corecard_org++;
}

// corecard_endcard - write end of program card

void corecard_endcard (void)
{
    corecard_flush();

    corecard[0] = 0;                    // effective length: add 1 to max origin, then 1 more to round up
    corecard[1] = 0;
    corecard[2] = 0x8000;               // they look for negative bit but all else must be zero
    corecard[52] = 0xabcd;              // index register 3 value, this is for fun
    corecard[53] = pta;                 // hmmm

    corecard_writecard(NULL);
}

/* ------------------------------------------------------------------------ 
 * upcase - force a string to uppercase (ASCII)
 * ------------------------------------------------------------------------ */

char *upcase (char *str)
{
    char *s;

    for (s = str; *s; s++) {
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
    } 

    return str;
}

#ifndef _WIN32

int strnicmp (char *a, char *b, int n)
{
    int ca, cb;

    for (;;) {
        if (--n < 0)                    // still equal after n characters? quit now
            return 0;

        if ((ca = *a) == 0)             // get character, stop on null terminator
            return *b ? -1 : 0;

        if (ca >= 'a' && ca <= 'z')     // fold lowercase to uppercase
            ca -= 32;

        cb = *b;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;

        if ((ca -= cb) != 0)            // if different, return comparison
            return ca;

        a++, b++;
    }
}

int strcmpi (char *a, char *b)
{
    int ca, cb;

    for (;;) {
        if ((ca = *a) == 0)             // get character, stop on null terminator
            return *b ? -1 : 0;

        if (ca >= 'a' && ca <= 'z')     // fold lowercase to uppercase
            ca -= 32;

        cb = *b;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;

        if ((ca -= cb) != 0)            // if different, return comparison
            return ca;

        a++, b++;
    }
}

#endif
