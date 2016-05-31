/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

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
/* Line 1489 of yacc.c.  */
#line 353 "m68k_parse.tab.h"
    YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

extern YYSTYPE yylval;

