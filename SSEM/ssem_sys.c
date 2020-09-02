/* ssem_sys.c: Manchester University SSEM (Small Scale Experimental Machine)
                         simulator interface

   Based on the SIMH package written by Robert M Supnik
 
   Copyright (c) 2006-2013 Gerardo Ospina

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   This is not a supported product, but the author welcomes bug reports and fixes.
   Mail to ngospina@gmail.com
*/

#include <ctype.h>
#include "ssem_defs.h"

extern uint32 S[];
extern uint32 C[];
extern int32  A[];

extern DEVICE cpu_dev;
extern REG cpu_reg[];

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader

   fprint_sym           memory examine
   parser_sym           memory deposit
*/

char sim_name[] = "SSEM";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = {
    &cpu_dev,
    NULL
    };

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Stop",
    "Breakpoint",
    };

/* SSEM binary dump */

t_stat ssem_dump (FILE *fi)
{
if (sim_fwrite(A, sizeof(int32), 1, fi) != 1 ||
    sim_fwrite(C, sizeof(uint32), 1, fi) != 1 ||
    sim_fwrite(S, sizeof(uint32), MEMSIZE, fi) != MEMSIZE) {
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* SSEM binary loader */

t_stat ssem_load_dmp (FILE *fi)
{
C[1] = 0;
if (sim_fread(A, sizeof(int32), 1, fi) != 1 ||
    sim_fread(C, sizeof(uint32), 1, fi) != 1 ||
    sim_fread(S, sizeof(uint32), MEMSIZE, fi) != MEMSIZE) {
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Loader

   Inputs:
        *fi     =       input stream
        *cptr   =       VM-specific arguments
        *fnam   =       file name
        flag    =       1 = dump, 0 = load
   Outputs:
        return  =       status code
*/

t_stat sim_load (FILE *fi, CONST char *cptr, CONST char *fnam, int flag)
{
size_t len;

len = strlen(fnam);
if (len <= 3 || strcmp(fnam + (len - 3), ".st") != 0) return SCPE_ARG;

if (flag == 1) return ssem_dump(fi);
return ssem_load_dmp(fi);
}

/* Utility routine - prints number in decimal */

t_stat ssem_fprint_decimal (FILE *of, uint32 inst)
{
if (inst & SMASK)
    fprintf (of, "%d [%u]", inst, inst);
else
    fprintf (of, "%d", inst);
return SCPE_OK;
}

/* Utility routine - prints number in backward binary */

t_stat ssem_fprint_binary_number (FILE *of, uint32 inst, uint8 nbits)
{
int i;
uint32 n;

n = inst;
for (i = 0; i < nbits; i++) {
    fprintf(of, "%d", n & 1 ? 1 : 0);
    n >>= 1;
    }
return SCPE_OK;
}

/* Utility routine - prints instruction in backward binary */

t_stat ssem_fprint_binary (FILE *of, uint32 inst, int flag)
{
uint32 op, ea;

if (!flag) return ssem_fprint_binary_number(of, inst, 32);

op = I_GETOP (inst);
if (op != OP_TEST && op != OP_STOP)  {
    ea = I_GETEA (inst);
    ssem_fprint_binary_number(of, ea, 5);
    fprintf (of, " ");
    }
ssem_fprint_binary_number(of, op, 3);

return SCPE_OK;
}

/* Utility routine 

   prints instruction in the mnemomic style used in the 1998
   competition reference manual:
   "The Manchester University Small Scale Experimental Machine
    Programmer's Reference manual"
    http://www.computer50.org/mark1/prog98/ssemref.html
*/

t_stat ssem_fprint_competition_mnemonic (FILE *of, uint32 inst)
{
uint32 op, ea;

op = I_GETOP (inst);
switch (op) {
    case OP_JUMP_INDIRECT:            /* JMP  */
        ea = I_GETEA (inst);
        fprintf (of, "JMP %d", ea);
        break;

    case OP_JUMP_INDIRECT_RELATIVE:    /* JRP  */
        ea = I_GETEA (inst);
        fprintf (of, "JRP %d", ea);
        break;

    case OP_LOAD_NEGATED:            /* LDN  */
        ea = I_GETEA (inst);
        fprintf (of, "LDN %d", ea);
        break;

    case OP_STORE:                    /* STO  */
        ea = I_GETEA (inst);
        fprintf (of, "STO %d", ea);
        break;

    case OP_SUBSTRACT:                /* SUB  */
        ea = I_GETEA (inst);
        fprintf (of, "SUB %d", ea);
        break;

    case OP_UNDOCUMENTED:            /* invalid instruction */
        return SCPE_ARG;

    case OP_TEST:                    /* CMP  */
        fprintf (of, "CMP");
        break;

    case OP_STOP:                    /* STOP */
        fprintf (of, "STOP");
        break;                        /* end switch */
    }

return SCPE_OK;
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

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
uint32 inst;

if (sw & SWMASK ('H')) return SCPE_ARG;    /* hexadecimal? */

inst = val[0];

if (sw & SWMASK ('D'))                    /* decimal? */
    return ssem_fprint_decimal(of, inst);

if (sw & SWMASK ('M')) {                /* mnemomic? */
    return ssem_fprint_competition_mnemonic(of, inst);
    }

return ssem_fprint_binary(of, inst, sw & SWMASK ('I') || sw & SWMASK ('M'));
}

static const char *opcode[] = {
    "JMP", "JRP", "LDN", 
    "STO", "SUB", "",
    "CMP", "STOP",
    NULL
};

/* Utility function - parses decimal number. */

t_stat parse_sym_d (const char *cptr, t_value *val)
{
const char *start;
int n;

start = cptr;
if (*cptr == '-') cptr++;    /* skip sign */
n = 0;
while (*cptr >= '0' && *cptr <= '9') {
    n = (n * 10) + (*cptr - '0');
    cptr++;
    }
if (*start == '-') n = -n;

if (*cptr) return SCPE_ARG;    /* junk at end? */

*val = n;
return SCPE_OK;
}

/* Utility function

   Parses mnemonic instruction. 

   It accepts the mnemonics used in the 1998 competition reference
   manual:
   "The Manchester University Small Scale Experimental Machine
    Programmer's Reference manual" 
    http://www.computer50.org/mark1/prog98/ssemref.html
*/

t_stat parse_sym_m (const char *cptr, t_value *val)
{
uint32 n,a;
char gbuf[CBUFSIZE];

cptr = get_glyph(cptr, gbuf, 0);

for (n = 0; opcode[n] != NULL && strcmp(opcode[n], gbuf) != 0; n++) ;
if (opcode[n] == NULL) return SCPE_ARG;                /* invalid mnemonic? */

if (!(*cptr) && n > OP_UNDOCUMENTED && n <= OP_STOP) {
    *val = n << I_V_OP; return SCPE_OK;
    }

while (isspace (*cptr)) cptr++;                        /* absorb spaces */

if (*cptr < '0' || *cptr > '9')  return SCPE_ARG;    /* address expected */

a = 0;
while (*cptr >= '0' && *cptr <= '9') {
    a = (a * 10) + (*cptr - '0');
    cptr++;
    }

if (a >= MEMSIZE) return SCPE_ARG;                    /* invalid address? */

if (*cptr) return SCPE_ARG;                            /* junk at end? */

*val = (n << I_V_OP) + a;
return SCPE_OK;
}

/* Utility function - parses binary backward number. */

t_stat parse_sym_b (const char *cptr, t_value *val)
{
int count;
t_value n;

count = 0;
n = 0;
while (*cptr == '0' || *cptr == '1') {
    n = n + ((*cptr - '0') << count);
    count++;
    cptr++;
    }

if (*cptr) return SCPE_ARG;    /* junk at end? */

*val = n;
return SCPE_OK;
}

/* Utility function - parses binary backward instruccion. */

t_stat parse_sym_i (const char *cptr, t_value *val)
{
int count;
t_value a,n;

count = 0;
n = 0;
while (*cptr == '0' || *cptr == '1') {
    n = n + ((*cptr - '0') << count);
    count++;
    cptr++;
    }

if (!(*cptr) && n > OP_UNDOCUMENTED && n <= OP_STOP) {
    *val = n << I_V_OP; return SCPE_OK;
    }

a = n;
if (a >= MEMSIZE) return SCPE_ARG;                    /* invalid addresss */

while (isspace (*cptr)) cptr++;                        /* absorb spaces */

if (*cptr != '0' && *cptr != '1') return SCPE_ARG;    /* instruction expected */

count = 0;
n = 0;
while (*cptr == '0' || *cptr == '1') {
    n = n + ((*cptr - '0') << count);
    count++;
    cptr++;
    }
if (n >= OP_UNDOCUMENTED) return SCPE_ARG;            /* invalid instruction? */

if (*cptr) return SCPE_ARG;                            /* junk at end? */

*val = (n << I_V_OP) + a;
return SCPE_OK;
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

if (sw & SWMASK ('H')) return SCPE_ARG;    /* hexadecimal? */

while (isspace (*cptr)) cptr++;            /* absorb spaces */

if (sw & SWMASK ('D')) {                 /* decimal? */
    return parse_sym_d (cptr, val);
    }

if (sw & SWMASK ('I')) {                 /* backward binary instruction? */
    return parse_sym_i (cptr, val);
    }

if (sw & SWMASK ('M')) {                 /* mnemonic? */
    return parse_sym_m (cptr, val);
    }

return parse_sym_b(cptr, val);            /* backward binary number */
}
