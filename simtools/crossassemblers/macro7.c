/******************************************************************************/
/*                                                                            */
/* Program:  MACRO7                                                          */
/* File:     macro7.c                                                        */
/* Author:   Gary A. Messenbrink <gary@netcom.com>                            */
/* MACRO7 modifications: Bob Supnik <bob.supnik@ljo.dec.com                  */
/*                                                                            */
/* Purpose:  A 2 pass PDP-7 assembler.                                 */
/*                                                                            */
/* NAME                                                                       */
/*    macro8x - a PDP-7 assembler.                               */
/*                                                                            */
/* SYNOPSIS:                                                                  */
/*    macro7 [ -d -p -m -r -x ] inputfile inputfile...                        */
/*                                                                            */
/* DESCRIPTION                                                                */
/*    This is a cross-assembler to for PDP-7 assembly language programs.      */
/*    It will produce an output file in rim format only. */
/*    A listing file is always produced and with an optional symbol table     */
/*    and/or a symbol cross-reference (concordance).  The permanent symbol    */
/*    table can be output in a form that may be read back in so a customized  */
/*    permanent symbol table can be produced.  Any detected errors are output */
/*    to a separate file giving the filename in which they were detected      */
/*    along with the line number, column number and error message as well as  */
/*    marking the error in the listing file.                                  */
/*    The following file name extensions are used:                            */
/*       .7      source code (input)                                          */
/*       .lst    assembly listing (output)                                    */
/*       .rim    assembly output in DEC's rim format (output)                 */
/*       .err    assembly errors detected (if any) (output)                   */
/*       .prm    permanent symbol table in form suitable for reading after    */
/*               the EXPUNGE pseudo-op.                                       */
/*                                                                            */
/* OPTIONS                                                                    */
/*    -d   Dump the symbol table at end of assembly                           */
/*    -p   Generate a file with the permanent symbols in it.                  */
/*         (To get the current symbol table, assemble a file than has only    */
/*          START in it.)                                                     */
/*    -x   Generate a cross-reference (concordance) of user symbols.          */
/*                                                                            */
/* DIAGNOSTICS                                                                */
/*    Assembler error diagnostics are output to an error file and inserted    */
/*    in the listing file.  Each line in the error file has the form          */
/*                                                                            */
/*       <filename>(<line>:<col>) : error:  <message> at Loc = <loc>          */
/*                                                                            */
/*    An example error message is:                                            */
/*                                                                            */
/*       bintst.7(17:9) : error:  undefined symbol "UNDEF" at Loc = 07616     */
/*                                                                            */
/*    The error diagnostics put in the listing start with a two character     */
/*    error code (if appropriate) and a short message.  A carat '^' is        */
/*    placed under the item in error if appropriate.                          */
/*    An example error message is:                                            */
/*                                                                            */
/*          17 07616 3000          DAC     UNDEF                              */
/*       UD undefined                      ^                                  */
/*          18 07617 1777          TAD  I  DUMMY                              */
/*                                                                            */
/*    When an indirect is generated, an at character '@' is placed after the  */
/*    the instruction value in the listing as an indicator as follows:        */
/*                                                                            */
/*          14 03716 1777@         TAD     OFFPAG                             */
/*                                                                            */
/*    Undefined symbols are marked in the symbol table listing by prepending  */
/*    a '?' to the symbol.  Redefined symbols are marked in the symbol table  */
/*    listing by prepending a '#' to the symbol.  Examples are:               */
/*                                                                            */
/*       #REDEF   04567                                                       */
/*        SWITCH  07612                                                       */
/*       ?UNDEF   00000                                                       */
/*                                                                            */
/*    Refer to the code for the diagnostic messages generated.                */
/*                                                                            */
/* REFERENCES:                                                                */
/*    This assembler is based on the pal assember by:                         */
/*       Douglas Jones <jones@cs.uiowa.edu> and                               */
/*       Rich Coon <coon@convexw.convex.com>                                  */
/*                                                                            */
/* COPYRIGHT NOTICE:                                                          */
/*    This is free software.  There is no fee for using it.  You may make     */
/*    any changes that you wish and also give it away.  If you can make       */
/*    a commercial product out of it, fine, but do not put any limits on      */
/*    the purchaser's right to do the same.  If you improve it or fix any     */
/*    bugs, it would be nice if you told me and offered me a copy of the      */
/*    new version.                                                            */
/*                                                                            */
/*                                                                            */
/* Amendments Record:                                                         */
/*  Version  Date    by   Comments                                            */
/*  ------- -------  ---  --------------------------------------------------- */
/*    v1.0  12Apr96  GAM  Original                                            */
/*    v1.1  18Nov96  GAM  Permanent symbol table initialization error.        */
/*    v1.2  20Nov96  GAM  Added BINPUNch and RIMPUNch pseudo-operators.       */
/*    v1.3  24Nov96  GAM  Added DUBL pseudo-op (24 bit integer constants).    */
/*    v1.4  29Nov96  GAM  Fixed bug in checksum generation.                   */
/*    v2.1  08Dec96  GAM  Added concordance processing (cross reference).     */
/*    v2.2  10Dec96  GAM  Added FLTG psuedo-op (floating point constants).    */
/*    v2.3   2Feb97  GAM  Fixed paging problem in cross reference output.     */
/*    v3.0  14Feb97  RMS  MACRO8X features.                                   */
/*                                                                            */
/******************************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINELEN              96
#define LIST_LINES_PER_PAGE  60         /* Includes 3 line page header.       */
#define NAMELEN             128
#define SYMBOL_COLUMNS        5
#define SYMLEN                7
#define SYMBOL_TABLE_SIZE  8192
#define MAC_MAX_ARGS         20         /* Must be < 26                       */
#define MAC_MAX_LENGTH     8192
#define MAC_TABLE_LENGTH   1024         /* Must be <= 4096.                   */
#define TITLELEN             63
#define XREF_COLUMNS          8

#define ADDRESS_FIELD  0017777
#define LIT_BASE       0017400
#define INDIRECT_BIT   0020000
#define LAST_PAGE_LOC  0017777
#define OP_CODE        0740000

/* Macro to get the number of elements in an array.                           */
#define DIM(a) (sizeof(a)/sizeof(a[0]))

/* Macro to get the address plus one of the end of an array.                  */
#define BEYOND(a) ((a) + DIM(A))

#define is_blank(c) ((c==' ') || (c=='\f') || (c=='>'))
#define isend(c)   ((c=='\0')|| (c=='\n'))
#define isdone(c)  ((c=='/') || (isend(c)) || (c=='\t'))

/* Macros for testing symbol attributes.  Each macro evaluates to non-zero    */
/* (true) if the stated condtion is met.                                      */
/* Use these to test attributes.  The proper bits are extracted and then      */
/* tested.                                                                    */
#define M_CONDITIONAL(s) ((s & CONDITION) == CONDITION)
#define M_DEFINED(s)     ((s & DEFINED) == DEFINED)
#define M_DUPLICATE(s)   ((s & DUPLICATE) == DUPLICATE)
#define M_FIXED(s)       ((s & FIXED) == FIXED)
#define M_LABEL(s)       ((s & LABEL) == LABEL)
#define M_MRI(s)         ((s & MRI) == MRI)
#define M_MRIFIX(s)      ((s & MRIFIX) == MRIFIX)
#define M_PSEUDO(s)      ((s & PSEUDO) == PSEUDO)
#define M_REDEFINED(s)   ((s & REDEFINED) == REDEFINED)
#define M_MACRO(s)       ((s & MACRO) == MACRO)
#define M_UNDEFINED(s)   (!M_DEFINED(s))
#define M_NOTRDEF(s)     ((s & NOTRDEF) != 0)

/* This macro is used to test symbols by the conditional assembly pseudo-ops. */
#define M_DEF(s) (M_DEFINED(s))
#define M_COND(s) (M_DEFINED(s))
#define M_DEFINED_CONDITIONALLY(t) ((M_DEF(t)&&pass==1)||(!M_COND(t)&&pass==2))

typedef unsigned char BOOL;
typedef unsigned char BYTE;
typedef          int  WORD32;

#ifndef FALSE
  #define FALSE 0
  #define TRUE (!FALSE)
#endif

/* Line listing styles.  Used to control listing of lines.                    */
enum linestyle_t
{
  LINE, LINE_VAL, LINE_LOC_VAL, LOC_VAL
};
typedef enum linestyle_t LINESTYLE_T;

/* Symbol Types.                                                              */  
/* Note that the names that have FIX as the suffix contain the FIXED bit      */
/* included in the value.                                                     */
/*                                                                            */
/* The CONDITION bit is used when processing the conditional assembly PSEUDO- */
/* OPs (e.g., IFDEF).  During pass 1 of the assembly, the symbol is either    */
/* defined or undefined.  The condition bit is set when the symbol is defined */
/* during pass 1 and reset on pass 2 at the location the symbol was defined   */
/* during pass 1.  When processing conditionals during pass 2, if the symbol  */
/* is defined and the condition bit is set, the symbol is treated as if it    */
/* were undefined.  This gives consistent behavior of the conditional         */
/* pseudo-ops during both pass 1 and pass 2.                                  */
enum symtyp
{
  UNDEFINED = 0000,
  DEFINED   = 0001,
  FIXED     = 0002,
  MRI       = 0004    | DEFINED,
  LABEL     = 0010    | DEFINED,
  REDEFINED = 0020    | DEFINED,
  DUPLICATE = 0040    | DEFINED,
  PSEUDO    = 0100    | FIXED | DEFINED,
  CONDITION = 0200    | DEFINED,
  MACRO     = 0400    | DEFINED,
  MRIFIX    = MRI     | FIXED | DEFINED,
  DEFFIX    = DEFINED | FIXED,
  NOTRDEF   = (MACRO | PSEUDO | LABEL | MRI | FIXED) & ~DEFINED
};
typedef enum symtyp SYMTYP;

enum pseudo_t
{
  DECIMAL, DEFINE,   EJECT,   IFDEF,   IFNDEF,   IFNZERO, IFZERO,
  LIST,    NOLIST,   OCTAL,   START,   TEXT,     TITLE,   VFD
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

struct lpool_t
{
  WORD32  error;                /* True if error message has been printed.    */
  WORD32  pool[LIT_BASE];
};
typedef struct lpool_t LPOOL_T;

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

/* Function Prototypes                                                        */

int     binarySearch( char *name, int start, int symbol_count );
int     copyMacLine( int length, int from, int term, int nargs );
int     compareSymbols( const void *a, const void *b );
void    conditionFalse( void );
void    conditionTrue( void );
SYM_T  *defineLexeme( WORD32 start, WORD32 term, WORD32 val, SYMTYP type );
SYM_T  *defineSymbol( char *name, WORD32 val, SYMTYP type, WORD32 start);
void    endOfBinary( void );
void    errorLexeme( EMSG_T *mesg, WORD32 col );
void    errorMessage( EMSG_T *mesg, WORD32 col );
void    errorSymbol( EMSG_T *mesg, char *name, WORD32 col );
SYM_T  *eval( void );
SYM_T  *evalSymbol( void );
void    getArgs( int argc, char *argv[] );
SYM_T  *getExpr( void );
WORD32  getExprs( void );
WORD32  incrementClc( void );
WORD32  insertLiteral( LPOOL_T *pool, WORD32 pool_page, WORD32 value );
char   *lexemeToName( char *name, WORD32 from, WORD32 term );
void    listLine( void );
SYM_T  *lookup( char *name );
void    moveToEndOfLine( void );
void    nextLexBlank( void );
void    nextLexeme( void );
void    onePass( void );
void    printCrossReference( void );
void    printErrorMessages( void );
void    printLine(char *line, WORD32 loc, WORD32 val, LINESTYLE_T linestyle);
void    printPageBreak( void );
void    printPermanentSymbolTable( void );
void    printSymbolTable( void );
BOOL    pseudoOperators( PSEUDO_T val );
void    punchLocObject( WORD32 loc, WORD32 val );
void    punchLiteralPool( LPOOL_T *p, WORD32 lpool_page );
void    punchOutObject( WORD32 loc, WORD32 val );
void    punchLeader( WORD32 count );
void    punchObject( WORD32 val );
void	punchTriplet( WORD32 val );
void    readLine( void );
void    saveError( char *mesg, WORD32 cc );
BOOL    testForLiteralCollision( WORD32 loc );
void    topOfForm( char *title, char *sub_title );

/*----------------------------------------------------------------------------*/

/* Table of pseudo-ops (directives) which are used to setup the symbol        */
/* table on startup and when the EXPUNGE pseudo-op is executed.               */
SYM_T pseudo[] =
{
  { PSEUDO, "DECIMA", DECIMAL },    /* Read literal constants in base 10.     */
  { PSEUDO, "DEFINE", DEFINE  },    /* Define macro.                          */
  { PSEUDO, "tEJECT", EJECT   },    /* Eject a page in the listing. DISABLED  */
  { PSEUDO, "IFDEF",  IFDEF   },    /* Assemble if symbol is defined.         */
  { PSEUDO, "IFNDEF", IFNDEF  },    /* Assemble if symbol is not defined.     */
  { PSEUDO, "IFNZER", IFNZERO },    /* Assemble if symbol value is not 0.     */
  { PSEUDO, "IFZERO", IFZERO  },    /* Assemble if symbol value is 0.         */
  { PSEUDO, "LIST",   LIST    },    /* Enable listing.                        */
  { PSEUDO, "NOLIST", NOLIST  },    /* Disable listing.                       */
  { PSEUDO, "OCTAL",  OCTAL   },    /* Read literal constants in base 8.      */
  { PSEUDO, "START",  START   },    /* Set starting address.                  */
  { PSEUDO, "TEXT",   TEXT    },    /* Pack 6 bit trimmed ASCII into memory.  */
  { PSEUDO, "TITLE",  TITLE   },    /* Use the text string as a listing title.*/
  { PSEUDO, "VFD",    VFD     },    /* Variable field definition.             */
};

/* Symbol Table                                                               */
/* The table is put in lexical order on startup, so symbols can be            */
/* inserted as desired into the initial table.                                */
#define DAC 0040000
#define JMP 0600000
SYM_T permanent_symbols[] =
{
  /* Memory Reference Instructions                                            */
  { MRIFIX, "CAL",    0000000 },
  { MRIFIX, "DAC",    0040000 },
  { MRIFIX, "JMS",    0100000 },
  { MRIFIX, "DZM",    0140000 },
  { MRIFIX, "LAC",    0200000 },
  { MRIFIX, "XOR",    0240000 },
  { MRIFIX, "ADD",    0300000 },
  { MRIFIX, "TAD",    0340000 },
  { MRIFIX, "XCT",    0400000 },
  { MRIFIX, "ISZ",    0440000 },
  { MRIFIX, "AND",    0500000 },
  { MRIFIX, "SAD",    0540000 },
  { MRIFIX, "JMP",    0600000 },
  { MRIFIX, "I",      0020000 },
  { DEFFIX, "EAE",    0640000 },
  { DEFFIX, "IOT",    0700000 },
  { DEFFIX, "OPR",    0740000 },
  { DEFFIX, "LAW",    0760000 },
  { DEFFIX, "LAM",    0777777 },
  /* EAE Microinstructions */
  { DEFFIX, "OSC",    0640001 },
  { DEFFIX, "LACS",   0641001 },
  { DEFFIX, "OMQ",    0640002 },
  { DEFFIX, "LACQ",   0641002 },
  { DEFFIX, "CMQ",    0640004 },
  { DEFFIX, "CLQ",    0650000 },
  { DEFFIX, "LMQ",    0652000 },
  { DEFFIX, "ABS",    0644000 },
  { DEFFIX, "GSM",    0664000 },
  { DEFFIX, "MUL",    0653122 },
  { DEFFIX, "MULS",   0657122 },
  { DEFFIX, "DIV",    0640323 },
  { DEFFIX, "DIVS",   0644323 },
  { DEFFIX, "IDIV",   0653323 },
  { DEFFIX, "IDIVS",  0657323 },
  { DEFFIX, "FRDIV",  0650323 },
  { DEFFIX, "FRDIVS", 0654323 },
  { DEFFIX, "NORM",   0640444 },
  { DEFFIX, "NORMS",  0660444 },
  { DEFFIX, "LRS",    0640500 },
  { DEFFIX, "LRSS",   0660500 },
  { DEFFIX, "LLS",    0640600 },
  { DEFFIX, "LLSS",   0660600 },
  { DEFFIX, "ALS",    0640700 },
  { DEFFIX, "ALSS",   0660700 },
  /* Operate Microinstructions */
  { DEFFIX, "NOP",    0740000 },
  { DEFFIX, "CMA",    0740001 },
  { DEFFIX, "CML",    0740002 },
  { DEFFIX, "OAS",    0740004 },
  { DEFFIX, "LAS",    0750004 },
  { DEFFIX, "RAL",    0740010 },
  { DEFFIX, "RCL",    0744010 },
  { DEFFIX, "RTL",    0742010 },
  { DEFFIX, "RAR",    0740020 },
  { DEFFIX, "RCR",    0744020 },
  { DEFFIX, "RTR",    0742020 },
  { DEFFIX, "HLT",    0740040 },
  { DEFFIX, "XX",     0740040 },
  { DEFFIX, "SMA",    0740100 },
  { DEFFIX, "SZA",    0740200 },
  { DEFFIX, "SNL",    0740400 },
  { DEFFIX, "SKP",    0741000 },
  { DEFFIX, "SPA",    0741100 },
  { DEFFIX, "SNA",    0741200 },
  { DEFFIX, "SZL",    0741400 },
  { DEFFIX, "CLL",    0744000 },
  { DEFFIX, "STL",    0744002 },
  { DEFFIX, "CLA",    0750000 },
  { DEFFIX, "CLC",    0750001 },
  { DEFFIX, "GLK",    0750010 },
  /* CPU IOT's */
  { DEFFIX, "CLSF",   0700001 },
  { DEFFIX, "IOF",    0700002 },
  { DEFFIX, "ION",    0700042 },
  { DEFFIX, "ITON",   0700062 },
  { DEFFIX, "CLOF",   0700004 },
  { DEFFIX, "CLON",   0700044 },
  { DEFFIX, "TTS",    0703301 },
  { DEFFIX, "SKP7",   0703341 },
  { DEFFIX, "CAF",    0703302 },
  { DEFFIX, "SEM",    0707701 },
  { DEFFIX, "EEM",    0707702 },
  { DEFFIX, "EMIR",   0707742 },
  { DEFFIX, "LEM",    0707704 },
  /* High Speed Paper Tape Reader */
  { DEFFIX, "RSF",    0700101 },
  { DEFFIX, "RRB",    0700112 },
  { DEFFIX, "RCF",    0700102 },
  { DEFFIX, "RSA",    0700104 },
  { DEFFIX, "RSB",    0700144 },
  /* High Speed Paper Tape Punch */
  { DEFFIX, "PSF",    0700201 },
  { DEFFIX, "PCF",    0700202 },
  { DEFFIX, "PSA",    0700204 },
  { DEFFIX, "PLS",    0700204 },
  { DEFFIX, "PSB",    0700244 },
  /* Keyboard */
  { DEFFIX, "KSF",    0700301 },
  { DEFFIX, "KRB",    0700312 },
  { DEFFIX, "IORS",   0700314 },
  /* Teleprinter */
  { DEFFIX, "TSF",    0700401 },
  { DEFFIX, "TCF",    0700402 },
  { DEFFIX, "TLS",    0700406 },
  /* Line Printer */
  { DEFINED, "LPSF",   0706501 },
  { DEFINED, "LPCB",   0706502 },
  { DEFINED, "LPB1",   0706566 },
  { DEFINED, "LPB2",   0706526 },
  { DEFINED, "LPB3",   0706546 },
  { DEFINED, "LPSE",   0706601 },
  { DEFINED, "LPCF",   0706602 },
  { DEFINED, "LPPB",   0706606 },
  { DEFINED, "LPLS",   0706626 },
  { DEFINED, "LPPS",   0706646 },
  /* Card Reader */
  { DEFFIX, "CRSF",   0706701 },
  { DEFFIX, "CRRB",   0706712 },
  { DEFFIX, "CRSA",   0706704 },
  { DEFFIX, "CRSB",   0706744 },
  /* DECtape */
  { DEFFIX, "MMDF",   0707501 },
  { DEFFIX, "MMEF",   0707541 },
  { DEFFIX, "MMRD",   0707512 },
  { DEFFIX, "MMWR",   0707504 },
  { DEFFIX, "MMBF",   0707601 },
  { DEFFIX, "MMRS",   0707612 },
  { DEFFIX, "MMLC",   0707604 },
  { DEFFIX, "MMSE",   0707644 },
};      /* End-of-Symbols for Permanent Symbol Table                          */

/* Global variables                                                           */
SYM_T *symtab;                  /* Symbol Table                               */
int    symbol_top;              /* Number of entries in symbol table.         */

SYM_T *fixed_symbols;           /* Start of the fixed symbol table entries.   */
int    number_of_fixed_symbols;

/*----------------------------------------------------------------------------*/

WORD32 *xreftab;                /* Start of the concordance table.            */

ERRSAVE_T error_list[20];
int     save_error_count;

LPOOL_T cp;                     /* Storage for current page constants.        */

char   s_detected[] = "detected";
char   s_error[]    = "error";
char   s_errors[]   = "errors";
char   s_no[]       = "No";
char   s_page[]     = "Page";
char   s_symtable[] = "Symbol Table";
char   s_xref[]     = "Cross Reference";

/* Assembler diagnostic messages.                                             */
/* Some attempt has been made to keep continuity with the PAL-III and         */
/* MACRO-8 diagnostic messages.  If a diagnostic indicator, (e.g., IC)        */
/* exists, then the indicator is put in the listing as the first two          */
/* characters of the diagnostic message.  The PAL-III indicators where used   */
/* when there was a choice between using MACRO-8 and PAL-III indicators.      */
/* The character pairs and their meanings are:                                */
/*      DT  Duplicate Tag (symbol)                                            */
/*      IC  Illegal Character                                                 */
/*      ID  Illegal Redefinition of a symbol.  An attempt was made to give    */
/*          a symbol a new value not via =.                                   */
/*      IE  Illegal Equals  An equal sign was used in the wrong context,      */
/*          (e.g., A+B=C, or TAD A+=B)                                        */
/*      II  Illegal Indirect  An off page reference was made, but a literal   */
/*          could not be generated because the indirect bit was already set.  */
/*      IR  Illegal Reference (address is not on current page or page zero)   */
/*      PE  Current, Non-Zero Page Exceeded (literal table flowed into code)  */
/*      RD  ReDefintion of a symbol                                           */
/*      ST  Symbol Table full                                                 */
/*      UA  Undefined Address (undefined symbol)                              */
/*      ZE  Zero Page Exceeded (see above, or out of space)                   */
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

/*----------------------------------------------------------------------------*/

FILE   *errorfile;
FILE   *infile;
FILE   *listfile;
FILE   *listsave;
FILE   *objectfile;
FILE   *objectsave;

char    errorpathname[NAMELEN];
char    filename[NAMELEN];
char    listpathname[NAMELEN];
char    objectpathname[NAMELEN];
char   *pathname;
char    permpathname[NAMELEN];

char    mac_buffer[MAC_MAX_LENGTH + 1];
char   *mac_bodies[MAC_TABLE_LENGTH];
char    mac_arg_name[MAC_MAX_ARGS][SYMLEN];
int     mac_arg_pos[26] = { 0 };

int     list_lineno;
int     list_pageno;
char    list_title[4*LINELEN];
BOOL    list_title_set;         /* Set if TITLE pseudo-op used.               */
char    line[4*LINELEN];        /* Input line.                                */
int     lineno;                 /* Current line number.                       */
char    mac_line[4*LINELEN];    /* Saved macro invocation line.               */
int     page_lineno;            /* print line number on current page.         */
WORD32  listed;                 /* Listed flag.                               */
WORD32  listedsave;

WORD32  cc;                     /* Column Counter (char position in line).    */
WORD32  checksum;               /* Generated checksum                         */
BOOL    binary_data_output;     /* Set true when data has been output.        */
WORD32  clc;                    /* Location counter                           */
char    delimiter;              /* Character immediately after eval'd term.   */
BOOL    end_of_input;           /* End of all input files.                    */
int     errors;                 /* Number of errors found so far.             */
BOOL    error_in_line;          /* TRUE if error on current line.             */
int     errors_pass_1;          /* Number of errors on pass 1.                */
int     filix_curr;             /* Index in argv to current input file.       */
int     filix_start;            /* Start of input files in argv.              */
BOOL    indirect_generated;     /* TRUE if an off page address generated.     */
WORD32  lexstartprev;           /* Where previous lexeme started.             */
WORD32  lextermprev;            /* Where previous lexeme ended.               */
WORD32  lexstart;               /* Index of current lexeme on line.           */
WORD32  lexterm;                /* Index of character after current lexeme.   */
WORD32  lit_loc;                /* Base of literal pool.                      */
WORD32  mac_cc;                 /* Saved cc after macro invocation.           */
WORD32  mac_count;              /* Total macros defined.                      */
char   *mac_ptr;                /* Pointer to macro body, NULL if no macro.   */
WORD32  maxcc;                  /* Current line length.                       */
BOOL    nomac_exp;              /* No macro expansions.                       */
WORD32  pass;                   /* Number of current pass.                    */
BOOL    print_permanent_symbols;
WORD32  radix;                  /* Default number radix.                      */
BOOL    rim_mode;               /* RIM mode output.                           */
int     save_argc;              /* Saved argc.                                */
char   **save_argv;             /* Saved *argv[].                             */
WORD32  start_addr;             /* Saved start address.                       */
BOOL    symtab_print;           /* Print symbol table flag                    */
BOOL    xref;

SYM_T   sym_eval = { DEFINED, "", 0 };       /* Value holder for eval()       */
SYM_T   sym_getexpr = { DEFINED, "", 0 };    /* Value holder for getexpr()    */
SYM_T   sym_undefined = { UNDEFINED, "", 0 };/* Symbol Table Terminator       */


/******************************************************************************/
/*                                                                            */
/*  Function:  main                                                           */
/*                                                                            */
/*  Synopsis:  Starting point.  Controls order of assembly.                   */
/*                                                                            */
/******************************************************************************/
int main( int argc, char *argv[] )
{
  int     ix;
  int     space;

  save_argc = argc;
  save_argv = argv;

  /* Set the default values for global symbols.                               */
  binary_data_output = FALSE;
  print_permanent_symbols = FALSE;
  nomac_exp = TRUE;
  rim_mode = TRUE;
  symtab_print = FALSE;
  xref = FALSE;
  pathname = NULL;
  for( ix = 0; ix < MAC_TABLE_LENGTH; ix++ )
  {
    mac_bodies[ix] = NULL;
  }

  /* Get the options and pathnames                                            */
  getArgs( argc, argv );

  /* Setup the error file in case symbol table overflows while installing the */
  /* permanent symbols.                                                       */
  errorfile = fopen( errorpathname, "w" );
  errors = 0;
  save_error_count = 0;
  pass = 0;             /* This is required for symbol table initialization.  */
  symtab = (SYM_T *) malloc( sizeof( SYM_T ) * SYMBOL_TABLE_SIZE );

  if( symtab == NULL )
  {
    fprintf( stderr, "Could not allocate memory for symbol table.\n");
    exit( -1 );
  }

  /* Place end marker in symbol table.                                        */
  symtab[0] = sym_undefined;
  symbol_top = 0;
  number_of_fixed_symbols = symbol_top;
  fixed_symbols = &symtab[symbol_top - 1];

  /* Enter the pseudo-ops into the symbol table                               */
  for( ix = 0; ix < DIM( pseudo ); ix++ )
  {
    defineSymbol( pseudo[ix].name, pseudo[ix].val, pseudo[ix].type, 0 );
  }

  /* Enter the predefined symbols into the table.                             */
  /* Also make them part of the permanent symbol table.                       */
  for( ix = 0; ix < DIM( permanent_symbols ); ix++ )
  {
    defineSymbol( permanent_symbols[ix].name,
                  permanent_symbols[ix].val,
                  permanent_symbols[ix].type, 0 );
  }

  number_of_fixed_symbols = symbol_top;
  fixed_symbols = &symtab[symbol_top - 1];

  /* Do pass one of the assembly                                              */
  checksum = 0;
  pass = 1;
  onePass();
  errors_pass_1 = errors;

  /* Set up for pass two                                                      */
  errorfile = fopen( errorpathname, "w" );
  objectfile = fopen( objectpathname, "wb" );
  objectsave = objectfile;

  listfile = fopen( listpathname, "w" );
  listsave = listfile;

  punchLeader( 0 );
  checksum = 0;

  /* Do pass two of the assembly                                              */
  errors = 0;
  save_error_count = 0;

  if( xref )
  {
    /* Get the amount of space that will be required for the concordance.     */
    for( space = 0, ix = 0; ix < symbol_top; ix++ )
    {
      symtab[ix].xref_index = space;    /* Index into concordance table.      */
      space += symtab[ix].xref_count + 1;
      symtab[ix].xref_count = 0;        /* Clear the count for pass 2.        */

    }
    /* Allocate the necessary space.                                          */
    xreftab = (WORD32 *) malloc( sizeof( WORD32 ) * space );

    /* Clear the cross reference space.                                       */
    for( ix = 0; ix < space; ix++ )
    {
      xreftab[ix] = 0;
    }
  }
  pass = 2;
  onePass();

  /* Undo effects of NOPUNCH for any following checksum                       */
  objectfile = objectsave;

  /* Works great for trailer.                                                 */
  punchLeader( 1 );

  /* undo effects of NOLIST for any following output to listing file.         */
  listfile = listsave;

  /* Display value of error counter.                                          */
  if( errors == 0 )
  {
    fprintf( listfile, "\n      %s %s %s\n", s_no, s_detected, s_errors );
  }
  else
  {
    fprintf( errorfile, "\n      %d %s %s\n", errors, s_detected,
                                        ( errors == 1 ? s_error : s_errors ));
    fprintf( listfile, "\n      %d %s %s\n", errors, s_detected,
                                        ( errors == 1 ? s_error : s_errors ));
    fprintf( stderr,   "      %d %s %s\n", errors, s_detected,
                                        ( errors == 1 ? s_error : s_errors ));
  }

  if( symtab_print )
  {
    printSymbolTable();
  }

  if( print_permanent_symbols )
  {
    printPermanentSymbolTable();
  }

  if( xref )
  {
    printCrossReference();
  }

  fclose( objectfile );
  fclose( listfile );
  fclose( errorfile );
  if( errors == 0 && errors_pass_1 == 0 )
  {
    remove( errorpathname );
  }

  return( errors != 0 );
} /* main()                                                                   */

/******************************************************************************/
/*                                                                            */
/*  Function:  getArgs                                                        */
/*                                                                            */
/*  Synopsis:  Parse command line, set flags accordingly and setup input and  */
/*             output files.                                                  */
/*                                                                            */
/******************************************************************************/
void getArgs( int argc, char *argv[] )
{
  WORD32  len;
  WORD32  ix, jx;

  /* Set the defaults                                                         */
  errorfile = NULL;
  infile = NULL;
  listfile = NULL;
  listsave = NULL;
  objectfile = NULL;
  objectsave = NULL;

  for( ix = 1; ix < argc; ix++ )
  {
    if( argv[ix][0] == '-' )
    {
      for( jx = 1; argv[ix][jx] != 0; jx++ )
      {
        switch( argv[ix][jx] )
        {
        case 'd':
          symtab_print = TRUE;
          break;

/*        case 'r':
          rim_mode = TRUE;
          break;
*/
        case 'm':
          nomac_exp = FALSE;
          break;

        case 'p':
          print_permanent_symbols = TRUE;
          break;

        case 'x':
          xref = TRUE;
          break;

        default:
          fprintf( stderr, "%s: unknown flag: %s\n", argv[0], argv[ix] );
          fprintf( stderr, " -d -- dump symbol table\n" );
          fprintf( stderr, " -m -- output macro expansions\n" );
/*          fprintf( stderr, " -r -- output rim format file\n" ); */
          fprintf( stderr, " -p -- output permanent symbols to file\n" );
          fprintf( stderr, " -x -- output cross reference to file\n" );
          fflush( stderr );
          exit( -1 );
        } /* end switch                                                       */
      } /* end for                                                            */
    }
    else
    {
      filix_start = ix;
      pathname = argv[ix];
      break;
    }
  } /* end for                                                                */

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

  /* Now make the pathnames                                                   */
  /* Find last '.', if it exists.                                             */
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

  /* Add the pathname extensions.                                             */
  strncpy( objectpathname, pathname, jx );
  objectpathname[jx] = '\0';
  strcat( objectpathname, rim_mode ? ".rim" : ".bin" );

  strncpy( listpathname, pathname, jx );
  listpathname[jx] = '\0';
  strcat( listpathname, ".lst" );

  strncpy( errorpathname, pathname, jx );
  errorpathname[jx] = '\0';
  strcat( errorpathname, ".err" );

  strncpy( permpathname, pathname, jx );
  permpathname[jx] = '\0';
  strcat( permpathname, ".prm" );

  /* Extract the filename from the path.                                      */
  if( isalpha( pathname[0] ) && pathname[1] == ':' && pathname[2] != '\\' )
  {
    pathname[1] = '\\';         /* MS-DOS style pathname                      */
  }

  jx = len - 1;
  while( pathname[jx] != '/' && pathname[jx] != '\\' && jx >= 0 )
  {
    jx--;
  }
  strcpy( filename, &pathname[jx + 1] );

} /* getArgs()                                                                */


/******************************************************************************/
/*                                                                            */
/*  Function:  onePass                                                        */
/*                                                                            */
/*  Synopsis:  Do one assembly pass.                                          */
/*                                                                            */
/******************************************************************************/
void onePass()
{
  BOOL    blanks;
  int     ix;
  int     jx;
  char    name[SYMLEN];
  WORD32  newclc;
  BOOL    scanning_line;
  WORD32  start;
  SYM_T  *sym;
  WORD32  term;
  WORD32  val;

  clc = 0100;                  /* Default starting address is 100 octal.     */
  start_addr = 0100;           /* No starting address.                       */
  lit_loc = LIT_BASE;          /* Literal pool base.                         */
  mac_count = 0;               /* No macros defined.                          */
  mac_ptr = NULL;              /* Not in a macro.                             */
  for( ix = 0; ix < MAC_TABLE_LENGTH; ix++)
  {
    if ( mac_bodies[ix] )
    {
      free( mac_bodies[ix] );
    }
  }
  cp.error = FALSE;
  listed = TRUE;
  lineno = 0;
  list_pageno = 0;
  list_lineno = 0;
  list_title_set = FALSE;
  page_lineno = LIST_LINES_PER_PAGE;    /* Force top of page for new titles.  */
  radix = 8;                    /* Initial radix is octal (base 8).           */

  /* Now open the first input file.                                           */
  end_of_input = FALSE;
  filix_curr = filix_start;     /* Initialize pointer to input files.         */
  if(( infile = fopen( save_argv[filix_curr], "r" )) == NULL )
  {
    fprintf( stderr, "%s: cannot open \"%s\"\n", save_argv[0],
      save_argv[filix_curr] );
    exit( -1 );
  }

  while( TRUE )
  {
    readLine();
    nextLexeme();

    scanning_line = TRUE;
    while( scanning_line )
    {
      if( end_of_input )
      {
        endOfBinary();
        fclose( infile );
        return;
      }
      if( isend( line[lexstart] ))
      {
        scanning_line = FALSE;
      }
      else
      {
        switch( line[lexstart] )
        {
        case '/':
          scanning_line = FALSE;
          break;

        case '\t':
          nextLexeme();
          break;

        default:
          for( jx = lexstart; jx < maxcc; jx++ )
          {
            if( is_blank( line[jx] ) || isdone( line[jx] )) break;
          }
          if( line[jx] == '/')
          {
            newclc = (getExpr())->val & 077777;
            /* Do not change Current Location Counter if an error occurred.   */
            if( !error_in_line )
            {
              clc = newclc;
            }
            printLine( line, 0, newclc, LINE_VAL );
            cc = jx + 1;
            nextLexeme();
            while( line[lexstart] == '\t' ) nextLexeme();
            break;
          }

          switch( line[lexterm] )
          {
          case ',':
            if( isalpha( line[lexstart] ))
            {
              /* Use lookup so symbol will not be counted as reference.       */
              sym = lookup( lexemeToName( name, lexstart, lexterm ));
              if( M_DEFINED( sym->type ))
              {
                if( sym->val != clc && pass == 2 )
                {
                  errorSymbol( &duplicate_label, sym->name, lexstart );
                }
                sym->type = sym->type | DUPLICATE;
              }
              /* Must call define on pass 2 to generate concordance.          */
              defineLexeme( lexstart, lexterm, clc, LABEL );
            }
            else
            {
              errorLexeme( &label_syntax, lexstart );
            }
            nextLexeme();           /* skip label                             */
            nextLexeme();           /* skip comma                             */
            while( line[lexstart] == '\t' ) nextLexeme();
            break;

          case '=':
            if( isalpha( line[lexstart] ))
            {
              start = lexstart;
              term = lexterm;
              delimiter = line[lexterm];
              nextLexBlank();       /* skip symbol                            */
              nextLexeme();         /* skip trailing =                        */
              val = getExprs();
              defineLexeme( start, term, val, DEFINED );
              printLine( line, 0, val, LINE_VAL );
            }
            else
            {
              errorLexeme( &symbol_syntax, lexstartprev );
              nextLexeme();         /* skip symbol                            */
              nextLexeme();         /* skip trailing =                        */
              getExprs();           /* skip expression                        */
            }
            while( line[lexstart] == '\t' ) nextLexeme();
            break;

          default:
            if( isalpha( line[lexstart] ))
            {
              sym = evalSymbol();
              val = sym->val;
              if( M_MACRO( sym->type ))
              {                     /* Find arguments.                       */
                blanks = TRUE;      /* Expecting blanks.                     */
                for( jx = 0; !isdone( line[cc] ) && ( jx < MAC_MAX_ARGS ); cc++ )
                {
                  if(( line[cc] == ',' ) || is_blank( line[cc] )) blanks = TRUE;
                  else if( blanks )
                  {
                     mac_arg_pos[jx++] = cc;
                     blanks = FALSE;
                  }
                } /* end for                                                 */                
                for( ; jx < MAC_MAX_ARGS; jx++ )
                {
                  mac_arg_pos[jx] = 0;
                }
                for( jx = 0; jx < LINELEN; jx++ )
                {
                  mac_line[jx] = line[jx];
                }
                mac_cc = cc;       /* Save line and position in line.        */
                mac_ptr = mac_bodies[val];
                if( mac_ptr ) scanning_line = FALSE;
                else nextLexeme();         
              } /* end if macro                                              */
              else if( M_PSEUDO( sym->type ))
              {
                nextLexeme();         /* Skip symbol                          */
                scanning_line = pseudoOperators( (PSEUDO_T)val & 0777777 );
              }
              else
              {
                /* Identifier is not a pseudo-op, interpret as load value     */
                punchOutObject( clc, getExprs() & 0777777 );
                incrementClc();
              }
            }
            else
            {
              /* Identifier is a value, interpret as load value               */
              punchOutObject( clc, getExprs() & 0777777 );
              incrementClc();
            }
            break;
          } /* end switch                                                     */
          break;
        } /* end switch                                                       */
      } /* end if                                                             */
    } /* end while( scanning_line )                                           */
  } /* end while( TRUE )                                                      */
} /* onePass()                                                                */


/******************************************************************************/
/*                                                                            */
/*  Function:  getExprs                                                       */
/*                                                                            */
/*  Synopsis:  Or together a list of blank separated expressions, from the    */
/*             current lexeme onward.  Leave the current lexeme as            */
/*             the last one in the list.                                      */
/*                                                                            */
/******************************************************************************/
WORD32 getExprs()
{
  SYM_T  *symv;
  SYM_T  *symt;
  WORD32  temp;
  SYMTYP  temp_type;
  WORD32  value;
  SYMTYP  value_type;

  symv = getExpr();
  value = symv->val;
  value_type = symv->type;

  while( TRUE )
  {
    if( isdone( line[lexstart] ) || line[lexstart] == ')' )
    {
      return( value );
    }

    /* Interpret space as add */
    symt = getExpr();
    temp = symt->val & 0777777;
    temp_type = symt->type;

    switch( value_type )
    {
    case MRI:
    case MRIFIX:
      /* Previous symbol was a Memory Reference Instruction.                  */
      switch( temp_type )
      {
      case MRI:
      case MRIFIX:
        /* Current symbol is also a Memory Reference Instruction.             */
        value |= temp;          /* Just OR the MRI instructions.              */
        break;

      default:
        /* Now have the address part of the MRI instruction.                  */
        if(( clc & 060000) == ( temp & 060000))
        {
          value += ( temp & ADDRESS_FIELD );        /* In range MRI.          */
        }
        else
        {
          if(( value & INDIRECT_BIT ) == INDIRECT_BIT )
          {
            /* Already indirect, can't generate                               */
            errorSymbol( &illegal_indirect, symt->name, lexstartprev );
          }
          else
          {
            /* Now fix off page reference.                                  */
            /* Search current page literal pool for needed value.           */
            /* Set Indirect                                                 */
            value += ( INDIRECT_BIT | insertLiteral( &cp, clc, temp & 077777));
            indirect_generated = TRUE;
          }
        }
        break;
      }
      break;

    default:
        value = value + temp;          /* Normal 18 bit value.                */
        if( value >= 0777777 ) value = ( value + 1 ) & 0777777;
        break;
    }
  } /* end while                                                              */
} /* getExprs()                                                               */


/******************************************************************************/
/*                                                                            */
/*  Function:  getExpr                                                        */
/*                                                                            */
/*  Synopsis:  Get an expression, from the current lexeme onward, leave the   */
/*             current lexeme as the one after the expression.  Expressions   */
/*             contain terminal symbols (identifiers) separated by operators. */
/*                                                                            */
/******************************************************************************/
SYM_T *getExpr()
{
  delimiter = line[lexterm];

  if( line[lexstart] == '-' )
  {
    nextLexBlank();
    sym_getexpr = *(eval());
    sym_getexpr.val = sym_getexpr.val ^ 0777777;
  }
  else
  {
    sym_getexpr = *(eval());
  }


  if( is_blank( delimiter ))
  {
    return( &sym_getexpr );
  }

  /* Here we assume the current lexeme is the operator separating the         */
  /* previous operator from the next, if any.                                 */
  while( TRUE )
  {
    /* assert line[lexstart] == delimiter                                     */
    if( is_blank( delimiter ))
    {
      return( &sym_getexpr );
    }

    switch( line[lexstart] )
    {
    case '+':                   /* add                                        */
      nextLexBlank();           /* skip over the operator                     */
      sym_getexpr.val += (eval())->val;
      if( sym_getexpr.val >= 01000000 )
      {
        sym_getexpr.val = ( sym_getexpr.val + 1 ) & 0777777;
      }
      break;

    case '-':                   /* subtract                                   */
      nextLexBlank();           /* skip over the operator                     */
      sym_getexpr.val = sym_getexpr.val +
        ( (eval())->val ^ 0777777 );
      if( sym_getexpr.val >= 01000000 )
      {
        sym_getexpr.val = ( sym_getexpr.val + 1 ) & 0777777;
      }
      break;

    case '^':                   /* multiply                                   */
      nextLexBlank();           /* skip over the operator                     */
      sym_getexpr.val *= (eval())->val;
      break;

    case '%':                   /* divide                                     */
      nextLexBlank();           /* skip over the operator                     */
      sym_getexpr.val /= (eval())->val;
      break;

    case '&':                   /* and                                        */
      nextLexBlank();           /* skip over the operator                     */
      sym_getexpr.val &= (eval())->val;
      break;

    case '!':                   /* or                                         */
      nextLexBlank();           /* skip over the operator                     */
      sym_getexpr.val |= (eval())->val;
      break;

    default:
      if( isend( line[lexstart] ))
      {
        return( &sym_getexpr );
      }

      switch( line[lexstart] )
      {
      case '/':
      case '\t':
      case ')':
      case '<':
      case ':':
      case ',':
        break;

      case '=':
        errorMessage( &illegal_equals, lexstart );
        moveToEndOfLine();
        sym_getexpr.val = 0;
        break;

      default:
        errorMessage( &illegal_expression, lexstart );
        moveToEndOfLine();
        sym_getexpr.val = 0;
        break;
      }
      return( &sym_getexpr );
    }
  } /* end while                                                              */
} /* getExpr()                                                                */


/******************************************************************************/
/*                                                                            */
/*  Function:  eval                                                           */
/*                                                                            */
/*  Synopsis:  Get the value of the current lexeme, set delimiter and advance.*/
/*                                                                            */
/******************************************************************************/
SYM_T *eval()
{
  WORD32  digit;
  WORD32  from;
  WORD32  loc;
  SYM_T  *sym;
  WORD32  val;

  val = 0;

  delimiter = line[lexterm];
  if( isalpha( line[lexstart] ))
  {
    sym = evalSymbol();
    if( M_UNDEFINED( sym->type ))
    {
      if( pass == 2 )
      {
        errorSymbol( &undefined_symbol, sym->name, lexstart );
      }
      nextLexeme();
      return( sym );
    }
    else if( M_PSEUDO( sym->type ))
    {
      if( sym->val == DECIMAL )
      {
        radix = 10;
      }
      else if( sym->val == OCTAL )
      {
        radix = 8;
      }
      else if( pass == 2 )
      {
        errorSymbol( &misplaced_symbol, sym->name, lexstart );
      }
      sym_eval.type = sym->type;
      sym_eval.val = 0;
      nextLexeme();
      return( &sym_eval );
    }
    else if( M_MACRO( sym->type ))
    {
      if( pass == 2 )
      {
        errorSymbol( &misplaced_symbol, sym->name, lexstart );
      }
      sym_eval.type = sym->type;
      sym_eval.val = 0;
      nextLexeme();
      return( &sym_eval );
    }
    else
    {
      nextLexeme();
      return( sym );
    }
  }
  else if( isdigit( line[lexstart] ))
  {
    from = lexstart;
    val = 0;
    while( from < lexterm )
    {
      if( isdigit( line[from] ))
      {
        digit = (WORD32) line[from++] - (WORD32) '0';
        if( digit < radix )
        {
          val = val * radix + digit;
        }
        else
        {
          errorLexeme( &number_not_radix, from - 1 );
          val = 0;
          from = lexterm;
        }
      }
      else
      {
        errorLexeme( &not_a_number, lexstart );
        val = 0;
        from = lexterm;
      }
    }
    nextLexeme();
    sym_eval.val = val;
    return( &sym_eval );
  }
  else
  {
    switch( line[lexstart] )
    {
    case '"':                   /* Character literal                          */
      if( cc + 2 < maxcc )
      {
        val = line[lexstart + 1] | 0200;
        delimiter = line[lexstart + 2];
        cc = lexstart + 2;
      }
      else
      {
        errorMessage( &no_literal_value, lexstart );
      }
      nextLexeme();
      break;

    case '.':                   /* Value of Current Location Counter          */
      val = clc;
      nextLexeme();
      break;

    case '(':                   /* Generate literal on current page.          */
      nextLexBlank();           /* Skip paren                                 */
      val = getExprs() & 0777777;

      if( line[lexstart] == ')' )
      {
        delimiter = line[lexterm];
        nextLexeme();           /* Skip end paren                             */
      }
      else
      {
        /* errorMessage( "parens", NULL );                                    */
      }

      loc = insertLiteral( &cp, clc, val );
      sym_eval.val = loc + ( clc & 060000 );
      return( &sym_eval );

    default:
      switch( line[lexstart] )
      {
      case '=':
        errorMessage( &illegal_equals, lexstart );
        moveToEndOfLine();
        break;

      default:
        errorMessage( &illegal_character, lexstart );
        break;
      }
      val = 0;                  /* On error, set value to zero.               */
      nextLexBlank();           /* Go past illegal character.                 */
    }
  }
  sym_eval.val = val;
  return( &sym_eval );
} /* eval()                                                                   */

/******************************************************************************/
/*                                                                            */
/*  Function:  incrementClc                                                   */
/*                                                                            */
/*  Synopsis:  Set the next assembly location.  Test for collision with       */
/*             the literal tables.                                            */
/*                                                                            */
/******************************************************************************/
WORD32 incrementClc()
{
  testForLiteralCollision( clc );
  clc = (( clc + 1 ) & ADDRESS_FIELD );
  return( clc );
} /* incrementClc()                                                           */


/******************************************************************************/
/*                                                                            */
/*  Function:  testForLiteralCollision                                        */
/*                                                                            */
/*  Synopsis:  Test the given location for collision with the literal tables. */
/*                                                                            */
/******************************************************************************/
BOOL testForLiteralCollision( WORD32 loc )
{
  WORD32  pagelc;
  BOOL    result = FALSE;

  pagelc  = loc & ADDRESS_FIELD;
  if( ( pagelc >= lit_loc ) && ( lit_loc != LIT_BASE ) && !cp.error )
  {
    errorMessage( &literal_overflow, -1 );
    cp.error = TRUE;
    result = TRUE;
  }
  return( result );
} /* testForLiteralCollision()                                                */


/******************************************************************************/
/*                                                                            */
/*  Function:  readLine                                                       */
/*                                                                            */
/*  Synopsis:  Get next line of input.  Print previous line if needed.        */
/*                                                                            */
/******************************************************************************/
void readLine()
{
  BOOL    ffseen;
  WORD32  ix;
  WORD32  iy;
  char    mc;
  char    inpline[4*LINELEN];

  listLine();                   /* List previous line if needed.              */
  indirect_generated = FALSE;   /* Mark no indirect address generated.        */
  error_in_line = FALSE;        /* No error in line.                          */

  if( mac_ptr && ( *mac_ptr == '\0' )) /* End of macro?                       */
  {
    mac_ptr = NULL;
    for( ix = 0; ix < LINELEN; ix++ )
    {
      line[ix] = mac_line[ix];  /* Restore invoking line.                     */
    }
    cc = lexstartprev = mac_cc; /* Restore cc.                                */
    maxcc = strlen( line );     /* Restore maxcc.                             */
    listed = TRUE;              /* Already listed.                            */
    return;
  }

  cc = 0;                       /* Initialize column counter.                 */
  lexstartprev = 0;
  if( mac_ptr )                 /* Inside macro? */
  {
    maxcc = 0;
    do
    {
      mc = *mac_ptr++;          /* Next character.                            */
      if( islower( mc ))        /* Encoded argument number?                   */
      {
        ix = mc - 'a';          /* Convert to index.                          */
        if( iy = mac_arg_pos[ix] )
        {
          do                    /* Copy argument string.                      */
            {
              line[maxcc++] = mac_line[iy++];
            } while(( mac_line[iy] != ',' ) && ( !is_blank( mac_line[iy] )) &&
                    ( !isdone( mac_line[iy] )));
        }
      }
      else                      /* Ordinary character, just copy.             */
      {
      line[maxcc++] = mc;
      }
    } while( !isend( mc ));
    line[maxcc] = '\0';
    listed = nomac_exp;
    return;
  }

  lineno++;                         /* Count lines read.                      */
  listed = FALSE;                   /* Mark as not listed.                    */
READ_LINE:
  if(( fgets( inpline, LINELEN - 1, infile )) == NULL )
  {
    filix_curr++;                   /* Advance to next file.                  */
    if( filix_curr < save_argc )    /* More files?                            */
    {
      fclose( infile );
      if(( infile = fopen( save_argv[filix_curr], "r" )) == NULL )
      {
        fprintf( stderr, "%s: cannot open \"%s\"\n", save_argv[0],
          save_argv[filix_curr] );
        exit( -1 );
      }
      goto READ_LINE;
    }
    else
    {
      end_of_input = TRUE;
    }
  }

  ffseen = FALSE;
  for( ix = 0, iy = 0; inpline[ix] != '\0'; ix++ )
  {
    if( inpline[ix] == '\f' )
    {
      if( !ffseen && list_title_set ) topOfForm( list_title, NULL );
      ffseen = TRUE;
    }
    else
    {
      line[iy++] = inpline[ix];
    }
  }
  line[iy] = '\0';

  /* If the line is terminated by CR-LF, remove, the CR.                      */
  if( line[iy - 2] == '\r' )
  {
    iy--;
    line[iy - 1] = line[iy - 0];
    line[iy] = '\0';
  }
  maxcc = iy;                   /* Save the current line length.              */
} /* readLine()                                                               */


/******************************************************************************/
/*                                                                            */
/*  Function:  listLine                                                       */
/*                                                                            */
/*  Synopsis:  Output a line to the listing file.                             */
/*                                                                            */
/******************************************************************************/
void listLine()
/* generate a line of listing if not already done!                            */
{
  if( listfile != NULL && listed == FALSE )
  {
    printLine( line, 0, 0, LINE );
  }
} /* listLine()                                                               */


/******************************************************************************/
/*                                                                            */
/*  Function:  printPageBreak                                                 */
/*                                                                            */
/*  Synopsis:  Output a Top of Form and listing header if new page necessary. */
/*                                                                            */
/******************************************************************************/
void printPageBreak()
{
  if( page_lineno >= LIST_LINES_PER_PAGE )
         /*  ( list_lineno % LIST_LINES_PER_PAGE ) == 0 ) */
  {
    if( !list_title_set )
    {
      strcpy( list_title, line );
      if( list_title[strlen(list_title) - 1] == '\n' )
      {
        list_title[strlen(list_title) - 1] = '\0';
      }
      if( strlen( list_title ) > TITLELEN )
      {
        list_title[TITLELEN] = '\0';
      }
      list_title_set = TRUE;
    }
    topOfForm( list_title, NULL );

  }
} /* printPageBreak()                                                         */


/******************************************************************************/
/*                                                                            */
/*  Function:  printLine                                                      */
/*                                                                            */
/*  Synopsis:  Output a line to the listing file with new page if necessary.  */
/*                                                                            */
/******************************************************************************/
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

  case LINE_LOC_VAL:
    if( !listed )
    {
      if( indirect_generated )
      {
        fprintf( listfile, "%5d %5.5o %6.6o@     ", lineno, loc, val );
      }
      else
      {
        fprintf( listfile, "%5d %5.5o %6.6o      ", lineno, loc, val );
      }
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
} /* printLine()                                                              */


/******************************************************************************/
/*                                                                            */
/*  Function:  printErrorMessages                                             */
/*                                                                            */
/*  Synopsis:  Output any error messages from the current list of errors.     */
/*                                                                            */
/******************************************************************************/
void printErrorMessages()
{
  WORD32  ix;
  WORD32  iy;

  if( listfile != NULL )
  {
    /* If any errors, display them now.                                       */
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
} /* printErrorMessages()                                                     */


/******************************************************************************/
/*                                                                            */
/*  Function:  endOfBinary                                                    */
/*                                                                            */
/*  Synopsis:  Outputs both literal tables at the end of a binary segment.    */
/*                                                                            */
/******************************************************************************/
void endOfBinary()
{
  punchLiteralPool( &cp, clc - 1 );
  if( start_addr >= 0)
  {
    punchTriplet( JMP | ( start_addr & 017777 ));
    punchTriplet( 0 );
  }
  return;
} /* endOfBinary()                                                            */


/******************************************************************************/
/*                                                                            */
/*  Function:  punchLeader                                                    */
/*                                                                            */
/*  Synopsis:  Generate 2 feet of leader on object file, as per DEC           */
/*             documentation.  Paper tape has 10 punches per inch.            */
/*                                                                            */
/******************************************************************************/
void punchLeader( WORD32 count )
{
  WORD32  ix;

  /* If value is zero, set to the default of 2 feet of leader.                */
  count = ( count == 0 ) ? 240 : count;

  if( objectfile != NULL )
  {
    for( ix = 0; ix < count; ix++ )
    {
      fputc( 0, objectfile );
    }
  }
} /* punchLeader()                                                            */


/******************************************************************************/
/*                                                                            */
/*  Function:  punchObject                                                    */
/*                                                                            */
/*  Synopsis:  Put one character to object file and include it in checksum.   */
/*                                                                            */
/******************************************************************************/
void punchObject( WORD32 val )
{
  val &= 0377;
  if( objectfile != NULL )
  {
    fputc( val, objectfile );
  }
  checksum += val;
  binary_data_output = TRUE;
} /* punchObject()                                                            */


/******************************************************************************/
/*                                                                            */
/*  Function:  punchOutObject                                                 */
/*                                                                            */
/*  Synopsis:  Output the current line and then then punch value to the       */
/*             object file.                                                   */
/*                                                                            */
/******************************************************************************/
void punchOutObject( WORD32 loc, WORD32 val )
{
  printLine( line, loc, val, LINE_LOC_VAL );
  punchLocObject( loc, val );
} /* punchOutObject()                                                         */


/******************************************************************************/
/*                                                                            */
/*  Function:  punchLocObject                                                 */
/*                                                                            */
/*  Synopsis:  Output the word (with origin if rim format) to the object file.*/
/*                                                                            */
/******************************************************************************/
void punchLocObject( WORD32 loc, WORD32 val )
{
  punchTriplet( DAC | loc );
  punchTriplet( val );
} /* punchLocObject()                                                         */


/******************************************************************************/
/*                                                                            */
/*  Function:  punchTriplet                                                   */
/*                                                                            */
/*  Synopsis:  Output 18b word as three 6b characters with ho bit set.        */
/*                                                                            */
/******************************************************************************/
void punchTriplet( WORD32 val )
{
  punchObject((( val >> 12) & 077) | 0200 );
  punchObject((( val >> 6 ) & 077) | 0200 );
  punchObject(( val & 077) | 0200 );
} /* punchTriplet */


/******************************************************************************/
/*                                                                            */
/*  Function:  punchLiteralPool                                               */
/*                                                                            */
/*  Synopsis:  Output the current page data.                                  */
/*                                                                            */
/******************************************************************************/
void punchLiteralPool( LPOOL_T *p, WORD32 lpool_page )
{
  WORD32  loc;
  WORD32  tmplc;

  if( lit_loc < LIT_BASE )
  {
    for( loc = lit_loc; loc < LIT_BASE; loc++ )
    {
      tmplc = loc + ( lpool_page & 060000 );
      printLine( line, tmplc, p->pool[loc], LOC_VAL );
      punchLocObject( tmplc, p->pool[loc] );
    }
    p->error = FALSE;
    lit_loc = LIT_BASE;
  }
} /* punchLiteralPool()                                                       */


/******************************************************************************/
/*                                                                            */
/*  Function:  insertLiteral                                                  */
/*                                                                            */
/*  Synopsis:  Add a value to the given literal pool if not already in pool.  */
/*             Return the location of the value in the pool.                  */
/*                                                                            */
/******************************************************************************/
WORD32 insertLiteral( LPOOL_T *p, WORD32 pool_page, WORD32 value )
{
  WORD32  ix;

  /* Search the literal pool for any occurence of the needed value.           */
  ix = LIT_BASE - 1;
  while( ix >= lit_loc && p->pool[ix] != value )
  {
    ix--;
  }

  /* Check if value found in literal pool. If not, then insert value.         */
  if( ix < lit_loc )
  {
    lit_loc--;
    p->pool[lit_loc] = value;
    ix = lit_loc;
  }
  return( ix );
} /* insertLiteral()                                                          */


/******************************************************************************/
/*                                                                            */
/*  Function:  printSymbolTable                                               */
/*                                                                            */
/*  Synopsis:  Output the symbol table.                                       */
/*                                                                            */
/******************************************************************************/
void printSymbolTable()
{
  int    col;
  int    cx;
  char  *fmt;
  int    ix;
  char   mark;
  int    page;
  int    row;
  int    symbol_base;
  int    symbol_lines;

  symbol_base = number_of_fixed_symbols;

  for( page=0, list_lineno=0, col=0, ix=symbol_base; ix < symbol_top; page++ )
  {
    topOfForm( list_title, s_symtable );
    symbol_lines = LIST_LINES_PER_PAGE - page_lineno;

    for( row = 0; page_lineno < LIST_LINES_PER_PAGE && ix < symbol_top; row++)
    {
      list_lineno++;
      page_lineno++;
      fprintf( listfile, "%5d", list_lineno );

      for( col = 0; col < SYMBOL_COLUMNS && ix < symbol_top; col++ )
      {
        /* Get index of symbol for the current line and column                         */
        cx = symbol_lines * ( SYMBOL_COLUMNS * page + col ) + row;
        cx += symbol_base;

        /* Make sure that there is a symbol to be printed.                    */
        if( number_of_fixed_symbols <= cx && cx < symbol_top )
        {
          switch( symtab[cx].type & LABEL )
          {
          case LABEL:
            fmt = " %c%-6.6s %5.5o ";
            break;

          default:
            fmt = " %c%-6.6s  %4.4o ";
            break;
          }

          switch( symtab[cx].type & ( DEFINED | REDEFINED ))
          {
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
          fprintf( listfile, fmt, mark, symtab[cx].name, symtab[cx].val );
          ix++;
        }
      }
      fprintf( listfile, "\n" );
    }
  }
} /* printSymbolTable()                                                       */


/******************************************************************************/
/*                                                                            */
/*  Function:  printPermanentSymbolTable                                      */
/*                                                                            */
/*  Synopsis:  Output the permanent symbol table to a file suitable for       */
/*             being input after the EXPUNGE pseudo-op.                       */
/*                                                                            */
/******************************************************************************/
void printPermanentSymbolTable()
{
  int     ix;
  FILE   *permfile;
  char  *s_type;

  if(( permfile = fopen( permpathname, "w" )) == NULL )
  {
    exit( 2 );
  }

  fprintf( permfile, "/ PERMANENT SYMBOL TABLE\n/\n" );
  fprintf( permfile, "        EXPUNGE\n/\n" );
  /* Print the memory reference instructions first.                           */
  s_type = "FIXMRI";
  for( ix = 0; ix < symbol_top; ix++ )
  {
    if( M_MRI( symtab[ix].type ))
    {
      fprintf( permfile, "%-7s %s=%4.4o\n",
                                    s_type, symtab[ix].name, symtab[ix].val );
    }
  }

  s_type = " ";
  for( ix = 0; ix < symbol_top; ix++ )
  {
    if( M_FIXED( symtab[ix].type ))
    {
      if( !M_MRI( symtab[ix].type ) && !M_PSEUDO( symtab[ix].type ))
      {
        fprintf( permfile, "%-7s %s=%4.4o\n",
                                    s_type, symtab[ix].name, symtab[ix].val );
      }
    }
  }
  fprintf( permfile, "/\n        FIXTAB\n" );
  fclose( permfile );
} /* printPermanentSymbolTable()                                              */


/******************************************************************************/
/*                                                                            */
/*  Function:  printCrossReference                                            */
/*                                                                            */
/*  Synopsis:  Output a cross reference (concordance) for the file being      */
/*             assembled.                                                     */
/*                                                                            */
/******************************************************************************/
void printCrossReference()
{
  int    ix;
  int    symbol_base;
  int    xc;
  int    xc_index;
  int    xc_refcount;
  int    xc_cols;

  /* Force top of form for first page.                                        */
  page_lineno = LIST_LINES_PER_PAGE;

  list_lineno = 0;
  symbol_base = number_of_fixed_symbols;

  for( ix = symbol_base; ix < symbol_top; ix++ )
  {
    list_lineno++;
    page_lineno++;
    if( page_lineno >= LIST_LINES_PER_PAGE )
    {
      topOfForm( list_title, s_xref );
    }

    fprintf( listfile, "%5d", list_lineno );

    /* Get reference count & index into concordance table for this symbol.    */
    xc_refcount = symtab[ix].xref_count;
    xc_index = symtab[ix].xref_index;
    /* Determine how to label symbol on concordance.                          */
    switch( symtab[ix].type & ( DEFINED | REDEFINED ))
    {
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
    fprintf( listfile, "%-6.6s  ", symtab[ix].name );

    /* Output the references, 8 numbers per line after symbol name.           */
    for( xc_cols = 0, xc = 1; xc < xc_refcount + 1; xc++, xc_cols++ )
    {
      if( xc_cols >= XREF_COLUMNS )
      {
        xc_cols = 0;
        page_lineno++;
        if( page_lineno >= LIST_LINES_PER_PAGE )
        {
          topOfForm( list_title, s_xref);
        }
        list_lineno++;
        fprintf( listfile, "\n%5d%-19s", list_lineno, " " );
      }
      fprintf( listfile, "  %5d", xreftab[xc_index + xc] );
    }
    fprintf( listfile, "\n" );
  }
} /* printCrossReference()                                                    */


/******************************************************************************/
/*                                                                            */
/*  Function:  topOfForm                                                      */
/*                                                                            */
/*  Synopsis:  Prints title and sub-title on top of next page of listing.     */
/*                                                                            */
/******************************************************************************/
void topOfForm( char *title, char *sub_title )
{
  char temp[10];

  list_pageno++;
  strcpy( temp, s_page );
  sprintf( temp, "%s %d", s_page, list_pageno );

  /* Output a top of form if not the first page of the listing.               */
  if( list_pageno > 1 )
  {
    fprintf( listfile, "\f" );
  }
  fprintf( listfile, "\n      %-63s %10s\n", title, temp );

  /* Reset the current page line counter.                                     */
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
} /* topOfForm()                                                              */


/******************************************************************************/
/*                                                                            */
/*  Function:  lexemeToName                                                   */
/*                                                                            */
/*  Synopsis:  Convert the current lexeme into a string.                      */
/*                                                                            */
/******************************************************************************/
char *lexemeToName( char *name, WORD32 from, WORD32 term )
{
  WORD32  to;

  to = 0;

  while( from < term && to < ( SYMLEN - 1 ))
  {
    name[to++] = toupper( line[from++] );
  }

  while( to < SYMLEN )
  {
    name[to++] = '\0';
  }
  return( name );
} /* lexemeToName()                                                           */

/******************************************************************************/
/*                                                                            */
/*  Function:  defineLexeme                                                   */
/*                                                                            */
/*  Synopsis:  Put lexeme into symbol table with a value.                     */
/*                                                                            */
/******************************************************************************/
SYM_T *defineLexeme( WORD32  start,     /* start of lexeme being defined.     */
		         WORD32  term,      /* end+1 of lexeme being defined.     */
                     WORD32  val,       /* value of lexeme being defined.     */
                     SYMTYP  type )     /* how symbol is being defined.       */
{
  char  name[SYMLEN];

  lexemeToName( name, start, term);
  return( defineSymbol( name, val, type, start ));
} /* defineLexeme()                                                           */


/******************************************************************************/
/*                                                                            */
/*  Function:  defineSymbol                                                   */
/*                                                                            */
/*  Synopsis:  Define a symbol in the symbol table, enter symbol name if not  */
/*             not already in table.                                          */
/*                                                                            */
/******************************************************************************/
SYM_T *defineSymbol( char *name, WORD32 val, SYMTYP type, WORD32 start )
{
  SYM_T  *sym;
  WORD32  xref_count;

  if( strlen( name ) < 1 )
  {
    return( &sym_undefined );   /* Protect against non-existent names.        */
  }
  sym = lookup( name );
  xref_count = 0;               /* Set concordance for normal defintion.      */

  if( M_DEFINED( sym->type ) && sym->val != val && M_NOTRDEF( sym -> type ))
  {
    if( pass == 2 )
    {
      errorSymbol( &redefined_symbol, sym->name, start );
      type = type | REDEFINED;
      sym->xref_count++;      /* Referenced symbol, count it.                */
      xref_count = sym->xref_count;
    }
  return ( sym );
  }

  if( pass == 2 && xref )
  {
    /* Put the definition line number in the concordance table.               */
    /* Defined symbols are not counted as references.                         */
    xreftab[sym->xref_index] = lineno;
    /* Put the line number in the concordance table.                          */
    xreftab[sym->xref_index + xref_count] = lineno;
  }

  /* Now set the value and the type.                                          */
  sym->val = ( type == LABEL) ? val : val & 0777777;
  sym->type = ( pass == 1 ) ? ( type | CONDITION ) : type;
  return( sym );
} /* defineSymbol()                                                           */


/******************************************************************************/
/*                                                                            */
/*  Function:  lookup                                                         */
/*                                                                            */
/*  Synopsis:  Find a symbol in table.  If not in table, enter symbol in      */
/*             table as undefined.  Return address of symbol in table.        */
/*                                                                            */
/******************************************************************************/
SYM_T *lookup( char *name )
{
  int     ix;                   /* Insertion index                            */
  int     lx;                   /* Left index                                 */
  int     rx;                   /* Right index                                */

  /* First search the permanent symbols.                                      */
  lx = 0;
  ix = binarySearch( name, lx, number_of_fixed_symbols );

  /* If symbol not in permanent symbol table.                                 */
  if( ix < 0 )
  {
    /* Now try the user symbol table.                                         */
    ix = binarySearch( name, number_of_fixed_symbols, symbol_top );

    /* If symbol not in user symbol table.                                    */
    if( ix < 0 )
    {
      /* Must put symbol in table if index is negative.                       */
      ix = ~ix;
      if( symbol_top + 1 >= SYMBOL_TABLE_SIZE )
      {
        errorSymbol( &symbol_table_full, name, lexstart );
        exit( 1 );
      }

      for( rx = symbol_top; rx >= ix; rx-- )
      {
        symtab[rx + 1] = symtab[rx];
      }
      symbol_top++;

      /* Enter the symbol as UNDEFINED with a value of zero.                  */
      strcpy( symtab[ix].name, name );
      symtab[ix].type = UNDEFINED;
      symtab[ix].val  = 0;
      symtab[ix].xref_count = 0;
      if( xref && pass == 2 )
      {
        xreftab[symtab[ix].xref_index] = 0;
      }
    }
  }

  return( &symtab[ix] );        /* Return the location of the symbol.         */
} /* lookup()                                                                 */


/******************************************************************************/
/*                                                                            */
/*  Function:  binarySearch                                                   */
/*                                                                            */
/*  Synopsis:  Searches the symbol table within the limits given.  If the     */
/*             symbol is not in the table, it returns the insertion point.    */
/*                                                                            */
/******************************************************************************/
int binarySearch( char *name, int start, int symbol_count )
{
  int     lx;                   /* Left index                                 */
  int     mx;                   /* Middle index                               */
  int     rx;                   /* Right index                                */
  int     compare;              /* Results of comparison                      */

  lx = start;
  rx = symbol_count - 1;
  while( lx <= rx )
  {
    mx = ( lx + rx ) / 2;   /* Find center of search area.                    */

    compare = strcmp( name, symtab[mx].name );

    if( compare < 0 )
    {
      rx = mx - 1;
    }
    else if( compare > 0 )
    {
      lx = mx + 1;
    }
    else
    {
      return( mx );         /* Found a match in symbol table.                 */
    }
  } /* end while                                                              */
  return( ~lx );            /* Return insertion point.                        */
} /* binarySearch()                                                           */


/******************************************************************************/
/*                                                                            */
/*  Function:  compareSymbols                                                 */
/*                                                                            */
/*  Synopsis:  Used to presort the symbol table when starting assembler.      */
/*                                                                            */
/******************************************************************************/
int compareSymbols( const void *a, const void *b )
{
  return( strcmp( ((SYM_T *) a)->name, ((SYM_T *) b)->name ));
} /* compareSymbols()                                                         */

/******************************************************************************/
/*                                                                            */
/*  Function:  copyMacLine                                                    */
/*                                                                            */
/*  Synopsis:  Used to copy a macro line to the macro buffer.                 */
/*                                                                            */
/******************************************************************************/
int copyMacLine( int length, int from, int term, int nargs )
{
  char    name[SYMLEN];
  int     ix;
  int     jx;
  int     kx;
  BOOL    bl;

  bl = TRUE;
  for( ix = from; ix < term; ix++ )
  {
    if( !is_blank( line[ix] ) || ( line[ix] == '\t' )) bl = FALSE;
  }
  if( bl || ( length < 0 )) return length;
  if(( length + term - from + 1) >= MAC_MAX_LENGTH ) return -1;
  for( ix = from; ix < term; )
  {
    if( nargs && isalpha( line[ix] ))      /* Start of symbol?                */
    {
      for( jx = ix + 1; jx < term; jx++)   /* Find end of symbol.             */
      {
        if( !isalnum( line[jx] )) break;
      }
      lexemeToName( name, ix, jx );        /* Make into name.                 */
      for( kx = 0; kx < nargs; kx++ )      /* Compare to arguments.           */
      {
        if( strncmp( name, &mac_arg_name[kx + 1][0], SYMLEN ) == 0 )
        {
           mac_buffer[length++] = 'a' + (char) kx;
           for( ix++; ix < jx; ix++ )
           {
             mac_buffer[length++] = 'z';
           }
           break;
        } /* end if strncmp                                                   */
      } /* end for kx                                                         */
      if( kx >= nargs )
      {
        for ( ; ix < jx; )
        {
          mac_buffer[length++] = toupper( line[ix++] );
		}
      }
    } /*end if nargs                                                          */
    else
    {
      mac_buffer[length++] = toupper( line[ix++] );
    } /* end else                                                            */
  } /* end for ix                                                            */
  mac_buffer[length++] = '\n';
  mac_buffer[length] = 0;
  return length;
}

/******************************************************************************/
/*                                                                            */
/*  Function:  evalSymbol                                                     */
/*                                                                            */
/*  Synopsis:  Get the pointer for the symbol table entry if exists.          */
/*             If symbol doesn't exist, return a pointer to the undefined sym */
/*                                                                            */
/******************************************************************************/
SYM_T *evalSymbol()
{
  char   name[SYMLEN];
  SYM_T *sym;

  sym = lookup( lexemeToName( name, lexstart, lexterm ));

  sym->xref_count++;            /* Count the number of references to symbol.  */

  if( xref && pass == 2 )
  {
    /* Put the line number in the concordance table.                          */
    xreftab[sym->xref_index + sym->xref_count] = lineno;
  }

  return( sym );
} /* evalSymbol()                                                             */


/******************************************************************************/
/*                                                                            */
/*  Function:  moveToEndOfLine                                                */
/*                                                                            */
/*  Synopsis:  Move the parser input to the end of the current input line.    */
/*                                                                            */
/******************************************************************************/
void moveToEndOfLine()
{
  while( !isend( line[cc] )) cc++;
  lexstart = cc;
  lexterm = cc;
  lexstartprev = lexstart;
} /* moveToEndOfLine()                                                        */

/******************************************************************************/
/*                                                                            */
/*  Function:  nextLexeme                                                     */
/*                                                                            */
/*  Synopsis:  Get the next lexical element from input line.                  */
/*                                                                            */
/******************************************************************************/
void nextLexeme()
{
  /* Save start column of previous lexeme for diagnostic messages.            */
  lexstartprev = lexstart;
  lextermprev = lexterm;

  while( is_blank( line[cc] ) ||
       (( line[cc] == '\t' ) && ( line[cc+1] == '\t' )) ||
       (( line[cc] == '\t' ) && isdone( line[cc+1] ))) { cc++; }
  lexstart = cc;

  if( isalnum( line[cc] ))
  {
    while( isalnum( line[cc] )) { cc++; }
  }
  else if( isend( line[cc] ))
  {
    /* End-of-Line, don't advance cc!                                         */
  }
  else
  {
    switch( line[cc] )
    {
    case '"':     /* Quoted letter                                            */
      if( cc + 2 < maxcc )
      {
        cc++;
        cc++;
      }
      else
      {
        errorMessage( &no_literal_value, lexstart );
        cc++;
      }
      break;

    case '/':     /* Comment, don't advance cc!                               */
      break;

    default:      /* All other punctuation.                                   */
      cc++;
      break;
    }
  }
  lexterm = cc;
} /* nextLexeme()                                                             */


/******************************************************************************/
/*                                                                            */
/*  Function:  nextLexBlank                                                   */
/*                                                                            */
/*  Synopsis:  Used to prevent illegal blanks in expressions.                 */
/*                                                                            */
/******************************************************************************/
void nextLexBlank()
{
  nextLexeme();
  if( is_blank( delimiter ))
  {
    errorMessage( &illegal_blank, lexstart - 1 );
  }
  delimiter = line[lexterm];
} /* nextLexBlank()                                                           */
 


/******************************************************************************/
/*                                                                            */
/*  Function:  pseudoOperators                                                */
/*                                                                            */
/*  Synopsis:  Process pseudo-ops (directives).                               */
/*                                                                            */
/******************************************************************************/
BOOL pseudoOperators( PSEUDO_T val )
{
  int     count;
  int     delim;
  int     index;
  int     ix;
  WORD32  length;
  int     level;
  int     lexstartsave;
  int     pack;
  int     pos;
  int     radixprev;
  BOOL    status;
  SYM_T  *sym;
  WORD32  value;
  WORD32  word;
  int     width;
  static int mask_tab[19] = { 0000000,
          0000001, 0000003, 0000007, 0000017, 0000037, 0000077,
          0000177, 0000377, 0000777, 0001777, 0003777, 0007777,
          0017777, 0037777, 0077777, 0177777, 0377777, 0777777 };

  status = TRUE;
  switch( (PSEUDO_T) val )
  {
  case DECIMAL:
    radix = 10;
    break;

  case DEFINE:
    count = 0;
    index = 0;
    lexstartsave = lexstart;
    while(( line[lexstart] != '<' ) && ( !isdone( line[lexstart] )) &&
          ( count < MAC_MAX_ARGS ))
    {
      if ( !isalpha( line[lexstart] ) && ( index == 0 ))
      {
        index = lexstart;
      }
      lexemeToName( &mac_arg_name[count++][0], lexstart, lexterm );
      nextLexeme();
    }
    if( count == 0 )                 /* No macro name.                        */
    {
      errorMessage( &no_macro_name, lexstartsave );
      index = 1;
    }
    else if( index )                 /* Bad argument name.                    */
    {
      errorMessage( &bad_dummy_arg, index );
    }
    else if( mac_count >= MAC_TABLE_LENGTH )
    {
      errorMessage( &macro_table_full, lexstartsave );
      index = 1;
    }
    else
    {
      value = mac_count;
      mac_count++;                  /* Value is entry in mac_bodies.         */
      defineSymbol( &mac_arg_name[0][0], value, MACRO, lexstartsave );
    }
    if( isend( line[lexstart] ) || ( line[lexstart] == '/' ))
    {
      readLine();
      nextLexeme();
    }
    if( index )
    {
      conditionFalse();             /* On error skip macro body.             */
    }
    else if( line[lexstart] == '<' )
    {
      /* Invariant: line[cc] is the next unexamined character.                */
      index = lexstart + 1;
      length = 0;
      level = 1;
      while( level > 0 )
      {
        if( end_of_input )
        {
          break;
        }
        if( isend( line[cc] ) || ( line[cc] == '/' ))
        {
          length = copyMacLine( length, index, cc, count - 1 );
          readLine();
          index = 0;
        }
        else
        {
          switch( line[cc] )
          {
          case '>':
            level--;
            cc++;
            break;

          case '<':
            level++;
            cc++;
            break;

          default:
            cc++;
            break;
          } /* end switch                                                     */
        } /* end if                                                           */
      } /* end while                                                          */
      length = copyMacLine( length, index, cc - 1, count - 1 );
      if( length < 0 )
      {
        errorMessage (&macro_too_long, lexstart );
      }
      else if( length == 0 )
      {
        mac_bodies[value] = NULL;
      }
      else
      {
        mac_bodies[value] = (char *) malloc( length + 1 );
        if( mac_bodies[value] )
        {
           strncpy( mac_bodies[value], mac_buffer, length );
           *( mac_bodies[value] + length ) = 0;
        }
        else
        {
           errorMessage( &no_virtual_memory, lexstart );
        }
      }
      nextLexeme();
    }
    else
    {
      errorMessage( &lt_expected, lexstart );
    } /* end if                                                               */
    break;
      
  case EJECT:
    page_lineno = LIST_LINES_PER_PAGE;  /* This will force a page break.      */
    status = FALSE;             /* This will force reading of next line       */
    break;

  case IFDEF:
    if( isalpha( line[lexstart] ))
    {
      sym = evalSymbol();
      nextLexeme();
      if( M_DEFINED_CONDITIONALLY( sym->type ))
      {
        conditionTrue();
      }
      else
      {
        conditionFalse();
      }
    }
    else
    {
      errorLexeme( &label_syntax, lexstart );
    }
    break;

  case IFNDEF:
    if( isalpha( line[lexstart] ))
    {
      sym = evalSymbol();
      nextLexeme();
      if( M_DEFINED_CONDITIONALLY( sym->type ))
      {
        conditionFalse();
      }
      else
      {
        conditionTrue();
      }
    }
    else
    {
      errorLexeme( &label_syntax, lexstart );
    }
    break;

  case IFNZERO:
    if( (getExpr())->val == 0 )
    {
      conditionFalse();
    }
    else
    {
      conditionTrue();
    }
    break;

  case IFZERO:
    if( (getExpr())->val == 0 )
    {
      conditionTrue();
    }
    else
    {
      conditionFalse();
    }
    break;

  case LIST:
    listfile = listsave;
    break;

  case NOLIST:
    listfile = NULL;
    break;

  case OCTAL:
    radix = 8;
    break;

  case START:
    if( !isdone( line[lexstart] ))
    {
      start_addr = (getExpr())->val & 077777;
      nextLexeme();
    }
    printLine( line, 0, start_addr, LINE_VAL );
    status = FALSE;
    break;

  case TEXT:
    delim = line[lexstart];
    pack = 0;
    count = 0;
    index = lexstart + 1;
    while( line[index] != delim && !isend( line[index] ))
    {
      pack = ( pack << 6 ) | ( line[index] & 077 );
      count++;
      if( count > 2 )
      {
        punchOutObject( clc, pack );
        incrementClc();
        count = 0;
        pack = 0;
      }
      index++;
    }

    if( count == 2 )
    {
      punchOutObject( clc, pack << 6 );
    }
    else if (count == 1)
    {
      punchOutObject( clc, pack << 12 );
    }
    else
    {
      punchOutObject( clc, 0 );
    }
    incrementClc();

    if( isend( line[index] ))
    {
      cc = index;
      lexterm = cc;
      errorMessage( &text_string, cc );
    }
    else
    {
      cc = index + 1;
      lexterm = cc;
    }
    nextLexeme();
    break;

  case TITLE:
    delim = line[lexstart];
    ix = lexstart + 1;
    /* Find string delimiter.                                                 */
    do
    {
      if( list_title[ix] == delim && list_title[ix + 1] == delim )
      {
        ix++;
      }
      ix++;
    } while( line[ix] != delim && !isend(line[ix]) );

    if( !isend( line[ix] ) )
    {
      count = 0;
      ix = lexstart + 1;
      do
      {
        if( list_title[ix] == delim && list_title[ix + 1] == delim )
        {
          ix++;
        }
        list_title[count] = line[ix];
        count++;
        ix++;
      } while( line[ix] != delim && !isend(line[ix]) );

      if( strlen( list_title ) > TITLELEN )
      {
        list_title[TITLELEN] = '\0';
      }

      cc = ix + 1;
      lexterm = cc;
      page_lineno = LIST_LINES_PER_PAGE;/* Force top of page for new titles.  */
      list_title_set = TRUE;
    }
    else
    {
      cc = ix;
      lexterm = cc;
      errorMessage( &text_string, cc );
    }

    nextLexeme();
    break;

  case VFD:
    pos = 0;
    word = 0;
    radixprev = radix;
    while( !isdone (line[lexstart] ))
    {
      lexstartsave = lexstart;
      radix = 10;
  	  width = (getExpr())->val;     /* Get field width.                       */ 
      radix = radixprev;
      if( (width <= 0) || ((width + pos) > 18) || (line[lexstart] != ':') )
      {
        errorMessage( &illegal_vfd_value, lexstartsave );
      }
      nextLexBlank();               /* Skip colon.                            */
      value = (getExpr())->val;     /* Get field value.                       */
      if( line[lexterm] == ',' ) cc++;
      nextLexeme();                 /* Advance to next field.                 */
      pos = pos + width;
	  if( pos <= 18 )
	  {
        word = word | ((value & mask_tab[width]) << (18 - pos));
	  }
    }
    punchOutObject( clc, word );
    incrementClc();
    break;

  default:
    break;
  } /* end switch for pseudo-ops                                              */
  return( status );
} /* pseudoOperators()                                                        */


/******************************************************************************/
/*                                                                            */
/*  Function:  conditionFalse                                                 */
/*                                                                            */
/*  Synopsis:  Called when a false conditional has been evaluated.            */
/*             Lex should be the opening <; ignore all text until             */
/*             the closing >.                                                 */
/*                                                                            */
/******************************************************************************/
void conditionFalse()
{
  int     level;

  if( line[lexstart] == '<' )
  {
    /* Invariant: line[cc] is the next unexamined character.                  */
    level = 1;
    while( level > 0 )
    {
      if( end_of_input )
      {
        break;
      }
      if( isend( line[cc] ) || ( line[cc] == '/' ))
      {
        readLine();
      }
      else
      {
        switch( line[cc] )
        {
        case '>':
          level--;
          cc++;
          break;

        case '<':
          level++;
          cc++;
          break;

        default:
          cc++;
          break;
        } /* end switch                                                       */
      } /* end if                                                             */
    } /* end while                                                            */
    nextLexeme();
  }
  else
  {
    errorMessage( &lt_expected, lexstart );
  }
} /* conditionFalse()                                                         */

/******************************************************************************/
/*                                                                            */
/*  Function:  conditionTrue                                                  */
/*                                                                            */
/*  Synopsis:  Called when a true conditional has been evaluated.             */
/*             Lex should be the opening <; skip it and setup for             */
/*             normal assembly.                                               */
/*                                                                            */
/******************************************************************************/
void conditionTrue()
{
  if( line[lexstart] == '<' )
  {
    nextLexeme();               /* Skip the opening '<'                       */
  }
  else
  {
    errorMessage( &lt_expected, lexstart );
  }
} /* conditionTrue()                                                          */


/******************************************************************************/
/*                                                                            */
/*  Function:  errorLexeme                                                    */
/*                                                                            */
/*  Synopsis:  Display an error message using the current lexical element.    */
/*                                                                            */
/******************************************************************************/
void errorLexeme( EMSG_T *mesg, WORD32 col )
{
  char   name[SYMLEN];

  errorSymbol( mesg, lexemeToName( name, lexstart, lexterm ), col );
} /* errorLexeme()                                                            */


/******************************************************************************/
/*                                                                            */
/*  Function:  errorSymbol                                                    */
/*                                                                            */
/*  Synopsis:  Display an error message with a given string.                  */
/*                                                                            */
/******************************************************************************/
void errorSymbol( EMSG_T *mesg, char *name, WORD32 col )
{
  char   linecol[12];
  char  *s;

  if( pass == 2 )
  {
    s = ( name == NULL ) ? "" : name ;
    errors++;
    sprintf( linecol, "(%d:%d)", lineno, col + 1 );
    fprintf( errorfile, "%s%-9s : error:  %s \"%s\" at Loc = %5.5o\n",
                                      filename, linecol, mesg->file, s, clc );
    saveError( mesg->list, col );
  }
  error_in_line = TRUE;
} /* errorSymbol()                                                            */


/******************************************************************************/
/*                                                                            */
/*  Function:  errorMessage                                                   */
/*                                                                            */
/*  Synopsis:  Display an error message without a name argument.              */
/*                                                                            */
/******************************************************************************/
void errorMessage( EMSG_T *mesg, WORD32 col )
{
  char   linecol[12];

  if( pass == 2 )
  {
    errors++;
    sprintf( linecol, "(%d:%d)", lineno, col + 1 );
    fprintf( errorfile, "%s%-9s : error:  %s at Loc = %5.5o\n",
                                         filename, linecol, mesg->file, clc );
    saveError( mesg->list, col );
  }
  error_in_line = TRUE;
} /* errorMessage()                                                           */

/******************************************************************************/
/*                                                                            */
/*  Function:  saveError                                                      */
/*                                                                            */
/*  Synopsis:  Save the current error in a list so it may displayed after the */
/*             the current line is printed.                                   */
/*                                                                            */
/******************************************************************************/
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
  {
    printErrorMessages();
  }
} /* saveError()                                                              */
/* End-of-File                                                                */
