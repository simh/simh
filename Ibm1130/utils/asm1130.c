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

#define VERSION "ASM1130 CROSS ASSEMBLER V1.22"

/********************************************************************************************
 * ASM1130 - IBM 1130 Cross Assembler
 *
 * Version
 *         1.22 - 2012Nov26 - When the -d argument is specified on the command line, % and < in sources 
 *                            act like ( and ), since in the DMS sources these characters need to be interpreted
 *                            this way.
 *         1.21 - 2012Oct25 - Fixed bug in XFLC. With no argument, it should spit out a zero
 *                            value, thus reserving core for a variable. It was not, so separate
 *                            variables ended up sharing the same core address.
 *         1.20 - 2011Mar25 - Made hex output in listings uppercase
 *         1.19 - 2008Nov25 - Set binout[4] in SBRK cards (probably irrelevant)
 *         1.18 - 2006Sep09 - Fixed opcode for BOD instruction, error found by Luiz Carlos Orsoni
 *         1.17 - 2004Dec14 - Made DEC, DECS output 0 if no argument is given
 *         1.16 - 2004Nov12 - Output WAIT instruction for LIBF if output mode is not binary
 *         1.15 - 2004Nov03 - Added -f option to offset displayed assembly address in listing
 *                            This is useful when viewing listings of code that is assembled to
 *                            one address but actually loaded to another (e.g. some APL\1130 modules)
 *         1.14 - 2004Oct22 - Fixed problem with BSS complaining about negative
 *                            sizes. This may be a fundamental problem with my using
 *                            32-bit expressions, but for now, it appears that just
 *                            truncating the BSS size to 16 bits is sufficient to build DMS.
 *         1.13 - 2004Jun05 - Fixed sign extension of constants in expressions. Statements
 *                            like LD /FFFF were being assembled incorrectly.
 *         1.12 - 2004Jun04 - Made WAIT instruction take a displacement value.
 *                            Doesn't affect operation, but these are used as indicators
 *                            in the IBM one-card diagnostic programs.
 *                            Also -- should mention that the .IPL directive was
 *                            removed some time ago. To create bootable cards, 
 *                            use -b flag to create binary output, and post-process the
 *                            binary output with program "mkboot"
 *         1.11 - 2004May22 - Added CMP, DCM, and DECS instructions for 1800,
 *                            thanks to Kevin Everets.
 *         1.10 - 2003Dec08 - Fixed opcode value for XCH instruction, thanks to
 *                            Roger Simpson.
 *         1.09 - 2003Aug03 - Added fxwrite so asm will write little-endian files
 *                            on all CPUs.
 *         1.08 - 2003Mar18 - Fixed bug that complained about valid MDX displacement of +127
 *         1.07 - 2003Jan05 - Filenames are now left in lower case. SYMBOLS.SYS stays all upper case
 *         1.06 - 2002May02 - Fixed bug in ebdic constants (data goes into low byte)
 *                            First stab at adding ISS level # info, this is iffy
 *         1.05 - 2002Apr24 - Made negative BSS size a warning not an error, as it
 *                            it's looking like it happens twice in PTMASMBL.
 *                            This version still doesn't do fixed point numbers and
 *                            negative floats may be wrong.
 *         1.04 - 2002Apr18 - Added binary (card loader format) output, removed
 *                            interim IPL output formats and moved that to MKBOOT.
 *                            Enhanced relocatable code handling. Added floating
 *                            point constants, but don't know how to make fixed point
 *                            constants yet. Made amenable to syntax variations found
 *                            in the DMS sources. Doesn't properly handle ILS
 *                            modules yet and ISS is probably wrong.
 *         1.03 - 2002Apr10 - numerous fixes, began to add relative/absolute support
 *         1.02 - 2002Feb26 - replaced "strupr" with "upcase" for compatibility
 *         1.01 - 2002Feb25 - minor compiler compatibility changes
 *         1.00 - 2002Feb01 - first release. Tested only under Win32.
 * ---------------------------------------------------------------------------------
 *
 * Usage:
 *      asm1130 [-bdvsx] [-o[file]] [-l[file] [-fXXXX]] [-rN.M] file...
 *
 * Description:
 *      -b      binary output (.bin, relocatable absolute format)
 *      -d      compile with DMS source character adjustments: % is interpreted as ( and < is intrepreted as ).
 *      -v      verbose
 *      -s      print symbol table
 *      -x      print cross references
 *      -o      output file (default is name of first source file + extension .out or .bin)
 *      -l      listing file (default is name of first source file + extension .lst)
 *      -y      preload system symbol table SYMBOLS.SYS (from the current directory)
 *      -w      write the system symbol table SYMBOLS.SYS in the current directory
 *      -W      same as -w but don't prompt to confirm overwriting existing file
 *      -r      set DMS release to release N version M, for sbrk cards
 *      -f      Apply offset XXXX (hex) to APPARENT assembly address in leftmost column of listing
 *
 * Listing and symbol table output can be turned on by *LIST directives in the source, too
 * Listing file default extension is .LST
 *
 * Input files can use strict IBM 1130 Assembler column format, or loose formatting
 * with tabs, or any mix on a line-by-line basis. Input files default extension is .ASM.
 *
 * Strict specification is:
 *
 *          label       columns  1 -  5
 *          opcode               7 - 10
 *          tag                  12
 *          index                13
 *          arguments            15 - 51
 *
 * Loose, indicated by presence of ascii tab character(s):
 *
 *          label<tab>opcode<tab>index and format indicators<tab>arguments
 *
 * In both cases, the IBM convention that the arguments section ends with the
 * first nonblank applies. This means that ".DC 1, 2, 3" assembles only the 1!
 *
 * Output file format is that used by the LOAD command in my 1130
 * simulator. Lines are any of the following. All values are in hex:
 *
 *  @addr           load address for subsequent words is addr
 *  Znwords         Zero the next "nwords" and increment load address by nwords.
 *  =addr           set IAR register to address addr (a convenience)
 *   value          load value at load address and increment load address
 *
 * Output file default extension is .OUT or .BIN for binary assemblies
 *
 * Note: this version does not handle relative assembly, and so doesn't carry
 * absolute/relative indication through expression calculation.
 *
 * Seems to work. Was able to assemble the resident monitor OK.
 * >>> Look for "bug here" though, for things to check out.
 *
 * Notes:
 * We assume that the computer on which the assembler runs uses ANSI floating point.
 * Also, the assembly of floating point values may be incorrect on non-Intel 
 * architectures, this needs to be investigated.
 *
 * org_advanced tells whether * in an expression refers to the address AFTER the
 * instruction (1 or 2 words, depending on length). This is the case for opcodes
 * but not all directives.
 *
 * Revision History
 * 16Apr02  1.03    Added sector break, relocation flag output
 * 02Apr02  1.02    Fixed bug in BOSC: it CAN be a short instruction.
 *                  Added directives for 1130 and 1800 IPL output formats
 *                  Added conditional assembly directives
 ********************************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>
#include <time.h>
#include <ctype.h>
#include "util_io.h"

/********************************************************************************************
 * DEFINITIONS
 ********************************************************************************************/

/* I have found some IBM source code where @ and ' seem interchangable (likely due to the
 * use of 026 keypunches).
 * Comment out this define to make @ and ' different in symbol names, keep to make equivalent
 */

#if defined(VMS)
    #  include <unistd.h>                   /* to pick up 'unlink' */
#endif

#define BETWEEN(v,a,b) (((v) >= (a)) && ((v) <= (b)))
#define MIN(a,b)       (((a) <= (b)) ? (a) : (b))
#define MAX(a,b)       (((a) >= (b)) ? (a) : (b))

#ifndef _WIN32
   int strnicmp (char *a, char *b, int n);
   int strcmpi  (char *a, char *b);
#endif

#define FIX_ATS 

#define DMSVERSION "V2M12"                  /* required 5 characters on sector break card col 67-71 */

#define DOLLAREXIT      "/38"               /* hmmm, are these really fixed absolutely in all versions? */
#define DOLLARDUMP      "/3F"

#define SYSTEM_TABLE "SYMBOLS.SYS"

#define BOOL  int
#define TRUE  1
#define FALSE 0

#define ISTV    0x33                        /* magic number from DMS R2V12 monitorm symbol @ISTV */

#define MAXLITERALS 300
#define MAXENTRIES   14

#define LINEFORMAT    "                          %4ld | %s"
#define LEFT_MARGIN   "                                |"
                 /*  XXXX XXXX XXXX XXXX XXXX XXXX */
                 /*  org  w1   w2   w3   w4   w5 */
                 /*  XXXX 1111 2222 3333 4444  LLLL | */
                 /*  12345678901234567890123456789012 */

typedef enum {ABSOLUTE = 0, RELATIVE = 1, LIBF = 2, CALL = 3} RELOC;

typedef struct tag_symbol {         /* symbol table entry: */
    char *name;                     /* name of symbol */
    int  value;                     /* value (absolute) */
    int  pass;                      /* defined during pass # */
    int  defined;                   /* definition state, see #defines below */
    RELOC relative;                 /* ABSOLUTE = absolute, RELATIVE = relative */
    struct tag_symbol *next;        /* next symbol in list */
    struct tag_xref   *xrefs;       /* cross references */
} SYMBOL, *PSYMBOL;

#define S_UNDEFINED     0           /* values of 'defined' */
#define S_PROVISIONAL   1           /* usually an expression with forward references */
#define S_DEFINED       2           /* ordering must be undef < prov < def */

typedef struct tag_xref {           /* cross reference entry */
    char *fname;                    /* filename */
    int  lno;                       /* line number */
    BOOL definition;                /* true = definition, false = reference */
    struct tag_xref *next;          /* next reference */
} XREF, *PXREF;

typedef struct tag_expr {           /* expression result: absolute or relative */
    int value;
    RELOC relative;
} EXPR;

typedef enum {PROGTYPE_ABSOLUTE = 1, PROGTYPE_RELOCATABLE = 2, PROGTYPE_LIBF = 3, PROGTYPE_CALL = 4,
              PROGTYPE_ISSLIBF  = 5, PROGTYPE_ISSCALL = 6,     PROGTYPE_ILS  = 7} PROGTYPE;

typedef enum {SUBTYPE_INCORE = 0, SUBTYPE_FORDISK = 1, SUBTYPE_ARITH = 2,
              SUBTYPE_FORNONDISK = 3, SUBTYPE_FUNCTION=8} SUBTYPE;

typedef enum {INTMODE_UNSPECIFIED  = 0, INTMODE_MATCHREAL = 0x0080, INTMODE_ONEWORD   = 0x0090} INTMODE;
typedef enum {REALMODE_UNSPECIFIED = 0, REALMODE_STANDARD = 0x0001, REALMODE_EXTENDED = 0x0002} REALMODE;

#define OP_INDEXED  0x0300          /* 1130 opcode modifier bits */
#define OP_LONG     0x0400
#define OP_INDIRECT 0x0080

typedef enum {OUTMODE_LOAD, OUTMODE_BINARY} OUTMODE;

#ifdef _WIN32
#  define OUTWRITEMODE "wb"         /* write outfile in binary mode */
#  define ENDLINE      "\r\n"       /* explictly write CR/LF */
#else
#  define OUTWRITEMODE "w"          /* use native mode */
#  define ENDLINE      "\n"
#endif

/********************************************************************************************
 * GLOBALS
 ********************************************************************************************/

/* command line syntax */
char *usestr =
"Usage: asm1130 [-bdpsvwxy8] [-o[file]] [-l[file] [-fXXXX]] [-rN.M] file...\n\n"
"-b  binary (relocatable format) output; default is simulator LOAD format\n"
"-d  interpret % and < as ( and ), for assembling DMS sources\n"
"-p  count passes required; no assembly output is created with this flag"
"-s  add symbol table to listing\n"
"-v  verbose mode\n"
"-w  write system symbol table as SYMBOLS.SYS\n"
"-W  same as -w but do not confirm overwriting previous file\n"
"-x  add cross reference table to listing\n"
"-y  preload system symbol table SYMBOLS.SYS\n"
"-8  enable IBM 1800 instructions\n"                /* (alternately, rename or link executable to asm1800.exe) */
"-o  set output file; default is first input file + .out or .bin\n"
"-l  create listing file; default is first input file + .lst\n"
"-r  set dms version to VN RM for system SBRK cards\n"
"-f  apply offset XXXX (hex) to APPARENT assembly address listing\n";

BOOL verbose = FALSE;                           /* verbose mode flag */
BOOL tabformat = FALSE;                         /* TRUE if tabs were seen in the file */
BOOL enable_1800 = FALSE;                       /* TRUE if 1800 mode is enabled by flag or executable name */
int  listoffset = 0;                            /* offset to use for listing assembly address column */
int  pass;                                      /* current assembler pass (1 or 2) */
char curfn[256];                                /* current input file name */
char progname[8];                               /* base name of primary input file */
char *outfn = NULL;                             /* output file name */
int  lno;                                       /* current input file line number */
BOOL preload = FALSE;                           /* preload system symbol table */
BOOL savetable = FALSE;                         /* write system symbol table */
BOOL saveprompt = TRUE;                         /* prompt before overwriting */
int  nerrors = 0;                               /* count of errors */
int  nwarnings = 0;                             /* count of warnings */
FILE *fin = NULL;                               /* current input file */
FILE *fout = NULL;                              /* output file stream */
OUTMODE outmode = OUTMODE_LOAD;                 /* output file mode */
int  outcols = 0;                               /* columns written in using card output */
int maxiplcols = 80;
char cardid[9];                                 /* characters used for IPL card ID */
FILE *flist = NULL;                             /* listing file stream */
char *listfn = NULL;                            /* listing filename */
BOOL do_list = FALSE;                           /* flag: create listing */
BOOL passcount = FALSE;                         /* flag: count passes only */
BOOL list_on = TRUE;                            /* listing is currently enabled */
BOOL do_xref = FALSE;                           /* cross reference listing */
BOOL do_syms = FALSE;                           /* symbol table listing */
BOOL ended = FALSE;                             /* end of current file */
BOOL hasforward = FALSE;                        /* true if there are any forward references */
char listline[350];                             /* output listing line */
BOOL line_error;                                /* already saw an error on current line */
RELOC relocate = RELATIVE;                      /* relocatable assembly mode */
BOOL assembled = FALSE;                         /* true if any output has been generated */
int  nwout;                                     /* number of words written on current line */
int  org = 0;                                   /* output address (origin) */
int  org_advanced;                              /* if nonzero, value of * is (instruction address + org_advanced) during evaluation */
int  pta = -1;                                  /* program transfer address */
BOOL cexpr = FALSE;                             /* "C" expression syntax (nonstandard, not enabled by default) */
PSYMBOL symbols = NULL;                         /* the symbol table (linear search) */
BOOL check_control = TRUE;                      /* check for control cards */
PROGTYPE progtype = PROGTYPE_RELOCATABLE;       /* program type */
INTMODE intmode   = INTMODE_UNSPECIFIED;        /* integer mode */
REALMODE realmode = REALMODE_UNSPECIFIED;       /* real mode */
int nintlevels = 0;                             /* # of interrupt levels for ISS */
int intlevel_primary = 0;                       /* primary level for ISS and level for ILS */
int intlevel_secondary = 0;                     /* secondary level for ISS */
int iss_number = 0;                             /* ISS number */
PSYMBOL entry[MAXENTRIES];                      /* entries for subroutines */
int nentries = 0;
int ndefined_files = 0;
struct lit {                                    /* accumulated literals waiting to be output */
    int  value;                                 /* constant value */
    int  tagno;                                 /* constant symbol tag number (e.g. _L001) */
    BOOL hex;                                   /* constant was expressed in hex */
    BOOL even;                                  /* constant was operand of a double-width instruction (e.g. AD) */
} literal[MAXLITERALS];

int n_literals = 0, lit_tag = 0;
BOOL requires_even_address;                     /* target of current instruction */
BOOL dmes_saved;                                /* odd character left over from dmes ending in ' */
int dmes_savew;
char opfield[256];                              /* extracted operand field from source line */
char dmsversion[12] = DMSVERSION;               /* version number for SBRK cards */
const char whitespace[] = " \t";                /* whitespace */

int ascii_to_ebcdic_table[128] = 
{
/*                                                                                          */
    0x00,0x01,0x02,0x03,0x37,0x2d,0x2e,0x2f, 0x16,0x05,0x25,0x0b,0x0c,0x0d,0x0e,0x0f,
/*                                                                                          */
    0x10,0x11,0x12,0x13,0x3c,0x3d,0x32,0x26, 0x18,0x19,0x3f,0x27,0x1c,0x1d,0x1e,0x1f,
/*  spac !    "    #    $    %    &    '     (    )    *    +    ,    -    .    /           */
    0x40,0x5a,0x7f,0x7b,0x5b,0x6c,0x50,0x7d, 0x4d,0x5d,0x5c,0x4e,0x6b,0x60,0x4b,0x61,
/*  0    1    2    3    4    5    6    7     8    9    :    ;    <    =    >    ?           */
    0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7, 0xf8,0xf9,0x7a,0x5e,0x4c,0x7e,0x6e,0x6f,
/*  @    A    B    C    D    E    F    G     H    I    J    K    L    M    N    O           */
    0x7c,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7, 0xc8,0xc9,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,
/*  P    Q    R    S    T    U    V    W     X    Y    Z    [    \    ]    &    _           */
    0xd7,0xd8,0xd9,0xe2,0xe3,0xe4,0xe5,0xe6, 0xe7,0xe8,0xe9,0xba,0xe0,0xbb,0xb0,0x6d,
/*       a    b    c    d    e    f    g     h    i    j    k    l    m    n    o           */
    0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87, 0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
/*  p    q    r    s    t    u    v    w     x    y    z    {    |    }    ~                */
    0x97,0x98,0x99,0xa2,0xa3,0xa4,0xa5,0xa6, 0xa7,0xa8,0xa9,0xc0,0x4f,0xd0,0xa1,0x07,
};
    
    /* note that by default, ascii_to_ebcdic_table maps % and < to the EBCDIC codes for these characters.
     * If the -d command line switch is used to enable DMS mode, these characters map to ( and )
     * Another way to look at the -d switch is that it interprets % and < as if they had been typed
     * on an 026 Commercial keypunch.
     */

int ascii_to_1403_table[128] = 
{ /*  00   01   02   03   04   05   06   07    08   09   0a   0b   0c   0d   0e  0f  */
    0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f, 0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,
    0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f, 0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,0x7f,
    0x7f,0x7f,0x7f,0x7f,0x62,0x7f,0x15,0x0b, 0x57,0x2f,0x23,0x6d,0x16,0x61,0x6e,0x4c,
    0x49,0x40,0x01,0x02,0x43,0x04,0x45,0x46, 0x07,0x08,0x7f,0x7f,0x7f,0x4a,0x7f,0x7f,
    0x7f,0x64,0x25,0x26,0x67,0x68,0x29,0x2a, 0x6b,0x2c,0x58,0x19,0x1a,0x5b,0x1c,0x5d,
    0x5e,0x1f,0x20,0x0d,0x0e,0x4f,0x10,0x51, 0x52,0x13,0x54,0x7f,0x7f,0x7f,0x7f,0x7f,
    0x7f,0x64,0x25,0x26,0x67,0x68,0x29,0x2a, 0x6b,0x2c,0x58,0x19,0x1a,0x5b,0x1c,0x5d,
    0x5e,0x1f,0x20,0x0d,0x0e,0x4f,0x10,0x51, 0x52,0x13,0x54,0x7f,0x7f,0x7f,0x7f,0x7f
};

#include "../ibm1130_conout.h"          /* conout_to_ascii_table */
#include "../ibm1130_prtwheel.h"        /* 1132 printer printwheel data */

/********************************************************************************************
 * PROTOTYPES
 ********************************************************************************************/

void init (int argc, char **argv);
void bail (char *msg);
void flag (char *arg);
void proc (char *fname);
void startpass (int n);
void errprintf (char *fmt, ...);
void asm_error (char *fmt, ...);
void asm_warning (char *fmt, ...);
char *astring (char *str);
PSYMBOL lookup_symbol (char *name, BOOL define);
void add_xref (PSYMBOL s, BOOL definition);
int  get_symbol (char *name);
void set_symbol (char *name, int value, int known, RELOC relative);
char * gtok (char **pc, char *tok);
char *skipbl (char *c);
void sym_list (void);
void xref_list (void);
void listhdr (void);
int  getexpr (char *pc, BOOL undefined_ok, EXPR *expr);
void passreport (void);
void listout (BOOL reset);
void output_literals (BOOL eof);
char *upcase (char *str);
void prep_line (char *line);
int ascii_to_hollerith (int ch);
char *detab (char *str);
void preload_symbols (void);
void save_symbols (void);
void bincard_init (void);
void bincard_writecard (char *sbrk_text);
void bincard_writedata (void);
void bincard_flush (void);
void bincard_sbrk (char *line);
void bincard_setorg (int neworg);
void bincard_writew (int word, RELOC relative);
void bincard_endcard (void);
void handle_sbrk (char *line);
void bincard_typecard (void);
void namecode (unsigned short *words, char *tok);
int  signextend (int v);

/********************************************************************************************
 * main routine
 ********************************************************************************************/

int main (int argc, char **argv)
{
    int i, sawfile = FALSE;

    init(argc, argv);                           /* initialize, process flags */

    startpass(1);                               /* first pass, process files  */

    for (i = 1; i < argc; i++)
        if (*argv[i] != '-')
            proc(argv[i]), sawfile = TRUE;

    if (! sawfile)                              /* should have seen at least one file */
        bail(usestr);

    if (passcount) {
        passreport();
        return 0;
    }

    startpass(2);                               /* second pass, process files again */

    for (i = 1; i < argc; i++)
        if (*argv[i] != '-')
            proc(argv[i]);

    if (outmode == OUTMODE_LOAD) {
        if (pta >= 0)                           /* write start address to the load file */
            fprintf(fout, "=%04X" ENDLINE, pta & 0xFFFF);
    }
    else 
        bincard_endcard();

    if (flist) {
        if (nerrors || nwarnings) {             /* summarize (or summarise) */
            if (nerrors == 0)
                fprintf(flist, "There %s ", (nwarnings == 1) ? "was" : "were");
            else
                fprintf(flist, "\nThere %s %d error%s %s",
                    (nerrors == 1) ? "was" : "were", nerrors, (nerrors == 1) ? "" : "s", nwarnings ? "and " : "");

            if (nwarnings > 0)
                fprintf(flist, "%d warning%s ", nwarnings, (nwarnings == 1) ? "" : "s");

            fprintf(flist, "in this assembly\n");
        }
        else
            fprintf(flist, "\nThere were no errors in this assembly\n"); 
    }

    if (flist) {                                /* finish the listing */
        if (pta >= 0)
            fprintf(flist, "\nProgram transfer address = %04X\n", pta);

        if (do_xref)
            xref_list();
        else if (do_syms)
            sym_list();
    }

    if (savetable)
        save_symbols();

    return 0;                                   /* all done */
}

/********************************************************************************************
 * init - initialize assembler, process command line flags
 ********************************************************************************************/

void init (int argc, char **argv)
{
    int i;

    enable_1800 = strstr(argv[0], "1800") != NULL;  /* if "1800" appears in the executable name, enable 1800 extensions */

    for (i = 1; i < argc; i++)                      /* process command line switches */
        if (*argv[i] == '-')
            flag(argv[i]+1);
}

/********************************************************************************************
 * flag - process one command line switch
 ********************************************************************************************/

void flag (char *arg)
{
    int major, minor;

    while (*arg) {
        switch (*arg++) {
            case 'o':                           /* output (load) file name */
                if (! *arg)
                    bail(usestr);
                outfn = arg;
                return;

            case 'p':
                passcount = TRUE;
                break;

            case 'v':                           /* mumble while running */
                verbose = TRUE;
                break;

            case 'x':                           /* print cross reference table */
                do_xref = TRUE;
                break;
            
            case 's':                           /* print symbol table */
                do_syms = TRUE;
                break;

            case 'l':                           /* listing file name */
                listfn = (* arg) ? arg : NULL;
                do_list = TRUE;
                return;

            case 'W':
                saveprompt = FALSE;
                /* fall through */
            case 'w':
                savetable = TRUE;
                break;

            case 'y':
                preload = TRUE;
                break;

            case 'b':
                outmode = OUTMODE_BINARY;
                break;

            case '8':
                enable_1800 = TRUE;
                break;

            case 'r':
                if (sscanf(arg, "%d.%d", &major, &minor) != 2)
                    bail(usestr);
                sprintf(dmsversion, "V%01.1dM%02.2d", major, minor);
                return;

            case 'f':
                if (sscanf(arg, "%x", &listoffset) != 1)
                    bail(usestr);

                if (listoffset & 0x8000)                /* sign extend from 16 to (int) bits */
                    listoffset |= ~0x7FFF;

                return;

            case 'd':                                   /* DMS source code mode: treat % and < as ( and ) */
                ascii_to_ebcdic_table['%'] = ascii_to_ebcdic_table['('];    /* an alternate intpretation of -d is that it interprets */
                ascii_to_ebcdic_table['<'] = ascii_to_ebcdic_table[')'];    /* % and < as if from an 026 Commercial keypunch */
                break;

            default:
                bail(usestr);
                break;
        }
    }
}

/********************************************************************************************
 * bail - print error message on stderr (only) and exit
 ********************************************************************************************/

void bail (char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

/********************************************************************************************
 * errprintf - print error message to stderr
 ********************************************************************************************/

void errprintf (char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);                            /* get pointer to argument list */

    vfprintf(stderr, fmt, args);                    /* write errors to terminal (stderr) */

    va_end(args);
}

/********************************************************************************************
 * asm_error - report an error to listing file and to user's console
 ********************************************************************************************/

void asm_error (char *fmt, ...)
{
    va_list args;

    if (pass == 1)                                  /* only print on pass 2 */
        return;

    va_start(args, fmt);                            /* get pointer to argument list */

    fprintf(stderr, "E: %s (%d): ", curfn, lno);
    vfprintf(stderr, fmt, args);                    /* write errors to terminal (stderr) */
    putc('\n', stderr);

    if (flist != NULL && list_on) {
        listout(FALSE);
        line_error = TRUE;

        fprintf(flist, "**** Error: ");
        vfprintf(flist, fmt, args);                 /* write errors to listing file */
        putc('\n', flist);
    }

    nerrors++;
    va_end(args);
}

/********************************************************************************************
 * asm_warning - same but warnings are not counted
 ********************************************************************************************/

void asm_warning (char *fmt, ...)
{
    va_list args;

    if (pass == 1)                                  /* only print on pass 2 */
        return;

    va_start(args, fmt);                            /* get pointer to argument list */

    fprintf(stderr, "W: %s (%d): ", curfn, lno);
    vfprintf(stderr, fmt, args);                    /* write errors to terminal (stderr) */
    putc('\n', stderr);

    if (flist != NULL && list_on) {
        listout(FALSE);
        line_error = TRUE;

        fprintf(flist, "**** Warning: ");
        vfprintf(flist, fmt, args);                 /* write errors to listing file */
        putc('\n', flist);
    }

    nwarnings++;
}

/********************************************************************************************
 * sym_list - print the symbol table
 ********************************************************************************************/

void sym_list (void)
{
    PSYMBOL s;
    int n = 5;

    if (symbols == NULL || flist == NULL)
        return;

    fprintf(flist, "\n=== SYMBOL TABLE ==============================================================\n");

    for (s = symbols, n = 0; s != NULL; s = s->next) {
        if (n >= 5) {
            putc('\n', flist);
            n = 0;
        }
        else if (n > 0)
            fprintf(flist, "     ");

        fprintf(flist, "%-6s ", s->name);
        if (s->defined == S_DEFINED)
            fprintf(flist, "%04X%s", s->value & 0xFFFF, s->relative ? "R" : " ");
        else
            fprintf(flist, "UUUU ");

        n++;
    }
    fprintf(flist, "\n");
}

/********************************************************************************************
 * passreport - report # of passes required for assembly on the 1130
 ********************************************************************************************/

void passreport (void)
{
    PSYMBOL s;

    for (s = symbols; s != NULL; s = s->next) {
        if (s->defined == S_UNDEFINED || s->defined == S_PROVISIONAL) {
            printf("There are undefined symbols. Cannot determine pass requirement.\n");
            return;
        }
    }

    if (hasforward)
        printf("There are forward references. Two passes are required.\n");
    else
        printf("There are no forward references. Only one pass is required.\n");
}

/********************************************************************************************
 * xref_list - print the cross-reference table
 ********************************************************************************************/

void xref_list (void)
{
    int n = 0;
    PXREF x;
    PSYMBOL s;

    if (flist == NULL || symbols == NULL)
        return;

    fprintf(flist, "\n=== CROSS REFERENCES ==========================================================\n");

    if (symbols == NULL || flist == NULL)
        return;

    fprintf(flist, "Name  Val   Defd  Referenced\n");

    for (s = symbols; s != NULL; s = s->next) {
        fprintf(flist, "%-5s %04X%s", s->name, s->value & 0xFFFF, s->relative ? "R" : " ");

        for (x = s->xrefs; x != NULL; x = x->next)
            if (x->definition)
                break;

        if (x == NULL)
            fprintf(flist, "----");
        else
            fprintf(flist, " %4d", x->lno);

        for (n = 0, x = s->xrefs; x != NULL; x = x->next) {
            if (x->definition)
                continue;

            if (n >= 12) {
                n = 0;
                fprintf(flist, "\n               ");
            }
            fprintf(flist, " %4d", x->lno);
            n++;
        } 
        putc('\n', flist);
    }
}

/********************************************************************************************
 * listhdr - print a banner header in the listing file. Since it's not paginated
 * at this time, this is not used often.
 ********************************************************************************************/

void listhdr (void)
{
    time_t t;
    
    time(&t);
    fprintf(flist, "%s -- %s -- ", VERSION, dmsversion);

    if (listoffset != 0)                /* be sure to note difference in the listing */
        fprintf(flist, "LIST OFFSET %04X -- ", listoffset & 0xFFFF);

    fprintf(flist, "%s\n", ctime(&t));
}

/********************************************************************************************
 * astring - allocate a copy of a string
 ********************************************************************************************/

char *astring (char *str)
{
    static char *s = NULL;

    if (s != NULL)
        if (strcmp(s, str) == 0)        /* if same as immediately previous allocation */
            return s;                   /* return same pointer (why did I do this?) */

    if ((s = malloc(strlen(str)+1)) == NULL)
        bail("out of memory");

    strcpy(s, str);
    return s;
}

/********************************************************************************************
 * lookup_symbol - get pointer to a symbol.
 * If define is TRUE, creates and marks 'undefined' if not previously defined.
 ********************************************************************************************/

PSYMBOL lookup_symbol (char *name, BOOL define)
{
    PSYMBOL s, n, prv = NULL;
    int c;
    char *at;

    if (strlen(name) > 5) {             /* (sigh) */
        asm_error("Symbol '%s' is longer than 5 letters", name);
        name[5] = '\0';
    }

#ifdef FIX_ATS
    while ((at = strchr(name, '@')) != NULL)
        *at = '\'';
#endif
                                        /* search sorted list of symbols */
    for (s = symbols; s != NULL; prv = s, s = s->next) {
        c = strcmpi(s->name, name);
        if (c == 0)
            return s;
        if (c > 0)
            break;
    }

    if (! define)
        return NULL;                    /* not found */

    if ((n = malloc(sizeof(SYMBOL))) == NULL)
        bail("out of memory");

    n->name    = astring(name);         /* symbol was undefined -- add it now */
    n->value   = 0;
    n->defined = FALSE;
    n->xrefs   = NULL;
    n->defined = FALSE;

    n->next    = s;                     /* link in alpha order */

    if (prv == NULL)                    /* we stopped before first item in list */
        symbols = n;
    else
        prv->next = n;                  /* insert after item before place we stopped */

    return n;
}

/********************************************************************************************
 * add_xref - add a cross reference entry to a symbol
 ********************************************************************************************/

void add_xref (PSYMBOL s, BOOL definition)
{
    PXREF x, prv = NULL, n;

    if (pass == 1 || ! do_xref)         /* define only during 2nd pass and only if listing was requested */
        return;

    for (x = s->xrefs; x != NULL; prv = x, x = x->next)
        if (strcmpi(x->fname, curfn) == 0 && x->lno == lno)
            return;                     /* ignore multiple refs on same line */

    if ((n = malloc(sizeof(XREF))) == NULL)
        bail("out of memory");

    n->fname = astring(curfn);
    n->lno   = lno;
    n->definition = definition;

    n->next = x;                        /* link at end of existing list */

    if (prv == NULL)
        s->xrefs = n;
    else
        prv->next = n;
}

/********************************************************************************************
 * get_symbol - get a symbol value, defining if necessary
 ********************************************************************************************/

int get_symbol (char *name) 
{
    PSYMBOL s;

    s = lookup_symbol(name, TRUE);          /* lookup, define if necessary */
    
    if (pass == 2)                          /* should be defined by now */
        if (! s->defined)
            asm_error("Symbol '%s' is undefined", name);

    add_xref(s, FALSE);                     /* note the reference */

    return s->value;            
}

/********************************************************************************************
 * set_symbol - set a symbol value. Known = TRUE means we really know the value;
 * FALSE means we're calculating it with forward referenced values or something like
 * that.
 ********************************************************************************************/

void set_symbol (char *name, int value, int known, RELOC relative) 
{
    PSYMBOL s;
    char *at;

    if (strlen(name) > 5) {
        asm_error("Symbol '%s' is longer than 5 letters", name);
        name[5] = '\0';
    }

#ifdef FIX_ATS
    while ((at = strchr(name, '@')) != NULL)
        *at = '\'';
#endif

    s = lookup_symbol(name, TRUE);
    
    if (s->defined == S_DEFINED)            /* once defined, it should not change */
        if (s->value != value)
            asm_error("Symbol '%s' %s", name, (s->pass == pass) ? "is multiply defined" : "changed between passes");

    s->value    = value;
    s->relative = relative;
    s->defined  = known ? S_DEFINED : S_PROVISIONAL;
    s->pass     = pass;

    if (! known)
        hasforward = TRUE;

    add_xref(s, TRUE);                      /* record the place of definition */
}

/********************************************************************************************
 * skipbl - return pointer to first nonblank character in string s
 ********************************************************************************************/

char *skipbl (char *s)
{
    while (*s && *s <= ' ')
        s++;

    return s;
}

/********************************************************************************************
 * gtok - extracts a whitespace-delimited token from the string pointed to by *pc;
 * stores the token into the buffer tok and returns pointer to same.  Returns NULL
 * when there are no tokens.  Best to call repeatedly with a pointer to the source
 * buffer, e.g.
 *          char *pbuf = buf;
 *          while (gtok(&pbuf, token) != NULL) ...
 ********************************************************************************************/

char * gtok (char **pc, char *tok)
{
    char *s = *pc, *otok = tok;

    while (*s && *s <= ' ')         /* skip blanks */
        s++;

    if (! *s) {                     /* no tokens to be found */
        *tok = '\0';
        *pc = s;
        return NULL;
    }

    while (*s > ' ')                /* save nonblanks into 'tok' */
        *tok++ = *s++;

    *tok = '\0';                    /* terminate */
    *pc = s;                        /* adjust caller's pointer */

    return otok;                    /* return pointer to token */
}

/* listing format:
 *
 * ADDR CODE                  SOURCE
 * 0000 0000 0000 0000 0000 | XXXXXXXXXXXXXXXXX
 */

/********************************************************************************************
 * trim - remove trailing whitespace from string s
 ********************************************************************************************/

char *trim (char *s)
{
    char *os = s, *nb;

    for (nb = s-1; *s; s++)
        if (*s > ' ')
            nb = s;

    nb[1] = '\0';
    return os;
}

/********************************************************************************************
 * listout - emit current constructed output listing line held in "listline" and
 * if "reset" is true, prepare listline for second and subsequent listing lines
 * for a given input statement.
 ********************************************************************************************/

void listout (BOOL reset)
{
    if (flist && list_on && ! line_error) {
        trim(listline);
        fputs(listline, flist);
        putc('\n', flist);
        if (reset)
            sprintf(listline, LEFT_MARGIN, org);
    }
}

/********************************************************************************************
 * storew - store a word in the output medium (hex or binary file). Most of the time
 * writew is used. Advances the origin!
 ********************************************************************************************/

void storew (int word, RELOC relative)
{
    if (pass == 2) {                    /* save in output (load) file. */
        switch (outmode) {
            case OUTMODE_BINARY:
                bincard_writew(word, relative);
                break;

            case OUTMODE_LOAD:
                fprintf(fout, " %04X%s" ENDLINE, word & 0xFFFF,
                    (relative == ABSOLUTE) ? ""  : (relative == RELATIVE) ? "R" :
                    (relative == LIBF)     ? "L" : (relative == CALL)     ? "$" : "?");
                break;

            default:
                bail("in storew, can't happen");
        }
    }

    if (relative != LIBF)
        org++;

    assembled = TRUE;                   /* remember that we wrote something */
}

/********************************************************************************************
 * setw - store a word value in the current listing output line in position 'pos'.
 ********************************************************************************************/

void setw (int pos, int word, RELOC relative)
{
    char tok[10], *p;
    int i;
    
    if (flist == NULL || ! list_on)
        return;

    sprintf(tok, "%04X", word & 0xFFFF);

    for (i = 0, p = listline + 5*pos; i < 4; i++)
        p[i] = tok[i];

    if (relative == RELATIVE)
        p[i] = 'R';
    else if (relative != ABSOLUTE)
        p[i] = '*';
}

/********************************************************************************************
 * writew - emit an assembled word value.  Words are also displayed in the listing file.
 * if relative is true, a relocation entry should be recorded.
 ********************************************************************************************/

void writew (int word, RELOC relative)
{                                       /* first, the listing stuff... */
    if (nwout == 0) {                   /* on first output word, display address in column 0 */
        setw(0, org+listoffset, FALSE);
    }
    else if (nwout >= 4) {              /* if 4 words have already been written, start new line */
        listout(TRUE);
        nwout = 0;
    }

    nwout++;
    setw(nwout, word, relative);        /* display word in the listing line */

    storew(word, relative);             /* write it to the output medium */
}

/********************************************************************************************
 * setorg - take note of new load address
 ********************************************************************************************/

void setorg (int neworg)
{
    if (pass == 2) {
        setw(0, neworg+listoffset, FALSE);          /* display in listing file in column 0 */

        if (outmode == OUTMODE_LOAD) {              /* write new load address to the output file */
            fprintf(fout, "@%04X%s" ENDLINE, neworg & 0xFFFF, relocate ? "R" : "");
        }
        else {
            bincard_setorg(neworg);
        }
    }

    org = neworg;
}

/********************************************************************************************
 * org_even - force load address to an even address
 ********************************************************************************************/

void org_even (void)
{
    if (org & 1)
        setorg(org+1);
}

/********************************************************************************************
 * tabtok - get the token in tab-delimited column number i, from source string c,
 * saving in string 'tok'. If save is nonnull, we copy the entire remainder of
 * the input string in buffer 'save' (in contrast to 'tok' which gets only the
 * first whitespace delimited token).
 ********************************************************************************************/

void tabtok (char *c, char *tok, int i, char *save)
{
    *tok = '\0';

    while (--i >= 0) {          /* skip to i'th tab-delimited field */
        if ((c = strchr(c, '\t')) == NULL) {
            if (save)           /* was none */
                *save = '\0';
            return;
        }
        c++;
    }

    while (*c == ' ')           /* skip leading blanks */
        c++;

    if (save != NULL)           /* save copy of entire remainder */
        strcpy(save, c);

    while (*c > ' ') {          /* take up to any whitespace */
        if (*c == '(') {            /* if we start with a paren, take all up to closing paren including spaces */
            while (*c && *c != ')')
                *tok++ = *c++;
        }
        else if (*c == '.') {       /* period means literal character following */
            *tok++ = *c++;
            if (*c)
                *tok++ = *c++;
        }
        else
            *tok++ = *c++;
    }

    *tok = '\0';
}

/********************************************************************************************
 * coltok - extract a token from string c, saving to buffer tok, by examining
 * columns ifrom through ito only.  If save is nonnull, the entire remainder
 * of the input from ifrom to the end is saved there. In this routine
 * if condense is true, we save all nonwhite characters in the column range;
 * not the usual thing. This helps us coalesce the format, tag, & index things
 * nto one string for the simple minded parser. If condense is FALSE, we terminate
 * on the first nonblank, except that if we start with a (, we take up to ) and
 * then terminate on a space.
 *
 * ifrom and ito on entry are column numbers, not indices; we change that right away
 ********************************************************************************************/

void coltok (char *c, char *tok, int ifrom, int ito, BOOL condense, char *save)
{
    char *otok = tok;
    int i;

    ifrom--;
    ito--;

    for (i = 0; i < ifrom; i++) {
        if (c[i] == '\0') {             /* line ended before this column */
            *tok = '\0';
            if (save)
                *save = '\0';
            return;
        }
    }

    if (save)                           /* save from ifrom on */
        strcpy(save, c+i);

    if (condense) {
        for (; i <= ito; i++) {         /* save only nonwhite characters */
            if (c[i] > ' ')
                *tok++ = c[i];
        }
    }
    else {
        if (c[i] == ' ' && save != NULL)/* if it starts with a space, it's empty */
            *save = '\0';

        while (i <= ito) {              /* take up to any whitespace */
            if (c[i] <= ' ')
                break;
            else if (c[i] == '(') {     /* starts with paren? take to close paren */
                while (i <= ito && c[i]) {
                    if ((*tok++ = c[i++]) == ')')
                        break;
                }
            }
            else if (c[i] == '.') {     /* period means literal character following */
                *tok++ = c[i++];
                if (i <= ito && c[i])
                    *tok++ = c[i++];
            }
            else
                *tok++ = c[i++];
        }
    }

    *tok = '\0';
    trim(otok);
}

/********************************************************************************************
 * opcode table
 ********************************************************************************************/

/* modifiers for the opcode definition table: */

#define L       "L"         /* long */
#define X       "X"         /* absolute displacement */
#define I       "I"         /* indirect */
#define IDX     "0123"      /* indexed (some LDX commands in the DMS source say LDX L0, so accept 0 */
#define E       "E"         /* even address */
#define NONE    ""
#define ALL     L X I IDX       /* hope non-Microsoft C accepts and concatenates strings like this */
#define ANY     "\xFF"
#define NUMS    "0123456789"

#define IS_DBL  0x0001          /* double word operand implies even address */
#define IS_ABS  0x0002          /* always uses absolute addressing mode (implied X) */
#define NO_IDX  0x0004          /* even with 1 or 2 modifier, this is not really indexed (for STX/LDX) */
#define NO_ARGS 0x0008          /* statement takes no arguments */
#define IS_1800 0x0010          /* 1800-only directive or instruction, flagged if 1800 mode is not enabled */
#define TRAP    0x1000          /* debug this instruction */

struct tag_op {                                         /* OPCODE TABLE */
    char *mnem;
    int  opcode;
    void (*handler)(struct tag_op *op, char *label, char *mods, char *arg);
    char *mods_allowed;
    char *mods_implied;
    int  flags;
};
                                /* special opcode handlers */
void std_op (struct tag_op *op, char *label, char *mods, char *arg);
void b_op   (struct tag_op *op, char *label, char *mods, char *arg);
void bsc_op (struct tag_op *op, char *label, char *mods, char *arg);
void bsi_op (struct tag_op *op, char *label, char *mods, char *arg);
void mdx_op (struct tag_op *op, char *label, char *mods, char *arg);
void shf_op (struct tag_op *op, char *label, char *mods, char *arg);

void x_aif  (struct tag_op *op, char *label, char *mods, char *arg);
void x_aifb (struct tag_op *op, char *label, char *mods, char *arg);
void x_ago  (struct tag_op *op, char *label, char *mods, char *arg);
void x_agob (struct tag_op *op, char *label, char *mods, char *arg);
void x_anop (struct tag_op *op, char *label, char *mods, char *arg);
void x_abs  (struct tag_op *op, char *label, char *mods, char *arg);
void x_call (struct tag_op *op, char *label, char *mods, char *arg);
void x_dsa  (struct tag_op *op, char *label, char *mods, char *arg);
void x_file (struct tag_op *op, char *label, char *mods, char *arg);
void x_link (struct tag_op *op, char *label, char *mods, char *arg);
void x_libf (struct tag_op *op, char *label, char *mods, char *arg);
void x_org  (struct tag_op *op, char *label, char *mods, char *arg);
void x_opt  (struct tag_op *op, char *label, char *mods, char *arg);
void x_ces  (struct tag_op *op, char *label, char *mods, char *arg);
void x_bes  (struct tag_op *op, char *label, char *mods, char *arg);
void x_bss  (struct tag_op *op, char *label, char *mods, char *arg);
void x_dc   (struct tag_op *op, char *label, char *mods, char *arg);
void x_dec  (struct tag_op *op, char *label, char *mods, char *arg);
void x_decs (struct tag_op *op, char *label, char *mods, char *arg);
void x_ebc  (struct tag_op *op, char *label, char *mods, char *arg);
void x_end  (struct tag_op *op, char *label, char *mods, char *arg);
void x_ent  (struct tag_op *op, char *label, char *mods, char *arg);
void x_epr  (struct tag_op *op, char *label, char *mods, char *arg);
void x_equ  (struct tag_op *op, char *label, char *mods, char *arg);
void x_exit (struct tag_op *op, char *label, char *mods, char *arg);
void x_ils  (struct tag_op *op, char *label, char *mods, char *arg);
void x_iss  (struct tag_op *op, char *label, char *mods, char *arg);
void x_libr (struct tag_op *op, char *label, char *mods, char *arg);
void x_lorg (struct tag_op *op, char *label, char *mods, char *arg);
void x_dmes (struct tag_op *op, char *label, char *mods, char *arg);
void x_dn   (struct tag_op *op, char *label, char *mods, char *arg);
void x_dump (struct tag_op *op, char *label, char *mods, char *arg);
void x_pdmp (struct tag_op *op, char *label, char *mods, char *arg);
void x_hdng (struct tag_op *op, char *label, char *mods, char *arg);
void x_list (struct tag_op *op, char *label, char *mods, char *arg);
void x_spac (struct tag_op *op, char *label, char *mods, char *arg);
void x_spr  (struct tag_op *op, char *label, char *mods, char *arg);
void x_ejct (struct tag_op *op, char *label, char *mods, char *arg);
void x_trap (struct tag_op *op, char *label, char *mods, char *arg);
void x_xflc (struct tag_op *op, char *label, char *mods, char *arg);

struct tag_op ops[] = {
    ".OPT", 0,      x_opt,  NONE,       NONE,   0,      /* non-IBM extensions */
    "TRAP", 0,      x_trap, NONE,       NONE,   0,      /* assembler breakpoint trap */
    ".CES", 0,      x_ces,  NONE,       NONE,   0,      /* lets us specify simulated console entry switch values for startup */

    "ABS",  0,      x_abs,  NONE,       NONE,   0,
    "BES",  0,      x_bes,  E,          NONE,   0,      /* standard pseudo-ops */
    "BSS",  0,      x_bss,  E,          NONE,   0,
    "DC",   0,      x_dc,   NONE,       NONE,   0,
    "DEC",  0,      x_dec,  E,          E,      IS_DBL,
    "DECS", 0,      x_decs, E,          E,      IS_DBL, /* this is an IBM 1800 directive */
    "DMES", 0,      x_dmes, ANY,        NONE,   0,
    "DN",   0,      x_dn,   NONE,       NONE,   0,
    "DSA",  0,      x_dsa,  NONE,       NONE,   0,
    "DUMP", 0,      x_dump, NONE,       NONE,   0,
    "EBC",  0,      x_ebc,  NONE,       NONE,   0,
    "EJCT", 0,      x_ejct, NONE,       NONE,   0,
    "END",  0,      x_end,  NONE,       NONE,   0,
    "ENT",  0,      x_ent,  NONE,       NONE,   0,
    "EPR",  0,      x_epr,  NONE,       NONE,   0,
    "EQU",  0,      x_equ,  NONE,       NONE,   0,
    "EXIT", 0,      x_exit, NONE,       NONE,   0,      /* alias for call $exit since we don't have macros yet */
    "FILE", 0,      x_file, NONE,       NONE,   0,
    "HDNG", 0,      x_hdng, ANY,        NONE,   0,
    "ILS",  0,      x_ils,  NUMS,       NONE,   0,
    "ISS",  0,      x_iss,  NUMS,       NONE,   0,
    "LIBF", 0,      x_libf, NONE,       NONE,   0,
    "LIBR", 0,      x_libr, NONE,       NONE,   0,
    "LINK", 0,      x_link, NONE,       NONE,   0,
    "LIST", 0,      x_list, NONE,       NONE,   0,
    "LORG", 0,      x_lorg, NONE,       NONE,   0,
    "ORG",  0,      x_org,  NONE,       NONE,   0,
    "PDMP", 0,      x_pdmp, NONE,       NONE,   0,
    "SPAC", 0,      x_spac, NONE,       NONE,   0,
    "SPR",  0,      x_spr,  NONE,       NONE,   0,
    "XFLC", 0,      x_xflc, NONE,       NONE,   0,

    "A",    0x8000, std_op, ALL,        NONE,   0,      /* standard addressing ops */
    "AD",   0x8800, std_op, ALL,        NONE,   IS_DBL,
    "AND",  0xE000, std_op, ALL,        NONE,   0,
    "BSI",  0x4000, bsi_op, ALL,        NONE,   0,
    "CALL", 0x4000, x_call, ALL,        L,      0,      /* alias for BSI L, or external call */
    "CMP",  0xB000, std_op, ALL,        NONE,   IS_1800,    /* this is an IBM 1800-only instruction */
    "DCM",  0xB800, std_op, ALL,        NONE,   IS_1800,    /* this is an IBM 1800-only instruction */
    "D" ,   0xA800, std_op, ALL,        NONE,   0,
    "EOR",  0xF000, std_op, ALL,        NONE,   0,
    "LD",   0xC000, std_op, ALL,        NONE,   0,
    "LDD",  0xC800, std_op, ALL,        NONE,   IS_DBL,
    "LDS",  0x2000, std_op, NONE,       NONE,   IS_ABS,
    "LDX",  0x6000, std_op, ALL,        NONE,   IS_ABS|NO_IDX,
    "M",    0xA000, std_op, ALL,        NONE,   0,
    "MDX",  0x7000, mdx_op, ALL,        NONE,   0,
    "MDM",  0x7000, mdx_op, L,          L,      0,      /* like MDX L */
    "NOP",  0x1000, std_op, NONE,       NONE,   NO_ARGS,
    "OR",   0xE800, std_op, ALL,        NONE,   0,
    "S",    0x9000, std_op, ALL,        NONE,   0,
    "SD",   0x9800, std_op, ALL,        NONE,   IS_DBL,
    "STD",  0xD800, std_op, ALL,        NONE,   IS_DBL,
    "STO",  0xD000, std_op, ALL,        NONE,   0,
    "STS",  0x2800, std_op, ALL,        NONE,   0,
    "STX",  0x6800, std_op, ALL,        NONE,   NO_IDX,
    "WAIT", 0x3000, std_op, NONE,       NONE,   IS_ABS,
    "XCH",  0x18D0, std_op, NONE,       NONE,   0,      /* same as RTE 16, 18C0 + 10 */
    "XIO",  0x0800, std_op, ALL,        NONE,   IS_DBL,

    "BSC",  0x4800, bsc_op, ALL,        NONE,   0,      /* branch family */
    "BOSC", 0x4840, bsc_op, ALL,        NONE,   0,      /* is BOSC always long form? No. */
    "SKP",  0x4800, bsc_op, NONE,       NONE,   0,      /* alias for BSC one word version */

    "B",    0x4800, b_op,   ALL,        NONE,   0,      /* alias for MDX or BSC L  */
    "BC",   0x4802, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BN",   0x4828, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BNN",  0x4810, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BNP",  0x4808, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BNZ",  0x4820, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BO",   0x4801, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BOD",  0x4804, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BP",   0x4830, std_op, ALL,        L,      0,      /* alias for BSC L  */
    "BZ",   0x4818, std_op, ALL,    L,      0,      /* alias for BSC L  */

    "RTE",  0x18C0, shf_op, IDX X,      X,      0,      /* shift family */
    "SLA",  0x1000, shf_op, IDX X,      X,      0,
    "SLC",  0x10C0, shf_op, IDX X,      X,      0,
    "SLCA", 0x1040, shf_op, IDX X,      X,      0,
    "SLT",  0x1080, shf_op, IDX X,      X,      0,
    "SRA",  0x1800, shf_op, IDX X,      X,      0,
    "SRT",  0x1880, shf_op, IDX X,      X,      0,

    "AIF",  0,      x_aif,  NONE,       NONE,   0,      /* assemble if */
    "AIFB", 0,      x_aifb, NONE,       NONE,   0,      /* assemble if */
    "AGO",  0,      x_ago,  NONE,       NONE,   0,      /* assemble goto */
    "AGOB", 0,      x_agob, NONE,       NONE,   0,      /* assemble goto */
    "ANOP", 0,      x_anop, NONE,       NONE,   0,      /* assemble target */

    NULL    /* end of table */
};

/********************************************************************************************
 * addextn - apply file extension 'extn' to filename 'fname' and put result in 'outbuf'
 * if outbuf is NULL, we allocate a buffer
 ********************************************************************************************/

char *addextn (char *fname, char *extn, char *outbuf)
{
    char *buf, line[500], *c;

    buf = (outbuf == NULL) ? line : outbuf;

    strcpy(buf, fname);                         /* create listfn from first source filename (e.g. xxx.lst); */
    if ((c = strrchr(buf, '\\')) == NULL)
        if ((c = strrchr(buf, '/')) == NULL)
            if ((c = strrchr(buf, ':')) == NULL)
                c = buf;

    if ((c = strrchr(c, '.')) == NULL)
        strcat(buf, extn);
    else
        strcpy(c, extn);

    return (outbuf == NULL) ? astring(line) : outbuf;
}

/********************************************************************************************
 * controlcard - examine an assembler control card (* in column 1)
 ********************************************************************************************/

BOOL controlcard (char *line)
{
    if (strnicmp(line, "*LIST", 5) == 0) {      /* turn on listing file even if not specified on command line */
        do_list = list_on = TRUE;
        return TRUE;
    }
    
    if (strnicmp(line, "*XREF", 5) == 0) {
        do_xref = TRUE;
        return TRUE;
    }

    if (strnicmp(line, "*PRINT SYMBOL TABLE", 19) == 0) {
        do_syms = TRUE;
        return TRUE;
    }

    if (strnicmp(line, "*SAVE SYMBOL TABLE", 18) == 0) {
        savetable = TRUE;
        return TRUE;
    }

    if (strnicmp(line, "*SYSTEM SYMBOL TABLE", 20) == 0) {
        preload = TRUE;
        preload_symbols();
        return TRUE;
    }

    return FALSE;
}

/********************************************************************************************
 * stuff - insert characters into a line
 ********************************************************************************************/

void stuff (char *buf, char *tok, int maxchars)
{
    while (*tok) {
        *buf++ = *tok++;
        
        if (maxchars)
            if (--maxchars <= 0)
                break;  
    }
}

/********************************************************************************************
 * format_line - construct a source code input line from components
 ********************************************************************************************/

void format_line (char *buf, char *label, char *op, char *mods, char *args, char *remarks)
{
    int i;

    if (tabformat) {
        sprintf(buf, "%s\t%s\t%s\t%s\t%s", label, op, mods, args, remarks);
    }
    else {
        for (i = 0; i < 72; i++)
            buf[i] = ' ';
        buf[i] = '\0';

        stuff(buf+20, label, 5);
        stuff(buf+26, op,    4);
        stuff(buf+31, mods,  2);
        stuff(buf+34, args,  72-34);
    }
}

/********************************************************************************************
 * lookup_op - find an opcode
 ********************************************************************************************/

struct tag_op * lookup_op (char *mnem)
{
    struct tag_op *op;
    int i;

    for (op = ops; op->mnem != NULL; op++) {
        if ((i = strcmp(op->mnem, mnem)) == 0)
            return op;

        if (i > 0)
            break;
    }
    return NULL;
}

/********************************************************************************************
 * bincard - routines to write IBM 1130 Card object format
 ********************************************************************************************/

unsigned short bincard[54];     /* the 54 data words that can fit on a binary format card */
char binflag[45];               /* the relocation flags of the 45 buffered object words (0, 1, 2, 3) */
int bincard_n   = 0;            /* number of object words stored in bincard (0-45) */
int bincard_seq = 0;            /* card output sequence number */
int bincard_org = 0;            /* origin of current card-full */
int bincard_maxaddr = 0;
BOOL bincard_first = TRUE;      /* TRUE when we're to write the program type card */

/********************************************************************************************
 * bincard_init - prepare a new object data output card
 ********************************************************************************************/

void bincard_init (void)
{
    memset(bincard, 0, sizeof(bincard));        /* clear card data */
    memset(binflag, 0, sizeof(binflag));        /* clear relocation data */
    bincard_n = 0;                              /* no data */
    bincard[0] = bincard_org;                   /* store load address */
    bincard_maxaddr = MAX(bincard_maxaddr, bincard_org-1);  /* save highest address written-to (this may be a BSS) */
}

/********************************************************************************************
 * binard_writecard - emit a card. sbrk_text = NULL for normal data cards, points to comment text for sbrk card
 * note: sbrk_text if not NULL MUST be a writeable buffer of at LEAST 71 characters
 ********************************************************************************************/

void bincard_writecard (char *sbrk_text)
{
    unsigned short binout[80];
    char ident[12];
    int i, j;
                                    /* this is an SBRK record */
    if (sbrk_text != NULL) {        /* sbrk card has 4 binary words followed by comment text */
        for (j = 66; j < 71; j++)   /* be sure input columns 67..71 are nonblank (have version number) */
            if (sbrk_text[j] <= ' ')
                break;
        
        if (j < 71)                 /* sbrk card didn't have the info, stuff in current release */
            for (j = 0; j < 5; j++)
                sbrk_text[66+j] = dmsversion[j];

        binout[0] = 0;
        binout[1] = 0;
        binout[2] = 0;
        binout[3] = 0x1000;
        // V1.19 - in previous versions of asm1130, binout[4] was left unset, random
        binout[4] = 0;

        sbrk_text += 5;             /* start at the real column 6 (after *SBRK */
        for (j = 5;  j < 72; j++)
            binout[j] = (*sbrk_text) ? ascii_to_hollerith(*sbrk_text++) : 0;

    }
    else {                          /* binary card format packs 54 words into 72 columns */
        for (i = j = 0; i < 54; i += 3, j += 4) {
            binout[j  ] = ( bincard[i]          & 0xFFF0);
            binout[j+1] = ((bincard[i]   << 12) & 0xF000)  | ((bincard[i+1] >> 4) & 0x0FF0);
            binout[j+2] = ((bincard[i+1] <<  8) & 0xFF00)  | ((bincard[i+2] >> 8) & 0x00F0);
            binout[j+3] = ((bincard[i+2] <<  4) & 0xFFF0);
        }
    }

    sprintf(ident, "%08ld", ++bincard_seq);         /* append sequence text */
    memmove(ident, progname, MIN(strlen(progname), 4));

    for (i = 0; i < 8; i++)
        binout[j++] = ascii_to_hollerith(ident[i]);
    
    fxwrite(binout, sizeof(binout[0]), 80, fout);       /* write card image */
}

/********************************************************************************************
 * binard_writedata - emit an object data card
 ********************************************************************************************/

void bincard_writedata (void)
{
    unsigned short rflag = 0;
    int i, j, nflag = 0;

    bincard[1] = 0;                             /* checksum */
    bincard[2] = 0x0A00 | bincard_n;            /* data card type + word count */

    for (i = 0, j = 3; i < bincard_n; i++) {    /* construct relocation indicator bitmap */
        if (nflag == 8) {
            bincard[j++] = rflag;
            rflag = 0;
            nflag = 0;
        }
        rflag = (rflag << 2) | (binflag[i] & 3);
        nflag++;
    }

    if (nflag > 0)
        bincard[j] = rflag << (16 - 2*nflag);

    bincard_writecard(FALSE);                   /* emit the card */
}

/********************************************************************************************
 * bincard_flush - flush any pending binary data
 ********************************************************************************************/

void bincard_flush (void)
{
    if (bincard_n > 0)
        bincard_writedata();

    bincard_init();
}

/********************************************************************************************
 * bincard_sbrk - emit an SBRK card
 ********************************************************************************************/

void bincard_sbrk (char *line)
{
    if (bincard_first)
        bincard_typecard();
    else
        bincard_flush();

    bincard_writecard(line);
}

/********************************************************************************************
 * bincard_setorg - set the origin
 ********************************************************************************************/

void bincard_setorg (int neworg)
{
    bincard_org = neworg;           /* set origin for next card */
    bincard_flush();                /* flush any current data & store origin */
}

/********************************************************************************************
 * bincard_endcard - write end of program card
 ********************************************************************************************/

void bincard_endcard (void)
{
    bincard_flush();

    bincard[0] = (bincard_maxaddr + 2) & ~1;    /* effective length: add 1 to max origin, then 1 more to round up */
    bincard[1] = 0;
    bincard[2] = 0x0F00;
    bincard[3] = pta & 0xFFFF;

    bincard_writecard(NULL);
}

/********************************************************************************************
 * bincard_typecard - write the program type 
 ********************************************************************************************/

void bincard_typecard (void)
{
    int i;

    if (! bincard_first) 
        return;

    bincard_first = FALSE;

    memset(bincard, 0, sizeof(bincard));

    bincard[2] = (unsigned short) ((progtype << 8) | intmode | realmode);

/* all indices not listed are documented as 'reserved' */

    switch (progtype) {
        case PROGTYPE_ABSOLUTE:
        case PROGTYPE_RELOCATABLE:
/*          bincard[ 4] = 0;        // length of common (fortran only) */
            bincard[ 5] = 0x0003;
/*          bincard[ 6] = 0;        // length of work area (fortran only) */
            bincard[ 8] = ndefined_files;
            namecode(&bincard[9], progname);
            bincard[11] = (pta < 0) ? 0 : pta;
            break;

        case PROGTYPE_LIBF:
        case PROGTYPE_CALL:
            bincard[ 5] = 3*nentries;
            for (i = 0; i < nentries; i++) {
                namecode(&bincard[9+3*i], entry[i]->name);
                bincard[11+3*i] = entry[i]->value;
            }
            break;

        case PROGTYPE_ISSLIBF:
        case PROGTYPE_ISSCALL:
            bincard[ 5] = 6+nintlevels;
            namecode(&bincard[9], entry[0]->name);
            bincard[11] = entry[0]->value;
            bincard[12] = iss_number + ISTV;            /* magic number ISTV is 0x33 in DMS R2V12 */
            bincard[13] = iss_number;
            bincard[14] = nintlevels;
            bincard[15] = intlevel_primary;
            bincard[16] = intlevel_secondary;
            bincard[29] = 1;
            break;

        case PROGTYPE_ILS:
            bincard[ 2] = (unsigned short) (progtype << 8);
            bincard[ 5] = 4;
            bincard[12] = intlevel_primary;
            break;

        default:
            bail("in bincard_typecard, can't happen");
    }

    bincard[1] = 0;     /* checksum */

    bincard_writecard(NULL);

    bincard_init();
}

/********************************************************************************************
 * bincard_writew - write a word to the current output card.
 ********************************************************************************************/

void bincard_writew (int word, RELOC relative)
{
    if (pass != 2)
        return;

    if (bincard_first)
        bincard_typecard();
    else if (bincard_n >= 45)               /* flush full card buffer */
        bincard_flush();

    binflag[bincard_n] = relative & 3;      /* store relocation bits and data word */
    bincard[9+bincard_n++] = word;

    if (relative != LIBF) {
        bincard_maxaddr = MAX(bincard_maxaddr, bincard_org);
        bincard_org++;
    }
}

/********************************************************************************************
 * writetwo - notification that we are about to write two words which must stay together
 ********************************************************************************************/

void writetwo (void)
{
    if (pass == 2 && outmode == OUTMODE_BINARY && bincard_n >= 44)
        bincard_flush();
}

/********************************************************************************************
 * handle_sbrk - handle an SBRK directive.
 * This was not part of the 1130 assembler; they must have assembled DMS on a 360 using a cross assembler
 ********************************************************************************************/

void handle_sbrk (char *line)
{
    char rline[90];

    if (pass != 2)
        return;

    strncpy(rline, line, 81);           /* get a copy and pad it if necessary to 80 characters */
    rline[80] = '\0';
    while (strlen(rline) < 80)
        strcat(rline, " ");

    switch (outmode) {
        case OUTMODE_LOAD:
            fprintf(fout, "#SBRK%s\n", trim(rline+5));

        case OUTMODE_BINARY:
            bincard_sbrk(rline);
            break;

        default:
            bail("in handle_sbrk, can't happen");
    }
}

/********************************************************************************************
 * namecode - turn a string into a two-word packed name
 ********************************************************************************************/

void namecode (unsigned short *words, char *tok)
{
    long val = 0;
    int i, ch;

    for (i = 0; i < 5; i++) {                   /* pick up bits */
        if (*tok)
            ch = *tok++;
        else
            ch = ' ';

        val = (val << 6) | (ascii_to_ebcdic_table[ch] & 0x3F);
    }

    words[0] = (unsigned short) (val >> 16);
    words[1] = (unsigned short) val;
}

/********************************************************************************************
 * parse_line - parse one input line.
 ********************************************************************************************/

void parse_line (char *line)
{
    char label[100], mnem[100], arg[200], mods[20], *c;
    struct tag_op *op;

    if (line[0] == '/' && line[1] == '/')       /* job control card? probably best to ignore it */
        return;

    if (line[0] == '*') {                       /* control card comment or comment in tab-format file */
        if (check_control)                      /* pay attention to control cards only at top of file */
            if (! controlcard(line))
                check_control = FALSE;          /* first non-control card shuts off sensitivity to them */

        if (strnicmp(line+1, "SBRK", 4) == 0)
            handle_sbrk(line);

        return;
    }

    check_control = FALSE;                      /* non-control card, consider them no more */

    label[0] = '\0';                            /* prepare to extract fields */
    mods[0]  = '\0';
    mnem[0]  = '\0';
    arg[0]   = '\0';

    if (tabformat || strchr(line, '\t') != NULL) {  /* if input line has tabs, parse loosely */
        tabformat = TRUE;                           /* this is a tab-formatted file */

        for (c = line; *c && *c <= ' '; c++)    /* find first nonblank */
            ;

        if (*c == '*' || ! *c)                  /* ignore as a comment */
            return;

        tabtok(line, label, 0, NULL);
        tabtok(line, mnem,  1, NULL);
        tabtok(line, mods,  2, NULL);
        tabtok(line, arg,   3, opfield);
    }
    else {                                      /* if no tabs, use strict card-column format */
        if (line[20] == '*')                    /* comment */
            return;

        line[72] = '\0';                        /* clip off sequence */

        coltok(line, label, 21, 25, TRUE, NULL);
        coltok(line, mnem,  27, 30, TRUE, NULL);
        coltok(line, mods,  32, 33, TRUE, NULL);
        coltok(line, arg,   35, 72, FALSE, opfield);
    }

    if (*label)                                 /* display org in any line with a label */
        setw(0, org+listoffset, FALSE);

    if (! *mnem) {                              /* label w/o mnemonic, just define the symbol */
        if (*label)
            set_symbol(label, org, TRUE, relocate);
        return;
    }

    if ((op = lookup_op(mnem)) == NULL) {       /* look up mnemonic */
        if (*label)
            set_symbol(label, org, TRUE, relocate);/* at least define the label */

        asm_error("Unknown opcode '%s'", mnem);
        return;
    }

    if (op->flags & TRAP)                       /* assembler debugging breakpoint */
        x_trap(op, label, mods, arg);

    if (*op->mods_allowed != '\xFF') {          /* validate modifiers against list of allowed characters */
        for (c = mods; *c; ) {
            if (strchr(op->mods_allowed, *c) == NULL) {
                asm_warning("Modifier '%c' not permitted", *c);
                strcpy(c, c+1);                 /* remove it and keep parsing */
            }
            else
                c++;
        }
    }

    strcat(mods, op->mods_implied);             /* tack on implied modifiers */

    if (strchr(mods, 'I'))                      /* indirect implies long */
        strcat(mods, "L");

    requires_even_address = op->flags & IS_DBL;

    org_advanced = strchr(mods, 'L') ? 2 : 1;   /* by default, * means address + 1 or 2. Sometimes it doesn't */
    (op->handler)(op, label, mods, arg);

    if ((op->flags & IS_1800) && ! enable_1800)
        asm_warning("%s is IBM 1800-specific; use the -8 command line option", op->mnem);
}

/********************************************************************************************
 * get one input line from current file or macro
 ********************************************************************************************/

BOOL get_line (char *buf, int nbuf, BOOL onelevel)
{
    char *retval;

    if (ended)                              /* we hit the END command */
        return FALSE;
    
    /* if macro active, return line from macro buffer, otherwise read from file */
    /* do not pop end-of-macro if onelevel is TRUE  */

    if ((retval = fgets(buf, nbuf, fin)) == NULL)
        return FALSE;

    lno++;                                  /* count the line */
    return TRUE;
}

/********************************************************************************************
 * proc - process one pass of one source file
 ********************************************************************************************/

void proc (char *fname)
{                                                                                                                   
    char line[256], *c;
    int i;

    if (strchr(fname, '.') == NULL)             /* if input file has no extension, */
        addextn(fname, ".asm", curfn);          /* set appropriate file extension */
    else
        strcpy(curfn, fname);                   /* otherwise use extension specified */

/* let's leave filename case alone even if it doesn't matter */
/*#if (defined(_WIN32) || defined(VMS)) */
/*  upcase(curfn);                              // only force uppercase of name on Windows and VMS */
/*#endif */

    if (progname[0] == '\0') {                  /* pick up primary filename */
        if ((c = strrchr(curfn, '\\')) == NULL)
            if ((c = strrchr(curfn, '/')) == NULL)
                if ((c = strrchr(curfn, ':')) == NULL)
                    c = curfn;

        strncpy(progname, c, sizeof(progname)); /* take name after path */
        progname[sizeof(progname)-1] = '\0';
        if ((c = strchr(progname, '.')) != NULL)/* remove extension */
            *c = '\0';
    }

    lno   = 0;                                  /* reset global input line number */
    ended = FALSE;                              /* have not seen END statement */

    if (listfn == NULL)                         /* if list file name is undefined, */
        listfn = addextn(fname, ".lst", NULL);  /* create from first filename */

    if (verbose)
        fprintf(stderr, "--- Starting file %s pass %d\n", curfn, pass);

    if ((fin = fopen(curfn, "r")) == NULL) {
        perror(curfn);                          /* oops */
        exit(1);
    }

    if (flist) {                                /* put banner in listing file */
        strcpy(listline,"=== FILE ======================================================================");
        for (i = 9, c = curfn; *c;)
            listline[i++] = *c++;
        listline[i] = ' ';
        fputs(listline, flist);
        putc('\n', flist);
        list_on = TRUE;
    }
                                                /* read all lines till EOF or END statement */
    while (get_line(line, sizeof(line), FALSE)) {
        prep_line(line);                        /* preform standard line prep */
        parse_line(line);                       /* parse */
        listout(FALSE);                         /* complete the listing */
    }

    fclose(fin);

    if (n_literals > 0) {                       /* force out any pending literal constants at end of file */
        output_literals(TRUE);
        listout(FALSE);
    }
}

/********************************************************************************************
 * prep_line - prepare input line for parsing
 ********************************************************************************************/

void prep_line (char *line)
{
    char *c;

    upcase(line);                           /* uppercase it */
    nwout = 0;                              /* number of words output so far */
    line_error = FALSE;                     /* no errors on this line so far */

    for (c = line; *c; c++) {               /* truncate at newline */
        if (*c == '\r' || *c == '\n') {
            *c = '\0';
            break;
        }
    }

    if (flist && list_on) {                 /* construct beginning of listing line */
        if (tabformat)
            sprintf(listline, LINEFORMAT, lno, detab(line));
        else {
            if (strlen(line) > 20)          /* get the part where the commands start */
                c = line+20;
            else
                c = "";

            sprintf(listline, LINEFORMAT, lno, c);
            stuff(listline, line, 20);      /* stuff the left margin in to the left side */
        }
    }
}

/********************************************************************************************
 * opcmp - operand name comparison routine for qsort
 ********************************************************************************************/

int opcmp (const void *a, const void *b)
{
    return strcmp(((struct tag_op *) a)->mnem, ((struct tag_op *) b)->mnem);
}

/********************************************************************************************
 * preload_symbols - load a saved symbol table
 ********************************************************************************************/

void preload_symbols (void)
{
    FILE *fd;
    char str[200], sym[20];
    int v;
    static BOOL preloaded_already = FALSE;

    if (pass > 1 || preloaded_already)
        return;

    preloaded_already = TRUE;

    if ((fd = fopen(SYSTEM_TABLE, "r")) == NULL)                /* read the system symbol tabl */
        perror(SYSTEM_TABLE);
    else {
        while (fgets(str, sizeof(str), fd) != NULL) {
            if (sscanf(str, "%s %x", sym, &v) == 2)
                set_symbol(sym, v, TRUE, FALSE);
        }
        fclose(fd);
    }
}

/********************************************************************************************
 * save_symbols - save a symbol table
 ********************************************************************************************/

void save_symbols (void)
{
    FILE *fd;
    char str[20];
    PSYMBOL s;

    if (relocate) {
        fprintf(stderr, "Can't save symbol table unless ABS assembly\n");
        return;
    }

    if ((fd = fopen(SYSTEM_TABLE, "r")) != NULL) {
        fclose(fd);
        if (saveprompt) {
            printf("Overwrite system symbol table %s? ", SYSTEM_TABLE);
            fgets(str, sizeof(str), stdin);
            if (str[0] != 'y' && str[0] != 'Y')
                return;
        }
        unlink(SYSTEM_TABLE);
    }

    if ((fd = fopen(SYSTEM_TABLE, "w")) == NULL) {
        perror(SYSTEM_TABLE);
        return;
    }

    for (s = symbols; s != NULL; s = s->next)
        fprintf(fd, "%-5s %04X\n", s->name, s->value);

    fclose(fd);
}

/********************************************************************************************
 * startpass - initialize data structures, prepare to start a pass
 ********************************************************************************************/

void startpass (int n)
{
    int nops;
    struct tag_op *p;

    pass       = n;                             /* reset globals: pass number */
    nerrors    = 0;                             /* error count */
    org        = 0;                             /* load address (origin) */
    lno        = 0;                             /* input line number */
    relocate   = TRUE;                          /* relocatable assembly mode */
    assembled  = FALSE;                         /* true if any output has been generated */
    list_on    = do_list;                       /* listing enable */
    dmes_saved = FALSE;                         /* partial character strings output */

    n_literals = 0;                             /* literal values pending output */
    lit_tag    = 0;

    if (pass == 1) {                                    /* first pass only */
        for (nops = 0, p = ops; p->mnem != NULL; p++, nops++)           /* count opcodes */
            ;

        qsort(ops, nops, sizeof(*p), opcmp);                            /* sort the opcode table */

        if (preload)
            preload_symbols();
    }
    else {                                              /* second pass only */
        if (outfn == NULL)
            outfn = addextn(curfn, (outmode == OUTMODE_LOAD) ? ".out" : ".bin" , NULL);

        if ((fout = fopen(outfn, OUTWRITEMODE)) == NULL) {              /* open output file */
            perror(outfn);
            exit(1);
        }

        if (do_list) {                                                  /* open listing file */
            if ((flist = fopen(listfn, "w")) == NULL) {
                perror(listfn);
                exit(1);
            }
            listhdr();                                                  /* print banner */
        }
    }
}

/********************************************************************************************
 * x_dc - DC define constant directive
 ********************************************************************************************/

void x_dc (struct tag_op *op, char *label, char *mods, char *arg)
{   
    EXPR expr;
/*  char *tok; */

    org_advanced = 1;                           /* assume * means this address+1 */
/* doesn't make sense, but I think I found DMS listings to support it */

    if (strchr(mods, 'E') != NULL)              /* force even address */
        org_even();

    setw(0, org+listoffset, FALSE);             /* display org in listing line */

    if (*label)                                 /* define label */
        set_symbol(label, org, TRUE, relocate);

    getexpr(arg, FALSE, &expr);
    writew(expr.value, expr.relative);          /* store value */
}

/********************************************************************************************
 * x_dec - DEC define double word constant directive.
 ********************************************************************************************/

/* wd[0]: 8 unused bits | characteristic (= exponent+128)
 * wd[1]: sign + 15 msb of mantissa in 2's complement 
 * wd[2]: 16 lsb of mantissa
 * NOTE: these are wrong with Fixed point numbers */

void convert_double_to_extended (double d, unsigned short *wd)
{
    int neg, exp;
    unsigned long mantissa;
    unsigned char *byte = (unsigned char *) &d;

    if (d == 0.) {
        wd[0] = wd[1] = wd[2] = 0;
        return;
    }
    /*                    7         6         5         4             0 */
    /* d = ansi real*8    SXXX XXXX XXXX MMMM MMMM MMMM MMMM MMMM ... MMMM MMMM */

    neg = byte[7] & 0x80;
    exp = ((byte[7] & 0x7F) << 4) | ((byte[6] & 0xF0) >> 4);    /* extract exponent */
    exp -= 1023;                                                /* remove bias */

    exp++;                                                      /* shift to account for implied 1 we added */

    /* get 32 bits worth of mantissa. add the implied point */
    mantissa = 0x80000000L | ((byte[6] & 0x0F) << 27) | (byte[5] << 19) | (byte[4] << 11) | (byte[3] << 3) | ((byte[2] & 0xE0) >> 5);

    if (mantissa & (0x80000000L >> 31))                         /* keep 31 bits, round if necessary */
        mantissa += (0x80000000L >> 31);

    mantissa >>= (32-31);                                       /* get into low 31 bits */

    /* now turn into IBM 1130 extended precision */

    exp += 128;

    if (neg)
        mantissa = (unsigned long) (- (long) mantissa);         /* two's complement */

    wd[0] = (unsigned short) (exp & 0xFF);
    wd[1] = (unsigned short) ((neg ? 0x8000 : 0) | ((mantissa >> (31-15)) & 0x7FFF));
    wd[2] = (unsigned short) (mantissa & 0xFFFF);
}

/********************************************************************************************
 * convert_double_to_standard - 
 ********************************************************************************************/

void convert_double_to_standard (double d, unsigned short *wd)
{
    int neg, exp;
    unsigned long mantissa;
    unsigned char *byte = (unsigned char *) &d;

    if (d == 0.) {
        wd[0] = wd[1] = 0;
        return;
    }
    /*                    7         6         5         4             0 */
    /* d = ansi real*8    SXXX XXXX XXXX MMMM MMMM MMMM MMMM MMMM ... MMMM MMMM */

    neg = byte[7] & 0x80;
    exp = ((byte[7] & 0x7F) << 4) | ((byte[6] & 0xF0) >> 4);    /* extract exponent */
    exp -= 1023;                                                /* remove bias */

    exp++;                                                      /* shift to account for implied 1 we added */

    /* get 32 bits worth of mantissa. add the implied point */
    mantissa = 0x80000000L | ((byte[6] & 0x0F) << 27) | (byte[5] << 19) | (byte[4] << 11) | (byte[3] << 3) | ((byte[2] & 0xE0) >> 5);

/*  if (mantissa & (0x80000000L >> 23))                         // keep 23 bits, round if necessary */
/*      mantissa += (0x80000000L >> 23); */

/* DEBUG */
/*  printf("%8.4lf: %08lX %d\n", d, mantissa, exp); */

    mantissa >>= (32-23);                                       /* get into low 23 bits */

    /* now turn into IBM 1130 standard precision */

    exp += 128;

    if (neg)
        mantissa = (unsigned long) (- (long) mantissa);         /* two's complement */

    wd[0] = (unsigned short) ((neg ? 0x8000 : 0) | ((mantissa >> (23-15)) & 0x7FFF));
    wd[1] = (unsigned short) ((mantissa & 0x00FF) << 8) | (exp & 0xFF);

/* DEBUG */
/* printf("       D %04x%04X\n", wd[0], wd[1]); */
}

/********************************************************************************************
 * convert_double_to_fixed - 
 ********************************************************************************************/

void convert_double_to_fixed (double d, unsigned short *wd, int bexp)
{
    int neg, exp, rshift;
    unsigned long mantissa;
    unsigned char *byte = (unsigned char *) &d;

    if (d == 0.) {
        wd[0] = wd[1] = 0;
        return;
    }

    /* note: we assume that this computer uses ANSI floating point */

    /*                    7         6         5         4             0 */
    /* d = ansi real*8    SXXX XXXX XXXX MMMM MMMM MMMM MMMM MMMM ... MMMM MMMM */

    neg = byte[7] & 0x80;
    exp = ((byte[7] & 0x7F) << 4) | ((byte[6] & 0xF0) >> 4);    /* extract exponent */
    exp -= 1023;                                                /* remove bias */

    exp++;                                                      /* shift to account for implied 1 we added */

    /* get 32 bits worth of mantissa. add the implied point */
    mantissa = 0x80000000L | ((byte[6] & 0x0F) << 27) | (byte[5] << 19) | (byte[4] << 11) | (byte[3] << 3) | ((byte[2] & 0xE0) >> 5);

    mantissa >>= 1;                                             /* shift it out of the sign bit */

/* DEBUG */
/* printf("%8.4lf: %08lX %d\n", d, mantissa, exp); */

    rshift = bexp - exp;

    if (rshift > 0) {
        mantissa >>= rshift;
    }
    else if (rshift < 0) {
        mantissa >>= (-rshift);
        asm_warning("Fixed point overflow");
    }

    if (neg)
        mantissa = (unsigned long) (- (long) mantissa);         /* two's complement */

/* DEBUG */
/* printf("       B %08lX\n", mantissa); */

    wd[0] = (unsigned short) ((mantissa >> 16) & 0xFFFF);       /* return all of the bits; no exponent here */
    wd[1] = (unsigned short) (mantissa & 0xFFFF);
}

/********************************************************************************************
 * getDconstant - 
 ********************************************************************************************/

void getDconstant (char *tok, unsigned short *wd)
{
    unsigned long l;
    char *b, *fmt;
    double d;
    int bexp;
    BOOL fixed = FALSE;

    wd[0] = 0;
    wd[1] = 0;

    tok = skipbl(tok);
    if (! *tok)
        return;                                             /* no argument is the same as 0 */

    if (strchr(tok, '.') == NULL && strchr(tok, 'B') == NULL && strchr(tok, 'E') == NULL) {
        fmt = "%ld";                                        /* not a floating point number -- parse as a 32-bit integer */
        if (*tok == '/') {                                  /* I don't see that this is legal but can't hurt to allow it */
            fmt = "%lx";
            tok++;
        }

        if (sscanf(tok, fmt, &l) != 1)
            asm_error("Syntax error in constant");
        else {
            wd[0] = (unsigned short) ((l >> 16) & 0xFFFF);  /* high word */
            wd[1] = (unsigned short) (l & 0xFFFF);          /* low word */
        }
    }
    else {
        if ((b = strchr(tok, 'B')) != NULL) {
            fixed = TRUE;
            bexp  = atoi(b+1);                              /* get binary exponent, after the B */
            *b    = '\0';                                   /* truncate at the B */
        }

        if (sscanf(tok, "%lg", &d) != 1)
            asm_error("Syntax error in constant");
        else if (fixed)
            convert_double_to_fixed(d, wd, bexp);
        else
            convert_double_to_standard(d, wd);
    }
}

/********************************************************************************************
 * x_dec - If the input value is an integer with no decimal point and no B or E,
 * DEC generates a double INTEGER value.
 * IBM documentation ranges from ambiguous to wrong on this point, but
 * examination of the DMS microfiche supports this.
 ********************************************************************************************/

void x_dec (struct tag_op *op, char *label, char *mods, char *arg)
{   
    unsigned short wd[2];

    org_advanced = 2;                   /* assume * means address after this location, since it's +1 for dc? */

    org_even();                         /* even address is implied */
    setw(0, org+listoffset, FALSE);     /* display the origin */

    if (*label)                         /* define label */
        set_symbol(label, org, TRUE, relocate);

    getDconstant(arg, wd);
    writew(wd[0], FALSE);               /* write hiword, then loword */
    writew(wd[1], FALSE);
}

/********************************************************************************************
 * DECS directive. Writes just the high word of a DEC value
 ********************************************************************************************/

void x_decs (struct tag_op *op, char *label, char *mods, char *arg)
{   
    unsigned short wd[2];

    org_advanced = 1;                   /* assume * means address after this location */

    setw(0, org+listoffset, FALSE);     /* display the origin */

    if (*label)                         /* define label */
        set_symbol(label, org, TRUE, relocate);

    getDconstant(arg, wd);
    writew(wd[0], FALSE);               /* write hiword ONLY */
}

/********************************************************************************************
 * x_xflc - extended precision constant. (Note: if there is no argument, we must write a zero value)
 ********************************************************************************************/

void x_xflc (struct tag_op *op, char *label, char *mods, char *arg)
{   
    char *tok, *b;
    double d;
    int bexp, fixed;
    unsigned short wd[3];

    org_advanced = 2;                   /* who knows? */

    setw(0, org+listoffset, FALSE);     /* display the origin */

    if (*label)                         /* define label */
        set_symbol(label, org, TRUE, relocate);

    if ((tok = strtok(arg, ",")) == NULL) { /* pick up value */
        tok = "0";                      /* if there is no argument at all, spit out a zero */
/*      fprintf(stderr, ">>> ENCOUNTERED XFLC WITH NO ARGUMENT IN %s -- THIS WAS BUGGY BEFORE\n", curfn); */
    }

    bexp = 0;                           /* parse the value */
    if ((b = strchr(tok, 'B')) != NULL) {
        bexp = atoi(b+1);
        fixed = TRUE;
        *b = '\0';                      /* truncate at the b */
        asm_warning("Fixed point extended floating constant?");
    }

    if (sscanf(tok, "%lg", &d) != 1) {
        asm_error("Syntax error in constant");
        d = 0.;
    }

    convert_double_to_extended(d, wd);

    writew(wd[0], ABSOLUTE);
    writew(wd[1], ABSOLUTE);
    writew(wd[2], ABSOLUTE);
}

/********************************************************************************************
 * x_equ - EQU directive
 ********************************************************************************************/

void x_equ (struct tag_op *op, char *label, char *mods, char *arg)
{   
    EXPR expr;

    org_advanced = 0;                   /* * means this address, not incremented */

    getexpr(arg, FALSE, &expr);

    setw(0, expr.value, expr.relative); /* show this as address */

    if (*label)                         /* EQU is all about defining labels, better have one */
        set_symbol(label, expr.value, TRUE, expr.relative);
/*  else                                // IBM assembler doesn't complain about this */
/*      asm_error("EQU without label?"); */
}

/********************************************************************************************
 * x_lorg - LORG directive -- output queued literal values
 ********************************************************************************************/

void x_lorg  (struct tag_op *op, char *label, char *mods, char *arg)
{
    org_advanced = 0;                   /* * means this address (not used, though) */
    output_literals(FALSE);             /* generate .DC's for queued literal values */
}

/********************************************************************************************
 * x_abs - ABS directive
 ********************************************************************************************/

void x_abs (struct tag_op *op, char *label, char *mods, char *arg)
{
    if (assembled)
        asm_error("ABS must be first statement");

    relocate = ABSOLUTE;

    switch (progtype) {
        case PROGTYPE_ABSOLUTE:
        case PROGTYPE_RELOCATABLE:
            progtype = PROGTYPE_ABSOLUTE;       /* change program type, still assumed to be mainline */
            break;

        case PROGTYPE_LIBF:
        case PROGTYPE_CALL:
        case PROGTYPE_ISSLIBF:
        case PROGTYPE_ISSCALL:
        case PROGTYPE_ILS:
            asm_error("ABS not allowed with LIBF, ENT, ILS or ISS");
            break;

        default:
            bail("in x_libr, can't happen");
    }
}

/********************************************************************************************
 * x_call - ORG pseudo-op
 ********************************************************************************************/

void x_call (struct tag_op *op, char *label, char *mods, char *arg)
{
    unsigned short words[2];
    static struct tag_op *bsi = NULL;

    if (*label)                         /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (! *arg) {
        asm_error("CALL missing argument");
        return;
    }

    if (pass == 1) {                        /* it will take two words in any case */
        org += 2;
        return;
    }

    setw(0, org+listoffset, FALSE);         /* display origin */

    if (lookup_symbol(arg, FALSE) != NULL) {/* it's a defined symbol? */
        if (bsi == NULL)
            if ((bsi = lookup_op("BSI")) == NULL)
                bail("Can't find BSI op");

        (bsi->handler)(bsi, "", "L", arg);
    }
    else if (outmode == OUTMODE_BINARY) {
        namecode(words, arg);               /* emit namecode for loader */

        writetwo();
        writew(words[0], CALL);
        writew(words[1], ABSOLUTE);
    }
    else {
        writew(0x3000, 0);                  /* write two WAIT commands */
        writew(0x3000, 0);
        asm_warning("CALL <libroutine> is not valid for simulator load output format, emitting WAIT");
    }
}

/********************************************************************************************
 * x_org - ORG directive
 ********************************************************************************************/

void x_org (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;

    org_advanced = 0;                   /* * means this address */

    if (*label)                         /* label is defined BEFORE the new origin is set!!! */
        set_symbol(label, org, TRUE, relocate);

    if (getexpr(arg, FALSE, &expr) != S_DEFINED)
        return;

    setorg(expr.value);                 /* set origin to this value */
}

/********************************************************************************************
 * x_end - END directive
 ********************************************************************************************/

void x_end (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;

    org_advanced = 0;                   /* * means this address */

    if (*arg) {                         /* they're specifing the program start address */
        if (getexpr(arg, FALSE, &expr) == S_DEFINED)
            pta = expr.value;
    }

    if (*label)                         /* define label */
        set_symbol(label, org, TRUE, relocate);

    setw(0, org+listoffset, FALSE);     /* display origin */

    ended = TRUE;                       /* assembly is done, stop reading file */
}

/********************************************************************************************
 * x_ent - ENT op
 ********************************************************************************************/

void x_ent (struct tag_op *op, char *label, char *mods, char *arg)
{
    PSYMBOL s;

    org_advanced = 0;                   /* * means this address */

    if (pass < 2)
        return;

/*  if (*label)                         // define label */
/*      set_symbol(label, org, TRUE, relocate); */
/* */
/*  setw(0, org+listoffset, FALSE);     // display origin */

    if (! *arg)
        asm_error("No entry label specified");

    else if ((s = lookup_symbol(arg, FALSE)) == NULL)
        asm_error("Entry symbol %s not defined", arg);

    else if (nentries >= MAXENTRIES)
        asm_error("Too many entries, limit is %d", MAXENTRIES);

    else
        entry[nentries++] = s;          /* save symbol pointer */

    switch (progtype) {
        case PROGTYPE_ABSOLUTE:
            asm_error("ENT not allowed with ABS");
            break;
        case PROGTYPE_RELOCATABLE:
            progtype = PROGTYPE_CALL;
            break;
        case PROGTYPE_LIBF:
        case PROGTYPE_CALL:
        case PROGTYPE_ISSLIBF:
        case PROGTYPE_ISSCALL:
            break;
        case PROGTYPE_ILS:
            asm_error("Can't mix ENT and ILS, can you?");
            break;
        default:
            bail("in x_libr, can't happen");
    }
}

/********************************************************************************************
 * declare a libf-type subprogram
 ********************************************************************************************/

void x_libr (struct tag_op *op, char *label, char *mods, char *arg)
{
    switch (progtype) {
        case PROGTYPE_ABSOLUTE:
            asm_error("LIBR not allowed with ABS");
            break;
        case PROGTYPE_RELOCATABLE:
        case PROGTYPE_LIBF:
        case PROGTYPE_CALL:
            progtype = PROGTYPE_LIBF;
            break;
        case PROGTYPE_ISSLIBF:
        case PROGTYPE_ISSCALL:
            progtype = PROGTYPE_ISSLIBF;
            break;
        case PROGTYPE_ILS:
            asm_error("Can't use LIBR in an ILS");
            break;
        default:
            bail("in x_libr, can't happen");
    }
}

/********************************************************************************************
 * x_ils - ILS directive
 ********************************************************************************************/

void x_ils  (struct tag_op *op, char *label, char *mods, char *arg)
{
    switch (progtype) {
        case PROGTYPE_ABSOLUTE:
            asm_error("ILS not allowed with ABS");
            break;
        case PROGTYPE_RELOCATABLE:
        case PROGTYPE_ILS:
            progtype = PROGTYPE_ILS;
            break;
        case PROGTYPE_LIBF:
        case PROGTYPE_CALL:
            asm_error("Invalid placement of ILS");
            break;
        case PROGTYPE_ISSLIBF:
        case PROGTYPE_ISSCALL:
            break;
        default:
            bail("in x_libr, can't happen");
    }

    intlevel_primary = atoi(mods);
}

/********************************************************************************************
 * x_iss - ISS directive
 ********************************************************************************************/

void x_iss  (struct tag_op *op, char *label, char *mods, char *arg)
{
    char *tok;

    switch (progtype) {
        case PROGTYPE_ABSOLUTE:
            asm_error("ISS not allowed with ABS");
            break;
        case PROGTYPE_RELOCATABLE:
        case PROGTYPE_CALL:
        case PROGTYPE_ISSCALL:
            progtype = PROGTYPE_ISSCALL;
            break;
        case PROGTYPE_LIBF:
        case PROGTYPE_ISSLIBF:
            progtype = PROGTYPE_ISSLIBF;
            break;
        case PROGTYPE_ILS:
            asm_error("Can't mix ISS and ILS");
        default:
            bail("in x_libr, can't happen");
    }

    iss_number = atoi(mods);                    /* get ISS number */

    opfield[16] = '\0';                         /* be sure not to look too far into this */

    nintlevels = 0;                             /* # of interrupt levels for ISS */
    intlevel_primary = 0;                       /* primary level for ISS and level for ILS */
    intlevel_secondary = 0;                     /* secondary level for ISS */

    if ((tok = strtok(opfield, " ")) == NULL)
        asm_error("ISS missing entry label");
    else
        x_ent(NULL, label, "", arg);            /* process as an ENT */

    if ((tok = strtok(NULL, " ")) != NULL) {    /* get associated levels */
        nintlevels++;
        intlevel_primary = atoi(tok);
    }

    if ((tok = strtok(NULL, " ")) != NULL) {
        nintlevels++;
        intlevel_secondary = atoi(tok);
    }
}

/********************************************************************************************
 * x_spr - 
 ********************************************************************************************/

void x_spr  (struct tag_op *op, char *label, char *mods, char *arg)
{
    realmode = REALMODE_STANDARD;
}

/********************************************************************************************
 * x_epr -
 ********************************************************************************************/

void x_epr  (struct tag_op *op, char *label, char *mods, char *arg)
{
    realmode = REALMODE_EXTENDED;
}

/********************************************************************************************
 * x_dsa -
 ********************************************************************************************/

void x_dsa  (struct tag_op *op, char *label, char *mods, char *arg)
{
    unsigned short words[2];

    setw(0, org+listoffset, FALSE);         /* display origin */

    if (*label)                             /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (! *arg) {
        asm_error("DSA missing filename");
    }
    else if (outmode == OUTMODE_BINARY) {
        namecode(words, arg);                   
        writetwo();
        writew(words[0], CALL);             /* special relocation bits here 3 and 1 */
        writew(words[1], RELATIVE);
    }
    else {
        writew(0, 0);                       /* write two zeroes */
        writew(0, 0);
        asm_warning("DSA is not valid for simulator load output format, emitting 0's");
    }
}

/********************************************************************************************
 * x_link - 
 ********************************************************************************************/

void x_link (struct tag_op *op, char *label, char *mods, char *arg)
{
    unsigned short words[2];
    char nline[128];

    setw(0, org+listoffset, FALSE);         /* display origin */

    if (*label)                             /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (! *arg) {
        asm_error("LINK missing program name");
    }
    else {
        format_line(nline, label, "CALL", "", "$LINK", "");
        parse_line(nline);

        if (outmode == OUTMODE_BINARY) {
            namecode(words, arg);                   
            writew(words[0], ABSOLUTE);     /* special relocation bits here 3 and 1 */
            writew(words[1], ABSOLUTE);
        }
        else {
            writew(0x3000, 0);              /* write two WAIT commands */
            writew(0x3000, 0);
            asm_warning("LINK is not valid for simulator load output format, emitting WAIT");
        }
    }
}

/********************************************************************************************
 * x_libf -
 ********************************************************************************************/

void x_libf (struct tag_op *op, char *label, char *mods, char *arg)
{
    unsigned short words[2];

    if (*label)                             /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (! *arg) {
        asm_error("LIBF missing argument");
        return;
    }

    if (pass == 1) {                        /* it will take one word in any case */
        org++;
        return;
    }

    setw(0, org+listoffset, FALSE);         /* display origin */

    if (outmode == OUTMODE_BINARY) {
        namecode(words, arg);               /* emit namecode for loader */

        writetwo();
        writew(words[0], LIBF);             /* this one does NOT advance org! */
        writew(words[1], ABSOLUTE);
    }
    else {
        writew(0x3000, 0);                  /* write a WAIT command */
        asm_warning("LIBF is not valid for simulator load output format, emitting WAIT");
    }
}

/********************************************************************************************
 * x_file -
 ********************************************************************************************/

void x_file (struct tag_op *op, char *label, char *mods, char *arg)
{
    int i, n, r;
    EXPR vals[5];
    char *tok;

    for (i = 0; i < 5; i++) {
        if ((tok = strtok(arg, ",")) == NULL) {
            asm_error("FILE has insufficient arguments");
            return;
        }
        arg = NULL;         /* for next strtok call */

        if (i == 3) {
            if (strcmpi(tok, "U") != 0)
                asm_error("Argument 4 must be the letter U");
        }
        else if (getexpr(tok, FALSE, &vals[i]) == S_DEFINED) {
            if (i <= 3 && vals[i].relative)
                asm_error("Argument %d must be absolute", i+1);
            else if (pass == 2 && vals[i].value == 0)
                asm_error("Argument %d must be nonzero", i+1);
        }
    }

    writew(vals[0].value, ABSOLUTE);
    writew(vals[1].value, ABSOLUTE);
    writew(vals[2].value, ABSOLUTE);
    writew(vals[4].value, vals[i].relative);
    writew(0, ABSOLUTE);
    n = MAX(1, vals[2].value);
    r = 320/n;
    writew(r, ABSOLUTE);
    r = MAX(1, r);
    writew((16*vals[1].value)/r, ABSOLUTE);

    if (pass == 2)
        ndefined_files++;
}

/********************************************************************************************
 * x_trap - place to set a breakpoint
 ********************************************************************************************/

void x_trap (struct tag_op *op, char *label, char *mods, char *arg)
{
    /* debugging breakpoint */
}

/********************************************************************************************
 * x_ces - .CES directive (nonstandard). Specify a value for the console entry
 * switches. When this program is loaded into the simulator, the switches will
 * be set accordingly. Handy for bootstraps and other programs that read
 * the switches.
 ********************************************************************************************/

void x_ces (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;

    if (outmode != OUTMODE_LOAD)                /* this works only in our loader format */
        return;

    if (getexpr(arg, FALSE, &expr) != S_DEFINED)
        return;

    if (pass == 2)
        fprintf(fout, "S%04X" ENDLINE, expr.value & 0xFFFF);
}

/********************************************************************************************
 * x_bss - BSS directive - reserve space in core
 ********************************************************************************************/

void x_bss (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;

    org_advanced = 0;                   /* * means this address */

    if (! *arg) {
        expr.value = 0;
        expr.relative = ABSOLUTE;
    }
    else if (getexpr(arg, FALSE, &expr) != S_DEFINED)
        return;

    if (strchr(mods, 'E') != NULL)      /* force even address */
        org_even();

    if (expr.relative)
        asm_error("BSS size must be an absolute value");

    setw(0, org+listoffset, FALSE);     /* display origin */

    if (*label)                         /* define label */
        set_symbol(label, org, TRUE, relocate);

    expr.value &= 0xFFFF;               /* truncate to 16 bits */

    if (expr.value & 0x8000) {
        asm_warning("Negative BSS size (%ld, /%04X)", (long)(short)expr.value, expr.value);
    }
    else if (expr.value > 0) {
        if (outmode == OUTMODE_LOAD) {
            org += expr.value;          /* advance the origin by appropriate number of words */
            if (pass == 2)              /* emit new load address in output file */
                fprintf(fout, "@%04X%s" ENDLINE, org & 0xFFFF, relocate ? "R" : "");
        }
        else {
            org += expr.value;          /* advance the origin by appropriate number of words */
            if (pass == 2)
                bincard_setorg(org);
        }
    }
}

/********************************************************************************************
 * x_bes - Block Ended by Symbol directive. Like BSS but label gets address AFTER the space, instead of first address
 ********************************************************************************************/

void x_bes (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;

    org_advanced = 0;                   /* * means this address */

    if (! *arg) {                       /* arg field = space */
        expr.value = 0;
        expr.relative = ABSOLUTE;
    }
    else if (getexpr(arg, FALSE, &expr) != S_DEFINED)
        return;

    if (strchr(mods, 'E') != NULL && (org & 1) != 0)
        org_even();                     /* force even address */

    if (expr.relative)
        asm_error("BES size must be an absolute value");

    if (expr.value < 0)
        asm_warning("Negative BES size");

    else if (expr.value > 0) {
        setw(0, org+expr.value+listoffset, FALSE);  /* display NEW origin */

        if (outmode == OUTMODE_LOAD) {
            org += expr.value;          /* advance the origin */
            if (pass == 2)              /* emit new load address in output file */
                fprintf(fout, "@%04X%s" ENDLINE, org & 0xFFFF, relocate ? "R" : "");
        }
        else {
            org += expr.value;          /* advance the origin */
            bincard_setorg(org);
        }
    }

    if (*label)                         /* NOW define the label */
        set_symbol(label, org, TRUE, relocate);
}

/********************************************************************************************
 * x_dmes - DMES define message directive.  Various encodings, none pretty.
 ********************************************************************************************/

int dmes_wd;
int dmes_nc;
enum {CODESET_CONSOLE, CODESET_1403, CODESET_1132, CODESET_EBCDIC} dmes_cs;
void stuff_dmes (int ch, int rpt);

void x_dmes (struct tag_op *op, char *label, char *mods, char *arg)
{
    int rpt;
    char *c = opfield;
    BOOL cont = FALSE;

    if (dmes_saved) {                   /* previous DMES had an odd character saved */
        dmes_wd = dmes_savew;
        dmes_nc = 1;                    /* stick it into the outbut buffer */
    }
    else
        dmes_nc = dmes_wd = 0;          /* clear output buffer */

    trim(opfield);                      /* remove trailing blanks from rest of input line (use whole thing) */
    setw(0, org+listoffset, FALSE);     /* display origin */

    if (*label)                         /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (strchr(mods, '1') != NULL)      /* determine the encoding scheme */
        dmes_cs = CODESET_1403;
    else if (strchr(mods, '2') != NULL)
        dmes_cs = CODESET_1132;
    else  if (strchr(mods, '0') != NULL || ! *mods)
        dmes_cs = CODESET_CONSOLE;
    else {
        asm_error("Invalid printer code in tag field");
        dmes_cs = CODESET_EBCDIC;
    }

    while (*c) {                        /* pick up characters */
        if (*c == '\'') {               /* quote (') is the escape character */
            c++;

            rpt = 0;                    /* get repeat count */
            while (BETWEEN(*c, '0', '9')) {
                rpt = rpt*10 + *c++ - '0';
            }
            if (rpt <= 0)               /* no count = insert one copy */
                rpt = 1;

            switch (*c) {               /* handle escape codes */
                case '\'':
                    stuff_dmes(*c, 1);
                    break;

                case 'E':
                    *c = '\0';                  /* end */
                    break;

                case 'X':
                case 'S':
                    stuff_dmes(' ', rpt);
                    break;

                case 'F':
                    stuff_dmes(*++c, rpt);      /* repeat character */
                    break;

                case ' ':
                case '\0':
                    cont = TRUE;
                    *c = '\0';                  /* end */
                    break;

                case 'T':
                    if (dmes_cs != CODESET_CONSOLE) {
badcode:                asm_error("Invalid ' escape for selected printer");
                        break;
                    }
                    stuff_dmes(0x41, -rpt);     /* tab */
                    break;

                case 'D':
                    if (dmes_cs != CODESET_CONSOLE) goto badcode;
                    stuff_dmes(0x11, -rpt);     /* backspace */
                    break;

                case 'B':
                    if (dmes_cs != CODESET_CONSOLE) goto badcode;
                    stuff_dmes(0x05, -rpt);     /* black */
                    break;

                case 'A':
                    if (dmes_cs != CODESET_CONSOLE) goto badcode;
                    stuff_dmes(0x09, -rpt);     /* red */
                    break;

                case 'R':
                    if (dmes_cs != CODESET_CONSOLE) goto badcode;
                    stuff_dmes(0x81, -rpt);     /* return */
                    break;

                case 'L':
                    if (dmes_cs != CODESET_CONSOLE) goto badcode;
                    stuff_dmes(0x03, -rpt);     /* line feed */
                    break;
                
                default:
                    asm_error("Invalid ' escape in DMES");
                    *c = '\0';
                    break;
            }
        }
        else                                    /* just copy literal character */
            stuff_dmes(*c, 1);

        if (*c)
            c++;
    }

    dmes_saved = FALSE;

    if (dmes_nc) {                              /* odd number of characters */
        if (cont) {
            dmes_saved = TRUE;
            dmes_savew = dmes_wd;               /* save for next time */
        }
        else
            stuff_dmes(' ', 1);                 /* pad with a space to force out even # of characters */
    }
}

/********************************************************************************************
 * stuff_dmes - insert 'rpt' copies of character 'ch' into output words
 ********************************************************************************************/

void stuff_dmes (int ch, int rpt)
{
    int nch, i;                     /* nch is translated output value */

    if (rpt < 0) {                  /* negative repeat means no translation needed */
        rpt = -rpt;
        nch = ch;
    }
    else {
        switch (dmes_cs) {
            case CODESET_CONSOLE:
                nch = 0x21;
                for (i = 0; i < 256; i++) {
                    if (conout_to_ascii[i] == ch) {
                        nch = i;
                        break;
                    }
                }
                break;

            case CODESET_EBCDIC:
                nch = ascii_to_ebcdic_table[ch & 0x7F];
                if (nch == 0)
                    nch = 0x7F;
                break;

            case CODESET_1403:
                nch = ascii_to_1403_table[ch & 0x7F];
                if (nch == 0)
                    nch = 0x7F;
                break;

            case CODESET_1132:
                nch = 0x40;
                for (i = 0; i < WHEELCHARS_1132; i++) {
                    if (codewheel1132[i].ascii == ch) {
                        nch = codewheel1132[i].ebcdic;
                        break;
                    }
                }
                break;

            default:
                bail("bad cs in x_dmes, can't happen");
                break;
        }
    }
            
    while (--rpt >= 0) {                /* pack them into words, output when we have two */
        if (dmes_nc == 0) {
            dmes_wd = (nch & 0xFF) << 8;
            dmes_nc = 1;
        }
        else {
            dmes_wd |= (nch & 0xFF);
            writew(dmes_wd, FALSE);
            dmes_nc = 0;
        }
    }
}

/********************************************************************************************
 * x_ebc - handle EBCDIC string definition (delimited with periods)
 ********************************************************************************************/

void x_ebc (struct tag_op *op, char *label, char *mods, char *arg)
{
    char *p;

/*  setw(0, org+listoffset, FALSE); */
    if (*label)
        set_symbol(label, org, TRUE, relocate);

    p = trim(opfield);                      /* remove trailing blanks from rest of input line (use whole thing) */

    if (*p != '.') {
        asm_error("EBC data must start with .");
        return;
    }
    p++;                                    /* skip leading period */

    dmes_nc = dmes_wd = 0;                  /* clear output buffer (we're borrowing the DMES packer) */
    dmes_cs = CODESET_EBCDIC;

    while (*p && *p != '.')                 /* store packed ebcdic */
        stuff_dmes(*p++, 1);

    if (dmes_nc)                            /* odd number of characters */
        stuff_dmes(' ', 1);                 /* pad with a space to force out even # of characters */

    if (*p != '.')
        asm_error("EBC missing closing .");
}

/********************************************************************************************
 * x_dn - define name DN directive. Pack 5 characters into two words. This by the
 * way is the reason the language Forth is not Fourth.
 ********************************************************************************************/

void x_dn (struct tag_op *op, char *label, char *mods, char *arg)
{
    unsigned short words[2];

    setw(0, org+listoffset, FALSE);         /* display origin */

    if (*label)                             /* define label */
        set_symbol(label, org, TRUE, relocate);

    namecode(words, arg);                   

    writew(words[0], ABSOLUTE);
    writew(words[1], ABSOLUTE);
}

/********************************************************************************************
 * x_dump - DUMP directive - pretend we saw "call $dump, call $exit"
 ********************************************************************************************/

void x_dump (struct tag_op *op, char *label, char *mods, char *arg)
{
    x_pdmp(op, label, mods, arg);
    x_exit(NULL, "", "", "");           /* compile "call $exit" */
}

/********************************************************************************************
 * x_pdmp - PDMP directive - like DUMP but without the call $exit
 ********************************************************************************************/

void x_pdmp (struct tag_op *op, char *label, char *mods, char *arg)
{
    char nline[200], *tok;
    EXPR addr[3];
    int i;

    for (i = 0, tok = strtok(arg, ","); i < 3 && tok != NULL; i++, tok = strtok(NULL, ",")) {
        if (getexpr(tok, FALSE, addr+i) != S_DEFINED) {
            addr[i].value = (i == 1) ? 0x3FFF : 0;
            addr[i].relative = ABSOLUTE;
        }
    }

    org_advanced = 0;                   /* * means this address+1 */

    format_line(nline, label, "BSI", "L", DOLLARDUMP, "");
    parse_line(nline);                  /* compile "call $dump" */

    writew(addr[2].value, ABSOLUTE);    /* append arguments (0, start, end address) */
    writew(addr[0].value, addr[0].relative);
    writew(addr[1].value, addr[1].relative);
}

/********************************************************************************************
 * x_hdng - HDNG directive
 ********************************************************************************************/

void x_hdng (struct tag_op *op, char *label, char *mods, char *arg)
{
    char *c;

    /* label is not entered into the symbol table */

    if (flist == NULL || ! list_on) {
        line_error = TRUE;                  /* inhibit listing: don't print the HDNG statement */
        return;
    }

    line_error = TRUE;                      /* don't print the statement */

    c = skipbl(opfield);
    trim(c);
    fprintf(flist, "\f%s\n\n", c);          /* print page header */
}

/********************************************************************************************
 * x_list - LIST directive. enable or disable listing
 ********************************************************************************************/

void x_list (struct tag_op *op, char *label, char *mods, char *arg)
{
    BOOL on;

    /* label is not entered into the symbol table */

    line_error = TRUE;          /* don't print the LIST statement */

    if (flist == NULL || ! list_on) {
        return;
    }

    if (strcmpi(arg, "ON") == 0)
        on = TRUE;
    else if (strcmpi(arg, "OFF") == 0)
        on = FALSE;
    else
        on = do_list;

    list_on = on;
}

/********************************************************************************************
 * x_spac - SPAC directive. Put blank lines in listing
 ********************************************************************************************/

void x_spac (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;

    /* label is not entered into the symbol table */

    if (flist == NULL || ! list_on) {
        line_error = TRUE;          /* don't print the SPAC statement */
        return;
    }

    if (getexpr(arg, FALSE, &expr) != S_DEFINED)
        return;

    line_error = TRUE;          /* don't print the statement */

    while (--expr.value >= 0) 
        putc('\n', flist);
}

/********************************************************************************************
 * x_ejct - EJCT directive - put formfeed in listing
 ********************************************************************************************/

void x_ejct (struct tag_op *op, char *label, char *mods, char *arg)
{
    /* label is not entered into the symbol table */

    if (flist == NULL || ! list_on) {
        line_error = TRUE;          /* don't print the EJCT statement */
        return;
    }

    line_error = TRUE;          /* don't print the statement */

    putc('\f', flist);
}

/********************************************************************************************
 * basic_opcode - construct a standard opcode value from op table entry and modifier chars
 ********************************************************************************************/

int basic_opcode (struct tag_op *op, char *mods)
{
    int opcode = op->opcode;                        /* basic code value */

    if (strchr(mods, '1') != 0)                     /* indexing */
        opcode |= 0x0100;
    else if (strchr(mods, '2') != 0)
        opcode |= 0x0200;
    else if (strchr(mods, '3') != 0)
        opcode |= 0x0300;

    if (strchr(mods, 'L')) {                        /* two-word format */
        opcode |= OP_LONG;
        if (strchr(mods, 'I') != 0)                 /* and indirect to boot */
            opcode |= OP_INDIRECT;
    }

    return opcode;
}

/********************************************************************************************
 * std_op - assemble a vanilla opcode
 ********************************************************************************************/

void std_op (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;
    int opcode = basic_opcode(op, mods);
    BOOL val_ok = FALSE;

    if (*label)                                     /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (*arg && ! (op->flags & NO_ARGS)) {          /* get value argument */
        if (getexpr(arg, FALSE, &expr) == S_DEFINED) 
            val_ok = TRUE;
    }
    else {
        expr.value = 0;
        expr.relative = FALSE;
    }

    if (opcode & OP_LONG) {                         /* two-word format, just write code and value */
        writew(opcode, FALSE);
        writew(expr.value, expr.relative);
    }
    else {                                          /* one-word format */
        if (strchr(mods, 'I') != 0)
            asm_error("Indirect mode not permitted on one-word instructions");

        if (val_ok && ! (strchr(mods, 'X') || (op->flags & IS_ABS) || ((opcode & OP_INDEXED) && ! (op->flags & NO_IDX))))
            expr.value -= (org+1);                  /* compute displacement */

        if (expr.value < -128 || expr.value > 127) {/* check range */
            asm_error("Offset of %d is too large", expr.value);
            expr.value = 0;
        }

        writew(opcode | (expr.value & 0x00FF), FALSE);/* that's the code */
    }
}

/********************************************************************************************
 * mdx_op - assemble a MDX family instruction
 ********************************************************************************************/

void mdx_op (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR dest, incr = {0, FALSE};
    int opcode = basic_opcode(op, mods);
    char *tok;

    if (*label)                                     /* define label */
        set_symbol(label, org, TRUE, relocate);

    if ((tok = strtok(arg, ",")) == NULL) {         /* argument format is dest[,increment] */
/*      asm_error("Destination not specified");     // seems not to be an error, IBM omits it sometimes */
        dest.value = 0;
        dest.relative = ABSOLUTE;
    }
    else
        getexpr(tok, FALSE, &dest);                 /* parse the address */

    tok = strtok(NULL, ",");                        /* look for second argument */

    if (opcode & OP_LONG) {                         /* two word format */
        if (opcode & OP_INDEXED) {                  /* format: MDX 2 dest */
            if (tok != NULL)
                asm_error("This format takes only one argument");
        }
        else {                                      /* format: MDX   dest,increment */
            if (opcode & OP_INDIRECT)
                asm_error("Indirect can't be used without indexing");

            if (tok == NULL) {
/*              asm_error("This format takes two arguments"); */
                incr.value = 0;
                incr.relative = ABSOLUTE;
            }
            else 
                getexpr(tok, FALSE, &incr);

            if (incr.value < -128 || incr.value > 127)          /* displacement style (fixed in ver 1.08) */
                asm_error("Invalid increment value (8 bits signed)");

            opcode |= (incr.value & 0xFF);
        }

        writew(opcode, ABSOLUTE);
        writew(dest.value, dest.relative);
    }
    else {                                          /* one word format MDX  val */
        if (tok != NULL)
            asm_error("This format takes only one argument");

        if (! (strchr(mods, 'X') || (opcode & OP_INDEXED)))
            dest.value -= (org+1);                      /* compute displacement */

        if (dest.value < -128 || dest.value > 127)
            asm_error("Offset/Increment of %d is too large", dest.value);

        writew(opcode | (dest.value & 0xFF), FALSE);
    }
}

/********************************************************************************************
 * bsi_op - BSI long instruction is like a BSC L, short is standard
 ********************************************************************************************/

void bsi_op (struct tag_op *op, char *label, char *mods, char *arg)
{
    if (strchr(mods, 'L') || strchr(mods, 'I'))
        bsc_op(op, label, mods, arg);
    else
        std_op(op, label, mods, arg);
}

/********************************************************************************************
 * b_op - branch; use short or long version
 ********************************************************************************************/

void b_op (struct tag_op *op, char *label, char *mods, char *arg)
{
    static struct tag_op *mdx = NULL;

    if (strchr(mods, 'L') || strchr(mods, 'I')) {
        bsi_op(op, label, mods, arg);   
        return;
    }

    if (mdx == NULL)
        if ((mdx = lookup_op("MDX")) == NULL)
            bail("Can't find MDX op");

    (mdx->handler)(mdx, label, mods, arg);
}

/********************************************************************************************
 * bsc_op - compute a BSC family instruction
 ********************************************************************************************/

void bsc_op (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR dest;
    int opcode = basic_opcode(op, mods);
    char *tok, *tests;

    if (*label)                                     /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (opcode & OP_LONG) {                         /* two word format */
        if ((tok = strtok(arg, ",")) == NULL) {     /* format is BSC dest[,tests] */
            asm_error("Destination not specified");
            dest.value = 0;
            dest.relative = ABSOLUTE;
        }
        else
            getexpr(tok, FALSE, &dest);

        tests = strtok(NULL, ",");                  /* get test characters */
    }
    else
        tests = arg;                                /* short format is BSC tests */

    if (tests != NULL) {                            /* stick in the testing bits */
        for (; *tests; tests++) {
            switch (*tests) {
                             /* bit 0x40 is the BOSC bit */
                case 'Z': opcode |= 0x20; break;
                case '-': opcode |= 0x10; break;
                case '+':
                case '&': opcode |= 0x08; break;
                case 'E': opcode |= 0x04; break;
                case 'C': opcode |= 0x02; break;
                case 'O': opcode |= 0x01; break;
                default:
                    asm_error("Invalid test flag: '%c'", *tests);
            }
        }
    }

    writew(opcode, ABSOLUTE);                           /* emit code */
    if (opcode & OP_LONG)
        writew(dest.value, dest.relative);
}

/********************************************************************************************
 * shf_op - assemble a shift instruction
 ********************************************************************************************/

void shf_op (struct tag_op *op, char *label, char *mods, char *arg)
{
    EXPR expr;
    int opcode = basic_opcode(op, mods);

    if (*label)                                     /* define label */
        set_symbol(label, org, TRUE, relocate);

    if (opcode & OP_INDEXED) {                      /* shift value comes from index register */
        expr.value = 0;
        expr.relative = ABSOLUTE;
    }
    else
        getexpr(arg, FALSE, &expr);

    if (expr.relative) {
        asm_error("Shift value is a relative address");
        expr.relative = ABSOLUTE;
    }

    if (expr.value < 0 || expr.value > 32) {        /* check range */
        asm_error("Shift count of %d is invalid", expr.value);
        expr.value = 0;
    }

    writew(opcode | (expr.value & 0x3F), FALSE);    /* put shift count into displacement field */
}

/********************************************************************************************
 * x_mdm - MDM instruction
 ********************************************************************************************/

void x_mdm (struct tag_op *op, char *label, char *mods, char *arg)
{
    int opcode = basic_opcode(op, mods);

    if (*label)                                     /* define label */
        set_symbol(label, org, TRUE, relocate);
                                                    /* oh dear: bug here */
    asm_error("'%s' is not yet supported", op->mnem);
}

/********************************************************************************************
 * x_exit - EXIT directive. Assembler manual says it treats like CALL $EXIT, but
 * object code reveals the truth: jump to $EXIT, which is a small value, so we can use LDX.
 ********************************************************************************************/

void x_exit (struct tag_op *op, char *label, char *mods, char *arg)
{
    char nline[120];

    format_line(nline, label, "LDX", "X", DOLLAREXIT, "");
    parse_line(nline);
}

/********************************************************************************************
 * x_opt - .OPT directive. Nonstandard. Possible values:
 *
 * .OPT CEXPR - use C precedence in evaluating expressions rather than strict left-right. The real
 *              1130 assembler was left-to-right. Enabling CEXPR enables precedence and makes the
 *              assembler NON STANDARD.
 ********************************************************************************************/

void x_opt (struct tag_op *op, char *label, char *mods, char *arg)
{
    char *tok;

    org_advanced = 0;                   /* * means this address */

    if (*label) {
        asm_error("Label not permitted on .OPT statement");
        return;
    }
                                        /* look for OPT arguments */
    for (tok = strtok(arg, ","); tok != NULL; tok = strtok(NULL, ",")) {
        if (strcmp(tok, "CEXPR") == 0) {
            cexpr = TRUE;               /* use C expression precedence (untested) */
        }
        else
            asm_error("Unknown .OPT: '%s'", tok);
    }
}

/********************************************************************************************
 * askip - skip input lines until a line with the target label appears
 ********************************************************************************************/

void askip (char *target)
{
    char nline[200], cur_label[20], *c;

    while (get_line(nline, sizeof(nline), TRUE)) {  /* read next line (but don't exit a macro) */
        listout(FALSE);                             /* end listing of previous input line */

        prep_line(nline);                           /* preform standard line prep */

        strncpy(cur_label, nline, 6);               /* get first 5 characters */
        cur_label[5] = '\0';

        for (c = cur_label; *c > ' '; c++)          /* truncate at first whitespace */
            ;
        *c = '\0';
                                                    /* stop if there's a match */
        if ((target == NULL) ? (cur_label[0] == '\0') : strcmp(target, cur_label) == 0) {
            parse_line(nline);                      /* process this line */
            return;
        }
    }

    if (target != NULL)
        asm_error("Label %s not found", target);
}

/********************************************************************************************
 * x_aif - process conditional assembly jump
 ********************************************************************************************/

void x_aif (struct tag_op *op, char *label, char *mods, char *arg)
{
    char *target, *tok;
    EXPR expr1, expr2;
    BOOL istrue;
    enum {OP_EQ, OP_LT, OP_GT, OP_NE, OP_LE, OP_GE} cmp_op;

    /* label is not entered into the symbol table */

    arg = skipbl(arg);
    if (*arg != '(') {
        asm_error("AIF operand must start with (");
        return;
    }

    arg++;                                          /* skip the paren */

    /* normally whitespace is never found in the arg string (see tabtok and coltok). */
    /* However, spaces inside parens are permitted.  */

    if ((tok = strtok(arg, whitespace)) == NULL) {
        asm_error("AIF missing first expression");
        return;
    }

    getexpr(tok, FALSE, &expr1);

    if ((tok = strtok(NULL, whitespace)) == NULL) {
        asm_error("AIF missing conditional operator");
        return;
    }

    if      (strcmp(tok, "EQ") == 0)
        cmp_op = OP_EQ;
    else if (strcmp(tok, "LT") == 0)
        cmp_op = OP_LT;
    else if (strcmp(tok, "GT") == 0)
        cmp_op = OP_GT;
    else if (strcmp(tok, "NE") == 0)
        cmp_op = OP_NE;
    else if (strcmp(tok, "LE") == 0)
        cmp_op = OP_LE;
    else if (strcmp(tok, "GE") == 0)
        cmp_op = OP_GE;
    else {
        asm_error("AIF: %s is not a valid conditional operator", tok);
        return;
    }

    if ((tok = strtok(NULL, ")")) == NULL) {
        asm_error("AIF missing second expression");
        return;
    }

    getexpr(tok, FALSE, &expr2);

    switch (cmp_op) {                               /* test the condition */
        case OP_EQ: istrue = expr1.value == expr2.value; break;
        case OP_LT: istrue = expr1.value <  expr2.value; break;
        case OP_GT: istrue = expr1.value >  expr2.value; break;
        case OP_NE: istrue = expr1.value != expr2.value; break;
        case OP_LE: istrue = expr1.value <= expr2.value; break;
        case OP_GE: istrue = expr1.value >= expr2.value; break;
        default: bail("in aif, can't happen");
    }

    /* After the closing paren coltok and tabtok guarantee we will have no whitespace */

    if ((target = strtok(arg, ",")) == NULL)        /* get target label */
        asm_warning("Missing target label");

    if (istrue)
        askip(target);                              /* skip to the target */
}

/********************************************************************************************
 * x_aifb - conditional assembly jump back (macro only)
 ********************************************************************************************/

void x_aifb (struct tag_op *op, char *label, char *mods, char *arg)
{
    asm_error("aifb valid in macros only and not implemented in any case");
}

/********************************************************************************************
 * x_ago 
 ********************************************************************************************/

void x_ago  (struct tag_op *op, char *label, char *mods, char *arg)
{
    char *target;

    /* label is not entered into the symbol table */

    /* handle differently in a macro */

    if ((target = strtok(arg, ",")) == NULL)        /* get target label */
        asm_warning("Missing target label");

    askip(target);                                  /* skip to the target */
}

/********************************************************************************************
 * x_agob - 
 ********************************************************************************************/

void x_agob (struct tag_op *op, char *label, char *mods, char *arg)
{
    asm_error("agob valid in macros only and not implemented in any case");
}

/********************************************************************************************
 * x_anop - 
 ********************************************************************************************/

void x_anop (struct tag_op *op, char *label, char *mods, char *arg)
{
    /* label is not entered into the symbol table */
    /* do nothing else */
}

/********************************************************************************************
 * expression parser, borrowed from older code, no comments, sorry
 ********************************************************************************************/

char *exprptr, *oexprptr;

#define GETNEXT (*exprptr++)
#define UNGET    --exprptr

#define LETTER  0           /* character types */
#define DIGIT   1
#define ETC 2
#define ILL 3
#define SPACE   4
#define MULOP   5
#define ADDOP   6
#define EXPOP   7

int    getnb      (void);
void   c_expr     (EXPR *ap);
void   c_expr_m   (EXPR *ap);
void   c_expr_e   (EXPR *ap);
void   c_expr_u   (EXPR *ap);
void   c_term     (EXPR *ap);
int    c_number   (int c, int r, int nchar);
int    digit      (int c, int r);
int    c_esc      (int c);
void   exprerr    (int n);
void   a1130_expr (EXPR *ap);
void   a1130_term (EXPR *ap);
                    
char    ctype[128] = {          /* character types */
/*^0ABCDEFG */  ILL,    ILL,    ILL,    ILL,    ILL,    ILL,    ILL,    ILL,
/*^HIJKLMNO */  ILL,    SPACE,  SPACE,  ILL,    SPACE,  SPACE,  ILL,    ILL,
/*^PQRSTUVW */  ILL,    ILL,    ILL,    ILL,    ILL,    ILL,    ILL,    ILL,
/*^XYZ      */  ILL,    ILL,    ILL,    ILL,    ILL,    ILL,    ILL,    ILL,
/*  !"#$%&' */  SPACE,  ETC,    ETC,    LETTER, LETTER, MULOP,  MULOP,  LETTER,     /* $ # @ and ' are letters here */
/* ()*+,-./ */  ETC,    ETC,    MULOP,  ADDOP,  ETC,    ADDOP,  ETC,    MULOP,
/* 01234567 */  DIGIT,  DIGIT,  DIGIT,  DIGIT,  DIGIT,  DIGIT,  DIGIT,  DIGIT,
/* 89:;<=>? */  DIGIT,  DIGIT,  ETC,    ETC,    MULOP,  ETC,    MULOP,  ETC,
/* @ABCDEFG */  LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
/* HIJKLMNO */  LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
/* PQRSTUVW */  LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
/* XYZ[\]^_ */  LETTER, LETTER, LETTER, ETC,    ETC,    ETC,    EXPOP,  LETTER,
/* `abcdefg */  ETC,    LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
/* hijklmno */  LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
/* pqrstuvw */  LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER, LETTER,
/* xyz{|}~  */  LETTER, LETTER, LETTER, ETC,    ADDOP,  ETC,    ETC,    ETC
};

char *errstr[] = {
    "Missing exponent",                 /* 0 */
    "Undefined symbol",                 /* 1 */
    "Division by zero",                 /* 2 */
    "Illegal operator",                 /* 3 */
    ") expected",                       /* 4 */
    "Char expected after '",            /* 5 */
    "Char expected after .",            /* 6 */
    "Number expected after =",          /* 7 */
    "Syntax error",                     /* 8 */
    "Number syntax",                    /* 9 */
    "Char expected after \\",           /* 10 */
    "Relocation error"                  /* 11 */
};

int getnb () {
    int c;

    if (cexpr) {            /* in C mode, handle normally */
        while (ctype[(c = GETNEXT)] == SPACE)
            ;
    }                       /* in 1130 mode, a space terminates the expression. Here, eat the rest */
    else if ((c = GETNEXT) == ' ') {
        while ((c = GETNEXT) != '\0')
            ;
    }

    return c;
}

int symbest, exprerrno;
jmp_buf exprjmp;

/********************************************************************************************
 * getexpr
 ********************************************************************************************/

int getexpr (char *pc, BOOL undefined_ok, EXPR *pval)
{
    symbest = S_DEFINED;            /* assume no questionable symbols */

    pval->value = 0;
    pval->relative = ABSOLUTE;

    if (! *pc)                      /* blank expression is same as zero, ok? */
        return S_DEFINED;

    if (setjmp(exprjmp) != 0) {     /* encountered a syntax error & bailed */
        pval->value = 0;
        pval->relative = ABSOLUTE;
        return S_UNDEFINED;
    }

    exprptr = oexprptr = pc;        /* make global the buffer pointer */

    c_expr(pval);

    if (GETNEXT)                    /* expression should have been entirely eaten */
        exprerr(8);                 /* if characters are left, it's an error */

    if (pval->relative < 0 || pval->relative > 1)
        exprerr(11);                /* has to work out to an absolute or a single relative term */

    if (symbest == S_DEFINED)       /* tell how it came out */
        return S_DEFINED;

    pval->value = 0;
    pval->relative = ABSOLUTE;
    return (pass == 1 && undefined_ok) ? S_PROVISIONAL : S_UNDEFINED;
}

/********************************************************************************************
 * output_literals - construct .DC assembler lines to assemble pending literal
 * constant values that have accumulated.
 ********************************************************************************************/

void output_literals (BOOL eof)
{
    char line[120], label[12], num[20];
    int i;

    for (i = 0; i < n_literals; i++) {          /* generate DC statements for any pending literal constants */
        if (literal[i].even && literal[i].hex)              /* create the value string */
            sprintf(num, "/%08lX", literal[i].value);
        else if (literal[i].even)
            sprintf(num, "%ld",    literal[i].value);
        else if (literal[i].hex)
            sprintf(num, "/%04X",  literal[i].value & 0xFFFF);
        else
            sprintf(num, "%d",     literal[i].value);

        sprintf(label, "_L%03d", literal[i].tagno);
        format_line(line, label, literal[i].even ? "DEC" : "DC", "", num, "GENERATED LITERAL CONSTANT");

        if (eof) {
            eof = FALSE;                            /* at end of file, for first literal, only prepare blank line */
            sprintf(listline, LEFT_MARGIN, org);
        }
        else
            listout(TRUE);                          /* push out any pending line(s) */

        if (flist && list_on)                       /* this makes stuff appear in the listing */
            sprintf(listline, LEFT_MARGIN " %s", detab(line));

        nwout = 0;

        parse_line(line);                           /* assemble the constant definition */
    }

    n_literals = 0;                                 /* clear list */
}

/********************************************************************************************
 * a1130_term - extract one term of an expression using 1130 assembler syntax (no precedence!)
 ********************************************************************************************/

void a1130_term (EXPR *ap)
{
    PSYMBOL s;
    char token[80], *t;
    int c;

    if (cexpr) {                        /* use C syntax and operator precedence */
        c_term(ap);
        return;
    }

    c = GETNEXT;

    if (ctype[c] == DIGIT) {            /* number */
        ap->value = signextend(c_number(c,10,-1));
        ap->relative = ABSOLUTE;
    }
    else if (c == '+') {                /* unary + */
        a1130_term(ap);
    }
    else if (c == '-') {                /* unary - */
        a1130_term(ap);
        ap->value = - ap->value;
    }
    else if (c == '/') {                /* / starts a hex constant */
        ap->value = signextend(c_number(c,16,-1));
        ap->relative = ABSOLUTE;
    }
    else if (c == '*') {                /* asterisk alone = org */
        ap->value = org + org_advanced; /* here is where that offset matters! */
        ap->relative = relocate;
    }
    else if (c == '.') {                /* EBCDIC constant */
        c = GETNEXT;
        if (c == '\0') {
            UNGET;
            c = ' ';
        }
        c = ascii_to_ebcdic_table[c];
        ap->value = c;                  /* VALUE IS IN LOW BYTE!!! */
        ap->relative = ABSOLUTE;
    }
    else if (ctype[c] == LETTER) {      /* symbol */
        t = token;
        do {
            *t++ = c;
            c = GETNEXT;
        } while (ctype[c] == LETTER || ctype[c] == DIGIT);
        UNGET;
        *t++ = '\0';

        s = lookup_symbol(token, TRUE);
        add_xref(s, FALSE);
        ap->value    = s->value;
        ap->relative = s->relative;

        symbest = MIN(symbest, s->defined);     /* this goes to lowest value (undefined < provisional < defined) */
        if (pass == 2 && s->defined != S_DEFINED)
            exprerr(1);
    }
    else
        exprerr(8);
}

/********************************************************************************************
 * signextend - sign-extend a 16-bit constant value to whatever "int" is.
 ********************************************************************************************/

int signextend (int v)
{
    v &= 0xFFFF;                /* clip to 16 bits (this may not be necessary, but best to be safe?) */

    if (v & 0x8000)             /* if sign bit is set */
        v |= ~0xFFFF;           /* sign extend */

    return v;
}

/********************************************************************************************
 * c_expr - evalate an expression using C syntax
 ********************************************************************************************/

void c_expr (EXPR *ap)
{
    int c;
    EXPR rop;

    c_expr_m(ap);                                   /* get combined multiplicative terms */
    for (;;) {                                      /* handle +/- precedence operators */
        if (ctype[c=getnb()] != ADDOP) {
            UNGET;
            break;
        }
        c_expr_m(&rop);                             /* right hand operand */
        switch (c) {
            case '+':
                ap->value += rop.value;
                ap->relative += rop.relative;
                break;

            case '-':
                ap->value -= rop.value;
                ap->relative -= rop.relative;
                break;

            case '|':
                if (ap->relative || rop.relative)
                    exprerr(11);
                ap->value = ((long) (ap->value)) | ((long) rop.value);
                break;

            default:
                printf("In expr, can't happen\n");
        }
    }
}

/********************************************************************************************
 * c_expr_m - get multiplicative precedence terms. Again, this is not usually used
 ********************************************************************************************/

void c_expr_m (EXPR *ap)
{
    int c;
    EXPR rop;

    c_expr_e(ap);                       /* get exponential precedence term */
    for (;;) {                          /* get operator */
        c = getnb();
        if ((c=='<') || (c=='>'))
            if (c != getnb())           /* << or >> */
                exprerr(3);
        if (ctype[c] != MULOP) {
            UNGET;
            break;
        }
        c_expr_e(&rop);                 /* right hand operand */

        switch(c) {
            case '*':
                if (ap->relative && rop.relative)
                    exprerr(11);

                ap->value *= rop.value;
                ap->relative = (ap->relative || rop.relative) ? RELATIVE : ABSOLUTE;
                break;

            case '/':
                if (rop.value == 0)
                    exprerr(2);
                if (ap->relative || rop.relative)
                    exprerr(11);

                ap->value /= rop.value;
                break;

            case '%':
                if (rop.value == 0)
                    exprerr(2);
                if (ap->relative || rop.relative)
                    exprerr(11);

                ap->value = ((long) (ap->value)) % ((long) rop.value);
                break;

            case '&':
                if (ap->relative || rop.relative)
                    exprerr(11);

                ap->value = ((long) (ap->value)) & ((long) rop.value);
                break;

            case '>':
                if (ap->relative || rop.relative)
                    exprerr(11);

                ap->value = ((long) (ap->value)) >> ((long) rop.value);
                break;

            case '<':
                if (ap->relative || rop.relative)
                    exprerr(11);

                ap->value = ((long) (ap->value)) << ((long) rop.value);
                break;

            default:
                printf("In expr_m, can't happen\n");
        }
    }
}

/********************************************************************************************
 * c_expr_e - get exponential precedence terms. Again, this is not usually used
 ********************************************************************************************/

void c_expr_e (EXPR *ap)
{
    int c, i, v;
    EXPR rop;

    c_expr_u(ap);
    for (;;) {
        c = getnb();
        if (ctype[c] != EXPOP) {
            UNGET;
            break;
        }
        c_expr_u(&rop);

        switch(c) {
            case '^':
                if (ap->relative || rop.relative)
                    exprerr(11);

                v = ap->value;
                ap->value = 1;
                for (i = 0; i < rop.value; i++)
                    ap->value *= v;
                break;

            default:
                printf("In expr_e, can't happen\n");
        }
    }
}

/********************************************************************************************
 * c_expr_u - get unary precedence terms. Again, this is not usually used
 ********************************************************************************************/

void c_expr_u (EXPR *ap)
{
    int c;

    if ((c = getnb()) == '!') {
        a1130_term(ap);
        ap->value = ~ ((long)(ap->value));
        if (ap->relative)
            exprerr(11);
    }
    else if (c == '-') {
        a1130_term(ap);
        ap->value = - ap->value;
        if (ap->relative)
            exprerr(11);
    }
    else {
        UNGET;
        a1130_term(ap);
    }
}

/********************************************************************************************
 * c_term - get basic operand or parenthesized expression.  Again, this is not usually used
 ********************************************************************************************/

void c_term (EXPR *ap)
{
    int c, cc;
    PSYMBOL s;
    char token[80], *t;

    ap->relative = ABSOLUTE;            /* assume absolute */

    if ((c = getnb()) == '(') {         /* parenthesized expr */
        c_expr(ap);                     /* start over at the top! */
        if ((cc = getnb()) != ')')
            exprerr(4);
    }
    else if (c == '\'') {               /* single quote: char */
        if ((c = GETNEXT) == '\0')
            c = ' ';
        ap->value = c_esc(c);
    }
    else if (ctype[c] == DIGIT) {       /* number */
        ap->value = signextend(c_number(c,10,-1));
    }
    else if (c == '0') {                /* 0 starts a hex or octal constant */
        if ((c = GETNEXT) == 'x') {
            c = GETNEXT;
            ap->value = signextend(c_number(c,16,-1));
        }
        else {
            ap->value = signextend(c_number(c,8,-1));
        }
    }
    else if (c == '*') {                /* asterisk alone = org */
        ap->value = org + org_advanced;
        ap->relative = relocate;
    }
    else if (ctype[c] == LETTER) {      /* symbol */
        t = token;
        do {
            *t++ = c;
            c = GETNEXT;
        } while (ctype[c] == LETTER || ctype[c] == DIGIT);
        UNGET;
        *t++ = '\0';

        s = lookup_symbol(token, TRUE);
        ap->value = s->value;
        ap->relative = s->relative;
        add_xref(s, FALSE);
        symbest = MIN(symbest, s->defined);     /* this goes to lowest value (undefined < provisional < defined) */

        if (pass == 2 && s->defined != S_DEFINED)
            exprerr(1);
    }
    else
        exprerr(8);
}

/********************************************************************************************
 * c_number - get a C format constant value.  Again, this is not usually used
 ********************************************************************************************/

int c_number (int c, int r, int nchar)
{
    int v, n;

    nchar--;

    if (c == '/' && ! cexpr) {                      /* special radix stuff */
        r = 16;
        c = GETNEXT;
    }
    else if (r == 10 && c == '0' && cexpr) {        /* accept C style 0x## also */
        c = GETNEXT;
        if (c == 'x') {
            r = 16;
            c = GETNEXT;
        }
        else {
            r = 8;
            UNGET;
            c = '0';
        }
    }

    n = 0;              /* decode number */
    while ((nchar-- != 0) && (v = digit(c, r)) >= 0) {
        if (v >= r)     /* out of range! */
            exprerr(9);

        n = r*n + v;

        c = GETNEXT;
        if (c == '.') {     /* maybe make it decimal? */
            c = GETNEXT;
            break;
        }
    }

    UNGET;
    return (n);
}

/********************************************************************************************
 * digit - get digit value of character c in radix r
 ********************************************************************************************/

int digit (int c, int r)
{
    if (r == 16) {
        if (c >= 'A' && c <= 'F')
            return (c - 'A' + 10);
    }

    if (c >= '0' && c <= '9')
        return (c - '0');

    return (-1);
}

/********************************************************************************************
 * c_esc - handle C character escape
 ********************************************************************************************/

int c_esc (int c)
{
    if (c != '\\')          /* not escaped */
        return(c);

    if ((c = GETNEXT) == '\0')  /* must be followed by something */
        exprerr(10);
    if ((c >= 'A') && (c <= 'Z'))   /* handle upper case */
        c += 'a'-'A';
    if (ctype[c] == LETTER)     /* control character abbrevs */
        switch (c) {
            case 'b': c = '\b'; break;  /* backspace */
            case 'e': c = 27  ; break;  /* escape */
            case 'f': c = '\f'; break;  /* formfeed */
            case 'n': c = '\n'; break;  /* newline */
            case 'r': c = '\r'; break;  /* return */
            case 't': c = '\t'; break;  /* horiz. tab */
        }
    else if (ctype[c] == DIGIT) {   /* get character by the numbers */
        c = c_number(c,8,3);    /* force octal */
    }

    return c;
}

/********************************************************************************************
 * exprerr - note an expression syntax error. Longjumps back to caller with failure code
 ********************************************************************************************/

void exprerr (int n)
{
    char msg[256];
    int nex = exprptr-oexprptr;

    strncpy(msg, oexprptr, nex);        /* show where the problem was */
    msg[nex] = '\0';
    strcat(msg, " << ");
    strcat(msg, errstr[n]);

    asm_error(msg);

    exprerrno = n;
    longjmp(exprjmp, 1);
}

/********************************************************************************************
 * upcase - force a string to uppercase (ASCII)
 ********************************************************************************************/

char *upcase (char *str)
{
    char *s;

    for (s = str; *s; s++) {
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
    } 

    return str;
}

/********************************************************************************************
 * hollerith table for IPL card ident field
 ********************************************************************************************/

typedef struct {
    int     hollerith;
    char    ascii;
} CPCODE;

static CPCODE cardcode_029[] =
{
    0x0000,     ' ',
    0x8000,     '&',            /* + in 026 Fortran */
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
    0x0420,     '#',        /* = in 026 Fortran */
    0x0220,     '@',        /* ' in 026 Fortran */
    0x0120,     '\'',
    0x00A0,     '=',
    0x0060,     '"',
    0x8820,     'c',        /* cent */
    0x8420,     '.',
    0x8220,     '<',        /* ) in 026 Fortran */
    0x8120,     '(',
    0x80A0,     '+',
    0x8060,     '|',
    0x4820,     '!',
    0x4420,     '$',
    0x4220,     '*',
    0x4120,     ')',
    0x40A0,     ';',
    0x4060,     'n',        /* not */
    0x2820,     'x',        /* what? */
    0x2420,     ',',
    0x2220,     '%',        /* ( in 026 Fortran */
    0x2120,     '_',
    0x20A0,     '>',
    0x2060,     '>',
};

/********************************************************************************************
 * ascii_to_hollerith - 
 ********************************************************************************************/

int ascii_to_hollerith (int ch)
{
    int i;

    for (i = 0; i < sizeof(cardcode_029) / sizeof(CPCODE); i++)
        if (cardcode_029[i].ascii == ch)
            return cardcode_029[i].hollerith;

    return 0;
}

/********************************************************************************************
 * detab - replace tabs with spaces for listing files
 ********************************************************************************************/

char *detab (char *instr)
{
    static char outstr[256];
    char *out = outstr;
    int col = 0;

    while (*instr) {
        if (*instr == '\t') {
            do {
                *out++ = ' ';
                col++;
            }
            while (col & 7);
        }
        else {
            *out++ = *instr;
            col++;
        }

        instr++;
    }
    
    *out = '\0';

    return outstr;
}

#ifndef _WIN32

/********************************************************************************************
 * routines provided by Microsoft C but not others
 ********************************************************************************************/

int strnicmp (char *a, char *b, int n)
{
    int ca, cb;

    for (;;) {
        if (--n < 0)                    /* still equal after n characters? quit now */
            return 0;

        if ((ca = *a) == 0)             /* get character, stop on null terminator */
            return *b ? -1 : 0;

        if (ca >= 'a' && ca <= 'z')     /* fold lowercase to uppercase */
            ca -= 32;

        cb = *b;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;

        if ((ca -= cb) != 0)            /* if different, return comparison */
            return ca;

        a++, b++;
    }
}

int strcmpi (char *a, char *b)
{
    int ca, cb;

    for (;;) {
        if ((ca = *a) == 0)             /* get character, stop on null terminator */
            return *b ? -1 : 0;

        if (ca >= 'a' && ca <= 'z')     /* fold lowercase to uppercase */
            ca -= 32;

        cb = *b;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;

        if ((ca -= cb) != 0)            /* if different, return comparison */
            return ca;

        a++, b++;
    }
}

#endif

