/*************************************************************************
 *                                                                       *
 * $Id: tx0_sys.c 2061 2009-02-24 07:05:58Z hharte $                     *
 *                                                                       *
 * Copyright (c) 2009-2012 Howard M. Harte.                              *
 * Based on pdp1_sys.c, Copyright (c) 1993-2007, Robert M. Supnik        *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * of Howard M. Harte.                                                   *
 *                                                                       *
 * Module Description:                                                   *
 *     TX-0 simulator interface                                          *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

#include "tx0_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern DEVICE petr_dev;
extern DEVICE tto_dev;
extern DEVICE tti_dev;
extern DEVICE ptp_dev;
#ifdef USE_DISPLAY
extern DEVICE dpy_dev;
#endif /* USE_DISPLAY */

#ifdef USE_FPC
extern DEVICE fpc_dev;
#endif /* USE_FPC */
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern int32 M[];
extern int32 PC;
extern int32 ascii_to_flexo[], flexo_to_ascii[];
extern int32 sc_map[];

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "TX-0";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &petr_dev,
    &tti_dev,
    &tto_dev,
    &ptp_dev,
#ifdef USE_DISPLAY
    &dpy_dev,
#endif/* USE_DISPLAY */
#ifdef USE_FPC
    &fpc_dev,
#endif /* USE_FPC */
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "Undefined instruction",
    "HALT instruction",
    "Breakpoint",
    "Nested XCT's",
    "Nested indirect addresses",
    "Infinite I/O wait state",
    "DECtape off reel"
     };

int32 tx0_getw (FILE *inf)
{
int32 i, tmp, word;

word = 0;
for (i = 0; i < 3;) {
    if ((tmp = getc (inf)) == EOF) return -1;
    if (tmp & 0200) {
        word = (word << 6) | (tmp & 077);
        i++;
        }
    }
return word;
}

/* Symbol tables */
typedef struct {
    int32 opr;
    char *mnemonic;
    char *desc;
} OPMAP;

typedef struct {
    char *mnemonic;
    char *desc;
} INSTMAP;

const INSTMAP instmap[] = {
/* Store Class */
    { "sto", "Store AC" },
    { "stx", "Store AC, Indexed" },
    { "sxa", "Store XR in Address" },
    { "ado", "Add One" },
    { "slr", "Store LR" },
    { "slx", "Store LR, Indexed" },
    { "stz", "Store Zero" },
    { "[!sto-nop]", "NOP" },

/* Add Class */
    { "add", "Add" },
    { "adx", "Add, Indexed" },
    { "ldx", "Load XR" },
    { "aux", "Augment XR" },
    { "llr", "Load LR" },
    { "llx", "Load LR, Indexed" },
    { "lda", "Load AC" },
    { "lax", "Load AC, Indexed" },

/* Transfer Class */
    { "trn", "Transfer Negative" },
    { "trz", "Transfer +/- Zero" },
    { "tsx", "Transfer and set Index" },
    { "tix", "Transfer and Index" },
    { "tra", "Transfer" },
    { "trx", "Transfer Indexed" },
    { "tlv", "Transfer on external Level" },
    { "[!tra-nop]", "NOP" }
};

const OPMAP opmap [] = {
    { 0600000, "opr", "No operation" },
    { 0600001, "xro", "Clear XR to +0" },
    { 0600003, "lxr", "Place LR in XR" },
    { 0600012, "cry", "Carry the contents of AC according to bits of LR" },
    { 0600022, "lpd", "Logical exclusive or of AC is placed in AC (partial add)" },
    { 0600032, "lad", "Add LR to AC" },
    { 0600040, "com", "Compliment the AC" },
    { 0600072, "lcd", "Contents of LR minus those of AC are placed in AC" },
    { 0600130, "xad", "Add index register to accumulator" },
    { 0600170, "xcd", "Contents of XR minus those of AC are placed in AC" },
    { 0600200, "lro", "Clear LR to +0" },
    { 0600300, "xlr", "Place XR in LR" },
    { 0600303, "ixl", "Interchange XR and LR" },
    { 0600400, "shr", "Shift accumulator right one place, bit 0 remains unchanged" },
    { 0600600, "cyr", "Cycle AC right one place" },
    { 0603000, "pen", "Contents of light pen and light cannon flip-flops replace contents of AC bits 0 and 1. The flip-flops are cleared." },
    { 0604000, "bsr", "Backspace tape unit by one record" },
    { 0604004, "rtb", "Read tape binary (odd parity)" },
    { 0604004, "rds", "Select tape unit for reading a record" },
    { 0604010, "rew", "Rewind tape unit" },
    { 0604014, "wtb", "Write tape binary (odd parity)" },
    { 0604014, "wrs", "Select tape unit for writing a record" },
    { 0604024, "rtd", "Read tape decimal (even parity)" },
    { 0604034, "wtd", "Write tape decimal (even parity)" },
    { 0607000, "cpf", "The program flag is cleared" },
    { 0620000, "cpy", "Transmit information between the live register and selected input-output unit" },
    { 0622000, "dis", "Display point on CRT corresponding to contents of AC" },
    { 0624000, "prt", "Print one on-line flexo character from bits 2, 5, etc." },
    { 0624600, "pnt", "PRT, then cycle AC right once to set up another character" },
    { 0625000, "typ", "Read one character from on-line flexowriter into LR bits 12-17" },
    { 0626600, "p6h", "Punch one line of paper tape; 6 holes from bits 2, 5, etc. of AC then cycle right once." },
    { 0627600, "p7h", "Same as p6h, but punch 7th hole" },
    { 0630000, "hlt", "Stops computer" },
    { 0631000, "cll", "Clear left half of AC to zero" },
    { 0632000, "clr", "Clear right half of AC" },
    { 0632022, "---", "CLR+PAD+LMB" },
    { 0640001, "axr", "Place AC contents in XR" },
    { 0640021, "axo", "AXR, then set AC to +0" },
    { 0640030, "cyl", "Cycle AC left one place" },
    { 0640031, "alx", "AXR, then cycle AC left once" },
    { 0640040, "amz", "Add minus zero to AC" },
    { 0640061, "axc", "AXR, then set AC to -0" },
    { 0640200, "alr", "Place accumulator contents in live register" },
    { 0640201, "---", "ALR+MBX, Place accumulator contents in live register, Transfer MBR to XR." },
    { 0640203, "rax", "Place LR in XR, then place AC in LR" },
    { 0640205, "orl", "Logical or of AC and LR is placed in LR" },
    { 0640207, "anl", "Logical and of AC and LR is placed in LR" },
    { 0640220, "alo", "ALR, then set AC to +0" },
    { 0640230, "all", "ALR, then cycle left once" },
    { 0640231, "---", "AMB+MBL+PAD+CRY+MBX" },
    { 0640232, "iad", "Interchange and add AC contents are placed in the LR and the previous contents of the LR ar added to AC" },
    { 0640260, "alc", "ALR, then set AC to -0" },
    { 0640601, "arx", "AXR, then cycle AC right once" },
    { 0647000, "spf", "Place AC in program flag register" },
    { 0662020, "dso", "DIS, then clear AC" },
    { 0664020, "pno", "PRT, then clear AC" },
    { 0664060, "pnc", "PRT, then clear AC to -0" },
    { 0666020, "p6o", "p6h then clear AC" },
    { 0667020, "p7o", "p7h then clear AC" },
    { 0700000, "cla", "Clear entire AC to +0" },
    { 0700001, "cax", "Clear AC and XR to +0" },
    { 0700012, "lal", "Place LR in AC cycled left once" },
    { 0700022, "lac", "Place LR in AC" },
    { 0700040, "clc", "Clear and complement: set AC to -0" },
    { 0700062, "lcc", "Place complement of LR in AC" },
    { 0700072, "laz", "Add LR to minus zero in AC" },
    { 0700110, "xal", "XAC, then cycle AC left once" },
    { 0700120, "xac", "Place index register in accumulator" },
    { 0700160, "xcc", "Place complement of XR in accumulator" },
    { 0700200, "cal", "Clear AC and LR to +0" },
    { 0700322, "rxe", "Place LR in AC, then place XR in LR" },
    { 0700622, "lar", "Place LR in AC cycled right once" },
    { 0701000, "tac", "Contents of test accumulator are placed in AC" },
    { 0702020, "tbr", "Contents of test buffer register are placed in AC" },
    { 0703000, "---", "Clear AC and read light pen" },
    { 0706020, "rpf", "The program flag register is placed in AC" },
    { 0721000, "rlc", "Read one line paper tape into AC bits 0, 3, etc." },
    { 0721600, "rlr", "rlc, then cycle AC right once" },
    { 0723000, "r3c", "Read three lines of paper tape" },
    { 0723032, "---", "R3C+LMB+PAD+CRY" },
    { 0726000, "p6a", "Clear AC and punch a line of blank tape" },
    { 0740025, "ora", "Logical or of AC and LR is placed in AC" },
    { 0740027, "ana", "Logical and of AC and LR is placed in AC" },
    { 0740207, "anc", "ANL, then clear AC" },
    { 0740205, "oro", "ORL, then clear AC" },
    { 0740222, "ial", "Interchange AC and LR" },
    { 0763232, "---", "AMB+CLA+R3L+MBL+LMB+PAD+CRY" },
    { 0766020, "p6b", "Punch a line of blank tape, but save AC" },
    { 0000000, NULL, NULL }
};

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       status code
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)
#define SIXTOASC(x) flexo_to_ascii[x]
#define ASCTOSIX(x) (ascii_to_flexo[x] & 077)

extern int32 cpu_get_mode (void);
extern t_stat fprint_sym_orig (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw);


t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 i, inst, op;

if(!cpu_get_mode()) {
    return fprint_sym_orig (of, addr, val, uptr, sw);
}


inst = val[0];
if (sw & SWMASK ('A')) {                                /* ASCII? */
    if (inst > 0377) return SCPE_ARG;
    fprintf (of, FMTASC (inst & 0177));
    return SCPE_OK;
    }
if (sw & SWMASK ('F')) {
    fputc (flexo_to_ascii[inst & 077], of);
    return SCPE_OK;
    }
if (sw & SWMASK ('C')) {                                /* character? */
    fprintf (of, "%c", SIXTOASC ((inst >> 12) & 077));
    fprintf (of, "%c", SIXTOASC ((inst >> 6) & 077));
    fprintf (of, "%c", SIXTOASC (inst & 077));
    return SCPE_OK;
    }
if (!(sw & SWMASK ('M'))) return SCPE_ARG;

/* Instruction decode */

    op = (inst >> 13) & 037;

    if ((op & 030) != 030) /* sto, add, trn (not an opr) */
    {
        fprintf (of, "%s %05o (%s)", instmap[op].mnemonic, inst & 017777, instmap[op].desc);
    } else { /* opr */
        for(i=0;opmap[i].opr != 0;i++) {
            if(inst == opmap[i].opr) {
                fprintf (of, "opr %s (%s)", opmap[i].mnemonic, opmap[i].desc);
            }
        }
    }
return SCPE_OK;
}

/* Get 18b signed number

   Inputs:
        *cptr   =       pointer to input string
        *sign   =       pointer to sign
        *status =       pointer to error status
   Outputs:
        val     =       output value
*/

t_value get_sint (char *cptr, int32 *sign, t_stat *status)
{
*sign = 1;
if (*cptr == '+') {
    *sign = 0;
    cptr++;
    }
else if (*cptr == '-') {
    *sign = -1;
    cptr++;
    }
return get_uint (cptr, 8, DMASK, status);
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        uptr    =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/
t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
#if 0
    int32 cflag, d, i, j, k, sign;
t_stat r;
static int32 sc_enc[10] = { 0, 01, 03, 07, 017, 037, 077, 0177, 0377, 0777 };
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;
for (i = 1; (i < 3) && (cptr[i] != 0); i++)
    if (cptr[i] == 0) for (j = i + 1; j <= 3; j++) cptr[j] = 0;
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (t_value) cptr[0];
    return SCPE_OK;
    }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* sixbit string? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = ((ASCTOSIX (cptr[0]) & 077) << 12) |
             ((ASCTOSIX (cptr[1]) & 077) << 6) |
              (ASCTOSIX (cptr[2]) & 077);
    return SCPE_OK;
    }

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL) return SCPE_ARG;
val[0] = opc_val[i] & DMASK;                            /* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;                    /* get class */

switch (j) {                                            /* case on class */

    case I_V_LAW:                                       /* LAW */
        cflag = 0;                                      /* fall through */
    case I_V_MRF: case I_V_MRI:                         /* mem ref */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next field */
        if ((j != I_V_MRI) && strcmp (gbuf, "I") == 0) { /* indirect? */
            val[0] = val[0] | IA;
            cptr = get_glyph (cptr, gbuf, 0);
            }
        d = get_uint (gbuf, 8, AMASK, &r);
        if (r != SCPE_OK) return SCPE_ARG;
        if (d <= DAMASK) val[0] = val[0] | d;
        else if (cflag && (((addr ^ d) & EPCMASK) == 0))
                val[0] = val[0] | (d & DAMASK);
        else return SCPE_ARG;
        break;

    case I_V_SHF:                                       /* shift */
        cptr = get_glyph (cptr, gbuf, 0);
        d = get_uint (gbuf, 10, 9, &r);
        if (r != SCPE_OK) return SCPE_ARG;
        val[0] = val[0] | sc_enc[d];
        break;

    case I_V_NPN: case I_V_IOT:
    case I_V_OPR: case I_V_SKP: case I_V_SPC:
        for (cptr = get_glyph (cptr, gbuf, 0); gbuf[0] != 0;
            cptr = get_glyph (cptr, gbuf, 0)) {
            for (i = 0; (opcode[i] != NULL) &&
                        (strcmp (opcode[i], gbuf) != 0); i++) ;
            if (opcode[i] != NULL) {
                k = opc_val[i] & DMASK;
                if ((k != IA) && (((k ^ val[0]) & 0760000) != 0))
                    return SCPE_ARG;
                val[0] = val[0] | k;
                }
            else {
                d = get_sint (gbuf, &sign, &r);
                if (r != SCPE_OK) return SCPE_ARG;
                if (sign == 0) val[0] = val[0] + d;  
                else if (sign < 0) val[0] = val[0] - d;
                else val[0] = val[0] | d;
                }
            }
        break;
        }                                               /* end case */
if (*cptr != 0) return SCPE_ARG;                        /* junk at end? */
#endif
return SCPE_ARG;
}
