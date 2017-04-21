/*
	Assembler compatible with MACRO-11.

Copyright (c) 2001, Richard Krehbiel
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

o Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

o Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

o Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

*/

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <limits.h>

#include "macro11.h"

#include "rad50.h"

#include "object.h"

#include "stream2.h"

#include "mlb.h"

#include "util.h"

#define SYMMAX 6			/* I will honor this many character
							   symbols */

#define issym(c) (isalpha(c) || isdigit(c) || (c) == '.' || (c) == '$')

/* Program sections: */

typedef struct section
{
	char *label;		/* Section name */
	unsigned type;		/* Section type */
#define USER 1			/* user-defined */
#define SYSTEM 2		/* A system symbol (like "."; value is an
						   enum) */
#define INSTRUCTION 3	/* An instruction code (like "MOV"; value is
						   an enum) */
#define PSEUDO 4		/* A pseudo-op (.PSECT, .TITLE, .MACRO, .IF;
						   value is an enum) */
#define REGISTER 5		/* Symbol is a register (value 0=$0, value
						   1=$1, ... $7) */
#define USERMACRO 6		/* Symbol is a user macro */

	unsigned flags;		/* Flags, defined in object.h */
	unsigned pc;		/* Current offset in the section */
	unsigned size;		/* Current section size */
	unsigned sector;	/* Used for complex relocation, and naught else */
} SECTION;

/* Symbol table entries */

typedef struct symbol
{
	char *label;		/* Symbol name */
	unsigned value;		/* Symbol value */
	int stmtno;			/* Statement number of symbol's definition */
	unsigned flags;		/* Symbol flags */
#define PERMANENT 1		/* Symbol may not be redefined */
#define GLOBAL 2		/* Symbol is global */
#define WEAK 4			/* Symbol definition is weak */
#define DEFINITION 8	/* Symbol is a global definition, not
						   reference */
#define UNDEFINED 16	/* Symbol is a phony, undefined */
#define LOCAL 32		/* Set if this is a local label (i.e. 10$) */

	SECTION *section;	/* Section in which this symbol is defined */
	struct symbol *next;	/* Next symbol with the same hash value */
} SYMBOL;

/* Arguments given to macros or .IRP/.IRPC blocks */

typedef struct arg
{
	struct arg *next;		/* Pointer in arg list */
	int locsym;				/* Whether arg represents an optional
							   local symbol */
	char *label;			/* Argument name */
	char *value;			/* Default or active substitution */
} ARG;

/* A MACRO is a superstructure surrounding a SYMBOL. */

typedef struct macro
{
	SYMBOL sym;				/* Surrounds a symbol, contains the macro
							   name */
	ARG *args;				/* The argument list */
	BUFFER *text;			/* The macro text */
} MACRO;

typedef struct ex_tree
{
	enum ex_type
	{
		EX_LIT=1,				/* Expression is a literal value */
		EX_SYM=2,				/* Expression has a symbol reference */
		EX_UNDEFINED_SYM=3,		/* Expression is undefined sym reference */
		EX_TEMP_SYM=4,			/* Expression is temp sym reference */

		EX_COM=5,				/* One's complement */
		EX_NEG=6,				/* Negate */
		EX_ERR=7,				/* Expression with an error */

		EX_ADD=8,				/* Add */
		EX_SUB=9,				/* Subtract */
		EX_MUL=10,				/* Multiply */
		EX_DIV=11,				/* Divide */
		EX_AND=12,				/* bitwise and */
		EX_OR=13				/* bitwise or */
	} type;

	char *cp;					/* points to end of parsed expression */

	union
	{
		struct
		{
			struct ex_tree *left, *right;	/* Left, right children */
		} child;
		unsigned lit;					/* Literal value */
		SYMBOL *symbol;					/* Symbol reference */
	} data;

} EX_TREE;

typedef struct addr_mode
{
	unsigned type;			/* The bits that represent the addressing mode */
							/* bits 0:2 are register number */
							/* bit 3 is indirect */
							/* bits 4:6 are mode, where 0=Rn, 1=(Rn)+,
							   2=-(Rn), 3=offset(Rn) */
	int rel;				/* the addressing mode is PC-relative */
	EX_TREE *offset;		/* Expression giving the offset */
} ADDR_MODE;

#define FALSE 0					/* Everybody needs FALSE and TRUE */
#define TRUE 1

enum pseudo_ops
{
	P_ASCII, P_ASCIZ, P_ASECT, P_BLKB, P_BLKW, P_BYTE, P_CSECT, P_DSABL,
	P_ENABL, P_END, P_ENDC, P_ENDM, P_ENDR, P_EOT, P_ERROR, P_EVEN,
	P_FLT2, P_FLT4, P_GLOBL, P_IDENT, P_IF, P_IFF, P_IFT, P_IFTF, P_IIF,
	P_IRP, P_IRPC, P_LIMIT, P_LIST, P_MCALL, P_MEXIT, P_NARG, P_NCHR,
	P_NLIST, P_NTYPE, P_ODD, P_PACKED, P_PAGE, P_PRINT, P_PSECT, P_RADIX,
	P_RAD50, P_REM, P_REPT, P_RESTORE, P_SAVE, P_SBTTL, P_TITLE,
	P_WORD, P_MACRO, P_INCLU, P_WEAK, P_IFDF
};

enum instruction_ops
{
	I_ADC   = 0005500, I_ADCB  = 0105500, I_ADD   = 0060000, I_ASH   = 0072000,
	I_ASHC  = 0073000, I_ASL   = 0006300, I_ASLB  = 0106300, I_ASR   = 0006200,
	I_ASRB  = 0106200, I_BCC   = 0103000, I_BCS   = 0103400, I_BEQ   = 0001400,
	I_BGE   = 0002000, I_BGT   = 0003000, I_BHI   = 0101000, I_BHIS  = 0103000,
	I_BIC   = 0040000, I_BICB  = 0140000, I_BIS   = 0050000, I_BISB  = 0150000,
	I_BIT   = 0030000, I_BITB  = 0130000, I_BLE   = 0003400, I_BLO   = 0103400,
	I_BLOS  = 0101400, I_BLT   = 0002400, I_BMI   = 0100400, I_BNE   = 0001000,
	I_BPL   = 0100000, I_BPT   = 0000003, I_BR    = 0000400, I_BVC   = 0102000,
	I_BVS   = 0102400, I_CALL  = 0004700, I_CALLR = 0000100, I_CCC   = 0000257,
	I_CLC   = 0000241, I_CLN   = 0000250, I_CLR   = 0005000, I_CLRB  = 0105000,
	I_CLV   = 0000242, I_CLZ   = 0000244, I_CMP   = 0020000, I_CMPB  = 0120000,
	I_COM   = 0005100, I_COMB  = 0105100, I_DEC   = 0005300, I_DECB  = 0105300,
	I_DIV   = 0071000, I_EMT   = 0104000, I_FADD  = 0075000, I_FDIV  = 0075030,
	I_FMUL  = 0075020, I_FSUB  = 0075010, I_HALT  = 0000000, I_INC   = 0005200,
	I_INCB  = 0105200, I_IOT   = 0000004, I_JMP   = 0000100, I_JSR   = 0004000,
	I_MARK  = 0006400, I_MED6X = 0076600, I_MED74C= 0076601, I_MFPD  = 0106500,
	I_MFPI  = 0006500, I_MFPS  = 0106700, I_MOV   = 0010000, I_MOVB  = 0110000,
	I_MTPD  = 0106600, I_MTPI  = 0006600, I_MTPS  = 0106400, I_MUL   = 0070000,
	I_NEG   = 0005400, I_NEGB  = 0105400, I_NOP   = 0000240, I_RESET = 0000005,
	I_RETURN= 0000207, I_ROL   = 0006100, I_ROLB  = 0106100, I_ROR   = 0006000,
	I_RORB  = 0106000, I_RTI   = 0000002, I_RTS   = 0000200, I_RTT   = 0000006,
	I_SBC   = 0005600, I_SBCB  = 0105600, I_SCC   = 0000277, I_SEC   = 0000261,
	I_SEN   = 0000270, I_SEV   = 0000262, I_SEZ   = 0000264, I_SOB   = 0077000,
	I_SPL   = 0000230, I_SUB   = 0160000, I_SWAB  = 0000300, I_SXT   = 0006700,
	I_TRAP  = 0104400, I_TST   = 0005700, I_TSTB  = 0105700, I_WAIT  = 0000001,
	I_XFC   = 0076700, I_XOR   = 0074000, I_MFPT  = 0000007,
	/* CIS not implemented - maybe later */
	/* FPU */
	I_ABSD  = 0170600, I_ABSF  = 0170600, I_ADDD  = 0172000, I_ADDF  = 0172000,
	I_CFCC  = 0170000, I_CLRD  = 0170400, I_CLRF  = 0170400, I_CMPD  = 0173400,
	I_CMPF  = 0173400, I_DIVD  = 0174400, I_DIVF  = 0174400, I_LDCDF = 0177400,
	I_LDCFD = 0177400, I_LDCID = 0177000, I_LDCIF = 0177000, I_LDCLD = 0177000,
	I_LDCLF = 0177000, I_LDD   = 0172400, I_LDEXP = 0176400, I_LDF   = 0172400,
	I_LDFPS = 0170100, I_MODD  = 0171400, I_MODF  = 0171400, I_MULD  = 0171000,
	I_MULF  = 0171000, I_NEGD  = 0170700, I_NEGF  = 0170700, I_SETD  = 0170011,
	I_SETF  = 0170001, I_SETI  = 0170002, I_SETL  = 0170012, I_STA0  = 0170005,
	I_STB0  = 0170006, I_STCDF = 0176000, I_STCDI = 0175400, I_STCDL = 0175400,
	I_STCFD = 0176000, I_STCFI = 0175400, I_STCFL = 0175400, I_STD   = 0174000,
	I_STEXP = 0175000, I_STF   = 0174000, I_STFPS = 0170200, I_STST  = 0170300,
	I_SUBD  = 0173000, I_SUBF  = 0173000, I_TSTD  = 0170500, I_TSTF  = 0170500
};

enum operand_codes
{
	OC_MASK = 0xff00,		/* mask over flags for operand types */
	OC_NONE = 0x0000,		/* No operands */
	OC_1GEN = 0x0100,		/* One general operand (CLR, TST, etc.) */
	OC_2GEN = 0x0200,		/* Two general operand (MOV, CMP, etc.) */
	OC_BR   = 0x0300,		/* Branch */
	OC_ASH  = 0x0400,		/* ASH and ASHC (one gen, one reg) */
	OC_MARK = 0x0500,		/* MARK instruction operand */
	OC_JSR  = 0x0600,		/* JSR, XOR (one reg, one gen) */
	OC_1REG = 0x0700,		/* FADD, FSUB, FMUL, FDIV, RTS */
	OC_SOB  = 0x0800,		/* SOB */
	OC_1FIS = 0x0900,		/* FIS (reg, gen) */
	OC_2FIS = 0x0a00,		/* FIS (gen, reg) */
	OC__LAST = 0xff00 };

/*
	format of a listing line
	Interestingly, no instances of this struct are ever created.
	It lives to be a way to layout the format of a list line.
	I wonder if I should have bothered.
*/

typedef struct lstformat
{
	char flag[2];				/* Error flags */
	char line_number[6];		/* Line number */
	char pc[8];					/* Location */
	char words[8][3];			/* three instruction words */
	char source[1];				/* source line */
} LSTFORMAT;

#define SIZEOF_MEMBER(s, m) (sizeof((s *)0)->m)

/* GLOBAL VARIABLES */

int pass = 0;				/* The current assembly pass.  0 = first
							   pass. */

int stmtno = 0;				/* The current source line number */
int radix = 8;				/* The current input conversion radix */
int lsb = 0;				/* The current local symbol section identifier */
int last_lsb = 0;			/* The last block in which a macro
							   automatic label was created */
int last_locsym = 32768;	/* The last local symbol number generated */

int enabl_debug	= 0;		/* Whether assembler debugging is enabled */

int enabl_ama = 0;			/* When set, chooses absolute (037) versus
							   PC-relative */
							/* (067) addressing mode */
int enabl_lsb = 0;			/* When set, stops non-local symbol
							   definitions from delimiting local
							   symbol sections. */

int enabl_gbl = 1;			/* Implicit definition of global symbols */

int list_md = 1;			/* option to list macro/rept definition = yes */

int list_me = 1;			/* option to list macro/rept expansion = yes */

int list_bex = 1;			/* option to show binary */

int list_level = 1;			/* Listing control level.  .LIST
							   increments; .NLIST decrements */

char *listline;				/* Source lines */

char *binline;				/* for octal expansion */

FILE *lstfile = NULL;

int suppressed = 0;			/* Assembly suppressed by failed conditional */

#define MAX_MLBS 32
MLB *mlbs[MAX_MLBS];		/* macro libraries specified on the
							   command line */
int nr_mlbs = 0;			/* Number of macro libraries */

typedef struct cond
{
	int ok;					/* What the condition evaluated to */
	char *file;				/* What file and line it occurred */
	int line;
} COND;

#define MAX_CONDS 256
COND conds[MAX_CONDS];		/* Stack of recent conditions */
int last_cond;				/* 0 means no stacked cond. */

SECTION *sect_stack[32];	/* 32 saved sections */
int sect_sp;				/* Stack pointer */

char *module_name = NULL;	/* The module name (taken from the 'TITLE'); */

char *ident = NULL;			/* .IDENT name */

EX_TREE *xfer_address = NULL; /* The transfer address */

SYMBOL *current_pc;			/* The current program counter */

unsigned last_dot_addr;		/* Last coded PC... */
SECTION *last_dot_section;	/* ...and it's program section */

#define DOT (current_pc->value)	/* Handy reference to the current location */

/* The following are dummy psects for symbols which have meaning to
   the assembler: */

SECTION register_section =
{ "", REGISTER, 0, 0 };			/* the section containing the registers */

SECTION pseudo_section =
{ "", PSEUDO, 0, 0 };			/* the section containing the
								   pseudo-operations */

SECTION instruction_section =
{ ". ABS.", INSTRUCTION, 0, 0 }; /* the section containing instructions */

SECTION macro_section =
{ "", SYSTEM, 0, 0, 0 };		/* Section for macros */

/* These are real psects that get written out to the object file */

SECTION absolute_section =
{ ". ABS.", SYSTEM, PSECT_GBL|PSECT_COM, 0, 0, 0}; /* The default
													absolute section */

SECTION blank_section =
{ "", SYSTEM, PSECT_REL, 0, 0, 1}; /* The default relocatable section */

SECTION *sections[256] = { /* Array of sections in the order they were
							  defined */
	&absolute_section, &blank_section, };

int sector = 2;				/* number of such sections */

SYMBOL *reg_sym[8];		/* Keep the register symbols in a handy array */

/* symbol tables */

#define HASH_SIZE 1023

typedef struct symbol_table
{
	SYMBOL *hash[HASH_SIZE];
} SYMBOL_TABLE;

SYMBOL_TABLE system_st;			/* System symbols (Instructions,
								   pseudo-ops, registers) */

SYMBOL_TABLE section_st;		/* Program sections */

SYMBOL_TABLE symbol_st; 		/* User symbols */

SYMBOL_TABLE macro_st;			/* Macros */

SYMBOL_TABLE implicit_st;		/* The symbols which may be implicit globals */

/* SYMBOL_ITER is used for iterating thru a symbol table. */

typedef struct symbol_iter
{
	int subscript;				/* Current hash subscript */
	SYMBOL *current;			/* Current symbol */
} SYMBOL_ITER;

/* EOL says whether a char* is pointing at the end of a line */
#define EOL(c) (!(c) || (c) == '\n' || (c) == ';')

/* reports errors */
static void report(STREAM *str, char *fmt, ...)
{
	va_list ap;
	char *name = "**";
	int line = 0;

	if(!pass)
		return;				/* Don't report now. */

	if(str)
	{
		name = str->name;
		line = str->line;
	}

	fprintf(stderr, "%s:%d: ***ERROR ", name, line);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if(lstfile)
	{
		fprintf(lstfile, "%s:%d: ***ERROR ", name, line);
		va_start(ap, fmt);
		vfprintf(lstfile, fmt, ap);
		va_end(ap);
	}
}

/* memcheck - crash out if a pointer (returned from malloc) is NULL. */

void *memcheck(void *ptr)
{
	if(ptr == NULL)
	{
		fprintf(stderr, "Out of memory.\n");
		exit(EXIT_FAILURE);
	}

	return ptr;
}

/* upcase turns a string to upper case */

static void upcase(char *str)
{
	while(*str)
	{
		*str = toupper(*str);
		str++;
	}
}

/* hash_name hashes a name into a value from 0-HASH_SIZE */

static int hash_name(char *label)
{
	unsigned accum = 0;

	while(*label)
		accum = (accum << 1) ^ *label++;

	accum %= HASH_SIZE;

	return accum;
}

/* Allocate a new symbol.  Does not add it to any symbol table. */

static SYMBOL *new_sym(char *label)
{
	SYMBOL *sym = memcheck(malloc(sizeof(SYMBOL)));
	sym->label = memcheck(strdup(label));
	sym->section = NULL;
	sym->value = 0;
	sym->flags = 0;
	return sym;
}

/* Free a symbol. Does not remove it from any symbol table.  */

static void free_sym(SYMBOL *sym)
{
	if(sym->label)
	{
		free(sym->label);
		sym->label = NULL;
	}
	free(sym);
}

/* remove_sym removes a symbol from it's symbol table. */

static void remove_sym(SYMBOL *sym, SYMBOL_TABLE *table)
{
	SYMBOL **prevp, *symp;
	int hash;

	hash = hash_name(sym->label);
	prevp = &table->hash[hash];
	while(symp = *prevp, symp != NULL && symp != sym)
		prevp = &symp->next;

	if(symp)
		*prevp = sym->next;
}

/* lookup_sym finds a symbol in a table */

static SYMBOL *lookup_sym(char *label, SYMBOL_TABLE *table)
{
	unsigned hash;
	SYMBOL *sym;

	hash = hash_name(label);

	sym = table->hash[hash];
	while(sym && strcmp(sym->label, label) != 0)
		sym = sym->next;

	return sym;
}

/* next_sym - returns the next symbol from a symbol table.  Must be
   preceeded by first_sym.  Returns NULL after the last symbol. */

static SYMBOL *next_sym(SYMBOL_TABLE *table, SYMBOL_ITER *iter)
{
	if(iter->current)
		iter->current = iter->current->next;

	while(iter->current == NULL)
	{
		if(iter->subscript >= HASH_SIZE)
			return NULL;	/* No more symbols. */
		iter->current = table->hash[iter->subscript];
		iter->subscript++;
	}

	return iter->current;				/* Got a symbol. */
}

/* first_sym - returns the first symbol from a symbol table. Symbols
   are stored in random order. */

static SYMBOL *first_sym(SYMBOL_TABLE *table, SYMBOL_ITER *iter)
{
	iter->subscript = 0;
	iter->current = NULL;
	return next_sym(table, iter);
}

/* add_table - add a symbol to a symbol table. */

static void add_table(SYMBOL *sym, SYMBOL_TABLE *table)
{
	int hash = hash_name(sym->label);
	sym->next = table->hash[hash];
	table->hash[hash] = sym;
}

/* add_sym - used throughout to add or update symbols in a symbol
   table.  */

static SYMBOL *add_sym(char *label, unsigned value, unsigned flags,
					   SECTION *section, SYMBOL_TABLE *table)
{
	SYMBOL *sym;

	sym = lookup_sym(label, table);
	if(sym != NULL)
	{
		// A symbol registered as "undefined" can be changed.

		if((sym->flags & UNDEFINED) &&
			!(flags & UNDEFINED))
		{
			sym->flags &= ~(PERMANENT|UNDEFINED);
		}

		/* Check for compatible definition */
		else if(sym->section == section &&
			sym->value == value)
		{
			sym->flags |= flags;	/* Merge flags quietly */
			return sym;				/* 's okay */
		}

		if(!(sym->flags & PERMANENT))
		{
			/* permit redefinition */
			sym->value = value;
			sym->flags |= flags;
			sym->section = section;
			return sym;
		}

		return NULL;			/* Bad symbol redefinition */
	}

	sym = new_sym(label);
	sym->flags = flags;
	sym->stmtno = stmtno;
	sym->section = section;
	sym->value = value;

	add_table(sym, table);

	return sym;
}

/* Allocate a new section */

static SECTION *new_section(void)
{
	SECTION *sect = memcheck(malloc(sizeof(SECTION)));
	sect->flags = 0;
	sect->size = 0;
	sect->pc = 0;
	sect->type = 0;
	sect->sector = 0;
	sect->label = NULL;
	return sect;
}

/* Allocate a new ARG */

static ARG *new_arg(void)
{
	ARG *arg = memcheck(malloc(sizeof(ARG)));
	arg->locsym = 0;
	arg->value = NULL;
	arg->next = NULL;
	arg->label = NULL;
	return arg;
}

/* Allocate a new macro */

static MACRO *new_macro(char *label)
{
	MACRO *mac = memcheck(malloc(sizeof(MACRO)));

	mac->sym.flags = 0;
	mac->sym.label = label;
	mac->sym.stmtno = stmtno;
	mac->sym.next = NULL;
	mac->sym.section = &macro_section;
	mac->sym.value = 0;
	mac->args = NULL;
	mac->text = NULL;

	return mac;
}

/* Free a list of args (as for a macro, or a macro expansion) */

static void free_args(ARG *arg)
{
	ARG *next;

	while(arg)
	{
		next = arg->next;
		if(arg->label)
		{
			free(arg->label);
			arg->label = NULL;
		}
		if(arg->value)
		{
			free(arg->value);
			arg->value = NULL;
		}
		free(arg);
		arg = next;
	}
}

/* free a macro, it's args, it's text, etc. */

static void free_macro(MACRO *mac)
{
	if(mac->text)
	{
		free(mac->text);
	}
	free_args(mac->args);
	free_sym(&mac->sym);
}

/* do_list returns TRUE if listing is enabled. */

static int dolist(void)
{
	int ok = lstfile != NULL && pass > 0 && list_level > 0;
	return ok;
}

/* list_source saves a text line for later listing by list_flush */

static void list_source(STREAM *str, char *cp)
{
	if(dolist())
	{
		int len = strcspn(cp, "\n");
		/* Save the line text away for later... */
		if(listline)
			free(listline);
		listline = memcheck(malloc(len + 1));
		memcpy(listline, cp, len);
		listline[len] = 0;

		if(!binline)
			binline = memcheck(malloc(sizeof(LSTFORMAT) + 16));

		sprintf(binline, "%*s%*d",
			SIZEOF_MEMBER(LSTFORMAT, flag), "",
			SIZEOF_MEMBER(LSTFORMAT, line_number), str->line);
	}
}

/* padto adds blanks to the end of a string until it's the given
   length. */

static void padto(char *str, int to)
{
	int needspace = to - strlen(str);
	str += strlen(str);
	while(needspace > 0)
		*str++ = ' ', needspace--;
	*str = 0;
}

/* list_flush produces a buffered list line. */

static void list_flush(void)
{
	if(dolist())
	{
		padto(binline, offsetof(LSTFORMAT, source));
		fputs(binline, lstfile);
		fputs(listline, lstfile);
		fputc('\n', lstfile);
		listline[0] = 0;
		binline[0] = 0;
	}
}

/* list_fit checks to see if a word will fit in the current listing
   line.  If not, it flushes and prepares another line. */

static void list_fit(STREAM *str, unsigned addr)
{
	int len = strlen(binline);
	size_t col1 = offsetof(LSTFORMAT, source);
	size_t col2 = offsetof(LSTFORMAT, pc);

	if(strlen(binline) >= col1)
	{
		int offset = offsetof(LSTFORMAT, pc);
		list_flush();
		listline[0] = 0;
		binline[0] = 0;
		sprintf(binline, "%*s %6.6o",
			offsetof(LSTFORMAT, pc), "",
			addr);
		padto(binline, offsetof(LSTFORMAT, words));
	}
	else if(strlen(binline) <= col2)
	{
		sprintf(binline, "%*s%*d %6.6o",
			SIZEOF_MEMBER(LSTFORMAT, flag), "",
			SIZEOF_MEMBER(LSTFORMAT, line_number), str->line,
			addr);
		padto(binline, offsetof(LSTFORMAT, words));
	}
}

/* list_value is used to show a computed value */

static void list_value(STREAM *str, unsigned word)
{
	if(dolist())
	{
		/* Print the value and go */
		binline[0] = 0;
		sprintf(binline, "%*s%*d %6.6o",
			SIZEOF_MEMBER(LSTFORMAT, flag), "",
			SIZEOF_MEMBER(LSTFORMAT, line_number), str->line,
			word & 0177777);
	}
}

/* Print a word to the listing file */

void list_word(STREAM *str, unsigned addr, unsigned value, int size,
			   char *flags)
{
	if(dolist())
	{
		list_fit(str, addr);
		if(size == 1)
			sprintf(binline + strlen(binline), "   %3.3o%1.1s ",
					value & 0377, flags);
		else
			sprintf(binline + strlen(binline), "%6.6o%1.1s ",
					value & 0177777, flags);
	}
}

/* This is called by places that are about to store some code, or
   which want to manually update DOT. */

static void change_dot(TEXT_RLD *tr, int size)
{
	if(size > 0)
	{
		if(last_dot_section != current_pc->section)
		{
			text_define_location(tr, current_pc->section->label,
								 &current_pc->value);
			last_dot_section = current_pc->section;
			last_dot_addr = current_pc->value;
		}
		if(last_dot_addr != current_pc->value)
		{
			text_modify_location(tr, &current_pc->value);
			last_dot_addr = current_pc->value;
		}

		/* Update for next time */
		last_dot_addr += size;
	}

	if(DOT+size > current_pc->section->size)
		current_pc->section->size = DOT+size;
}

/* store_word stores a word to the object file and lists it to the
   listing file */

static int store_word(STREAM *str, TEXT_RLD *tr, int size,
					  unsigned word)
{
	change_dot(tr, size);
	list_word(str, DOT, word, size, "");
	return text_word(tr, &DOT, size, word);
}

/* store_word stores a word to the object file and lists it to the
   listing file */

static int store_displaced_word(STREAM *str, TEXT_RLD *tr,
								int size, unsigned word)
{
	change_dot(tr, size);
	list_word(str, DOT, word, size, "'");
	return text_displaced_word(tr, &DOT, size, word);
}

static int store_global_displaced_offset_word(STREAM *str, TEXT_RLD *tr,
											  int size,
											  unsigned word, char *global)
{
	change_dot(tr, size);
	list_word(str, DOT, word, size, "G");
	return text_global_displaced_offset_word(tr, &DOT, size,
											 word, global);
}

static int store_global_offset_word(STREAM *str, TEXT_RLD *tr,
									int size,
									unsigned word, char *global)
{
	change_dot(tr, size);
	list_word(str, DOT, word, size, "G");
	return text_global_offset_word(tr, &DOT, size, word, global);
}

static int store_internal_word(STREAM *str, TEXT_RLD *tr,
							   int size, unsigned word)
{
	change_dot(tr, size);
	list_word(str, DOT, word, size, "");
	return text_internal_word(tr, &DOT, size, word);
}

static int store_psect_displaced_offset_word(STREAM *str, TEXT_RLD *tr,
											 int size,
											 unsigned word, char *name)
{
	change_dot(tr, size);
	list_word(str, DOT, word, size, "");
	return text_psect_displaced_offset_word(tr, &DOT, size, word, name);
}

static int store_psect_offset_word(STREAM *str, TEXT_RLD *tr,
								   int size,
								   unsigned word, char *name)
{
	change_dot(tr, size);
	list_word(str, DOT, word, size, "");
	return text_psect_offset_word(tr, &DOT, size, word, name);
}

static int store_limits(STREAM *str, TEXT_RLD *tr)
{
	change_dot(tr, 4);
	list_word(str, DOT, 0, 2, "");
	list_word(str, DOT+2, 0, 2, "");
	return text_limits(tr, &DOT);
}

/* skipwhite - used everywhere to advance a char pointer past spaces */

static char *skipwhite(char *cp)
{
	while(*cp == ' ' || *cp == '\t')
		cp++;
	return cp;
}

/* skipdelim - used everywhere to advance between tokens.  Whitespace
   and one comma are allowed delims. */

static char *skipdelim(char *cp)
{
	cp = skipwhite(cp);
	if(*cp == ',')
		cp = skipwhite(cp+1);
	return cp;
}

/* Parse PDP-11 64-bit floating point format. */
/* Give a pointer to "size" words to receive the result. */
/* Note: there are probably degenerate cases that store incorrect
   results.  For example, I think rounding up a FLT2 might cause
   exponent overflow.  Sorry. */
/* Note also that the full 49 bits of precision probably aren't
   available on the source platform, given the widespread application
   of IEEE floating point formats, so expect some differences.  Sorry
   again. */

int parse_float(char *cp, char **endp, int size, unsigned *flt)
{
	double d;					/* value */
	double frac;				/* fractional value */
	ulong64 ufrac;				/* fraction converted to 49 bit
								   unsigned integer */
	int i;						/* Number of fields converted by sscanf */
	int n;						/* Number of characters converted by sscanf */
	int sexp;					/* Signed exponent */
	unsigned exp;				/* Unsigned excess-128 exponent */
	unsigned sign = 0;			/* Sign mask */

	i = sscanf(cp, "%lf%n", &d, &n);
	if(i == 0)
		return 0;				/* Wasn't able to convert */

	cp += n;
	if(endp)
		*endp = cp;

	if(d == 0.0)
	{
		flt[0] = flt[1] = flt[2] = flt[3] = 0; /* All-bits-zero equals zero */
		return 1;					/* Good job. */
	}

	frac = frexp(d, &sexp);			/* Separate into exponent and mantissa */
	if(sexp < -128 || sexp > 127)
		return 0;					/* Exponent out of range. */

	exp = sexp + 128;				/* Make excess-128 mode */
	exp &= 0xff;					/* express in 8 bits */

	if(frac < 0)
	{
		sign = 0100000;				/* Negative sign */
		frac = -frac;				/* fix the mantissa */
	}

	/* The following big literal is 2 to the 49th power: */
	ufrac = (ulong64) (frac * 72057594037927936.0); /* Align fraction bits */

	/* Round from FLT4 to FLT2 */
	if(size < 4)
	{
		ufrac += 0x80000000;		/* Round to nearest 32-bit
									   representation */

		if(ufrac > 0x200000000000)	/* Overflow? */
		{
			ufrac >>= 1;			/* Normalize */
			exp--;
		}
	}

	flt[0] = (unsigned) (sign | (exp << 7) | (ufrac >> 48) & 0x7F);
	if(size > 1)
	{
		flt[1] = (unsigned) ((ufrac >> 32) & 0xffff);
		if(size > 2)
		{
			flt[2] = (unsigned) ((ufrac >> 16) & 0xffff);
			flt[3] = (unsigned) ((ufrac >>  0) & 0xffff);
		}
	}

	return 1;
}

/* Allocate an EX_TREE */

static EX_TREE *new_ex_tree(void)
{
	EX_TREE *tr = memcheck(malloc(sizeof(EX_TREE)));
	return tr;
}


/* Create an EX_TREE representing a parse error */

static EX_TREE *ex_err(EX_TREE *tp, char *cp)
{
	EX_TREE *errtp;

	errtp = new_ex_tree();
	errtp->cp = cp;
	errtp->type = EX_ERR;
	errtp->data.child.left = tp;

	return errtp;
}

/* Create an EX_TREE representing a literal value */

static EX_TREE *new_ex_lit(unsigned value)
{
	EX_TREE *tp;

	tp = new_ex_tree();
	tp->type = EX_LIT;
	tp->data.lit = value;

	return tp;
}

/* The recursive-descent expression parser parse_expr. */

/* This parser was designed for expressions with operator precedence.
   However, MACRO-11 doesn't observe any sort of operator precedence.
   If you feel your source deserves better, give the operators
   appropriate precedence values right here. */

#define ADD_PREC 1
#define MUL_PREC 1
#define AND_PREC 1
#define OR_PREC 1

EX_TREE *parse_unary(char *cp);	/* Prototype for forward calls */

EX_TREE *parse_binary(char *cp, char term, int depth)
{
	EX_TREE *leftp, *rightp, *tp;

	leftp = parse_unary(cp);

	while(leftp->type != EX_ERR)
	{
		cp = skipwhite(leftp->cp);

		if(*cp == term)
			return leftp;

		switch(*cp)
		{
		case '+':
			if(depth >= ADD_PREC)
				return leftp;

			rightp = parse_binary(cp+1, term, ADD_PREC);
			tp = new_ex_tree();
			tp->type = EX_ADD;
			tp->data.child.left = leftp;
			tp->data.child.right = rightp;
			tp->cp = rightp->cp;
			leftp = tp;
			break;

		case '-':
			if(depth >= ADD_PREC)
				return leftp;

			rightp = parse_binary(cp+1, term, ADD_PREC);
			tp = new_ex_tree();
			tp->type = EX_SUB;
			tp->data.child.left = leftp;
			tp->data.child.right = rightp;
			tp->cp = rightp->cp;
			leftp = tp;
			break;

		case '*':
			if(depth >= MUL_PREC)
				return leftp;

			rightp = parse_binary(cp+1, term, MUL_PREC);
			tp = new_ex_tree();
			tp->type = EX_MUL;
			tp->data.child.left = leftp;
			tp->data.child.right = rightp;
			tp->cp = rightp->cp;
			leftp = tp;
			break;

		case '/':
			if(depth >= MUL_PREC)
				return leftp;

			rightp = parse_binary(cp+1, term, MUL_PREC);
			tp = new_ex_tree();
			tp->type = EX_DIV;
			tp->data.child.left = leftp;
			tp->data.child.right = rightp;
			tp->cp = rightp->cp;
			leftp = tp;
			break;

		case '!':
			if(depth >= OR_PREC)
				return leftp;

			rightp = parse_binary(cp+1, term, 2);
			tp = new_ex_tree();
			tp->type = EX_OR;
			tp->data.child.left = leftp;
			tp->data.child.right = rightp;
			tp->cp = rightp->cp;
			leftp = tp;
			break;

		case '&':
			if(depth >= AND_PREC)
				return leftp;

			rightp = parse_binary(cp+1, term, AND_PREC);
			tp = new_ex_tree();
			tp->type = EX_AND;
			tp->data.child.left = leftp;
			tp->data.child.right = rightp;
			tp->cp = rightp->cp;
			leftp = tp;
			break;

		default:
			/* Some unknown character.  Let caller decide if it's okay. */

			return leftp;

		} /* end switch */
	} /* end while */

	/* Can't be reached except by error. */
	return leftp;
}

/* get_symbol is used all over the place to pull a symbol out of the
   text.  */

static char *get_symbol(char *cp, char **endp, int *islocal)
{
	int len;
	char *symcp;
	int digits = 0;

	cp = skipwhite(cp);	/* Skip leading whitespace */

	if(!issym(*cp))
		return NULL;

	digits = 0;
	if(isdigit(*cp))
		digits = 2;				/* Think about digit count */

	for(symcp = cp + 1; issym(*symcp); symcp++)
	{
		if(!isdigit(*symcp))	/* Not a digit? */
			digits--;			/* Make a note. */
	}

	if(digits == 2)
		return NULL;			/* Not a symbol, it's a digit string */

	if(endp)
		*endp = symcp;

	len = symcp - cp;

	/* Now limit length */
	if(len > SYMMAX)
		len = SYMMAX;

	symcp = memcheck(malloc(len + 1));

	memcpy(symcp, cp, len);
	symcp[len] = 0;
	upcase(symcp);

	if(islocal)
	{
		*islocal = 0;

		/* Turn to local label format */
		if(digits == 1)
		{
			if(symcp[len-1] == '$')
			{
				char *newsym = memcheck(malloc(32));	/* Overkill */
				sprintf(newsym, "%d$%d", strtol(symcp, NULL, 10), lsb);
				free(symcp);
				symcp = newsym;
				if(islocal)
					*islocal = LOCAL;
			}
			else
			{
				free(symcp);
				return NULL;
			}
		}
	}
	else
	{
		/* disallow local label format */
		if(isdigit(*symcp))
		{
			free(symcp);
			return NULL;
		}
	}

	return symcp;
}

/*
  brackrange is used to find a range of text which may or may not be
  bracketed.

  If the brackets are <>, then nested brackets are detected.
  If the brackets are of the form ^/.../ no detection of nesting is
  attempted.

  Using brackets ^<...< will mess this routine up.  What in the world
  are you thinking?
*/

int brackrange(char *cp, int *start, int *length, char **endp)
{
	char endstr[6];
	int endlen;
	int nest;
	int len;

	switch(*cp)
	{
	case '^':
		endstr[0] = cp[1];
		strcpy(endstr+1, "\n");
		*start = 2;
		endlen = 1;
		break;
	case '<':
		strcpy(endstr, "<>\n");
		endlen = 1;
		*start = 1;
		break;
	default:
		return FALSE;
	}

	cp += *start;

	len = 0;
	nest = 1;
	while(nest)
	{
		int sublen;
		sublen = strcspn(cp+len, endstr);
		if(cp[len+sublen] == '<')
			nest++;
		else
			nest--;
		len += sublen;
	}

	*length = len;
	if(endp)
		*endp = cp + len + endlen;

	return 1;
}

/* parse_unary parses out a unary operator or leaf expression.  */

EX_TREE *parse_unary(char *cp)
{
	EX_TREE *tp;

	/* Skip leading whitespace */
	cp = skipwhite(cp);

	if(*cp == '%')				/* Register notation */
	{
		unsigned reg;
		cp++;
		reg = strtoul(cp, &cp, 8);
		if(reg > 7)
			return ex_err(NULL, cp);

		/* This returns references to the built-in register symbols */
		tp = new_ex_tree();
		tp->type = EX_SYM;
		tp->data.symbol = reg_sym[reg];
		tp->cp = cp;
		return tp;
	}

	/* Unary negate */
	if(*cp == '-')
	{
		tp = new_ex_tree();
		tp->type = EX_NEG;
		tp->data.child.left = parse_unary(cp+1);
		tp->cp = tp->data.child.left->cp;
		return tp;
	}

	/* Unary + I can ignore. */
	if(*cp == '+')
		return parse_unary(cp+1);

	if(*cp == '^')
	{
		int save_radix;
		switch(tolower(cp[1]))
		{
		case 'c':
			/* ^C, ones complement */
			tp = new_ex_tree();
			tp->type = EX_COM;
			tp->data.child.left = parse_unary(cp+2);
			tp->cp = tp->data.child.left->cp;
			return tp;
		case 'b':
			/* ^B, binary radix modifier */
			save_radix = radix;
			radix = 2;
			tp = parse_unary(cp+2);
			radix = save_radix;
			return tp;
		case 'o':
			/* ^O, octal radix modifier */
			save_radix = radix;
			radix = 8;
			tp = parse_unary(cp+2);
			radix = save_radix;
			return tp;
		case 'd':
			/* ^D, decimal radix modifier */
			save_radix = radix;
			radix = 10;
			tp = parse_unary(cp+2);
			radix = save_radix;
			return tp;
		case 'x':
			/* An enhancement!  ^X, hexadecimal radix modifier */
			save_radix = radix;
			radix = 16;
			tp = parse_unary(cp+2);
			radix = save_radix;
			return tp;
		case 'r':
			/* ^R, RAD50 literal */
			{
				int start, len;
				char *endcp;
				unsigned value;
				cp += 2;
				if(brackrange(cp, &start, &len, &endcp))
					value = rad50(cp+start, NULL);
				else
					value = rad50(cp, &endcp);
				tp = new_ex_lit(value);
				tp->cp = endcp;
				return tp;
			}
		case 'f':
			/* ^F, single-word floating point literal indicator */
			{
				unsigned flt[1];
				char *endcp;
				if(!parse_float(cp+2, &endcp, 1, flt))
				{
					tp = ex_err(NULL, cp+2);
				}
				else
				{
					tp = new_ex_lit(flt[0]);
					tp->cp = endcp;
				}
				return tp;
			}
		}

		if(ispunct(cp[1]))
		{
			char *ecp;
			/* oddly-bracketed expression like this: ^/expression/ */
			tp = parse_binary(cp+2, cp[1], 0);
			ecp = skipwhite(tp->cp);

			if(*ecp != cp[1])
				return ex_err(tp, ecp);

			tp->cp = ecp + 1;
			return tp;
		}
	}

	/* Bracketed subexpression */
	if(*cp == '<')
	{
		char *ecp;
		tp = parse_binary(cp+1, '>', 0);
		ecp = skipwhite(tp->cp);
		if(*ecp != '>')
			return ex_err(tp, ecp);

		tp->cp = ecp + 1;
		return tp;
	}

	/* Check for ASCII constants */

	if(*cp == '\'')
	{
		/* 'x single ASCII character */
		cp++;
		tp = new_ex_tree();
		tp->type = EX_LIT;
		tp->data.lit = *cp & 0xff;
		tp->cp = ++cp;
		return tp;
	}

	if(*cp == '\"')
	{
		/* "xx ASCII character pair */
		cp++;
		tp = new_ex_tree();
		tp->type = EX_LIT;
		tp->data.lit = (cp[0] & 0xff) | ((cp[1] & 0xff) << 8);
		tp->cp = cp + 2;
		return tp;
	}

	/* Numeric constants are trickier than they need to be, */
	/* since local labels start with a digit too. */
	if(isdigit(*cp))
	{
		char *label;
		int local;

		if((label = get_symbol(cp, NULL, &local)) == NULL)
		{
			char *endcp;
			unsigned long value;
			int rad = radix;

			/* get_symbol returning NULL assures me that it's not a
			   local label.  */

			/* Look for a trailing period, to indicate decimal... */
			for(endcp = cp; isdigit(*endcp); endcp++)
				;
			if(*endcp == '.')
				rad = 10;

			value = strtoul(cp, &endcp, rad);
			if(*endcp == '.')
				endcp++;

			tp = new_ex_tree();
			tp->type = EX_LIT;
			tp->data.lit = value;
			tp->cp = endcp;

			return tp;
		}

		free(label);
	}

	/* Now check for a symbol */

	{
		char *label;
		int local;
		SYMBOL *sym;

		/* Optimization opportunity: I don't really need to call
		   get_symbol a second time. */

		if(!(label = get_symbol(cp, &cp, &local)))
		{
			tp = ex_err(NULL, cp);	/* Not a valid label. */
			return tp;
		}

		sym = lookup_sym(label, &symbol_st);
		if(sym == NULL)
		{
			/* A symbol from the "PST", which means an instruction
			   code. */
			sym = lookup_sym(label, &system_st);
		}

		if(sym != NULL)
		{
			tp = new_ex_tree();
			tp->cp = cp;
			tp->type = EX_SYM;
			tp->data.symbol = sym;

			free(label);
			return tp;
		}

		/* The symbol was not found. Create an "undefined symbol"
		   reference. */
		sym = memcheck(malloc(sizeof(SYMBOL)));
		sym->label = label;
		sym->flags = UNDEFINED | local;
		sym->stmtno = stmtno;
		sym->next = NULL;
		sym->section = &absolute_section;
		sym->value = 0;

		tp = new_ex_tree();
		tp->cp = cp;
		tp->type = EX_UNDEFINED_SYM;
		tp->data.symbol = sym;

		return tp;
	}
}

/* Diagnostic: symflags returns a char* which gives flags I can use to
   show the context of a symbol. */

static char *symflags(SYMBOL *sym)
{
	static char temp[8];
	char *fp = temp;
	if(sym->flags & GLOBAL)
		*fp++ = 'G';
	if(sym->flags & PERMANENT)
		*fp++ = 'P';
	if(sym->flags & DEFINITION)
		*fp++ = 'D';
	*fp = 0;
	return fp;
}

/* Diagnostic: print an expression tree.  I used this in various
   places to help me diagnose parse problems, by putting in calls to
   print_tree when I didn't understand why something wasn't working.
   This is currently dead code, nothing calls it; but I don't want it
   to go away. Hopefully the compiler will realize when it's dead, and
   eliminate it. */

static void print_tree(FILE *printfile, EX_TREE *tp, int depth)
{
	SYMBOL *sym;

	switch(tp->type)
	{
	case EX_LIT:
		fprintf(printfile, "%o", tp->data.lit & 0177777);
		break;

	case EX_SYM:
	case EX_TEMP_SYM:
		sym = tp->data.symbol;
		fprintf(printfile, "%s{%s%o:%s}", tp->data.symbol->label,
				symflags(sym), sym->value, sym->section->label);
		break;

	case EX_UNDEFINED_SYM:
		fprintf(printfile, "%s{%o:undefined}", tp->data.symbol->label,
				tp->data.symbol->value);
		break;

	case EX_COM:
		fprintf(printfile, "^C<");
		print_tree(printfile, tp->data.child.left, depth+4);
		fprintf(printfile, ">");
		break;

	case EX_NEG:
		fprintf(printfile, "-<");
		print_tree(printfile, tp->data.child.left, depth+4);
		fputc('>', printfile);
		break;

	case EX_ERR:
		fprintf(printfile, "{expression error}");
		if(tp->data.child.left)
		{
			fputc('<', printfile);
			print_tree(printfile, tp->data.child.left, depth+4);
			fputc('>', printfile);
		}
		break;

	case EX_ADD:
		fputc('<', printfile);
		print_tree(printfile, tp->data.child.left, depth+4);
		fputc('+', printfile);
		print_tree(printfile, tp->data.child.right, depth+4);
		fputc('>', printfile);
		break;

	case EX_SUB:
		fputc('<', printfile);
		print_tree(printfile, tp->data.child.left, depth+4);
		fputc('-', printfile);
		print_tree(printfile, tp->data.child.right, depth+4);
		fputc('>', printfile);
		break;

	case EX_MUL:
		fputc('<', printfile);
		print_tree(printfile, tp->data.child.left, depth+4);
		fputc('*', printfile);
		print_tree(printfile, tp->data.child.right, depth+4);
		fputc('>', printfile);
		break;

	case EX_DIV:
		fputc('<', printfile);
		print_tree(printfile, tp->data.child.left, depth+4);
		fputc('/', printfile);
		print_tree(printfile, tp->data.child.right, depth+4);
		fputc('>', printfile);
		break;

	case EX_AND:
		fputc('<', printfile);
		print_tree(printfile, tp->data.child.left, depth+4);
		fputc('&', printfile);
		print_tree(printfile, tp->data.child.right, depth+4);
		fputc('>', printfile);
		break;

	case EX_OR:
		fputc('<', printfile);
		print_tree(printfile, tp->data.child.left, depth+4);
		fputc('!', printfile);
		print_tree(printfile, tp->data.child.right, depth+4);
		fputc('>', printfile);
		break;
	}

	if(depth == 0)
		fputc('\n', printfile);
}

/* free_tree frees an expression tree. */

static void free_tree(EX_TREE *tp)
{
	switch(tp->type)
	{
	case EX_UNDEFINED_SYM:
	case EX_TEMP_SYM:
		free(tp->data.symbol->label);
		free(tp->data.symbol);
	case EX_LIT:
	case EX_SYM:
		free(tp);
		break;

	case EX_COM:
	case EX_NEG:
		free_tree(tp->data.child.left);
		free(tp);
		break;

	case EX_ERR:
		if(tp->data.child.left)
			free_tree(tp->data.child.left);
		free(tp);
		break;

	case EX_ADD:
	case EX_SUB:
	case EX_MUL:
	case EX_DIV:
	case EX_AND:
	case EX_OR:
		free_tree(tp->data.child.left);
		free_tree(tp->data.child.right);
		free(tp);
		break;
	}
}

/* new_temp_sym allocates a new EX_TREE entry of type "TEMPORARY
   SYMBOL" (slight semantic difference from "UNDEFINED"). */

static EX_TREE *new_temp_sym(char *label, SECTION *section, unsigned value)
{
	SYMBOL *sym;
	EX_TREE *tp;

	sym = memcheck(malloc(sizeof(SYMBOL)));
	sym->label = memcheck(strdup(label));
	sym->flags = 0;
	sym->stmtno = stmtno;
	sym->next = NULL;
	sym->section = section;
	sym->value = value;

	tp = new_ex_tree();
	tp->type = EX_TEMP_SYM;
	tp->data.symbol = sym;

	return tp;
}

#define RELTYPE(tp) (((tp)->type == EX_SYM || (tp)->type == EX_TEMP_SYM) && \
		(tp)->data.symbol->section->flags & PSECT_REL)

/* evaluate "evaluates" an EX_TREE, ideally trying to produce a
   constant value, else a symbol plus an offset.  */

static EX_TREE *evaluate(EX_TREE *tp, int undef)
{
	EX_TREE *res;
	char *cp = tp->cp;

	switch(tp->type)
	{
	case EX_SYM:
		{
			SYMBOL *sym = tp->data.symbol;

			/* Change some symbols to "undefined" */

			if(undef)
			{
				int change = 0;

#if 0							/* I'd prefer this behavior, but
								   MACRO.SAV is a bit too
								   primitive. */
				/* A temporary symbol defined later is "undefined." */
				if(!(sym->flags & PERMANENT) && sym->stmtno > stmtno)
					change = 1;
#endif

				/* A global symbol with no assignment is "undefined." */
				/* Go figure. */
				if((sym->flags & (GLOBAL|DEFINITION)) == GLOBAL)
					change = 1;

				if(change)
				{
					res = new_temp_sym(tp->data.symbol->label,
						tp->data.symbol->section, tp->data.symbol->value);
					res->type = EX_UNDEFINED_SYM;
					break;
				}
			}

			/* Turn defined absolute symbol to a literal */
			if(!(sym->section->flags & PSECT_REL) &&
				(sym->flags & (GLOBAL|DEFINITION)) != GLOBAL &&
				sym->section->type != REGISTER)
			{
				res = new_ex_lit(sym->value);
				break;
			}

			/* Make a temp copy of any reference to "." since it might
			   change as complex relocatable expressions are written out
			*/
			if(strcmp(sym->label, ".") == 0)
			{
				res = new_temp_sym(".", sym->section, sym->value);
				break;
			}

			/* Copy other symbol reference verbatim. */
			res = new_ex_tree();
			res->type = EX_SYM;
			res->data.symbol = tp->data.symbol;
			res->cp = tp->cp;
			break;
		}

	case EX_LIT:
		res = new_ex_tree();
		*res = *tp;
		break;

	case EX_TEMP_SYM:
	case EX_UNDEFINED_SYM:
		/* Copy temp and undefined symbols */
		res = new_temp_sym(tp->data.symbol->label,
						   tp->data.symbol->section,
						   tp->data.symbol->value);
		res->type = tp->type;
		break;

	case EX_COM:
		/* Complement */
		tp = evaluate(tp->data.child.left, undef);
		if(tp->type == EX_LIT)
		{
			/* Complement the literal */
			res = new_ex_lit(~tp->data.lit);
			free_tree(tp);
		}
		else
		{
			/* Copy verbatim. */
			res = new_ex_tree();
			res->type = EX_NEG;
			res->cp = tp->cp;
			res->data.child.left = tp;
		}

		break;

	case EX_NEG:
		tp = evaluate(tp->data.child.left, undef);
		if(tp->type == EX_LIT)
		{
			/* negate literal */
			res = new_ex_lit((unsigned)-(int)tp->data.lit);
			free_tree(tp);
		}
		else if(tp->type == EX_SYM || tp->type == EX_TEMP_SYM)
		{
			/* Make a temp sym with the negative value of the given
			   sym (this works for symbols within relocatable sections
			   too) */
			res = new_temp_sym("*TEMP", tp->data.symbol->section,
				(unsigned)-(int)tp->data.symbol->value);
			res->cp = tp->cp;
			free_tree(tp);
		}
		else
		{
			/* Copy verbatim. */
			res = new_ex_tree();
			res->type = EX_NEG;
			res->cp = tp->cp;
			res->data.child.left = tp;
		}
		break;

	case EX_ERR:
		/* Copy */
		res = ex_err(tp->data.child.left, tp->cp);
		break;

	case EX_ADD:
		{
			EX_TREE *left, *right;

			left = evaluate(tp->data.child.left, undef);
			right = evaluate(tp->data.child.right, undef);

			/* Both literals?  Sum them and return result. */
			if(left->type == EX_LIT && right->type == EX_LIT)
			{
				res = new_ex_lit(left->data.lit + right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			/* Commutative: A+x == x+A.
			   Simplify by putting the literal on the right */
			if(left->type == EX_LIT)
			{
				EX_TREE *temp = left;
				left = right;
				right = temp;
			}

			if(right->type == EX_LIT &&		/* Anything plus 0 == itself */
				right->data.lit == 0)
			{
				res = left;
				free_tree(right);
				break;
			}

			/* Relative symbol plus lit is replaced with a temp sym
			   holding the sum */
			if(RELTYPE(left) && right->type == EX_LIT)
			{
				SYMBOL *sym = left->data.symbol;
				res = new_temp_sym("*ADD", sym->section, sym->value +
								   right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			/* Associative:  <A+x>+y == A+<x+y> */
			/*  and if x+y is constant, I can do that math. */
			if(left->type == EX_ADD && right->type == EX_LIT)
			{
				EX_TREE *leftright = left->data.child.right;
				if(leftright->type == EX_LIT)
				{
					/* Do the shuffle */
					res = left;
					leftright->data.lit += right->data.lit;
					free_tree(right);
					break;
				}
			}

			/* Associative:  <A-x>+y == A+<y-x> */
			/*  and if y-x is constant, I can do that math. */
			if(left->type == EX_SUB && right->type == EX_LIT)
			{
				EX_TREE *leftright = left->data.child.right;
				if(leftright->type == EX_LIT)
				{
					/* Do the shuffle */
					res = left;
					leftright->data.lit = right->data.lit - leftright->data.lit;
					free_tree(right);
					break;
				}
			}

			/* Anything else returns verbatim */
			res = new_ex_tree();
			res->type = EX_ADD;
			res->data.child.left = left;
			res->data.child.right = right;
		}
		break;

	case EX_SUB:
		{
			EX_TREE *left, *right;

			left = evaluate(tp->data.child.left, undef);
			right = evaluate(tp->data.child.right, undef);

			/* Both literals?  Subtract them and return a lit. */
			if(left->type == EX_LIT && right->type == EX_LIT)
			{
				res = new_ex_lit(left->data.lit - right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			if(right->type == EX_LIT &&		/* Symbol minus 0 == symbol */
				right->data.lit == 0)
			{
				res = left;
				free_tree(right);
				break;
			}

			/* A relocatable minus an absolute - make a new temp sym
			   to represent that. */
			if(RELTYPE(left) &&
				right->type == EX_LIT)
			{
				SYMBOL *sym = left->data.symbol;
				res = new_temp_sym("*SUB", sym->section,
								   sym->value - right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			if(RELTYPE(left) &&
				RELTYPE(right) &&
				left->data.symbol->section == right->data.symbol->section)
			{
				/* Two defined symbols in the same psect.  Resolve
				   their difference as a literal. */
				res = new_ex_lit(left->data.symbol->value -
								 right->data.symbol->value);
				free_tree(left);
				free_tree(right);
				break;
			}

			/* Associative:  <A+x>-y == A+<x-y> */
			/*  and if x-y is constant, I can do that math. */
			if(left->type == EX_ADD && right->type == EX_LIT)
			{
				EX_TREE *leftright = left->data.child.right;
				if(leftright->type == EX_LIT)
				{
					/* Do the shuffle */
					res = left;
					leftright->data.lit -= right->data.lit;
					free_tree(right);
					break;
				}
			}

			/* Associative:  <A-x>-y == A-<x+y> */
			/*  and if x+y is constant, I can do that math. */
			if(left->type == EX_SUB && right->type == EX_LIT)
			{
				EX_TREE *leftright = left->data.child.right;
				if(leftright->type == EX_LIT)
				{
					/* Do the shuffle */
					res = left;
					leftright->data.lit += right->data.lit;
					free_tree(right);
					break;
				}
			}

			/* Anything else returns verbatim */
			res = new_ex_tree();
			res->type = EX_SUB;
			res->data.child.left = left;
			res->data.child.right = right;
		}
		break;

	case EX_MUL:
		{
			EX_TREE *left, *right;

			left = evaluate(tp->data.child.left, undef);
			right = evaluate(tp->data.child.right, undef);

			/* Can only multiply if both are literals */
			if(left->type == EX_LIT && right->type == EX_LIT)
			{
				res = new_ex_lit(left->data.lit * right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			/* Commutative: A*x == x*A.
			   Simplify by putting the literal on the right */
			if(left->type == EX_LIT)
			{
				EX_TREE *temp = left;
				left = right;
				right = temp;
			}

			if(right->type == EX_LIT &&		/* Symbol times 1 == symbol */
				right->data.lit == 1)
			{
				res = left;
				free_tree(right);
				break;
			}

			if(right->type == EX_LIT &&		/* Symbol times 0 == 0 */
				right->data.lit == 0)
			{
				res = right;
				free_tree(left);
				break;
			}

			/* Associative: <A*x>*y == A*<x*y> */
			/* If x*y is constant, I can do this math. */
			/* Is this safe?  I will potentially be doing it */
			/* with greater accuracy than the target platform. */
			/* Hmmm. */

			if(left->type == EX_MUL && right->type == EX_LIT)
			{
				EX_TREE *leftright = left->data.child.right;
				if(leftright->type == EX_LIT)
				{
					/* Do the shuffle */
					res = left;
					leftright->data.lit *= right->data.lit;
					free_tree(right);
					break;
				}
			}

			/* Anything else returns verbatim */
			res = new_ex_tree();
			res->type = EX_MUL;
			res->data.child.left = left;
			res->data.child.right = right;
		}
		break;

	case EX_DIV:
		{
			EX_TREE *left, *right;

			left = evaluate(tp->data.child.left, undef);
			right = evaluate(tp->data.child.right, undef);

			/* Can only divide if both are literals */
			if(left->type == EX_LIT && right->type == EX_LIT)
			{
				res = new_ex_lit(left->data.lit / right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			if(right->type == EX_LIT &&		/* Symbol divided by 1 == symbol */
				right->data.lit == 1)
			{
				res = left;
				free_tree(right);
				break;
			}

			/* Anything else returns verbatim */
			res = new_ex_tree();
			res->type = EX_DIV;
			res->data.child.left = left;
			res->data.child.right = right;
		}
		break;

	case EX_AND:
		{
			EX_TREE *left, *right;

			left = evaluate(tp->data.child.left, undef);
			right = evaluate(tp->data.child.right, undef);

			/* Operate if both are literals */
			if(left->type == EX_LIT && right->type == EX_LIT)
			{
				res = new_ex_lit(left->data.lit & right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			/* Commutative: A&x == x&A.
			   Simplify by putting the literal on the right */
			if(left->type == EX_LIT)
			{
				EX_TREE *temp = left;
				left = right;
				right = temp;
			}

			if(right->type == EX_LIT &&		/* Symbol AND 0 == 0 */
				right->data.lit == 0)
			{
				res = new_ex_lit(0);
				free_tree(left);
				free_tree(right);
				break;
			}

			if(right->type == EX_LIT &&		/* Symbol AND 0177777 == symbol */
				right->data.lit == 0177777)
			{
				res = left;
				free_tree(right);
				break;
			}

			/* Anything else returns verbatim */
			res = new_ex_tree();
			res->type = EX_AND;
			res->data.child.left = left;
			res->data.child.right = right;
		}
		break;

	case EX_OR:
		{
			EX_TREE *left, *right;

			left = evaluate(tp->data.child.left, undef);
			right = evaluate(tp->data.child.right, undef);

			/* Operate if both are literals */
			if(left->type == EX_LIT && right->type == EX_LIT)
			{
				res = new_ex_lit(left->data.lit | right->data.lit);
				free_tree(left);
				free_tree(right);
				break;
			}

			/* Commutative: A!x == x!A.
			   Simplify by putting the literal on the right */
			if(left->type == EX_LIT)
			{
				EX_TREE *temp = left;
				left = right;
				right = temp;
			}

			if(right->type == EX_LIT &&		/* Symbol OR 0 == symbol */
				right->data.lit == 0)
			{
				res = left;
				free_tree(right);
				break;
			}

			if(right->type == EX_LIT &&		/* Symbol OR 0177777 == 0177777 */
				right->data.lit == 0177777)
			{
				res = new_ex_lit(0177777);
				free_tree(left);
				free_tree(right);
				break;
			}

			/* Anything else returns verbatim */
			res = new_ex_tree();
			res->type = EX_OR;
			res->data.child.left = left;
			res->data.child.right = right;
		}
		break;
	}

	res->cp = cp;
	return res;
}

/*
  parse_expr - this gets called everywhere.  It parses and evaluates
  an arithmetic expression.
*/

EX_TREE *parse_expr(char *cp, int undef)
{
	EX_TREE *expr;
	EX_TREE *value;

	expr = parse_binary(cp, 0, 0); /* Parse into a tree */
	value = evaluate(expr, undef); /* Perform the arithmetic */
	value->cp = expr->cp;		/* Pointer to end of text is part of
								   the rootmost node  */
	free_tree(expr);			/* Discard parse in favor of
                                   evaluation */

	return value;
}

/* free_addr_mode frees the storage consumed by an addr_mode */

static void free_addr_mode(ADDR_MODE *mode)
{
	if(mode->offset)
		free_tree(mode->offset);
	mode->offset = NULL;
}

/* Get the register indicated by the expression */

#define NO_REG 0777

static unsigned get_register(EX_TREE *expr)
{
	unsigned reg;

	if(expr->type == EX_LIT &&
		expr->data.lit <= 7)
	{
		reg = expr->data.lit;
		return reg;
	}

	if(expr->type == EX_SYM &&
		expr->data.symbol->section->type == REGISTER)
	{
		reg = expr->data.symbol->value;
		return reg;
	}

	return NO_REG;
}

/* get_mode - parse a general addressing mode. */

int get_mode(char *cp, char **endp, ADDR_MODE *mode)
{
	EX_TREE *value;

	mode->offset = NULL;
	mode->rel = 0;
	mode->type = 0;

	cp = skipwhite(cp);

	/* @ means "indirect," sets bit 3 */
	if(*cp == '@')
	{
		cp++;
		mode->type |= 010;
	}

	/* Immediate modes #imm and @#imm */
	if(*cp == '#')
	{
		cp++;
		mode->type |= 027;
		mode->offset = parse_expr(cp, 0);
		if(endp)
			*endp = mode->offset->cp;
		return TRUE;
	}

	/* Check for -(Rn) */

	if(*cp == '-')
	{
		char *tcp = skipwhite(cp + 1);
		if(*tcp++ == '(')
		{
			unsigned reg;
			/* It's -(Rn) */
			value = parse_expr(tcp, 0);
			reg = get_register(value);
			if(reg == NO_REG ||
				(tcp = skipwhite(value->cp), *tcp++ != ')'))
			{
				free_tree(value);
				return FALSE;
			}
			mode->type |= 040 | reg;
			if(endp)
				*endp = tcp;
			free_tree(value);
			return TRUE;
		}
	}

	/* Check for (Rn) */
	if(*cp == '(')
	{
		char *tcp;
		unsigned reg;
		value = parse_expr(cp + 1, 0);
		reg = get_register(value);

		if(reg == NO_REG ||
			(tcp = skipwhite(value->cp), *tcp++ != ')'))
		{
			free_tree(value);
			return FALSE;
		}

		tcp = skipwhite(tcp);
		if(*tcp == '+')
		{
			tcp++;				/* It's (Rn)+ */
			if(endp)
				*endp = tcp;
			mode->type |= 020 | reg;
			free_tree(value);
			return TRUE;
		}

		if(mode->type == 010)		/* For @(Rn) there's an implied 0 offset */
		{
			mode->offset = new_ex_lit(0);
			mode->type |= 060 | reg;
			free_tree(value);
			if(endp)
				*endp = tcp;
			return TRUE;
		}

		mode->type |= 010 | reg;	/* Mode 10 is register indirect as
									   in (Rn) */
		free_tree(value);
		if(endp)
			*endp = tcp;
		return TRUE;
	}

	/* Modes with an offset */

	mode->offset = parse_expr(cp, 0);

	cp = skipwhite(mode->offset->cp);

	if(*cp == '(')
	{
		unsigned reg;
		/* indirect register plus offset */
		value = parse_expr(cp+1, 0);
		reg = get_register(value);
		if(reg == NO_REG ||
			(cp = skipwhite(value->cp), *cp++ != ')'))
		{
			free_tree(value);
			return FALSE;		/* Syntax error in addressing mode */
		}

		mode->type |= 060 | reg;

		free_tree(value);

		if(endp)
			*endp = cp;
		return TRUE;
	}

	/* Plain old expression. */

	if(endp)
		*endp = cp;

	/* It might be a register, though. */
	if(mode->offset->type == EX_SYM)
	{
		SYMBOL *sym = mode->offset->data.symbol;
		if(sym->section->type == REGISTER)
		{
			free_tree(mode->offset);
			mode->offset = NULL;
			mode->type |= sym->value;
			return TRUE;
		}
	}

	/* It's either 067 (PC-relative) or 037 (absolute) mode, depending */
	/* on user option. */

	if(mode->type & 010)		/* Have already noted indirection? */
	{
		mode->type |= 067;		/* If so, then PC-relative is the only
								   option */
		mode->rel = 1;			/* Note PC-relative */
	}
	else if(enabl_ama)			/* User asked for absolute adressing? */
	{
		mode->type |= 037;		/* Give it to him. */
	}
	else
	{
		mode->type |= 067;		/* PC-relative */
		mode->rel = 1;			/* Note PC-relative */
	}

	return TRUE;
}

/*
  implicit_gbl is a self-recursive routine that adds undefined symbols
  to the "implicit globals" symbol table.
*/

void implicit_gbl(EX_TREE *value)
{
	if(pass)
		return;					/* Only do this in first pass */
	
	if(!enabl_gbl)
		return;					/* Option not enabled, don't do it. */

	switch(value->type)
	{
	case EX_UNDEFINED_SYM:
		{
			SYMBOL *sym;
			if(!(value->data.symbol->flags & LOCAL)) /* Unless it's a
														local symbol, */
			{
				sym = add_sym(value->data.symbol->label,
					0, GLOBAL, &absolute_section, &implicit_st);
			}
		}
		break;
	case EX_LIT:
	case EX_SYM:
		return;
	case EX_ADD:
	case EX_SUB:
	case EX_MUL:
	case EX_DIV:
	case EX_AND:
	case EX_OR:
		implicit_gbl(value->data.child.right);
		/* falls into... */
	case EX_COM:
	case EX_NEG:
		implicit_gbl(value->data.child.left);
		break;
	case EX_ERR:
		if(value->data.child.left)
			implicit_gbl(value->data.child.left);
		break;
	}
}

/* Done between the first and second passes */
/* Migrates the symbols from the "implicit" table into the main table. */

static void migrate_implicit(void)
{
	SYMBOL_ITER iter;
	SYMBOL *isym, *sym;

	for(isym = first_sym(&implicit_st, &iter);
		isym != NULL;
		isym = next_sym(&implicit_st, &iter))
	{
		sym = lookup_sym(isym->label, &symbol_st);
		if(sym)
			continue;			// It's already in there.  Great.
		sym = add_sym(isym->label, isym->value, isym->flags,
					  isym->section, &symbol_st);
		// Just one other thing - migrate the stmtno
		sym->stmtno = isym->stmtno;
	}
}

static int express_sym_offset(EX_TREE *value, SYMBOL **sym, unsigned *offset)
{
	implicit_gbl(value);		/* Translate tree's undefined syms
								   into global syms */

	/* Internally relocatable symbols will have been summed down into
	   EX_TEMP_SYM's. */

	if(value->type == EX_SYM ||
		value->type == EX_TEMP_SYM)
	{
		*sym = value->data.symbol;
		*offset = 0;
		return 1;
	}

	/* What remains is external symbols. */

	if(value->type == EX_ADD)
	{
		EX_TREE *left = value->data.child.left;
		EX_TREE *right = value->data.child.right;
		if((left->type != EX_SYM &&
			left->type != EX_UNDEFINED_SYM) ||
			right->type != EX_LIT)
			return 0;				/* Failed. */
		*sym = left->data.symbol;
		*offset = right->data.lit;
		return 1;
	}

	if(value->type == EX_SUB)
	{
		EX_TREE *left = value->data.child.left;
		EX_TREE *right = value->data.child.right;
		if((left->type != EX_SYM &&
			left->type != EX_UNDEFINED_SYM) ||
			right->type != EX_LIT)
			return 0;				/* Failed. */
		*sym = left->data.symbol;
		*offset = (unsigned)-(int)(right->data.lit);
		return 1;
	}

	return 0;
}

/*
  Translate an EX_TREE into a TEXT_COMPLEX suitable for encoding
  into the object file. */

int complex_tree(TEXT_COMPLEX *tx, EX_TREE *tree)
{
	switch(tree->type)
	{
	case EX_LIT:
		text_complex_lit(tx, tree->data.lit);
		return 1;

	case EX_TEMP_SYM:
	case EX_SYM:
		{
			SYMBOL *sym = tree->data.symbol;
			if((sym->flags & (GLOBAL|DEFINITION)) == GLOBAL)
			{
				text_complex_global(tx, sym->label);
			}
			else
			{
				text_complex_psect(tx, sym->section->sector, sym->value);
			}
		}
		return 1;

	case EX_COM:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		text_complex_com(tx);
		return 1;

	case EX_NEG:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		text_complex_neg(tx);
		return 1;

	case EX_ADD:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		if(!complex_tree(tx, tree->data.child.right))
			return 0;
		text_complex_add(tx);
		return 1;

	case EX_SUB:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		if(!complex_tree(tx, tree->data.child.right))
			return 0;
		text_complex_sub(tx);
		return 1;

	case EX_MUL:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		if(!complex_tree(tx, tree->data.child.right))
			return 0;
		text_complex_mul(tx);
		return 1;

	case EX_DIV:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		if(!complex_tree(tx, tree->data.child.right))
			return 0;
		text_complex_div(tx);
		return 1;

	case EX_AND:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		if(!complex_tree(tx, tree->data.child.right))
			return 0;
		text_complex_and(tx);
		return 1;

	case EX_OR:
		if(!complex_tree(tx, tree->data.child.left))
			return 0;
		if(!complex_tree(tx, tree->data.child.right))
			return 0;
		text_complex_or(tx);
		return 1;

	default:
		return 0;
	}
}

/* store a word which is represented by a complex expression. */

static void store_complex(STREAM *refstr, TEXT_RLD *tr,
						  int size, EX_TREE *value)
{
	TEXT_COMPLEX tx;
	
	change_dot(tr, size);	/* About to store - update DOT */
	
	implicit_gbl(value);		/* Turn undefined symbols into globals */

	text_complex_begin(&tx);	/* Open complex expression */

	if(!complex_tree(&tx, value)) /* Translate */
	{
		report(refstr, "Invalid expression\n");
		store_word(refstr, tr, size, 0);
	}
	else
	{
		list_word(refstr, DOT, 0, size, "C");
		text_complex_commit(tr, &DOT, size, &tx, 0);
	}
}

/* store_complex_displaced is the same as store_complex but uses the
   "displaced" RLD code */

static void store_complex_displaced(STREAM *refstr, TEXT_RLD *tr,
									int size,
									EX_TREE *value)
{
	TEXT_COMPLEX tx;

	change_dot(tr, size);
	
	implicit_gbl(value);		/* Turn undefined symbols into globals */

	text_complex_begin(&tx);

	if(!complex_tree(&tx, value))
	{
		report(refstr, "Invalid expression\n");
		store_word(refstr, tr, size, 0);
	}
	else
	{
		list_word(refstr, DOT, 0, size, "C");
		text_complex_commit_displaced(tr, &DOT, size, &tx, 0);
	}
}

/*
  mode_extension - writes the extension word required by an addressing
  mode */

static void mode_extension(TEXT_RLD *tr, ADDR_MODE *mode,
						   STREAM *str)
{
	EX_TREE *value = mode->offset;
	SYMBOL *sym;
	unsigned offset;

	/* Also frees the mode. */

	if(value == NULL)
	{
		free_addr_mode(mode);
		return;
	}

	if(value->type == EX_LIT)
	{
		if(mode->rel)			/* PC-relative? */
			store_displaced_word(str, tr, 2, value->data.lit);
		else
			store_word(str, tr, 2, value->data.lit);	/* Just a
															   known
															   value. */
	}
	else if(express_sym_offset(value, &sym, &offset))
	{
		if((sym->flags & (GLOBAL|DEFINITION)) == GLOBAL)
		{
			/* Reference to a global symbol. */
			/* Global symbol plus offset */
			if(mode->rel)
				store_global_displaced_offset_word(str, tr,
												   2, offset, sym->label);
			else
				store_global_offset_word(str, tr, 2, offset,
										 sym->label);
		}
		else
		{
			/* Relative to non-external symbol. */
			if(current_pc->section == sym->section)
			{
				/* In the same section */
				if(mode->rel)
				{
					/* I can compute this myself. */
					store_word(str, tr, 2,
						sym->value + offset - DOT - 2);
				}
				else
					store_internal_word(str, tr, 2, sym->value+offset);
			}
			else
			{
				/* In a different section */
				if(mode->rel)
					store_psect_displaced_offset_word(str, tr, 2,
						sym->value+offset, sym->section->label);
				else
					store_psect_offset_word(str, tr, 2,
						sym->value+offset, sym->section->label);
			}
		}
	}
	else
	{
		/* Complex relocation */

		if(mode->rel)
			store_complex_displaced(str, tr, 2, mode->offset);
		else
			store_complex(str, tr, 2, mode->offset);
	}

	free_addr_mode(mode);
}

/* eval_defined - take an EX_TREE and returns TRUE if the tree
   represents "defined" symbols. */

int eval_defined(EX_TREE *value)
{
	switch(value->type)
	{
	case EX_LIT:
		return 1;
	case EX_SYM:
		return 1;
	case EX_UNDEFINED_SYM:
		return 0;
	case EX_AND:
		return eval_defined(value->data.child.left) &&
			eval_defined(value->data.child.right);
	case EX_OR:
		return eval_defined(value->data.child.left) ||
			eval_defined(value->data.child.right);
	default:
		return 0;
	}
}

/* eval_undefined - take an EX_TREE and returns TRUE if it represents
   "undefined" symbols. */

int eval_undefined(EX_TREE *value)
{
	switch(value->type)
	{
	case EX_UNDEFINED_SYM:
		return 1;
	case EX_SYM:
		return 0;
	case EX_AND:
		return eval_undefined(value->data.child.left) &&
			eval_undefined(value->data.child.right);
	case EX_OR:
		return eval_undefined(value->data.child.left) ||
			eval_undefined(value->data.child.right);
	default:
		return 0;
	}
}

/* push_cond - a new conditional (.IF) block has been activated.  Push
   it's context. */

void push_cond(int ok, STREAM *str)
{
	last_cond++;
	assert(last_cond < MAX_CONDS);
	conds[last_cond].ok = ok;
	conds[last_cond].file = memcheck(strdup(str->name));
	conds[last_cond].line = str->line;
}

/*
  pop_cond - pop stacked conditionals. */

void pop_cond(int to)
{
	while(last_cond > to)
	{
		free(conds[last_cond].file);
		last_cond--;
	}
}

/* Parses a string from the input stream. */
/* If not bracketed by <...> or ^/.../, then */
/* the string is delimited by trailing comma or whitespace. */
/* Allows nested <>'s */

char *getstring(char *cp, char **endp)
{
	int len;
	int start;
	char *str;

	if(!brackrange(cp, &start, &len, endp))
	{
		start = 0;
		len = strcspn(cp, " \t\n,;");
		if(endp)
			*endp = cp + len;
	}

	str = memcheck(malloc(len + 1));
	memcpy(str, cp + start, len);
	str[len] = 0;

	return str;
}

/* Get what would be the operation code from the line.  */
/* Used to find the ends of streams without evaluating them, like
   finding the closing .ENDM on a macro definition */

SYMBOL *get_op(char *cp, char **endp)
{
	int local;
	char *label;
	SYMBOL *op;

	cp = skipwhite(cp);
	if(EOL(*cp))
		return NULL;

	label = get_symbol(cp, &cp, &local);
	if(label == NULL)
		return NULL;			/* No operation code. */

	cp = skipwhite(cp);
	if(*cp == ':')				/* A label definition? */
	{
		cp++;
		if(*cp == ':')
			cp++;				/* Skip it */
		free(label);
		label = get_symbol(cp, &cp, NULL);
		if(label == NULL)
			return NULL;
	}

	op = lookup_sym(label, &system_st);
	free(label);

	if(endp)
		*endp = cp;

	return op;
}

/* Here's where I pretend I'm a C++ compiler.  :-/ */

/* *** derive a MACRO_STREAM from a BUFFER_STREAM with a few other args */

typedef struct macro_stream
{
	BUFFER_STREAM bstr;		/* Base class: buffer stream */
	int nargs;				/* Add number-of-macro-arguments */
	int cond;				/* Add saved conditional stack */
} MACRO_STREAM;

/* macro_stream_delete is called when a macro expansion is
   exhausted.  The unique behavior is to unwind any stacked
   conditionals.  This allows a nested .MEXIT to work.  */

void macro_stream_delete(STREAM *str)
{
	MACRO_STREAM *mstr = (MACRO_STREAM *)str;
	pop_cond(mstr->cond);
	buffer_stream_delete(str);
}

STREAM_VTBL macro_stream_vtbl = { macro_stream_delete,
								  buffer_stream_gets,
								  buffer_stream_rewind };

STREAM *new_macro_stream(STREAM *refstr, BUFFER *buf, MACRO *mac,
						 ARG *args)
{
	MACRO_STREAM *mstr = memcheck(malloc(sizeof(MACRO_STREAM)));

	{
		char *name = memcheck(malloc(strlen(refstr->name) + 32));
		sprintf(name, "%s:%d->%s", refstr->name, refstr->line,
				mac->sym.label);
		buffer_stream_construct(&mstr->bstr, buf, name);
		free(name);
	}

	mstr->bstr.stream.vtbl = &macro_stream_vtbl;
	/* Count the args and save their number */
	for(mstr->nargs = 0; args; args = args->next, mstr->nargs++)
		;
	mstr->cond = last_cond;
	return &mstr->bstr.stream;
}

/* read_body fetches the body of .MACRO, .REPT, .IRP, or .IRPC into a
   BUFFER. */

void read_body(STACK *stack, BUFFER *gb, char *name,
			   int called)
{
	int nest;

	/* Read the stream in until the end marker is hit */

	/* Note: "called" says that this body is being pulled from a macro
	   library, and so under no circumstance should it be listed. */

	nest = 1;
	for(;;)
	{
		SYMBOL *op;
		char *nextline;
		char *cp;

		nextline = stack_gets(stack);		/* Now read the line */
		if(nextline == NULL)				/* End of file. */
		{
			report(stack->top, "Macro body not closed\n");
			break;
		}

		if(!called && (list_level - 1 + list_md) > 0)
		{
			list_flush();
			list_source(stack->top, nextline);
		}

		op = get_op(nextline, &cp);

		if(op == NULL)						/* Not a pseudo-op */
		{
			buffer_append_line(gb, nextline);
			continue;
		}
		if(op->section->type == PSEUDO)
		{
			if(op->value == P_MACRO ||
				op->value == P_REPT ||
				op->value == P_IRP ||
				op->value == P_IRPC)
				nest++;

			if(op->value == P_ENDM ||
				op->value == P_ENDR)
			{
				nest--;
				/* If there's a name on the .ENDM, then */
				/* close the body early if it matches the definition */
				if(name && op->value == P_ENDM)
				{
					cp = skipwhite(cp);
					if(!EOL(*cp))
					{
						char *label = get_symbol(cp, &cp, NULL);
						if(label)
						{
							if(strcmp(label, name) == 0)
								nest = 0;		/* End of macro body. */
							free(label);
						}
					}
				}
			}

			if(nest == 0)
				return;				/* All done. */
		}

		buffer_append_line(gb, nextline);
	}
}

/* Diagnostic: dumpmacro dumps a macro definition to stdout.
   I used this for debugging; it's not called at all right now, but
   I hate to delete good code. */

void dumpmacro(MACRO *mac, FILE *fp)
{
	ARG *arg;

	fprintf(fp, ".MACRO %s ", mac->sym.label);

	for(arg = mac->args; arg != NULL; arg = arg->next)
	{
		fputs(arg->label, fp);
		if(arg->value)
		{
			fputc('=', fp);
			fputs(arg->value, fp);
		}
		fputc(' ', fp);
	}
	fputc('\n', fp);

	fputs(mac->text->buffer, fp);

	fputs(".ENDM\n", fp);
}

/* defmacro - define a macro. */
/* Also used by .MCALL to pull macro definitions from macro libraries */

MACRO *defmacro(char *cp, STACK *stack, int called)
{
	MACRO *mac;
	ARG *arg, **argtail;
	char *label;

	cp = skipwhite(cp);
	label = get_symbol(cp, &cp, NULL);
	if(label == NULL)
	{
		report(stack->top, "Invalid macro definition\n");
		return NULL;
	}

	/* Allow redefinition of a macro; new definition replaces the old. */
	mac = (MACRO *)lookup_sym(label, &macro_st);
	if(mac)
	{
		/* Remove from the symbol table... */
		remove_sym(&mac->sym, &macro_st);
		free_macro(mac);
	}

	mac = new_macro(label);

	add_table(&mac->sym, &macro_st);

	argtail = &mac->args;
	cp = skipdelim(cp);

	while(!EOL(*cp))
	{
		arg = new_arg();
		if(arg->locsym = (*cp == '?')) /* special argument flag? */
			cp++;
		arg->label = get_symbol(cp, &cp, NULL);
		if(arg->label == NULL)
		{
			/* It turns out that I have code which is badly formatted
			   but which MACRO.SAV assembles.  Sigh.  */
			/* So, just quit defining arguments. */
			break;
#if 0
			report(str, "Illegal macro argument\n");
			remove_sym(&mac->sym, &macro_st);
			free_macro(mac);
			return NULL;
#endif
		}

		cp = skipwhite(cp);
		if(*cp == '=')
		{
			/* Default substitution given */
			arg->value = getstring(cp+1, &cp);
			if(arg->value == NULL)
			{
				report(stack->top, "Illegal macro argument\n");
				remove_sym(&mac->sym, &macro_st);
				free_macro(mac);
				return NULL;
			}
		}

		/* Append to list of arguments */
		arg->next = NULL;
		*argtail = arg;
		argtail = &arg->next;

		cp = skipdelim(cp);
	}

	/* Read the stream in until the end marker is hit */
	{
		BUFFER *gb;
		int levelmod = 0;

		gb = new_buffer();

		if(!called && !list_md)
		{
			list_level--;
			levelmod = 1;
		}

		read_body(stack, gb, mac->sym.label, called);

		list_level += levelmod;

		if(mac->text != NULL)					/* Discard old macro body */
			buffer_free(mac->text);

		mac->text = gb;
	}

	return mac;
}

/* find_arg - looks for an arg with the given name in the given
   argument list */

static ARG *find_arg(ARG *arg, char *name)
{
	for(; arg != NULL; arg = arg->next)
	{
		if(strcmp(arg->label, name) == 0)
			return arg;
	}

	return NULL;
}

/* subst_args - given a BUFFER and a list of args, generate a new
   BUFFER with argument replacement having taken place. */

BUFFER *subst_args(BUFFER *text, ARG *args)
{
	char *in;
	char *begin;
	BUFFER *gb;
	char *label;
	ARG *arg;

	gb = new_buffer();

	/* Blindly look for argument symbols in the input. */
	/* Don't worry about quotes or comments. */

	for(begin = in = text->buffer; in < text->buffer + text->length;)
	{
		char *next;

		if(issym(*in))
		{
			label = get_symbol(in, &next, NULL);
			if(label)
			{
				if(arg = find_arg(args, label))
				{
					/* An apostrophy may appear before or after the symbol. */
					/* In either case, remove it from the expansion. */

					if(in > begin && in[-1] == '\'')
						in --;			/* Don't copy it. */
					if(*next == '\'')
						next++;

					/* Copy prior characters */
					buffer_appendn(gb, begin, in-begin);
					/* Copy replacement string */
					buffer_append_line(gb, arg->value);
					in = begin = next;
					--in; /* prepare for subsequent increment */
				}
				free(label);
				in = next;
			}
			else
				in++;
		}
		else
			in++;
	}

	/* Append the rest of the text */
	buffer_appendn(gb, begin, in - begin);

	return gb;			/* Done. */
}

/* eval_arg - the language allows an argument expression to be given
   as "\expression" which means, evaluate the expression and
   substitute the numeric value in the current radix. */

void eval_arg(STREAM *refstr, ARG *arg)
{
	/* Check for value substitution */

	if(arg->value[0] == '\\')
	{
		EX_TREE *value = parse_expr(arg->value+1, 0);
		unsigned word = 0;
		char temp[10];
		if(value->type != EX_LIT)
		{
			report(refstr, "Constant value required\n");
		}
		else
			word = value->data.lit;

		free_tree(value);

		/* printf can't do base 2. */
		my_ultoa(word & 0177777, temp, radix);
		free(arg->value);
		arg->value = memcheck(strdup(temp));
	}

}

/* expandmacro - return a STREAM containing the expansion of a macro */

STREAM *expandmacro(STREAM *refstr, MACRO *mac, char *cp)
{
	ARG *arg, *args, *macarg;
	char *label;
	STREAM *str;
	BUFFER *buf;

	args = NULL;
	arg = NULL;

	/* Parse the arguments */

	while(!EOL(*cp))
	{
		char *nextcp;
		/* Check for named argument */
		label = get_symbol(cp, &nextcp, NULL);
		if(label &&
			(nextcp = skipwhite(nextcp), *nextcp == '=') &&
			(macarg = find_arg(mac->args, label)))
		{
			/* Check if I've already got a value for it */
			if(find_arg(args, label) != NULL)
			{
				report(refstr, "Duplicate submission of keyword "
					   "argument %s\n", label);
				free(label);
				free_args(args);
				return NULL;
			}

			arg = new_arg();
			arg->label = label;
			nextcp = skipwhite(nextcp+1);
			arg->value = getstring(nextcp, &nextcp);
		}
		else
		{
			if(label)
				free(label);

			/* Find correct positional argument */

			for(macarg = mac->args; macarg != NULL; macarg = macarg->next)
			{
				if(find_arg(args, macarg->label) == NULL)
					break;			/* This is the next positional arg */
			}

			if(macarg == NULL)
				break;				/* Don't pick up any more arguments. */

			arg = new_arg();
			arg->label = memcheck(strdup(macarg->label));	/* Copy the name */
			arg->value = getstring(cp, &nextcp);
		}

		arg->next = args;
		args = arg;

		eval_arg(refstr, arg);			/* Check for expression evaluation */

		cp = skipdelim(nextcp);
	}

	/* Now go back and fill in defaults */

	{
		int locsym;
		if(last_lsb != lsb)
			locsym = last_locsym = 32768;
		else
			locsym = last_locsym;
		last_lsb = lsb;

		for(macarg = mac->args; macarg != NULL; macarg = macarg->next)
		{
			arg = find_arg(args, macarg->label);
			if(arg == NULL)
			{
				arg = new_arg();
				arg->label = memcheck(strdup(macarg->label));
				if(macarg->locsym)
				{
					char temp[32];
					/* Here's where we generate local labels */
					sprintf(temp, "%d$", locsym++);
					arg->value = memcheck(strdup(temp));
				}
				else if(macarg->value)
				{
					arg->value = memcheck(strdup(macarg->value));
				}
				else
					arg->value = memcheck(strdup(""));

				arg->next = args;
				args = arg;
			}
		}

		last_locsym = locsym;
	}

	buf = subst_args(mac->text, args);

	str = new_macro_stream(refstr, buf, mac, args);

	free_args(args);
	buffer_free(buf);

	return str;
}

/* *** implement REPT_STREAM */

typedef struct rept_stream
{
	BUFFER_STREAM bstr;
	int count;					/* The current repeat countdown */
	int savecond;				/* conditional stack level at time of
								   expansion */
} REPT_STREAM;

/* rept_stream_gets gets a line from a repeat stream.  At the end of
   each count, the coutdown is decreated and the stream is reset to
   it's beginning. */

char *rept_stream_gets(STREAM *str)
{
	REPT_STREAM *rstr = (REPT_STREAM *)str;
	char *cp;

	for(;;)
	{
		if((cp = buffer_stream_gets(str)) != NULL)
			return cp;

		if(--rstr->count <= 0)
			return NULL;

		buffer_stream_rewind(str);
	}
}

/* rept_stream_delete unwinds nested conditionals like .MEXIT does. */

void rept_stream_delete(STREAM *str)
{
	REPT_STREAM *rstr = (REPT_STREAM *)str;
	pop_cond(rstr->savecond);			/* complete unterminated
										   conditionals */
	buffer_stream_delete(&rstr->bstr.stream);
}

/* The VTBL */

STREAM_VTBL rept_stream_vtbl = { rept_stream_delete,
								 rept_stream_gets,
								 buffer_stream_rewind };

/* expand_rept is called when a .REPT is encountered in the input. */

STREAM *expand_rept(STACK *stack, char *cp)
{
	EX_TREE *value;
	BUFFER *gb;
	REPT_STREAM *rstr;
	int levelmod;

	value = parse_expr(cp, 0);
	if(value->type != EX_LIT)
	{
		report(stack->top, ".REPT value must be constant\n");
		free_tree(value);
		return NULL;
	}

	gb = new_buffer();

	levelmod = 0;
	if(!list_md)
	{
		list_level--;
		levelmod = 1;
	}

	read_body(stack, gb, NULL, FALSE);

	list_level += levelmod;

	rstr = memcheck(malloc(sizeof(REPT_STREAM)));
	{
		char *name = memcheck(malloc(strlen(stack->top->name) + 32));
		sprintf(name, "%s:%d->.REPT", stack->top->name, stack->top->line);
		buffer_stream_construct(&rstr->bstr, gb, name);
		free(name);
	}

	rstr->count = value->data.lit;
	rstr->bstr.stream.vtbl = &rept_stream_vtbl;
	rstr->savecond = last_cond;

	buffer_free(gb);
	free_tree(value);

	return &rstr->bstr.stream;
}

/* *** implement IRP_STREAM */

typedef struct irp_stream
{
	BUFFER_STREAM bstr;
	char *label;			/* The substitution label */
	char *items;			/* The substitution items (in source code
							   format) */
	int offset;				/* Current offset into "items" */
	BUFFER *body;			/* Original body */
	int savecond;			/* Saved conditional level */
} IRP_STREAM;

/* irp_stream_gets expands the IRP as the stream is read. */
/* Each time an iteration is exhausted, the next iteration is
   generated. */

char *irp_stream_gets(STREAM *str)
{
	IRP_STREAM *istr = (IRP_STREAM *)str;
	char *cp;
	BUFFER *buf;
	ARG *arg;

	for(;;)
	{
		if((cp = buffer_stream_gets(str)) != NULL)
			return cp;

		cp = istr->items + istr->offset;

		if(!*cp)
			return NULL;		/* No more items.  EOF. */

		arg = new_arg();
		arg->next = NULL;
		arg->locsym = 0;
		arg->label = istr->label;
		arg->value = getstring(cp, &cp);
		cp = skipdelim(cp);
		istr->offset = cp - istr->items;

		eval_arg(str, arg);
		buf = subst_args(istr->body, arg);

		free(arg->value);
		free(arg);
		buffer_stream_set_buffer(&istr->bstr, buf);
		buffer_free(buf);
	}
}

/* irp_stream_delete - also pops the conditional stack */

void irp_stream_delete(STREAM *str)
{
	IRP_STREAM *istr = (IRP_STREAM *)str;

	pop_cond(istr->savecond);			/* complete unterminated
										   conditionals */

	buffer_free(istr->body);
	free(istr->items);
	free(istr->label);
	buffer_stream_delete(str);
}

STREAM_VTBL irp_stream_vtbl = { irp_stream_delete, irp_stream_gets,
								buffer_stream_rewind };

/* expand_irp is called when a .IRP is encountered in the input. */

STREAM *expand_irp(STACK *stack, char *cp)
{
	char *label, *items;
	BUFFER *gb;
	int levelmod = 0;
	IRP_STREAM *str;

	label = get_symbol(cp, &cp, NULL);
	if(!label)
	{
		report(stack->top, "Illegal .IRP syntax\n");
		return NULL;
	}

	cp = skipdelim(cp);

	items = getstring(cp, &cp);
	if(!items)
	{
		report(stack->top, "Illegal .IRP syntax\n");
		free(label);
		return NULL;
	}

	gb = new_buffer();

	levelmod = 0;
	if(!list_md)
	{
		list_level--;
		levelmod++;
	}

	read_body(stack, gb, NULL, FALSE);

	list_level += levelmod;

	str = memcheck(malloc(sizeof(IRP_STREAM)));
	{
		char *name = memcheck(malloc(strlen(stack->top->name) + 32));
		sprintf(name, "%s:%d->.IRP", stack->top->name, stack->top->line);
		buffer_stream_construct(&str->bstr, NULL, name);
		free(name);
	}

	str->bstr.stream.vtbl = &irp_stream_vtbl;

	str->body = gb;
	str->items = items;
	str->offset = 0;
	str->label = label;
	str->savecond = last_cond;

	return &str->bstr.stream;
}

/* *** implement IRPC_STREAM */

typedef struct irpc_stream
{
	BUFFER_STREAM bstr;
	char *label;			/* The substitution label */
	char *items;			/* The substitution items (in source code
							   format) */
	int offset;				/* Current offset in "items" */
	BUFFER *body;			/* Original body */
	int savecond;			/* conditional stack at invocation */
} IRPC_STREAM;

/* irpc_stream_gets - same comments apply as with irp_stream_gets, but
   the substitution is character-by-character */

char *irpc_stream_gets(STREAM *str)
{
	IRPC_STREAM *istr = (IRPC_STREAM *)str;
	char *cp;
	BUFFER *buf;
	ARG *arg;

	for(;;)
	{
		if((cp = buffer_stream_gets(str)) != NULL)
			return cp;

		cp = istr->items + istr->offset;

		if(!*cp)
			return NULL;		/* No more items.  EOF. */

		arg = new_arg();
		arg->next = NULL;
		arg->locsym = 0;
		arg->label = istr->label;
		arg->value = memcheck(malloc(2));
		arg->value[0] = *cp++;
		arg->value[1] = 0;
		istr->offset = cp - istr->items;

		buf = subst_args(istr->body, arg);

		free(arg->value);
		free(arg);
		buffer_stream_set_buffer(&istr->bstr, buf);
		buffer_free(buf);
	}
}

/* irpc_stream_delete - also pops contidionals */

void irpc_stream_delete(STREAM *str)
{
	IRPC_STREAM *istr = (IRPC_STREAM *)str;
	pop_cond(istr->savecond);			/* complete unterminated
										   conditionals */
	buffer_free(istr->body);
	free(istr->items);
	free(istr->label);
	buffer_stream_delete(str);
}

STREAM_VTBL irpc_stream_vtbl = { irpc_stream_delete,
								 irpc_stream_gets,
								 buffer_stream_rewind };

/* expand_irpc - called when .IRPC is encountered in the input */

STREAM *expand_irpc(STACK *stack, char *cp)
{
	char *label, *items;
	BUFFER *gb;
	int levelmod = 0;
	IRPC_STREAM *str;

	label = get_symbol(cp, &cp, NULL);
	if(!label)
	{
		report(stack->top, "Illegal .IRPC syntax\n");
		return NULL;
	}

	cp = skipdelim(cp);

	items = getstring(cp, &cp);
	if(!items)
	{
		report(stack->top, "Illegal .IRPC syntax\n");
		free(label);
		return NULL;
	}

	gb = new_buffer();

	levelmod = 0;
	if(!list_md)
	{
		list_level--;
		levelmod++;
	}

	read_body(stack, gb, NULL, FALSE);

	list_level += levelmod;

	str = memcheck(malloc(sizeof(IRPC_STREAM)));
	{
		char *name = memcheck(malloc(strlen(stack->top->name) + 32));
		sprintf(name, "%s:%d->.IRPC", stack->top->name, stack->top->line);
		buffer_stream_construct(&str->bstr, NULL, name);
		free(name);
	}

	str->bstr.stream.vtbl = &irpc_stream_vtbl;
	str->body = gb;
	str->items = items;
	str->offset = 0;
	str->label = label;
	str->savecond = last_cond;

	return &str->bstr.stream;
}

/* go_section - sets current_pc to a new program section */

void go_section(TEXT_RLD *tr, SECTION *sect)
{
	if(current_pc->section == sect)
		return;						/* This is too easy */

	/* save current PC value for old section */
	current_pc->section->pc = DOT;

	/* Set current section and PC value */
	current_pc->section = sect;
	DOT = sect->pc;
}

/*
  store_value - used to store a value represented by an expression
  tree into the object file.  Used by do_word and .ASCII/.ASCIZ.
*/

static void store_value(STACK *stack, TEXT_RLD *tr,
						int size, EX_TREE *value)
{
	SYMBOL *sym;
	unsigned offset;

	implicit_gbl(value);	/* turn undefined symbols into globals */

	if(value->type == EX_LIT)
	{
		store_word(stack->top, tr, size, value->data.lit);
	}
	else if(!express_sym_offset(value, &sym, &offset))
	{
		store_complex(stack->top, tr, size, value);
	}
	else
	{
		if((sym->flags & (GLOBAL|DEFINITION)) == GLOBAL)
		{
			store_global_offset_word(stack->top, tr, size,
									 sym->value+offset,
									 sym->label);
		}
		else if(sym->section != current_pc->section)
		{
			store_psect_offset_word(stack->top, tr, size,
									sym->value+offset,
									sym->section->label);
		}
		else
		{
			store_internal_word(stack->top, tr, size,
								sym->value+offset);
		}
	}
}

/* do_word - used by .WORD, .BYTE, and implied .WORD. */

static int do_word(STACK *stack, TEXT_RLD *tr, char *cp, int size)
{

	if(size == 2 && (DOT & 1))
	{
		report(stack->top, ".WORD on odd boundary\n");
		store_word(stack->top, tr, 1, 0);	/* Align it */
	}

	do
	{
		EX_TREE *value = parse_expr(cp, 0);

		store_value(stack, tr, size, value);

		cp = skipdelim(value->cp);

		free_tree(value);

	} while(cp = skipdelim(cp), !EOL(*cp));

	return 1;
}

/*
  check_branch - check branch distance.
*/

static int check_branch(STACK *stack, unsigned offset, int min, int max)
{
	int s_offset;
	/* Sign-extend */
	if(offset & 0100000)
		s_offset = offset | ~0177777;
	else
		s_offset = offset & 077777;
	if(s_offset > max || s_offset < min)
	{
		char temp[16];
		/* printf can't do signed octal. */
		my_ltoa(s_offset, temp, 8);
		report(stack->top,
			   "Branch target out of range (distance=%s)\n",
			   temp);
		return 0;
	}
	return 1;
}

/* assemble - read a line from the input stack, assemble it. */

/* This function is way way too large, because I just coded most of
   the operation code and pseudo-op handling right in line.  */

int assemble(STACK *stack, TEXT_RLD *tr)
{
	char *cp;					/* Parse character pointer */
	char *opcp;					/* Points to operation mnemonic text */
	char *ncp;					/* "next" cp */
	char *label;				/* A label */
	char *line;					/* The whole line */
	SYMBOL *op;					/* The operation SYMBOL */
	int local;					/* Whether a label is a local label or
								   not */

	line = stack_gets(stack);
	if(line == NULL)
		return -1;					/* Return code for EOF. */

	cp = line;

	/* Frankly, I don't need to keep "line."  But I found it quite
	   handy during debugging, to see what the whole operation was,
	   when I'm down to parsing the second operand and things aren't
	   going right. */

	stmtno++;						/* Increment statement number */

	list_source(stack->top, line);			/* List source */

	if(suppressed)
	{
		/* Assembly is suppressed by unsatisfoed conditional.  Look
           for ending and enabling statements. */

		op = get_op(cp, &cp);		/* Look at operation code */

		/* FIXME: this code will blindly look into .REM commentary and
		   find operation codes.  Incidentally, so will read_body. */

		if(op == NULL)
			return 1;			/* Not found.  Don't care. */
		if(op->section->type != PSEUDO)
			return 1;			/* Not a pseudo-op. */
		switch(op->value)
		{
		case P_IF:
		case P_IFDF:
			suppressed++;		/* Nested.  Suppressed. */
			break;
		case P_IFTF:
			if(suppressed == 1)	/* Reduce suppression from 1 to 0. */
				suppressed = 0;
			break;
		case P_IFF:
			if(suppressed == 1)	/* Can reduce suppression from 1 to 0. */
			{
				if(!conds[last_cond].ok)
					suppressed = 0;
			}
			break;
		case P_IFT:
			if(suppressed == 1)	/* Can reduce suppression from 1 to 0. */
			{
				if(conds[last_cond].ok)
					suppressed = 0;
			}
			break;
		case P_ENDC:
			suppressed--;		/* Un-nested. */
			if(suppressed == 0)
				pop_cond(last_cond-1);		/* Re-enabled. */
			break;
		}
		return 1;
	}

	/* The line may begin with "label<ws>:[:]" */

	opcp = cp;
	if((label = get_symbol(cp, &ncp, &local)) != NULL)
	{
		int flag = PERMANENT|DEFINITION|local;
		SYMBOL *sym;

		ncp = skipwhite(ncp);
		if(*ncp == ':')			/* Colon, for symbol definition? */
		{
			ncp++;
			/* maybe it's a global definition */
			if(*ncp == ':')
			{
				flag |= GLOBAL;	/* Yes, include global flag */
				ncp++;
			}

			sym = add_sym(label, DOT, flag, current_pc->section, &symbol_st);
			cp = ncp;

			if(sym == NULL)
				report(stack->top, "Illegal symbol definition %s\n", label);

			free(label);

			/* See if local symbol block should be incremented */
			if(!enabl_lsb && !local)
				lsb++;

			cp = skipwhite(ncp);
			opcp = cp;
			label = get_symbol(cp, &ncp, NULL);	/* Now, get what follows */
		}
	}

	/* PSEUDO P_IIF jumps here.  */
reassemble:
	cp = skipwhite(cp);

	if(EOL(*cp))
		return 1;				/* It's commentary.  All done. */

	if(label)					/* Something looks like a label. */
	{
		/* detect assignment */

		ncp = skipwhite(ncp);	/* The pointer to the text that
								   follows the symbol */

		if(*ncp == '=')
		{
			unsigned flags;
			EX_TREE *value;
			SYMBOL *sym;

			cp = ncp;

			/* Symbol assignment. */

			flags = DEFINITION|local;
			cp++;
			if(*cp == '=')
			{
				flags |= GLOBAL;	/* Global definition */
				cp++;
			}
			if(*cp == ':')
			{
				flags |= PERMANENT;
				cp++;
			}

			cp = skipwhite(cp);

			value = parse_expr(cp, 0);

			/* Special code: if the symbol is the program counter,
			   this is harder. */

			if(strcmp(label, ".") == 0)
			{
				if(current_pc->section->flags & PSECT_REL)
				{
					SYMBOL *sym;
					unsigned offset;

					/* Express the given expression as a symbol and an
					   offset. The symbol must not be global, the
					   section must = current. */

					if(!express_sym_offset(value, &sym, &offset))
					{
						report(stack->top, "Illegal ORG\n");
					}
					else if((sym->flags & (GLOBAL|DEFINITION)) == GLOBAL)
					{
						report(stack->top,
							   "Can't ORG to external location\n");
					}
					else if(sym->flags & UNDEFINED)
					{
						report(stack->top, "Can't ORG to undefined sym\n");
					}
					else if(sym->section != current_pc->section)
					{
						report(stack->top,
							   "Can't ORG to alternate section "
							   "(use PSECT)\n");
					}
					else
					{
						DOT = sym->value + offset;
						list_value(stack->top, DOT);
						change_dot(tr, 0);
					}
				}
				else
				{
					/* If the current section is absolute, the value
					   must be a literal */
					if(value->type != EX_LIT)
					{
						report(stack->top,
							   "Can't ORG to non-absolute location\n");
						free_tree(value);
						free(label);
						return 0;
					}
					DOT = value->data.lit;
					list_value(stack->top, DOT);
					change_dot(tr, 0);
				}
				free_tree(value);
				free(label);
				return 1;
			}

			/* regular symbols */
			if(value->type == EX_LIT)
			{
				sym = add_sym(label, value->data.lit,
					flags, &absolute_section, &symbol_st);
			}
			else if(value->type == EX_SYM ||
				value->type == EX_TEMP_SYM)
			{
				sym = add_sym(label, value->data.symbol->value,
					flags, value->data.symbol->section, &symbol_st);
			}
			else
			{
				report(stack->top, "Complex expression cannot be assigned "
					"to a symbol\n");

				if(!pass)
				{
					/* This may work better in pass 2 - something in
					   RT-11 monitor needs the symbol to apear to be
					   defined even if I can't resolve it's value. */
					sym = add_sym(label, 0, UNDEFINED,
								  &absolute_section, &symbol_st);
				}
				else
					sym = NULL;
			}

			if(sym != NULL)
				list_value(stack->top, sym->value);

			free_tree(value);
			free(label);

			return sym != NULL;
		}

		/* Try to resolve macro */

		op = lookup_sym(label, &macro_st);
		if(op &&
			op->stmtno < stmtno)
		{
			STREAM *macstr;

			free(label);

			macstr = expandmacro(stack->top, (MACRO *)op, ncp);

			stack_push(stack, macstr);		/* Push macro expansion
											   onto input stream */

			return 1;
		}

		/* Try to resolve instruction or pseudo */
		op = lookup_sym(label, &system_st);
		if(op)
		{
			cp = ncp;

			free(label);		/* Don't need this hanging around anymore */

			switch(op->section->type)
			{
			case PSEUDO:
				switch(op->value)
				{
				case P_ENDR:
				case P_ENDM:
				case P_SBTTL:
				case P_LIST:
				case P_NLIST:
				case P_PRINT:
					return 1;		/* Accepted, ignored.  (An obvious
									 need: get assembly listing
									 controls working. ) */

				case P_IDENT:
					{
						char endc[6];
						int len;

						cp = skipwhite(cp);
						endc[0] = *cp++;
						endc[1] = '\n';
						endc[2] = 0;
						len = strcspn(cp, endc);
						if(len > 6)
							len = 6;

						if(ident) /* An existing ident? */
							free(ident); /* Discard it. */

						ident = memcheck(malloc(len + 1));
						memcpy(ident, cp, len);
						ident[len] = 0;
						upcase(ident);

						return 1;
					}

				case P_RADIX:
					{
						int old_radix = radix;
						radix = strtoul(cp, &cp, 10);
						if(radix != 8 && radix != 10 && radix != 16 &&
							radix != 2)
						{
							radix = old_radix;
							report(stack->top, "Illegal radix\n");
							return 0;
						}
						return 1;
					}

				case P_FLT4:
				case P_FLT2:
					{
						int ok = 1;

						while(!EOL(*cp))
						{
							unsigned flt[4];
							if(parse_float(cp, &cp,
										   (op->value == P_FLT4 ? 4 : 2),
										   flt))
							{
								/* Store the word values */
								store_word(stack->top, tr, 2, flt[0]);
								store_word(stack->top, tr, 2, flt[1]);
								if(op->value == P_FLT4)
								{
									store_word(stack->top, tr,
											   2, flt[2]);
									store_word(stack->top, tr,
											   2, flt[3]);
								}
							}
							else
							{
								report(stack->top,
									   "Bad floating point format\n");
								ok = 0;
							}
							cp = skipdelim(cp);
						}
						return ok;
					}

				case P_ERROR:
					report(stack->top, "%.*s\n", strcspn(cp, "\n"), cp);
					return 0;

				case P_SAVE:
					sect_sp++;
					sect_stack[sect_sp] = current_pc->section;
					return 1;

				case P_RESTORE:
					if(sect_sp < 0)
					{
						report(stack->top, "No saved section for .RESTORE\n");
						return 0;
					}
					else
					{
						go_section(tr, sect_stack[sect_sp]);
						sect_sp++;
					}
					return 1;

				case P_NARG:
					{
						STREAM *str;
						MACRO_STREAM *mstr;
						int local;

						label = get_symbol(cp, &cp, &local);

						if(label == NULL)
						{
							report(stack->top, "Bad .NARG syntax\n");
							return 0;
						}

						/* Walk up the stream stack to find the
						   topmost macro stream */
						for(str = stack->top;
							str != NULL &&
								str->vtbl != &macro_stream_vtbl;
							str = str->next)
							;

						if(!str)
						{
							report(str, ".NARG not within macro expansion\n");
							free(label);
							return 0;
						}

						mstr = (MACRO_STREAM *)str;

						add_sym(label, mstr->nargs, DEFINITION|local,
								&absolute_section, &symbol_st);
						free(label);
						return 1;
					}

				case P_NCHR:
					{
						char *string;
						int local;
						label = get_symbol(cp, &cp, &local);

						if(label == NULL)
						{
							report(stack->top, "Bad .NCHR syntax\n");
							return 0;
						}

						cp = skipdelim(cp);

						string = getstring(cp, &cp);

						add_sym(label, strlen(string),
								DEFINITION|local,
								&absolute_section, &symbol_st);
						free(label);
						free(string);
						return 1;
					}

				case P_NTYPE:
					{
						ADDR_MODE mode;
						int local;

						label = get_symbol(cp, &cp, &local);
						if(label == NULL)
						{
							report(stack->top, "Bad .NTYPE syntax\n");
							return 0;
						}

						cp = skipdelim(cp);

						if(!get_mode(cp, &cp, &mode))
						{
							report(stack->top,
								   "Bad .NTYPE addressing mode\n");
							free(label);
							return 0;
						}

						add_sym(label, mode.type, DEFINITION|local,
								&absolute_section, &symbol_st);
						free_addr_mode(&mode);
						free(label);

						return 1;
					}

				case P_INCLU:
					{
						char *name = getstring(cp, &cp);
						STREAM *incl;

						if(name == NULL)
						{
							report(stack->top, "Bad .INCLUDE file name\n");
							return 0;
						}

						incl = new_file_stream(name);
						if(incl == NULL)
						{
							report(stack->top,
								   "Unable to open .INCLUDE file %s\n", name);
							free(name);
							return 0;
						}

						free(name);

						stack_push(stack, incl);

						return 1;
					}

				case P_REM:
					{
						char quote[4];
						/* Read and discard lines until one with a
						   closing quote */

						cp = skipwhite(cp);
						quote[0] = *cp++;
						quote[1] = '\n';
						quote[2] = 0;

						for(;;)
						{
							cp += strcspn(cp, quote);
							if(*cp == quote[0])
								break;		/* Found closing quote */
							cp = stack_gets(stack);	/* Read next input line */
							if(cp == NULL)
								break;			/* EOF */
						}
					}
					return 1;

				case P_IRP:
					{
						STREAM *str = expand_irp(stack, cp);
						if(str)
							stack_push(stack, str);
						return str != NULL;
					}

				case P_IRPC:
					{
						STREAM *str = expand_irpc(stack, cp);
						if(str)
							stack_push(stack, str);
						return str != NULL;
					}

				case P_MCALL:
					{
						STREAM *macstr;
						BUFFER *macbuf;
						char *maccp;
						int saveline;
						MACRO *mac;
						int i;
						char macfile[FILENAME_MAX];
						char hitfile[FILENAME_MAX];

						for(;;)
						{
							cp = skipdelim(cp);

							if(EOL(*cp))
								return 1;

							label = get_symbol(cp, &cp, NULL);
							if(!label)
							{
								report(stack->top, "Illegal .MCALL format\n");
								return 0;
							}

							/* See if that macro's already defined */
							if(lookup_sym(label, &macro_st))
							{
								free(label);		/* Macro already
													   registered.  No
													   prob. */
								cp = skipdelim(cp);
								continue;
							}

							/* Find the macro in the list of included
							   macro libraries */
							macbuf = NULL;
							for(i = 0; i < nr_mlbs; i++)
							{
								if((macbuf = mlb_entry(mlbs[i],
													   label)) != NULL)
									break;
							}
							if(macbuf != NULL)
							{
								macstr = new_buffer_stream(macbuf, label);
								buffer_free(macbuf);
							}
							else
							{
								strncpy(macfile, label, sizeof(macfile));
								strncat(macfile, ".MAC", sizeof(macfile) - strlen(macfile));
								my_searchenv(macfile, "MCALL", hitfile, sizeof(hitfile));
								if(hitfile[0])
									macstr = new_file_stream(hitfile);
							}

							if(macstr != NULL)
							{
								for(;;)
								{
									char *mlabel;
									maccp = macstr->vtbl->gets(macstr);
									if(maccp == NULL)
										break;
									mlabel = get_symbol(maccp, &maccp, NULL);
									if(mlabel == NULL)
										continue;
									op = lookup_sym(mlabel, &system_st);
									free(mlabel);
									if(op == NULL)
										continue;
									if(op->value == P_MACRO)
										break;
								}

								if(maccp != NULL)
								{
									STACK macstack = { macstr };
									int savelist = list_level;
									saveline = stmtno;
									list_level = -1;
									mac = defmacro(maccp, &macstack, TRUE);
									if(mac == NULL)
									{
										report(stack->top,
											   "Failed to define macro "
											   "called %s\n",
											label);
									}

									stmtno = saveline;
									list_level = savelist;
								}

								macstr->vtbl->delete(macstr);
							}
							else
								report(stack->top,
									   "MACRO %s not found\n", label);

							free(label);
						}
					}
					return 1;

				case P_MACRO:
					{
						MACRO *mac = defmacro(cp, stack, FALSE);
						return mac != NULL;
					}

				case P_MEXIT:
					{
						STREAM *macstr;

						/* Pop a stream from the input. */
						/* It must be the first stream, and it must be */
						/* a macro, rept, irp, or irpc. */
						macstr = stack->top;
						if(macstr->vtbl != &macro_stream_vtbl &&
							macstr->vtbl != &rept_stream_vtbl &&
							macstr->vtbl != &irp_stream_vtbl &&
							macstr->vtbl != &irpc_stream_vtbl)
						{
							report(stack->top, ".MEXIT not within a macro\n");
							return 0;
						}

						/* and finally, pop the macro */
						stack_pop(stack);

						return 1;
					}

				case P_REPT:
					{
						STREAM *reptstr = expand_rept(stack, cp);
						if(reptstr)
							stack_push(stack, reptstr);
						return reptstr != NULL;
					}

				case P_ENABL:

					/* FIXME - add all the rest of the options. */
					while(!EOL(*cp))
					{
						label = get_symbol(cp, &cp, NULL);
						if(strcmp(label, "AMA") == 0)
							enabl_ama = 1;
						else if(strcmp(label, "LSB") == 0)
						{
							enabl_lsb = 1;
							lsb++;
						}
						else if(strcmp(label, "GBL") == 0)
							enabl_gbl = 1;
						free(label);
						cp = skipdelim(cp);
					}
					return 1;

				case P_DSABL:

					/* FIXME Ditto as for .ENABL */
					while(!EOL(*cp))
					{
						label = get_symbol(cp, &cp, NULL);
						if(strcmp(label, "AMA") == 0)
							enabl_ama = 0;
						else if(strcmp(label, "LSB") == 0)
							enabl_lsb = 0;
						else if(strcmp(label, "GBL") == 0)
							enabl_gbl = 0;
						free(label);
						cp = skipdelim(cp);
					}
					return 1;

				case P_LIMIT:
					store_limits(stack->top, tr);
					return 1;

				case P_TITLE:
					/* accquire module name */
					if(module_name != NULL)
					{
						free(module_name);
					}
					module_name = get_symbol(cp, &cp, NULL);
					return 1;

				case P_END:
					/* Accquire transfer address */
					cp = skipwhite(cp);
					if(!EOL(*cp))
					{
						if(xfer_address)
							free_tree(xfer_address);
						xfer_address = parse_expr(cp, 0);
					}
					return 1;

				case P_IFDF:
					opcp = skipwhite(opcp);
					cp = opcp + 3;			/* Point cp at the "DF" or
											   "NDF" part */
					/* Falls into... */
				case P_IIF:
				case P_IF:
					{
						EX_TREE *value;
						int ok;

						label = get_symbol(cp, &cp, NULL); /* Get condition */
						cp = skipdelim(cp);

						if(strcmp(label, "DF") == 0)
						{
							value = parse_expr(cp, 1);
							cp = value->cp;
							ok = eval_defined(value);
							free_tree(value);
						}
						else if(strcmp(label, "NDF") == 0)
						{
							value = parse_expr(cp, 1);
							cp = value->cp;
							ok = eval_undefined(value);
							free_tree(value);
						}
						else if(strcmp(label, "B") == 0)
						{
							char *thing;
							cp = skipwhite(cp);
							if(!EOL(*cp))
								thing = getstring(cp, &cp);
							else
								thing = memcheck(strdup(""));
							ok = (*thing == 0);
							free(thing);
						}
						else if(strcmp(label, "NB") == 0)
						{
							char *thing;
							cp = skipwhite(cp);
							if(!EOL(*cp))
								thing = getstring(cp, &cp);
							else
								thing = memcheck(strdup(""));
							ok = (*thing != 0);
							free(thing);
						}
						else if(strcmp(label, "IDN") == 0)
						{
							char *thing1, *thing2;
							thing1 = getstring(cp, &cp);
							cp = skipdelim(cp);
							if(!EOL(*cp))
								thing2 = getstring(cp, &cp);
							else
								thing2 = memcheck(strdup(""));
							ok = (strcmp(thing1, thing2) == 0);
							free(thing1);
							free(thing2);
						}
						else if(strcmp(label, "DIF") == 0)
						{
							char *thing1, *thing2;
							thing1 = getstring(cp, &cp);
							cp = skipdelim(cp);
							if(!EOL(*cp))
								thing2 = getstring(cp, &cp);
							else
								thing2 = memcheck(strdup(""));
							ok = (strcmp(thing1, thing2) != 0);
							free(thing1);
							free(thing2);
						}
						else
						{
							int sword;
							unsigned uword;
							EX_TREE *value = parse_expr(cp, 0);

							cp = value->cp;

							if(value->type != EX_LIT)
							{
								report(stack->top, "Bad .IF expression\n");
								list_value(stack->top, 0);
								free_tree(value);
								ok = FALSE;		/* Pick something. */
							}
							else
							{
								unsigned word;
								/* Convert to signed and unsigned words */
								sword = value->data.lit & 0x7fff;

								/* FIXME I don't know if the following
								   is portable enough.  */
								if(value->data.lit & 0x8000)
									sword |= ~0xFFFF; /* Render negative */

								 /* Reduce unsigned value to 16 bits */
								uword = value->data.lit & 0xffff;

								if(strcmp(label, "EQ") == 0 ||
								   strcmp(label, "Z") == 0)
									ok = (uword == 0), word = uword;
								else if(strcmp(label, "NE") == 0 ||
										strcmp(label, "NZ") == 0)
									ok = (uword != 0), word = uword;
								else if(strcmp(label, "GT") == 0 ||
										strcmp(label, "G") == 0)
									ok = (sword > 0), word = sword;
								else if(strcmp(label, "GE") == 0)
									ok = (sword >= 0), word = sword;
								else if(strcmp(label, "LT") == 0 ||
										strcmp(label, "L") == 0)
									ok = (sword < 0), word = sword;
								else if(strcmp(label, "LE") == 0)
									ok = (sword <= 0), word = sword;

								list_value(stack->top, word);

								free_tree(value);
							}
						}

						free(label);

						if(op->value == P_IIF)
						{
							stmtno++;		/* the second half is a
											   separate statement */
							if(ok)
							{
								/* The "immediate if" */
								/* Only slightly tricky. */
								cp = skipdelim(cp);
								label = get_symbol(cp, &ncp, &local);
								goto reassemble;
							}
							return 1;
						}

						push_cond(ok, stack->top);

						if(!ok)
							suppressed++;			/* Assembly
													   suppressed
													   until .ENDC */
					}
					return 1;

				case P_IFF:
					if(last_cond < 0)
					{
						report(stack->top, "No conditional block active\n");
						return 0;
					}
					if(conds[last_cond].ok)	/* Suppress if last cond
											   is true */
						suppressed++;
					return 1;

				case P_IFT:
					if(last_cond < 0)
					{
						report(stack->top, "No conditional block active\n");
						return 0;
					}
					if(!conds[last_cond].ok) /* Suppress if last cond
												is false */
						suppressed++;
					return 1;

				case P_IFTF:
					if(last_cond < 0)
					{
						report(stack->top, "No conditional block active\n");
						return 0;
					}
					return 1;						/* Don't suppress. */

				case P_ENDC:
					if(last_cond < 0)
					{
						report(stack->top, "No conditional block active\n");
						return 0;
					}

					pop_cond(last_cond-1);
					return 1;

				case P_EVEN:
					if(DOT & 1)
					{
						list_word(stack->top, DOT, 0, 1, "");
						DOT++;
					}
					return 1;

				case P_ODD:
					if(!(DOT & 1))
					{
						list_word(stack->top, DOT, 0, 1, "");
						DOT++;
					}
					return 1;

				case P_ASECT:
					go_section(tr, &absolute_section);
					return 1;

				case P_CSECT:
				case P_PSECT:
					{
						SYMBOL *sectsym;
						SECTION *sect;

						label = get_symbol(cp, &cp, NULL);
						if(label == NULL)
							label = memcheck(strdup("")); /* Allow blank */

						sectsym = lookup_sym(label, &section_st);
						if(sectsym)
						{
							sect = sectsym->section;
							free(label);
						}
						else
						{
							sect = new_section();
							sect->label = label;
							sect->flags = 0;
							sect->pc = 0;
							sect->size = 0;
							sect->type = USER;
							sections[sector++] = sect;
							sectsym = add_sym(label, 0, 0, sect, &section_st);
						}

						if(op->value == P_PSECT)
							sect->flags |= PSECT_REL;
						else if(op->value == P_CSECT)
							sect->flags |= PSECT_REL|PSECT_COM|PSECT_GBL;

						while(cp = skipdelim(cp), !EOL(*cp))
						{
							/* Parse section options */
							label = get_symbol(cp, &cp, NULL);
							if(strcmp(label, "ABS") == 0)
							{
								sect->flags &= ~PSECT_REL; /* Not relative */
								sect->flags |= PSECT_COM; /* implies common */
							}
							else if(strcmp(label, "REL") == 0)
							{
								sect->flags |= PSECT_REL;	/* Is relative */
							}
							else if(strcmp(label, "SAV") == 0)
							{
								sect->flags |= PSECT_SAV;	/* Is root */
							}
							else if(strcmp(label, "OVR") == 0)
							{
								sect->flags |= PSECT_COM;	/* Is common */
							}
							else if(strcmp(label, "RW") == 0)
							{
								sect->flags &= ~PSECT_RO;	/* Not read-only */
							}
							else if(strcmp(label, "RO") == 0)
							{
								sect->flags |= PSECT_RO;	/* Is read-only */
							}
							else if(strcmp(label, "I") == 0)
							{
								sect->flags &= ~PSECT_DATA;	/* Not data */
							}
							else if(strcmp(label, "D") == 0)
							{
								sect->flags |= PSECT_DATA;	/* data */
							}
							else if(strcmp(label, "GBL") == 0)
							{
								sect->flags |= PSECT_GBL; /* Global */
							}
							else if(strcmp(label, "LCL") == 0)
							{
								sect->flags &= ~PSECT_GBL;	/* Local */
							}
							else
							{
								report(stack->top,
									   "Unknown flag %s given to "
									   ".PSECT directive\n", label);
								free(label);
								return 0;
							}

							free(label);
						}

						go_section(tr, sect);

						return 1;
					} /* end PSECT code */
					break;

				case P_WEAK:
				case P_GLOBL:
					{
						SYMBOL *sym;
						while(!EOL(*cp))
						{
							/* Loop and make definitions for
							   comma-separated symbols */
							label = get_symbol(cp, &ncp, NULL);
							if(label == NULL)
							{
								report(stack->top,
									   "Illegal .GLOBL/.WEAK "
									   "syntax\n");
								return 0;
							}

							sym = lookup_sym(label, &symbol_st);
							if(sym)
							{
								sym->flags |=
									GLOBAL|
									(op->value == P_WEAK ? WEAK : 0);
							}
							else
								sym = add_sym(label, 0,
											  GLOBAL|
											  (op->value == P_WEAK ? WEAK : 0),
											  &absolute_section, &symbol_st);

							free(label);
							cp = skipdelim(ncp);
						}
					}
					return 1;

				case P_WORD:
					{
						/* .WORD might be followed by nothing, which
						   is an implicit .WORD 0 */
						if(EOL(*cp))
						{
							if(DOT & 1)
							{
								report(stack->top, ".WORD on odd "
									   "boundary\n");
								DOT++; /* Fix it, too */
							}
							store_word(stack->top, tr, 2, 0);
							return 1;
						}
						else
							return do_word(stack, tr, cp, 2);
					}

				case P_BYTE:
					if(EOL(*cp))
					{
						/* Blank .BYTE.  Same as .BYTE 0 */
						store_word(stack->top, tr, 1, 0);
						return 1;
					}
					else
						return do_word(stack, tr, cp, 1);

				case P_BLKW:
				case P_BLKB:
					{
						EX_TREE *value = parse_expr(cp, 0);
						int ok = 1;
						if(value->type != EX_LIT)
						{
							report(stack->top,
								   "Argument to .BLKB/.BLKW "
								   "must be constant\n");
							ok = 0;
						}
						else
						{
							list_value(stack->top, DOT);
							DOT += value->data.lit *
								(op->value == P_BLKW ? 2 : 1);
							change_dot(tr, 0);
						}
						free_tree(value);
						return ok;
					}

				case P_ASCIZ:
				case P_ASCII:
					{
						EX_TREE *value;

						do
						{
							cp = skipwhite(cp);
							if(*cp == '<' || *cp == '^')
							{
								/* A byte value */
								value = parse_expr(cp, 0);
								cp = value->cp;
								store_value(stack, tr, 1, value);
								free_tree(value);
							}
							else
							{
								char quote = *cp++;
								while(*cp && *cp != '\n' && *cp != quote)
								{
									store_word(stack->top, tr, 1, *cp++);
								}
								cp++;		/* Skip closing quote */
							}

							cp = skipwhite(cp);
						} while(!EOL(*cp));

						if(op->value == P_ASCIZ)
						{
							store_word(stack->top, tr, 1, 0);
						}

						return 1;
					}

				case P_RAD50:

					if(DOT & 1)
					{
						report(stack->top, ".RAD50 on odd "
							   "boundary\n");
						DOT++;	/* Fix it */
					}

					while(!EOL(*cp))
					{
						char endstr[6];
						int len;
						char *radstr;
						char *radp;

						endstr[0] = *cp++;
						endstr[1] = '\n';
						endstr[2] = 0;

						len = strcspn(cp, endstr);
						radstr = memcheck(malloc(len + 1));
						memcpy(radstr, cp, len);
						radstr[len] = 0;
						cp += len;
						if(*cp && *cp != '\n')
							cp++;
						for(radp = radstr; *radp;)
						{
							unsigned rad;
							rad = rad50(radp, &radp);
							store_word(stack->top, tr, 2, rad);
						}
						free(radstr);

						cp = skipwhite(cp);
					}
					return 1;

				default:
					report(stack->top, "Unimplemented directive %s\n",
						   op->label);
					return 0;

				} /* end switch (PSEUDO operation) */

				case INSTRUCTION:
					{
						/* The PC must always be even. */
						if(DOT & 1)
						{
							report(stack->top,
								   "Instruction on odd address\n");
							DOT++; /* ...and fix it... */
						}

						switch(op->flags & OC_MASK)
						{
						case OC_NONE:
							/* No operands. */
							store_word(stack->top, tr, 2, op->value);
							return 1;

						case OC_MARK:
							/* MARK, EMT, TRAP */
							{
								EX_TREE *value;
								unsigned word;

								cp = skipwhite(cp);
								if(*cp == '#')
									cp++;		/* Allow the hash, but
												   don't require it */
								value = parse_expr(cp, 0);
								if(value->type != EX_LIT)
								{
									report(stack->top,
										   "Instruction requires "
										   "simple literal operand\n");
									word = op->value;
								}
								else
								{
									word = op->value | value->data.lit;
								}

								store_word(stack->top, tr, 2, word);
								free_tree(value);
							}
							return 1;

						case OC_1GEN:
							/* One general addressing mode */
							{
								ADDR_MODE mode;
								unsigned word;

								if(!get_mode(cp, &cp, &mode))
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									return 0;
								}

								if(op->value == 0100 &&
								   (mode.type & 07) == 0)
								{
									report(stack->top,
										   "JMP Rn is illegal\n");
									/* But encode it anyway... */
								}

								 /* Build instruction word */
								word = op->value | mode.type;
								store_word(stack->top, tr, 2, word);
								mode_extension(tr, &mode, stack->top);
							}
							return 1;

						case OC_2GEN:
							/* Two general addressing modes */
							{
								ADDR_MODE left, right;
								unsigned word;

								if(!get_mode(cp, &cp, &left))
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									return 0;
								}

								if(*cp++ != ',')
								{
									report(stack->top, "Illegal syntax\n");
									free_addr_mode(&left);
									return 0;
								}

								if(!get_mode(cp, &cp, &right))
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_addr_mode(&left);
									return 0;
								}

								/* Build instruction word */
								word = op->value | left.type << 6 | right.type;
								store_word(stack->top, tr, 2, word);
								mode_extension(tr, &left, stack->top);
								mode_extension(tr, &right, stack->top);
							}
							return 1;

						case OC_BR:
							/* branches */
							{
								EX_TREE *value;
								unsigned offset;

								value = parse_expr(cp, 0);
								cp = value->cp;

								/* Relative PSECT or absolute? */
								if(current_pc->section->flags & PSECT_REL)
								{
									SYMBOL *sym;

									/* Can't branch unless I can
									   calculate the offset. */

									/* You know, I *could* branch
									   between sections if I feed the
									   linker a complex relocation
									   expression to calculate the
									   offset.  But I won't. */

									if(!express_sym_offset(value,
														   &sym,
														   &offset) ||
									   sym->section != current_pc->section)
									{
										report(stack->top,
											   "Bad branch target\n");
										store_word(stack->top, tr,
												   2, op->value);
										free_tree(value);
										return 0;
									}

									/* Compute the branch offset and
									   check for addressability */
									offset += sym->value;
									offset -= DOT + 2;
								}
								else
								{
									if(value->type != EX_LIT)
									{
										report(stack->top,
											   "Bad branch target\n");
										store_word(stack->top, tr,
												   2, op->value);
										free_tree(value);
										return 0;
									}

									offset = value->data.lit -
										(DOT + 2);
								}

								if(!check_branch(stack, offset, -256,
												 255))
									offset = 0;

								/* Emit the branch code */
								offset &= 0777;/* Reduce to 9 bits */
								offset >>= 1; /* Shift to become
												 word offset */

								store_word(stack->top, tr,
										   2, op->value | offset);

								free_tree(value);
							}
							return 1;

						case OC_SOB:
							{
								EX_TREE *value;
								unsigned reg;
								unsigned offset;

								value = parse_expr(cp, 0);
								cp = value->cp;

								reg = get_register(value);
								free_tree(value);
								if(reg == NO_REG)
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									return 0;
								}

								cp = skipwhite(cp);
								if(*cp++ != ',')
								{
									report(stack->top, "Illegal syntax\n");
									return 0;
								}

								value = parse_expr(cp, 0);
								cp = value->cp;

								/* Relative PSECT or absolute? */
								if(current_pc->section->flags & PSECT_REL)
								{
									SYMBOL *sym;

									if(!express_sym_offset(value,
														   &sym, &offset))
									{
										report(stack->top,
											   "Bad branch target\n");
										free_tree(value);
										return 0;
									}
									/* Must be same section */
									if(sym->section != current_pc->section)
									{
										report(stack->top,
											   "Bad branch target\n");
										free_tree(value);
										offset = 0;
									}
									else
									{
										/* Calculate byte offset */
										offset += DOT + 2;
										offset -= sym->value;
									}
								}
								else
								{
									if(value->type != EX_LIT)
									{
										report(stack->top, "Bad branch "
											   "target\n");
										offset = 0;
									}
									else
									{
										offset = DOT + 2 -
											value->data.lit;
									}
								}

								if(!check_branch(stack, offset, 0, 126))
									offset = 0;

								offset &= 0177;		/* Reduce to 7 bits */
								offset >>= 1;		/* Shift to become word offset */
								store_word(stack->top, tr, 2,
										   op->value | offset | (reg << 6));

								free_tree(value);
							}
							return 1;

						case OC_ASH:
								/* First op is gen, second is register. */
							{
								ADDR_MODE mode;
								EX_TREE *value;
								unsigned reg;
								unsigned word;

								if(!get_mode(cp, &cp, &mode))
								{
									report(stack->top, "Illegal addressing mode\n");
									return 0;
								}

								cp = skipwhite(cp);
								if(*cp++ != ',')
								{
									report(stack->top, "Illegal addressing mode\n");
									free_addr_mode(&mode);
									return 0;
								}
								value = parse_expr(cp, 0);
								cp = value->cp;

								reg = get_register(value);
								if(reg == NO_REG)
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_tree(value);
									free_addr_mode(&mode);
									return 0;
								}

								/* Instruction word */
								word = op->value | mode.type | (reg << 6);
								store_word(stack->top, tr, 2, word);
								mode_extension(tr, &mode, stack->top);
								free_tree(value);
							}
							return 1;

						case OC_JSR:
								/* First op is register, second is gen. */
							{
								ADDR_MODE mode;
								EX_TREE *value;
								unsigned reg;
								unsigned word;

								value = parse_expr(cp, 0);
								cp = value->cp;

								reg = get_register(value);
								if(reg == NO_REG)
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_tree(value);
									return 0;
								}

								cp = skipwhite(cp);
								if(*cp++ != ',')
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									return 0;
								}

								if(!get_mode(cp, &cp, &mode))
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_tree(value);
									return 0;
								}
								word = op->value | mode.type | (reg << 6);
								store_word(stack->top, tr, 2, word);
								mode_extension(tr, &mode, stack->top);
								free_tree(value);
							}
							return 1;

						case OC_1REG:
							/* One register (RTS) */
							{
								EX_TREE *value;
								unsigned reg;

								value = parse_expr(cp, 0);
								cp = value->cp;
								reg = get_register(value);
								if(reg == NO_REG)
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_tree(value);
									reg = 0;
								}

								store_word(stack->top, tr,
										   2, op->value | reg);
								free_tree(value);
							}
							return 1;

						case OC_1FIS:
							/* One one gen and one reg 0-3 */
							{
								ADDR_MODE mode;
								EX_TREE *value;
								unsigned reg;
								unsigned word;

								if(!get_mode(cp, &cp, &mode))
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									return 0;
								}

								cp = skipwhite(cp);
								if(*cp++ != ',')
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_addr_mode(&mode);
									return 0;
								}

								value = parse_expr(cp, 0);
								cp = value->cp;

								reg = get_register(value);
								if(reg == NO_REG || reg > 4)
								{
									report(stack->top,
										   "Invalid destination register\n");
									reg = 0;
								}

								word = op->value | mode.type | (reg << 6);
								store_word(stack->top, tr, 2, word);
								mode_extension(tr, &mode, stack->top);
								free_tree(value);
							}
							return 1;

						case OC_2FIS:
							/* One reg 0-3 and one gen */
							{
								ADDR_MODE mode;
								EX_TREE *value;
								unsigned reg;
								unsigned word;
								int ok = 1;

								value = parse_expr(cp, 0);
								cp = value->cp;

								reg = get_register(value);
								if(reg == NO_REG || reg > 4)
								{
									report(stack->top,
										   "Illegal source register\n");
									reg = 0;
									ok = 0;
								}

								cp = skipwhite(cp);
								if(*cp++ != ',')
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_tree(value);
									return 0;
								}

								if(!get_mode(cp, &cp, &mode))
								{
									report(stack->top,
										   "Illegal addressing mode\n");
									free_tree(value);
									return 0;
								}

								word = op->value | mode.type | (reg << 6);
								store_word(stack->top, tr, 2, word);
								mode_extension(tr, &mode, stack->top);
								free_tree(value);
							}
							return 1;

						default:
							report(stack->top,
								   "Unimplemented instruction format\n");
							return 0;
						} /* end(handle an instruction) */
					}
					break;
			} /* end switch(section type) */
		} /* end if (op is a symbol) */
	}

	/* Only thing left is an implied .WORD directive */

	free(label);

	return do_word(stack, tr, cp, 2);
}

/* assemble_stack assembles the input stack.  It returns the error
   count. */

static int assemble_stack(STACK *stack, TEXT_RLD *tr)
{
	int res;
	int count = 0;

	while((res = assemble(stack, tr)) >= 0)
	{
		list_flush();
		if(res == 0)
			count++;			/* Count an error */
	}

	return count;
}

/* write_globals writes out the GSD prior to the second assembly pass */

static void write_globals(FILE *obj)
{
	GSD gsd;
	SYMBOL *sym;
	SECTION *psect;
	SYMBOL_ITER sym_iter;
	int isect;

	if(obj == NULL)
		return;					/* Nothing to do if no OBJ file. */

	gsd_init(&gsd, obj);

	gsd_mod(&gsd, module_name);

	if(ident)
		gsd_ident(&gsd, ident);

	/* write out each PSECT with it's global stuff */
	/* Sections must be written out in the order that they
	   appear in the assembly file.  */
	for(isect = 0; isect < sector; isect++)
	{
		psect = sections[isect];

		gsd_psect(&gsd, psect->label, psect->flags, psect->size);
		psect->sector = isect;	/* Assign it a sector */
		psect->pc = 0;			/* Reset it's PC for second pass */

		sym = first_sym(&symbol_st, &sym_iter);
		while(sym)
		{
			if((sym->flags & GLOBAL) &&
				sym->section == psect)
			{
				gsd_global(&gsd, sym->label,
					(sym->flags & DEFINITION ? GLOBAL_DEF : 0) |
					((sym->flags & WEAK) ? GLOBAL_WEAK : 0) |
					((sym->section->flags & PSECT_REL) ? GLOBAL_REL : 0) |
					0100,	/* Looks undefined, but add it in anyway */
					sym->value);
			}
			sym = next_sym(&symbol_st, &sym_iter);
		}
	}

	/* Now write out the transfer address */
	if(xfer_address->type == EX_LIT)
	{
		gsd_xfer(&gsd, ". ABS.", xfer_address->data.lit);
	}
	else
	{
		SYMBOL *sym;
		unsigned offset;
		if(!express_sym_offset(xfer_address, &sym, &offset))
		{
			report(NULL, "Illegal program transfer address\n");
		}
		else
		{
			gsd_xfer(&gsd, sym->section->label, sym->value + offset);
		}
	}

	gsd_flush(&gsd);

	gsd_end(&gsd);
}

/* add_symbols adds all the internal symbols. */

static void add_symbols(SECTION *current_section)
{
	current_pc = add_sym(".", 0, 0, current_section, &symbol_st);

	reg_sym[0] = add_sym("R0", 0, 0, &register_section, &system_st);
	reg_sym[1] = add_sym("R1", 1, 0, &register_section, &system_st);
	reg_sym[2] = add_sym("R2", 2, 0, &register_section, &system_st);
	reg_sym[3] = add_sym("R3", 3, 0, &register_section, &system_st);
	reg_sym[4] = add_sym("R4", 4, 0, &register_section, &system_st);
	reg_sym[5] = add_sym("R5", 5, 0, &register_section, &system_st);
	reg_sym[6] = add_sym("SP", 6, 0, &register_section, &system_st);
	reg_sym[7] = add_sym("PC", 7, 0, &register_section, &system_st);

	add_sym(".ASCII", P_ASCII,   0, &pseudo_section, &system_st);
	add_sym(".ASCIZ", P_ASCIZ,   0, &pseudo_section, &system_st);
	add_sym(".ASECT", P_ASECT,   0, &pseudo_section, &system_st);
	add_sym(".BLKB",  P_BLKB,    0, &pseudo_section, &system_st);
	add_sym(".BLKW",  P_BLKW,    0, &pseudo_section, &system_st);
	add_sym(".BYTE",  P_BYTE,    0, &pseudo_section, &system_st);
	add_sym(".CSECT", P_CSECT,   0, &pseudo_section, &system_st);
	add_sym(".DSABL", P_DSABL,   0, &pseudo_section, &system_st);
	add_sym(".ENABL", P_ENABL,   0, &pseudo_section, &system_st);
	add_sym(".END",   P_END,     0, &pseudo_section, &system_st);
	add_sym(".ENDC",  P_ENDC,    0, &pseudo_section, &system_st);
	add_sym(".ENDM",  P_ENDM,    0, &pseudo_section, &system_st);
	add_sym(".ENDR",  P_ENDR,    0, &pseudo_section, &system_st);
	add_sym(".EOT",   P_EOT,     0, &pseudo_section, &system_st);
	add_sym(".ERROR", P_ERROR,   0, &pseudo_section, &system_st);
	add_sym(".EVEN",  P_EVEN,    0, &pseudo_section, &system_st);
	add_sym(".FLT2",  P_FLT2,    0, &pseudo_section, &system_st);
	add_sym(".FLT4",  P_FLT4,    0, &pseudo_section, &system_st);
	add_sym(".GLOBL", P_GLOBL,   0, &pseudo_section, &system_st);
	add_sym(".IDENT", P_IDENT,   0, &pseudo_section, &system_st);
	add_sym(".IF",    P_IF,      0, &pseudo_section, &system_st);
	add_sym(".IFDF",  P_IFDF,    0, &pseudo_section, &system_st);
	add_sym(".IFNDF", P_IFDF,    0, &pseudo_section, &system_st);
	add_sym(".IFF",   P_IFF,     0, &pseudo_section, &system_st);
	add_sym(".IFT",   P_IFT,     0, &pseudo_section, &system_st);
	add_sym(".IFTF",  P_IFTF,    0, &pseudo_section, &system_st);
	add_sym(".IIF",   P_IIF,     0, &pseudo_section, &system_st);
	add_sym(".IRP",   P_IRP,     0, &pseudo_section, &system_st);
	add_sym(".IRPC",  P_IRPC,    0, &pseudo_section, &system_st);
	add_sym(".LIMIT", P_LIMIT,   0, &pseudo_section, &system_st);
	add_sym(".LIST",  P_LIST,    0, &pseudo_section, &system_st);
	add_sym(".MCALL", P_MCALL,   0, &pseudo_section, &system_st);
	add_sym(".MEXIT", P_MEXIT,   0, &pseudo_section, &system_st);
	add_sym(".NARG",  P_NARG,    0, &pseudo_section, &system_st);
	add_sym(".NCHR",  P_NCHR,    0, &pseudo_section, &system_st);
	add_sym(".NLIST", P_NLIST,   0, &pseudo_section, &system_st);
	add_sym(".NTYPE", P_NTYPE,   0, &pseudo_section, &system_st);
	add_sym(".ODD",   P_ODD,     0, &pseudo_section, &system_st);
	add_sym(".PACKE", P_PACKED,  0, &pseudo_section, &system_st);
	add_sym(".PAGE",  P_PAGE,    0, &pseudo_section, &system_st);
	add_sym(".PRINT", P_PRINT,   0, &pseudo_section, &system_st);
	add_sym(".PSECT", P_PSECT,   0, &pseudo_section, &system_st);
	add_sym(".RADIX", P_RADIX,   0, &pseudo_section, &system_st);
	add_sym(".RAD50", P_RAD50,   0, &pseudo_section, &system_st);
	add_sym(".REM",   P_REM,     0, &pseudo_section, &system_st);
	add_sym(".REPT",  P_REPT,    0, &pseudo_section, &system_st);
	add_sym(".RESTO", P_RESTORE, 0, &pseudo_section, &system_st);
	add_sym(".SAVE",  P_SAVE,    0, &pseudo_section, &system_st);
	add_sym(".SBTTL", P_SBTTL,   0, &pseudo_section, &system_st);
	add_sym(".TITLE", P_TITLE,   0, &pseudo_section, &system_st);
	add_sym(".WORD",  P_WORD,    0, &pseudo_section, &system_st);
	add_sym(".MACRO", P_MACRO,   0, &pseudo_section, &system_st);
	add_sym(".WEAK",  P_WEAK,    0, &pseudo_section, &system_st);

	add_sym("ADC",    I_ADC,    OC_1GEN, &instruction_section, &system_st);
	add_sym("ADCB",   I_ADCB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("ADD",    I_ADD,    OC_2GEN, &instruction_section, &system_st);
	add_sym("ASH",    I_ASH,    OC_ASH,  &instruction_section, &system_st);
	add_sym("ASHC",   I_ASHC,   OC_ASH,  &instruction_section, &system_st);
	add_sym("ASL",    I_ASL,    OC_1GEN, &instruction_section, &system_st);
	add_sym("ASLB",   I_ASLB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("ASR",    I_ASR,    OC_1GEN, &instruction_section, &system_st);
	add_sym("ASRB",   I_ASRB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("BCC",    I_BCC,    OC_BR,   &instruction_section, &system_st);
	add_sym("BCS",    I_BCS,    OC_BR,   &instruction_section, &system_st);
	add_sym("BEQ",    I_BEQ,    OC_BR,   &instruction_section, &system_st);
	add_sym("BGE",    I_BGE,    OC_BR,   &instruction_section, &system_st);
	add_sym("BGT",    I_BGT,    OC_BR,   &instruction_section, &system_st);
	add_sym("BHI",    I_BHI,    OC_BR,   &instruction_section, &system_st);
	add_sym("BHIS",   I_BHIS,   OC_BR,   &instruction_section, &system_st);
	add_sym("BIC",    I_BIC,    OC_2GEN, &instruction_section, &system_st);
	add_sym("BICB",   I_BICB,   OC_2GEN, &instruction_section, &system_st);
	add_sym("BIS",    I_BIS,    OC_2GEN, &instruction_section, &system_st);
	add_sym("BISB",   I_BISB,   OC_2GEN, &instruction_section, &system_st);
	add_sym("BIT",    I_BIT,    OC_2GEN, &instruction_section, &system_st);
	add_sym("BITB",   I_BITB,   OC_2GEN, &instruction_section, &system_st);
	add_sym("BLE",    I_BLE,    OC_BR,   &instruction_section, &system_st);
	add_sym("BLO",    I_BLO,    OC_BR,   &instruction_section, &system_st);
	add_sym("BLOS",   I_BLOS,   OC_BR,   &instruction_section, &system_st);
	add_sym("BLT",    I_BLT,    OC_BR,   &instruction_section, &system_st);
	add_sym("BMI",    I_BMI,    OC_BR,   &instruction_section, &system_st);
	add_sym("BNE",    I_BNE,    OC_BR,   &instruction_section, &system_st);
	add_sym("BPL",    I_BPL,    OC_BR,   &instruction_section, &system_st);
	add_sym("BPT",    I_BPT,    OC_NONE, &instruction_section, &system_st);
	add_sym("BR",     I_BR,     OC_BR,   &instruction_section, &system_st);
	add_sym("BVC",    I_BVC,    OC_BR,   &instruction_section, &system_st);
	add_sym("BVS",    I_BVS,    OC_BR,   &instruction_section, &system_st);
	add_sym("CALL",   I_CALL,   OC_1GEN, &instruction_section, &system_st);
	add_sym("CALLR",  I_CALLR,  OC_1GEN, &instruction_section, &system_st);
	add_sym("CCC",    I_CCC,    OC_NONE, &instruction_section, &system_st);
	add_sym("CLC",    I_CLC,    OC_NONE, &instruction_section, &system_st);
	add_sym("CLN",    I_CLN,    OC_NONE, &instruction_section, &system_st);
	add_sym("CLR",    I_CLR,    OC_1GEN, &instruction_section, &system_st);
	add_sym("CLRB",   I_CLRB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("CLV",    I_CLV,    OC_NONE, &instruction_section, &system_st);
	add_sym("CLZ",    I_CLZ,    OC_NONE, &instruction_section, &system_st);
	add_sym("CMP",    I_CMP,    OC_2GEN, &instruction_section, &system_st);
	add_sym("CMPB",   I_CMPB,   OC_2GEN, &instruction_section, &system_st);
	add_sym("COM",    I_COM,    OC_1GEN, &instruction_section, &system_st);
	add_sym("COMB",   I_COMB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("DEC",    I_DEC,    OC_1GEN, &instruction_section, &system_st);
	add_sym("DECB",   I_DECB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("DIV",    I_DIV,    OC_ASH,  &instruction_section, &system_st);
	add_sym("EMT",    I_EMT,    OC_MARK, &instruction_section, &system_st);
	add_sym("FADD",   I_FADD,   OC_1REG, &instruction_section, &system_st);
	add_sym("FDIV",   I_FDIV,   OC_1REG, &instruction_section, &system_st);
	add_sym("FMUL",   I_FMUL,   OC_1REG, &instruction_section, &system_st);
	add_sym("FSUB",   I_FSUB,   OC_1REG, &instruction_section, &system_st);
	add_sym("HALT",   I_HALT,   OC_NONE, &instruction_section, &system_st);
	add_sym("INC",    I_INC,    OC_1GEN, &instruction_section, &system_st);
	add_sym("INCB",   I_INCB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("IOT",    I_IOT,    OC_NONE, &instruction_section, &system_st);
	add_sym("JMP",    I_JMP,    OC_1GEN, &instruction_section, &system_st);
	add_sym("JSR",    I_JSR,    OC_JSR,  &instruction_section, &system_st);
	add_sym("MARK",   I_MARK,   OC_MARK, &instruction_section, &system_st);
	add_sym("MED6X",  I_MED6X,  OC_NONE, &instruction_section, &system_st);
	add_sym("MED74C", I_MED74C, OC_NONE, &instruction_section, &system_st);
	add_sym("MFPD",   I_MFPD,   OC_1GEN, &instruction_section, &system_st);
	add_sym("MFPI",   I_MFPI,   OC_1GEN, &instruction_section, &system_st);
	add_sym("MFPS",   I_MFPS,   OC_1GEN, &instruction_section, &system_st);
	add_sym("MOV",    I_MOV,    OC_2GEN, &instruction_section, &system_st);
	add_sym("MOVB",   I_MOVB,   OC_2GEN, &instruction_section, &system_st);
	add_sym("MTPD",   I_MTPD,   OC_1GEN, &instruction_section, &system_st);
	add_sym("MTPI",   I_MTPI,   OC_1GEN, &instruction_section, &system_st);
	add_sym("MTPS",   I_MTPS,   OC_1GEN, &instruction_section, &system_st);
	add_sym("MUL",    I_MUL,    OC_ASH,  &instruction_section, &system_st);
	add_sym("NEG",    I_NEG,    OC_1GEN, &instruction_section, &system_st);
	add_sym("NEGB",   I_NEGB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("NOP",    I_NOP,    OC_NONE, &instruction_section, &system_st);
	add_sym("RESET",  I_RESET,  OC_NONE, &instruction_section, &system_st);
	add_sym("RETURN", I_RETURN, OC_NONE, &instruction_section, &system_st);
	add_sym("ROL",    I_ROL,    OC_1GEN, &instruction_section, &system_st);
	add_sym("ROLB",   I_ROLB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("ROR",    I_ROR,    OC_1GEN, &instruction_section, &system_st);
	add_sym("RORB",   I_RORB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("RTI",    I_RTI,    OC_NONE, &instruction_section, &system_st);
	add_sym("RTS",    I_RTS,    OC_1REG, &instruction_section, &system_st);
	add_sym("RTT",    I_RTT,    OC_NONE, &instruction_section, &system_st);
	add_sym("SBC",    I_SBC,    OC_1GEN, &instruction_section, &system_st);
	add_sym("SBCB",   I_SBCB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("SCC",    I_SCC,    OC_NONE, &instruction_section, &system_st);
	add_sym("SEC",    I_SEC,    OC_NONE, &instruction_section, &system_st);
	add_sym("SEN",    I_SEN,    OC_NONE, &instruction_section, &system_st);
	add_sym("SEV",    I_SEV,    OC_NONE, &instruction_section, &system_st);
	add_sym("SEZ",    I_SEZ,    OC_NONE, &instruction_section, &system_st);
	add_sym("SOB",    I_SOB,    OC_SOB,  &instruction_section, &system_st);
	add_sym("SPL",    I_SPL,    OC_1REG, &instruction_section, &system_st);
	add_sym("SUB",    I_SUB,    OC_2GEN, &instruction_section, &system_st);
	add_sym("SWAB",   I_SWAB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("SXT",    I_SXT,    OC_1GEN, &instruction_section, &system_st);
	add_sym("TRAP",   I_TRAP,   OC_MARK, &instruction_section, &system_st);
	add_sym("TST",    I_TST,    OC_1GEN, &instruction_section, &system_st);
	add_sym("TSTB",   I_TSTB,   OC_1GEN, &instruction_section, &system_st);
	add_sym("WAIT",   I_WAIT,   OC_NONE, &instruction_section, &system_st);
	add_sym("XFC",    I_XFC,    OC_NONE, &instruction_section, &system_st);
	add_sym("XOR",    I_XOR,    OC_JSR,  &instruction_section, &system_st);
	add_sym("MFPT",   I_MFPT,   OC_NONE, &instruction_section, &system_st);

	add_sym("ABSD",   I_ABSD,   OC_1GEN, &instruction_section, &system_st);
	add_sym("ABSF",   I_ABSF,   OC_1GEN, &instruction_section, &system_st);
	add_sym("ADDD",   I_ADDD,   OC_1FIS, &instruction_section, &system_st);
	add_sym("ADDF",   I_ADDF,   OC_1FIS, &instruction_section, &system_st);
	add_sym("CFCC",   I_CFCC,   OC_NONE, &instruction_section, &system_st);
	add_sym("CLRD",   I_CLRD,   OC_1GEN, &instruction_section, &system_st);
	add_sym("CLRF",   I_CLRF,   OC_1GEN, &instruction_section, &system_st);
	add_sym("CMPD",   I_CMPD,   OC_1FIS, &instruction_section, &system_st);
	add_sym("CMPF",   I_CMPF,   OC_1FIS, &instruction_section, &system_st);
	add_sym("DIVD",   I_DIVD,   OC_1FIS, &instruction_section, &system_st);
	add_sym("DIVF",   I_DIVF,   OC_1FIS, &instruction_section, &system_st);
	add_sym("LDCDF",  I_LDCDF,  OC_1FIS, &instruction_section, &system_st);
	add_sym("LDCID",  I_LDCID,  OC_1FIS, &instruction_section, &system_st);
	add_sym("LDCIF",  I_LDCIF,  OC_1FIS, &instruction_section, &system_st);
	add_sym("LDCLD",  I_LDCLD,  OC_1FIS, &instruction_section, &system_st);
	add_sym("LDCLF",  I_LDCLF,  OC_1FIS, &instruction_section, &system_st);
	add_sym("LDD",    I_LDD,    OC_1FIS, &instruction_section, &system_st);
	add_sym("LDEXP",  I_LDEXP,  OC_1FIS, &instruction_section, &system_st);
	add_sym("LDF",    I_LDF,    OC_1FIS, &instruction_section, &system_st);
	add_sym("LDFPS",  I_LDFPS,  OC_1GEN, &instruction_section, &system_st);
	add_sym("MODD",   I_MODD,   OC_1FIS, &instruction_section, &system_st);
	add_sym("MODF",   I_MODF,   OC_1FIS, &instruction_section, &system_st);
	add_sym("MULD",   I_MULD,   OC_1FIS, &instruction_section, &system_st);
	add_sym("MULF",   I_MULF,   OC_1FIS, &instruction_section, &system_st);
	add_sym("NEGD",   I_NEGD,   OC_1GEN, &instruction_section, &system_st);
	add_sym("NEGF",   I_NEGF,   OC_1GEN, &instruction_section, &system_st);
	add_sym("SETD",   I_SETD,   OC_NONE, &instruction_section, &system_st);
	add_sym("SETF",   I_SETF,   OC_NONE, &instruction_section, &system_st);
	add_sym("SETI",   I_SETI,   OC_NONE, &instruction_section, &system_st);
	add_sym("SETL",   I_SETL,   OC_NONE, &instruction_section, &system_st);
	add_sym("STA0",   I_STA0,   OC_NONE, &instruction_section, &system_st);
	add_sym("STB0",   I_STB0,   OC_NONE, &instruction_section, &system_st);
	add_sym("STCDF",  I_STCDF,  OC_2FIS, &instruction_section, &system_st);
	add_sym("STCDI",  I_STCDI,  OC_2FIS, &instruction_section, &system_st);
	add_sym("STCDL",  I_STCDL,  OC_2FIS, &instruction_section, &system_st);
	add_sym("STCFD",  I_STCFD,  OC_2FIS, &instruction_section, &system_st);
	add_sym("STCFI",  I_STCFI,  OC_2FIS, &instruction_section, &system_st);
	add_sym("STCFL",  I_STCFL,  OC_2FIS, &instruction_section, &system_st);
	add_sym("STD",    I_STD,    OC_2FIS, &instruction_section, &system_st);
	add_sym("STEXP",  I_STEXP,  OC_2FIS, &instruction_section, &system_st);
	add_sym("STF",    I_STF,    OC_2FIS, &instruction_section, &system_st);
	add_sym("STFPS",  I_STFPS,  OC_1GEN, &instruction_section, &system_st);
	add_sym("STST",   I_STST,   OC_1GEN, &instruction_section, &system_st);
	add_sym("SUBD",   I_SUBD,   OC_1FIS, &instruction_section, &system_st);
	add_sym("SUBF",   I_SUBF,   OC_1FIS, &instruction_section, &system_st);
	add_sym("TSTD",   I_TSTD,   OC_1GEN, &instruction_section, &system_st);
	add_sym("TSTF",   I_TSTF,   OC_1GEN, &instruction_section, &system_st);

	/* FIXME: The CIS instructions are missing! */

	add_sym(current_section->label, 0, 0, current_section, &section_st);
}

/* dump_all_macros is a diagnostic function that's currently not
   used.  I used it while debugging, and I haven't removed it. */

static void dump_all_macros(void)
{
	MACRO *mac;
	SYMBOL_ITER iter;

	for(mac = (MACRO *)first_sym(&macro_st, &iter);
	mac != NULL; mac = (MACRO *)next_sym(&macro_st, &iter))
	{
		dumpmacro(mac, lstfile);

		printf("\n\n");
	}
}

/* sym_hist is a diagnostic function that prints a histogram of the
   hash table useage of a symbol table.  I used this to try to tune
   the hash function for better spread.  It's not used now. */

static void sym_hist(SYMBOL_TABLE *st, char *name)
{
	int i;
	SYMBOL *sym;
	fprintf(lstfile, "Histogram for symbol table %s\n", name);
	for(i = 0; i < 1023; i++)
	{
		fprintf(lstfile, "%4d: ", i);
		for(sym = st->hash[i]; sym != NULL; sym = sym->next)
			fputc('#', lstfile);
		fputc('\n', lstfile);
	}
}

/* enable_tf is called by command argument parsing to enable and
   disable named options. */

static void enable_tf(char *opt, int tf)
{
	if(strcmp(opt, "AMA") == 0)
		enabl_ama = tf;
	else if(strcmp(opt, "GBL") == 0)
		enabl_gbl = tf;
	else if(strcmp(opt, "ME") == 0)
		list_me = tf;
	else if(strcmp(opt, "BEX") == 0)
		list_bex = tf;
	else if(strcmp(opt, "MD") == 0)
		list_md = tf;
}

int main(int argc, char *argv[])
{
	char *fnames[32];
	int nr_files = 0;
	FILE *obj = NULL;
	static char line[1024];
	TEXT_RLD tr;
	char *macname = NULL;
	char *objname = NULL;
	char *lstname = NULL;
	int arg;
	int i;
	STACK stack;
	int count;

	for(arg = 1; arg < argc; arg++)
	{
		if(*argv[arg] == '-')
		{
			char *cp;
			cp = argv[arg] + 1;
			switch(tolower(*cp))
			{
			case 'v':
				fprintf(stderr,
						"macro11 Copyright 2001 Richard Krehbiel\n"
						"Version 0.2   July 15, 2001\n");
				break;

			case 'e':
				/* Followed by options to enable */
				/* Since /SHOW and /ENABL option names don't overlap,
				   I consolidate. */
				upcase(argv[++arg]);
				enable_tf(argv[arg], 1);
				break;

			case 'd':
				/* Followed by an option to disable */
				upcase(argv[++arg]);
				enable_tf(argv[arg], 0);
				break;

			case 'm':
				/* Macro library */
				/* This option gives the name of an RT-11 compatible
				   macro library from which .MCALLed macros can be
				   found. */
				arg++;
				mlbs[nr_mlbs] = mlb_open(argv[arg]);
				if(mlbs[nr_mlbs] == NULL)
				{
					fprintf(stderr,
							"Unable to register macro library %s\n",
							argv[arg]);
					exit(EXIT_FAILURE);
				}
				nr_mlbs++;
				break;

			case 'p':		/* P for search path */
				/* The -p option gives the name of a directory in
				   which .MCALLed macros may be found.  */
				{
					char *env = getenv("MCALL");
					char *temp;

					if(env == NULL)
						env = "";

					temp = memcheck(malloc(strlen(env) +
										   strlen(argv[arg+1]) + 8));
					strcpy(temp, "MCALL=");
					strcat(temp, env);
					strcat(temp, PATHSEP);
					strcat(temp, argv[arg+1]);
					putenv(temp);
					arg++;
				}
				break;

			case 'o':
				/* The -o option gives the object file name (.OBJ) */
				++arg;
				objname = argv[arg];
				break;

			case 'l':
				/* The option -l gives the listing file name (.LST) */
				/* -l - enables listing to stdout. */
				lstname = argv[++arg];
				if(strcmp(lstname, "-") == 0)
					lstfile = stdout;
				else
					lstfile = fopen(lstname, "w");
				break;

			case 'x':
				/* The -x option invokes macro11 to expand the
				   contents of the registered macro libraries (see -m)
				   into individual .MAC files in the current
				   directory.  No assembly of input is done.  This
				   must be the last command line option.  */
				{
					int i;
					for(i = 0; i < nr_mlbs; i++)
						mlb_extract(mlbs[i]);
					return EXIT_SUCCESS;
				}

			default:
				fprintf(stderr, "Unknown argument %s\n", argv[arg]);
				exit(EXIT_FAILURE);
			}
		}
		else
		{
			fnames[nr_files++] = argv[arg];
		}
	}

	if(objname)
	{
		obj = fopen(objname, "wb");
		if(obj == NULL)
			return EXIT_FAILURE;
	}

	add_symbols(&blank_section);

	text_init(&tr, NULL, 0);

	module_name = memcheck(strdup(""));

	xfer_address = new_ex_lit(1);		/* The undefined transfer address */

	stack_init(&stack);
	/* Push the files onto the input stream in reverse order */
	for(i = nr_files-1; i >= 0; --i)
	{
		STREAM *str = new_file_stream(fnames[i]);
		if(str == NULL)
		{
			report(NULL, "Unable to open file %s\n", fnames[i]);
			exit(EXIT_FAILURE);
		}
		stack_push(&stack, str);
	}

	DOT = 0;
	current_pc->section = &blank_section;
	last_dot_section = NULL;
	pass = 0;
	stmtno = 0;
	lsb = 0;
	last_lsb = -1;
	last_locsym = 32767;
	last_cond = -1;
	sect_sp = -1;
	suppressed = 0;

	assemble_stack(&stack, &tr);

#if 0
	if(enabl_debug)
		dump_all_macros();
#endif

	assert(stack.top == NULL);

	migrate_implicit();			/* Migrate the implicit globals */
	write_globals(obj);			/* Write the global symbol dictionary */

#if 0
	sym_hist(&symbol_st, "symbol_st");	/* Draw a symbol table histogram */
#endif

	
	text_init(&tr, obj, 0);

	stack_init(&stack);			/* Superfluous... */
	/* Re-push the files onto the input stream in reverse order */
	for(i = nr_files-1; i >= 0; --i)
	{
		STREAM *str = new_file_stream(fnames[i]);
		if(str == NULL)
		{
			report(NULL, "Unable to open file %s\n", fnames[i]);
			exit(EXIT_FAILURE);
		}
		stack_push(&stack, str);
	}

	DOT = 0;
	current_pc->section = &blank_section;
	last_dot_section = NULL;

	pass = 1;
	stmtno = 0;
	lsb = 0;
	last_lsb = -1;
	last_locsym = 32767;
	pop_cond(-1);
	sect_sp = -1;
	suppressed = 0;

	count = assemble_stack(&stack, &tr);

	text_flush(&tr);

	while(last_cond >= 0)
	{
		report(NULL, "%s:%d: Unterminated conditional\n",
			conds[last_cond].file, conds[last_cond].line);
		pop_cond(last_cond - 1);
		count++;
	}

	for(i = 0; i < nr_mlbs; i++)
		mlb_close(mlbs[i]);

	write_endmod(obj);

	if(obj != NULL)
		fclose(obj);

	if(count > 0)
		fprintf(stderr, "%d Errors\n", count);

	if(lstfile && strcmp(lstname, "-") != 0)
		fclose(lstfile);

	return count > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
