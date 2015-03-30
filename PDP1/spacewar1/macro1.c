/*
 * $Id: macro1.c,v 1.73 2003/10/23 22:49:45 phil Exp $
 *
 * TODO:
 * have flex() use nextfiodec()?? (what if legal in repeat???)
 * "flex<SP><SP><SP>x" should give right justified result???
 * squawk if nextfiodec called in a repeat w/ a delim?
 *
 * forbid variables/constants in macros
 * forbid text in repeat??
 * forbid start in repeat or macro
 * use same error TLA's as MACRO???
 * IPA error for overbar on LHS of =
 * variables returns value??
 *
 * macro addressing: labels defined during macro are local use only????
 *  spacewar expects this??? (is it wrong?)
 *
 * self-feeding lines: \n legal anywhere \t is
 *  read next token into "token" buffer -- avoid saving "line"?
 *  remove crocks from "define"
 * list title (first line of file) should not be parsed as source?
 * incorrect listing for bare "start"
 * list only 4 digits for address column
 *
 * other;
 * note variables in symbol dump, xref?
 * no "permenant" symbols; flush -p? rename .ini?
 * keep seperate macro/pseudo table?
 * verify bad input(?) detected
 * implement dimension pseudo?
 * remove crocks for '/' and ','?
 */

/*
 * Program:  MACRO1
 * File:     macro1.c
 * Author:   Gary A. Messenbrink <gary@netcom.com> (macro8)
 *  MACRO7 modifications: Bob Supnik <bob.supnik@ljo.dec.com>
 *  MACRO1 modifications: Bob Supnik <bob.supnik@ljo.dec.com>
 *  slashed to be more like real MACRO like by Phil Budne <phil@ultimate.com>
 *
 * Purpose:  A 2 pass PDP-1 assembler
 *
 * NAME
 *    macro1 - a PDP-1 assembler.
 *
 * SYNOPSIS:
 *    macro1 [ -d -p -m -r -s -x ] inputfile inputfile...
 *
 * DESCRIPTION
 *    This is a cross-assembler to for PDP-1 assembly language programs.
 *    It will produce an output file in rim format only.
 *    A listing file is always produced and with an optional symbol table
 *    and/or a symbol cross-reference (concordance).  The permanent symbol
 *    table can be output in a form that may be read back in so a customized
 *    permanent symbol table can be produced.  Any detected errors are output
 *    to a separate file giving the filename in which they were detected
 *    along with the line number, column number and error message as well as
 *    marking the error in the listing file.
 *    The following file name extensions are used:
 *   .mac    source code (input)
 *   .lst    assembly listing (output)
 *   .rim    assembly output in DEC's rim format (output)
 *   .prm    permanent symbol table in form suitable for reading after
 *       the EXPUNGE pseudo-op.
 *   .sym    "symbol punch" tape (for DDT, or reloading into macro)
 *
 * OPTIONS
 *    -d   Dump the symbol table at end of assembly
 *    -p   Generate a file with the permanent symbols in it.
 *     (To get the current symbol table, assemble a file than has only
 *      START in it.)
 *    -x   Generate a cross-reference (concordance) of user symbols.
 *    -r   Output a tape using only RIM format (else output block loader)
 *    -s   Output a symbol dump tape (loader + loader blocks)
 *    -S file
 *     Read a symbol tape back in
 *
 * DIAGNOSTICS
 *    Assembler error diagnostics are output to an error file and inserted
 *    in the listing file.  Each line in the error file has the form
 *
 *   <filename>:<line>:<col> : error:  <message> at Loc = <loc>
 *
 *    An example error message is:
 *
 *   bintst.7:17:9 : error:  undefined symbol "UNDEF" at Loc = 07616
 *
 *    The error diagnostics put in the listing start with a two character
 *    error code (if appropriate) and a short message.  A carat '^' is
 *    placed under the item in error if appropriate.
 *    An example error message is:
 *
 *      17 07616 3000      DAC     UNDEF
 *   UD undefined              ^
 *      18 07617 1777      TAD  I  DUMMY
 *
 *    Undefined symbols are marked in the symbol table listing by prepending
 *    a '?' to the symbol.  Redefined symbols are marked in the symbol table
 *    listing by prepending a '#' to the symbol.  Examples are:
 *
 *   #REDEF   04567
 *    SWITCH  07612
 *   ?UNDEF   00000
 *
 *    Refer to the code for the diagnostic messages generated.
 *
 * REFERENCES:
 *    This assembler is based on the pal assember by:
 *   Douglas Jones <jones@cs.uiowa.edu> and
 *   Rich Coon <coon@convexw.convex.com>
 *
 * COPYRIGHT NOTICE:
 *    This is free software.  There is no fee for using it.  You may make
 *    any changes that you wish and also give it away.  If you can make
 *    a commercial product out of it, fine, but do not put any limits on
 *    the purchaser's right to do the same.  If you improve it or fix any
 *    bugs, it would be nice if you told me and offered me a copy of the
 *    new version.
 *
 *
 * Amendments Record:
 *  Version  Date    by   Comments
 *  ------- -------  ---  ---------------------------------------------------
 *    v1.0  12Apr96  GAM  Original
 *    v1.1  18Nov96  GAM  Permanent symbol table initialization error.
 *    v1.2  20Nov96  GAM  Added BINPUNch and RIMPUNch pseudo-operators.
 *    v1.3  24Nov96  GAM  Added DUBL pseudo-op (24 bit integer constants).
 *    v1.4  29Nov96  GAM  Fixed bug in checksum generation.
 *    v2.1  08Dec96  GAM  Added concordance processing (cross reference).
 *    v2.2  10Dec96  GAM  Added FLTG psuedo-op (floating point constants).
 *    v2.3   2Feb97  GAM  Fixed paging problem in cross reference output.
 *    v3.0  14Feb97  RMS  MACRO8X features.
 *          ?        RMS  MACRO7
 *          ?        RMS  MACRO1 released w/ lispswre
 *          ?        RMS  MACRO1 released w/ tools
 *          ?        RMS  MACRO1 released w/ ddt1
 *          2003     PLB  major reworking
 */


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINELEN              96
#define LIST_LINES_PER_PAGE  60     /* Includes 3 line page header. */
#define NAMELEN             128
#define SYMBOL_COLUMNS        5
#define SYMLEN                7
/*#define SYMSIG          4     /* EXP: significant chars in a symbol */
#define SYMBOL_TABLE_SIZE  8192
#define MAC_MAX_ARGS         20
#define MAC_MAX_LENGTH     8192
#define MAC_TABLE_LENGTH   1024     /* Must be <= 4096. */

#define MAX_LITERALS       1000
#define MAX_CONSTANTS        10     /* max number of "constants" blocks  */

#define XREF_COLUMNS          8

#define ADDRESS_FIELD  0007777
#define INDIRECT_BIT   0010000
#define OP_CODE        0760000

#define CONCISE_LC 072
#define CONCISE_UC 074

/* Macro to get the number of elements in an array. */
#define DIM(a) (sizeof(a)/sizeof(a[0]))

#define ISBLANK(c) ((c==' ') || (c=='\f'))
#define ISEND(c)   ((c=='\0')|| (c=='\n') || (c == '\t'))
#define ISDONE(c)  ((c=='/') || ISEND(c))

#define ISOVERBAR(c) (c == '\\' || c == '~')

/* Macros for testing symbol attributes.  Each macro evaluates to non-zero */
/* (true) if the stated condtion is met. */
/* Use these to test attributes.  The proper bits are extracted and then */
/* tested. */
#define M_DEFINED(s)    (((s) & DEFINED) == DEFINED)
#define M_DUPLICATE(s)  (((s) & DUPLICATE) == DUPLICATE)
#define M_FIXED(s)  (((s) & FIXED) == FIXED)
#define M_LABEL(s)  (((s) & LABEL) == LABEL)
#define M_PSEUDO(s) (((s) & PSEUDO) == PSEUDO)
#define M_EPSEUDO(s)    (((s) & EPSEUDO) == EPSEUDO)
#define M_MACRO(s)  (((s) & MACRO) == MACRO)
#define M_NOTRDEF(s)    (((s) & NOTRDEF) != 0)

typedef unsigned char BOOL;
typedef unsigned char BYTE;
typedef          int  WORD32;

#ifndef FALSE
  #define FALSE 0
  #define TRUE (!FALSE)
#endif

/* Line listing styles.  Used to control listing of lines. */
enum linestyle_t
{
  LINE, LINE_VAL, LINE_LOC_VAL, LOC_VAL, LINE_LOC
};
typedef enum linestyle_t LINESTYLE_T;

/* Symbol Types. */
/* Note that the names that have FIX as the suffix contain the FIXED bit */
/* included in the value. */
enum symtyp
{
  UNDEFINED = 0000,
  DEFINED   = 0001,
  FIXED     = 0002,
  LABEL     = 0010    | DEFINED,
  REDEFINED = 0020    | DEFINED,
  DUPLICATE = 0040    | DEFINED,
  PSEUDO    = 0100    | FIXED | DEFINED,
  EPSEUDO   = 0200    | FIXED | DEFINED,
  MACRO     = 0400    | DEFINED,
  DEFFIX    = DEFINED | FIXED,
  NOTRDEF   = (MACRO | PSEUDO | LABEL | FIXED) & ~DEFINED
};
typedef enum symtyp SYMTYP;

enum pseudo_t {
    DECIMAL,
    DEFINE,
    FLEX,
    CONSTANTS,
    OCTAL,
    REPEAT,
    START,
    CHAR,
    VARIABLES,
    TEXT,
    NOINPUT,
    EXPUNGE
};
typedef enum pseudo_t PSEUDO_T;

struct sym_t
{
  SYMTYP  type;
  char    name[SYMLEN];
  WORD32  val;
  WORD32  xref_index;
  WORD32  xref_count;
};
typedef struct sym_t SYM_T;

struct emsg_t
{
  char  *list;
  char  *file;
};
typedef struct emsg_t EMSG_T;

struct errsave_t
{
  char   *mesg;
  WORD32  col;
};
typedef struct errsave_t ERRSAVE_T;

/*----------------------------------------------------------------------------*/

/* Function Prototypes */

int     binarySearch( char *name, int start, int symbol_count );
int     compareSymbols( const void *a, const void *b );
SYM_T  *defineLexeme( WORD32 start, WORD32 term, WORD32 val, SYMTYP type );
SYM_T  *defineSymbol( char *name, WORD32 val, SYMTYP type, WORD32 start);
void    errorLexeme( EMSG_T *mesg, WORD32 col );
void    errorMessage( EMSG_T *mesg, WORD32 col );
void    errorSymbol( EMSG_T *mesg, char *name, WORD32 col );
SYM_T   eval( void );
SYM_T  *evalSymbol( void );
void    getArgs( int argc, char *argv[] );
SYM_T   getExpr( void );
WORD32  getExprs( void );
WORD32  incrementClc( void );
WORD32  literal( WORD32 value );
BOOL    isLexSymbol();
char   *lexemeToName( char *name, WORD32 from, WORD32 term );
void    listLine( void );
SYM_T  *lookup( char *name, int type );
void    moveToEndOfLine( void );
void    next(int);
void    onePass( void );
void    printCrossReference( void );
void    printErrorMessages( void );
void    printLine(char *line, WORD32 loc, WORD32 val, LINESTYLE_T linestyle);
void    printPageBreak( void );
void    printPermanentSymbolTable( void );
void    printSymbolTable( void );
BOOL    pseudo( PSEUDO_T val );
void    punchLocObject( WORD32 loc, WORD32 val );
void    punchOutObject( WORD32 loc, WORD32 val );
void    punchLeader( WORD32 count );
void    punchLoader( void );
void    flushLoader( void );
void    readLine( void );
void    saveError( char *mesg, WORD32 cc );
void    topOfForm( char *title, char *sub_title );
void    constants(void);
void    variables(void);
void    eob(void);
void    dump_symbols(void);

/*----------------------------------------------------------------------------*/

/* Table of pseudo-ops (directives) which are used to setup the symbol */
/* table on startup */
SYM_T pseudos[] =
{
  { PSEUDO,  "consta",  CONSTANTS },
  { PSEUDO,  "define",  DEFINE  },  /* Define macro. */
  { PSEUDO,  "repeat",  REPEAT  },
  { PSEUDO,  "start",   START   },  /* Set starting address. */
  { PSEUDO,  "variab",  VARIABLES },
  { PSEUDO,  "text",    TEXT    },
  { PSEUDO,  "noinpu",  NOINPUT },
  { PSEUDO,  "expung",  EXPUNGE },
/* the following can appear in expressions: */
  { EPSEUDO, "charac",  CHAR    },
  { EPSEUDO, "decima",  DECIMAL },  /* base 10. */
  { EPSEUDO, "flexo",   FLEX    },
  { EPSEUDO, "octal",   OCTAL   },  /* Read literal constants in base 8. */
};

/* Symbol Table */
/* The table is put in lexical order on startup, so symbols can be */
/* inserted as desired into the initial table. */
#define DIO 0320000
#define JMP 0600000
SYM_T permanent_symbols[] =
{
  /* Memory Reference Instructions */
  { DEFFIX, "and",    0020000 },
  { DEFFIX, "ior",    0040000 },
  { DEFFIX, "xor",    0060000 },
  { DEFFIX, "xct",    0100000 },
  { DEFFIX, "lac",    0200000 },
  { DEFFIX, "lio",    0220000 },
  { DEFFIX, "dac",    0240000 },
  { DEFFIX, "dap",    0260000 },
  { DEFFIX, "dip",    0300000 },
  { DEFFIX, "dio",    0320000 },
  { DEFFIX, "dzm",    0340000 },
  { DEFFIX, "add",    0400000 },
  { DEFFIX, "sub",    0420000 },
  { DEFFIX, "idx",    0440000 },
  { DEFFIX, "isp",    0460000 },
  { DEFFIX, "sad",    0500000 },
  { DEFFIX, "sas",    0520000 },
  { DEFFIX, "mul",    0540000 },
  { DEFFIX, "mus",    0540000 },    /* for spacewar */
  { DEFFIX, "div",    0560000 },
  { DEFFIX, "dis",    0560000 },    /* for spacewar */
  { DEFFIX, "jmp",    0600000 },
  { DEFFIX, "jsp",    0620000 },
  { DEFFIX, "skip",   0640000 },    /* for spacewar */
  { DEFFIX, "cal",    0160000 },
  { DEFFIX, "jda",    0170000 },
  { DEFFIX, "i",      0010000 },
  { DEFFIX, "skp",    0640000 },
  { DEFFIX, "law",    0700000 },
  { DEFFIX, "iot",    0720000 },
  { DEFFIX, "opr",    0760000 },
  { DEFFIX, "nop",    0760000 },
  /* Shift Instructions */
  { DEFFIX, "ral",    0661000 },
  { DEFFIX, "ril",    0662000 },
  { DEFFIX, "rcl",    0663000 },
  { DEFFIX, "sal",    0665000 },
  { DEFFIX, "sil",    0666000 },
  { DEFFIX, "scl",    0667000 },
  { DEFFIX, "rar",    0671000 },
  { DEFFIX, "rir",    0672000 },
  { DEFFIX, "rcr",    0673000 },
  { DEFFIX, "sar",    0675000 },
  { DEFFIX, "sir",    0676000 },
  { DEFFIX, "scr",    0677000 },
  { DEFFIX, "1s",     0000001 },
  { DEFFIX, "2s",     0000003 },
  { DEFFIX, "3s",     0000007 },
  { DEFFIX, "4s",     0000017 },
  { DEFFIX, "5s",     0000037 },
  { DEFFIX, "6s",     0000077 },
  { DEFFIX, "7s",     0000177 },
  { DEFFIX, "8s",     0000377 },
  { DEFFIX, "9s",     0000777 },
  /* Skip Microinstructions */
  { DEFFIX, "sza",    0640100 },
  { DEFFIX, "spa",    0640200 },
  { DEFFIX, "sma",    0640400 },
  { DEFFIX, "szo",    0641000 },
  { DEFFIX, "spi",    0642000 },
  { DEFFIX, "szs",    0640000 },
  { DEFFIX, "szf",    0640000 },
  /*{ DEFFIX, "clo",    0651600 },*/

  /* Operate Microinstructions */
  { DEFFIX, "clf",    0760000 },
  { DEFFIX, "stf",    0760010 },
  { DEFFIX, "cla",    0760200 },
  /*{ DEFFIX, "lap",    0760300 },*/
  { DEFFIX, "hlt",    0760400 },
  { DEFFIX, "xx",     0760400 },
  { DEFFIX, "cma",    0761000 },
  { DEFFIX, "clc",    0761200 },
  { DEFFIX, "lat",    0762200 },
  { DEFFIX, "cli",    0764000 },
  /* IOT's */
  /*{ DEFFIX, "ioh",    0730000 },*/
  { DEFFIX, "rpa",    0730001 },
  { DEFFIX, "rpb",    0730002 },
  { DEFFIX, "rrb",    0720030 },
  { DEFFIX, "ppa",    0730005 },
  { DEFFIX, "ppb",    0730006 },
  { DEFFIX, "tyo",    0730003 },
  { DEFFIX, "tyi",    0720004 },
  { DEFFIX, "dpy",    0730007 },    /* for spacewar, munching squares! */
  { DEFFIX, "lsm",    0720054 },
  { DEFFIX, "esm",    0720055 },
  { DEFFIX, "cbs",    0720056 },
  { DEFFIX, "lem",    0720074 },
  { DEFFIX, "eem",    0724074 },
  { DEFFIX, "cks",    0720033 },
};                  /* End-of-Symbols for Permanent Symbol Table */

/* Global variables */
SYM_T *symtab;              /* Symbol Table */
int    symbol_top;          /* Number of entries in symbol table. */

#define LOADERBASE 07751

/* make it relocatable (DDT expects it at 7751) */
#define LOADER_IN LOADERBASE
#define LOADER_B (LOADERBASE+06)
#define LOADER_A (LOADERBASE+07)
#define LOADER_CK (LOADERBASE+025)
#define LOADER_EN1 (LOADERBASE+026)

WORD32 loader[] = {
    0730002,                /* in,  rpb */
    0320000+LOADER_A,           /*  dio a */
    0100000+LOADER_A,           /*  xct a */
    0320000+LOADER_CK,          /*  dio ck */
    0730002,                    /*  rpb */
    0320000+LOADER_EN1,         /*  dio en1 */
    0730002,                /* b,   rpb */
    0000000,                /* a,   xx */
    0210000+LOADER_A,           /*  lac i a */
    0400000+LOADER_CK,          /*  add ck */
    0240000+LOADER_CK,          /*  dac ck */
    0440000+LOADER_A,           /*  idx a */
    0520000+LOADER_EN1,         /*  sas en1 */
    0600000+LOADER_B,           /*  jmp b */
    0200000+LOADER_CK,          /*  lac ck */
    0400000+LOADER_EN1,         /*  add en1 */
    0730002,                    /*  rpb */
    0320000+LOADER_CK,          /*  dio ck */
    0520000+LOADER_CK,              /*  sas ck */
    0760400,                    /*  hlt */
    0600000+LOADER_IN           /*  jmp in */
                    /* ck,  0 */
                    /* en1, 0 */
};

#define LOADERBUFSIZE 0100      /* <=0100, power of 2*/
#define LOADERBUFMASK (LOADERBUFSIZE-1) /* for block alignment */

WORD32 loaderbuf[LOADERBUFSIZE];
WORD32 loaderbufcount;
WORD32 loaderbufstart;

/*----------------------------------------------------------------------------*/

WORD32 *xreftab;            /* Start of the concordance table. */

ERRSAVE_T error_list[20];
int     save_error_count;

char   s_detected[] = "detected";
char   s_error[]    = "error";
char   s_errors[]   = "errors";
char   s_no[]       = "No";
char   s_page[]     = "Page";
char   s_symtable[] = "Symbol Table";
char   s_xref[]     = "Cross Reference";

/* Assembler diagnostic messages. */
/* Some attempt has been made to keep continuity with the PAL-III and */
/* MACRO-8 diagnostic messages.  If a diagnostic indicator, (e.g., IC) */
/* exists, then the indicator is put in the listing as the first two */
/* characters of the diagnostic message.  The PAL-III indicators where used */
/* when there was a choice between using MACRO-8 and PAL-III indicators. */
/* The character pairs and their meanings are: */
/*      DT  Duplicate Tag (symbol) */
/*      IC  Illegal Character */
/*      ID  Illegal Redefinition of a symbol.  An attempt was made to give */
/*          a symbol a new value not via =. */
/*      IE  Illegal Equals  An equal sign was used in the wrong context, */
/*          (e.g., A+B=C, or TAD A+=B) */
/*      II  Illegal Indirect  An off page reference was made, but a literal */
/*          could not be generated because the indirect bit was already set. */
/*      IR  Illegal Reference (address is not on current page or page zero) */
/*      PE  Current, Non-Zero Page Exceeded (literal table flowed into code) */
/*      RD  ReDefintion of a symbol */
/*      ST  Symbol Table full */
/*      UA  Undefined Address (undefined symbol) */
/*  VR  Value Required */
/*      ZE  Zero Page Exceeded (see above, or out of space) */
EMSG_T  duplicate_label     = { "DT duplicate",  "duplicate label" };
EMSG_T  illegal_blank       = { "IC illegal blank", "illegal blank" };
EMSG_T  illegal_character   = { "IC illegal char",  "illegal character" };
EMSG_T  illegal_expression  = { "IC in expression", "illegal expression" };
EMSG_T  label_syntax        = { "IC label syntax",  "label syntax" };
EMSG_T  not_a_number        = { "IC numeric syntax", "numeric syntax of" };
EMSG_T  number_not_radix    = { "IC radix", "number not in current radix"};
EMSG_T  symbol_syntax       = { "IC symbol syntax", "symbol syntax" };
EMSG_T  illegal_equals      = { "IE illegal =",  "illegal equals" };
EMSG_T  illegal_indirect    = { "II off page",   "illegal indirect" };
EMSG_T  illegal_reference   = { "IR off page",   "illegal reference" };
EMSG_T  undefined_symbol    = { "UD undefined",  "undefined symbol" };
EMSG_T  misplaced_symbol    = { "misplaced symbol", "misplaced symbol" };
EMSG_T  redefined_symbol    = { "RD redefined",  "redefined symbol" };
EMSG_T  value_required      = { "VR value required",  "value required" };
EMSG_T  literal_gen_off     = { "lit generation off",
                                   "literal generation disabled" };
EMSG_T  literal_overflow    = { "PE page exceeded",
                                   "current page literal capacity exceeded" };
EMSG_T  zblock_too_small    = { "expr too small", "ZBLOCK value too small" };
EMSG_T  zblock_too_large    = { "expr too large", "ZBLOCK value too large" };
EMSG_T  no_pseudo_op        = { "not implemented", "Unimplemented pseudo-op" };
EMSG_T  illegal_vfd_value   = { "width out of range",
                                   "VFD field width not in range" };
EMSG_T  no_literal_value    = { "no value",  "No literal value" };
EMSG_T  text_string         = { "no delimiter",
                                    "Text string delimiters not matched" };
EMSG_T  lt_expected         = { "'<' expected",  "'<' expected" };
EMSG_T  symbol_table_full   = { "ST Symbol Tbl full", "Symbol table full" };
EMSG_T  no_macro_name       = { "no macro name", "No name following DEFINE" };
EMSG_T  bad_dummy_arg       = { "bad dummy arg",
                                    "Bad dummy argument following DEFINE" };
EMSG_T  macro_too_long      = { "macro too long", "Macro too long" };
EMSG_T  no_virtual_memory   = { "out of memory",
                                    "Insufficient memory for macro" };
EMSG_T  macro_table_full    = { "Macro Table full", "Macro table full" };
EMSG_T  define_in_repeat    = { "define in a repeat", "Define in a repeat" };

/*----------------------------------------------------------------------------*/

FILE   *errorfile;
FILE   *infile;
FILE   *listfile;
FILE   *listsave;
FILE   *objectfile;
FILE   *objectsave;

char    filename[NAMELEN];
char    listpathname[NAMELEN];
char    sympathname[NAMELEN];
char    objectpathname[NAMELEN];
char   *pathname;
char    permpathname[NAMELEN];

WORD32  mac_count;          /* Total macros defined. */

/*
 * malloced macro bodies, indexed by sym->val dummies are evaluated at
 * invocation time, and value saved in "args"; if recursive macros are
 * desired (without conditionals, how would you escape?), keep a name
 * list here and move symbols to "macinv"
 */
struct macdef {
    int nargs;              /* number of args */
    SYM_T args[MAC_MAX_ARGS+1];     /* symbol for each and one for "r" */
    char body[1];           /* malloc'ed accordingly */
} *mac_defs[MAC_TABLE_LENGTH];

struct macinv {             /* current macro invocation */
    char    mac_line[LINELEN];      /* Saved macro invocation line. */
    WORD32  mac_cc;         /* Saved cc after macro invocation. */
    char   *mac_ptr;            /* Pointer to macro body, NULL if no macro. */
    struct macdef *defn;        /* pointer to definition for dummies */
    struct macinv *prev;        /* previous invocation in stack */
} *curmacro;                /* macro stack */

int nrepeats;           /* count of nested repeats */

int     list_lineno;
int     list_pageno;
char    list_title[LINELEN];
BOOL    list_title_set;         /* Set if TITLE pseudo-op used. */
char    line[LINELEN];          /* Input line. */
int     lineno;             /* Current line number. */
int     page_lineno;            /* print line number on current page. */
WORD32  listed;             /* Listed flag. */
WORD32  listedsave;

WORD32  cc;             /* Column Counter (char position in line). */
WORD32  clc;                /* Location counter */
BOOL    end_of_input;           /* End of all input files. */
int     errors;             /* Number of errors found so far. */
BOOL    error_in_line;          /* TRUE if error on current line. */
int     errors_pass_1;          /* Number of errors on pass 1. */
int     filix_curr;         /* Index in argv to current input file. */
int     filix_start;            /* Start of input files in argv. */
int lexstartprev;           /* Where previous lexeme started. */
int lextermprev;            /* Where previous lexeme ended. */
int lexstart;           /* Index of current lexeme on line. */
int lexterm;            /* Index of character after current lexeme. */
int overbar;            /* next saw an overbar in last token */

int nconst;             /* number of "constants" blocks */
int lit_count[MAX_CONSTANTS];   /* # of lits in each block in pass 1 */
WORD32  lit_loc[MAX_CONSTANTS];     /* Base of literal blocks */

int noinput;            /* don't punch loader */

int nvars;              /* number of variables */
WORD32  vars_addr;          /* address of "variables" */
WORD32  vars_end;           /* end of "variables" */

/* pass 2 only; */
int nlit;               /* number of literals in litter[] */
WORD32  litter[MAX_LITERALS];       /* literals */

WORD32  maxcc;              /* Current line length. */
BOOL    nomac_exp;          /* No macro expansion */
WORD32  pass;               /* Number of current pass. */
BOOL    print_permanent_symbols;
WORD32  radix;              /* Default number radix. */
BOOL    rim_mode;           /* RIM mode output. */
BOOL    sym_dump;           /* punch symbol tape */
int     save_argc;          /* Saved argc. */
char   **save_argv;         /* Saved *argv[]. */
WORD32  start_addr;         /* Saved start address. */
BOOL    symtab_print;           /* Print symbol table flag */
BOOL    xref;

SYM_T   sym_undefined = { UNDEFINED, "", 0 };/* Symbol Table Terminator */

/* initial data from SIMH v3.0 pdp1_stddev.c (different encoding of UC/LC) */
#define UC 0100             /* Upper case */
#define LC 0200
#define CHARBITS 077
#define BC LC|UC            /* both case bits */
#define BAD 014             /* unused concise code */

unsigned char ascii_to_fiodec[128] = {
    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,
    BC|075, BC|036, BAD,    BAD,    BAD,    BC|077, BAD,    BAD,
    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,
    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,    BAD,
    BC|000, UC|005, UC|001, UC|004, BAD,    BAD,    UC|006, UC|002,
    LC|057, LC|055, UC|073, UC|054, LC|033, LC|054, LC|073, LC|021,
    LC|020, LC|001, LC|002, LC|003, LC|004, LC|005, LC|006, LC|007,
    LC|010, LC|011, BAD,    BAD,    UC|007, UC|033, UC|010, UC|021,
    LC|040, UC|061, UC|062, UC|063, UC|064, UC|065, UC|066, UC|067,
    UC|070, UC|071, UC|041, UC|042, UC|043, UC|044, UC|045, UC|046,
    UC|047, UC|050, UC|051, UC|022, UC|023, UC|024, UC|025, UC|026,
    UC|027, UC|030, UC|031, UC|057, LC|056, UC|055, UC|011, UC|040,
    UC|020, LC|061, LC|062, LC|063, LC|064, LC|065, LC|066, LC|067,
    LC|070, LC|071, LC|041, LC|042, LC|043, LC|044, LC|045, LC|046,
    LC|047, LC|050, LC|051, LC|022, LC|023, LC|024, LC|025, LC|026,
    LC|027, LC|030, LC|031, BAD,    UC|056, BAD,    UC|003, BC|075
};

/* for symbol punch tape conversion only!! */
char fiodec_to_ascii[64] = {
    0, '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 0, 0, 0, 0, 0, 0,
    '0', 0, 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z', 0, 0, 0, 0, 0, 0,
    0, 'j', 'k', 'l', 'm', 'n', 'o', 'p',
    'q', 'r', 0, 0, 0, 0, 0, 0,
    0, 'a', 'b', 'c', 'd', 'e', 'f', 'g',
    'h', 'i', 0, 0, 0, 0, 0, 0 };

/* used at startup & for expunge */
void
init_symtab(void) {
    /* Place end marker in symbol table. */
    symtab[0] = sym_undefined;
    symbol_top = 0;
}

/*  Function:  main */
/*  Synopsis:  Starting point.  Controls order of assembly. */
int
main( int argc, char *argv[] )
{
    int     ix;
    int     space;

    save_argc = argc;
    save_argv = argv;

    /* Set the default values for global symbols. */
    print_permanent_symbols = FALSE;
    nomac_exp = TRUE;
    rim_mode = FALSE;           /* default to loader tapes */
    sym_dump = FALSE;
    noinput = FALSE;

    symtab_print = FALSE;
    xref = FALSE;
    pathname = NULL;

    /* init symbol table before processing arguments, so we can
     * load symbol punch tapes on the fly
     */

    /*
     * Setup the error file in case symbol table overflows while
     * installing the permanent symbols.
     */
    errorfile = stderr;
    pass = 0;               /* required for symbol table init */
    symtab = (SYM_T *) malloc( sizeof( SYM_T ) * SYMBOL_TABLE_SIZE );

    if( symtab == NULL ) {
    fprintf( stderr, "Could not allocate memory for symbol table.\n");
    exit( -1 );
    }

    init_symtab();

    /* Enter the pseudo-ops into the symbol table */
    for( ix = 0; ix < DIM( pseudos ); ix++ )
    defineSymbol( pseudos[ix].name, pseudos[ix].val, pseudos[ix].type, 0 );

    /* Enter the predefined symbols into the table. */
    /* Also make them part of the permanent symbol table. */
    for( ix = 0; ix < DIM( permanent_symbols ); ix++ )
    defineSymbol( permanent_symbols[ix].name,
              permanent_symbols[ix].val,
              permanent_symbols[ix].type, 0 );

    /* Get the options and pathnames */
    getArgs( argc, argv );

    /* Do pass one of the assembly */
    pass = 1;
    onePass();
    errors_pass_1 = errors;

    /* Set up for pass two */
    objectfile = fopen( objectpathname, "wb" );
    objectsave = objectfile;

    listfile = fopen( listpathname, "w" );
    listsave = listfile;

    /* XXX punch title into tape! */
    punchLeader( 0 );
    if (!rim_mode) {
    punchLoader();
    punchLeader(5);
    }

    if (nlit > 0)
    constants();            /* implied "constants"? */

    /* Do pass two of the assembly */
    errors = 0;
    save_error_count = 0;

    if( xref ) {
    /* Get the amount of space that will be required for the concordance */
    for( space = 0, ix = 0; ix < symbol_top; ix++ ) {
        symtab[ix].xref_index = space; /* Index into concordance table. */
        space += symtab[ix].xref_count + 1;
        symtab[ix].xref_count = 0;  /* Clear the count for pass 2. */
    }
    /* Allocate & clear the necessary space. */
    xreftab = (WORD32 *) calloc( space, sizeof( WORD32 ));
    }
    pass = 2;
    onePass();

    objectfile = objectsave;

    /* Works great for trailer. */
    punchLeader( 1 );

    /* undo effects of NOLIST for any following output to listing file. */
    listfile = listsave;

    /* Display value of error counter. */
    if( errors == 0 ) {
    fprintf( listfile, "\n      %s %s %s\n", s_no, s_errors, s_detected );
    }
    else {
    fprintf( errorfile, "\n      %d %s %s\n", errors, s_detected,
         ( errors == 1 ? s_error : s_errors ));
    fprintf( listfile, "\n      %d %s %s\n", errors, s_detected,
         ( errors == 1 ? s_error : s_errors ));
    }

    if( symtab_print )
    printSymbolTable();

    if( print_permanent_symbols )
    printPermanentSymbolTable();

    if( xref )
    printCrossReference();

    fclose( objectfile );
    fclose( listfile );
    if( errors == 0 && errors_pass_1 == 0 ) {
    /* after closing objectfile -- we reuse the FILE *!! */
    if (sym_dump)
        dump_symbols();
    }
    else
    remove( objectpathname );

    return( errors != 0 );
} /* main() */

/* read a word from a binary punch file */
WORD32
getw(FILE *f)
{
    int i, c;
    WORD32 w;

    w = 0;
    for (i = 0; i < 3;) {
    c = getc(f);
    if (c == -1)
        return -1;
    if (c & 0200) {         /* ignore if ch8 not punched */
        w <<= 6;
        w |= c & 077;
        i++;
    }
    }
    return w;
}

/*
 * "permute zone bits" like MACRO does for proper sorting
 * (see routine "per" in MACRO) -- it's what DDT expects
 *
 * it's it's own inverse!
 */

WORD32
permute(WORD32 name)
{
    WORD32 temp;

    temp = name & 0202020;      /* get zone bits */
    temp = ((temp << 1) & 0777777) | ((temp >> 17) & 1); /* rotate left */
    name ^= temp;           /* flip zone bits */
    name ^= 0400000;            /* toggle sign */
    return name;
}

/* add a symbol from a "symbol punch" tape */
void
addsym(WORD32 sym, WORD32 val)
{
    char name[4];

    sym = permute(sym);
    name[0] = fiodec_to_ascii[(sym >>12) & 077];
    name[1] = fiodec_to_ascii[(sym >> 6) & 077];
    name[2] = fiodec_to_ascii[sym & 077];
    name[3] = '\0';
    defineSymbol( name, val, LABEL, 0);
}

void
read_symbols(char *fname)
{
    FILE *f;

    f = fopen(fname, "rb");
    if (!f) {
    perror(fname);
    exit(1);
    }

    /* skip loader */
    for (;;) {
    WORD32 w;

    w = getw(f);
    if (w == -1)
        goto err;           /* XXX complain? */
    if ((w & OP_CODE) == JMP)
        break;
    if ((w & OP_CODE) != DIO)
        goto err;           /* XXX complain? */
    w = getw(f);
    if (w == -1)
        goto err;           /* XXX complain? */
    }


    /* XXX should push block reader down into a co-routine */
    for (;;) {
    WORD32 start, end, sum;

    start = getw(f);
    if ((start & OP_CODE) == JMP) {
        fclose(f);
        return;
    }

    if (start == -1 || (start & OP_CODE) != DIO)
        goto err;

    end = getw(f);
    if (end == -1 || (end & OP_CODE) != DIO)
        goto err;           /* XXX complain? */

    sum = start + end;
    while (start < end) {
        WORD32 sym, val;
        sym = getw(f);
        if (sym == -1)
        goto err;
        sum += sym;
        start++;
        /* XXX handle block boundaries? */
        if (start >= end)
        goto err;
        val = getw(f);
        if (val == -1)
        goto err;
        /*printf("%06o %06o\n", sym, val);*/
        addsym(sym, val);
        sum += val;
        start++;
    }
    start = getw(f);        /* eat checksum XXX verify? */
    if (start == -1)
        goto err;
    /* roll over all the overflows at once */
    if (sum & ~0777777) {
        sum = (sum & 0777777) + (sum >> 18);
        if (sum & 01000000)         /* one more time */
        sum++;
    }
    if (start != sum)
        goto err;
    }
err:
    fprintf(stderr, "error reading symbol file %s\n", fname);
    exit(1);
}

/*  Function:  getArgs */
/*  Synopsis:  Parse command line, set flags accordingly and setup input and */
/*             output files. */
void getArgs( int argc, char *argv[] )
{
  WORD32  len;
  WORD32  ix, jx;

  /* Set the defaults */
  infile = NULL;
  listfile = NULL;
  listsave = NULL;
  objectfile = NULL;
  objectsave = NULL;

  for( ix = 1; ix < argc; )
  {
    if( argv[ix][0] == '-' )
    {
      char *switches = argv[ix++];
      for( jx = 1; switches[jx] != 0; jx++ )
      {
        switch( switches[jx] )
        {
        case 'd':
          symtab_print = TRUE;
          break;

        case 'r':
          rim_mode = TRUE;      /* punch pure rim-mode tapes */
          break;

    case 's':
      sym_dump = TRUE;
      break;

        case 'm':
          nomac_exp = FALSE;
          break;

        case 'p':
          print_permanent_symbols = TRUE;
          break;

        case 'x':
          xref = TRUE;
          break;

    case 'S':
      if (ix <= argc)
          read_symbols(argv[ix++]);
      break;

        default:
          fprintf( stderr, "%s: unknown flag: %s\n", argv[0], argv[ix] );
          fprintf( stderr, " -d -- dump symbol table\n" );
          fprintf( stderr, " -m -- output macro expansions\n" );
          fprintf( stderr, " -p -- output permanent symbols to file\n" );
          fprintf( stderr, " -r -- output RIM format file\n" );
          fprintf( stderr, " -s -- output symbol punch tape to file\n" ); 
          fprintf( stderr, " -S file -- read symbol punch tape\n" );
      fprintf( stderr, " -x -- output cross reference to file\n" );
          fflush( stderr );
          exit( -1 );
        } /* end switch */
      } /* end for */
    }
    else
    {
      filix_start = ix;
      pathname = argv[ix];
      break;
    }
  } /* end for */

  if( pathname == NULL )
  {
    fprintf( stderr, "%s:  no input file specified\n", argv[0] );
    exit( -1 );
  }

  len = strlen( pathname );
  if( len > NAMELEN - 5 )
  {
    fprintf( stderr, "%s: pathname \"%s\" too long\n", argv[0], pathname );
    exit( -1 );
  }

  /* Now make the pathnames */
  /* Find last '.', if it exists. */
  jx = len - 1;
  while( pathname[jx] != '.'  && pathname[jx] != '/'
      && pathname[jx] != '\\' && jx >= 0 )
  {
    jx--;
  }

  switch( pathname[jx] )
  {
  case '.':
    break;

  case '/':
  case '\\':
    jx = len;
    break;

  default:
    break;
  }

  /* Add the pathname extensions. */
  strncpy( objectpathname, pathname, jx );
  objectpathname[jx] = '\0';
  strcat( objectpathname, ".rim");

  strncpy( listpathname, pathname, jx );
  listpathname[jx] = '\0';
  strcat( listpathname, ".lst" );

  strncpy( permpathname, pathname, jx );
  permpathname[jx] = '\0';
  strcat( permpathname, ".prm" );

  strncpy( sympathname, pathname, jx );
  sympathname[jx] = '\0';
  strcat( sympathname, ".sym" );

  /* Extract the filename from the path. */
  if( isalpha( pathname[0] ) && pathname[1] == ':' && pathname[2] != '\\' )
      pathname[1] = '\\';       /* MS-DOS style pathname */

  jx = len - 1;
  while( pathname[jx] != '/' && pathname[jx] != '\\' && jx >= 0 )
      jx--;
  strcpy( filename, &pathname[jx + 1] );
} /* getArgs() */


int
invokeMacro(int index)
{
    struct macinv *mip;
    struct macdef *mdp;
    int jx;

    mdp = mac_defs[index];
    if (mdp == NULL || mdp->body[0] == '\0')
    return 0;

    /* Find arguments. */
    while (ISBLANK(line[lexstart]))
    next(0);

    mip = calloc(1, sizeof(struct macinv));
    if (!mip) {
    fprintf(stderr, "could not allocate memory for macro invocation\n");
    exit(1);
    }
    mip->defn = mdp;

    /* evaluate args, saving values in SYM_T entries in defn.
     * (cannot have recursive macros)
     */
    mdp->args[0].val = clc;     /* r is location at start */
    for( jx = 1; !ISDONE(line[lexstart]) && jx <= MAC_MAX_ARGS; ) {
    WORD32 val;

    next(0);
    if (ISDONE(line[lexstart]))
        break;

    if (line[lexstart] == ',')
        next(0);

    while( ISBLANK( line[lexstart] ))
        next(0);

    if (ISDONE(line[lexstart]))
        break;

    val = getExprs();

    /* ignore excess values silently? */
    if (jx <= mdp->nargs)
        mdp->args[jx].val = val;
    jx++;
    } /* end for */

    /* XXX complain if too few actuals? -- nah */
    while (jx <= mdp->nargs)
    mdp->args[jx++].val = 0;

    strcpy(mip->mac_line, line);    /* save line */
    mip->mac_cc = cc;           /* save position in line */
    mip->mac_ptr = mdp->body;
    mip->prev = curmacro;       /* push the old entry */
    curmacro = mip;         /* step up to the plate! */
    return 1;
}

/* process input; used by onePass and repeat */
void
processLine() {
    if (!list_title_set) {
    char *cp;

    /* assert(sizeof(title) >= sizeof(line)); */
    strcpy(list_title, line);

    if ((cp = strchr(list_title, '\n')))
        *cp = '\0';

    if (list_title[0]) {
        list_title_set = TRUE;
        fprintf(stderr, "%s - pass %d\n", list_title, pass );
        /* XXX punch title into tape  banner (until an '@' seen) */
    }
    return;
    }

    for (;;) {
    int jx;
    SYM_T evalue;

    next(0);
    if( end_of_input )
        return;

    if( ISEND( line[lexstart] )) {
        if (line[lexstart] != '\t')
        return;
        continue;
    }
    if (line[lexstart] == '/')  /* comment? */
        return;         /* done */

    /* look ahead for 'exp/' */
    /* skip until whitespace or terminator */
    for( jx = lexstart; jx < maxcc; jx++ )
        if( ISBLANK(line[jx]) || ISDONE(line[jx]))
        break;
    if( line[jx] == '/') {      /* EXP/ set location */
        WORD32  newclc;

        newclc = getExprs();

        /* Do not change Current Location Counter if an error occurred. */
        if( !error_in_line )
        clc = newclc;

        printLine( line, newclc, 0, LINE_LOC );
        cc = jx + 1;
        next(0);            /* discard slash */
        continue;
    }

    switch( line[lexterm] ) {
    case ',':
        if( isLexSymbol()) {
        WORD32 val;
        SYM_T *sym;
        char name[SYMLEN];

        /* Use lookup so symbol will not be counted as reference. */
        sym = lookup(lexemeToName(name, lexstart, lexterm), UNDEFINED);

        if (curmacro) {
            /* relative during macro expansion!! */
            val = clc - curmacro->defn->args[0].val;
        }
        else
            val = clc;

        if( M_DEFINED( sym->type )) {
            if( sym->val != val && pass == 2 )
            errorSymbol( &duplicate_label, sym->name, lexstart );
            sym->type |= DUPLICATE; /* XXX never used! */
        }
        /* Must call define on pass 2 to generate concordance. */
        defineLexeme( lexstart, lexterm, val, LABEL );
        }
        else if (isdigit(line[lexstart])) { /* constant, */
        int i;
        WORD32 val = 0;

        for( i = lexstart; i < lexterm; i++ ) {
            if( isdigit( line[i] )) {
            int digit;
            digit = line[i] - '0';
            if( digit >= radix ) {
                errorLexeme( &number_not_radix, i );
                val = 0;
                break;
            }
            val = val * radix + digit;
            }
            else {
            errorLexeme( &not_a_number, lexstart );
            val = 0;
            break;
            }
        }
        if (i == lexterm) {
            if( clc != val && pass == 2 )
            errorLexeme( &duplicate_label, lexstart); /* XXX */
        }
        }
        else
        errorLexeme( &label_syntax, lexstart );
        next(0);            /* skip comma */
        continue;

    case '=':
        if( isLexSymbol()) {
        WORD32 start, term, val;

        start = lexstart;
        term = lexterm;
        next(0);        /* skip symbol */
        next(0);        /* skip trailing = */
        val = getExprs();
        defineLexeme( start, term, val, DEFINED );
        printLine( line, 0, val, LINE_VAL );
        }
        else {
        errorLexeme( &symbol_syntax, lexstartprev );
        next(0);        /* skip symbol */
        next(0);        /* skip trailing = */
        getExprs();     /* skip expression */
        }
        continue;
    } /* switch on terminator */

    if( isLexSymbol()) {
        SYM_T  *sym;
        WORD32  val;

        sym = evalSymbol();
        val = sym->val;
        if( M_MACRO(sym->type)) {
        if (!invokeMacro(val))
            next(0);        /* bad defn? or body is empty! */
        continue;
        } /* macro invocation */
        else if( M_PSEUDO(sym->type)) { /* NO EPSEUDOs */
        pseudo( (PSEUDO_T)val & 0777777 );
        continue;
        } /* pseudo */
    } /* macro, or non-char pseudo */

    evalue = getExpr();
    if (evalue.type != PSEUDO) {    /* not a  bare pseudo-op? */
        if (line[lexstart] == ',') {    /* EXP, */
        if(evalue.val != clc && pass == 2 )
            errorLexeme( &duplicate_label, lexstart); /* XXX */
        }
        else if (line[lexstart] == '/') {   /* EXP/ */
        clc = evalue.val;
        printLine( line, clc, 0, LINE_LOC );
        next(0);
        }
        else {
        punchOutObject( clc, evalue.val & 0777777); /* punch it! */
        incrementClc();
        }
    }
    } /* forever */
}

/*  Function:  onePass */
/*  Synopsis:  Do one assembly pass. */
void onePass() {
    int     ix;

    clc = 4;                /* Default location is 4 */
    start_addr = 0;         /* No starting address. */
    nconst = 0;             /* No constant blocks seen */
    nvars = 0;              /* No variables seen */

    while (curmacro) {          /* pop macro stack */
    struct macinv *mp;

    mp = curmacro->prev;
    free(curmacro);
    curmacro = mp;
    }

    for( ix = 0; ix < mac_count; ix++) {
    if (mac_defs[ix])
        free( mac_defs[ix] );
    mac_defs[ix] = NULL;
    }
    mac_count = 0;          /* No macros defined. */

    listed = TRUE;
    lineno = 0;
    list_pageno = 0;
    list_lineno = 0;
    list_title_set = FALSE;
    page_lineno = LIST_LINES_PER_PAGE;  /* Force top of page for new titles. */
    radix = 8;              /* Initial radix is octal (base 8). */

    /* Now open the first input file. */
    end_of_input = FALSE;
    filix_curr = filix_start;       /* Initialize pointer to input files. */
    if(( infile = fopen( save_argv[filix_curr], "r" )) == NULL ) {
    fprintf( stderr, "%s: cannot open \"%s\"\n", save_argv[0],
        save_argv[filix_curr] );
    exit( -1 );
    }

    for (;;) {
    readLine();
    if (end_of_input) {
        eob();
        fclose( infile );
        return;
    }
    processLine();
    } /* forever */
} /* onePass */


/*  Function:  getExprs */
/*  Synopsys:  gutted like a fish */
WORD32 getExprs()
{
    SYM_T sym;

    sym = getExpr();
    if (sym.type == PSEUDO)
    errorMessage( &value_required, lexstart ); /* XXX wrong pointer? */

    return sym.val & 0777777;
} /* getExprs */


SYM_T getExpr()
{
    SYM_T sym;

    sym = eval();

    /* Here we assume the current lexeme is the operator separating the */
    /* previous operator from the next, if any. */

    for (;;) {
    int space;
    /*
     * falling out of switch breaks loop and returns from routine
     * so if you want to keep going, you must "continue"!!
     */
    space = FALSE;
    switch( line[lexstart] ) {
    case ' ':
        space = TRUE;
        /* fall */
    case '+':           /* add */
        next(1);            /* skip operator */
        if (space && ISEND(line[lexstart])) /* tollerate a trailing space */
        return sym;
        sym.val += eval().val;  /* XXX look at type? */
        sym.type = DEFINED;
        if( sym.val >= 01000000 )
        sym.val = ( sym.val + 1 ) & 0777777;
        continue;

    case '-':           /* subtract */
        next(1);            /* skip over the operator */
        sym.val += eval().val ^ 0777777; /* XXX look at type? */
        sym.type = DEFINED;
        if( sym.val >= 01000000 )
        sym.val = ( sym.val + 1 ) & 0777777;
        continue;

    case '*':           /* multiply */
        next(1);            /* skip over the operator */
        sym.val *= eval().val;
        sym.type = DEFINED;
        if( sym.val >= 01000000 )
        sym.val = ( sym.val + 1 ) & 0777777;
        continue;

#if 0
    case '%':           /* divide !??? */
        /*
         * neither '%' nor the divide symbol appear in FIO-DEC,
         * does any known program use such an operator?
         * Easily confused for "MOD", which is how C uses '%'!
         */
        next(1);
        sym.val /= eval().val;
        sym.type = DEFINED;
        continue;
#endif

    case '&':           /* and */
        next(1);            /* skip over the operator */
        sym.val &= eval().val;
        sym.type = DEFINED;
        continue;

    case '!':           /* or */
        next(1);            /* skip over the operator */
        sym.val |= eval().val;
        sym.type = DEFINED;
        continue;

    case '/':
    case ')':
    case ']':
    case ':':
    case ',':
        break;

    case '=':
        errorMessage( &illegal_equals, lexstart );
        moveToEndOfLine();
        sym.val = 0;
        break;

    default:
        if (!ISEND(line[lexstart])) {
        errorMessage( &illegal_expression, lexstart );
        moveToEndOfLine();
        sym.val = 0;
        break;
        }
    } /* switch */
    break;              /* break loop!! */
    } /* "forever" */
    return( sym );
} /* getExpr */

/*
 * return fio-dec code for next char
 * embeds shifts as needed
 */
int
nextfiodec(int *ccase, int delim)
{
    unsigned char c;

    for (;;) {
    if (cc >= maxcc) {
        if (delim == -1)
        return -1;

        /* XXX MUST NOT BE IN A REPEAT!! */
        readLine();         /* danger will robinson! */
        if (end_of_input)
        return -1;
    }
    c = line[cc];
    switch (c) {
    case '\n':
        c = '\r';
        break;
    case '\r':
        continue;
    }
    break;
    }

    if (delim != -1 && c == delim) {
    if (*ccase == LC) {
        cc++;           /* eat delim */
        return -1;
    }
    *ccase = LC;
    return CONCISE_LC;      /* shift down first */
    }

    if (c > 0177) {         /* non-ascii */
    errorMessage( &illegal_character, cc );
    c = 0;              /* space?! */
    }

    c = ascii_to_fiodec[c&0177];
    if (c == BAD) {
    errorMessage( &illegal_character, cc );
    c = 0;              /* space?! */
    }

    if (!(c & *ccase)) {        /* char not in current case? */
    *ccase ^= BC;           /* switch case */
    if (*ccase == LC)
        return CONCISE_LC;      /* shift down */
    else
        return CONCISE_UC;      /* shift up */
    }
    cc++;
    return c & CHARBITS;
}

/*
 * Function: flex
 * Synopsis: Handle data for "flexo" pseudo
 * handle upper case by doing shifts
 */

WORD32 flex()
{
    WORD32 w;
    int shift;
    int ccase;

    if (line[lexstart] == ' ')      /* always? */
    next(0);

    /* original version appears to take next 3 characters,
     * REGARDLESS of what they are (tab, newline, space?)!
     */
    w = 0;
    ccase = LC;             /* current case */
    for (shift = 12; shift >= 0; shift -= 6) {
    unsigned char c;
    if( lexstart >= maxcc )
        break;

    c = line[lexstart];
    if (c == '\t' || c == '\n') {
        if (ccase == LC)
        break;
        c = CONCISE_LC;         /* shift down first */
    }
    else {
        if (c > 0177) {     /* non-ascii */
        errorMessage( &illegal_character, lexstart );
        c = 0;
        }

        c = ascii_to_fiodec[c&0177];
        if (c == BAD) {
        errorMessage( &illegal_character, lexstart );
        c = 0;
        }

        if (!(c & ccase)) {     /* char not in current case? */
        ccase ^= BC;        /* switch case */
        if (ccase == LC)
            c = CONCISE_LC; /* shift down */
        else
            c = CONCISE_UC;     /* shift up */
        }
        else
        lexstart++;
    }
    w |= (c & CHARBITS) << shift;
    }
    /* error to get here w/ case == UC? nah. shift down could be next */
    return w;
} /* flex */

/*
 * Function: getChar
 * Synopsis: Handle data for "char" pseudo
 */

WORD32 getChar()
{
    unsigned char c, pos;

    if( cc >= maxcc )
    return 0;           /* XXX error? */
    pos = line[cc++];
    if (pos != 'l' && pos != 'm' && pos != 'r') {
    errorMessage( &illegal_character, lexstart );
    return 0;
    }

    if( cc >= maxcc )
    return 0;           /* XXX error? */

    c = line[cc++];
    if (c > 0177) {
    errorMessage( &illegal_character, lexstart );
    c = 0;
    }

    c = ascii_to_fiodec[c];
    if (c == BAD) {
    errorMessage( &illegal_character, lexstart );
    c = 0;
    }

    if (!(c & LC)) {            /* upper case only char? */
    c = CONCISE_UC;         /* take a shift up */
    cc--;               /* and leave char for next luser */
    }

    c &= CHARBITS;
    switch (pos) {
    case 'l': return c << 12;
    case 'm': return c << 6;
    case 'r': return c;
    }
    /* should not happen */
    return 0;
} /* flex */

/*  Function:  eval */
/*  Synopsis:  Get the value of the current lexeme, and advance.*/
SYM_T eval2()
{
  WORD32  digit;
  WORD32  from;
  SYM_T  *sym;
  WORD32  val;
  SYM_T   sym_eval;

  sym_eval.type = DEFINED;
  sym_eval.name[0] = '\0';
  sym_eval.val = sym_eval.xref_index = sym_eval.xref_count = 0;

  val = 0;

  if( isLexSymbol()) {
    sym = evalSymbol();
    if(!M_DEFINED( sym->type )) {
      if( pass == 2 )
        errorSymbol( &undefined_symbol, sym->name, lexstart );
      next(1);
      return( *sym );
    }
    else if( M_PSEUDO(sym->type) || M_EPSEUDO(sym->type)) {
      switch (sym->val) {
      case DECIMAL:
        radix = 10;
    sym_eval.type = PSEUDO;
    sym_eval.val = 0;       /* has zero as a value! */
    break;
      case OCTAL:
        radix = 8;
    sym_eval.type = PSEUDO;
    sym_eval.val = 0;       /* has zero as a value */
    break;
      case FLEX:
    next(1);            /* skip keyword */
    sym_eval.val = flex();
    break;
      case CHAR:
    next(1);            /* skip keyword */
    sym_eval.val = getChar();
    break;
      default:
        errorSymbol( &value_required, sym->name, lexstart );
        sym_eval.type = sym->type;
    sym_eval.val = 0;
    break;
      }
      next(1);
      return( sym_eval );
    }
    else if( M_MACRO( sym->type ))
    {
      if( pass == 2 )
      {
        errorSymbol( &misplaced_symbol, sym->name, lexstart );
      }
      sym_eval.type = sym->type;
      sym_eval.val = 0;
      next(1);
      return( sym_eval );
    }
    else
    {
      next(1);
      return( *sym );
    }
  } /* symbol */
  else if( isdigit( line[lexstart] )) {
    from = lexstart;
    val = 0;
    while( from < lexterm ) {
    if( isdigit( line[from] )) {
        digit = line[from++] - '0';
        if( digit >= radix ) {
        errorLexeme( &number_not_radix, from - 1 );
        val = 0;
        break;
        }
        val = val * radix + digit;
    }
    else {
        errorLexeme( &not_a_number, lexstart );
        val = 0;
        break;
    }
    }
    next(1);
    sym_eval.val = val;
    return( sym_eval );
  } /* digit */
  else {
    switch( line[lexstart] ) {
    case '.':               /* Value of Current Location Counter */
    val = clc;
    next(1);
    break;
    case '(':               /* Generate literal */
    next(1);            /* Skip paren */
    val = getExprs();       /* recurse */
    if( line[lexstart] == ')' )
        next(1);            /* Skip end paren */
    sym_eval.val = literal(val);
    return sym_eval;
    case '[':               /* parens!! */
    next(1);
    sym_eval.val = getExprs();  /* mutual recursion */
    if( line[lexstart] == ']' )
        next(1);            /* Skip close bracket */
    else
        errorMessage( &illegal_character, lexstart );
    return sym_eval;
    default:
    switch( line[lexstart] ) {
    case '=':
        errorMessage( &illegal_equals, lexstart );
        moveToEndOfLine();
        break;
    default:
        errorMessage( &illegal_character, lexstart );
        break;
    } /* error switch */
    val = 0;                /* On error, set value to zero. */
    next(1);                /* Go past illegal character. */
    } /* switch on first char */
  } /* not symbol or number */
  sym_eval.val = val;
  return( sym_eval );
} /* eval2 */


SYM_T eval() {
    SYM_T sym;

    switch (line[lexstart]) {
    case '-':               /* unary - */
    next(1);
    sym = eval2();          /* skip op */
    sym.val ^= 0777777;
    break;
    case '+':               /* unary + */
    next(1);            /* skip op */
    /* fall */
    default:
    sym = eval2();
    }
    return sym;
}

/*  Function:  incrementClc */
/*  Synopsis:  Set the next assembly location.  Test for collision with */
/*             the literal tables. */
WORD32 incrementClc()
{
  clc = (( clc + 1 ) & ADDRESS_FIELD );
  return( clc );
} /* incrementClc */


/*  Function:  readLine */
/*  Synopsis:  Get next line of input.  Print previous line if needed. */
void readLine()
{
    BOOL    ffseen;
    WORD32  ix;
    WORD32  iy;
    char    inpline[LINELEN];

    /* XXX panic if nrepeats > 0 (if self-feeding, do the backup here?) */

    listLine();             /* List previous line if needed. */
    error_in_line = FALSE;      /* No error in line. */

    if(curmacro && *curmacro->mac_ptr == '\0') { /* end of macro? */
    struct macinv *mp;

    listed = TRUE;          /* Already listed. */

    /* Restore invoking line. */
    strcpy(line, curmacro->mac_line);
    cc = lexstartprev = curmacro->mac_cc; /* Restore cc. */
    maxcc = strlen( line );     /* Restore maxcc. */

    mp = curmacro->prev;        /* pop stack */
    free(curmacro);
    curmacro = mp;

    return;
    } /* end of macro */

    cc = 0;             /* Initialize column counter. */
    lexstartprev = 0;
    if( curmacro ) {            /* Inside macro? */
    char mc;

    maxcc = 0;
    do {

        mc = *curmacro->mac_ptr++;  /* Next character. */
        /* watch for overflow? how could it?? */
        line[maxcc++] = mc;
    } while( !ISEND( mc ));     /* note: terminates on tab?! */
    line[maxcc] = '\0';
    listed = nomac_exp;
    return;
    } /* inside macro */

    lineno++;               /* Count lines read. */
    listed = FALSE;         /* Mark as not listed. */
 READ_LINE:
    if(( fgets( inpline, LINELEN - 1, infile )) == NULL ) {
    filix_curr++;           /* Advance to next file. */
    if( filix_curr < save_argc ) {  /* More files? */
        fclose( infile );
        if(( infile = fopen( save_argv[filix_curr], "r" )) == NULL ) {
        fprintf( stderr, "%s: cannot open \"%s\"\n", save_argv[0],
            save_argv[filix_curr] );
        exit( -1 );
        }
        list_title_set = FALSE;
        goto READ_LINE;
    }
    else
        end_of_input = TRUE;
    } /* fgets failed */

    ffseen = FALSE;
    for( ix = 0, iy = 0; inpline[ix] != '\0'; ix++ ) {
    if( inpline[ix] == '\f' ) {
        if( !ffseen && list_title_set ) topOfForm( list_title, NULL );
        ffseen = TRUE;
    }
    else
        line[iy++] = inpline[ix];
    }
    line[iy] = '\0';

    /* If the line is terminated by CR-LF, remove, the CR. */
    if( line[iy - 2] == '\r' ) {
    iy--;
    line[iy - 1] = line[iy - 0];
    line[iy] = '\0';
    }
    maxcc = iy;             /* Save the current line length. */
} /* readLine */


/*  Function:  listLine */
/*  Synopsis:  Output a line to the listing file. */
void listLine()
/* generate a line of listing if not already done! */
{
  if( listfile != NULL && listed == FALSE )
  {
    printLine( line, 0, 0, LINE );
  }
} /* listLine */


/*  Function:  printPageBreak */
/*  Synopsis:  Output a Top of Form and listing header if new page necessary. */
void printPageBreak()
{
  if( page_lineno >= LIST_LINES_PER_PAGE )
         /*  ( list_lineno % LIST_LINES_PER_PAGE ) == 0 ) */
  {
    topOfForm( list_title, NULL );
  }
} /* printPageBreak */


/*  Function:  printLine */
/*  Synopsis:  Output a line to the listing file with new page if necessary. */
void printLine( char *line, WORD32 loc, WORD32 val, LINESTYLE_T linestyle )
{
  if( listfile == NULL )
  {
    save_error_count = 0;
    return;
  }

  printPageBreak();

  list_lineno++;
  page_lineno++;
  switch( linestyle )
  {
  default:
  case LINE:
    fprintf( listfile, "%5d                   ", lineno );
    fputs( line, listfile );
    listed = TRUE;
    break;

  case LINE_VAL:
    if( !listed )
    {
      fprintf( listfile, "%5d       %6.6o      ", lineno, val );
      fputs( line, listfile );
      listed = TRUE;
    }
    else
    {
      fprintf( listfile, "            %6.6o\n", val );
    }
    break;

  case LINE_LOC:
    if( !listed )
    {
      fprintf( listfile, "%5d %5.5o             ", lineno, loc );
      fputs( line, listfile );
      listed = TRUE;
    }
    else
    {
      fprintf( listfile, "      %5.5o\n", loc );
    }
    break;

  case LINE_LOC_VAL:
    if( !listed )
    {
      fprintf( listfile, "%5d %5.5o %6.6o      ", lineno, loc, val );
      fputs( line, listfile );
      listed = TRUE;
    }
    else
    {
      fprintf( listfile, "      %5.5o %6.6o\n", loc, val );
    }
    break;

  case LOC_VAL:
    fprintf( listfile, "      %5.5o %6.6o\n", loc, val );
    break;
  }
  printErrorMessages();
} /* printLine */


/*  Function:  printErrorMessages */
/*  Synopsis:  Output any error messages from the current list of errors. */
void printErrorMessages()
{
  WORD32  ix;
  WORD32  iy;

  if( listfile != NULL )
  {
    /* If any errors, display them now. */
    for( iy = 0; iy < save_error_count; iy++ )
    {
      printPageBreak();
      fprintf( listfile, "%-18.18s      ", error_list[iy].mesg );
      if( error_list[iy].col >= 0 )
      {
        for( ix = 0; ix < error_list[iy].col; ix++ )
        {
          if( line[ix] == '\t' )
          {
            putc( '\t', listfile );
          }
          else
          {
            putc( ' ', listfile );
          }
        }
        fputs( "^", listfile );
        list_lineno++;
        page_lineno++;
      }
      fputs( "\n", listfile );
    }
  }
  save_error_count = 0;
} /* printErrorMessages */


/*  Function:  punchObject */
/*  Synopsis:  Put one character to object file */
void punchObject( WORD32 val )
{
  val &= 0377;
  if( objectfile != NULL )
      fputc( val, objectfile );
} /* punchObject */

/*  Function:  punchTriplet */
/*  Synopsis:  Output 18b word as three 6b characters with ho bit set. */
void punchTriplet( WORD32 val )
{
  punchObject((( val >> 12) & 077) | 0200 );
  punchObject((( val >> 6 ) & 077) | 0200 );
  punchObject(( val & 077) | 0200 );
} /* punchTriplet */

void
eob() {
    /* in case no "start" in file (an error?) */
}

/*  Function:  punchLeader */
/*  Synopsis:  Generate 2 feet of leader on object file, as per DEC */
/*             documentation.  Paper tape has 10 punches per inch. */
void punchLeader( WORD32 count )
{
  WORD32  ix;

  /* If value is zero, set to the default of 2 feet of leader. */
  count = ( count == 0 ) ? 240 : count;

  if( objectfile != NULL )
  {
    for( ix = 0; ix < count; ix++ )
    {
      fputc( 0, objectfile );
    }
  }
} /* punchLeader */

/*  Function:  punchOutObject */
/*  Synopsis:  Output the current line and then then punch value to the */
/*             object file. */
void punchOutObject( WORD32 loc, WORD32 val )
{
  printLine( line, loc, val, LINE_LOC_VAL );
  punchLocObject( loc, val );
} /* punchOutObject */


/*  Function:  punchLocObjectRIM */
/*  Synopsis:  Output the word in RIM mode */
void punchLocObjectRIM( WORD32 loc, WORD32 val )
{
    punchTriplet( DIO | loc );
    punchTriplet( val );
} /* punchLocObject */

/* punch loader in RIM mode */
void
punchLoader() {
    int i;

    if (noinput)
    return;

    for (i = 0; i < DIM(loader); i++)
    punchLocObjectRIM(LOADERBASE+i, loader[i]);
    punchTriplet( JMP | LOADERBASE );
}

/*
 * flush out loader buffer; output a block:
 * DIO start
 * DIO end+1
 * .... data ....
 * sum
 */
#define PW(X) { WORD32 x = X; sum += x; punchTriplet(x); }
void
flushLoader() {
    WORD32 sum;
    int i;

    if (loaderbufcount == 0)
    return;

    sum = 0;
    PW( DIO | loaderbufstart );
    PW( DIO | loaderbufstart + loaderbufcount );
    for (i = 0; i < loaderbufcount; i++)
    PW( loaderbuf[i] );

    /* roll over all the overflows at once */
    if (sum & ~0777777)
    sum = (sum & 0777777) + (sum >> 18);
    if (sum & 01000000)         /* one more time */
    sum++;
    PW( sum );

    punchLeader(5);
    loaderbufcount = 0;
}

void punchLocObject( WORD32 loc, WORD32 val )
{
    if (!rim_mode) {
    if ((loc & LOADERBUFMASK) == 0 || /* full/force alignment */
        loaderbufcount > 0 &&
        loc != loaderbufstart + loaderbufcount) /* disjoint */
        flushLoader();
    if (loaderbufcount == 0)
        loaderbufstart = loc;
    loaderbuf[loaderbufcount++] = val;
    }
    else
    punchLocObjectRIM( loc, val );
}

/*  Function:  literal */
/*  Synopsis:  Add a value to the literal pool */
WORD32
literal( WORD32 value )
{
    int i;

    if (nconst >= MAX_CONSTANTS) {
    fprintf(stderr, "too many 'constants'; increase MAX_CONSTANTS\n");
    exit(1);
    }

    if (pass == 1) {
    if (++lit_count[nconst] == MAX_LITERALS) {
        fprintf(stderr, "too many literals; increase MAX_LITERALS\n");
        exit(1);
    }
    return lit_count[nconst];
    }

#if 1
    /*
     * pool constants; makes for a shorter tape
     * (but "middle" constants blocks can't shrink)
     */
    for (i = 0; i < nlit; i++)
    if (litter[i] == value)
        return lit_loc[nconst] + i;
#endif

    /* paranoia */
    if (nlit == MAX_LITERALS) {
    fprintf(stderr, "too many literals; increase MAX_LITERALS\n");
    exit(1);
    }

    /* not found, save it */
    litter[nlit] = value;

    /* use base for this block, determined on pass1 */
    return lit_loc[nconst] + nlit++;
} /* literal */


/*  Function:  printSymbolTable */
/*  Synopsis:  Output the symbol table. */
/* XXX now prints FIXED symbols too */
void printSymbolTable()
{
    int    ix;
    int    symbol_lines;
    SYM_T *sym;
    char mark;

    symbol_lines = 0;
    for (ix = 0, sym = symtab; ix < symbol_top; ix++, sym++) {
    if (M_FIXED(sym->type) || M_PSEUDO(sym->type) ||
        M_MACRO(sym->type) || M_EPSEUDO(sym->type))
        continue;

    if (symbol_lines == 0) {
        topOfForm( list_title, s_symtable );
        symbol_lines = LIST_LINES_PER_PAGE;
    }

    switch( sym->type & ( DEFINED | REDEFINED )) {
    case UNDEFINED:
        mark = '?';
        break;

    case REDEFINED:
        mark = '#';
        break;

    default:
        mark = ' ';
        break;
    }
    fprintf( listfile, "%c%-6.6s %6.6o\n", mark, sym->name, sym->val );
    symbol_lines--;
    }
} /* printSymbolTable */


/*  Function:  printPermanentSymbolTable */
/*  Synopsis:  Output the permanent symbol table to a file suitable for */
/*             being input after the EXPUNGE pseudo-op. */
void printPermanentSymbolTable()
{
    int     ix;
    FILE   *permfile;

    if(( permfile = fopen( permpathname, "w" )) == NULL )
    {
    exit( 2 );
    }

    fprintf( permfile, "/ PERMANENT SYMBOL TABLE\n/\n" );
    fprintf( permfile, "        expunge\n/\n" );

    for( ix = 0; ix < symbol_top; ix++ )
    {
    int type = symtab[ix].type;
    if( M_FIXED(type) && !M_PSEUDO(type) && !M_EPSEUDO(type) )
        fprintf( permfile, "\t%s=%o\n",
             symtab[ix].name, symtab[ix].val );
    }
    fclose( permfile );
} /* printPermanentSymbolTable */


/*  Function:  printCrossReference */
/*  Synopsis:  Output a cross reference (concordance) for the file being */
/*             assembled. */
void printCrossReference()
{
    int    ix;
    int    xc;
    int    xc_index;
    int    xc_refcount;
    int    xc_cols;
    SYM_T  *sym;

    /* Force top of form for first page. */
    page_lineno = LIST_LINES_PER_PAGE;

    list_lineno = 0;

    for( ix = 0, sym = symtab; ix < symbol_top; ix++, sym++ ) {
    if (M_FIXED(sym->type) && xreftab[sym->xref_index] == 0)
        continue;
    list_lineno++;
    page_lineno++;
    if( page_lineno >= LIST_LINES_PER_PAGE )
        topOfForm( list_title, s_xref );

    fprintf( listfile, "%5d", list_lineno );

    /* Get reference count & index into concordance table for this symbol */
    xc_refcount = sym->xref_count;
    xc_index = sym->xref_index;
    /* Determine how to label symbol on concordance. */
    /* XXX flag variables? */
    switch( sym->type & ( DEFINED | REDEFINED )) {
    case UNDEFINED:
        fprintf( listfile, " U         ");
        break;

    case REDEFINED:
        fprintf( listfile, " M  %5d  ", xreftab[xc_index] );
        break;

    default:
        fprintf( listfile, " A  %5d  ", xreftab[xc_index] );
        break;
    }
    fprintf( listfile, "%-6.6s  ", sym->name );

    /* Output the references, 8 numbers per line after symbol name. */
    for( xc_cols = 0, xc = 1; xc < xc_refcount + 1; xc++, xc_cols++ ) {
        if( xc_cols >= XREF_COLUMNS ) {
        xc_cols = 0;
        page_lineno++;
        if( page_lineno >= LIST_LINES_PER_PAGE )
            topOfForm( list_title, s_xref);
        list_lineno++;
        fprintf( listfile, "\n%5d%-19s", list_lineno, " " );
        }
        fprintf( listfile, "  %5d", xreftab[xc_index + xc] );
    }
    fprintf( listfile, "\n" );
    } /* for */
} /* printCrossReference */


/*  Function:  topOfForm */
/*  Synopsis:  Prints title and sub-title on top of next page of listing. */
void topOfForm( char *title, char *sub_title )
{
    char temp[10];

    list_pageno++;
    strcpy( temp, s_page );
    sprintf( temp, "%s %d", s_page, list_pageno );

    if (!listfile)
    return;

    /* Output a top of form if not the first page of the listing. */
    if( list_pageno > 1 )
    fprintf( listfile, "\f" );

    fprintf( listfile, "\n      %-63s %10s\n", title, temp );

    /* Reset the current page line counter. */
    page_lineno = 1;
    if( sub_title != NULL )
    {
    fprintf( listfile, "%80s\n", sub_title );
    page_lineno++;
    }
    else
    {
    fprintf( listfile, "\n" );
    page_lineno++;
    }
    fprintf( listfile, "\n" );
    page_lineno++;
} /* topOfForm */


/*  Function:  lexemeToName */
/*  Synopsis:  Convert the current lexeme into a string. */
char *lexemeToName( char *name, WORD32 from, WORD32 term )
{
    int to;

    to = 0;
    while( from < term && to < SYMLEN-1) {
    char c = line[from++];
    if (ISOVERBAR(c))
        continue;
    name[to++] = c;
    }
    name[to] = '\0';

    return( name );
} /* lexemeToName */

/*  Function:  defineLexeme */
/*  Synopsis:  Put lexeme into symbol table with a value. */
SYM_T *defineLexeme( WORD32  start,     /* start of lexeme being defined. */
             WORD32  term,  /* end+1 of lexeme being defined. */
                     WORD32  val,       /* value of lexeme being defined. */
                     SYMTYP  type )     /* how symbol is being defined. */
{
    char  name[SYMLEN];

    lexemeToName( name, start, term);
    return( defineSymbol( name, val, type, start ));
} /* defineLexeme */


/*  Function:  defineSymbol */
/*  Synopsis:  Define a symbol in the symbol table, enter symbol name if not */
/*             not already in table. */
SYM_T *defineSymbol( char *name, WORD32 val, SYMTYP type, WORD32 start )
{
    SYM_T  *sym;
    WORD32  xref_count;

    if( strlen( name ) < 1 )
    {
    return( &sym_undefined );       /* Protect against non-existent names. */
    }
    sym = lookup( name, type );
    xref_count = 0;         /* Set concordance for normal defintion. */

    if( M_DEFINED( sym->type ) && sym->val != val && M_NOTRDEF( sym -> type ))
    {
    if( pass == 2 )
    {
        errorSymbol( &redefined_symbol, sym->name, start );
        type = type | REDEFINED;
        sym->xref_count++;      /* Referenced symbol, count it. */
        xref_count = sym->xref_count;
        /* moved inside "if pass2" -plb 10/2/03 allow redefinition
         * of predefined symbols during pass1
         */
        return ( sym );
    }
    }

    if( pass == 2 && xref )
    {
    /* Put the definition line number in the concordance table. */
    /* Defined symbols are not counted as references. */
    if (sym->xref_index >= 0) { /* beware macro dummies */
        xreftab[sym->xref_index] = lineno;
        /* Put the line number in the concordance table. */
        xreftab[sym->xref_index + xref_count] = lineno;
    }
    }

    /* Now set the value and the type. */
    sym->val = val & 0777777;
    sym->type = type;
    return( sym );
} /* defineSymbol */


/*  Function:  lookup */
/*  Synopsis:  Find a symbol in table.  If not in table, enter symbol in */
/*             table as undefined.  Return address of symbol in table. */
SYM_T *lookup( char *name, int type )
{
    int ix;             /* Insertion index */
    int lx;             /* Left index */
    int rx;             /* Right index */
    SYM_T *best;            /* best match */
    SYM_T *sym;

    /* YIKES!  Search dummies (and "R") before anything else!! */
    if (curmacro && curmacro->defn) {
    struct macdef *mdp = curmacro->defn;
    int i;

    for (i = 0, sym = mdp->args; i <= mdp->nargs; i++, sym++)
        if (strcmp(name, sym->name) == 0)
        return sym;
    }

    lx = 0;
    rx = symbol_top - 1;
    best = NULL;
    while (lx <= rx) {
    int mx = (lx + rx) / 2;     /* Find center of search area. */
    int compare;

    sym = symtab + mx;

    compare = strcmp(name, sym->name);
    if (compare < 0)
        rx = mx - 1;
    else if (compare > 0)
        lx = mx + 1;
    else {              /* match */
        if (overbar && !M_DEFINED(sym->type) && pass == 2) {
        sym->type = DEFINED;
        sym->val = vars_addr++;
        nvars++;
        }
        return sym;         /* return exact match */
    } /* match */

    /* save best non-exact match; MACRO returns last defined n-x match! */
    if ((M_PSEUDO(sym->type)||M_EPSEUDO(sym->type)||M_MACRO(sym->type)) &&
        strncmp(name, sym->name, 3) == 0)
        best = sym;
    } /* while */

    /* return best match (pseudo or macro) if any for lookups (not defns) */
    if (best && type == UNDEFINED)
    return best;

    /* Must put symbol in table if index is negative. */
    ix = lx;                /* insertion point */
    if( symbol_top + 1 >= SYMBOL_TABLE_SIZE ) {
    errorSymbol( &symbol_table_full, name, lexstart );
    exit( 1 );
    }

    for( rx = symbol_top; rx >= ix; rx-- )
    symtab[rx + 1] = symtab[rx];

    symbol_top++;

    /* Enter the symbol as UNDEFINED with a value of zero. */
    sym = symtab + ix;
    strcpy( sym->name, name );
    sym->type = UNDEFINED;
    sym->val  = 0;
    sym->xref_count = 0;
    if( xref && pass == 2 && sym->xref_index >= 0)
    xreftab[sym->xref_index] = 0;

    if (overbar)
    nvars++;

    return sym;
} /* lookup */

/*  Function:  compareSymbols */
/*  Synopsis:  Used to presort the symbol table when starting assembler. */
int compareSymbols( const void *a, const void *b )
{
    return( strcmp( ((SYM_T *) a)->name, ((SYM_T *) b)->name ));
} /* compareSymbols */

/*  Function:  evalSymbol */
/*  Synopsis:  Get the pointer for the symbol table entry if exists. */
/*             If symbol doesn't exist, return a pointer to the undefined sym */
SYM_T *evalSymbol()
{
    char   name[SYMLEN];
    SYM_T *sym;

    sym = lookup( lexemeToName( name, lexstart, lexterm ), UNDEFINED);

    sym->xref_count++;          /* Count the number of references to symbol. */

    if( xref && pass == 2 && sym->xref_index >= 0)
    {
    /* Put the line number in the concordance table. */
    xreftab[sym->xref_index + sym->xref_count] = lineno;
    }

    return( sym );
} /* evalSymbol */


/*  Function:  moveToEndOfLine */
/*  Synopsis:  Move the parser input to the end of the current input line. */
void moveToEndOfLine()
{
    while( !ISEND( line[cc] )) cc++;    /* XXX wrong! will stop on a tab! */
    lexstart = cc;
    lexterm = cc;
    lexstartprev = lexstart;
} /* moveToEndOfLine */

/* frame the next token in "line" with lexstart and lexterm indicies */
void
next(int op) {
    char c;

    /* Save start column of previous lexeme for diagnostic messages. */
    lexstartprev = lexstart;
    lextermprev = lexterm;

    c = line[cc];
    if (c == ' ') {
    /* eat spaces */
    do {
        c = line[++cc];
    } while (c == ' ');
    if (op)             /* looking for operators? */
        cc--;           /* return one */
    }

    overbar = 0;
    lexstart = cc;
    c = line[cc];
    if( isalnum(c) || ISOVERBAR(c)) {
    if (ISOVERBAR(c))
        overbar = 1;
    do {
        c = line[++cc];
        if (ISOVERBAR(c))
        overbar = 1;
    } while (isalnum(c) || ISOVERBAR(c));
    }
    else if(!ISDONE(c) || c == '\t')    /* not end of line, or comment */
    cc++;               /* advance past all punctuation */
    lexterm = cc;
} /* next */

BOOL isLexSymbol()
{
    int ix;

    /* XXX alpha within first 4? 3?? */
    for( ix = lexstart; ix < lexterm; ix++ )
    if(isalpha(line[ix]))
        return TRUE;            /* any position will do! */
    return FALSE;
} /* isLexSymbol */

/*
 * from macro manual (F-36BP), p.18;
 *
 * "A macro-instruction definition consists of four parts;
 * the pseudo-instruction _define_, the _macro instruction name_
 * amd _dummy symbol list,_ the _body_, and the pseudo-instruction
 * _terminate_.  Each part is followed by at least one tabulation or
 * carriage return."
 *
 * and in the next paragraph;
 *
 * "The name is terminated by a _space_ or by a _tab_ or _cr_
 * if there is no dummy symbol list."
 *
 * This accepts tabs and/or a newline after define
 * (but will accept a space), and only accepts spaces
 * between macro and dummy names.
 */

void
defineMacro() {
    int lexstartsave;           /* point to macro name */
    int index;              /* point to error char */
    int error;              /* error boolean */
    int i;
    int count;
    WORD32  length;
    WORD32  value;
    char    termin[SYMLEN];
    char    args[MAC_MAX_ARGS][SYMLEN]; /* macro & arg names */
    char    body[MAC_MAX_LENGTH + 1];
    struct macdef *mdp;
    SYM_T *sym;

    if (nrepeats) {
    /* we can call readLine, so throw up hands now */
    errorLexeme( &define_in_repeat, lexstartprev );
    return;
    }

    while (line[lexstart] == ' ' || line[lexstart] == '\t')
    next(0);

    /* not a tab or space */
    if (ISEND(line[lexstart])) {    /* newline or EOS? */
    /* crock; next token should invisibly skip over line boundaries? */
    readLine();
    next(0);
    while (line[lexstart] == ' ' || line[lexstart] == '\t')
        next(0);
    }

    /* XXX pick up macro name out here */

    count = 0;
    index = 0;
    error = FALSE;
    lexstartsave = lexstart;
    while (!ISDONE(line[lexstart]) && count < MAC_MAX_ARGS) {
    if (!isalnum(line[lexstart]) && index == 0)
        index = lexstart;       /* error pointer */
    lexemeToName( args[count++], lexstart, lexterm );
    /* XXX error if NOT a comma (& not first dummy) ? */
    if (line[lexterm] == ',')
        next(0);            /* eat the comma */
    next(0);
    if (line[lexstart] == ' ')
        next(0);
    }
    if( count == 0 ) {          /* No macro name. */
    errorMessage( &no_macro_name, lexstartsave );
    error = TRUE;
    }
    else if( index ) {          /* Bad argument name. */
    errorMessage( &bad_dummy_arg, index );
    error = TRUE;
    }
    else if( mac_count >= MAC_TABLE_LENGTH ) {
    errorMessage( &macro_table_full, lexstartsave );
    error = TRUE;
    }
    else {
    value = mac_count++;        /* sym value is index into mac */
    defineSymbol( args[0], value, MACRO, lexstartsave );
    }

    for( length = 0;; ) {
    readLine();
    if (end_of_input)
        break;
    next(0);
    while (line[lexstart] == ' ' || line[lexstart] == '\t')
        next(0);

    lexemeToName( termin, lexstart, lexterm ); /* just look at line? */
    if (strncmp( termin, "term", 4 ) == 0)
        break;

    if (!error) {
        int ll = strlen(line);
        int allblank = FALSE;

        /* don't save blank lines! */
        for( i = 0; i < ll && allblank; i++ )
        if(!ISBLANK(line[i]))
            allblank = FALSE;

        if (allblank)           /* nothing but air? */
        continue;           /* skip it! */

        if ((length + ll + 1) >= MAC_MAX_LENGTH ) {
        errorMessage (&macro_too_long, lexstart );
        error = TRUE;
        continue;
        }

        strcpy(body+length, line);
        length += ll;
    }
    } /* for */
    if( error )
    return;

    mdp = calloc(1, sizeof(struct macdef) + length);
    if (mdp == NULL) {
    fprintf(stderr, "error allocating memory for macro definition\n");
    exit(1);
    }
    mac_defs[value] = mdp;

    strncpy(mdp->body, body, length);
    mdp->body[length] = '\0';
    mdp->nargs = count - 1;

    /*
     * save dummy names
     * symbol slot 0 reserved for "r" symbol
     * move SYM_T entries to macinv to allow recursion
     */
    sym = mdp->args;
    sym->type = DEFINED;
    strcpy(sym->name, "R");
    sym->val = 0;
    sym->xref_index = -1;       /* ??? allow xref? */
    sym++;

    for (i = 1; i <= mdp->nargs; i++, sym++) {
    sym->type = DEFINED;
    strcpy(sym->name, args[i]);
    sym->val = 0;
    sym->xref_index = -1;       /* don't xref!! */
    }
} /* defineMacro */

/* VARIABLES pseudo-op */
void
variables() {
    /* XXX error if "variables" already seen (in this pass) */
    /* XXX error if different address on pass 2 */
    if (pass == 2)
    printLine( line, clc, 0, LINE_LOC );
    vars_addr = clc;
    vars_end = clc = (clc + nvars) & ADDRESS_FIELD;
    if (pass == 2)
    printLine( line, clc, 0, LINE_LOC);
}

/* TEXT pseudo-op */
void
text(void)
{
    char delim;
    WORD32 w;
    int count;
    int ccase;
    /* XXX error in repeat!! */
    do {
    if (cc == maxcc) {
        /* XXX EOL before delim found!!! */
        fprintf(stderr, "FIX ME!\n");
        return;
    }
    delim = line[cc++];
    } while (delim == ' ');     /* others? NL */

    w = count = 0;
    ccase = LC;
    for (;;) {
    int c = nextfiodec(&ccase, delim);
    if (c == -1)
        break;
    w |= c << ((2-count)*6);
    if (++count == 3) {
        punchOutObject(clc, w); /* punch it! */
        incrementClc();
        count = w = 0;
    }
    }
    if (count > 0) {
    punchOutObject(clc, w);     /* punch remainder */
    incrementClc();
    }
}

/* CONSTANTS pseudo-op */
void
constants(void) {
    int i;

    /* XXX illegal inside macro (curmacro != NULL) */

    if (pass == 1) {
    lit_loc[nconst] = clc;

    /* just use addition?! */
    for (i = 0; i < lit_count[nconst]; i++)
        incrementClc();

    nconst++;
    return;
    }

    /* pass 2: */
    /* XXX complain if clc != lit_base[nconst]? */

    for (i = 0; i < lit_count[nconst]; i++) {
    if (i < nlit)
        punchOutObject( clc, litter[i] & 0777777); /* punch it! */
    incrementClc();
    }

    nconst++;
    nlit = 0;               /* litter[] now empty */
} /* constants */


/* process pseudo-ops
 * return FALSE if line scan should end (no longer used)
 */
BOOL pseudo( PSEUDO_T val )
{
    int count;
    int repeatstart;

    switch( (PSEUDO_T) val ) {
    case CONSTANTS:
    next(0);            /* Skip symbol */
    constants();
    break;

    case VARIABLES:
    next(0);            /* Skip symbol */
    variables();
    break;

    case DEFINE:
    next(0);            /* Skip symbol */
    defineMacro();
    return FALSE;
    break;

    case REPEAT:
    next(0);            /* Skip symbol */

    /* NOTE!! constant followed by SPACE picked up as expression!! */
    count = getExprs() & ADDRESS_FIELD;
    /* XXX error if sign bit set? */

    /* allow comma, but do not require */
    if( line[lexstart] == ',')
        next(0);

    nrepeats++;
    repeatstart = lexstart;     /* save line start */
    while (count-- > 0) {
        cc = repeatstart;       /* reset input pointer */
        processLine();      /* recurse! */
    }
    cc = maxcc;
    nrepeats--;

    return FALSE;
    break;

    case START:
    next(0);            /* Skip symbol */
    /* XXX illegal in macro or repeat */
    flushLoader();
    if (!ISDONE(line[lexstart])) {
        if (line[lexstart] == ' ')
        next(0);
        start_addr = getExprs() & ADDRESS_FIELD;
        next(0);
        printLine( line, 0, start_addr, LINE_VAL );
        /* MACRO punches 4" of leader */
        punchTriplet(JMP | start_addr);
        /* MACRO punches 24" of leader? */
    }
    /*
     * handle multiple tapes concatenated into one file!!
     * have command line option?? treat "start" as EOF??
     */
    list_title_set = FALSE;
    return FALSE;

    case TEXT:
    /* NOTE!! no next()! */
    text();
    break;

    case NOINPUT:
    next(0);            /* Skip symbol */
    noinput = TRUE;
    break;

    case EXPUNGE:
    next(0);            /* Skip symbol */
    if (pass == 1)
        init_symtab();
    break;

    default:
    break;
    } /* end switch for pseudo-ops */
    return TRUE;            /* keep scanning */
} /* pseudo */


/*  Function:  errorLexeme */
/*  Synopsis:  Display an error message using the current lexical element. */
void errorLexeme( EMSG_T *mesg, WORD32 col )
{
  char   name[SYMLEN];

  errorSymbol( mesg, lexemeToName( name, lexstart, lexterm ), col );
} /* errorLexeme */


/*  Function:  errorSymbol */
/*  Synopsis:  Display an error message with a given string. */
void errorSymbol( EMSG_T *mesg, char *name, WORD32 col )
{
  char   linecol[12];
  char  *s;

  if( pass == 2 )
  {
    s = ( name == NULL ) ? "" : name ;
    errors++;
    sprintf( linecol, ":%d:%d", lineno, col + 1 );
    fprintf( errorfile, "%s%-9s : error:  %s \"%s\" at Loc = %5.5o\n",
        filename, linecol, mesg->file, s, clc );
    saveError( mesg->list, col );
  }
  error_in_line = TRUE;
} /* errorSymbol */


/*  Function:  errorMessage */
/*  Synopsis:  Display an error message without a name argument. */
void errorMessage( EMSG_T *mesg, WORD32 col )
{
  char   linecol[12];

  if( pass == 2 )
  {
    errors++;
    sprintf( linecol, ":%d:%d", lineno, col + 1 );
    fprintf( errorfile, "%s%-9s : error:  %s at Loc = %5.5o\n",
        filename, linecol, mesg->file, clc );
    saveError( mesg->list, col );
  }
  error_in_line = TRUE;
} /* errorMessage */

/*  Function:  saveError */
/*  Synopsis:  Save the current error in a list so it may displayed after the */
/*             the current line is printed. */
void saveError( char *mesg, WORD32 col )
{
  if( save_error_count < DIM( error_list ))
  {
    error_list[save_error_count].mesg = mesg;
    error_list[save_error_count].col = col;
    save_error_count++;
  }
  error_in_line = TRUE;

  if( listed )
    printErrorMessages();
} /* saveError */

/* create a "symbol punch" for DDT */
/* MUST be called after object file closed; we reuse the FILE*! */

void
dump_symbols(void) {
    int ix;
    WORD32 addr;

    objectfile = fopen( sympathname, "wb" );
    if (!objectfile) {
    perror(sympathname);
    return;
    }

    punchLeader(0);
    punchLoader();
    punchLeader(5);

    /* XXX fudge addr -- get count, and subtract 2N from 07750? */
    addr = 05000;

    for( ix = 0; ix < symbol_top; ix++ ) {
    int i, type;
    WORD32 name;

    type = symtab[ix].type;
    if (M_FIXED(type) || M_PSEUDO(type) || M_MACRO(type))
        continue;

    name = 0;
    for (i = 0; i < 3; i++) {
        char c;

        c = symtab[ix].name[i];
        /* XXX leave on NUL? */

        c = ascii_to_fiodec[tolower(c) & 0177];
        /* XXX check for BAD entries? */

        /* XXX OR in val<<(3-i)*6?? */
        name <<= 6;
        name |= c & CHARBITS;
    }
    punchLocObject(addr++, permute(name));
    punchLocObject(addr++, symtab[ix].val);
    }
    flushLoader();
    punchTriplet( JMP );        /* ??? */
    punchLeader(0);
    fclose(objectfile);
}
