/* gri_sys.c: GRI-909 simulator interface

   Copyright (c) 2001-2015, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   14-Jan-08    RMS     Added GRI-99 support
   18-Oct-02    RMS     Fixed bug in symbolic decode (Hans Pufal)
*/

#include "gri_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern DEVICE tti_dev, tto_dev;
extern DEVICE hsr_dev, hsp_dev;
extern DEVICE rtc_dev;
extern REG cpu_reg[];
extern uint16 M[];

void fprint_addr (FILE *of, uint32 val, uint32 mod, uint32 dst);

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "GRI-909";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 2;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &tti_dev,
    &tto_dev,
    &hsr_dev,
    &hsp_dev,
    &rtc_dev,
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "Unimplemented unit",
    "HALT instruction",
    "Breakpoint",
    "Invalid interrupt request"
     };

/* Binary loader

   Bootstrap loader format consists of blocks separated by zeroes.  Each
   word in the block has three frames: a control frame (ignored) and two
   data frames.  The user must specify the load address.  Switch -c means
   continue and load all blocks until end of tape.
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
int32 c;
uint32 org;
t_stat r;
char gbuf[CBUFSIZE];

if (*cptr != 0) {                                       /* more input? */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get origin */
    org = get_uint (gbuf, 8, AMASK, &r);
    if (r != SCPE_OK)
        return r;
    if (*cptr != 0)                                     /* no more */
        return SCPE_ARG;
    }
else org = 0200;                                        /* default 200 */

for (;;) {                                              /* until EOF */
    while ((c = getc (fileref)) == 0) ;                 /* skip starting 0's */
    if (c == EOF)                                       /* EOF? done */
        break;
    for ( ; c != 0; ) {                                 /* loop until ctl = 0 */
                                                        /* ign ctrl frame */
        if ((c = getc (fileref)) == EOF)                /* get high byte */
            return SCPE_FMT;                            /* EOF is error */
        if (!MEM_ADDR_OK (org))
            return SCPE_NXM;
        M[org] = ((c & 0377) << 8);                     /* store high */
        if ((c = getc (fileref)) == EOF)                /* get low byte */
            return SCPE_FMT;                            /* EOF is error */
        M[org] = M[org] | (c & 0377);                   /* store low */
        org = org + 1;                                  /* incr origin */
        if ((c = getc (fileref)) == EOF)                /* get ctrl frame */
            return SCPE_OK;                             /* EOF is ok */
        }                                               /* end block for */
    if (!(sim_switches & SWMASK ('C')))
        return SCPE_OK;
    }                                                   /* end tape for */
return SCPE_OK;
}

/* Symbol tables */

#define F_V_FL  16                                      /* class flag */
#define F_M_FL  017
#define F_V_FO  000                                     /* function out */
#define F_V_FOI 001                                     /* FO, impl reg */
#define F_V_SF  002                                     /* skip function */
#define F_V_SFI 003                                     /* SF, impl reg */
#define F_V_RR  004                                     /* reg reg */
#define F_V_ZR  005                                     /* zero reg */
#define F_V_RS  006                                     /* reg self */
#define F_V_JC  010                                     /* jump cond */
#define F_V_JU  011                                     /* jump uncond */
#define F_V_RM  012                                     /* reg mem */
#define F_V_ZM  013                                     /* zero mem */
#define F_V_MR  014                                     /* mem reg */
#define F_V_MS  015                                     /* mem self */
#define F_2WD   010                                     /* 2 words */

#define F_FO    (F_V_FO << F_V_FL)
#define F_FOI   (F_V_FOI << F_V_FL)
#define F_SF    (F_V_SF << F_V_FL)
#define F_SFI   (F_V_SFI << F_V_FL)
#define F_RR    (F_V_RR << F_V_FL)
#define F_ZR    (F_V_ZR << F_V_FL)
#define F_RS    (F_V_RS << F_V_FL)
#define F_JC    (F_V_JC << F_V_FL)
#define F_JU    (F_V_JU << F_V_FL)
#define F_RM    (F_V_RM << F_V_FL)
#define F_ZM    (F_V_ZM << F_V_FL)
#define F_MR    (F_V_MR << F_V_FL)
#define F_MS    (F_V_MS << F_V_FL)

struct fnc_op {
    uint32      inst;                                   /* instr prot */
    uint32      imask;                                  /* instr mask */
    uint32      oper;                                   /* operator */
    uint32      omask;                                  /* oper mask */
    };

static const int32 masks[] = {
    0176000, 0176077, 0000077, 0176077,
    0000300, 0176300, 0000300, 0177777,
    0000077, 0177777, 0000377, 0176377,
    0176300, 0176377
    };

/* Instruction mnemonics

   Order is critical, as some instructions are more precise versions of
   others.  For example, JU must precede JC, otherwise, JU will be decoded
   as JC 0,ETZ,dst.  There are some ambiguities, eg, what is 02-xxxx-06?
   Priority is as follows:

   FO (02-xxxx-rr)
   SF (rr-xxxx-02)
   MR (06-xxxx-rr)
   RM (rr-xxxx-06)
   JC (rr-xxxx-03)
   RR 
*/

static const char *opcode[] = {
 "FOM", "FOA", "FOI", "FO",                             /* FOx before FO */
 "SFM", "SFA", "SFI", "SF",                             /* SFx before SF */
 "ZM", "ZMD", "ZMI", "ZMID",                            /* ZM before RM */
 "MS", "MSD", "MSI", "MSID",
 "RM", "RMD", "RMI", "RMID",
 "MR", "MRD", "MRI", "MRID",
 "JO", "JOD", "JN", "JND",                              /* JU before JC */
 "JU", "JUD", "JC", "JCD",
 "ZR", "ZRC", "RR", "RRC",                              /* ZR before RR */
 "RS", "RSC",
 NULL
 };

static const uint32 opc_val[] = {
 0004000+F_FOI, 0004013+F_FOI, 0004004+F_FOI, 0004000+F_FO,
 0000002+F_SFI, 0026002+F_SFI, 0010002+F_SFI, 0000002+F_SF,
 0000006+F_ZM, 0000106+F_ZM, 0000206+F_ZM, 0000306+F_ZM,
 0014006+F_MS, 0014106+F_MS, 0014206+F_MS, 0014306+F_MS,
 0000006+F_RM, 0000106+F_RM, 0000206+F_RM, 0000306+F_RM,
 0014000+F_MR, 0014100+F_MR, 0014200+F_MR, 0014300+F_MR,
 0037003+F_JU, 0037103+F_JU, 0037203+F_JU, 0037303+F_JU,
 0000403+F_JU, 0000503+F_JU, 0000003+F_JC, 0000103+F_JC,
 0000000+F_ZR, 0000200+F_ZR, 0000000+F_RR, 0000200+F_RR,
 0000000+F_RS, 0000200+F_RS
 };

/* Unit mnemonics.  All 64 units are decoded, most just to octal integers */

static const char *unsrc[64] = {
 "0", "IR", "2", "TRP", "ISR", "MA", "MB", "SC",        /* 00 - 07 */
 "SWR", "AX", "AY", "AO", "14", "15", "16", "MSR",      /* 10 - 17 */
 "20", "21", "XR", "ATRP", "BSW", "BPK", "BCPA", "BCPB",/* 20 - 27 */
 "GR1", "GR2", "GR3", "GR4", "GR5", "GR6", "36", "37",  /* 30 - 37 */
 "40", "41", "42", "43", "44", "45", "46", "47",
 "50", "51", "52", "53", "54", "CDR", "56", "CADR",
 "60", "61", "62", "63", "64", "65", "DWC", "DCA",
 "DISK", "LPR", "72", "73", "CAS", "RTC", "HSR", "TTI"  /* 70 - 77 */
 };

static const char *undst[64] = {
 "0", "IR", "2", "TRP", "ISR", "5", "MB", "SC",         /* 00 - 07 */
 "SWR", "AX", "AY", "13", "EAO", "15", "16", "MSR",     /* 10 - 17 */
 "20", "21", "XR", "ATRP", "BSW", "BPK", "BCPA", "BCPB",/* 20 - 27 */
 "GR1", "GR2", "GR3", "GR4", "GR5", "GR6", "36", "37",  /* 30 - 37 */
 "40", "41", "42", "43", "44", "45", "46", "47",
 "50", "51", "52", "53", "54", "CDR", "56", "CADR",
 "60", "61", "62", "63", "64", "65", "DWC", "DCA",
 "DISK", "LPR", "72", "73", "CAS", "RTC", "HSP", "TTO"  /* 70 - 77 */
 };

/* Operators */

static const char *opname[4] = {
 NULL, "P1", "L1", "R1"
 };

/* Conditions */

static const char *cdname[8] = {
 "NEVER", "ALWAYS", "ETZ", "NEZ", "LTZ", "GEZ", "LEZ", "GTZ"
 };

/* Function out/sense function */

static const char *fname[] = {
 "NOT",                                                 /* any SF */
 "POK", "LNK", "BOV",                                   /* SFM */
 "SOV", "AOV",                                          /* SFA */
 "IRDY", "ORDY",                                        /* any SF */
 "CLL", "STL", "CML", "HLT",                            /* FOM */
 "ICF", "ICO",                                          /* FOI */
 "ADD", "AND", "XOR", "OR",                             /* FOA */
 "INP", "IRDY", "ORDY", "STRT",                         /* any FO */
 NULL
 };

static const struct fnc_op fop[] = {
 { 0000002, 0000077, 001, 001 },                        /* NOT */
 { 0000002, 0176077, 010, 010 },                        /* POK */
 { 0000002, 0176077, 004, 004 },                        /* LNK */
 { 0000002, 0176077, 002, 002 },                        /* BOV */
 { 0026002, 0176077, 004, 004 },                        /* SOV */
 { 0026002, 0176077, 002, 002 },                        /* AOV */
 { 0000002, 0000077, 010, 010 },                        /* IRDY */
 { 0000002, 0000077, 002, 002 },                        /* ORDY */
 { 0004000, 0176077, 001, 003 },                        /* CLL */
 { 0004000, 0176077, 002, 003 },                        /* STL */
 { 0004000, 0176077, 003, 003 },                        /* CML */
 { 0004000, 0176077, 004, 004 },                        /* HLT */
 { 0004004, 0176077, 001, 001 },                        /* ICF */
 { 0004004, 0176077, 002, 002 },                        /* ICO */
 { 0004013, 0176077, 000, 014 },                        /* ADD */
 { 0004013, 0176077, 004, 014 },                        /* AND */
 { 0004013, 0176077, 010, 014 },                        /* XOR */
 { 0004013, 0176077, 014, 014 },                        /* OR */
 { 0004000, 0176000, 011, 011 },                        /* INP */
 { 0004000, 0176000, 010, 010 },                        /* IRDY */
 { 0004000, 0176000, 002, 002 },                        /* ORDY */
 { 0004000, 0176000, 001, 001 }                         /* STRT */
 };

/* Print opcode field for FO, SF */

/* Use scp.c provided fprintf function */
#define fprintf Fprintf
#define fputs(_s,f) Fprintf(f,"%s",_s)
#define fputc(_c,f) Fprintf(f,"%c",_c)

void fprint_op (FILE *of, uint32 inst, uint32 op)
{
int32 i, nfirst;

for (i = nfirst = 0; fname[i] != NULL; i++) {
    if (((inst & fop[i].imask) == fop[i].inst) &&
        ((op & fop[i].omask) == fop[i].oper)) {
        op = op & ~fop[i].omask;
        if (nfirst)
            fputc (' ', of);
        nfirst = 1;
        fprintf (of, "%s", fname[i]);
        }
    }
if (op)
    fprintf (of, " %o", op);
return;
}

/* Print address field with potential indexing */

void fprint_addr (FILE *of, uint32 val, uint32 mode, uint32 dst)
{
if ((val & INDEX) &&
    ((dst == U_SC) || (mode != MEM_IMM)))
    fprintf (of, "#%o", val & AMASK);
else fprintf (of, "%o", val);
return;
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to data
        *uptr   =       pointer to unit 
        sw      =       switches
   Outputs:
        return  =       status code
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 i, j;
uint32 inst, src, dst, op, bop;

inst = val[0];
if (sw & SWMASK ('A')) {                                /* ASCII? */
    if (inst > 0377)
        return SCPE_ARG;
    fprintf (of, FMTASC (inst & 0177));
    return SCPE_OK;
    }
if (sw & SWMASK ('C')) {                                /* characters? */
    fprintf (of, FMTASC ((inst >> 8) & 0177));
    fprintf (of, FMTASC (inst & 0177));
    return SCPE_OK;
    }
if (!(sw & SWMASK ('M')))
    return SCPE_ARG;

/* Instruction decode */

inst = val[0];
src = I_GETSRC (inst);                                  /* get fields */
op = I_GETOP (inst);
dst = I_GETDST (inst);
bop = op >> 2;                                          /* bus op */
for (i = 0; opcode[i] != NULL; i++) {                   /* loop thru ops */
    j = (opc_val[i] >> F_V_FL) & F_M_FL;                /* get class */
    if ((opc_val[i] & DMASK) == (inst & masks[j])) {    /* match? */

        switch (j) {                                    /* case on class */

        case F_V_FO:                                    /* func out */
            fprintf (of, "%s ", opcode[i]);
            fprint_op (of, inst, op);
            fprintf (of, ",%s", undst[dst]);
            break;

        case F_V_FOI:                                   /* func out impl */
            fprintf (of, "%s ", opcode[i]);
            fprint_op (of, inst, op);
            break;

        case F_V_SF:                                    /* skip func */
            fprintf (of, "%s %s,", opcode[i], unsrc[src]);
            fprint_op (of, inst, op);
            break;

        case F_V_SFI:                                   /* skip func impl */
            fprintf (of, "%s ", opcode[i]);
            fprint_op (of, inst, op);
            break;

        case F_V_RR:                                    /* reg reg */
            if (strcmp (unsrc[src], undst[dst]) == 0) {
                if (bop)
                    fprintf (of, "%s %s,%s", opcode[i + 2],
                             unsrc[src], opname[bop]);
                else fprintf (of, "%s %s", opcode[i + 2], unsrc[src]);
                }
            else {
                if (bop)
                    fprintf (of, "%s %s,%s,%s", opcode[i],
                             unsrc[src], opname[bop], undst[dst]);
                else fprintf (of, "%s %s,%s", opcode[i],
                              unsrc[src], undst[dst]);
                }
            break;

        case F_V_ZR:                                    /* zero reg */
            if (bop)
                fprintf (of, "%s %s,%s", opcode[i],
                         opname[bop], undst[dst]);
            else fprintf (of, "%s %s", opcode[i], undst[dst]);
            break;

        case F_V_JC:                                    /* jump cond */
            fprintf (of, "%s %s,%s,",
                     opcode[i], unsrc[src], cdname[op >> 1]);
            fprint_addr (of, val[1], 0, U_SC);
            break;

        case F_V_JU:                                    /* jump uncond */
            fprintf (of, "%s ", opcode[i]);
            fprint_addr (of, val[1], 0, U_SC);
            break;

        case F_V_RM:                                    /* reg mem */
            if (bop)
                fprintf (of, "%s %s,%s,",
                         opcode[i], unsrc[src], opname[bop]);
            else fprintf (of, "%s %s,", opcode[i], unsrc[src]);
            fprint_addr (of, val[1], op & MEM_MOD, dst);
            break;

        case F_V_ZM:                                    /* zero mem */
            if (bop)
                fprintf (of, "%s %s,", opcode[i], opname[bop]);
            else fprintf (of, "%s ", opcode[i]);
            fprint_addr (of, val[1], op & MEM_MOD, dst);
            break;

        case F_V_MR:                                    /* mem reg */
            fprintf (of, "%s ", opcode[i]);
            fprint_addr (of, val[1], op & MEM_MOD, dst);
            if (bop)
                fprintf (of, ",%s,%s", opname[bop], undst[dst]);
            else fprintf (of, ",%s", undst[dst]);
            break;

        case F_V_MS:                                    /* mem self */
            fprintf (of, "%s ", opcode[i]);
            fprint_addr (of, val[1], op & MEM_MOD, dst);
            if (bop)
                fprintf (of, ",%s", opname[bop]);
            break;
            }                                           /* end case */

        return (j >= F_2WD)? -1: SCPE_OK;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;
}

/* Field parse routines

        get_fnc         get function field
        get_ma          get memory address
        get_sd          get source or dest
        get_op          get optional bus operator
*/

CONST char *get_fnc (CONST char *cptr, t_value *val)
{
char gbuf[CBUFSIZE];
int32 i;
t_value d;
t_stat r;
uint32 inst = val[0];
uint32 fncv = 0, fncm = 0;

while (*cptr) {
    cptr = get_glyph (cptr, gbuf, 0);                   /* get glyph */
    d = get_uint (gbuf, 8, 017, &r);                    /* octal? */
    if (r == SCPE_OK) {                                 /* ok? */
        if (d & fncm)                                   /* already filled? */
            return NULL;
        fncv = fncv | d;                                /* save */
        fncm = fncm | d;                                /* field filled */
        }
    else {                                              /* symbol? */
        for (i = 0; fname[i] != NULL; i++) {            /* search table */
            if ((strcmp (gbuf, fname[i]) == 0) &&       /* match for inst? */
                ((inst & fop[i].imask) == fop[i].inst)) {
                if (fop[i].oper & fncm)                 /* already filled? */
                    return NULL;
                fncm = fncm | fop[i].omask;
                fncv = fncv | fop[i].oper;
                break;
                }
            }
        if (fname[i] == NULL)
            return NULL;
        }                                               /* end else */
    }                                                   /* end while */
val[0] = val[0] | (fncv << I_V_OP);                     /* store fnc */
return cptr;
}

CONST char *get_ma (CONST char *cptr, t_value *val, char term)
{
char gbuf[CBUFSIZE];
t_value d;
t_stat r;

cptr = get_glyph (cptr, gbuf, term);                    /* get glyph */
if (gbuf[0] == '#')                                     /* indexed? */
    d = get_uint (gbuf + 1, 8, AMASK, &r) | INDEX;      /* [0, 77777] */
else d = get_uint (gbuf, 8, DMASK, &r);                 /* [0,177777] */
if (r != SCPE_OK)
    return NULL;
val[1] = d;                                             /* second wd */
return cptr;
}

CONST char *get_sd (CONST char *cptr, t_value *val, char term, t_bool src)
{
char gbuf[CBUFSIZE];
int32 d;
t_stat r;

cptr = get_glyph (cptr, gbuf, term);                    /* get glyph */
for (d = 0; d < 64; d++) {                              /* symbol match? */
    if ((strcmp (gbuf, unsrc[d]) == 0) ||
        (strcmp (gbuf, undst[d]) == 0))
        break;
    }
if (d >= 64) {                                          /* no, [0,63]? */
    d = get_uint (gbuf, 8, 077, &r);
    if (r != SCPE_OK)
        return NULL;
    }
val[0] = val[0] | (d << (src? I_V_SRC: I_V_DST));       /* or to inst */
return cptr;
}

CONST char *get_op (CONST char *cptr, t_value *val, char term)
{
char gbuf[CBUFSIZE];
CONST char *tptr;
int32 i;

tptr = get_glyph (cptr, gbuf, term);                    /* get glyph */
for (i = 1; i < 4; i++) {                               /* symbol match? */
    if (strcmp (gbuf, opname[i]) == 0) {
        val[0] = val[0] | (i << (I_V_OP + 2));          /* or to inst */
        return tptr;
        }
    }
return cptr;                                            /* original ptr */
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *uptr   =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 i, j, k;
char gbuf[CBUFSIZE];

while (isspace (*cptr)) cptr++;                         /* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = (t_value) cptr[0] & 0177;
    return SCPE_OK;
    }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* char string? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = (((t_value) cptr[0] & 0177) << 8) | ((t_value) cptr[1] & 0177);
    return SCPE_OK;
    }

/* Instruction parse */

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
val[0] = opc_val[i] & DMASK;                            /* get value */
j = (opc_val[i] >> F_V_FL) & F_M_FL;                    /* get class */

switch (j) {                                            /* case on class */

    case F_V_FO:                                        /* func out */
        cptr = get_glyph (cptr, gbuf, ',');             /* fo # */
        if ((!cptr) || (!*cptr))                        /* none? */
            return SCPE_ARG;
        get_fnc (gbuf, val);                            /* fo # */
        cptr = get_sd (cptr, val, 0, FALSE);            /* dst */
        break;

    case F_V_FOI:                                       /* func out impl */
        cptr = get_fnc (cptr, val);                     /* fo # */
        break;

    case F_V_SF:                                        /* skip func */
        cptr = get_sd (cptr, val, ',', TRUE);           /* src */
        if (!cptr)
            return SCPE_ARG;

    case F_V_SFI:                                       /* skip func impl */
        cptr = get_fnc (cptr, val);                     /* fo # */
        break;

    case F_V_RR:                                        /* reg-reg */
        cptr = get_sd (cptr, val, ',', TRUE);           /* src */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_op (cptr, val, ',');                 /* op */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_sd (cptr, val, 0, FALSE);            /* dst */
        break;

    case F_V_ZR:                                        /* zero-reg */
        cptr = get_op (cptr, val, ',');                 /* op */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_sd (cptr, val, 0, FALSE);            /* dst */
        break;

    case F_V_RS:                                        /* reg self */
        cptr = get_sd (cptr, val, ',', TRUE);           /* src */
        if (!cptr)
            return SCPE_ARG;
        val[0] = val[0] | I_GETSRC (val[0]);            /* duplicate */
        cptr = get_op (cptr, val, 0);                   /* op */
        break;

    case F_V_JC:                                        /* jump cond */
        cptr = get_sd (cptr, val, ',', TRUE);           /* src */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_glyph (cptr, gbuf, ',');             /* cond */
        for (k = 0; k < 8; k++) {                       /* symbol? */
            if (strcmp (gbuf, cdname[k]) == 0)
                break;
            }
        if (k >= 8)
            return SCPE_ARG;
        val[0] = val[0] | (k << (I_V_OP + 1));          /* or to inst */

    case F_V_JU:                                        /* jump uncond */
        cptr = get_ma (cptr, val, 0);                   /* addr */
        break;

    case F_V_RM:                                        /* reg mem */
        cptr = get_sd (cptr, val, ',', TRUE);           /* src */
        if (!cptr)
            return SCPE_ARG;
    case F_V_ZM:                                        /* zero mem */
        cptr = get_op (cptr, val, ',');                 /* op */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_ma (cptr, val, 0);                   /* addr */
        break;

    case F_V_MR:                                        /* mem reg */
        cptr = get_ma (cptr, val, ',');                 /* addr */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_op (cptr, val, ',');                 /* op */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_sd (cptr, val, 0, FALSE);            /* dst */
        break;

    case F_V_MS:                                        /* mem self */
        cptr = get_ma (cptr, val, ',');                 /* addr */
        if (!cptr)
            return SCPE_ARG;
        cptr = get_op (cptr, val, 0);                   /* op */
        break;
    }                                                   /* end case */

if (!cptr || (*cptr != 0))                              /* junk at end? */
    return SCPE_ARG;
return (j >= F_2WD)? -1: SCPE_OK;
}
