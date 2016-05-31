/* h316_sys.c: Honeywell 316/516 simulator interface

   Copyright (c) 1999-2015, Robert M Supnik

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

   15-Sep-13    RMS     Added device name support for IO instructions
                        Fixed handling of OTK
   21-May-13    RLA     Add IMP/TIP devices
   01-Dec-04    RMS     Fixed fprint_opr calling sequence
   24-Oct-03    RMS     Added DMA/DMC support
   17-Sep-01    RMS     Removed multiconsole support
*/

#include "h316_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE tty_dev;
extern DEVICE lpt_dev;
extern DEVICE clk_dev;
extern DEVICE dp_dev;
extern DEVICE fhd_dev;
extern DEVICE mt_dev;
#ifdef VM_IMPTIP
extern DEVICE rtc_dev, wdt_dev, imp_dev;
extern DEVICE mi1_dev, mi2_dev, mi3_dev, mi4_dev, mi5_dev;
extern DEVICE hi1_dev, hi2_dev, hi3_dev, hi4_dev;
#endif
extern REG cpu_reg[];
extern uint16 M[];

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "H316";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &ptr_dev,
    &ptp_dev,
    &lpt_dev,
    &tty_dev,
    &mt_dev,
    &clk_dev,
    &fhd_dev,
    &dp_dev,
#ifdef VM_IMPTIP
    &wdt_dev,
    &rtc_dev,
    &imp_dev,
    &mi1_dev, &mi2_dev, &mi3_dev, &mi4_dev, &mi5_dev,
    &hi1_dev, &hi2_dev, &hi3_dev, &hi4_dev,
#endif
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "Unimplemented instruction",
    "Unimplemented I/O device",
    "HALT instruction",
    "Breakpoint",
    "Indirect address loop",
    "DMA error",
    "MT write protected",
    "DP write overrun, track destroyed",
    "DP track format invalid"
    };

/* Binary loader

   Tbs.
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
return SCPE_FMT;
}

/* Symbol tables */

#define I_V_FL          16                              /* flag start */
#define I_M_FL          07                              /* flag mask */
#define I_V_NPN         0                               /* no operand */
#define I_V_MRF         1                               /* mem ref */
#define I_V_MRX         2                               /* mem ref, no idx */
#define I_V_IOT         3                               /* I/O */
#define I_V_SHF         4                               /* shift */
#define I_V_SK0         5                               /* skip 0 */
#define I_V_SK1         6                               /* skip 1 */
#define I_NPN           (I_V_NPN << I_V_FL)
#define I_MRF           (I_V_MRF << I_V_FL)
#define I_MRX           (I_V_MRX << I_V_FL)
#define I_IOT           (I_V_IOT << I_V_FL)
#define I_SHF           (I_V_SHF << I_V_FL)
#define I_SK0           (I_V_SK0 << I_V_FL)
#define I_SK1           (I_V_SK1 << I_V_FL)

static const int32 masks[] = {
 0177777, 0136000, 0176000, 0176000,
 0177700, 0177000, 0177000
 };

static const char *opcode[] = {
 "HLT", "SGL", "DBL",
 "DXA", "EXA", "RMP",
 "SCA", "INK", "NRM",
 "IAB", "ENB", "INH", "ERM",
 "CHS", "CRA", "SSP",
 "RCB", "CSA", "CMA",
 "TCA", "SSM", "SCB",
 "CAR", "CAL", "ICL",
 "AOA", "ACA", "ICR", "ICA",
 "NOP", "SKP", "SSR", "SSS",
 "OTK",         "JMP", "JMP*",
 "LDA", "LDA*", "ANA", "ANA*",
 "STA", "STA*", "ERA", "ERA*",
 "ADD", "ADD*", "SUB", "SUB*",
 "JST", "JST*", "CAS", "CAS*",
 "IRS", "IRS*", "IMA", "IMA*",
 "MPY", "MPY*", "DIV", "DIV*",
 "STX", "STX*", "LDX", "LDX*",
 "LRL", "LRS", "LRR",
 "LGR", "ARS", "ARR",
 "LLL", "LLS", "LLR",
 "LGL", "ALS", "ALR",
 "OCP", "SKS", "INA", "OTA",
 "SMK",
 "SPL", "SPN", "SLZ",                                   /* encode only */
 "SZE", "SR1", "SR2",
 "SR3", "SR4", "SRC",
 "SMI", "SPS", "SLN",
 "SNZ", "SS1", "SS2",
 "SS3", "SS4", "SSC",
 NULL, NULL,                                            /* decode only */
 NULL
 };

static const char *ioname[DEV_MAX] = {
 NULL, "PTR", "PTP", "LPT", "TTY", "CDR", NULL, NULL,
 "MT", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
 "CLK", NULL, "FHD", NULL, "DMA", "DP", NULL, NULL,
 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
 NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
 };
 
static const int32 opc_val[] = {
 0000000+I_NPN, 0000005+I_NPN, 0000007+I_NPN,
 0000011+I_NPN, 0000013+I_NPN, 0000021+I_NPN,
 0000041+I_NPN, 0000043+I_NPN, 0000101+I_NPN,
 0000201+I_NPN, 0000401+I_NPN, 0001001+I_NPN, 0001401+I_NPN,
 0140024+I_NPN, 0140040+I_NPN, 0140100+I_NPN,
 0140200+I_NPN, 0140320+I_NPN, 0140401+I_NPN,
 0140407+I_NPN, 0140500+I_NPN, 0140600+I_NPN,
 0141044+I_NPN, 0141050+I_NPN, 0141140+I_NPN,
 0141206+I_NPN, 0141216+I_NPN, 0141240+I_NPN, 0141340+I_NPN,
 0101000+I_NPN, 0100000+I_NPN, 0100036+I_NPN, 0101036+I_NPN,
 0171020+I_NPN,                0002000+I_MRF, 0102000+I_MRF,
 0004000+I_MRF, 0104000+I_MRF, 0006000+I_MRF, 0106000+I_MRF,
 0010000+I_MRF, 0110000+I_MRF, 0012000+I_MRF, 0112000+I_MRF,
 0014000+I_MRF, 0114000+I_MRF, 0016000+I_MRF, 0116000+I_MRF,
 0020000+I_MRF, 0120000+I_MRF, 0022000+I_MRF, 0122000+I_MRF,
 0024000+I_MRF, 0124000+I_MRF, 0026000+I_MRF, 0126000+I_MRF,
 0034000+I_MRF, 0134000+I_MRF, 0036000+I_MRF, 0136000+I_MRF,
 0032000+I_MRX, 0132000+I_MRX, 0072000+I_MRX, 0172000+I_MRX,
 0040000+I_SHF, 0040100+I_SHF, 0040200+I_SHF,
 0040400+I_SHF, 0040500+I_SHF, 0040600+I_SHF,
 0041000+I_SHF, 0041100+I_SHF, 0041200+I_SHF,
 0041400+I_SHF, 0041500+I_SHF, 0041600+I_SHF,
 0030000+I_IOT, 0070000+I_IOT, 0130000+I_IOT, 0170000+I_IOT,
 0170000+I_IOT,
 0100400+I_SK0, 0100200+I_SK0, 0100100+I_SK0,           /* encode only */
 0100040+I_SK0, 0100020+I_SK0, 0100010+I_SK0,
 0100004+I_SK0, 0100002+I_SK0, 0100001+I_SK0,
 0101400+I_SK1, 0101200+I_SK1, 0101100+I_SK1,
 0101040+I_SK1, 0101020+I_SK1, 0101010+I_SK1,
 0101004+I_SK1, 0101002+I_SK1, 0101001+I_SK1,
 0100000+I_SK0, 0101000+I_SK1,                          /* decode only */
 -1
 };

/* Operate decode

   Inputs:
        *of     =       output stream
        inst    =       mask bits
        class   =       instruction class code
        sp      =       space needed?
   Outputs:
        status  =       space needed
*/

/* Use scp.c provided fprintf function */
#define fprintf Fprintf
#define fputs(_s,f) Fprintf(f,"%s",_s)
#define fputc(_c,f) Fprintf(f,"%c",_c)

void fprint_opr (FILE *of, int32 inst, int32 Class)
{
int32 i, j, sp;

for (i = sp = 0; opc_val[i] >= 0; i++) {                /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((j == Class) && (opc_val[i] & inst)) {          /* same class? */
        inst = inst & ~opc_val[i];                      /* mask bit set? */
        fprintf (of, (sp? " %s": "%s"), opcode[i]);
        sp = 1;
        }
    }
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
int32 cflag, i, j, inst, fnc, disp;

cflag = (uptr == NULL) || (uptr == &cpu_unit);
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

for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((opc_val[i] & DMASK) == (inst & masks[j])) {    /* match? */

        switch (j) {                                    /* case on class */

        case I_V_NPN:                                   /* no operands */
            fprintf (of, "%s", opcode[i]);              /* opcode */
            break;

        case I_V_MRF: case I_V_MRX:                     /* mem ref */
            disp = inst & DISP;                         /* displacement */
            fprintf (of, "%s ", opcode[i]);             /* opcode */
            if (inst & SC) {                            /* current sector? */
                if (cflag)
                    fprintf (of, "%-o", (addr & PAGENO) | disp);
                else fprintf (of, "C %-o", disp);
                }
            else fprintf (of, "%-o", disp);             /* sector zero */
            if ((j == I_V_MRF) && (inst & IDX))
                fprintf (of, ",1");
            break;

        case I_V_IOT:                                   /* I/O */
            fnc = I_GETFNC (inst);                      /* get func */
            disp = inst & DEVMASK;                      /* get dev */
            if (ioname[disp] != NULL)
                fprintf (of, "%s %o,%s", opcode[i], fnc, ioname[disp]);
            else fprintf (of, "%s %o,%o", opcode[i], fnc, disp);
            break;

        case I_V_SHF:                                   /* shift */
            disp = -inst & SHFMASK;                     /* shift count */
            fprintf (of, "%s %o", opcode[i], disp);
            break;

        case I_V_SK0: case I_V_SK1:                     /* skips */
            fprint_opr (of, inst & 0777, j);            /* print skips */       
            break;
            }                                           /* end case */

        return SCPE_OK;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;
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
int32 cflag, d, i, j, k;
t_stat r;
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
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
    val[0] = (((t_value) cptr[0] & 0177) << 8) |
              ((t_value) cptr[1] & 0177);
    return SCPE_OK;
    }

/* Instruction parse */

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
val[0] = opc_val[i] & DMASK;                            /* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;                    /* get class */

switch (j) {                                            /* case on class */

    case I_V_NPN:                                       /* no operand */
        break;

    case I_V_IOT:                                       /* IOT */
        cptr = get_glyph (cptr, gbuf, ',');             /* get field */
        if (*cptr == 0) {                               /* single field? */
            d = get_uint (gbuf, 8, 01777, &r);          /* pulse+dev */
            if (r != SCPE_OK)
                return SCPE_ARG;
            val[0] = val[0] | d;
            }
        else {                                          /* multiple fields */
            d = get_uint (gbuf, 8, 017, &r);            /* get pulse */
            if (r != SCPE_OK)
                return SCPE_ARG;
            cptr = get_glyph (cptr, gbuf, 0);           /* get dev name */
            for (k = 0; k < DEV_MAX; k++) {             /* sch for name */
                if ((ioname[k] != NULL) && (strcmp (gbuf, ioname[k]) == 0))
                    break;                              /* match? */
                }
            if (k >= DEV_MAX) {                         /* no match */
                k = get_uint (gbuf, 8, DEV_MAX - 1, &r);/* integer */
                if (r != SCPE_OK)
                    return SCPE_ARG;
                }
            val[0] = val[0] | (d << I_V_FNC) | k;
            }
        break;

    case I_V_SHF:                                       /* shift */
        cptr = get_glyph (cptr, gbuf, 0);               /* get shift count */
        d = get_uint (gbuf, 8, SHFMASK, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (-d & SHFMASK);               /* store 2's comp */
        break;

    case I_V_MRF: case I_V_MRX:                         /* mem ref */
        cptr = get_glyph (cptr, gbuf, ',');             /* get next field */
        if ((k = (strcmp (gbuf, "C") == 0))) {          /* C specified? */
            val[0] = val[0] | SC;
            cptr = get_glyph (cptr, gbuf, 0);
            }
        else if ((k = (strcmp (gbuf, "Z") == 0))) {     /* Z specified? */
            cptr = get_glyph (cptr, gbuf, ',');
            }
        d = get_uint (gbuf, 8, X_AMASK, &r);            /* construe as addr */
        if (r != SCPE_OK)
            return SCPE_ARG;
        if (d <= DISP)                                  /* fits? */
            val[0] = val[0] | d;
        else if (cflag && !k && (((addr ^ d) & PAGENO) == 0))
            val[0] = val[0] | (d & DISP) | SC;
        else return SCPE_ARG;
        if ((j == I_V_MRX) || (*cptr == 0))             /* indexed? */
            break;
        cptr = get_glyph (cptr, gbuf, 0);
        d = get_uint (gbuf, 8, 1, &r);                  /* get tag */
        if (r != SCPE_OK)
            return SCPE_ARG;
        if (d)                                          /* or in index */
            val[0] = val[0] | IDX;
        break;

    case I_V_SK0: case I_V_SK1:                         /* skips */
        for (cptr = get_glyph (cptr, gbuf, 0); gbuf[0] != 0;
             cptr = get_glyph (cptr, gbuf, 0)) {
            for (i = 0; (opcode[i] != NULL) &&
                (strcmp (opcode[i], gbuf) != 0) ; i++) ;
            k = opc_val[i] & DMASK;
            if ((opcode[i] == NULL) || (((k ^ val[0]) & 0177000) != 0))
                return SCPE_ARG;
            val[0] = val[0] | k;
            }
        break;
        }                                               /* end case */

if (*cptr != 0)                                         /* junk at end? */
    return SCPE_ARG;
return SCPE_OK;
}
