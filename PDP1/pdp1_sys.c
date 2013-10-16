/* pdp1_sys.c: PDP-1 simulator interface

   Copyright (c) 1993-2008, Robert M. Supnik

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

   03-Jan-07    RMS     Fixed bugs in block loader, char input
   21-Dec-06    RMS     Added 16-channel sequence break support, PDP-1D support
   06-Apr-04    RMS     Fixed bug in binary loader (found by Mark Crispin)
   08-Feb-04    PLB     Merged display support
   08-Dec-03    RMS     Added parallel drum support, drum mnemonics
   18-Oct-03    RMS     Added DECtape off reel message
   01-Sep-03    RMS     Added support for loading in multiple fields
   22-Jul-03    RMS     Updated for "hardware" RIM loader
   05-Dec-02    RMS     Added drum support
   21-Nov-02    RMS     Changed typewriter to half duplex
   20-Aug-02    RMS     Added DECtape support
   17-Sep-01    RMS     Removed multiconsole support
   13-Jul-01    RMS     Fixed RIM loader format
   27-May-01    RMS     Added multiconsole support
   14-Mar-01    RMS     Revised load/dump interface (again)
   30-Oct-00    RMS     Added support for examine to file
   27-Oct-98    RMS     V2.4 load interface
   20-Oct-97    RMS     Fixed endian-dependence in RIM loader
                        (found by Michael Somos)
*/

#include "pdp1_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern DEVICE clk_dev;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE tti_dev;
extern DEVICE tto_dev;
extern DEVICE lpt_dev;
extern DEVICE dt_dev;
extern DEVICE drm_dev;
extern DEVICE drp_dev;
extern DEVICE dcs_dev, dcsl_dev;
#if defined(USE_DISPLAY)
extern DEVICE dpy_dev;
#endif
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern int32 M[];
extern int32 PC;
extern int32 ascii_to_fiodec[], fiodec_to_ascii[];
extern int32 sc_map[];

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "PDP-1";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &clk_dev,
    &ptr_dev,
    &ptp_dev,
    &tti_dev,
    &tto_dev,
    &lpt_dev,
    &dt_dev,
    &drm_dev,
    &drp_dev,
    &dcs_dev,
    &dcsl_dev,
#if defined(USE_DISPLAY)
    &dpy_dev,
#endif
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

/* Binary loader - supports both RIM format and Macro block format */

int32 pdp1_getw (FILE *inf)
{
int32 i, tmp, word;

word = 0;
for (i = 0; i < 3;) {
    if ((tmp = getc (inf)) == EOF)
        return -1;
    if (tmp & 0200) {
        word = (word << 6) | (tmp & 077);
        i++;
        }
    }
return word;
}

t_stat rim_load (FILE *inf, int32 fld)
{
int32 origin, val;

for (;;) {
    if ((val = pdp1_getw (inf)) < 0)
        return SCPE_FMT;
    if (((val & 0760000) == OP_DIO) ||                  /* DIO? */
        ((val & 0760000) == OP_DAC)) {                  /* hack - Macro1 err */
        origin = val & DAMASK;
        if ((val = pdp1_getw (inf)) < 0)
            return SCPE_FMT;
        M[fld | origin] = val;
        }
    else if ((val & 0760000) == OP_JMP) {               /* JMP? */
        PC = fld | (val & DAMASK);
        break;
        }
    else return SCPE_FMT;                               /* bad instr */
    }
return SCPE_OK;                                         /* done */
}

t_stat blk_load (FILE *inf, int32 fld)
{
int32 val, start, count, csum;

for (;;) {
    if ((val = pdp1_getw (inf)) < 0)                    /* get word, EOF? */
        return SCPE_FMT;
    if ((val & 0760000) == OP_DIO) {                    /* DIO? */
        csum = val;                                     /* init checksum */
        start = val & DAMASK;                           /* starting addr */
        if ((val = pdp1_getw (inf)) < 0)
            return SCPE_FMT;
        if ((val & 0760000) != OP_DIO)
            return SCPE_FMT;
        csum = csum + val;
        if (csum > DMASK)
            csum = (csum + 1) & DMASK;
        count = (val & DAMASK) - start;                 /* block count */
        if (count <= 0)
            return SCPE_FMT;
        while (count--) {                               /* loop on data */
            if ((val = pdp1_getw (inf)) < 0)
                return SCPE_FMT;
            csum = csum + val;
            if (csum > DMASK)
                csum = (csum + 1) & DMASK;
            M[fld | start] = val;
            start = (start + 1) & DAMASK;
            }
        if ((val = pdp1_getw (inf)) < 0)
            return SCPE_FMT;
        if (val != csum)
            return SCPE_CSUM;
        }
    else if ((val & 0760000) == OP_JMP) {               /* JMP? */
        PC = fld | (val & DAMASK);
        break;
        }
    else return SCPE_FMT;                               /* bad instr */
    }
return SCPE_OK;                                         /* done */
}

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
t_stat sta;
int32 fld;

if (flag != 0)
    return SCPE_ARG;
if (cptr && (*cptr != 0)) {
    fld = get_uint (cptr, 8, AMASK, &sta);
    if (sta != SCPE_OK)
        return sta;
    fld = fld & EPCMASK;
    }
else fld = 0;
sta = rim_load (fileref, fld);
if (sta != SCPE_OK)
    return sta;
if ((sim_switches & SWMASK ('B')) || match_ext (fnam, "BIN"))
    return blk_load (fileref, fld);
return SCPE_OK;
}

/* Symbol tables */

#define I_V_FL          18                              /* inst class */
#define I_M_FL          017                             /* class mask */
#define I_V_NPN         0                               /* no operand */
#define I_V_IOT         1                               /* IOT */
#define I_V_LAW         2                               /* LAW */
#define I_V_MRF         3                               /* memory reference */
#define I_V_MRI         4                               /* mem ref no ind */
#define I_V_OPR         5                               /* OPR */
#define I_V_SKP         6                               /* skip */
#define I_V_SHF         7                               /* shift */
#define I_V_SPC         8                               /* special */
#define I_NPN           (I_V_NPN << I_V_FL)             /* no operand */
#define I_IOT           (I_V_IOT << I_V_FL)             /* IOT */
#define I_LAW           (I_V_LAW << I_V_FL)             /* LAW */
#define I_MRF           (I_V_MRF << I_V_FL)             /* memory reference */
#define I_MRI           (I_V_MRI << I_V_FL)             /* mem ref no ind */
#define I_OPR           (I_V_OPR << I_V_FL)             /* OPR */
#define I_SKP           (I_V_SKP << I_V_FL)             /* skip */
#define I_SHF           (I_V_SHF << I_V_FL)             /* shift */
#define I_SPC           (I_V_SPC << I_V_FL)

static const int32 masks[] = {
 0777777, 0760077, 0760000, 0760000,
 0770000, 0760017, 0760077, 0777000,
 0760003
 };

static const char *opcode[] = {
 "AND", "IOR", "XOR", "XCT",                            /* mem refs */
 "LAC", "LIO", "DAC", "DAP",
 "DIP", "DIO", "DZM", "ADD",
 "SUB", "IDX", "ISP", "SAD",
 "SAS", "MUL", "DIV", "JMP",
 "JSP", "LCH", "DCH", "TAD",

 "CAL", "JDA",                                          /* mem ref no ind */

 "LAW",

 "IOH", "RPA", "RPB", "RRB",                            /* I/O instructions */
 "PPA", "PPB", "TYO", "TYI",
 "DPY",
 "DSC", "ASC", "ISC", "CAC",
 "LSM", "ESM", "CBS",
 "LEM", "EEM", "CKS",
 "MSE", "MLC", "MRD", "MWR", "MRS",
 "DIA", "DBA", "DWC", "DRA", "DCL",
 "DRD", "DWR", "DBL", "DCN",
 "DTD", "DSE", "DSP",
 "LRG", "ERG", "LRM", "ERM",
 "RNM", "RSM", "RCK", "CTB",
 "RCH", "RCC", "TCC", "TCB",
 "RRC", "SSB", "RSC",

 "SKP", "SKP I", "CLO",                                 /* base as NPNs */
 "SFT", "SPC", "OPR",

 "RAL", "RIL", "RCL",                                   /* shifts */
 "SAL", "SIL", "SCL",
 "RAR", "RIR", "RCR",
 "SAR", "SIR", "SCR",

 "SZF1", "SZF2", "SZF3",                                /* skips */
 "SZF4", "SZF5", "SZF6", "SZF7",
 "SZS1", "SZS1 SZF1", "SZS1 SZF2", "SZS1 SZ3",
 "SZS1 SZF4", "SZS1 SZF5", "SZS1 SZF6", "SZS1 SZF7",
 "SZS2", "SZS2 SZF1", "SZS2 SZF2", "SZS2 SZ3",
 "SZS2 SZF4", "SZS2 SZF5", "SZS2 SZF6", "SZS2 SZF7",
 "SZS3", "SZS3 SZF1", "SZS3 SZF2", "SZS3 SZ3",
 "SZS3 SZF4", "SZS3 SZF5", "SZS3 SZF6", "SZS3 SZF7",
 "SZS4", "SZS4 SZF1", "SZS4 SZF2", "SZS4 SZ3",
 "SZS4 SZF4", "SZS4 SZF5", "SZS4 SZF6", "SZS4 SZF7",
 "SZS5", "SZS5 SZF1", "SZS5 SZF2", "SZS5 SZ3",
 "SZS5 SZF4", "SZS5 SZF5", "SZS5 SZF6", "SZS5 SZF7",
 "SZS6", "SZS6 SZF1", "SZS6 SZF2", "SZS6 SZ3",
 "SZS6 SZF4", "SZS6 SZF5", "SZS6 SZF6", "SZS6 SZF7",
 "SZS7", "SZS7 SZF1", "SZS7 SZF2", "SZS7 SZ3",
 "SZS7 SZF4", "SZS7 SZF5", "SZS7 SZF6", "SZS7 SZF7",

 "CLF1", "CLF2", "CLF3",                                /* operates */
 "CLF4", "CLF5", "CLF6", "CLF7",
 "STF1", "STF2", "STF3",
 "STF4", "STF5", "STF6", "STF7",

 "FF1", "FF2", "FF3",                                   /* specials */

 "SZA", "SPA", "SMA",                                   /* uprog skips */
 "SZO", "SPI", "SNI",
 "I",                                                   /* encode only */

 "LIA", "LAI", "SWP",                                   /* uprog opers */
 "LAP", "CLA", "HLT",
 "CMA", "LAT", "CLI",
 "CMI",

 "CML", "CLL", "SZL",                                   /* uprog specials */
 "SCF", "SCI", "SCM",
 "IDA", "IDC", "IFI",
 "IIF",

 NULL, NULL, NULL,                                      /* decode only */
 NULL,
 };

static const int32 opc_val[] = {
 0020000+I_MRF, 0040000+I_MRF, 0060000+I_MRF, 0100000+I_MRF,
 0200000+I_MRF, 0220000+I_MRF, 0240000+I_MRF, 0260000+I_MRF,
 0300000+I_MRF, 0320000+I_MRF, 0340000+I_MRF, 0400000+I_MRF,
 0420000+I_MRF, 0440000+I_MRF, 0460000+I_MRF, 0500000+I_MRF,
 0520000+I_MRF, 0540000+I_MRF, 0560000+I_MRF, 0600000+I_MRF,
 0620000+I_MRF, 0120000+I_MRF, 0140000+I_MRF, 0360000+I_MRF,

 0160000+I_MRI, 0170000+I_MRI,

 0700000+I_LAW,

 0730000+I_NPN, 0720001+I_IOT, 0720002+I_IOT, 0720030+I_IOT,
 0720005+I_IOT, 0720006+I_IOT, 0720003+I_IOT, 0720004+I_IOT,
 0720007+I_IOT,
 0720050+I_IOT, 0720051+I_IOT, 0720052+I_IOT, 0720053+I_NPN,
 0720054+I_NPN, 0720055+I_NPN, 0720056+I_NPN,
 0720074+I_NPN, 0724074+I_NPN, 0720033+I_NPN,
 0720301+I_NPN, 0720401+I_NPN, 0720501+I_NPN, 0720601+I_NPN, 0720701+I_NPN, 
 0720061+I_NPN, 0722061+I_NPN, 0720062+I_NPN, 0722062+I_NPN, 0720063+I_NPN,
 0720161+I_NPN, 0721161+I_NPN, 0720162+I_NPN, 0721162+I_NPN,
 0720163+I_NPN, 0720164+I_NPN, 0721164+I_NPN,
 0720010+I_NPN, 0720011+I_NPN, 0720064+I_NPN, 0720065+I_NPN,
 0720066+I_IOT, 0720067+I_NPN, 0720032+I_NPN, 0720035+I_NPN,
 0720022+I_NPN, 0721022+I_NPN, 0725022+I_NPN, 0724022+I_NPN,
 0720122+I_NPN, 0724122+I_NPN, 0721122+I_NPN,

 0640000+I_NPN, 0650000+I_NPN, 0651600+I_NPN,
 0660000+I_NPN, 0740000+I_NPN, 0760000+I_NPN,

 0661000+I_SHF, 0662000+I_SHF, 0663000+I_SHF,
 0665000+I_SHF, 0666000+I_SHF, 0667000+I_SHF,
 0671000+I_SHF, 0672000+I_SHF, 0673000+I_SHF,
 0675000+I_SHF, 0676000+I_SHF, 0677000+I_SHF,

 0640001+I_SKP, 0640002+I_SKP, 0640003+I_SKP,
 0640004+I_SKP, 0640005+I_SKP, 0640006+I_SKP, 0640007+I_SKP,
 0640010+I_SKP, 0640011+I_SKP, 0640012+I_SKP, 0640013+I_SKP,
 0640014+I_SKP, 0640015+I_SKP, 0640016+I_SKP, 0640017+I_SKP,
 0640020+I_SKP, 0640021+I_SKP, 0640022+I_SKP, 0640023+I_SKP,
 0640024+I_SKP, 0640025+I_SKP, 0640026+I_SKP, 0640027+I_SKP,
 0640030+I_SKP, 0640031+I_SKP, 0640032+I_SKP, 0640033+I_SKP,
 0640034+I_SKP, 0640035+I_SKP, 0640036+I_SKP, 0640037+I_SKP,
 0640040+I_SKP, 0640041+I_SKP, 0640042+I_SKP, 0640043+I_SKP,
 0640044+I_SKP, 0640045+I_SKP, 0640046+I_SKP, 0640047+I_SKP,
 0640050+I_SKP, 0640051+I_SKP, 0640052+I_SKP, 0640053+I_SKP,
 0640054+I_SKP, 0640055+I_SKP, 0640056+I_SKP, 0640057+I_SKP,
 0640060+I_SKP, 0640061+I_SKP, 0640062+I_SKP, 0640063+I_SKP,
 0640064+I_SKP, 0640065+I_SKP, 0640066+I_SKP, 0640067+I_SKP,
 0640070+I_SKP, 0640071+I_SKP, 0640072+I_SKP, 0640073+I_SKP,
 0640074+I_SKP, 0640075+I_SKP, 0640076+I_SKP, 0640077+I_SKP,

 0760001+I_OPR, 0760002+I_OPR, 0760003+I_OPR,
 0760004+I_OPR, 0760005+I_OPR, 0760006+I_OPR, 0760007+I_OPR,
 0760011+I_OPR, 0760012+I_OPR, 0760013+I_OPR,
 0760014+I_OPR, 0760015+I_OPR, 0760016+I_OPR, 0760017+I_OPR,

 0740001+I_SPC, 0740002+I_SPC, 0740003+I_OPR,

 0640100+I_SKP, 0640200+I_SKP, 0640400+I_SKP,
 0641000+I_SKP, 0642000+I_SKP, 0644000+I_SKP,
 0010000+I_SKP,                                         /* encode only */

 0760020+I_OPR, 0760040+I_OPR, 0760060+I_NPN,
 0760100+I_OPR, 0760200+I_OPR, 0760400+I_OPR,
 0761000+I_OPR, 0762000+I_OPR, 0764000+I_OPR,
 0770000+I_OPR,

 0740004+I_SPC, 0740010+I_SPC, 0740020+I_SPC,
 0740040+I_SPC, 0740100+I_SPC, 0740200+I_SPC,
 0740400+I_SPC, 0741000+I_SPC, 0742000+I_SPC,
 0744000+I_SPC,

 0640000+I_SKP, 0740000+I_SPC, 0760000+I_OPR,           /* decode only */
 -1
 };

/* Operate or skip decode

   Inputs:
        *of     =       output stream
        inst    =       mask bits
        class   =       instruction class code
        sp      =       space needed?
   Outputs:
        status  =       space needed?
*/

int32 fprint_opr (FILE *of, int32 inst, int32 class, int32 sp)
{
int32 i, j;

for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((j == class) && (opc_val[i] & inst)) {          /* same class? */
        inst = inst & ~opc_val[i];                      /* mask bit set? */
        fprintf (of, (sp? " %s": "%s"), opcode[i]);
        sp = 1;
        }
    }
return sp;
}

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
#define SIXTOASC(x) fiodec_to_ascii[x]
#define ASCTOSIX(x) (ascii_to_fiodec[x] & 077)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 cflag, i, j, sp, inst, disp, ma;

inst = val[0];
cflag = (uptr == NULL) || (uptr == &cpu_unit);
if (sw & SWMASK ('A')) {                                /* ASCII? */
    if (inst > 0377)
        return SCPE_ARG;
    fprintf (of, FMTASC (inst & 0177));
    return SCPE_OK;
    }
if (sw & SWMASK ('F')) {
    fputc (fiodec_to_ascii[inst & 077], of);
    return SCPE_OK;
    }
if (sw & SWMASK ('C')) {                                /* character? */
    fprintf (of, "%c", SIXTOASC ((inst >> 12) & 077));
    fprintf (of, "%c", SIXTOASC ((inst >> 6) & 077));
    fprintf (of, "%c", SIXTOASC (inst & 077));
    return SCPE_OK;
    }
if (!(sw & SWMASK ('M')))
    return SCPE_ARG;

/* Instruction decode */

disp = inst & 007777;
ma = (addr & EPCMASK) | disp;
for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((opc_val[i] & DMASK) == (inst & masks[j])) {    /* match? */

        switch (j) {                                    /* case on class */

        case I_V_NPN:                                   /* no operands */
            fprintf (of, "%s", opcode[i]);              /* opcode */
            break;

        case I_V_IOT:                                   /* IOT */
            disp = (inst - opc_val[i]) & 017777;
            if (disp == IA)
                fprintf (of, "%s I", opcode[i]);
            else if (disp)
                fprintf (of, "%s %-o", opcode[i], disp);
            else fprintf (of, "%s", opcode[i]);
            break;

        case I_V_LAW:                                   /* LAW */
            cflag = 0;                                  /* fall thru to MRF */
        case I_V_MRF:                                   /* mem ref */
            fprintf (of, "%s%s%-o", opcode[i],
                ((inst & IA)? " I ": " "), (cflag? ma: disp));
            break;

        case I_V_MRI:                                   /* mem ref no ind */
            fprintf (of, "%s %-o", opcode[i], (cflag? ma: disp));
            break;

        case I_V_OPR:                                   /* operates */
            sp = fprint_opr (of, inst & 017760, j, 0);
            if (opcode[i])
                fprintf (of, (sp? " %s": "%s"), opcode[i]);
            break;

        case I_V_SKP:                                   /* skips */
            sp = fprint_opr (of, inst & 007700, j, 0);
            if (opcode[i])
                sp = fprintf (of, (sp? " %s": "%s"), opcode[i]);
            if (inst & IA)
                fprintf (of, sp? " I": "I");
            break;

        case I_V_SPC:                                   /* specials */
            sp = fprint_opr (of, inst & 007774, j, 0);
            if (opcode[i])
                sp = fprintf (of, (sp? " %s": "%s"), opcode[i]);
            if (inst & IA)
                fprintf (of, sp? " I": "I");
            break;

        case I_V_SHF:                                   /* shifts */
            fprintf (of, "%s %-d", opcode[i], sc_map[inst & 0777]);
            break;
            }                                           /* end case */

        return SCPE_OK;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;
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
int32 cflag, d, i, j, k, sign;
t_stat r;
static int32 sc_enc[10] = { 0, 01, 03, 07, 017, 037, 077, 0177, 0377, 0777 };
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;
for (i = 1; (i < 3) && (cptr[i] != 0); i++) {
    if (cptr[i] == 0) {
        for (j = i + 1; j <= 3; j++)
            cptr[j] = 0;
        }
    }
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = (t_value) cptr[0];
    return SCPE_OK;
    }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* sixbit string? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = ((ASCTOSIX (cptr[0]) & 077) << 12) |
             ((ASCTOSIX (cptr[1]) & 077) << 6) |
              (ASCTOSIX (cptr[2]) & 077);
    return SCPE_OK;
    }

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
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
        if (r != SCPE_OK)
            return SCPE_ARG;
        if (d <= DAMASK)
            val[0] = val[0] | d;
        else if (cflag && (((addr ^ d) & EPCMASK) == 0))
                val[0] = val[0] | (d & DAMASK);
        else return SCPE_ARG;
        break;

    case I_V_SHF:                                       /* shift */
        cptr = get_glyph (cptr, gbuf, 0);
        d = get_uint (gbuf, 10, 9, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
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
                if (r != SCPE_OK)
                    return SCPE_ARG;
                if (sign == 0)
                    val[0] = val[0] + d;  
                else if (sign < 0)
                    val[0] = val[0] - d;
                else val[0] = val[0] | d;
                }
            }
        break;
        }                                               /* end case */
if (*cptr != 0)                                         /* junk at end? */
    return SCPE_ARG;
return SCPE_OK;
}
