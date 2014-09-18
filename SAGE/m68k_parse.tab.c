/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     A0 = 258,
     A1 = 259,
     A2 = 260,
     A3 = 261,
     A4 = 262,
     A5 = 263,
     A6 = 264,
     A7 = 265,
     D0 = 266,
     D1 = 267,
     D2 = 268,
     D3 = 269,
     D4 = 270,
     D5 = 271,
     D6 = 272,
     D7 = 273,
     CCR = 274,
     SR = 275,
     USP = 276,
     PC = 277,
     NUMBER = 278,
     ABCD = 279,
     ADD = 280,
     ADDA = 281,
     ADDI = 282,
     ADDQ = 283,
     ADDX = 284,
     AND = 285,
     ANDI = 286,
     OR = 287,
     ORI = 288,
     SBCD = 289,
     SUB = 290,
     SUBA = 291,
     SUBI = 292,
     SUBQ = 293,
     SUBX = 294,
     ASL = 295,
     ASR = 296,
     LSL = 297,
     LSR = 298,
     ROL = 299,
     ROR = 300,
     ROXL = 301,
     ROXR = 302,
     BCC = 303,
     BCS = 304,
     BEQ = 305,
     BGE = 306,
     BGT = 307,
     BHI = 308,
     BLE = 309,
     BLS = 310,
     BLT = 311,
     BMI = 312,
     BNE = 313,
     BPL = 314,
     BVC = 315,
     BVS = 316,
     BSR = 317,
     BRA = 318,
     BCLR = 319,
     BSET = 320,
     BCHG = 321,
     BTST = 322,
     CHK = 323,
     CMP = 324,
     CMPA = 325,
     CMPI = 326,
     CMPM = 327,
     EOR = 328,
     EORI = 329,
     EXG = 330,
     EXT = 331,
     DIVU = 332,
     DIVS = 333,
     MULU = 334,
     MULS = 335,
     DBCC = 336,
     DBCS = 337,
     DBEQ = 338,
     DBF = 339,
     DBGE = 340,
     DBGT = 341,
     DBHI = 342,
     DBLE = 343,
     DBLS = 344,
     DBLT = 345,
     DBMI = 346,
     DBNE = 347,
     DBPL = 348,
     DBT = 349,
     DBVC = 350,
     DBVS = 351,
     SCC = 352,
     SCS = 353,
     SEQ = 354,
     SF = 355,
     SGE = 356,
     SGT = 357,
     SHI = 358,
     SLE = 359,
     SLS = 360,
     SLT = 361,
     SMI = 362,
     SNE = 363,
     SPL = 364,
     ST = 365,
     SVC = 366,
     SVS = 367,
     ILLEGAL = 368,
     NOP = 369,
     RESET = 370,
     RTE = 371,
     RTR = 372,
     RTS = 373,
     TRAPV = 374,
     JMP = 375,
     JSR = 376,
     LEA = 377,
     LINK = 378,
     MOVE = 379,
     MOVEA = 380,
     MOVEM = 381,
     MOVEP = 382,
     MOVEQ = 383,
     CLR = 384,
     NEG = 385,
     NEGX = 386,
     NBCD = 387,
     NOT = 388,
     PEA = 389,
     STOP = 390,
     TAS = 391,
     SWAP = 392,
     TRAP = 393,
     TST = 394,
     UNLK = 395,
     PREDEC = 396,
     POSTINC = 397,
     BSIZE = 398,
     WSIZE = 399,
     LSIZE = 400,
     SSIZE = 401
   };
#endif
/* Tokens.  */
#define A0 258
#define A1 259
#define A2 260
#define A3 261
#define A4 262
#define A5 263
#define A6 264
#define A7 265
#define D0 266
#define D1 267
#define D2 268
#define D3 269
#define D4 270
#define D5 271
#define D6 272
#define D7 273
#define CCR 274
#define SR 275
#define USP 276
#define PC 277
#define NUMBER 278
#define ABCD 279
#define ADD 280
#define ADDA 281
#define ADDI 282
#define ADDQ 283
#define ADDX 284
#define AND 285
#define ANDI 286
#define OR 287
#define ORI 288
#define SBCD 289
#define SUB 290
#define SUBA 291
#define SUBI 292
#define SUBQ 293
#define SUBX 294
#define ASL 295
#define ASR 296
#define LSL 297
#define LSR 298
#define ROL 299
#define ROR 300
#define ROXL 301
#define ROXR 302
#define BCC 303
#define BCS 304
#define BEQ 305
#define BGE 306
#define BGT 307
#define BHI 308
#define BLE 309
#define BLS 310
#define BLT 311
#define BMI 312
#define BNE 313
#define BPL 314
#define BVC 315
#define BVS 316
#define BSR 317
#define BRA 318
#define BCLR 319
#define BSET 320
#define BCHG 321
#define BTST 322
#define CHK 323
#define CMP 324
#define CMPA 325
#define CMPI 326
#define CMPM 327
#define EOR 328
#define EORI 329
#define EXG 330
#define EXT 331
#define DIVU 332
#define DIVS 333
#define MULU 334
#define MULS 335
#define DBCC 336
#define DBCS 337
#define DBEQ 338
#define DBF 339
#define DBGE 340
#define DBGT 341
#define DBHI 342
#define DBLE 343
#define DBLS 344
#define DBLT 345
#define DBMI 346
#define DBNE 347
#define DBPL 348
#define DBT 349
#define DBVC 350
#define DBVS 351
#define SCC 352
#define SCS 353
#define SEQ 354
#define SF 355
#define SGE 356
#define SGT 357
#define SHI 358
#define SLE 359
#define SLS 360
#define SLT 361
#define SMI 362
#define SNE 363
#define SPL 364
#define ST 365
#define SVC 366
#define SVS 367
#define ILLEGAL 368
#define NOP 369
#define RESET 370
#define RTE 371
#define RTR 372
#define RTS 373
#define TRAPV 374
#define JMP 375
#define JSR 376
#define LEA 377
#define LINK 378
#define MOVE 379
#define MOVEA 380
#define MOVEM 381
#define MOVEP 382
#define MOVEQ 383
#define CLR 384
#define NEG 385
#define NEGX 386
#define NBCD 387
#define NOT 388
#define PEA 389
#define STOP 390
#define TAS 391
#define SWAP 392
#define TRAP 393
#define TST 394
#define UNLK 395
#define PREDEC 396
#define POSTINC 397
#define BSIZE 398
#define WSIZE 399
#define LSIZE 400
#define SSIZE 401




/* Copy the first part of user declarations.  */
#line 1 "m68k_parse.y"

/* m68k_parse.c: line assembler for generic m68k_cpu
  
   Copyright (c) 2009-2010 Holger Veit

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   HOLGER VEIT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Holger Veit et al shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Holger Veit et al.

   04-Oct-09    HV      Initial version
*/

#include "m68k_cpu.h"
#include <ctype.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

struct _ea {
	int ea;
	int cnt;
	t_value arg[10];
};
struct _rea {
	int reg;
	struct _ea ea;
};
struct _mask {
	int x;
	int d;
};
struct _brop {
	int opc;
	int len;
};

static int oplen;
static int movemx[] = { 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
				 	    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080 };
static int movemd[] = { 0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001,
					    0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100 };
static int yyrc;
static int yyerrc;
extern int yylex();
static int _genop(t_value arg);
static int _genea(struct _ea arg);
static int _genbr(t_value arg,t_value,int);
static void yyerror(char* s);

#define YYDEBUG 1


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 74 "m68k_parse.y"
{
	int rc;
	int reg;
	int wl;
	int opc;
	struct _ea ea;
	t_value num;
	struct _rea rea;
	struct _mask mask;
	struct _brop brop;
}
/* Line 187 of yacc.c.  */
#line 473 "m68k_parse.tab.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 486 "m68k_parse.tab.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  266
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   928

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  153
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  49
/* YYNRULES -- Number of rules.  */
#define YYNRULES  276
/* YYNRULES -- Number of states.  */
#define YYNSTATES  462

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   401

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,   147,     2,     2,     2,     2,
     151,   152,     2,     2,   148,   150,     2,   149,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     6,     9,    15,    21,    27,    29,    32,
      37,    43,    48,    51,    57,    62,    68,    74,    79,    85,
      90,    95,   100,   105,   109,   111,   114,   119,   125,   131,
     136,   141,   146,   152,   158,   164,   170,   176,   182,   186,
     192,   195,   199,   202,   204,   206,   208,   211,   213,   216,
     219,   222,   225,   228,   231,   234,   237,   240,   243,   246,
     249,   252,   255,   259,   262,   266,   269,   273,   276,   280,
     283,   287,   290,   294,   297,   301,   304,   308,   310,   312,
     314,   316,   318,   320,   322,   324,   326,   328,   330,   332,
     334,   336,   338,   340,   343,   346,   349,   352,   355,   358,
     361,   364,   367,   370,   373,   376,   379,   382,   385,   388,
     390,   392,   394,   396,   399,   401,   404,   407,   410,   412,
     414,   416,   418,   420,   422,   424,   426,   428,   430,   432,
     434,   436,   438,   440,   442,   444,   447,   449,   451,   453,
     455,   457,   459,   461,   463,   465,   467,   469,   471,   473,
     475,   477,   479,   481,   483,   485,   487,   489,   491,   493,
     495,   497,   499,   501,   503,   505,   507,   511,   516,   520,
     524,   528,   532,   534,   536,   538,   540,   542,   544,   546,
     548,   550,   552,   554,   556,   558,   560,   562,   564,   566,
     568,   570,   572,   574,   576,   578,   580,   582,   584,   586,
     588,   592,   594,   596,   600,   604,   606,   608,   610,   612,
     614,   616,   618,   620,   622,   624,   626,   628,   630,   632,
     634,   636,   638,   640,   642,   644,   646,   648,   650,   652,
     654,   656,   658,   660,   662,   664,   666,   668,   670,   672,
     674,   676,   678,   680,   682,   684,   686,   688,   690,   692,
     694,   696,   698,   700,   702,   704,   706,   708,   710,   712,
     714,   716,   718,   722,   726,   730,   736,   745,   754,   759,
     763,   769,   771,   780,   789,   792,   794
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int16 yyrhs[] =
{
     154,     0,    -1,   156,   170,    -1,   157,   171,    -1,   158,
     147,    23,   148,   185,    -1,   160,   147,    23,   148,   182,
      -1,   159,   147,    23,   148,   186,    -1,   161,    -1,   162,
      23,    -1,   163,   173,   148,   185,    -1,   163,   147,    23,
     148,   185,    -1,    68,   183,   148,   173,    -1,   164,   185,
      -1,    69,   176,   184,   148,   173,    -1,   165,   183,   148,
     173,    -1,    70,   175,   184,   148,   172,    -1,    72,   176,
     193,   148,   193,    -1,   166,   173,   148,    23,    -1,    73,
     176,   173,   148,   185,    -1,    75,   173,   148,   173,    -1,
      75,   172,   148,   172,    -1,    75,   172,   148,   173,    -1,
      75,   173,   148,   172,    -1,    76,   175,   173,    -1,   167,
      -1,   168,   187,    -1,   122,   187,   148,   172,    -1,   123,
     172,   148,   147,    23,    -1,   124,   177,   184,   148,   186,
      -1,   124,    20,   148,   185,    -1,   124,    21,   148,   172,
      -1,   124,   172,   148,    21,    -1,   125,   178,   185,   148,
     172,    -1,   126,   175,   179,   148,   189,    -1,   126,   175,
     188,   148,   179,    -1,   127,   175,   173,   148,   195,    -1,
     127,   175,   195,   148,   173,    -1,   128,   147,    23,   148,
     173,    -1,   135,   147,    23,    -1,   155,   175,   184,   148,
     172,    -1,   137,   173,    -1,   138,   147,    23,    -1,   140,
     172,    -1,    26,    -1,    36,    -1,    24,    -1,    29,   176,
      -1,    34,    -1,    39,   176,    -1,    25,   176,    -1,    30,
     176,    -1,    32,   176,    -1,    35,   176,    -1,    27,   176,
      -1,    71,   176,    -1,    37,   176,    -1,    31,   176,    -1,
      74,   176,    -1,    33,   176,    -1,    28,   176,    -1,    38,
     176,    -1,    40,   181,    -1,    40,   176,   169,    -1,    41,
     181,    -1,    41,   176,   169,    -1,    42,   181,    -1,    42,
     176,   169,    -1,    43,   181,    -1,    43,   176,   169,    -1,
      44,   181,    -1,    44,   176,   169,    -1,    45,   181,    -1,
      45,   176,   169,    -1,    46,   181,    -1,    46,   176,   169,
      -1,    47,   181,    -1,    47,   176,   169,    -1,    48,    -1,
      49,    -1,    50,    -1,    51,    -1,    52,    -1,    53,    -1,
      54,    -1,    55,    -1,    56,    -1,    57,    -1,    58,    -1,
      59,    -1,    60,    -1,    61,    -1,    62,    -1,    63,    -1,
      48,   174,    -1,    49,   174,    -1,    50,   174,    -1,    51,
     174,    -1,    52,   174,    -1,    53,   174,    -1,    54,   174,
      -1,    55,   174,    -1,    56,   174,    -1,    57,   174,    -1,
      58,   174,    -1,    59,   174,    -1,    60,   174,    -1,    61,
     174,    -1,    62,   174,    -1,    63,   174,    -1,    66,    -1,
      64,    -1,    65,    -1,    67,    -1,   129,   176,    -1,   132,
      -1,   130,   176,    -1,   131,   176,    -1,   133,   176,    -1,
      97,    -1,    98,    -1,    99,    -1,   100,    -1,   101,    -1,
     102,    -1,   103,    -1,   104,    -1,   105,    -1,   106,    -1,
     107,    -1,   108,    -1,   109,    -1,   110,    -1,   111,    -1,
     112,    -1,   136,    -1,   139,   176,    -1,    78,    -1,    77,
      -1,    80,    -1,    79,    -1,    81,    -1,    82,    -1,    83,
      -1,    85,    -1,    86,    -1,    87,    -1,    88,    -1,    89,
      -1,    90,    -1,    91,    -1,    92,    -1,    93,    -1,    95,
      -1,    96,    -1,    84,    -1,    94,    -1,   113,    -1,   114,
      -1,   115,    -1,   116,    -1,   117,    -1,   118,    -1,   119,
      -1,   120,    -1,   121,    -1,   134,    -1,   173,   148,   173,
      -1,   147,    23,   148,   173,    -1,   190,   148,   190,    -1,
     194,   148,   194,    -1,   173,   148,   182,    -1,   181,   148,
     173,    -1,     3,    -1,     4,    -1,     5,    -1,     6,    -1,
       7,    -1,     8,    -1,     9,    -1,    10,    -1,    11,    -1,
      12,    -1,    13,    -1,    14,    -1,    15,    -1,    16,    -1,
      17,    -1,    18,    -1,   146,    -1,   144,    -1,   145,    -1,
     143,    -1,   144,    -1,   145,    -1,   143,    -1,   144,    -1,
     145,    -1,   144,    -1,   145,    -1,   180,    -1,   180,   149,
     179,    -1,   172,    -1,   173,    -1,   172,   150,   172,    -1,
     173,   150,   173,    -1,   191,    -1,   192,    -1,   193,    -1,
     194,    -1,   195,    -1,   196,    -1,   197,    -1,   198,    -1,
     199,    -1,   200,    -1,   190,    -1,   191,    -1,   192,    -1,
     193,    -1,   194,    -1,   195,    -1,   196,    -1,   197,    -1,
     190,    -1,   192,    -1,   193,    -1,   194,    -1,   195,    -1,
     196,    -1,   197,    -1,   198,    -1,   199,    -1,   200,    -1,
     190,    -1,   181,    -1,   190,    -1,   192,    -1,   193,    -1,
     194,    -1,   195,    -1,   196,    -1,   197,    -1,   185,    -1,
     201,    -1,   192,    -1,   195,    -1,   196,    -1,   197,    -1,
     198,    -1,   199,    -1,   192,    -1,   193,    -1,   195,    -1,
     196,    -1,   197,    -1,   192,    -1,   194,    -1,   195,    -1,
     196,    -1,   197,    -1,   173,    -1,   172,    -1,   151,   172,
     152,    -1,   151,   172,   142,    -1,   141,   172,   152,    -1,
     151,    23,   148,   172,   152,    -1,   151,    23,   148,   172,
     148,   173,   175,   152,    -1,   151,    23,   148,   172,   148,
     172,   175,   152,    -1,   151,    23,   152,   175,    -1,   151,
      23,   152,    -1,   151,    23,   148,    22,   152,    -1,    23,
      -1,   151,    23,   148,    22,   148,   173,   175,   152,    -1,
     151,    23,   148,    22,   148,   172,   175,   152,    -1,   147,
      23,    -1,    19,    -1,    20,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   115,   115,   116,   117,   119,   120,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   163,   164,   167,   168,   169,   170,   174,
     175,   176,   177,   181,   182,   183,   187,   188,   189,   193,
     194,   198,   199,   200,   201,   202,   203,   204,   205,   206,
     207,   208,   209,   210,   211,   212,   213,   217,   218,   219,
     220,   221,   222,   223,   224,   225,   226,   227,   228,   229,
     230,   231,   232,   233,   234,   235,   236,   237,   238,   239,
     240,   241,   242,   243,   244,   245,   246,   247,   248,   252,
     253,   254,   255,   259,   260,   261,   262,   263,   264,   265,
     266,   267,   268,   269,   270,   271,   272,   273,   274,   275,
     276,   277,   278,   279,   280,   281,   285,   286,   287,   288,
     292,   293,   294,   295,   296,   297,   298,   299,   300,   301,
     302,   303,   304,   305,   306,   307,   311,   312,   313,   314,
     315,   316,   317,   321,   322,   323,   326,   327,   330,   331,
     334,   337,   340,   341,   342,   343,   344,   345,   346,   347,
     350,   351,   352,   353,   354,   355,   356,   357,   360,   363,
     364,   367,   368,   369,   372,   373,   374,   377,   378,   381,
     382,   385,   386,   387,   389,   393,   393,   393,   393,   393,
     393,   393,   393,   393,   393,   394,   394,   394,   394,   394,
     394,   394,   394,   395,   395,   395,   395,   395,   395,   395,
     395,   395,   395,   396,   396,   397,   397,   397,   397,   397,
     397,   397,   398,   398,   399,   399,   399,   399,   399,   399,
     400,   400,   400,   400,   400,   401,   401,   401,   401,   401,
     404,   406,   408,   410,   412,   414,   416,   418,   421,   423,
     426,   427,   429,   431,   434,   438,   439
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "A0", "A1", "A2", "A3", "A4", "A5", "A6",
  "A7", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "CCR", "SR", "USP",
  "PC", "NUMBER", "ABCD", "ADD", "ADDA", "ADDI", "ADDQ", "ADDX", "AND",
  "ANDI", "OR", "ORI", "SBCD", "SUB", "SUBA", "SUBI", "SUBQ", "SUBX",
  "ASL", "ASR", "LSL", "LSR", "ROL", "ROR", "ROXL", "ROXR", "BCC", "BCS",
  "BEQ", "BGE", "BGT", "BHI", "BLE", "BLS", "BLT", "BMI", "BNE", "BPL",
  "BVC", "BVS", "BSR", "BRA", "BCLR", "BSET", "BCHG", "BTST", "CHK", "CMP",
  "CMPA", "CMPI", "CMPM", "EOR", "EORI", "EXG", "EXT", "DIVU", "DIVS",
  "MULU", "MULS", "DBCC", "DBCS", "DBEQ", "DBF", "DBGE", "DBGT", "DBHI",
  "DBLE", "DBLS", "DBLT", "DBMI", "DBNE", "DBPL", "DBT", "DBVC", "DBVS",
  "SCC", "SCS", "SEQ", "SF", "SGE", "SGT", "SHI", "SLE", "SLS", "SLT",
  "SMI", "SNE", "SPL", "ST", "SVC", "SVS", "ILLEGAL", "NOP", "RESET",
  "RTE", "RTR", "RTS", "TRAPV", "JMP", "JSR", "LEA", "LINK", "MOVE",
  "MOVEA", "MOVEM", "MOVEP", "MOVEQ", "CLR", "NEG", "NEGX", "NBCD", "NOT",
  "PEA", "STOP", "TAS", "SWAP", "TRAP", "TST", "UNLK", "PREDEC", "POSTINC",
  "BSIZE", "WSIZE", "LSIZE", "SSIZE", "'#'", "','", "'/'", "'-'", "'('",
  "')'", "$accept", "stmt", "arop", "bcdop", "dualop", "immop", "immop2",
  "qop", "shftop", "brop", "btop", "monop", "mdop", "dbop", "direct",
  "jop", "shftarg", "bcdarg", "dualarg", "areg", "dreg", "szs", "szwl",
  "szbwl", "szmv", "szm", "reglist", "regs", "eama", "eaa", "ead", "eaall",
  "eada", "eadas", "eac", "eacai", "eacad", "ea0", "ea1", "ea2", "ea3",
  "ea4", "ea5", "ea6", "ea70", "ea72", "ea73", "ea74", "easr", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,    35,    44,    47,
      45,    40,    41
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,   153,   154,   154,   154,   154,   154,   154,   154,   154,
     154,   154,   154,   154,   154,   154,   154,   154,   154,   154,
     154,   154,   154,   154,   154,   154,   154,   154,   154,   154,
     154,   154,   154,   154,   154,   154,   154,   154,   154,   154,
     154,   154,   154,   155,   155,   156,   156,   156,   156,   157,
     157,   157,   157,   158,   158,   158,   159,   159,   159,   160,
     160,   161,   161,   161,   161,   161,   161,   161,   161,   161,
     161,   161,   161,   161,   161,   161,   161,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   162,
     162,   162,   162,   162,   162,   162,   162,   162,   162,   163,
     163,   163,   163,   164,   164,   164,   164,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   164,   164,   164,   164,
     164,   164,   164,   164,   164,   164,   165,   165,   165,   165,
     166,   166,   166,   166,   166,   166,   166,   166,   166,   166,
     166,   166,   166,   166,   166,   166,   167,   167,   167,   167,
     167,   167,   167,   168,   168,   168,   169,   169,   170,   170,
     171,   171,   172,   172,   172,   172,   172,   172,   172,   172,
     173,   173,   173,   173,   173,   173,   173,   173,   174,   175,
     175,   176,   176,   176,   177,   177,   177,   178,   178,   179,
     179,   180,   180,   180,   180,   181,   181,   181,   181,   181,
     181,   181,   181,   181,   181,   182,   182,   182,   182,   182,
     182,   182,   182,   183,   183,   183,   183,   183,   183,   183,
     183,   183,   183,   184,   184,   185,   185,   185,   185,   185,
     185,   185,   186,   186,   187,   187,   187,   187,   187,   187,
     188,   188,   188,   188,   188,   189,   189,   189,   189,   189,
     190,   191,   192,   193,   194,   195,   196,   196,   197,   197,
     198,   198,   199,   199,   200,   201,   201
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     2,     2,     5,     5,     5,     1,     2,     4,
       5,     4,     2,     5,     4,     5,     5,     4,     5,     4,
       4,     4,     4,     3,     1,     2,     4,     5,     5,     4,
       4,     4,     5,     5,     5,     5,     5,     5,     3,     5,
       2,     3,     2,     1,     1,     1,     2,     1,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     3,     2,     3,     2,     3,     2,     3,     2,
       3,     2,     3,     2,     3,     2,     3,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     1,
       1,     1,     1,     2,     1,     2,     2,     2,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     3,     4,     3,     3,
       3,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       3,     1,     1,     3,     3,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     3,     3,     3,     5,     8,     8,     4,     3,
       5,     1,     8,     8,     2,     1,     1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       0,    45,     0,    43,     0,     0,     0,     0,     0,     0,
       0,    47,     0,    44,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,   110,   111,   109,   112,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   137,   136,   139,   138,   140,   141,
     142,   154,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   155,   152,   153,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     156,   157,   158,   159,   160,   161,   162,   163,   164,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   114,
       0,   165,     0,   134,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     7,     0,     0,     0,     0,
       0,    24,     0,   191,   192,   193,    49,    53,    59,    46,
      50,    56,    51,    58,    52,    55,    60,    48,   172,   173,
     174,   175,   176,   177,   178,   179,   271,     0,     0,     0,
     261,     0,    61,   205,   206,   207,   208,   209,   210,   211,
     212,   213,   214,     0,    63,     0,    65,     0,    67,     0,
      69,     0,    71,     0,    73,     0,    75,   188,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   180,   181,   182,   183,   184,   185,
     186,   187,   260,     0,   223,   224,   225,   226,   227,   228,
     229,   230,   231,   232,     0,   189,   190,     0,    54,     0,
       0,    57,     0,     0,     0,     0,     0,   244,   245,   246,
     247,   248,   249,     0,     0,     0,   194,   195,   196,     0,
       0,   197,   198,     0,     0,     0,     0,   113,   115,   116,
     117,     0,    40,     0,   135,    42,     1,     0,     2,     0,
       0,     3,     0,     0,     0,     0,     0,     8,     0,     0,
       0,    12,   235,   236,   237,   238,   239,   240,   241,     0,
       0,    25,     0,   274,     0,     0,     0,    62,     0,    64,
      66,    68,    70,    72,    74,    76,     0,   234,     0,   233,
       0,     0,     0,     0,     0,     0,    23,     0,     0,     0,
       0,     0,     0,     0,     0,   201,   202,     0,   199,     0,
     250,   251,   252,   253,   254,     0,     0,     0,     0,    38,
      41,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   264,     0,   269,   263,   262,     0,
       0,    11,     0,     0,     0,     0,     0,    20,    21,    22,
      19,    26,     0,    29,    30,    31,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   168,   169,
     170,   215,   216,   217,   218,   219,   220,   221,   222,   171,
       0,     0,     0,     0,     9,     0,    14,    17,     0,     0,
     268,     0,   166,    13,    15,    16,    18,    27,   275,   276,
     242,    28,   243,    32,   203,   204,     0,    33,   255,   256,
     257,   258,   259,   200,    34,     0,    35,    36,    37,    39,
       4,     6,     5,    10,     0,   270,     0,   265,   167,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   273,   272,
     267,   266
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   297,   268,   271,   160,
     212,   188,   227,   136,   250,   253,   327,   328,   307,   390,
     213,   308,   420,   421,   236,   329,   427,   282,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   422
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -343
static const yytype_int16 yypact[] =
{
     675,  -343,  -126,  -343,  -126,  -126,  -126,  -126,  -126,  -126,
    -126,  -343,  -126,  -343,  -126,  -126,  -126,   456,   456,   456,
     456,   456,   456,   456,   456,  -139,  -139,  -139,  -139,  -139,
    -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,  -139,
    -139,  -343,  -343,  -343,  -343,   477,  -126,  -107,  -126,  -126,
    -126,  -126,   626,  -107,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,   -20,
     642,   141,  -105,  -107,  -107,  -137,  -126,  -126,  -126,  -343,
    -126,  -343,   -82,  -343,   646,   -80,  -126,   642,    72,  -107,
     557,    18,   -72,   -65,   -50,  -343,    78,    31,    76,   477,
     646,  -343,   -20,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,   642,    85,   203,
    -343,   236,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,   236,  -343,   236,  -343,   236,  -343,   236,
    -343,   236,  -343,   236,  -343,   236,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,   -37,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,    18,  -343,  -343,    18,  -343,   -42,
     646,  -343,   -36,   -35,   646,   203,   -32,  -343,  -343,  -343,
    -343,  -343,  -343,   -29,   -27,   -19,  -343,  -343,  -343,   -13,
      18,  -343,  -343,    76,   538,    41,    92,  -343,  -343,  -343,
    -343,   100,  -343,   113,  -343,  -343,  -343,    18,  -343,   -10,
      -9,  -343,    -8,    -7,   119,   120,   129,  -343,   131,     8,
     581,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,    15,
      16,  -343,    19,  -343,  -143,  -138,   149,  -343,    25,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,   646,  -343,    27,  -343,
      29,   642,    32,    33,   626,   626,  -343,    22,   642,    37,
      76,   642,   155,    39,    40,    35,    36,    43,    45,    47,
    -343,  -343,  -343,  -343,  -343,   166,    51,    54,    57,  -343,
    -343,    66,   646,    77,   515,   646,    71,    73,    74,    83,
      76,   -84,   646,   197,  -343,   605,  -107,  -343,  -343,    84,
     646,  -343,   646,   642,    93,   -42,    76,  -343,  -343,  -343,
    -343,  -343,   211,  -343,  -343,  -343,   546,   642,   642,   646,
    -135,   626,   626,    90,    88,   646,   646,   642,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
      76,   546,   515,    76,  -343,   642,  -343,  -343,   -68,   -67,
    -343,   646,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,   581,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,   642,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,   626,  -343,   626,  -343,  -343,    91,
    -107,  -107,  -107,  -107,   104,   106,   107,   109,  -343,  -343,
    -343,  -343
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,  -343,
    -343,  -343,  -343,  -343,  -343,  -343,   248,  -343,  -343,   -39,
     -51,   811,   -53,   812,  -343,  -343,  -276,  -343,   559,  -162,
     115,  -150,  -120,  -159,   128,  -343,  -343,   -34,  -342,   -25,
     108,   137,   -30,    38,    69,    26,    28,   -33,  -343
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint16 yytable[] =
{
     234,   233,   392,   156,   357,   355,   157,   187,   281,   356,
     256,   214,   223,   232,   358,   218,   426,   133,   134,   135,
     215,   148,   149,   150,   151,   152,   153,   154,   155,   204,
     205,   206,   207,   208,   209,   210,   211,   225,   226,   251,
     252,   156,   204,   205,   206,   207,   208,   209,   210,   211,
     254,   255,   204,   205,   206,   207,   208,   209,   210,   211,
     392,   243,   249,   262,   405,   261,   267,   263,   356,   238,
     272,   221,   266,   222,   237,   274,   279,   310,   265,   290,
     444,   446,   275,   219,   445,   447,   269,   204,   205,   206,
     207,   208,   209,   210,   211,   214,   223,   276,   286,   218,
     323,   277,   238,   283,   215,   433,   434,   237,   293,   311,
     298,   306,   314,   315,   220,   338,   318,   341,   292,   319,
     295,   320,   298,   339,   298,   241,   298,   242,   298,   321,
     298,   235,   298,   324,   298,   322,   340,   239,   342,   343,
     344,   345,   346,   347,   148,   149,   150,   151,   152,   153,
     154,   155,   348,   216,   349,   221,   350,   222,   241,   157,
     242,   244,   245,   352,   353,   158,   287,   219,   240,   159,
     239,   354,   359,   360,   358,   362,   375,   363,   278,   313,
     365,   366,   217,   316,   372,   378,   379,   376,   377,   383,
     309,   380,   335,   309,   381,   382,   317,   288,   220,   384,
     373,   240,   385,   326,   336,   386,   148,   149,   150,   151,
     152,   153,   154,   155,   387,   325,   309,   157,   157,   400,
     407,   401,   402,   286,   332,   337,   294,   280,   283,   330,
     404,   403,   411,   309,   417,   357,   284,   216,   435,   335,
     442,   295,   441,   447,   289,     0,   416,   204,   205,   206,
     207,   208,   209,   210,   211,   361,   458,   270,   459,   460,
     291,   461,     0,   368,   370,   285,   217,     0,     0,     0,
       0,     0,   364,     0,     0,   367,   369,     0,     0,   371,
     440,     0,   374,   443,   246,   247,   248,     0,     0,     0,
     286,   287,   333,     0,   399,   283,     0,     0,     0,     0,
       0,   406,     0,   410,     0,     0,     0,     0,   388,   412,
     391,   413,     0,     0,   396,     0,   409,     0,     0,   393,
     286,     0,   288,   334,   414,   283,     0,     0,   425,     0,
     326,   326,     0,     0,   437,   438,   286,   312,   423,   424,
       0,   283,   325,   325,     0,     0,   286,     0,   439,     0,
     430,   283,     0,     0,   436,   428,     0,     0,   287,     0,
     448,   284,   331,     0,     0,     0,   409,     0,   391,     0,
     286,   286,   396,   286,     0,   283,   283,   393,   283,     0,
       0,     0,   397,   296,     0,     0,     0,   317,   287,   288,
     285,     0,     0,   451,     0,   453,   449,   454,   455,   456,
     457,     0,     0,     0,   287,   450,     0,   452,     0,     0,
       0,     0,     0,   398,   287,     0,     0,     0,   431,   288,
       0,   299,     0,   300,     0,   301,     0,   302,   284,   303,
       0,   304,     0,   305,     0,   288,     0,     0,   287,   287,
     397,   287,     0,     0,     0,   288,     0,     0,     0,   432,
       0,     0,   394,     0,     0,     0,     0,   285,   284,   148,
     149,   150,   151,   152,   153,   154,   155,     0,     0,   288,
     288,   398,   288,   415,   284,     0,     0,     0,     0,   156,
     389,   395,     0,     0,   284,     0,     0,   285,   204,   205,
     206,   207,   208,   209,   210,   211,     0,     0,     0,     0,
     156,     0,     0,   285,     0,     0,     0,     0,   284,   284,
     394,   284,     0,   285,     0,     0,     0,   429,   148,   149,
     150,   151,   152,   153,   154,   155,   204,   205,   206,   207,
     208,   209,   210,   211,     0,     0,     0,   285,   285,   395,
     285,   148,   149,   150,   151,   152,   153,   154,   155,   204,
     205,   206,   207,   208,   209,   210,   211,   204,   205,   206,
     207,   208,   209,   210,   211,   418,   419,     0,   204,   205,
     206,   207,   208,   209,   210,   211,   162,   174,   176,   178,
     180,   182,   184,   186,   148,   149,   150,   151,   152,   153,
     154,   155,     0,     0,     0,     0,     0,   157,     0,   133,
     134,   135,     0,   158,   351,     0,     0,   159,   148,   149,
     150,   151,   152,   153,   154,   155,     0,     0,   157,     0,
       0,     0,     0,     0,   158,     0,     0,   408,   159,   148,
     149,   150,   151,   152,   153,   154,   155,   204,   205,   206,
     207,   208,   209,   210,   211,   148,   149,   150,   151,   152,
     153,   154,   155,     0,     0,     0,   157,   204,   205,   206,
     207,   208,   209,   210,   211,     0,   280,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     273,     0,     0,     0,     0,     0,     0,   157,     0,   280,
       0,     0,     0,     0,     0,     0,     0,   280,   157,     1,
       2,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   137,   138,   139,   140,
     141,   142,   143,     0,   144,     0,   145,   146,   147,   161,
     173,   175,   177,   179,   181,   183,   185,   189,   190,   191,
     192,   193,   194,   195,   196,   197,   198,   199,   200,   201,
     202,   203,     0,     0,     0,     0,     0,     0,   224,     0,
     228,   229,   230,   231,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   257,   258,
     259,     0,   260,     0,     0,     0,     0,     0,   264
};

static const yytype_int16 yycheck[] =
{
      53,    52,   344,    23,   142,   148,   141,   146,   128,   152,
     147,    45,    45,    52,   152,    45,   151,   143,   144,   145,
      45,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,   144,   145,   144,
     145,    23,    11,    12,    13,    14,    15,    16,    17,    18,
     103,   104,    11,    12,    13,    14,    15,    16,    17,    18,
     402,   100,   101,   114,   148,   147,   119,   147,   152,    99,
     121,    45,     0,    45,    99,   147,   127,   227,   117,   130,
     148,   148,   147,    45,   152,   152,   120,    11,    12,    13,
      14,    15,    16,    17,    18,   129,   129,   147,   128,   129,
     250,    23,   132,   128,   129,   381,   382,   132,    23,   151,
     161,   148,   148,   148,    45,    23,   148,   267,   157,   148,
     159,   148,   173,    23,   175,    99,   177,    99,   179,   148,
     181,   151,   183,   253,   185,   148,    23,    99,   148,   148,
     148,   148,    23,    23,     3,     4,     5,     6,     7,     8,
       9,    10,    23,    45,    23,   129,   148,   129,   132,   141,
     132,    20,    21,   148,   148,   147,   128,   129,    99,   151,
     132,   152,    23,   148,   152,   148,    21,   148,   147,   230,
     148,   148,    45,   234,   147,   150,   150,   148,   148,    23,
     224,   148,   151,   227,   149,   148,   235,   128,   129,   148,
     320,   132,   148,   254,   255,   148,     3,     4,     5,     6,
       7,     8,     9,    10,   148,   254,   250,   141,   141,   148,
      23,   148,   148,   253,   254,   255,    23,   151,   253,   254,
     350,   148,   148,   267,    23,   142,   128,   129,   148,   151,
     402,   280,   401,   152,   129,    -1,   366,    11,    12,    13,
      14,    15,    16,    17,    18,   306,   152,   120,   152,   152,
     132,   152,    -1,   314,   315,   128,   129,    -1,    -1,    -1,
      -1,    -1,   311,    -1,    -1,   314,   315,    -1,    -1,   318,
     400,    -1,   321,   403,   143,   144,   145,    -1,    -1,    -1,
     320,   253,   254,    -1,   345,   320,    -1,    -1,    -1,    -1,
      -1,   352,    -1,   356,    -1,    -1,    -1,    -1,   342,   360,
     344,   362,    -1,    -1,   344,    -1,   355,    -1,    -1,   344,
     350,    -1,   253,   254,   363,   350,    -1,    -1,   379,    -1,
     381,   382,    -1,    -1,   385,   386,   366,   229,   377,   378,
      -1,   366,   381,   382,    -1,    -1,   376,    -1,   387,    -1,
     380,   376,    -1,    -1,   384,   380,    -1,    -1,   320,    -1,
     411,   253,   254,    -1,    -1,    -1,   405,    -1,   402,    -1,
     400,   401,   402,   403,    -1,   400,   401,   402,   403,    -1,
      -1,    -1,   344,   147,    -1,    -1,    -1,   426,   350,   320,
     253,    -1,    -1,   444,    -1,   446,   435,   450,   451,   452,
     453,    -1,    -1,    -1,   366,   444,    -1,   446,    -1,    -1,
      -1,    -1,    -1,   344,   376,    -1,    -1,    -1,   380,   350,
      -1,   173,    -1,   175,    -1,   177,    -1,   179,   320,   181,
      -1,   183,    -1,   185,    -1,   366,    -1,    -1,   400,   401,
     402,   403,    -1,    -1,    -1,   376,    -1,    -1,    -1,   380,
      -1,    -1,   344,    -1,    -1,    -1,    -1,   320,   350,     3,
       4,     5,     6,     7,     8,     9,    10,    -1,    -1,   400,
     401,   402,   403,   365,   366,    -1,    -1,    -1,    -1,    23,
     343,   344,    -1,    -1,   376,    -1,    -1,   350,    11,    12,
      13,    14,    15,    16,    17,    18,    -1,    -1,    -1,    -1,
      23,    -1,    -1,   366,    -1,    -1,    -1,    -1,   400,   401,
     402,   403,    -1,   376,    -1,    -1,    -1,   380,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    -1,    -1,    -1,   400,   401,   402,
     403,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    -1,    11,    12,
      13,    14,    15,    16,    17,    18,    17,    18,    19,    20,
      21,    22,    23,    24,     3,     4,     5,     6,     7,     8,
       9,    10,    -1,    -1,    -1,    -1,    -1,   141,    -1,   143,
     144,   145,    -1,   147,    23,    -1,    -1,   151,     3,     4,
       5,     6,     7,     8,     9,    10,    -1,    -1,   141,    -1,
      -1,    -1,    -1,    -1,   147,    -1,    -1,    22,   151,     3,
       4,     5,     6,     7,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,     3,     4,     5,     6,     7,
       8,     9,    10,    -1,    -1,    -1,   141,    11,    12,    13,
      14,    15,    16,    17,    18,    -1,   151,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     121,    -1,    -1,    -1,    -1,    -1,    -1,   141,    -1,   151,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   151,   141,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,     4,     5,     6,     7,
       8,     9,    10,    -1,    12,    -1,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    -1,    -1,    -1,    -1,    -1,    -1,    46,    -1,
      48,    49,    50,    51,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   106,   107,
     108,    -1,   110,    -1,    -1,    -1,    -1,    -1,   116
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   154,   155,
     156,   157,   158,   159,   160,   161,   162,   163,   164,   165,
     166,   167,   168,   143,   144,   145,   176,   176,   176,   176,
     176,   176,   176,   176,   176,   176,   176,   176,     3,     4,
       5,     6,     7,     8,     9,    10,    23,   141,   147,   151,
     172,   176,   181,   191,   192,   193,   194,   195,   196,   197,
     198,   199,   200,   176,   181,   176,   181,   176,   181,   176,
     181,   176,   181,   176,   181,   176,   181,   146,   174,   174,
     174,   174,   174,   174,   174,   174,   174,   174,   174,   174,
     174,   174,   174,   174,    11,    12,    13,    14,    15,    16,
      17,    18,   173,   183,   190,   192,   193,   194,   195,   196,
     197,   198,   199,   200,   176,   144,   145,   175,   176,   176,
     176,   176,   172,   173,   175,   151,   187,   192,   195,   196,
     197,   198,   199,   172,    20,    21,   143,   144,   145,   172,
     177,   144,   145,   178,   175,   175,   147,   176,   176,   176,
     176,   147,   173,   147,   176,   172,     0,   175,   170,   190,
     194,   171,   173,   181,   147,   147,   147,    23,   147,   173,
     151,   185,   190,   192,   193,   194,   195,   196,   197,   183,
     173,   187,   172,    23,    23,   172,   147,   169,   173,   169,
     169,   169,   169,   169,   169,   169,   148,   181,   184,   190,
     184,   151,   193,   173,   148,   148,   173,   172,   148,   148,
     148,   148,   148,   184,   185,   172,   173,   179,   180,   188,
     192,   193,   195,   196,   197,   151,   173,   195,    23,    23,
      23,   184,   148,   148,   148,   148,    23,    23,    23,    23,
     148,    23,   148,   148,   152,   148,   152,   142,   152,    23,
     148,   173,   148,   148,   172,   148,   148,   172,   173,   172,
     173,   172,   147,   185,   172,    21,   148,   148,   150,   150,
     148,   149,   148,    23,   148,   148,   148,   148,   190,   194,
     182,   190,   191,   192,   193,   194,   195,   196,   197,   173,
     148,   148,   148,   148,   185,   148,   173,    23,    22,   172,
     175,   148,   173,   173,   172,   193,   185,    23,    19,    20,
     185,   186,   201,   172,   172,   173,   151,   189,   192,   194,
     195,   196,   197,   179,   179,   148,   195,   173,   173,   172,
     185,   186,   182,   185,   148,   152,   148,   152,   173,   172,
     172,   173,   172,   173,   175,   175,   175,   175,   152,   152,
     152,   152
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (YYLEX_PARAM)
#else
# define YYLEX yylex ()
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yyrule)
    YYSTYPE *yyvsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yymsg, yytype, yyvaluep)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  YYUSE (yyvaluep);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */



/* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;



/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  
  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;

  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 115 "m68k_parse.y"
    { _genop((yyvsp[(1) - (2)].opc) | (yyvsp[(2) - (2)].opc)); yyrc = -1; ;}
    break;

  case 3:
#line 116 "m68k_parse.y"
    { _genop((yyvsp[(1) - (2)].opc) | (yyvsp[(2) - (2)].rea).reg | (yyvsp[(2) - (2)].rea).ea.ea); yyrc = _genea((yyvsp[(2) - (2)].rea).ea) -1; ;}
    break;

  case 4:
#line 117 "m68k_parse.y"
    { _genop((yyvsp[(1) - (5)].opc) | (yyvsp[(5) - (5)].ea).ea); if (oplen==0) { _genop((yyvsp[(3) - (5)].num) & 0xff); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 3; }
		else if (oplen==1) { _genop((yyvsp[(3) - (5)].num)); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 3; } else { _genop((yyvsp[(3) - (5)].num)>>16); _genop((yyvsp[(3) - (5)].num) & 0xffff); yyrc = _genea((yyvsp[(5) - (5)].ea))-5; } ;}
    break;

  case 5:
#line 119 "m68k_parse.y"
    { _genop((yyvsp[(1) - (5)].opc) | (((yyvsp[(3) - (5)].num)&7)<<9) | (yyvsp[(5) - (5)].ea).ea); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 1; ;}
    break;

  case 6:
#line 120 "m68k_parse.y"
    { _genop((yyvsp[(1) - (5)].opc) | (yyvsp[(5) - (5)].ea).ea); if (oplen==0) { _genop((yyvsp[(3) - (5)].num) & 0xff); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 3; }
		else if (oplen==1) { _genop((yyvsp[(3) - (5)].num)); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 3; } else { _genop((yyvsp[(3) - (5)].num)>>16); _genop((yyvsp[(3) - (5)].num) & 0xffff); yyrc = _genea((yyvsp[(5) - (5)].ea))-5; } ;}
    break;

  case 7:
#line 122 "m68k_parse.y"
    { _genop((yyvsp[(1) - (1)].rea).reg); if (((yyvsp[(1) - (1)].rea).reg&0xc0)==0xc0) yyrc = _genea((yyvsp[(1) - (1)].rea).ea) - 1; else { yyrc = -1; } ;}
    break;

  case 8:
#line 123 "m68k_parse.y"
    { yyrc = _genbr((yyvsp[(1) - (2)].brop).opc,(yyvsp[(2) - (2)].num),(yyvsp[(1) - (2)].brop).len) - 1; ;}
    break;

  case 9:
#line 124 "m68k_parse.y"
    { _genop((yyvsp[(1) - (4)].opc) | ((yyvsp[(2) - (4)].reg)<<9) | 0x100 | (yyvsp[(4) - (4)].ea).ea); yyrc = _genea((yyvsp[(4) - (4)].ea)) - 1; ;}
    break;

  case 10:
#line 125 "m68k_parse.y"
    { _genop((yyvsp[(1) - (5)].opc) | 0x0800 | (yyvsp[(5) - (5)].ea).ea); _genop((yyvsp[(3) - (5)].num)); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 3; ;}
    break;

  case 11:
#line 126 "m68k_parse.y"
    { _genop(0x4180 | ((yyvsp[(4) - (4)].reg)<<9) | (yyvsp[(2) - (4)].ea).ea); yyrc = _genea((yyvsp[(2) - (4)].ea)) - 1; ;}
    break;

  case 12:
#line 127 "m68k_parse.y"
    { _genop((yyvsp[(1) - (2)].opc) | (yyvsp[(2) - (2)].ea).ea); yyrc = _genea((yyvsp[(2) - (2)].ea)) - 1; ;}
    break;

  case 13:
#line 128 "m68k_parse.y"
    { _genop(0xb000 | ((yyvsp[(2) - (5)].wl)<<6) | ((yyvsp[(5) - (5)].reg)<<9) | (yyvsp[(3) - (5)].ea).ea); yyrc = _genea((yyvsp[(3) - (5)].ea)) - 1; ;}
    break;

  case 14:
#line 129 "m68k_parse.y"
    { _genop((yyvsp[(1) - (4)].opc) | ((yyvsp[(4) - (4)].reg)<<9) | (yyvsp[(2) - (4)].ea).ea); yyrc = _genea((yyvsp[(2) - (4)].ea)) - 1; ;}
    break;

  case 15:
#line 130 "m68k_parse.y"
    { _genop(0xb0c0 | ((yyvsp[(2) - (5)].wl)<<8) | ((yyvsp[(5) - (5)].reg)<<9) | (yyvsp[(3) - (5)].ea).ea); yyrc = _genea((yyvsp[(3) - (5)].ea)) - 1; ;}
    break;

  case 16:
#line 131 "m68k_parse.y"
    { _genop(0xb108 | ((yyvsp[(5) - (5)].ea).ea<<9) | ((yyvsp[(2) - (5)].wl)<<6) | (yyvsp[(3) - (5)].ea).ea); yyrc = -1; ;}
    break;

  case 17:
#line 132 "m68k_parse.y"
    { yyrc = _genbr((yyvsp[(1) - (4)].opc) | (yyvsp[(2) - (4)].reg), (yyvsp[(4) - (4)].num), 1) - 1; ;}
    break;

  case 18:
#line 133 "m68k_parse.y"
    { _genop(0xb000 | ((yyvsp[(2) - (5)].wl) << 6) | 0x100 | (yyvsp[(5) - (5)].ea).ea); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 1; ;}
    break;

  case 19:
#line 134 "m68k_parse.y"
    { _genop(0xc140 | ((yyvsp[(2) - (4)].reg)<<9) | (yyvsp[(4) - (4)].reg)); yyrc = -1; ;}
    break;

  case 20:
#line 135 "m68k_parse.y"
    { _genop(0xc148 | ((yyvsp[(2) - (4)].reg)<<9) | (yyvsp[(4) - (4)].reg)); yyrc = -1; ;}
    break;

  case 21:
#line 136 "m68k_parse.y"
    { _genop(0xc188 | ((yyvsp[(4) - (4)].reg)<<9) | (yyvsp[(2) - (4)].reg)); yyrc = -1; ;}
    break;

  case 22:
#line 137 "m68k_parse.y"
    { _genop(0xc188 | ((yyvsp[(2) - (4)].reg)<<9) | (yyvsp[(4) - (4)].reg)); yyrc = -1; ;}
    break;

  case 23:
#line 138 "m68k_parse.y"
    { _genop(0x4840 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].reg)); yyrc = -1; ;}
    break;

  case 24:
#line 139 "m68k_parse.y"
    { _genop((yyvsp[(1) - (1)].opc)); yyrc = -1; ;}
    break;

  case 25:
#line 140 "m68k_parse.y"
    { _genop((yyvsp[(1) - (2)].opc) | (yyvsp[(2) - (2)].ea).ea); yyrc = _genea((yyvsp[(2) - (2)].ea)) -1; ;}
    break;

  case 26:
#line 141 "m68k_parse.y"
    { _genop(0x41c0 | (yyvsp[(2) - (4)].ea).ea); yyrc = _genea((yyvsp[(2) - (4)].ea)) - 1; ;}
    break;

  case 27:
#line 142 "m68k_parse.y"
    { _genop(0x4e50 | (yyvsp[(2) - (5)].reg)); _genop((yyvsp[(5) - (5)].num)); yyrc = -3; ;}
    break;

  case 28:
#line 143 "m68k_parse.y"
    { if ((yyvsp[(5) - (5)].ea).ea==074) { _genop(0x44c0 | ((yyvsp[(5) - (5)].ea).cnt==1?0x0200:0x0000) | (yyvsp[(3) - (5)].ea).ea); yyrc = _genea((yyvsp[(3) - (5)].ea)) - 1; }
							    else { int tmp = (((yyvsp[(5) - (5)].ea).ea&070)>>3)|(((yyvsp[(5) - (5)].ea).ea&7)<<3); _genop(0x0000 | ((yyvsp[(2) - (5)].wl)<<12) | (tmp<<6) | (yyvsp[(3) - (5)].ea).ea);
    	                           	   yyrc = _genea((yyvsp[(3) - (5)].ea)) - 1; yyrc += _genea((yyvsp[(5) - (5)].ea)); } ;}
    break;

  case 29:
#line 146 "m68k_parse.y"
    { _genop(0x40c0 | (yyvsp[(4) - (4)].ea).ea); yyrc = _genea((yyvsp[(4) - (4)].ea)) - 1; ;}
    break;

  case 30:
#line 147 "m68k_parse.y"
    { _genop(0x4e68 | (yyvsp[(4) - (4)].reg));  yyrc = -1; ;}
    break;

  case 31:
#line 148 "m68k_parse.y"
    { _genop(0x4e60 | (yyvsp[(2) - (4)].reg));  yyrc = -1; ;}
    break;

  case 32:
#line 149 "m68k_parse.y"
    { _genop(0x0040 | ((yyvsp[(2) - (5)].wl)<<12) | ((yyvsp[(5) - (5)].reg)<<9) | (yyvsp[(3) - (5)].ea).ea); yyrc = _genea((yyvsp[(3) - (5)].ea)) - 1; ;}
    break;

  case 33:
#line 150 "m68k_parse.y"
    { _genop(0x4880 | ((yyvsp[(2) - (5)].wl)<<6) | (yyvsp[(5) - (5)].ea).ea); _genop(((yyvsp[(5) - (5)].ea).ea&070)==040 ? (yyvsp[(3) - (5)].mask).d : (yyvsp[(3) - (5)].mask).x); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 3; ;}
    break;

  case 34:
#line 151 "m68k_parse.y"
    { _genop(0x4c80 | ((yyvsp[(2) - (5)].wl)<<6) | (yyvsp[(3) - (5)].ea).ea); _genop((yyvsp[(5) - (5)].mask).x); yyrc = _genea((yyvsp[(3) - (5)].ea)) - 3; ;}
    break;

  case 35:
#line 152 "m68k_parse.y"
    { _genop(0x0108 | ((yyvsp[(3) - (5)].reg)<<9) | ((yyvsp[(2) - (5)].wl)<<6) | ((yyvsp[(5) - (5)].ea).ea & 7)); yyrc = _genea((yyvsp[(5) - (5)].ea)) - 1; ;}
    break;

  case 36:
#line 153 "m68k_parse.y"
    { _genop(0x0188 | ((yyvsp[(5) - (5)].reg)<<9) | ((yyvsp[(2) - (5)].wl)<<6) | ((yyvsp[(3) - (5)].ea).ea & 7)); yyrc = _genea((yyvsp[(3) - (5)].ea)) - 1; ;}
    break;

  case 37:
#line 154 "m68k_parse.y"
    { _genop(0x7000 | ((yyvsp[(5) - (5)].reg)<<9) | ((yyvsp[(3) - (5)].num)&0xff)); yyrc = -1; ;}
    break;

  case 38:
#line 155 "m68k_parse.y"
    { _genop(0x4e72); yyrc = _genop((yyvsp[(3) - (3)].num)&0xffff) - 1; ;}
    break;

  case 39:
#line 156 "m68k_parse.y"
    { _genop((yyvsp[(1) - (5)].opc) | ((yyvsp[(5) - (5)].reg)<<9) | ((yyvsp[(2) - (5)].wl)<<8) | (yyvsp[(3) - (5)].ea).ea); yyrc = _genea((yyvsp[(3) - (5)].ea)) - 1; ;}
    break;

  case 40:
#line 157 "m68k_parse.y"
    { _genop(0x4840 | (yyvsp[(2) - (2)].reg)); yyrc = -1; ;}
    break;

  case 41:
#line 158 "m68k_parse.y"
    { _genop(0x4e40 | ((yyvsp[(3) - (3)].num) & 0x0f)); yyrc = -1; ;}
    break;

  case 42:
#line 159 "m68k_parse.y"
    { _genop(0x4e58 | (yyvsp[(2) - (2)].reg)); yyrc = -1; ;}
    break;

  case 43:
#line 163 "m68k_parse.y"
    { (yyval.opc) = 0xd0c0; ;}
    break;

  case 44:
#line 164 "m68k_parse.y"
    { (yyval.opc) = 0x90c0; ;}
    break;

  case 45:
#line 167 "m68k_parse.y"
    { (yyval.opc) = 0xc100; ;}
    break;

  case 46:
#line 168 "m68k_parse.y"
    { (yyval.opc) = 0xd100 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 47:
#line 169 "m68k_parse.y"
    { (yyval.opc) = 0x8100; ;}
    break;

  case 48:
#line 170 "m68k_parse.y"
    { (yyval.opc) = 0x9100 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 49:
#line 174 "m68k_parse.y"
    { (yyval.opc) = 0xd000 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 50:
#line 175 "m68k_parse.y"
    { (yyval.opc) = 0xc000 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 51:
#line 176 "m68k_parse.y"
    { (yyval.opc) = 0x8000 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 52:
#line 177 "m68k_parse.y"
    { (yyval.opc) = 0x9000 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 53:
#line 181 "m68k_parse.y"
    { (yyval.opc) = 0x0600 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 54:
#line 182 "m68k_parse.y"
    { (yyval.opc) = 0x0c00 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 55:
#line 183 "m68k_parse.y"
    { (yyval.opc) = 0x0400 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 56:
#line 187 "m68k_parse.y"
    { (yyval.opc) = 0x0200 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 57:
#line 188 "m68k_parse.y"
    { (yyval.opc) = 0x0a00 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 58:
#line 189 "m68k_parse.y"
    { (yyval.opc) = 0x0000 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 59:
#line 193 "m68k_parse.y"
    { (yyval.opc) = 0x5000 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 60:
#line 194 "m68k_parse.y"
    { (yyval.opc) = 0x5100 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 61:
#line 198 "m68k_parse.y"
    { (yyval.rea).reg = 0xe1c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 62:
#line 199 "m68k_parse.y"
    { (yyval.rea).reg = 0xe100 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 63:
#line 200 "m68k_parse.y"
    { (yyval.rea).reg = 0xe0c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 64:
#line 201 "m68k_parse.y"
    { (yyval.rea).reg = 0xe000 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 65:
#line 202 "m68k_parse.y"
    { (yyval.rea).reg = 0xe3c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 66:
#line 203 "m68k_parse.y"
    { (yyval.rea).reg = 0xe108 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 67:
#line 204 "m68k_parse.y"
    { (yyval.rea).reg = 0xe2c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 68:
#line 205 "m68k_parse.y"
    { (yyval.rea).reg = 0xe008 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 69:
#line 206 "m68k_parse.y"
    { (yyval.rea).reg = 0xe7c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 70:
#line 207 "m68k_parse.y"
    { (yyval.rea).reg = 0xe118 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 71:
#line 208 "m68k_parse.y"
    { (yyval.rea).reg = 0xe6c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 72:
#line 209 "m68k_parse.y"
    { (yyval.rea).reg = 0xe018 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 73:
#line 210 "m68k_parse.y"
    { (yyval.rea).reg = 0xe5c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 74:
#line 211 "m68k_parse.y"
    { (yyval.rea).reg = 0xe100 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 75:
#line 212 "m68k_parse.y"
    { (yyval.rea).reg = 0xe4c0 | (yyvsp[(2) - (2)].ea).ea; (yyval.rea).ea = (yyvsp[(2) - (2)].ea); ;}
    break;

  case 76:
#line 213 "m68k_parse.y"
    { (yyval.rea).reg = 0xe000 | ((yyvsp[(2) - (3)].wl)<<6) | (yyvsp[(3) - (3)].opc); ;}
    break;

  case 77:
#line 217 "m68k_parse.y"
    { (yyval.brop).opc = 0x6400; (yyval.brop).len = 1; ;}
    break;

  case 78:
#line 218 "m68k_parse.y"
    { (yyval.brop).opc = 0x6500; (yyval.brop).len = 1; ;}
    break;

  case 79:
#line 219 "m68k_parse.y"
    { (yyval.brop).opc = 0x6700; (yyval.brop).len = 1; ;}
    break;

  case 80:
#line 220 "m68k_parse.y"
    { (yyval.brop).opc = 0x6c00; (yyval.brop).len = 1; ;}
    break;

  case 81:
#line 221 "m68k_parse.y"
    { (yyval.brop).opc = 0x6e00; (yyval.brop).len = 1; ;}
    break;

  case 82:
#line 222 "m68k_parse.y"
    { (yyval.brop).opc = 0x6200; (yyval.brop).len = 1; ;}
    break;

  case 83:
#line 223 "m68k_parse.y"
    { (yyval.brop).opc = 0x6f00; (yyval.brop).len = 1; ;}
    break;

  case 84:
#line 224 "m68k_parse.y"
    { (yyval.brop).opc = 0x6300; (yyval.brop).len = 1; ;}
    break;

  case 85:
#line 225 "m68k_parse.y"
    { (yyval.brop).opc = 0x6d00; (yyval.brop).len = 1; ;}
    break;

  case 86:
#line 226 "m68k_parse.y"
    { (yyval.brop).opc = 0x6b00; (yyval.brop).len = 1; ;}
    break;

  case 87:
#line 227 "m68k_parse.y"
    { (yyval.brop).opc = 0x6600; (yyval.brop).len = 1; ;}
    break;

  case 88:
#line 228 "m68k_parse.y"
    { (yyval.brop).opc = 0x6a00; (yyval.brop).len = 1; ;}
    break;

  case 89:
#line 229 "m68k_parse.y"
    { (yyval.brop).opc = 0x6800; (yyval.brop).len = 1; ;}
    break;

  case 90:
#line 230 "m68k_parse.y"
    { (yyval.brop).opc = 0x6900; (yyval.brop).len = 1; ;}
    break;

  case 91:
#line 231 "m68k_parse.y"
    { (yyval.brop).opc = 0x6100; (yyval.brop).len = 1; ;}
    break;

  case 92:
#line 232 "m68k_parse.y"
    { (yyval.brop).opc = 0x6000; (yyval.brop).len = 1; ;}
    break;

  case 93:
#line 233 "m68k_parse.y"
    { (yyval.brop).opc = 0x6400; (yyval.brop).len = 0; ;}
    break;

  case 94:
#line 234 "m68k_parse.y"
    { (yyval.brop).opc = 0x6500; (yyval.brop).len = 0; ;}
    break;

  case 95:
#line 235 "m68k_parse.y"
    { (yyval.brop).opc = 0x6700; (yyval.brop).len = 0; ;}
    break;

  case 96:
#line 236 "m68k_parse.y"
    { (yyval.brop).opc = 0x6c00; (yyval.brop).len = 0; ;}
    break;

  case 97:
#line 237 "m68k_parse.y"
    { (yyval.brop).opc = 0x6e00; (yyval.brop).len = 0; ;}
    break;

  case 98:
#line 238 "m68k_parse.y"
    { (yyval.brop).opc = 0x6200; (yyval.brop).len = 0; ;}
    break;

  case 99:
#line 239 "m68k_parse.y"
    { (yyval.brop).opc = 0x6f00; (yyval.brop).len = 0; ;}
    break;

  case 100:
#line 240 "m68k_parse.y"
    { (yyval.brop).opc = 0x6300; (yyval.brop).len = 0; ;}
    break;

  case 101:
#line 241 "m68k_parse.y"
    { (yyval.brop).opc = 0x6d00; (yyval.brop).len = 0; ;}
    break;

  case 102:
#line 242 "m68k_parse.y"
    { (yyval.brop).opc = 0x6b00; (yyval.brop).len = 0; ;}
    break;

  case 103:
#line 243 "m68k_parse.y"
    { (yyval.brop).opc = 0x6600; (yyval.brop).len = 0; ;}
    break;

  case 104:
#line 244 "m68k_parse.y"
    { (yyval.brop).opc = 0x6a00; (yyval.brop).len = 0; ;}
    break;

  case 105:
#line 245 "m68k_parse.y"
    { (yyval.brop).opc = 0x6800; (yyval.brop).len = 0; ;}
    break;

  case 106:
#line 246 "m68k_parse.y"
    { (yyval.brop).opc = 0x6900; (yyval.brop).len = 0; ;}
    break;

  case 107:
#line 247 "m68k_parse.y"
    { (yyval.brop).opc = 0x6100; (yyval.brop).len = 0; ;}
    break;

  case 108:
#line 248 "m68k_parse.y"
    { (yyval.brop).opc = 0x6000; (yyval.brop).len = 0; ;}
    break;

  case 109:
#line 252 "m68k_parse.y"
    { (yyval.opc) = 0x0040; ;}
    break;

  case 110:
#line 253 "m68k_parse.y"
    { (yyval.opc) = 0x0080; ;}
    break;

  case 111:
#line 254 "m68k_parse.y"
    { (yyval.opc) = 0x00c0; ;}
    break;

  case 112:
#line 255 "m68k_parse.y"
    { (yyval.opc) = 0x0000; ;}
    break;

  case 113:
#line 259 "m68k_parse.y"
    { (yyval.opc) = 0x4200 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 114:
#line 260 "m68k_parse.y"
    { (yyval.opc) = 0x4800; ;}
    break;

  case 115:
#line 261 "m68k_parse.y"
    { (yyval.opc) = 0x4400 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 116:
#line 262 "m68k_parse.y"
    { (yyval.opc) = 0x4000 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 117:
#line 263 "m68k_parse.y"
    { (yyval.opc) = 0x4600 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 118:
#line 264 "m68k_parse.y"
    { (yyval.opc) = 0x54c0; ;}
    break;

  case 119:
#line 265 "m68k_parse.y"
    { (yyval.opc) = 0x55c0; ;}
    break;

  case 120:
#line 266 "m68k_parse.y"
    { (yyval.opc) = 0x57c0; ;}
    break;

  case 121:
#line 267 "m68k_parse.y"
    { (yyval.opc) = 0x51c0; ;}
    break;

  case 122:
#line 268 "m68k_parse.y"
    { (yyval.opc) = 0x5cc0; ;}
    break;

  case 123:
#line 269 "m68k_parse.y"
    { (yyval.opc) = 0x5ec0; ;}
    break;

  case 124:
#line 270 "m68k_parse.y"
    { (yyval.opc) = 0x52c0; ;}
    break;

  case 125:
#line 271 "m68k_parse.y"
    { (yyval.opc) = 0x5fc0; ;}
    break;

  case 126:
#line 272 "m68k_parse.y"
    { (yyval.opc) = 0x53c0; ;}
    break;

  case 127:
#line 273 "m68k_parse.y"
    { (yyval.opc) = 0x5dc0; ;}
    break;

  case 128:
#line 274 "m68k_parse.y"
    { (yyval.opc) = 0x5bc0; ;}
    break;

  case 129:
#line 275 "m68k_parse.y"
    { (yyval.opc) = 0x56c0; ;}
    break;

  case 130:
#line 276 "m68k_parse.y"
    { (yyval.opc) = 0x5ac0; ;}
    break;

  case 131:
#line 277 "m68k_parse.y"
    { (yyval.opc) = 0x50c0; ;}
    break;

  case 132:
#line 278 "m68k_parse.y"
    { (yyval.opc) = 0x58c0; ;}
    break;

  case 133:
#line 279 "m68k_parse.y"
    { (yyval.opc) = 0x59c0; ;}
    break;

  case 134:
#line 280 "m68k_parse.y"
    { (yyval.opc) = 0x4ac0; ;}
    break;

  case 135:
#line 281 "m68k_parse.y"
    { (yyval.opc) = 0x4a00 | ((yyvsp[(2) - (2)].wl)<<6); ;}
    break;

  case 136:
#line 285 "m68k_parse.y"
    { (yyval.opc) = 0x81c0; ;}
    break;

  case 137:
#line 286 "m68k_parse.y"
    { (yyval.opc) = 0x80c0; ;}
    break;

  case 138:
#line 287 "m68k_parse.y"
    { (yyval.opc) = 0xc1c0; ;}
    break;

  case 139:
#line 288 "m68k_parse.y"
    { (yyval.opc) = 0xc0c0; ;}
    break;

  case 140:
#line 292 "m68k_parse.y"
    { (yyval.opc) = 0x54c8; ;}
    break;

  case 141:
#line 293 "m68k_parse.y"
    { (yyval.opc) = 0x55c8; ;}
    break;

  case 142:
#line 294 "m68k_parse.y"
    { (yyval.opc) = 0x57c8; ;}
    break;

  case 143:
#line 295 "m68k_parse.y"
    { (yyval.opc) = 0x5cc8; ;}
    break;

  case 144:
#line 296 "m68k_parse.y"
    { (yyval.opc) = 0x5ec8; ;}
    break;

  case 145:
#line 297 "m68k_parse.y"
    { (yyval.opc) = 0x52c8; ;}
    break;

  case 146:
#line 298 "m68k_parse.y"
    { (yyval.opc) = 0x5fc8; ;}
    break;

  case 147:
#line 299 "m68k_parse.y"
    { (yyval.opc) = 0x53c8; ;}
    break;

  case 148:
#line 300 "m68k_parse.y"
    { (yyval.opc) = 0x5dc8; ;}
    break;

  case 149:
#line 301 "m68k_parse.y"
    { (yyval.opc) = 0x5bc8; ;}
    break;

  case 150:
#line 302 "m68k_parse.y"
    { (yyval.opc) = 0x56c8; ;}
    break;

  case 151:
#line 303 "m68k_parse.y"
    { (yyval.opc) = 0x5ac8; ;}
    break;

  case 152:
#line 304 "m68k_parse.y"
    { (yyval.opc) = 0x58c8; ;}
    break;

  case 153:
#line 305 "m68k_parse.y"
    { (yyval.opc) = 0x59c8; ;}
    break;

  case 154:
#line 306 "m68k_parse.y"
    { (yyval.opc) = 0x51c8; ;}
    break;

  case 155:
#line 307 "m68k_parse.y"
    { (yyval.opc) = 0x50c8; ;}
    break;

  case 156:
#line 311 "m68k_parse.y"
    { (yyval.opc) = 0x4afc; ;}
    break;

  case 157:
#line 312 "m68k_parse.y"
    { (yyval.opc) = 0x4e71; ;}
    break;

  case 158:
#line 313 "m68k_parse.y"
    { (yyval.opc) = 0x4e70; ;}
    break;

  case 159:
#line 314 "m68k_parse.y"
    { (yyval.opc) = 0x4e73; ;}
    break;

  case 160:
#line 315 "m68k_parse.y"
    { (yyval.opc) = 0x4e77; ;}
    break;

  case 161:
#line 316 "m68k_parse.y"
    { (yyval.opc) = 0x4e75; ;}
    break;

  case 162:
#line 317 "m68k_parse.y"
    { (yyval.opc) = 0x4e76; ;}
    break;

  case 163:
#line 321 "m68k_parse.y"
    { (yyval.opc) = 0x4ec0; ;}
    break;

  case 164:
#line 322 "m68k_parse.y"
    { (yyval.opc) = 0x4e80; ;}
    break;

  case 165:
#line 323 "m68k_parse.y"
    { (yyval.opc) = 0x4840; ;}
    break;

  case 166:
#line 326 "m68k_parse.y"
    { (yyval.opc) = ((yyvsp[(1) - (3)].reg)<<9) | 0x20 | (yyvsp[(3) - (3)].reg); ;}
    break;

  case 167:
#line 327 "m68k_parse.y"
    { (yyval.opc) = (((yyvsp[(2) - (4)].num) & 7)<<9) | (yyvsp[(4) - (4)].reg); ;}
    break;

  case 168:
#line 330 "m68k_parse.y"
    { (yyval.opc) = (((yyvsp[(1) - (3)].ea).ea & 7) << 9) |          ((yyvsp[(3) - (3)].ea).ea & 7); ;}
    break;

  case 169:
#line 331 "m68k_parse.y"
    { (yyval.opc) = (((yyvsp[(1) - (3)].ea).ea & 7) << 9) | 0x0008 | ((yyvsp[(3) - (3)].ea).ea & 7); ;}
    break;

  case 170:
#line 334 "m68k_parse.y"
    { if (((yyvsp[(3) - (3)].ea).ea & 070)==0) { /* dx,dy must be swapped */
						(yyval.rea).reg = ((yyvsp[(3) - (3)].ea).ea & 7)<<9; (yyvsp[(3) - (3)].ea).ea = (yyvsp[(1) - (3)].reg) & 7; (yyval.rea).ea = (yyvsp[(3) - (3)].ea); }
					else { (yyval.rea).reg = ((yyvsp[(1) - (3)].reg)<<9) | 0x100; (yyval.rea).ea = (yyvsp[(3) - (3)].ea); } ;}
    break;

  case 171:
#line 337 "m68k_parse.y"
    { (yyval.rea).reg = ((yyvsp[(3) - (3)].reg)<<9); (yyval.rea).ea = (yyvsp[(1) - (3)].ea); ;}
    break;

  case 172:
#line 340 "m68k_parse.y"
    { (yyval.reg)=0; ;}
    break;

  case 173:
#line 341 "m68k_parse.y"
    { (yyval.reg)=1; ;}
    break;

  case 174:
#line 342 "m68k_parse.y"
    { (yyval.reg)=2; ;}
    break;

  case 175:
#line 343 "m68k_parse.y"
    { (yyval.reg)=3; ;}
    break;

  case 176:
#line 344 "m68k_parse.y"
    { (yyval.reg)=4; ;}
    break;

  case 177:
#line 345 "m68k_parse.y"
    { (yyval.reg)=5; ;}
    break;

  case 178:
#line 346 "m68k_parse.y"
    { (yyval.reg)=6; ;}
    break;

  case 179:
#line 347 "m68k_parse.y"
    { (yyval.reg)=7; ;}
    break;

  case 180:
#line 350 "m68k_parse.y"
    { (yyval.reg)=0; ;}
    break;

  case 181:
#line 351 "m68k_parse.y"
    { (yyval.reg)=1; ;}
    break;

  case 182:
#line 352 "m68k_parse.y"
    { (yyval.reg)=2; ;}
    break;

  case 183:
#line 353 "m68k_parse.y"
    { (yyval.reg)=3; ;}
    break;

  case 184:
#line 354 "m68k_parse.y"
    { (yyval.reg)=4; ;}
    break;

  case 185:
#line 355 "m68k_parse.y"
    { (yyval.reg)=5; ;}
    break;

  case 186:
#line 356 "m68k_parse.y"
    { (yyval.reg)=6; ;}
    break;

  case 187:
#line 357 "m68k_parse.y"
    { (yyval.reg)=7; ;}
    break;

  case 188:
#line 360 "m68k_parse.y"
    { (yyval.wl) = 1; oplen = 0; ;}
    break;

  case 189:
#line 363 "m68k_parse.y"
    { (yyval.wl) = 0; oplen = 1; ;}
    break;

  case 190:
#line 364 "m68k_parse.y"
    { (yyval.wl) = 1; oplen = 2; ;}
    break;

  case 191:
#line 367 "m68k_parse.y"
    { (yyval.wl) = 0; oplen = 0; ;}
    break;

  case 192:
#line 368 "m68k_parse.y"
    { (yyval.wl) = 1; oplen = 1; ;}
    break;

  case 193:
#line 369 "m68k_parse.y"
    { (yyval.wl) = 2; oplen = 2; ;}
    break;

  case 194:
#line 372 "m68k_parse.y"
    { (yyval.wl) = 1; oplen = 0; ;}
    break;

  case 195:
#line 373 "m68k_parse.y"
    { (yyval.wl) = 3; oplen = 1; ;}
    break;

  case 196:
#line 374 "m68k_parse.y"
    { (yyval.wl) = 2; oplen = 2; ;}
    break;

  case 197:
#line 377 "m68k_parse.y"
    { (yyval.wl) = 3; oplen = 1; ;}
    break;

  case 198:
#line 378 "m68k_parse.y"
    { (yyval.wl) = 2; oplen = 2; ;}
    break;

  case 199:
#line 381 "m68k_parse.y"
    { (yyval.mask) = (yyvsp[(1) - (1)].mask); ;}
    break;

  case 200:
#line 382 "m68k_parse.y"
    { (yyval.mask).x = (yyvsp[(1) - (3)].mask).x | (yyvsp[(3) - (3)].mask).x; (yyval.mask).d = (yyvsp[(1) - (3)].mask).d | (yyvsp[(3) - (3)].mask).d; ;}
    break;

  case 201:
#line 385 "m68k_parse.y"
    { (yyval.mask).x = movemx[(yyvsp[(1) - (1)].reg)]; (yyval.mask).d = movemd[(yyvsp[(1) - (1)].reg)]; ;}
    break;

  case 202:
#line 386 "m68k_parse.y"
    { (yyval.mask).x = movemx[(yyvsp[(1) - (1)].reg)+8]; (yyval.mask).d = movemd[(yyvsp[(1) - (1)].reg)+8]; ;}
    break;

  case 203:
#line 387 "m68k_parse.y"
    { int i,l=(yyvsp[(1) - (3)].reg),h=(yyvsp[(3) - (3)].reg); if (l>h) { l=(yyvsp[(3) - (3)].reg); h=(yyvsp[(1) - (3)].reg); } (yyval.mask).x = (yyval.mask).d = 0; 
					for (i=l; i<=h; i++) { (yyval.mask).d |= movemx[i]; (yyval.mask).d |= movemd[i]; } ;}
    break;

  case 204:
#line 389 "m68k_parse.y"
    { int i,l=(yyvsp[(1) - (3)].reg),h=(yyvsp[(3) - (3)].reg); if (l>h) { l=(yyvsp[(3) - (3)].reg); h=(yyvsp[(1) - (3)].reg); } (yyval.mask).x = (yyval.mask).d = 0; 
					for (i=l; i<=h; i++) { (yyval.mask).x |= movemx[i+8]; (yyval.mask).d |= movemd[i+8]; } ;}
    break;

  case 260:
#line 404 "m68k_parse.y"
    { (yyval.ea).ea = (yyvsp[(1) - (1)].reg); (yyval.ea).cnt = 0; ;}
    break;

  case 261:
#line 406 "m68k_parse.y"
    { (yyval.ea).ea = 010 | (yyvsp[(1) - (1)].reg); (yyval.ea).cnt = 0; ;}
    break;

  case 262:
#line 408 "m68k_parse.y"
    { (yyval.ea).ea = 020 | (yyvsp[(2) - (3)].reg); (yyval.ea).cnt = 0; ;}
    break;

  case 263:
#line 410 "m68k_parse.y"
    { (yyval.ea).ea = 030 | (yyvsp[(2) - (3)].reg); (yyval.ea).cnt = 0; ;}
    break;

  case 264:
#line 412 "m68k_parse.y"
    { (yyval.ea).ea = 040 | (yyvsp[(2) - (3)].reg); (yyval.ea).cnt = 0; ;}
    break;

  case 265:
#line 414 "m68k_parse.y"
    { (yyval.ea).ea = 050 | (yyvsp[(4) - (5)].reg); (yyval.ea).cnt = 1; (yyval.ea).arg[0] = (yyvsp[(2) - (5)].num); ;}
    break;

  case 266:
#line 417 "m68k_parse.y"
    { (yyval.ea).ea = 060 | (yyvsp[(4) - (8)].reg); (yyval.ea).cnt = 1; (yyval.ea).arg[0] = 0x8000 | ((yyvsp[(6) - (8)].reg)<<12) | ((yyvsp[(7) - (8)].wl)<<11) | ((yyvsp[(2) - (8)].num) & 0xff); ;}
    break;

  case 267:
#line 419 "m68k_parse.y"
    { (yyval.ea).ea = 060 | (yyvsp[(4) - (8)].reg); (yyval.ea).cnt = 1; (yyval.ea).arg[0] =          ((yyvsp[(6) - (8)].reg)<<12) | ((yyvsp[(7) - (8)].wl)<<11) | ((yyvsp[(2) - (8)].num) & 0xff); ;}
    break;

  case 268:
#line 421 "m68k_parse.y"
    { if ((yyvsp[(4) - (4)].wl)==0) { (yyval.ea).ea = 070; (yyval.ea).cnt = 1; (yyval.ea).arg[0] = (yyvsp[(2) - (4)].num); } 
				          else {       (yyval.ea).ea = 071; (yyval.ea).cnt = 2; (yyval.ea).arg[0] = (yyvsp[(2) - (4)].num) >> 16; (yyval.ea).arg[1] = (yyvsp[(2) - (4)].num) & 0xffff; } ;}
    break;

  case 269:
#line 423 "m68k_parse.y"
    { int tmp = ((yyvsp[(2) - (3)].num)>>15) & 0x1ffff; if (tmp==0 || tmp==0x1ffff) { (yyval.ea).ea = 070; (yyval.ea).cnt = 1; (yyval.ea).arg[0] = (yyvsp[(2) - (3)].num); }
			 else { (yyval.ea).ea = 070; (yyval.ea).cnt = 2;  (yyval.ea).arg[0] = (yyvsp[(2) - (3)].num) >> 16; (yyval.ea).arg[1] = (yyvsp[(2) - (3)].num) & 0xffff; } ;}
    break;

  case 270:
#line 426 "m68k_parse.y"
    { (yyval.ea).ea = 072; (yyval.ea).cnt = 1; (yyval.ea).arg[0] = (yyvsp[(2) - (5)].num); ;}
    break;

  case 271:
#line 427 "m68k_parse.y"
    { (yyval.ea).ea = 072; (yyval.ea).cnt = 1; (yyval.ea).arg[0] = (yyvsp[(1) - (1)].num); ;}
    break;

  case 272:
#line 430 "m68k_parse.y"
    { (yyval.ea).ea = 073; (yyval.ea).cnt = 1; (yyval.ea).arg[0] = 0x8000 | ((yyvsp[(6) - (8)].reg)<<12) | ((yyvsp[(7) - (8)].wl)<<11) | ((yyvsp[(2) - (8)].num) & 0xff); ;}
    break;

  case 273:
#line 432 "m68k_parse.y"
    { (yyval.ea).ea = 073; (yyval.ea).cnt = 1; (yyval.ea).arg[0] = ((yyvsp[(6) - (8)].reg)<<12) | ((yyvsp[(7) - (8)].wl)<<11) | ((yyvsp[(2) - (8)].num) & 0xff); ;}
    break;

  case 274:
#line 434 "m68k_parse.y"
    { (yyval.ea).ea = 074; if (oplen==0) { (yyval.ea).cnt = 1; (yyval.ea).arg[0] = (yyvsp[(2) - (2)].num) & 0xff; }
				 else if (oplen==1) { (yyval.ea).cnt = 1; (yyval.ea).arg[0] = (yyvsp[(2) - (2)].num) & 0xffff; }
				 else { (yyval.ea).cnt = 2; (yyval.ea).arg[0] = (yyvsp[(2) - (2)].num) >> 16; (yyval.ea).arg[1] = (yyvsp[(2) - (2)].num) & 0xffff; } ;}
    break;

  case 275:
#line 438 "m68k_parse.y"
    { (yyval.ea).ea = 074; (yyval.ea).cnt = 0; ;}
    break;

  case 276:
#line 439 "m68k_parse.y"
    { (yyval.ea).ea = 074; (yyval.ea).cnt = 1; ;}
    break;


/* Line 1267 of yacc.c.  */
#line 3344 "m68k_parse.tab.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}


#line 440 "m68k_parse.y"


static void yyerror(char* s)
{
	/* do not emit anything, but set error flag */
	yyerrc = 1;
}

struct _optable {
	char* mnem;
	int token;
};

static struct _optable ops[] = {
	{ "abcd",	ABCD },		{ "add",	ADD },		{ "adda",	ADDA },		{ "addi",	ADDI },
	{ "addq",	ADDQ },		{ "addx",	ADDX },		{ "and",	AND },		{ "andi",	ANDI },
	{ "asl",	ASL }, 		{ "asr",	ASR },		{ "bcc",	BCC },		{ "bcs",	BCS },
	{ "beq",	BEQ },		{ "bge",	BGE },		{ "bgt",	BGT },		{ "bhi",	BHI },
	{ "ble",	BLE },		{ "bls",	BLS },		{ "blt",	BLT },		{ "bmi",	BMI },
	{ "bne",	BNE },		{ "bpl",	BPL },		{ "bvc",	BVC },		{ "bvs",	BVS },
	{ "bchg",	BCHG },		{ "bclr",	BCLR },		{ "bra",	BRA },		{ "bset",	BSET },
	{ "bsr",	BSR },		{ "btst",	BTST },		{ "chk",	CHK },		{ "clr",	CLR },
	{ "cmp",	CMP },		{ "cmpa",	CMPA },		{ "cmpi",	CMPI },		{ "cmpm",	CMPM },
	{ "dbcc",	DBCC },		{ "dbcs",	DBCS },		{ "dbeq",	DBEQ },		{ "dbf",	DBF },
	{ "dbge",	DBGE },		{ "dbgt",	DBGT },		{ "dbhi",	DBHI },		{ "dble",	DBLE },
	{ "dbls",	DBLS },		{ "dblt",	DBLT },		{ "dbmi",	DBMI },		{ "dbne",	DBNE },
	{ "dbpl",	DBPL },		{ "dbt",	DBT },		{ "dbvc",	DBVC },		{ "dbvs",	DBVS },
	{ "divs",	DIVS },		{ "divu",	DIVU },		{ "eor",	EOR },		{ "eori",	EORI },
	{ "exg",	EXG },		{ "ext",	EXT },		{ "illegal",ILLEGAL },	{ "jmp",	JMP },
	{ "jsr",	JSR },		{ "lea",	LEA },		{ "link",	LINK },		{ "lsl",	LSL },
	{ "lsr",	LSR }, 		{ "move",	MOVE },		{ "movea",	MOVEA },	{ "movem",	MOVEM },
	{ "movep",	MOVEP },	{ "moveq",	MOVEQ },	{ "muls",	MULS },		{ "mulu",	MULU },
	{ "nbcd",	NBCD },		{ "neg",	NEG },		{ "negx",	NEGX },		{ "nop",	NOP },
	{ "not",	NOT },		{ "or",		OR },		{ "ori",	ORI },		{ "pea",	PEA },
	{ "reset",	RESET },	{ "rol",	ROL },		{ "ror",	ROR },		{ "roxl",	ROXL },
	{ "roxr",	ROXR },		{ "rte",	RTE },		{ "rtr",	RTR },
	{ "rts",	RTS },		{ "scc",	SCC },		{ "scs",	SCS },		{ "seq",	SEQ },
	{ "sf",		SF },		{ "sge",	SGE },		{ "sgt",	SGT },		{ "shi",	SHI },
	{ "sle",	SLE },		{ "sls",	SLS },		{ "slt",	SLT },		{ "smi",	SMI },
	{ "sne",	SNE },		{ "spl",	SPL },		{ "st",		ST },		{ "svc",	SVC },
	{ "svs",	SVS },		{ "stop",	STOP },		{ "sub",	SUB },		{ "suba",	SUBA },
	{ "subi",	SUBI },		{ "subq",	SUBQ },		{ "subx",	SUBX },		{ "swap",	SWAP },
	{ "tas",	TAS },		{ "trap",	TRAP },		{ "trapv",	TRAPV },	{ "tst",	TST },
	{ "unlk",	UNLK },		{ "a0",		A0 },		{ "a1",		A1 },		{ "a2",		A2 },
	{ "a3",		A3 },		{ "a4",		A4 },		{ "a5",		A5 },		{ "a6",		A6 },
	{ "a7",		A7 },		{ "d0",		D0 },		{ "d1",		D1 },		{ "d2",		D2 },
	{ "d3",		D3 },		{ "d4",		D4 },		{ "d5",		D5 },		{ "d6",		D6 },
	{ "d7",		D7 },		{ "ccr",	CCR },		{ "sr",		SR },		{ "usp",	USP },
	{ "pc",		PC },		
	{ 0, 		0 }
};

typedef struct _ophash {
	struct _ophash* next;
	struct _optable* op;
} OPHASH;
#define OPHASHSIZE 97

static OPHASH **ophash = 0;

static int getophash(const char* s)
{
	int h = 0;
	while (*s++) h += (int)*s;
	return h % OPHASHSIZE;
}

static int oplookup(const char* s)
{
	int idx = getophash(s);
	OPHASH* oph = ophash[idx];
	if (oph) {
		if (oph->next) {
			while (oph) {
				if (!strcmp(s,oph->op->mnem)) return oph->op->token;
				oph = oph->next;
			}
			return 0;
		}
		return oph->op->token;
	}
	return 0;
}

static void init_ophash() 
{
	struct _optable* op = ops;
	OPHASH* oph;
	ophash = (OPHASH**)calloc(sizeof(OPHASH*),OPHASHSIZE);
	while (op->mnem) {
		int idx = getophash(op->mnem);
		oph = (OPHASH*)malloc(sizeof(OPHASH));
		oph->next = ophash[idx];
		oph->op = op;
		ophash[idx] = oph;
		op++;
	}
}

static char* yystream;

int yylex()
{
	char ident[30];
	char *p = ident;
	char c = yystream[0];
	
	while (c != 0 && (c=='\t' || c==' ')) {
		c = *++yystream;
	}
	if (c==0) return EOF;
	
	if (isalpha(c)) {
		while (isalnum(c) && (p-ident)<28) {
			*p++ = tolower(c); c = *++yystream;
		}
		*p = 0;
		if (p>ident) { return oplookup(ident); }
		return EOF;
	} else if (isdigit(c)) {
		*p++ = c; 
		if (yystream[1]=='x' || yystream[1]=='X') { *p++ = 'x'; yystream++; }
		c = *++yystream;
		while ((isdigit(c) || isxdigit(c)) && (p-ident)<28) {
			*p++ = c; c = *++yystream;
		}
		*p = 0;
		yylval.num = strtol(ident,0,0);
		return NUMBER;
    } else if (c=='$') {
    	if (isdigit(yystream[1]) || isxdigit(yystream[1])) {
			c = *++yystream;
			while ((isdigit(c) || isxdigit(c)) && (p-ident)<28) {
				*p++ = c; c = *++yystream;
			}
			*p = 0;
			yylval.num = strtol(ident,0,16);
			return NUMBER;
		} else return '$';
	} else if (c == '-' && yystream[1] == '(') {
		yystream += 2; return PREDEC;
	} else if (c == ')' && yystream[1] == '+') {
		yystream += 2; return POSTINC;
	} else if (c == '.') {
		switch (yystream[1]) {
		case 'b': yystream += 2; return BSIZE;
		case 'w': yystream += 2; return WSIZE;
		case 'l': yystream += 2; return LSIZE;
		case 's': yystream += 2; return SSIZE;
		default: yystream++; return '.';
		}
	} else {
		++yystream; return c;
	}
}

static t_value *yyvalptr;
static t_addr yyaddr;

t_stat parse_sym(char* c, t_addr a, UNIT* u, t_value* val, int32 sw)
{
	char ch;
	
	if (!ophash) init_ophash();

	yyvalptr = val;
	yyaddr = a;

	yystream = c;
	yyerrc = 0;
	
	ch = *yystream;
	while (ch != 0 && (ch=='\t' || ch==' ')) {
		ch = *++yystream;
	}
	if (ch == 0) return 0;

	if (sw & SWMASK('Y')) yydebug = 1 - yydebug;
	if ((sw & SWMASK('A')) || ch=='\'') {
		if ((ch = yystream[1])) {
			val[0] = (uint32)ch;
			return SCPE_OK;
		} else return SCPE_ARG;
	}
	if ((sw & SWMASK('C')) || ch=='"') {
		if ((ch = yystream[1])) {
			val[0] = ((uint32)ch << 8) | (uint32)yystream[1];
			return SCPE_OK;
		} else return SCPE_ARG;
	}
	
	yyparse();
	printf("rc=%d\n",yyrc);
	if (yyerrc) return SCPE_ARG;
	return yyrc;
}

static int _genop(t_value arg) 
{
//	printf("_genop(%x)@%x\n",arg,(int)yyvalptr);
	*yyvalptr = arg;
	yyvalptr++;
	return -1;
}

static int _genea(struct _ea arg)
{
	int i;
	for (i=0; i<arg.cnt; i++) _genop(arg.arg[i]);
	return -(arg.cnt*2)-1;
}

static int _genbr(t_value arg,t_addr tgt,int len) 
{
	t_addr a = tgt - yyaddr -2;
	if (len==1) {
		_genop(arg);
		_genop(a & 0xffff);
		a &= 0xffff8000;
		if (a != 0x00000000 && a != 0xffff8000) return SCPE_ARG;
		return -3;
	} else {
		_genop(arg | (a&0xff));
		a &= 0xffffff80;
		if (a != 0x00000000 && a != 0xffffff80) return SCPE_ARG;
		return -1;
	}
}

