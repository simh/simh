// -------------------------------------------------------------------------------------------
// DISKLIST - print directory listing of DMS2 disk image
// -------------------------------------------------------------------------------------------

/*
 * (C) Copyright 2004, Brian Knittel.
 * You may freely use this program, but: it offered strictly on an AS-IS, AT YOUR OWN
 * RISK basis, there is no warranty of fitness for any purpose, and the rest of the
 * usual yada-yada. Please keep this notice and the copyright in any distributions
 * or modifications.
 */

// -------------------------------------------------------------------------------------------
// HISTORY
// -------------------------------------------------------------------------------------------
// 24-Nov-2004 Written. It would be nice to make this distinguish a DMS disk from
//                      other potential disk formats (e.g. APL\1130).
//
// 03-Dec-2004 Split get_let and print_let so we could get listings for specific files.
// 06-Dec-2004 Added printout of detailed file info (print_xxx_info) and dump, and listing of calls
// 21-Dec-2006 Added file type column in standard output mode
// -------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util_io.h"

// -------------------------------------------------------------------------------------------
// DEFINITIONS
// -------------------------------------------------------------------------------------------

typedef int            BOOL;                        // boolean
typedef unsigned short uint16;                      // unsigned 16-bit integer
typedef short          int16;                       // signed   16-bit integer

#define TRUE  1                                     // BOOL values
#define FALSE 0

#define ALLOCATE(obj) ((obj *) calloc(1, sizeof(obj)))      // macro to allocate an object and return pointer to it

#define SEC_WORDS       320                         // useful words per sector
#define SEC_BYTES       640                         // bytes per sector
#define PHY_WORDS       321                         // physical words per sector
#define SLET_LENGTH     160                         // size of SLET (2 sectors of 4 words per entry)
#define SEC_BLOCKS       16                         // disk blocks per sector
#define BLK_WORDS        20                         // size of a "disk block", a sub-sector
#define BLK_BYTES        40

#define THOUSANDS_SEP ','                           // thousands separator (eg. 9,999,999)
                                                    // (in Europe, define as '.', or comment out definition)

typedef struct tag_letentry {                       // linked list node for directory entry
    struct tag_letentry *next;
    char name[6];                                   // file name (1-5 chars)
    uint16 filetype;                                // file image type
    uint16 dbcount;                                 // length in DMS "disk blocks" (20 words per block)
    uint16 dbaddr;                                  // disk block address
    struct tag_letentry *master;                    // master entry, if this is an alternate name entry
    BOOL dummy;                                     // TRUE if this is a 1DUMY entry
} LETENTRY;

typedef struct tag_filearg {                        // node in linked list of filename arguments
    struct tag_filearg *next;
    char *name;
} FILEARG;

static char *progtype_nm[16] = {                    // DMS2 program types for DSF format files
    "Undefined",        "Mainline, absolute",   "Mainline, relocatable",    "LIBF Subprogram",
    "CALL Subprogram",  "LIBF Interrupt Service Subroutine (ISS)",          
                        "CALL Interrupt Service Subroutine (ISS)",  "Interrupt Level Subroutine (ILS)",
    "Undefined",        "Undefined",            "Undefined",                "Undefined",
    "Undefined",        "Undefined",            "Undefined",                "Undefined"
};

struct {                                            // DMS2 program subtypes for DSF format files
    unsigned progtype;
    unsigned subtype;
    char *descr;
} subtype_nm[] = {
    3, 0, "In-core subprogram",
    3, 1, "FORTRAN Disk IO subroutine",
    3, 2, "Arithmetic subroutine",
    3, 3, "FORTRAN non-disk IO and \"Z\" conversion subroutine",
    5, 3, "\"Z\" device suboutine",
    5, 0, NULL,                                     // NULL suppresses printing of subtype
    4, 0, "In-core subprogram",
    4, 8, "Function subprogram",
    7, 1, "Dummy ILS02 or ILS04",
};
#define N_SUBTYPE_NMS (sizeof(subtype_nm) / sizeof(subtype_nm[0]))

#pragma pack(1)

typedef struct tag_dsf_program_header {             // header block of a DSF format file, disk layout
    uint16      zero1;
    uint16      checksum;
    uint16      type;
    uint16      proglen;                // effective length, terminal address
    uint16      commonlen;              // length of common
    uint16      hdr_len9;               // length of this header - 9
    uint16      zero2;
    uint16      dblen;                  // length of program including header in disk blocks (20 wds)
    uint16      fortran_info;
    union {
        struct {                        // normal programs: entry point information. 1-15. Actual number is hdr_len9/3
            uint16  name[2];
            uint16  addr;
        } entry[15];
        struct {                        // ISS (types 5 and 6)
            uint16  name[2];
            uint16  addr;
            uint16  iss_50;             // ISS number + 50
            uint16  issnumber;          // ISS number
            uint16  nlevels;            // # of levels required (1 or 2)
            uint16  level[2];           // interrupt level associated with interrupt (nlevels entries used)
        } iss;
        struct {                        // ILS (type 7)
            uint16  name[2];
            uint16  addr;
            uint16  level;              // interrupt level
        } ils;
    } x;
} DSF_PROGRAM_HEADER;

typedef struct tag_dci_program_header {         // header of a DCI (core image) format file, disk layout
    uint16  xeqa;                       // execute address
    uint16  cmon;                       // length of COMMON
    uint16  dreq;                       // disk IO indicator, /FFFF for DISKZ, 0000 for DISK1, 0001 for DISKN
    uint16  file;                       // number of files defined
    uint16  hwct;                       // length of core image header in words
    uint16  lsct;                       // sector count of files in system WS
    uint16  ldad;                       // loading address of core load
    uint16  xctl;                       // exit control address for DISK1/N
    uint16  tvwc;                       // length of transfer vector in words
    uint16  wcnt;                       // length, in words of the core load, core image header and transfer vector
    uint16  xr3x;                       // setting for index register 3 during execution of core load
    uint16  itv[6];                     // ITV (values of words 8-13 during execution)
    uint16  reserved1;
    uint16  ibt[8];                     // IBT for ILS04. interrupt entry for ISS of 1231 (3 words), 1403, 2501, 1442, keyboard/prt, 1134/1055 respectively
    uint16  ovsw;                       // sector count of LOCALs/SOCALs
    uint16  core;                       // core size of system on which core load was built
    uint16  reserved2[2];
    uint16  hend;                       // last word of header
} DCI_PROGRAM_HEADER;

#pragma pack()

// -------------------------------------------------------------------------------------------
// GLOBALS
// -------------------------------------------------------------------------------------------

#define FILETYPE_DSF        0                       // DMS2 filetypes. Type 1 is undefined
#define FILETYPE_1          1
#define FILETYPE_DCI        2
#define FILETYPE_DDF        3

uint16 defective[3] = {0xFFFF, 0xFFFF, 0xFFFF};     // defective cylinder table, number is 1st sector number of bad cylinder
FILE *fd;                                           // stream for open disk image file
BOOL verbose = FALSE;                               // verbose switch
BOOL show_all = FALSE;                              // switch to display alternate file entries
BOOL dumpslet = FALSE;                              // dump SLET switch
BOOL do_dump = FALSE;
char *ftname[4] = {"DSF", "???", "DCI", "DDF"};     // DMS2 filetype names
    
LETENTRY *flet = NULL, *let = NULL;                 // pointers to contents of FLET and LET

#pragma pack(1)                                     // (don't pad struct elements)

struct {                                            // buffer for one sector
    uint16 secno;
    uint16 data[SEC_WORDS];
} sector;

struct {                                            // structure of the SLET on disk
    uint16  id;
    uint16  addr;
    uint16  size;
    uint16  secno;
} slet[SLET_LENGTH];

struct {                                            // DCOM sector
    uint16  _dummy0;
    uint16  _dummy1;
    uint16  _dummy2;
    uint16  _dummy3;
    uint16  name[2];                                //   4  name of program
    uint16  dbct;                                   //   6  disk block count of program
    uint16  fcnt;                                   //   7  files indicator
    uint16  sysc;                                   //   8  system cartridge indicator switch
    uint16  jbsw;                                   //   9  temporary job switch (nonzero = JOB T)
    uint16  cbsw;                                   //  10  clb switch (nonzero = storeci)
    uint16  lcnt;                                   //  11  local indicator (# of locals)
    uint16  mpsw;                                   //  12  core map desired switch
    uint16  mdf1;                                   //  13  no. of dup ctrl rcds (modif)
    uint16  mdf2;                                   //  14  addr of modif buffer
    uint16  ncnt;                                   //  15  nocal indicator
    uint16  enty;                                   //  16  rel entry addr of program
    uint16  rp67;                                   //  17  1442-5 switch (nonzero = mod 6 or 7)
    uint16  todr;                                   //  18  -to- wk stg drive code
    uint16  frdr;                                   //  19  -from- wk stg drive code
    uint16  fhol;                                   //  20  addr of largest hole in fxa
    uint16  fsze;                                   //  21  blk cnt largest hole in fxa
    uint16  uhol;                                   //  22  addr of largest hole in ua
    uint16  usze;                                   //  23  blk cnt largest hole in ua
    uint16  dcsw;                                   //  24  dup call switch
    uint16  piod;                                   //  25  principal IO device indicator
    uint16  pptr;                                   //  26  print print device indicator
    uint16  ciad;                                   //  27  sctr 0 loc of cil sctr addr
    uint16  cain;                                   //  28  avail cartridge indicator
    uint16  grph;                                   //  29  2250 indicator
    uint16  gcnt;                                   //  30  g2260 count
    uint16  locw;                                   //  31  local call locals sw
    uint16  x3sw;                                   //  32  special ils sw
    uint16  ecnt;                                   //  33  equat count
    uint16  _dummy34;                               //  34
    uint16  andu[5];                                //  35  end of UA address (adj)
    uint16  bndu[5];                                //  40  end of UA address (base)
    uint16  fpad[5];                                //  45  file protected address
    uint16  pcid[5];                                //  50  available cartridge IDs (physical drvs 0..4)
    uint16  cidn[5];                                //  55  cartridge ID            (logical drvs 0..4)
    uint16  ciba[5];                                //  60  sector address of CIB
    uint16  scra[5];                                //  65  sector address of SCRA
    uint16  fmat[5];                                //  70  format of program in WS
    uint16  flet[5];                                //  75  FLET sector address
    uint16  ulet[5];                                //  80  LET sector address
    uint16  wsct[5];                                //  85  BLK count of program in WS
    uint16  cshn[5];                                //  90  1+sctr addr of end of cusn.
} dcom;

#pragma pack()

BOOL is_system = FALSE;                                 // TRUE if this is a system disk

// NOTE: in DMS, disk blocks are 1/16 of a sector (20 words) -- DMS suballocates sectors for files. Some
// files have to start on a sector boundary, and there are 1DUMY entries for the little lost bits. The last
// 1DUMY entry in the LET is the size of Working Storage.

// -------------------------------------------------------------------------------------------
// PROTOTYPES
// -------------------------------------------------------------------------------------------

void getsec (uint16 secno);                             // read sector by number
void getdata (void *buf, uint16 dbaddr, uint16 offset, uint16 nwords);  // read data from file relative to its disk block address
void bail (char *msg);                                  // print error message and exit
void print_slet (void);                                 // print contents of SLET
void get_let (LETENTRY **listhead, uint16 secno);       // read FLET or LET, building linked list
void print_let (char *title, LETENTRY *listhead);       // print contents of FLET or LET
void list_named_files (char *name, char *image) ;       // print info for specified file(s)
void print_onefile (LETENTRY *entry, BOOL in_flet);     // print detailed info about one particular file
int  ebcdic_to_ascii (int ch);                          // convert EBCDIC character to ASCII
void convert_namecode (uint16 *namecode, char *name);   // convert DMS name code words into ASCII filename
char *upcase (char *str);                               // convert string to upper case
void commas (int n, int width);                         // print number n with commas
char *astring (char *str);                              // allocate memory for and return copy of string
void print_dsf_info (LETENTRY *entry);                  // print information about a Disk System Format file
void print_dci_info (LETENTRY *entry);                  // print information about a Disk Core Image file
void print_ddf_info (LETENTRY *entry);                  // print information about a Disk Data Format file
char * file_progtype (LETENTRY *entry);                 // description of module type

// -------------------------------------------------------------------------------------------
// main - main routine
// -------------------------------------------------------------------------------------------

int main (int argc, char **argv)
{
    int i;
    char *arg, cartid[10];
    char *image = NULL;
    FILEARG *fileargs = NULL, *filearg, *filearg_tail = NULL;
    char *usestr = "Usage: disklist [-sadv] diskfile [filename ...]\n"
                   "\n"
                   "Lists contents of fixed and user area directories in IBM 1130 DMS 2\n"
                   "disk image file \"diskfile\". With the optional filename argument(s)\n"
                   "(1-5 letters), prints detailed information about the named file(s).\n"
                   "Wildcard characters ? and * may be specfied in the filename."
                   "\n"
                   "  -s  dump SLET in addition to fixed and user areas\n"
                   "  -a  dump additional information including alternate entries and addresses\n"
                   "      For named file(s), prints information about entry points and calls\n"
                   "  -d  dumps contents of named file(s) in hex\n"
                   "  -v  verbose mode, prints internal information\n";

    for (i = 1; i < argc; i++) {                            // scan command line arguments
        arg = argv[i];
        if (*arg == '-') {                                  // command line switch
            arg++;                                          // skip over the -
            while (*arg) {                                  // process all flags
                switch (*arg++) {
                    case 'v':
                        verbose = TRUE;                     // -v turns on verbose mode
                        break;
                    case 'a':
                        show_all = TRUE;                    // -a turns on listing of alternate file entries & pad spaces
                        break;
                    case 's':                               // -s turns on dump slet
                        dumpslet = TRUE;
                        break;
                    case 'd':
                        do_dump = TRUE;
                        break;
                    default:
                        bail(usestr);                       // unrecognized flag
                }
            }
        }
        else if (image == NULL)
            image = arg;                                    // first name is the name of disk image file
        else {
            filearg = ALLOCATE(FILEARG);                    // subsequent names are filename arguments,
            filearg->name = upcase(astring(arg));           // copy to a FILEARG object (as uppercase)
            filearg->next = NULL;

            if (fileargs == NULL)                           // add to end of linked list
                fileargs = filearg;
            else
                filearg_tail->next = filearg;

            filearg_tail = filearg;
        }
    }

    if (image == NULL)                                      // filename was not specified
        bail(usestr);

    if ((fd = fopen(image, "rb")) == NULL) {                // open file in binary mode
        perror(image);                                      // print reason for open failure and exit
        return 1;
    }

    getsec(0);                                              // get sector 0, which has defective cylinder table

    defective[0] = sector.data[0];                          // load defective cylinder table
    defective[1] = sector.data[1];
    defective[2] = sector.data[2];

    if (verbose)
        printf("Defective cylinder table: %04x %04x %04x\n", defective[0], defective[1], defective[2]);

    printf("Filename: %s", image);

    sprintf(cartid, "%04x", sector.data[3]);                // display cartridge ID in upper case
    printf("   Cartridge ID: %s", upcase(cartid));
    if (show_all)
        printf("   Copy: number %d", sector.data[4]);

    printf("\n\n");

    getsec(1);                                              // get sector 1, save to DCOM
    memcpy(&dcom, sector.data, min(sizeof(dcom), SEC_BYTES));

    is_system = dcom.sysc != 0;                             // is this a system cartridge?

    if (dumpslet) {                                         // display SLET
        if (is_system) {
            getsec(3);
            memcpy(slet+0,  sector.data, SEC_BYTES);
            getsec(4);
            memcpy(slet+80, sector.data, SEC_BYTES);

            print_slet();
        }
        else
            printf("(Not a system cartridge, no SLET)\n\n");
    }

    if (dcom.flet[0] != 0)                                  // do we have a FLET?
        get_let(&flet, dcom.flet[0]);                       // read it

    get_let(&let, dcom.ulet[0]);                            // read LET

    if (fileargs == NULL) {                                 // if there are no filename arguments
        if (flet != NULL)                                   // print FLET and LET
            print_let("FIXED AREA", flet);

        print_let("USER AREA", let);
    }
    else {                                                  // print information for specified file(s)
        for (filearg = fileargs; filearg != NULL; filearg = filearg->next)
            list_named_files(filearg->name, image);
    }

    return 0;
}

// -------------------------------------------------------------------------------------------
// upcase - force a string to uppercase (ASCII)
// -------------------------------------------------------------------------------------------

char *upcase (char *str)
{
    char *s;

    for (s = str; *s; s++) {
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
    } 

    return str;
}

// -------------------------------------------------------------------------------------------
// commas - print n as a decimal number with commas; width is minimum width
// -------------------------------------------------------------------------------------------

void commas (int n, int width)
{
    char fmt[20];
#ifdef THOUSANDS_SEP
    int nchar;
    char tmp[20], *cin, *cout;

    sprintf(tmp, "%d", n);                  // format number n into string
    nchar = strlen(tmp);                    // get length of string

    for (cin = tmp, cout = fmt; *cin; ) {   // scan through the formatted number
        *cout++ = *cin++;                   // output digit
        --nchar;                            // get number of digits left
        if (nchar > 0 && (nchar % 3) == 0)
            *cout++ = THOUSANDS_SEP;        // if there is a multiple of three digits left, emit a comma
    }
    *cout = '\0';                           // terminate string

#else                                       // THOUSANDS_SEP is undefined, output number w/o commas

    sprintf(fmt, "%d", n);

#endif

    width -= strlen(fmt);                   // get width shortage
    while (--width >= 0)                    // output spaces if necessary
        putchar(' ');

    fputs(fmt, stdout);                     // print formatted number
}

// -------------------------------------------------------------------------------------------
// bail - print fatal error message and exit
// -------------------------------------------------------------------------------------------

void bail (char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

// -------------------------------------------------------------------------------------------
// getsec - read desired sector
// -------------------------------------------------------------------------------------------

void getsec (uint16 secno)
{
    int i, phys_sec;
    static uint16 cur_sec = 0xFFFF;

    if (secno == cur_sec)                                   // see if we already have the sector. Presumes
        return;                                             // we haven't modified its contents!

    cur_sec = secno;                                        // remember current sector

    phys_sec = secno;                                       // physical sector

    for (i = 0; i < 3; i++) {                               // bump cylinder if it's past any in the defective cylinder list
        if (secno >= defective[i])                          // (use logical secno for comparisons, not physical secno)
            phys_sec += 8;
        else
            break;
    }

    fseek(fd, phys_sec*PHY_WORDS*2, SEEK_SET);              // jump to translated sector

    if (fxread(&sector, 2, PHY_WORDS, fd) != PHY_WORDS)     // fxread handles flipping data on little-endian machines
        bail("error reading disk image");

    if (sector.secno != secno) {                            // verify that 1st word in sector is sector number
        fprintf(stderr, "* expected sector number /%04x, got /%04x\n", secno, sector.secno);
        bail("disk image is corrupt");
    }
}

// -------------------------------------------------------------------------------------------
// getdata -- read data from file relative to its disk block address
// -------------------------------------------------------------------------------------------

void getdata (void *buf, uint16 dbaddr, uint16 offset, uint16 nwords)
{
    uint16 secno, nsec, nw;

    if (nwords == 0)
        return;

    secno   = dbaddr / SEC_BLOCKS;                      // desired sector number from dbaddr
    dbaddr -= SEC_BLOCKS*secno;                         // # of blocks offset within that sector
    offset += dbaddr*BLK_WORDS;                         // add offset in words

    nsec    = offset / SEC_WORDS;                       // turn offset into integer sectors
    secno  += nsec;                                     // bump sector number
    offset -= nsec*SEC_WORDS;                           // ultimate offset within sector

    for (;;) {
        getsec(secno);                                  // read desired sector
        nw = min(SEC_WORDS-offset, nwords);             // number of words to copy from this sector
        memcpy(buf, &sector.data[offset], nw*2);        // copy the data

        if ((nwords -= nw) <= 0)                        // decrement remaining word count
            break;

        secno++;                                        // bump sector
        offset = 0;                                     // no offset in subsequent sector(s)
        ((uint16 *) buf) += nw;                         // bump buffer pointer
    }
}

// -------------------------------------------------------------------------------------------
// print_slet - list the contents of the SLET
// -------------------------------------------------------------------------------------------

struct {
    unsigned int id;
    char *name;
} slet_phase[] = {
    0x01, "@DDUP DUPCO *** DUP",                            // DMS R1V12 phase ID's and names
    0x02, "@DCTL DUP CONTROL - PART 1",
    0x03, "@STOR STORE",
    0x04, "@FILQ FILE EQUATE",
    0x05, "@DUMP DUMP",
    0x06, "@DL/F DUMP LET/FLET",
    0x07, "@DLTE DELETE",
    0x08, "@DFNE DEFINE",
    0x09, "@EXIT DEXIT",
    0x0A, "@CFCE CARD INTERFACE",
    0x0B, "@DU11 KEYBOARD INTERFACE",
    0x0C, "@DU12 PAPER TAPE INTERFACE",
    0x0D, "@DU13 DUP UPCOR",
    0x0E, "@DU14 DUP PRINCIPAL I/O",
    0x0F, "@DU15 DUP PRINCIPAL I/O SANS KB",
    0x10, "@DU16 DUP PAPER TAPE I/O",
    0x11, "@PRCI PRE CORE IMAGE",
    0x12, "@DU18 DUP RESERVED",
    0x1F, "@FR01 INPUT *** FORTRAN COMPILER",
    0x20, "@FR02 CLASSIFIER",
    0x21, "@FR03 CHECK ORDER/STMNT NUMBER",
    0x22, "@FR04 COMMON/SUBROUTINE OR FUNC",
    0x23, "@FR05 DIM/REAL, INTEGER, EXTERNAL",
    0x24, "@FR06 REAL CONSTANTS",
    0x25, "@FR07 DEFN FILE, CALL LINK/EXIT",
    0x26, "@FR08 VARIABLES AND STMNT FUNC",
    0x27, "@FR09 DATA STATEMENT",
    0x28, "@FR10 FORMAT",
    0x29, "@FR11 SUBSCRIPT DECOMPOSITION",
    0x2A, "@FR12 ASCAN I",
    0x2B, "@FR13 ASCAN II",
    0x2C, "@FR14 DO, CONTINUE, ETC",
    0x2D, "@FR15 SUBSCRIPT OPTIMIZE",
    0x2E, "@FR16 SCAN",
    0x2F, "@FR17 EXPANDER I",
    0x30, "@FR18 EXPANDER II",
    0x31, "@FR19 DATA ALLOCATION",
    0x32, "@FR20 COMPILATION ERRORS",
    0x33, "@FR21 STATEMENT ALLOCATION",
    0x34, "@FR22 LIST STATEMENT ALLOCATION",
    0x35, "@FR23 LIST SYMBOLS",
    0x36, "@FR24 LIST CONSTANTS",
    0x37, "@FR25 OUTPUT I",
    0x38, "@FR26 OUTPUT II",
    0x39, "@FR27 RECOVERY",
    0x3A, "DUMMY DUMMY NAME",
    0x3B, "DUMMY DUMMY NAME",
    0x3C, "DUMMY DUMMY NAME",
    0x51, "@QCTL PROCESS CTL CDS *** COBOL COMPILER ",
    0x52, "@QTXT SOURCE TEXT REDUCTION",
    0x53, "@QLIT LITERAL ALLOCATION",
    0x54, "@QDTA DATA DIVISION PROCESSING",
    0x55, "@QPRO PROCEDURE DIV SCAN",
    0x56, "@QGEN GENERATE INST STRINGS",
    0x57, "@QOBJ PRODUCE DSF-MODULE",
    0x58, "@QERR MAP/DIAGNOSTIC OUTPUT",
    0x59, "@QEND COMPILE TERMINATION",
    0x5A, "@QSER PRODUCE SERVICEABILITY",
    0x5B, "@QXR1",
    0x5C, "@QXR2",
    0x6E, "@SUP1 MONITOR CTRL RCD ANALYZER *** SUPERVISOR",
    0x6F, "@SUP2 JOB RECORD PROCESSING",
    0x70, "@SUP3 DELETE TEMPOTARY LET",
    0x71, "@SUP4 XEQ RECORD PROCESSING",
    0x72, "@SUP5 SCR PROCESSING",
    0x73, "@SUP6 SYSTEM DUMP PROGRAM",
    0x74, "@SUP7 AUXILIARY SUPERVISOR",
    0x78, "@CLB1 PHASE 1 *** CORE LOAD BUILDER",
    0x79, "@CLB2 PHASE 2",
    0x7A, "@CLB3 PHASE 3",
    0x7B, "@CLB4 PHASE 4",
    0x7C, "@CLB5 PHASE 5",
    0x7D, "@CLB6 PHASE 6",
    0x7E, "@CLB7 PHASE 7",
    0x7F, "@CLB8 PHASE 8",
    0x80, "@CLB9 PHASE 9",
    0x81, "@CLBA PHASE 10",
    0x82, "@CLBB PHASE 11",
    0x83, "@CLBC PHASE 12",
    0x84, "@CLBD PHASE 13 (GRAPHICS)",
    0x8C, "@1403 1403 SUBR *** SYSTEM DEVICE DRIVERS",
    0x8D, "@1132 1132 SUBR",
    0x8E, "@CPTR CONSOLE PRINTER SUBR",
    0x8F, "@2501 2501 SUBR",
    0x90, "@1442 1442 SUBR",
    0x91, "@1134 1134 SUBR",
    0x92, "@KBCP KB/CONSOLE PRINTER SUBR",
    0x93, "@CDCV 2501/1442 CONVERSION SUBR",
    0x94, "@PTCV 1134 CONVERSION SUBR",
    0x95, "@KBCV KB/CP CONVERSION SUBR",
    0x96, "@DZID DISKZ",
    0x97, "@D1ID DISK1",
    0x98, "@DNID DISKN",
    0x99, "@PPRT PRINCIPAL PRINT SUBROUTINE",
    0x9A, "@PIWK PRINCIPAL INPUT SUBROUTINE",
    0x9B, "@PIXK PRINCIPAL INPUT W/O KB",
    0x9C, "@PCWK PRINCIPAL CONV W/ KEYBOARD",
    0x9D, "@PCXK PRINCIPAL CONV W/O KEYBOARD",
    0xA0, "@CIL1 PHASE 1 *** CORE IMAGE LOADER",
    0xA1, "@CIL2 PHASE 2",
    0xB0, "@RG00 PHASE 0 *** RPG COMPILER",
    0xB1, "@RG02 PHASE 2",
    0xB2, "@RG04 PHASE 4",
    0xB3, "@RG06 PHASE 6",
    0xB4, "@RG08 PHASE 8",
    0xB5, "@RG10 PHASE 10",
    0xB6, "@RG12 PHASE 12",
    0xB7, "@RG14 PHASE 14",
    0xB8, "@RG16 PHASE 16",
    0xB9, "@RG17 PHASE 17",
    0xBA, "@RG19 PHASE 19",
    0xBB, "@RG20 PHASE 20",
    0xBC, "@RG21 PHASE 21",
    0xBD, "@RG22 PHASE 22",
    0xBE, "@RG24 PHASE 24",
    0xBF, "@RG26 PHASE 26",
    0xC0, "@RG28 PHASE 28",
    0xC1, "@RG32 PHASE 32",
    0xC2, "@RG34 PHASE 34",
    0xC3, "@RG36 PHASE 36",
    0xC4, "@RG38 PHASE 38",
    0xC5, "@RG40 PHASE 40",
    0xC6, "@RG42 PHASE 42",
    0xC7, "@RG44 PHASE 44",
    0xC8, "@RG46 PHASE 46",
    0xC9, "@RG52 PHASE 52",
    0xCA, "@RG54 PHASE 54",
    0xCB, "@RG58 PHASE 58",
    0xCC, "@RG60 PHASE 60",
    0xCD, "@DCL2 *** DUP CONTROL - PART 2",
    0xCE, "@DMUP MACRO UPDATE PROGRAM",
    0xCF, "@AS00 PHASE 0 *** ASSEMBLER",
    0xD0, "@ACNV CARD CONVERSION",
    0xD1, "@AS10 PHASE 10",
    0xD2, "@AS11 PHASE 11",
    0xD3, "@AS12 PHASE 12",
    0xD4, "@AERM ERROR MESSAGES",
    0xD5, "@AS01 PHASE 1",
    0xD6, "@AS1A PHASE 1A",
    0xD7, "@ASYM SYSTEM SYMBOL TABLE",
    0xD8, "@AS03 PHASE 3",
    0xD9, "@AS04 PHASE 4",
    0xDA, "@AS02 PHASE 2",
    0xDB, "@AS2A PHASE 2A",
    0xDC, "@AS09 PHASE 9",
    0xDD, "@AS05 PHASE 5",
    0xDE, "@AS06 PHASE 6",
    0xDF, "@AS07 PHASE 7",
    0xE0, "@AS7A PHASE 7A",
    0xE1, "@AS08 PHASE 8",
    0xE2, "@AS8A PHASE 8A",
    0xE3, "@APCV CARD PUNCH CONVERSION",
    0xE4, "@AINT INTERMEDIATE DISK OUTPT",
    0xE5, "@ASAA PHASE 10A",
    0xE6, "@ASGR PHASE 13 GRAPHICS",
    0xE7, "@ADIV DIVISION OPERATOR",
    0xE8, "@AMCC MACRO CONTROL CARDS III",
    0xE9, "@AM01 MACRO PHASE 1",
    0xEA, "@AM1A MACRO PHASE 1A",
    0xEB, "@AM1B MACRO PHASE 1B",
    0xEC, "@AM02 MACRO PHASE 2",
    0xED, "@AM2A MACRO PHASE 2A",
    0xEE, "@AM2B MACRO PHASE 2B",
    0xEF, "@AM03 MACRO PHASE 3",
    0xF0, "@AM3A MACRO PHASE 3A",
    0xF1, "@AM3B MACRO PHASE 3B",
    0xF2, "@AX01 CROSS REF - PART 1",
    0xF3, "@AX2A CROSS REF - PART 2A",
    0xF4, "@AX2B CROSS REF - PART 2B",
    0xF5, "@AX2C CROSS REF - PART 2C",
    0xF6, "@AX03 CROSS REF - PART 3",
    0x100, "@AS00 *** MSP7 ASSEMBLER",
    0x101, "@ACNV",
    0x102, "@AS10",
    0x103, "@AS11",
    0x104, "@AS12",
    0x105, "@AERM",
    0x106, "@AS01",
    0x107, "@AS1A",
    0x108, "@ASYM",
    0x109, "@AS03",
    0x10A, "@AS04",
    0x10B, "@AS02",
    0x10C, "@AS2A",
    0x10D, "@AS09",
    0x10E, "@AS05",
    0x10F, "@AS06",
    0x110, "@AS07",
    0x111, "@AS7A",
    0x112, "@AS08",
    0x113, "@AS8A",
    0x114, "@APCV",
    0x115, "@AINT",
    0x116, "@ASAA",
    0x117, "@ASGR",
    0x118, "@ADIV",
    0x119, "@AMCC",
    0x11A, "@AM01",
    0x11B, "@AM1A",
    0x11C, "@AM1B",
    0x11D, "@AM02",
    0x11E, "@AM2A",
    0x11F, "@AM2B",
    0x120, "@AM03",
    0x121, "@AM3A",
    0x122, "@AM3B",
    0x123, "@AX01",
    0x124, "@AX2A",
    0x125, "@AX2B",
    0x126, "@AX2C",
    0x127, "@AX03",
    0x128, "@ASP7",
    0xFFFF, ""
};
#define N_SLET_PHASES (sizeof(slet_phase)/sizeof(slet_phase[0]) - 1)

void print_slet (void)
{
    int i, j = 0;

    printf("SLET (System Logical Equivalence Table)\n\n");  // dump SLET
    printf("ID   Addr Size Sect Description\n");
    printf("---- ---- ---- ---- -----------------------\n");

    for (i = 0; i < SLET_LENGTH; i++) {
        if (slet[i].id == 0 && slet[i].secno == 0)
            break;

        printf("%04x %04x %04x %04x ", slet[i].id, slet[i].addr, slet[i].size, slet[i].secno);

        while (j < N_SLET_PHASES && slet_phase[j].id < slet[i].id)
            j++;                                            // skip to entry in slet_phase name table

        printf("%s\n", (slet_phase[j].id == slet[i].id) ? slet_phase[j].name : "?");
    }

    putchar('\n');
}

// -------------------------------------------------------------------------------------------
// ebcdic_to_ascii - convert EBCDIC character to ASCII (ignores controls)
// -------------------------------------------------------------------------------------------

int ebcdic_to_ascii (int ch)
{
    int j;
    static int ascii_to_ebcdic_table[128] = {
    //
        0x00,0x01,0x02,0x03,0x37,0x2d,0x2e,0x2f, 0x16,0x05,0x25,0x0b,0x0c,0x0d,0x0e,0x0f,
    //
        0x10,0x11,0x12,0x13,0x3c,0x3d,0x32,0x26, 0x18,0x19,0x3f,0x27,0x1c,0x1d,0x1e,0x1f,
    //  spac !    "    #    $    %    &    '     (    )    *    +    ,    -    .    /
        0x40,0x5a,0x7f,0x7b,0x5b,0x6c,0x50,0x7d, 0x4d,0x5d,0x5c,0x4e,0x6b,0x60,0x4b,0x61,
    //  0    1    2    3    4    5    6    7     8    9    :    ;    <    =    >    ?
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7, 0xf8,0xf9,0x7a,0x5e,0x4c,0x7e,0x6e,0x6f,
    //  @    A    B    C    D    E    F    G     H    I    J    K    L    M    N    O
        0x7c,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7, 0xc8,0xc9,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,
    //  P    Q    R    S    T    U    V    W     X    Y    Z    [    \    ]    &    _
        0xd7,0xd8,0xd9,0xe2,0xe3,0xe4,0xe5,0xe6, 0xe7,0xe8,0xe9,0xba,0xe0,0xbb,0xb0,0x6d,
    //       a    b    c    d    e    f    g     h    i    j    k    l    m    n    o
        0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87, 0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
    //  p    q    r    s    t    u    v    w     x    y    z    {    |    }    ~
        0x97,0x98,0x99,0xa2,0xa3,0xa4,0xa5,0xa6, 0xa7,0xa8,0xa9,0xc0,0x4f,0xd0,0xa1,0x07,
    };

    for (j = 32; j < 128; j++)                      // look it up in table. (Of course if we constructed
        if (ascii_to_ebcdic_table[j] == ch)         // an ebcdic_to_ascii table we could just get the result directly)
            return j;

    return '?';
}

// -------------------------------------------------------------------------------------------
// convert_namecode - convert two-word name code into 5 character ASCII name
// -------------------------------------------------------------------------------------------

void convert_namecode (uint16 *namecode, char *name)
{
    unsigned long val;
    int i, ch;

    val = (namecode[0] << 16) | namecode[1];    // reconstruct the 30-bit code

    for (i = 0; i < 5; i++) {                   // scan for 5 letters
        ch = ((val >> 24) & 0x3F);              // pick up 6 bits at leftmost character position
        if (ch == 0)
            ch = ' ';                           // zero is a space
        else
            ch = ebcdic_to_ascii(ch | 0xC0);    // add assumed high bits and convert to ASCII
        
        name[i] = ch;                           // save it
        val <<= 6;                              // shift next character into position
    }

    while (--i >= 0)                            // back up to last nonblank character
        if (name[i] != ' ')
            break;      

    name[i+1] = '\0';                           // terminate string
}

// -------------------------------------------------------------------------------------------
// get_let - get FLET or LET, build into linked list
// -------------------------------------------------------------------------------------------

void get_let (LETENTRY **listhead, uint16 secno)
{
    uint16 seq, sec_addr, avail, chain, namecode[2], dbcount, addr;
    int i, nwords, filetype;
    char name[6];
    LETENTRY *head = NULL, *tail, *entry, *master = NULL;

    for (; secno != 0; secno = chain) { // scan through linked sectors
        getsec(secno);

        seq      = sector.data[0];      // relative sector number
        sec_addr = sector.data[1];      // sector address of FA or UA
        avail    = sector.data[3];      // available words in this sector
        chain    = sector.data[4];      // next sector number, 0 if this is the last

        if (seq == 0)                   // first time through, get starting address of FA or UA
            addr = sec_addr*16;         // (convert sector to "disk block")

        if (verbose)
            printf("  (sector %d, addr /%04x, next %04x)\n", seq, secno, chain);

        nwords = SEC_WORDS - 5 - avail; // number of words used by F/LET entries in this sector

        for (i = 5; nwords >= 3; ) {    // scan through entries
            filetype = (sector.data[i] >> 14) & 0x03;       // get file type: 0=DSF, 2=DCI, 3=data

            namecode[0] = sector.data[i] & 0x3FFF;          // get name
            namecode[1] = sector.data[i+1];
            convert_namecode(namecode, name);

            dbcount = sector.data[i+2];                     // get disk block count

            i += 3;                                         // advance index, decrement words-left count
            nwords -= 3;

            entry = ALLOCATE(LETENTRY);
            strcpy(entry->name, name);
            entry->next     = NULL;
            entry->filetype = filetype;
            entry->dbaddr   = addr;
            entry->dbcount  = dbcount;
            entry->dummy    = strcmp(entry->name, "1DUMY") == 0;

            if (dbcount == 0)
                entry->master = master;                     // this is an alternate entry for previous master entry
            else {
                entry->master = NULL;                       // this is a master entry
                master = entry;
            }

            if (head == NULL)
                head = entry;                               // first entry is head of linked list
            else
                tail->next = entry;                         // add to end of linked list

            tail = entry;

            addr += dbcount;                                // skip to next file
        }
    }

    *listhead = head;
}

// -------------------------------------------------------------------------------------------
// print_let - list FLET or LET
// -------------------------------------------------------------------------------------------

void print_let (char *title, LETENTRY *entry)
{
    int nblocks, nfiles, nalternates, nfree;                    // used to get total files, blocks in a directory
    static char *ftname[4] = {"DSF", "???", "DCI", "DDF"};      // filetype names

    nfiles      = 0;                                            // reset total counts
    nblocks     = 0;
    nalternates = 0;
    nfree       = 0;

    printf("%s\n\n", title);
    printf("Name  Type  Blocks%s\n", show_all ? " Addr Type" : "");
    printf("----- ----  ------%s\n", show_all ? " ---- --------------------------------------" : "");

    for (; entry != NULL; entry = entry->next) {
        if (entry->dummy) {                                     // this is an unused LET/FLET slot
            if (entry->next == NULL) {
                nfree = entry->dbcount;                         // last 1DUMY gives amount of free space
            }
            else {
                nblocks += entry-> dbcount;                     // in middle, it's a bit of space lost due to sector-padding

                if (show_all)           {                       // display padding entries in show_all mode
                    printf("%-5s %-3s ", "(pad)", "");
                    commas(entry->dbcount, 8);
                    printf("  %04x\n", entry->dbaddr);
                }
            }
        }
        else if (entry->dbcount > 0) {                          // if disk block count is nonzero, it's a file
            printf("%-5s %-3s", entry->name, ftname[entry->filetype]);
            commas(entry->dbcount, 8);
            if (show_all)
                printf("  %04x %s", entry->dbaddr, file_progtype(entry));
            putchar('\n');

            nblocks += entry->dbcount;                          // add to cumulative totals
            nfiles++;
        }
        else {                                                  // 0 blocks means it's an alternate entry for previous file
            if (show_all)
                printf("%-5s\n", entry->name);

            nalternates++;
        }
    }

    putchar('\n');                                              // double space after table

    printf("\nTotal: ");                                        // print summary
    commas(nfiles, 0);
    printf(" file%s", (nfiles == 1) ? "" : "s");
    if (show_all) {
        printf(", ");
        commas(nalternates, 0);
        printf(" entr%s", (nalternates == 1) ? "y" : "ies");
    }
    putchar('\n');

    printf("Space Used: ");
    commas(nblocks, 0);
    printf(" block%s, ", (nblocks == 1) ? "" : "s");
    commas(nblocks*BLK_WORDS, 0);
    printf(" words\n");

    printf("Space Free: ");
    commas(nfree, 0);
    printf(" block%s, ", (nfree == 1) ? "" : "s");
    commas(nfree*BLK_WORDS, 0);
    printf(" words\n\n");
}

// -------------------------------------------------------------------------------------------
// astring - allocate memory for an return copy of a string
// -------------------------------------------------------------------------------------------

char *astring (char *str)
{
    char *cpy;

    cpy = malloc(strlen(str)+1);
    strcpy(cpy, str);
    
    return cpy;
}

// -------------------------------------------------------------------------------------------
// matchname - see if filename matches (wildcard) file specification. We assume that both
// name and spec are uppercase.
// -------------------------------------------------------------------------------------------

BOOL matchname (char *name, char *spec)
{
    while (*name) {                             // scan through the filename
        if (*name == *spec || *spec == '?') {   // if exact match, or single-char ? match,
            name++;                             // so far so good; skip to next character
            spec++;
        }
        else if (*spec == '*') {                // we are at a * in the pattern. We need to try all possible
            while (*spec == '*')                // see if there are any more literal characters
                spec++;

            if (*spec == '\0')                  // no more literal pattern characters; this qualifies as a match
                return TRUE;

            // if we get here, we need to start matching the remaining part of the pattern against
            // some reduction of the name; we can skip 0 or more characters looking for a hit.
            // For example, if called with matchname("ABCDEF", "AB*F"), when we get here,
            // name = "CDEF" and spec = "F". What we need to do is start eating away at name to see
            // if it can be made to match the final F.

            while (*name) {
                if (matchname(name, spec))      // if the remaining part of the pattern matches the name,
                    return TRUE;                // it's a hit

                name++;                         // skip one character in name (the part matched by our *) and try again
            }

            return FALSE;                       // we skipped everything and still couldn't match the residual pattern
        }
        else
            return FALSE;                       // this is a definite mismatch
    }
                                                // we've hit the end of the actual filename
    while (*spec == '*')
        spec++;                                 // skip over any trailing * wildcards

    return *spec == '\0';                       // match is true if we're now at end of the pattern
}

typedef struct tag_namelist {
    struct tag_namelist *next;
    char name[6];
} NAMENODE, *NAMELIST;

NAMELIST free_nodes = NULL;

#define INDENT  "      "                    // indent for information lines
#define INDENT2 "          "                // indent for debugging lines

// -------------------------------------------------------------------------------------------
// add_list - add a name to a linked list of names, in alphabetical order
// -------------------------------------------------------------------------------------------

void add_list (char *name, NAMELIST *plisthead)
{
    NAMELIST n, prev;
    int cmp;

    for (n = *plisthead, prev = NULL; n != NULL; prev = n, n = n->next) {       // scan list. 'Prev' is trailing pointer
        if ((cmp = strcmp(n->name, name)) == 0)
            return;                                                 // name is already in the list

        if (cmp > 0)                                                // new entry goes before this entry
            break;
    }

    if (free_nodes == NULL)                                         // get a NAMELIST node from freelist or
        n = ALLOCATE(NAMENODE);                                     // by allocating more memory
    else {
        n = free_nodes;
        free_nodes = n->next;
    }

    strcpy(n->name, name);                                          // save the name

    if (prev == NULL) {                                             // add to head of list
        n->next = *plisthead;
        *plisthead = n;
    }
    else {                                                          // add to middle of list, after entry 'prev'
        n->next    = prev->next;
        prev->next = n;
    }
}

// -------------------------------------------------------------------------------------------
// print_list - print list of names
// -------------------------------------------------------------------------------------------

void print_list (NAMELIST list, char *title)
{
    int i;

    printf(INDENT "%-14s", title);                                  // print title string

    for (i = 0; list != NULL; list = list->next, i++) {             // print up to 8 names per line
        if (i == 8) {
            printf("\n" INDENT "%-14s", "");                        // (start a new line)
            i = 0;
        }
        printf("%s%s", (i == 0) ? "" : ", ", list->name);           // print names, separated by commas
    }

    putchar('\n');                                                  // terminate last line
}

// -------------------------------------------------------------------------------------------
// free_list - put list of names into the freelist for possible reuse
// -------------------------------------------------------------------------------------------

void free_list (NAMELIST list)
{
    NAMELIST n;

    if (free_nodes == NULL)                                         // freelist is empty, this list becomes the freelist
        free_nodes = list;
    else {
        for (n = free_nodes; n->next != NULL; n = n->next)          // find last node in freelist
            ;

        n->next = list;                                             // tack this list onto the end
    }
}

// -------------------------------------------------------------------------------------------
// init_dsf_stream - like "fopen", prepares to read data out of a DSF file. A DSFSTREAM structure
// hold necessary state information like file offset, current data module and current data block
// -------------------------------------------------------------------------------------------

typedef struct {
    uint16      dbaddr;                 // dbaddr of current file
    uint16      offset;                 // current offset in file
    uint16      nwords;                 // number of words left in current data module
    uint16      addr;                   // load address of next word
    uint16      nw;                     // number of words in current data block (2-9)
    uint16      ind;                    // index of next word to extract from current data block (1-8)
    uint16      relflag;                // relocation flags; next word's flags are left adjusted
    uint16      datablock[9];           // current data block; datablock[ind] is next word
} DSFSTREAM;

void init_dsf_stream (DSFSTREAM *dsf_stream, LETENTRY *entry, DSF_PROGRAM_HEADER *hdr)
{
    dsf_stream->dbaddr = entry->dbaddr;     // set up dbaddr and offset for data in chosen file
    dsf_stream->offset = hdr->hdr_len9 + 9; // point just past the file header (hdr_len9 is the length - 9)

    dsf_stream->ind    = 999;               // set state so we have to read the first data module
    dsf_stream->nw     = 0;
    dsf_stream->nwords = 0;
}

// -------------------------------------------------------------------------------------------
// get_dsf_word - read next data word and associated relocation flag bits from DSF data stream
// -------------------------------------------------------------------------------------------

BOOL get_dsf_word (DSFSTREAM *dsf_stream, uint16 *word, uint16 *addr, uint16 *relflag)
{
    uint16 dataheader[2];
    int i;

    if (dsf_stream->ind >= dsf_stream->nw) {            // we've exhausted the current data block, get next one
        if (dsf_stream->nwords == 0) {                  // we've exhausted the current module, get next one
            getdata(dataheader, dsf_stream->dbaddr, dsf_stream->offset, 2);     // get two-word data block header
            dsf_stream->offset += 2;

            dsf_stream->addr   = dataheader[0];         // save address and module size
            dsf_stream->nwords = dataheader[1];

            if (dsf_stream->nwords == 0)                // end of file
                return FALSE;

            if (verbose)                                // in verbose mode, show module header
                printf(INDENT2 "%04x %04x %d\n", dsf_stream->addr, dsf_stream->nwords, dsf_stream->nwords-2);

            dsf_stream->nwords -= 2;                    // deduct 2 dataheader words we just read
        }

        dsf_stream->nw = min(dsf_stream->nwords, 9);    // size of next data block
        getdata(dsf_stream->datablock, dsf_stream->dbaddr, dsf_stream->offset, dsf_stream->nw);
        dsf_stream->offset += dsf_stream->nw;           // bump file offset
        dsf_stream->nwords -= dsf_stream->nw;           // and number of words left in current module
        dsf_stream->relflag = dsf_stream->datablock[0]; // get relocation flag word
        dsf_stream->ind     = 1;                        // initialize index

        if (verbose) {                                  // in verbose mode, show data block including relocation bits
            static char flagchar[4] = {'.', 'r', 'L', 'C'};     // (show . r L or C for abs, rel, LIBF or CALL)
            char flagstr[10];

            for (i = 1; i < dsf_stream->nw; i++)                // construct string showing meaning of relocation bits
                flagstr[i-1] = flagchar[(dsf_stream->relflag >> (16-2*i)) & 3];
            flagstr[i-1] = '\0';
                                                                // display address, relocation info, data words
            printf(INDENT2 "   %04x [%04x %-8s]", dsf_stream->addr, dsf_stream->relflag, flagstr);
            for (i = 1; i < dsf_stream->nw; i++)
                printf(" %04x", dsf_stream->datablock[i]);
            putchar('\n');
        }
    }
                                                        // ready to extract the next word...
    *word = dsf_stream->datablock[dsf_stream->ind++];   // give caller the word, and increment index
    *relflag = (dsf_stream->relflag >> 14) & 3;         // give caller the top two bits of the relocation flag word
    dsf_stream->relflag <<= 2;                          // and slide next two bits into place

    *addr = dsf_stream->addr;                           // give caller the word's address, and increment address
    if (*relflag != 2)                                  // unless relflag was 2 (LIBF), which occupies only 1 word
        dsf_stream->addr++;                             // in core. We'll increment addr when we fetch the 2nd name word
}

// -------------------------------------------------------------------------------------------
// print_dsf_info - print information about a Disk System Format file
// -------------------------------------------------------------------------------------------

void print_dsf_info (LETENTRY *entry)
{
    DSF_PROGRAM_HEADER hdr;
    char name[6], *nm, label[4];
    int i, nentries;
    unsigned subtype, progtype, int_precis, real_precis, n_defined_files, fortran_indicator;
    uint16 namewords[2], word, addr, relflag;
    NAMELIST call_list, dsn_list;
    DSFSTREAM dsf_stream;

    getdata(&hdr, entry->dbaddr, 0, sizeof(DSF_PROGRAM_HEADER)/2);      // read file header (assume maximum size)

    subtype           = (hdr.type >> 12) & 0x0F;                        // extract file type and subtype
    progtype          = (hdr.type >>  8) & 0x0F;
    int_precis        = (hdr.type >>  4) & 0x0F;                        // get precision specification
    real_precis       = hdr.type & 0x0F;
    fortran_indicator = (hdr.fortran_info >> 8) & 0xFF;                 // get fortran specifications
    n_defined_files   = hdr.fortran_info & 0xFF;

    if (hdr.zero1 != 0)                                                 // this word is supposed to be zero
        printf(INDENT "CORRUPT:      hdr word 1 should be 0, is %d\n", hdr.zero1);
//  if (hdr.zero2 != 0)                                                 // so is this word, but it turns out not to be reliably zero
//      printf(INDENT "CORRUPT:      hdr word 7 should be 0, is %d\n", hdr.zero2);

    printf(INDENT "Program type: %d=%s\n", progtype, progtype_nm[progtype]);    
    if (progtype == 3 || progtype == 4 || progtype == 5 || progtype == 7) {
        nm = "Undefined";                                               // types 3, 4, 5 and 7 should have a subtype
        for (i = 0; i < N_SUBTYPE_NMS; i++) {
            if (subtype_nm[i].progtype == progtype && subtype_nm[i].subtype == subtype) {
                nm = subtype_nm[i].descr;
                break;
            }
        }
        if (nm != NULL)                                                 // print subtype unless name was defined as NULL
            printf(INDENT "Subtype:      %d=%s\n", subtype, nm);
    }
                                                                        // print fortran information
    printf(INDENT "Precision:    Real=%s Integer=%s\n",
        (real_precis == 0) ? "Unspecified" : (real_precis == 1) ? "Standard"     : (real_precis == 2) ? "Extended" : "invalid",
        (int_precis  == 0) ? "Unspecified" : (int_precis  == 8) ? "Matches Real" : (int_precis  == 9) ? "One word" : "invalid");
    printf(INDENT "Prog length:  %d wd\n", hdr.proglen); 
    printf(INDENT "COMMON:       %d wd\n", hdr.commonlen);
    printf(INDENT "Fortran ind:  0x%02x, %d defined file%s\n",
        fortran_indicator, n_defined_files, (n_defined_files == 1) ? "" : "s");

    switch (progtype) {                                                 // print entry information for...
        default:                                                        // ... mainline or subprogram
            if ((hdr.hdr_len9 % 3) != 0) {
                printf(INDENT "CORRUPT:      header length-9 is %d, should be multiple of 3\n", hdr.hdr_len9);
                break;
            }
            nentries = hdr.hdr_len9 / 3;                                // get number of entry points (assuming not an ILS or ISS)
            if (nentries > 15)
                printf(INDENT "CORRUPT:      # of entries is %d, max is 15\n", nentries);

            for (i = 0; i < nentries; i++) {                            // list entry point names and addresses
                convert_namecode(hdr.x.entry[i].name, name);
                sprintf(label, (i < 9) ? "%d: " : "%d:", i+1);          // print, e.g. "2: " or "12:"
                printf(INDENT "Entry %s     %-5s addr /%04x\n", label, name, hdr.x.entry[i].addr);
            }
            break;

        case 5:                                                         // ... ISS (device interrupt service routine)
        case 6:
            if (hdr.hdr_len9 != 7 && hdr.hdr_len9 != 8)
                printf(INDENT "CORRUPT:      header length-9 is %d, should be 7 or 8\n", hdr.hdr_len9);

            convert_namecode(hdr.x.iss.name, name);                     // has just one entry point name
            printf(INDENT "Entry:        %-5s addr /%04x\n", name, hdr.x.iss.addr);
            printf(INDENT "ISS number:   %d\n", hdr.x.iss.issnumber);

            if (hdr.x.iss.nlevels != 1 && hdr.x.iss.nlevels != 2) {     // should have 1 or 2 associated interrupt levels
                printf(INDENT "CORRUPT:      # of levels is %d, should be 1 or 2\n", hdr.x.iss.nlevels);
                hdr.x.iss.nlevels = 1;
            }

            for (i = 0; i < hdr.x.iss.nlevels; i++)
                printf(INDENT "Int level %d:  %d\n", i+1, hdr.x.iss.level[i]);
            break;

        case 7:                                                         // ... ILS (interrupt handler)
            if (hdr.hdr_len9 != 4)
                printf(INDENT "CORRUPT:      header length-9 is %d, should be 4\n", hdr.hdr_len9);

            convert_namecode(hdr.x.ils.name, name);
            printf(INDENT "Entry:        %-5s addr /%04x\n", name, hdr.x.ils.addr);
            printf(INDENT "ILS level:    %d\n", hdr.x.ils.level);
            break;
    }

    call_list = dsn_list = NULL;                                        // clear list of external references

    init_dsf_stream(&dsf_stream, entry, &hdr);                          // prepare to read data from the file

    while (get_dsf_word(&dsf_stream, &word, &addr, &relflag)) {
        switch (relflag) {
            case 0:                                         // 00 - absolute data
            case 1:                                         // 01 - relocatable data
                break;

            case 2:                                         // 10 - LIBF
                namewords[0] = word;                            // save first name word and get the second
                get_dsf_word(&dsf_stream, &namewords[1], &addr, &relflag);
                convert_namecode(namewords, name);              // convert to ASCII
                add_list(name, &call_list);                     // add name to list of external references
                break;

            case 3:                                         // 11 - CALL or DSN
                namewords[0] = word;                            // save first name word and get the second
                get_dsf_word(&dsf_stream, &namewords[1], &addr, &relflag);
                convert_namecode(namewords, name);              // convert to ASCII
                if (relflag == 0)                               // 1100 - CALL
                    add_list(name, &call_list);                 // add name to list of external references
                else if (relflag == 1)                          // 1101 - DSN
                    add_list(name, &dsn_list);                  // add name to list of data source names
                else                                            // 1110 or 1111 - invalid
                    printf(INDENT, "CORRUPT: object data contains invalid relocation bits 111%d\n", relflag & 1);
                break;
        }
    }

    if (call_list != NULL) {            // print list(s) of external references
        print_list(call_list, "Calls:");
        free_list(call_list);
    }

    if (dsn_list != NULL) {
        print_list(dsn_list, "DSN's referenced:");
        free_list(dsn_list);
    }

    putchar('\n');
}

// -------------------------------------------------------------------------------------------
// print_dci_info - print information about a Disk Core Image file
// -------------------------------------------------------------------------------------------

void print_dci_info (LETENTRY *entry)
{
    DCI_PROGRAM_HEADER hdr;
    char *diskprog;
    int i;

    getdata(&hdr, entry->dbaddr, 0, sizeof(DCI_PROGRAM_HEADER)/2);          // read file header

    diskprog = (hdr.dreq == 0xFFFF) ? "DISKZ" : (hdr.dreq == 0x0000) ? "DISK1" : (hdr.dreq == 0x0001) ? "DISKN" : "Unknown";

    printf(INDENT "Execute addr: /%04x\n", hdr.xeqa);                       // interpret and print it
    printf(INDENT "COMMON:       %d wd\n", hdr.cmon);
    printf(INDENT "Disk IO:      /%04x (%s)\n", hdr.dreq, diskprog);
    printf(INDENT "# files defd: %d\n", hdr.file);
    printf(INDENT "Hdr length:   %d wd\n", hdr.hwct);
    printf(INDENT "Sector cnt:   %d files in WS\n", hdr.lsct);
    printf(INDENT "Load address: /%04x\n", hdr.ldad);
    printf(INDENT "Exit addr:    /%04x\n", hdr.xctl);
    printf(INDENT "TV length:    %d wd\n", hdr.tvwc);
    printf(INDENT "Load size:    %d wd including TV\n", hdr.wcnt-hdr.hwct);
    printf(INDENT "XR3:          /%04x\n", hdr.xr3x);

#define NO_VECTOR 0x0091            // appears to be DMS default handler for unrecognized interrupt

    for (i = 0; i < 6; i++) {
        if (hdr.itv[i] != NO_VECTOR)
            printf(INDENT "Lvl %d vector: /%04x\n", i, hdr.itv[i]);
    }

    if (hdr.ibt[0] != NO_VECTOR || hdr.ibt[1] != NO_VECTOR || hdr.ibt[2] != NO_VECTOR)
        printf(INDENT "ISS of 1231:  /%04x /%04x /%04x\n", hdr.ibt[0], hdr.ibt[1], hdr.ibt[2]);
    if (hdr.ibt[3] != NO_VECTOR)
        printf(INDENT "ISS of 1403:  /%04x\n", hdr.ibt[3]);
    if (hdr.ibt[4] != NO_VECTOR)
        printf(INDENT "ISS of 2501:  /%04x\n", hdr.ibt[4]);
    if (hdr.ibt[5] != NO_VECTOR)
        printf(INDENT "ISS of 1442:  /%04x\n", hdr.ibt[5]);
    if (hdr.ibt[6] != NO_VECTOR)
        printf(INDENT "ISS of kb/pr: /%04x\n", hdr.ibt[6]);
    if (hdr.ibt[7] != NO_VECTOR)
        printf(INDENT "ISS of ptr/p: /%04x\n", hdr.ibt[7]);
    printf(INDENT "LOCAL/SOCALs: %d sectors\n", hdr.ovsw);
    printf(INDENT "Built for:    %d wds core\n", hdr.core);

    putchar('\n');
}

// -------------------------------------------------------------------------------------------
// print_ddf_info - print information about a Disk Data Format file
// -------------------------------------------------------------------------------------------

void print_ddf_info (LETENTRY *entry)
{
    // there's nothing to say, really -- these are user-defined files
}

// -------------------------------------------------------------------------------------------
// dumpfile - print file contents in hex
// -------------------------------------------------------------------------------------------

void dumpfile (LETENTRY *entry)
{
    uint16 offset = 0, nw, nwords, buf[8], i;

    nwords = entry->dbcount*BLK_WORDS;                  // number of words to dump

    while (nwords > 0) {
        printf("   %04x |", offset);                    // print current offset

        nw = min(nwords, 8);                            // fetch (up to) 8 words of data
        getdata(buf, entry->dbaddr, offset, nw);
        offset += nw;                                   // bump offset and count
        nwords -= nw;

        for (i = 0; i < nw; i++)                        // print values in hex
            printf(" %04x", buf[i]);

        putchar('\n');
    }

    putchar('\n');
}

// -------------------------------------------------------------------------------------------
// print_onefile - print detailed info about one particular file
// -------------------------------------------------------------------------------------------

void print_onefile (LETENTRY *entry, BOOL in_flet)
{
    static BOOL first = TRUE;
    LETENTRY *mst;

    if (first) {                                                // print column headings
        first = FALSE;
        printf("Name  Type  Blocks  Addr Remarks\n");
        printf("----- ----  ------  ---- ---------------------------------------------------\n");
    }

    mst = (entry->master != NULL) ? entry->master : entry;      // pointer to main (primary) entry

    printf("%-5s %-3s ", entry->name, ftname[mst->filetype]);   // print file name and size info
    commas(mst->dbcount, 8);
    printf("  %04x", mst->dbaddr);
    if (entry->master != NULL)                                  // this is an alternate entry since it points to a master
        printf(" (alternate entry point in %s)", mst->name);
    printf("%s\n", in_flet ? " (in FLET)" : "");

    if (do_dump)                                                // if -d specified, dump contents
        dumpfile(mst);

    if (show_all) {                                             // if -a specified, print detailed info for particular file type
        switch (mst->filetype) {
            case FILETYPE_DSF:      print_dsf_info(mst);        break;
            case FILETYPE_1:        /* unknown format */        break;
            case FILETYPE_DCI:      print_dci_info(mst);        break;
            case FILETYPE_DDF:      print_ddf_info(mst);        break;
            default:    bail("in print_onefile, can't happen");
        }
    }
}

// -------------------------------------------------------------------------------------------
// list_named_files - print info for file(s) matching a filename specified on the command line
// -------------------------------------------------------------------------------------------

void list_named_files (char *name, char *image)
{
    BOOL has_wild = strchr(name, '?') != NULL || strchr(name, '*') != NULL;
    BOOL in_flet, matched;
    LETENTRY *entry;

    if (flet != NULL) {                             // start at head of FLET if we have one, otherwise LET
        in_flet = TRUE;
        entry   = flet;
    }
    else {
        in_flet = FALSE;
        entry   = let;
    }

    matched = FALSE;

    while (entry != NULL) {                         // scan through flet/let lists
        if (! entry->dummy) {
            if (matchname(entry->name, name)) {     // does this file match the specified name?
                print_onefile(entry, in_flet);      // print it
                matched = TRUE;
                if (! has_wild)                     // if there were no wildcard characters in the name, stop scanning
                    break;
            }
        }
                                                    // try next file...
        if (entry->next == NULL) {                  // if at end of current list
            if (in_flet) {
                entry   = let;                      // move from flet to let
                in_flet = FALSE;
            }
            else
                break;                              // done with both lists
        }
        else
            entry = entry->next;                    // go to next entry in list
    }

    if (! matched)
        printf("%s: no such file in %s", name, image);
}

char * file_progtype (LETENTRY *entry)                  // description of module type
{
    static char buf[100];
    DSF_PROGRAM_HEADER hdr;
    char *nm;
    unsigned subtype, progtype;
    int i;

    *buf = '\0';

    switch (entry->filetype) {
        case FILETYPE_DSF:
            getdata(&hdr, entry->dbaddr, 0, sizeof(DSF_PROGRAM_HEADER)/2);      // read file header (assume maximum size)
            subtype           = (hdr.type >> 12) & 0x0F;                        // extract file type and subtype
            progtype          = (hdr.type >>  8) & 0x0F;

            strcpy(buf, progtype_nm[progtype]); 
            if (progtype == 3 || progtype == 4 || progtype == 5 || progtype == 7) {
                nm = NULL;
                for (i = 0; i < N_SUBTYPE_NMS; i++) {
                    if (subtype_nm[i].progtype == progtype && subtype_nm[i].subtype == subtype) {
                        nm = subtype_nm[i].descr;
                        break;
                    }
                }
                if (nm != NULL) {
                    strcat(buf, "; ");
                    strcat(buf, nm);
                }
            }
            break;

        case FILETYPE_DCI:
            return "Mainline, core image";

        case FILETYPE_DDF:
            return "Data";

        default:
            return "unknown";
    }

    return buf;
}

