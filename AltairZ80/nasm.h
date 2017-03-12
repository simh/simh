/* nasm.h   main header file for the Netwide Assembler: inter-module interface
 *
 * The Netwide Assembler is copyright (C) 1996 Simon Tatham and
 * Julian Hall. All rights reserved. The software is
 * redistributable under the licence given in the file "Licence"
 * distributed in the NASM archive.
 *
 * initial version: 27/iii/95 by Simon Tatham
 */

#ifndef NASM_NASM_H
#define NASM_NASM_H

#include <stdio.h>

#ifndef NULL
#define NULL 0
#endif

#ifndef FALSE
#define FALSE 0                /* comes in handy */
#endif
#ifndef TRUE
#define TRUE 1
#endif

/*
 * Name pollution problems: <time.h> on Digital UNIX pulls in some
 * strange hardware header file which sees fit to define R_SP. We
 * undefine it here so as not to break the enum below.
 */
#ifdef R_SP
#undef R_SP
#endif

/*
 * Special values for expr->type. ASSUMPTION MADE HERE: the number
 * of distinct register names (i.e. possible "type" fields for an
 * expr structure) does not exceed 124 (EXPR_REG_START through
 * EXPR_REG_END).
 */
#define EXPR_REG_START 1

/*
 * Here we define the operand types. These are implemented as bit
 * masks, since some are subsets of others; e.g. AX in a MOV
 * instruction is a special operand type, whereas AX in other
 * contexts is just another 16-bit register. (Also, consider CL in
 * shift instructions, DX in OUT, etc.)
 */

/* size, and other attributes, of the operand */
#define BITS8     0x00000001L
#define BITS16    0x00000002L
#define BITS32    0x00000004L
#define BITS64    0x00000008L          /* FPU only */
#define BITS80    0x00000010L          /* FPU only */
#define FAR       0x00000020L          /* grotty: this means 16:16 or */
                       /* 16:32, like in CALL/JMP */
#define NEAR      0x00000040L
#define SHORT     0x00000080L          /* and this means what it says :) */

#define SIZE_MASK 0x000000FFL          /* all the size attributes */
#define NON_SIZE  (~SIZE_MASK)

#define TO        0x00000100L          /* reverse effect in FADD, FSUB &c */
#define COLON     0x00000200L          /* operand is followed by a colon */

/* type of operand: memory reference, register, etc. */
#define MEMORY    0x00204000L
#define REGISTER  0x00001000L          /* register number in 'basereg' */
#define IMMEDIATE 0x00002000L

#define REGMEM    0x00200000L          /* for r/m, ie EA, operands */
#define REGNORM   0x00201000L          /* 'normal' reg, qualifies as EA */
#define REG8      0x00201001L
#define REG16     0x00201002L
#define REG32     0x00201004L
#define MMXREG    0x00201008L          /* MMX registers */
#define XMMREG    0x00201010L          /* XMM Katmai reg */
#define FPUREG    0x01000000L          /* floating point stack registers */
#define FPU0      0x01000800L          /* FPU stack register zero */

/* special register operands: these may be treated differently */
#define REG_SMASK 0x00070000L          /* a mask for the following */
#define REG_ACCUM 0x00211000L          /* accumulator: AL, AX or EAX */
#define REG_AL    0x00211001L          /* REG_ACCUM | BITSxx */
#define REG_AX    0x00211002L          /* ditto */
#define REG_EAX   0x00211004L          /* and again */
#define REG_COUNT 0x00221000L          /* counter: CL, CX or ECX */
#define REG_CL    0x00221001L          /* REG_COUNT | BITSxx */
#define REG_CX    0x00221002L          /* ditto */
#define REG_ECX   0x00221004L          /* another one */
#define REG_DL    0x00241001L
#define REG_DX    0x00241002L
#define REG_EDX   0x00241004L
#define REG_SREG  0x00081002L          /* any segment register */
#define REG_CS    0x01081002L          /* CS */
#define REG_DESS  0x02081002L          /* DS, ES, SS (non-CS 86 registers) */
#define REG_FSGS  0x04081002L          /* FS, GS (386 extended registers) */
#define REG_SEG67 0x08081002L          /* Non-implemented segment registers */
#define REG_CDT   0x00101004L          /* CRn, DRn and TRn */
#define REG_CREG  0x08101004L          /* CRn */
#define REG_DREG  0x10101004L          /* DRn */
#define REG_TREG  0x20101004L          /* TRn */

/* special type of EA */
#define MEM_OFFS  0x00604000L          /* simple [address] offset */

/* special type of immediate operand */
#define ONENESS   0x00800000L          /* so UNITY == IMMEDIATE | ONENESS */
#define UNITY     0x00802000L          /* for shift/rotate instructions */
#define BYTENESS  0x40000000L          /* so SBYTE == IMMEDIATE | BYTENESS */
#define SBYTE     0x40002000L          /* for op r16/32,immediate instrs. */

/* Register names automatically generated from regs.dat */
/* automatically generated from ./regs.dat - do not edit */
enum reg_enum {
    R_AH = EXPR_REG_START,
    R_AL,
    R_AX,
    R_BH,
    R_BL,
    R_BP,
    R_BX,
    R_CH,
    R_CL,
    R_CR0,
    R_CR1,
    R_CR2,
    R_CR3,
    R_CR4,
    R_CR5,
    R_CR6,
    R_CR7,
    R_CS,
    R_CX,
    R_DH,
    R_DI,
    R_DL,
    R_DR0,
    R_DR1,
    R_DR2,
    R_DR3,
    R_DR4,
    R_DR5,
    R_DR6,
    R_DR7,
    R_DS,
    R_DX,
    R_EAX,
    R_EBP,
    R_EBX,
    R_ECX,
    R_EDI,
    R_EDX,
    R_ES,
    R_ESI,
    R_ESP,
    R_FS,
    R_GS,
    R_MM0,
    R_MM1,
    R_MM2,
    R_MM3,
    R_MM4,
    R_MM5,
    R_MM6,
    R_MM7,
    R_SEGR6,
    R_SEGR7,
    R_SI,
    R_SP,
    R_SS,
    R_ST0,
    R_ST1,
    R_ST2,
    R_ST3,
    R_ST4,
    R_ST5,
    R_ST6,
    R_ST7,
    R_TR0,
    R_TR1,
    R_TR2,
    R_TR3,
    R_TR4,
    R_TR5,
    R_TR6,
    R_TR7,
    R_XMM0,
    R_XMM1,
    R_XMM2,
    R_XMM3,
    R_XMM4,
    R_XMM5,
    R_XMM6,
    R_XMM7,
    REG_ENUM_LIMIT
};

enum {                     /* condition code names */
    C_A, C_AE, C_B, C_BE, C_C, C_E, C_G, C_GE, C_L, C_LE, C_NA, C_NAE,
    C_NB, C_NBE, C_NC, C_NE, C_NG, C_NGE, C_NL, C_NLE, C_NO, C_NP,
    C_NS, C_NZ, C_O, C_P, C_PE, C_PO, C_S, C_Z
};

/*
 * Note that because segment registers may be used as instruction
 * prefixes, we must ensure the enumerations for prefixes and
 * register names do not overlap.
 */
enum {                     /* instruction prefixes */
    PREFIX_ENUM_START = REG_ENUM_LIMIT,
    P_A16 = PREFIX_ENUM_START, P_A32, P_LOCK, P_O16, P_O32, P_REP, P_REPE,
    P_REPNE, P_REPNZ, P_REPZ, P_TIMES
};

enum {                     /* extended operand types */
    EOT_NOTHING, EOT_DB_STRING, EOT_DB_NUMBER
};

enum {                          /* special EA flags */
    EAF_BYTEOFFS = 1,           /* force offset part to byte size */
    EAF_WORDOFFS = 2,           /* force offset part to [d]word size */
    EAF_TIMESTWO = 4            /* really do EAX*2 not EAX+EAX */
};

enum {                          /* values for `hinttype' */
    EAH_NOHINT = 0,             /* no hint at all - our discretion */
    EAH_MAKEBASE = 1,           /* try to make given reg the base */
    EAH_NOTBASE = 2             /* try _not_ to make reg the base */
};

typedef struct {                /* operand to an instruction */
    long type;                  /* type of operand */
    int addr_size;              /* 0 means default; 16; 32 */
    int basereg, indexreg, scale;   /* registers and scale involved */
    int hintbase, hinttype;     /* hint as to real base register */
    long segment;               /* immediate segment, if needed */
    long offset;                /* any immediate number */
    long wrt;                   /* segment base it's relative to */
    int eaflags;                /* special EA flags */
    int opflags;                /* see OPFLAG_* defines below */
} operand;

#define OPFLAG_FORWARD      1   /* operand is a forward reference */
#define OPFLAG_EXTERN       2   /* operand is an external reference */

typedef struct extop {          /* extended operand */
    struct extop *next;         /* linked list */
    long type;                  /* defined above */
    char *stringval;            /* if it's a string, then here it is */
    int stringlen;              /* ... and here's how long it is */
    long segment;               /* if it's a number/address, then... */
    long offset;                /* ... it's given here ... */
    long wrt;                   /* ... and here */
} extop;

#define MAXPREFIX 4

typedef struct {                /* an instruction itself */
    /* char *label; not needed */               /* the label defined, or NULL */
    int prefixes[MAXPREFIX];    /* instruction prefixes, if any */
    int nprefix;                /* number of entries in above */
    int opcode;                 /* the opcode - not just the string */
    int condition;              /* the condition code, if Jcc/SETcc */
    int operands;               /* how many operands? 0-3
                                    * (more if db et al) */
    operand oprs[3];            /* the operands, defined as above */
    extop *eops;                /* extended operands */
    int eops_float;             /* true if DD and floating */
    long times;                 /* repeat count (TIMES prefix) */
    int forw_ref;               /* is there a forward reference? */
} insn;

enum geninfo { GI_SWITCH };

/*
 * values for the `type' parameter to an output function. Each one
 * must have the actual number of _bytes_ added to it.
 *
 * Exceptions are OUT_RELxADR, which denote an x-byte relocation
 * which will be a relative jump. For this we need to know the
 * distance in bytes from the start of the relocated record until
 * the end of the containing instruction. _This_ is what is stored
 * in the size part of the parameter, in this case.
 *
 * Also OUT_RESERVE denotes reservation of N bytes of BSS space,
 * and the contents of the "data" parameter is irrelevant.
 *
 * The "data" parameter for the output function points to a "long",
 * containing the address in question, unless the type is
 * OUT_RAWDATA, in which case it points to an "unsigned char"
 * array.
 */
#define OUT_RAWDATA 0x00000000UL
#define OUT_ADDRESS 0x10000000UL
#define OUT_REL2ADR 0x20000000UL
#define OUT_REL4ADR 0x30000000UL
#define OUT_RESERVE 0x40000000UL
#define OUT_TYPMASK 0xF0000000UL
#define OUT_SIZMASK 0x0FFFFFFFUL

/*
 * The type definition macros
 * for debugging
 *
 * low 3 bits: reserved
 * next 5 bits: type
 * next 24 bits: number of elements for arrays (0 for labels)
 */

#define TY_UNKNOWN 0x00
#define TY_LABEL   0x08
#define TY_BYTE    0x10
#define TY_WORD    0x18
#define TY_DWORD   0x20
#define TY_FLOAT   0x28
#define TY_QWORD   0x30
#define TY_TBYTE   0x38
#define TY_COMMON  0xE0
#define TY_SEG     0xE8
#define TY_EXTERN  0xF0
#define TY_EQU     0xF8

#define TYM_TYPE(x) ((x) & 0xF8)
#define TYM_ELEMENTS(x) (((x) & 0xFFFFFF00) >> 8)

#define TYS_ELEMENTS(x)  ((x) << 8)
/*
 * -----
 * Other
 * -----
 */

/*
 * This is a useful #define which I keep meaning to use more often:
 * the number of elements of a statically defined array.
 */

#define elements(x)     ( sizeof(x) / sizeof(*(x)) )

extern int tasm_compatible_mode;

/*
 * This declaration passes the "pass" number to all other modules
 * "pass0" assumes the values: 0, 0, ..., 0, 1, 2
 * where 0 = optimizing pass
 *       1 = pass 1
 *       2 = pass 2
 */

extern int pass0;   /* this is globally known */
extern int optimizing;

#endif
