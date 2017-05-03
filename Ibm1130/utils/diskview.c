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

// DISKVIEW - lists contents of an 1130 system disk image file. Not finished yet.
// needs LET/SLET listing routine.
//
// usage:
//      diskview -v diskfile

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "util_io.h"

#define BETWEEN(v,a,b) (((v) >= (a)) && ((v) <= (b)))
#define MIN(a,b)       (((a) <= (b)) ? (a) : (b))
#define MAX(a,b)       (((a) >= (b)) ? (a) : (b))

#ifndef TRUE
#   define TRUE  1
#   define FALSE 0
#   define BOOL  int
#endif

#define NOT_DEF 0x0658              // defective cylinder table entry means no defect

#define DSK_NUMWD   321             /* words/sector */
#define DSK_NUMCY   203             /* cylinders/drive */
#define DSK_SECCYL    8             /* sectors per cylinder */
#define SECLEN      320             /* data words per sector */
#define SLETLEN     ((3*SECLEN)/4)  /* length of slet in records */

typedef unsigned short WORD;

FILE *fp;
WORD buf[DSK_NUMWD];
WORD dcom[DSK_NUMWD];

#pragma pack(2)
struct tag_slet {
    WORD    phid;
    WORD    addr;
    WORD    nwords;
    WORD    sector;
} slet[SLETLEN];

#pragma pack()

WORD dcyl[3];
BOOL verbose = FALSE;

void checksectors (void);
void dump_id      (void);
void dump_dcom    (void);
void dump_resmon  (void);
void dump_slet    (void);
void dump_hdng    (void);
void dump_scra    (void);
void dump_let     (void);
void dump_flet    (void);
void dump_cib     (void);
void getsector    (int sec, WORD *sbuf);
void getdcyl      (void);
char *lowcase (char *str);

void bail(char *fmt, ...);
char *trim (char *s);

int main (int argc, char **argv)
{
    char *fname = NULL, *arg;
    static char usestr[] = "Usage: diskview [-v] filename";
    int i;

    for (i = 1; i < argc;) {
        arg = argv[i++];
        if (*arg == '-') {
            arg++;
            lowcase(arg);
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
        else if (fname == NULL)
            fname = arg;
        else
            bail(usestr);
    }

    if (fname == NULL)
        bail(usestr);

    if ((fp = fopen(fname, "rb")) == NULL) {
        perror(fname);
        return 2;
    }

    printf("%s:\n", fname);

    checksectors();
    getdcyl();

    dump_id();              // ID & coldstart
    dump_dcom();            // DCOM
    dump_resmon();          // resident image
    dump_slet();            // SLET
    dump_hdng();            // heading sector
    dump_scra();
    dump_flet();
    dump_cib();
    dump_let();

    fclose(fp);
    return 0;
}

// checksectors - verify that all sectors are properly numbered

void checksectors ()
{
    WORD sec = 0;

    fseek(fp, 0, SEEK_SET);

    for (sec = 0; sec < DSK_NUMCY*DSK_SECCYL; sec++) {
        if (fxread(buf, sizeof(WORD), DSK_NUMWD, fp) != DSK_NUMWD)
            bail("File read error or not a disk image file");

        if (buf[0] != sec)
            bail("Sector /%x is misnumbered, run checkdisk [-f]", sec);
    }
}

// get defective cylinder list

void getdcyl (void)
{
    fseek(fp, sizeof(WORD), SEEK_SET);  // skip sector count
    if (fxread(dcyl, sizeof(WORD), 3, fp) != 3)
        bail("Unable to read defective cylinder table");
}

// getsector - read specified absolute sector

void getsector (int sec, WORD *sbuf)
{
    int i, cyl, ssec;

    sec &= 0x7FF;                   // mask of drive bits, if any

    cyl  = sec / DSK_SECCYL;        // get cylinder
    ssec = sec & ~(DSK_SECCYL-1);   // mask to get starting sector of cylinder
    for (i = 0; i < 3; i++) {       // map through defective cylinder table
        if (dcyl[i] == ssec) {
            sec &= (DSK_SECCYL-1);  // mask to get base sector
            cyl  = DSK_NUMCY-3+i;   // replacements are last three on disk
            sec += cyl*DSK_SECCYL;  // add new cylinder offset
            break;
        }
    }
                                    // read the sector
    if (fseek(fp, (sec*DSK_NUMWD+1)*sizeof(WORD), SEEK_SET) != 0)
        bail("File seek failed");

    if (fxread(sbuf, sizeof(WORD), DSK_NUMWD, fp) != DSK_NUMWD)
        bail("File read error or not a disk image file");
}

void dump (int nwords)
{
    int i, nline = 0;

    for (i = 0; i < nwords; i++) {
        if (nline == 16) {
            putchar('\n');
            nline = 0;
        }

        printf("%04x", buf[i]);
        nline++;
    }
    putchar('\n');
}

void showmajor (char *label)
{
    int i;

    printf("\n--- %s ", label);

    for (i = strlen(label); i < 40; i++)
        putchar('-');

    putchar('\n');
    putchar('\n');
}

void name (char *label)
{
    printf("%-32.32s ", label);
}

void pbf (char *label, WORD *buf, int nwords)
{
    int i, nout;

    name(label);

    for (i = nout = 0; i < nwords; i++, nout++) {
        if (nout == 8) {
            putchar('\n');
            name("");
            nout = 0;
        }
        printf(" %04x", buf[i]);
    }

    putchar('\n');
}

void prt (char *label, char *fmt, ...)
{
    va_list args;

    name(label);

    putchar(' ');
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    putchar('\n');
}

void dump_id (void)
{
    showmajor("Sector 0 - ID & coldstart");
    getsector(0, buf);

    pbf("DCYL  def cyl table", buf+  0, 3);
    pbf("CIDN  cart id",       buf+  3, 1);
    pbf("      copy code",     buf+  4, 1);
    pbf("DTYP  disk type",     buf+  7, 1);
    pbf("      diskz copy",    buf+ 30, 8);
    pbf("      cold start pgm",buf+270, 8);
}

// EQUIVALENCES FOR DCOM PARAMETERS 
#define NAME  4   // NAME OF PROGRAM/CORE LOAD
#define DBCT  6   // BLOCK CT OF PROGRAM/CORE LOAD
#define FCNT  7   // FILES SWITCH
#define SYSC  8   // SYSTEM/NON-SYSTEM CARTRIDGE INDR
#define JBSW  9   // JOBT SWITCH
#define CBSW 10   // CLB-RETURN SWITCH
#define LCNT 11   // NO. OF LOCALS
#define MPSW 12   // CORE MAP SWITCH
#define MDF1 13   // NO. DUP CTRL RECORDS (MODIF)
#define MDF2 14   // ADDR OF MODIF BUFFER
#define NCNT 15   // NO. OF NOCALS
#define ENTY 16   // RLTV ENTRY ADDR OF PROGRAM
#define RP67 17   // 1442-5 SWITCH
#define TODR 18   // OBJECT WORK STORAGE DRIVE CODE
#define FHOL 20   // ADDR LARGEST HOLE IN FIXED AREA
#define FSZE 21   // BLK CNT LARGEST HOLE IN FXA
#define UHOL 22   // ADDR LAST HOLE IN USER AREA 2-10
#define USZE 23   // BLK CNT LAST HOLE IN UA     2-10
#define DCSW 24   // DUP CALL SWITCH
#define PIOD 25   // PRINCIPAL I/O DEVICE INDICATOR
#define PPTR 26   // PRINCIPAL PRINT DEVICE INDICATOR
#define CIAD 27   // RLTV ADDR IN @STRT OF CIL ADDR
#define ACIN 28   // AVAILABLE CARTRIDGE INDICATOR
#define GRPH 29   // 2250 INDICATOR               2G2
#define GCNT 30   // NO. G2250 RECORDS            2G2
#define LOSW 31   // LOCAL-CALLS-LOCAL SWITCH     2-2
#define X3SW 32   // SPECIAL ILS SWITCH           2-2
#define ECNT 33   // NO. OF *EQUAT RCDS           2-4
#define ANDU 35   // 1+BLK ADDR END OF UA (ADJUSTED)
#define BNDU 40   // 1+BLK ADDR END OF UA (BASE)
#define FPAD 45   // FILE PROTECT ADDR
#define PCID 50   // CARTRIDGE ID, PHYSICAL DRIVE
#define CIDN 55   // CARTRIDGE ID, LOGICAL DRIVE
#define CIBA 60   // SCTR ADDR OF CIB
#define SCRA 65   // SCTR ADDR OF SCRA
#define FMAT 70   // FORMAT OF PROG IN WORKING STG
#define FLET 75   // SCTR ADDR 1ST SCTR OF FLET
#define ULET 80   // SCTR ADDR 1ST SCTR OF LET
#define WSCT 85   // BLK CNT OF PROG IN WORKING STG
#define CSHN 90   // NO. SCTRS IN CUSHION AREA

struct tag_dcominfo {
    char *nm;
    int offset;
    char *descr;
} dcominfo[] = {
    "NAME",  4, "NAME OF PROGRAM/CORE LOAD",
    "DBCT",  6, "BLOCK CT OF PROGRAM/CORE LOAD",
    "FCNT",  7, "FILES SWITCH",
    "SYSC",  8, "SYSTEM/NON-SYSTEM CARTRIDGE INDR",
    "JBSW",  9, "JOBT SWITCH",
    "CBSW", 10, "CLB-RETURN SWITCH",
    "LCNT", 11, "NO. OF LOCALS",
    "MPSW", 12, "CORE MAP SWITCH",
    "MDF1", 13, "NO. DUP CTRL RECORDS (MODIF)",
    "MDF2", 14, "ADDR OF MODIF BUFFER",
    "NCNT", 15, "NO. OF NOCALS",
    "ENTY", 16, "RLTV ENTRY ADDR OF PROGRAM",
    "RP67", 17, "1442-5 SWITCH",
    "TODR", 18, "OBJECT WORK STORAGE DRIVE CODE",
    "FHOL", 20, "ADDR LARGEST HOLE IN FIXED AREA",
    "FSZE", 21, "BLK CNT LARGEST HOLE IN FXA",
    "UHOL", 22, "ADDR LAST HOLE IN USER AREA",
    "USZE", 23, "BLK CNT LAST HOLE IN UA",
    "DCSW", 24, "DUP CALL SWITCH",
    "PIOD", 25, "PRINCIPAL I/O DEVICE INDICATOR",
    "PPTR", 26, "PRINCIPAL PRINT DEVICE INDICATOR",
    "CIAD", 27, "RLTV ADDR IN @STRT OF CIL ADDR",
    "ACIN", 28, "AVAILABLE CARTRIDGE INDICATOR",
    "GRPH", 29, "2250 INDICATOR",
    "GCNT", 30, "NO. G2250 RECORDS",
    "LOSW", 31, "LOCAL-CALLS-LOCAL SWITCH",
    "X3SW", 32, "SPECIAL ILS SWITCH",
    "ECNT", 33, "NO. OF *EQUAT RCDS",
    "ANDU", 35, "1+BLK ADDR END OF UA (ADJUSTED)",
    "BNDU", 40, "1+BLK ADDR END OF UA (BASE)",
    "FPAD", 45, "FILE PROTECT ADDR",
    "PCID", 50, "CARTRIDGE ID, PHYSICAL DRIVE",
    "CIDN", 55, "CARTRIDGE ID, LOGICAL DRIVE",
    "CIBA", 60, "SCTR ADDR OF CIB",
    "SCRA", 65, "SCTR ADDR OF SCRA",
    "FMAT", 70, "FORMAT OF PROG IN WORKING STG",
    "FLET", 75, "SCTR ADDR 1ST SCTR OF FLET",
    "ULET", 80, "SCTR ADDR 1ST SCTR OF LET",
    "WSCT", 85, "BLK CNT OF PROG IN WORKING STG",
    "CSHN", 90, "NO. SCTRS IN CUSHION AREA",
    NULL
};

void dump_dcom (void)
{
    struct tag_dcominfo *d;
    char txt[50];

    showmajor("Sector 1 - DCOM");
    getsector(1, dcom);

    for (d = dcominfo; d->nm != NULL; d++) {
        sprintf(txt, "%-4.4s %s", d->nm, d->descr);
        pbf(txt, dcom+d->offset, 1);
    }
}

void dump_resmon (void)
{
    showmajor("Sector 2 - Resident Image");
    getsector(2, buf);
    dump(verbose ? SECLEN : 32);
}

struct {
    int pfrom, pto;
    int printed;
    char *name;
} sletinfo[] = {
    0x01,   0x12,   FALSE, "DUP",
    0x1F,   0x39,   FALSE, "Fortran",
    0x51,   0x5C,   FALSE, "Cobol",
    0x6E,   0x74,   FALSE, "Supervisor",
    0x78,   0x84,   FALSE, "Core Load Builder",
    0x8C,   0x8C,   FALSE, "Sys 1403 prt",
    0x8D,   0x8D,   FALSE, "Sys 1132 prt",
    0x8E,   0x8E,   FALSE, "Sys console prt",
    0x8F,   0x8F,   FALSE, "Sys 2501 rdr",
    0x90,   0x90,   FALSE, "Sys 1442 rdr/pun",
    0x91,   0x91,   FALSE, "Sys 1134 paper tape",
    0x92,   0x92,   FALSE, "Sys kbd",
    0x93,   0x93,   FALSE, "Sys 2501/1442 conv",
    0x94,   0x94,   FALSE, "Sys 1134 conv",
    0x95,   0x95,   FALSE, "Sys kbd conv",
    0x96,   0x96,   FALSE, "Sys diskz",
    0x97,   0x97,   FALSE, "Sys disk1",
    0x98,   0x98,   FALSE, "Sys diskn",
    0x99,   0x99,   FALSE, "(primary print)",
    0x9A,   0x9A,   FALSE, "(primary input)",
    0x9B,   0x9B,   FALSE, "(primary input excl kbd)",
    0x9C,   0x9C,   FALSE, "(primary sys conv)",
    0x9D,   0x9D,   FALSE, "(primary conv excl kbd)",
    0xA0,   0xA1,   FALSE, "Core Image Loader",
    0xB0,   0xCC,   FALSE, "RPG",
    0xCD,   0xCE,   FALSE, "Dup Part 2",
    0xCF,   0xF6,   FALSE, "Macro Assembler",
    0
};

void dump_slet (void)
{
    int i, j, iphase, nsecs, sec, max_sec = 0;
    char sstr[16], *smark;

    showmajor("Sectors 3-5 - SLET");
    for (i = 0; i < 3; i++) {
        getsector(3+i, buf);
        memmove(((WORD *) slet)+SECLEN*i, buf, SECLEN*sizeof(WORD));
    }

    printf("#   PHID      Addr  Len Sector        Secs\n");
    printf("------------------------------------------\n");
    for (i = 0; i < SLETLEN; i++) {
        if (slet[i].phid == 0)
            break;

        sec    = slet[i].sector;
        iphase = (int) (signed short) slet[i].phid;
        nsecs  = (slet[i].nwords + SECLEN-1)/SECLEN;

        if (sec & 0xF800) {
            smark = "*";
            sec  &= 0x7FF;
        }
        else
            smark = " ";

        for (j = 0; sletinfo[j].pfrom != 0; j++)
            if (sletinfo[j].pfrom <= iphase && sletinfo[j].pto >= iphase)
                break;

        sprintf(sstr, "(%d.%d)", sec / DSK_SECCYL, slet[i].sector % DSK_SECCYL);

        printf("%3d %04x %4d %04x %04x %04x %s %-7s %3x",
            i, slet[i].phid, iphase, slet[i].addr, slet[i].nwords, slet[i].sector, smark, sstr, nsecs);

        if (iphase < 0)
            iphase = -iphase;

        if (sletinfo[j].pfrom == 0)
            printf(" ???");
        else if (! sletinfo[j].printed) {
            printf(" %s", sletinfo[j].name);
            sletinfo[j].printed = TRUE;
        }

        for (j = 0; j < i; j++) {
            if (sec == (slet[j].sector & 0x7FF)) {
                printf(" (same as %04x)", slet[j].phid);
                break;
            }
        }

        max_sec = MAX(max_sec, sec+nsecs-1);        // find last sector used

        putchar('\n');

        if (i >= 15 && ! verbose) {
            printf("...\n");
            break;
        }
    }
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

int ebcdic_to_ascii (int ch)
{
    int j;

    for (j = 32; j < 128; j++)
        if (ascii_to_ebcdic_table[j] == ch)
            return j;

    return '?';
}

#define HDR_LEN 120

void dump_hdng(void)
{
    int i;
    char str[HDR_LEN+1], *p = str;

    showmajor("Sector 7 - Heading");
    getsector(7, buf);

    for (i = 0; i < (HDR_LEN/2); i++) {
        *p++ = ebcdic_to_ascii((buf[i] >> 8) & 0xFF);
        *p++ = ebcdic_to_ascii( buf[i]       & 0xFF);
    }

    *p = '\0';
    trim(str);
    printf("%s\n", str);
}

BOOL mget (int offset, char *name)
{
    char title[80];

    if (dcom[offset] == 0)
        return FALSE;

    getsector(dcom[offset], buf);
    sprintf(title, "Sector %x - %s", dcom[offset], name);
    showmajor(title);
    return TRUE;
}

void dump_scra (void)
{
    if (! mget(SCRA, "SCRA"))
        return;

    dump(verbose ? SECLEN : 32);
}

void dump_let (void)
{
    if (! mget(ULET, "LET"))
        return;
}

void dump_flet (void)
{
    if (! mget(FLET, "FLET"))
        return;
}

void dump_cib (void)
{
    if (! mget(CIBA, "CIB"))
        return;

    dump(verbose ? SECLEN : 32);
}

#define LFHD 5    // WORD COUNT OF LET/FLET HEADER    PMN09970
#define LFEN 3    // NO OF WDS PER LET/FLET ENTRY     PMN09980
#define SCTN 0    // RLTY ADDR OF LET/FLET SCTR NO.   PMN09990
#define UAFX 1    // RLTV ADDR OF SCTR ADDR OF UA/FXA PMN10000
#define WDSA 3    // RLTV ADDR OF WDS AVAIL IN SCTR   PMN10010
#define NEXT 4    // RLTV ADDR OF ADDR NEXT SCTR      PMN10020
#define LFNM 0    // RLTV ADDR OF LET/FLET ENTRY NAME PMN10030
#define BLCT 2    // RLTV ADDR OF LET/FLET ENTRY DBCT PMN10040

void bail (char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, fmt, args);
    va_end(args);
    putchar('\n');

    exit(1);
}

// ---------------------------------------------------------------------------------
// trim - remove trailing whitespace from string s
// ---------------------------------------------------------------------------------

char *trim (char *s)
{
    char *os = s, *nb;

    for (nb = s-1; *s; s++)
        if (*s > ' ')
            nb = s;

    nb[1] = '\0';
    return os;
}

/* ------------------------------------------------------------------------ 
 * lowcase - force a string to lowercase (ASCII)
 * ------------------------------------------------------------------------ */

char *lowcase (char *str)
{
    char *s;

    for (s = str; *s; s++) {
        if (*s >= 'A' && *s <= 'Z')
            *s += 32;
    } 

    return str;
}

