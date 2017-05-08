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
///         bindump    deckfile          lists object header info & sector break cards
//          bindump -v deckfile          lists object data records as well
//          bindump -p deckfile          for system program, lists phase IDs in the deck
//          bindump -s deckfile >outfile for system program, sorts the phases & writes to stdout

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

typedef enum {PACKED, UNPACKED} PACKMODE;

#define CARDTYPE_COREIMAGE  0x00
#define CARDTYPE_ABS        0x01
#define CARDTYPE_REL        0x02
#define CARDTYPE_LIB        0x03
#define CARDTYPE_SUB        0x04
#define CARDTYPE_ISSL       0x05
#define CARDTYPE_ISSC       0x06
#define CARDTYPE_ILS        0x07
#define CARDTYPE_END        0x0F
#define CARDTYPE_ENDC       0x80
#define CARDTYPE_81         0x81
#define CARDTYPE_DATA       0x0A

BOOL verbose = FALSE;
BOOL phid    = FALSE;
BOOL sort    = FALSE;
unsigned short card[80], buf[54];

// bindump - dump a binary (card format) deck to verify sbrks, etc

void  bail        (char *msg);
void  dump        (char *fname);
void  dump_data   (char *fname);
void  dump_phids  (char *fname);
char *getname     (unsigned short *ptr);
char *getseq      (void);
int   hollerith_to_ascii (unsigned short h);
void  process     (char *fname);
void  show_raw    (char *name);
void  show_data   (void);
void  show_core   (void);
void  show_endc   (void);
void  show_81     (void);
void  show_main   (void);
void  show_sub    (void);
void  show_ils    (void);
void  show_iss    (void);
void  show_end    (void);
void  sort_phases (char *fname);
void  trim        (char *s);
void  unpack      (unsigned short *icard, unsigned short *obuf, int nwords);
void  pack        (unsigned short *ocard, unsigned short *ibuf);
void  verify_checksum(unsigned short *buf);
int   type_of_card(unsigned short *buf, PACKMODE packed);
char *card_type_name (unsigned short cardtype);

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
                        phid = TRUE;        // print only phase ID's
                        break;
                    case 's':
                        sort = TRUE;        // sort deck by phases, writing to stdout
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
    dump(nm);                   // on unices, sh globs for us
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
    len = ftell(fd);                // get length of file
    fseek(fd, 0, SEEK_SET);

    if (len <= 0 || (len % 160) != 0) {
        fprintf(stderr, "%s is not a binard deck image\n");
        fclose(fd);
        return;
    }

    ncards = len / 160;

    if (ncards <= 0) {
        fprintf(stderr, "%s: can't sort, empty deck\n");
        fclose(fd);
        return;
    }

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

        deck[i].seq  = seq++;                       // store current sequence
        deck[i].phid = phid;                        // store current phase ID

        cardtype = type_of_card(deck[i].card, PACKED);

        switch (cardtype) {
            case CARDTYPE_ABS:                      // start of deck is same as sector break
                saw_sbrk = TRUE;                    // (though I don't ever expect to get a REL deck)
                break;

            case CARDTYPE_DATA:
                if (saw_sbrk) {
                    unpack(deck[i].card, buf, 0);
                    verify_checksum(buf);

                    phid = (int) (signed short) buf[10];
                    if (phid < 0)
                        phid = -phid;

                    deck[i].phid   = phid;                  // this belongs to the new phase
                    deck[i-1].phid = phid;                  // as does previous card (START or SBRK card)
                    saw_sbrk = FALSE;
                }
                break;

            case CARDTYPE_END:
                break;

            default:
                fprintf(stderr, "%s is a %s deck, can't sort\n", card_type_name(cardtype));
                free(deck);
                fclose(fd);
                return;
        }
    }
    fclose(fd);

    qsort(deck, ncards, sizeof(struct tag_card), cardcomp); // sort the deck

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);                           // set standard output to binary mode
#endif

    for (i = 0; i < ncards; i++) {                                  // write to stdout
        cardtype = type_of_card(deck[i].card, PACKED);
        if (cardtype != CARDTYPE_END || (i == (ncards-1)))          // don't write embedded END cards
            fxwrite(deck[i].card, sizeof(deck[i].card[0]), 80, stdout);
    }
    
    if (cardtype != CARDTYPE_END) {                                 // fudge an end card
        memset(buf, 0, sizeof(buf));
        buf[2] = CARDTYPE_END;
        pack(card, buf);
        fxwrite(card, sizeof(card[0]), 80, stdout);
    }

    free(deck);
}

void dump_phids (char *fname)
{
    FILE *fp;
    BOOL saw_sbrk = FALSE, neg;
    unsigned short cardtype;
    short id;

    if ((fp = fopen(fname, "rb")) == NULL) {
        perror(fname);
        return;
    }
    
    printf("\n%s:\n", fname);

    while (fxread(card, sizeof(card[0]), 80, fp) > 0) {
        cardtype = type_of_card(card, PACKED);

        if (saw_sbrk && cardtype != CARDTYPE_DATA) {
            printf("DECK STRUCTURE ERROR: ABS/SBRK card was followed by %s, not DATA", card_type_name(cardtype));
        }

        switch (cardtype) {
            case CARDTYPE_ABS:          // beginning of absolute deck, or SBRK card (which spoofs an ABS start card)
                saw_sbrk = TRUE;
                break;

            case CARDTYPE_END:
                break;

            case CARDTYPE_DATA:
                if (saw_sbrk) {         // first data card after a SBRK or new deck has the phase ID
                    unpack(card, buf, 11);
                    id = buf[10];
                    if (id < 0)
                        id = -id, neg = TRUE;
                    else
                        neg = FALSE;
                    printf("   : %3d / %02x%s\n", id, id, neg ? " (neg)" : "");
                    saw_sbrk = FALSE;
                }
                break;

            case CARDTYPE_COREIMAGE:
            case CARDTYPE_REL:
            case CARDTYPE_LIB:
            case CARDTYPE_SUB:
            case CARDTYPE_ISSL:
            case CARDTYPE_ISSC:
            case CARDTYPE_ILS:
                printf("%s module not expected in a system load deck\n", card_type_name(cardtype));
                break;

            default:
                show_raw("??? ");
        }
    }

    fclose(fp);
}

void dump_data (char *fname)
{
    FILE *fp;
    BOOL first = TRUE;
    unsigned short cardtype;
    char str[80];
    int i;

    if ((fp = fopen(fname, "rb")) == NULL) {
        perror(fname);
        return;
    }
    
    printf("\n%s:\n", fname);

    while (fxread(card, sizeof(card[0]), 80, fp) > 0) {
        unpack(card, buf, 0);
        verify_checksum(buf);

        cardtype = type_of_card(buf, UNPACKED);

        if (cardtype == 1 && ! first) {         // sector break
            for (i = 4; i < 72; i++)
                str[i] = hollerith_to_ascii(card[i]);

            str[i] = '\0';
            trim(str+4);
            printf("*SBRK %s\n", str+4);
            continue;
        }
        else {
            switch (cardtype) {
                case CARDTYPE_COREIMAGE:
                    if (first)
                        show_raw("CORE");
                    if (verbose)
                        show_core();
                    break;

                case CARDTYPE_ABS:
                    show_raw("ABS ");
                    show_main();
                    break;
                case CARDTYPE_REL:
                    show_raw("REL ");
                    show_main();
                    break;
                case CARDTYPE_LIB:
                    show_raw("LIB ");
                    show_sub();
                    break;
                case CARDTYPE_SUB:
                    show_raw("SUB ");
                    show_sub();
                    break;
                case CARDTYPE_ISSL:
                    show_raw("ISSL");
                    show_iss();
                    break;
                case CARDTYPE_ISSC:
                    show_raw("ISSC");
                    show_iss();
                    break;
                case CARDTYPE_ILS:
                    show_raw("ILS ");
                    show_ils();
                    break;
                case CARDTYPE_END:
                    show_raw("END ");
                    show_end();
                    break;
                case CARDTYPE_ENDC:
                    show_raw("ENDC");
                    show_endc();
                    break;
                case CARDTYPE_81:
                    show_raw("81  ");
                    show_81();
                    break;
                case CARDTYPE_DATA:
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

// unpack nwords of data from card image icard into buffer obuf

void unpack (unsigned short *icard, unsigned short *obuf, int nwords)
{
    int i, j;
    unsigned short wd1, wd2, wd3, wd4;

    if (nwords <= 0 || nwords > 54) nwords = 54;        // the default is to unpack all 54 words

    for (i = j = 0; i < nwords; i++) {
        wd1 = icard[j++];
        wd2 = icard[j++];
        wd3 = icard[j++];
        wd4 = icard[j++];

        obuf[i] = (wd1        & 0xFFF0) | ((wd2 >> 12) & 0x000F);
        if (++i >= nwords) break;
        obuf[i] = ((wd2 << 4) & 0xFF00) | ((wd3 >>  8) & 0x00FF);
        if (++i >= nwords) break;
        obuf[i] = ((wd3 << 8) & 0xF000) | ((wd4 >>  4) & 0x0FFF);
    }
}

// pack - pack 54 words of data in ibuf into card image icard

void pack (unsigned short *ocard, unsigned short *ibuf)
{
    int i, j;

    for (i = j = 0; i < 54; i += 3, j += 4) {
        ocard[j  ] = ( ibuf[i]          & 0xFFF0);
        ocard[j+1] = ((ibuf[i]   << 12) & 0xF000)  | ((ibuf[i+1] >> 4) & 0x0FF0);
        ocard[j+2] = ((ibuf[i+1] <<  8) & 0xFF00)  | ((ibuf[i+2] >> 8) & 0x00F0);
        ocard[j+3] = ((ibuf[i+2] <<  4) & 0xFFF0);
    }
}

void verify_checksum (unsigned short *obuf)
{
//  unsigned short sum;

    if (obuf[1] == 0)           // no checksum
        return;

//  if (sum != card[1])
//      printf("Checksum %04x doesn't match card %04x\n", sum, card[1]);
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
        ch = ((v >> 24) & 0x3F) | 0xC0;     // recover those lost two bits
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

int type_of_card (unsigned short *buf, PACKMODE packed)
{
    unsigned short unp[3];

    // card type is the 3rd unpacked word on the card

    if (packed == PACKED) {
        unpack(buf, unp, 3);        // unpack the first 3 words only
        buf = unp;
    } 

    return (buf[2] >> 8) & 0xFF;
}

char * card_type_name (unsigned short cardtype)
{
    switch (cardtype) {
        case CARDTYPE_COREIMAGE:    return "core image";
        case CARDTYPE_ABS:          return "absolute";
        case CARDTYPE_REL:          return "relative";
        case CARDTYPE_LIB:          return "LIB";
        case CARDTYPE_SUB:          return "SUB";
        case CARDTYPE_ISSL:         return "ISSL";
        case CARDTYPE_ISSC:         return "ISSC";
        case CARDTYPE_ILS:          return "ILS";
        case CARDTYPE_END:          return "END";
        case CARDTYPE_ENDC:         return "ENDC";
        case CARDTYPE_81:           return "81";
        case CARDTYPE_DATA:         return "data";
    }
    return "unknown";
}
