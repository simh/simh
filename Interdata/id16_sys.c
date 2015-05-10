/* id16_sys.c: Interdata 16b simulator interface

   Copyright (c) 2000-2008, Robert M. Supnik

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

   04-Feb-08    RMS     Modified to allow -A, -B use with 8b devices
   18-Oct-06    RMS     Re-ordered device list
   26-Mar-04    RMS     Fixed warning with -std=c99
   27-Feb-03    RMS     Added relative addressing support
*/

#include "id_defs.h"
#include <ctype.h>

#define MSK_SBF         0x0100

extern DEVICE cpu_dev;
extern DEVICE sch_dev;
extern DEVICE pt_dev;
extern DEVICE tt_dev, ttp_dev;
extern DEVICE pas_dev, pasl_dev;
extern DEVICE lpt_dev;
extern DEVICE pic_dev, lfc_dev;
extern DEVICE dp_dev, idc_dev;
extern DEVICE fd_dev, mt_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint16 *M;

t_stat fprint_sym_m (FILE *of, t_addr addr, t_value *val);
t_stat parse_sym_m (char *cptr, t_addr addr, t_value *val);
extern t_stat lp_load (FILE *fileref, char *cptr, char *fnam);
extern t_stat pt_dump (FILE *of, char *cptr, char *fnam);

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "Interdata 16b";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 2;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &sch_dev,
    &pic_dev,
    &lfc_dev,
    &pt_dev,
    &tt_dev,
    &ttp_dev,
    &pas_dev,
    &pasl_dev,
    &lpt_dev,
    &dp_dev,
    &idc_dev,
    &fd_dev,
    &mt_dev,
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "Reserved instruction",
    "HALT instruction",
    "Breakpoint",
    "Wait state",
    "Runaway VFU"
    };

/* Binary loader -- load carriage control tape
   Binary dump -- paper tape dump */

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
if (flag)
    return pt_dump (fileref, cptr, fnam);
return lp_load (fileref, cptr, fnam);
}

/* Symbol tables */

#define I_V_FL          16                              /* class bits */
#define I_M_FL          0xF                             /* class mask */
#define I_V_MR          0x0                             /* mask-register */
#define I_V_RR          0x1                             /* register-register */
#define I_V_R           0x2                             /* register */
#define I_V_MX          0x3                             /* mask-memory */
#define I_V_RX          0x4                             /* register-memory */
#define I_V_X           0x5                             /* memory */
#define I_V_FF          0x6                             /* float reg-reg */
#define I_V_FX          0x7                             /* float reg-mem */
#define I_V_SI          0x8                             /* short immed */
#define I_V_SB          0x9                             /* short branch */
#define I_V_SX          0xA                             /* short ext branch */
#define I_MR            (I_V_MR << I_V_FL)
#define I_RR            (I_V_RR << I_V_FL)
#define I_R             (I_V_R << I_V_FL)
#define I_MX            (I_V_MX << I_V_FL)
#define I_RX            (I_V_RX << I_V_FL)
#define I_X             (I_V_X << I_V_FL)
#define I_FF            (I_V_FF << I_V_FL)
#define I_FX            (I_V_FX << I_V_FL)
#define I_SI            (I_V_SI << I_V_FL)
#define I_SB            (I_V_SB << I_V_FL)
#define I_SX            (I_V_SX << I_V_FL)

#define R_X             0                               /* no reg */
#define R_M             1                               /* reg mask */
#define R_R             2                               /* reg int reg */
#define R_F             3                               /* reg flt reg */

static const int32 masks[] = {
 0xFF00, 0xFF00, 0xFFF0, 0xFF00,
 0xFF00, 0xFFF0, 0xFF00, 0xFF00,
 0xFF00, 0xFE00, 0xFEF0
 };

static const uint32 r1_type[] = {
 R_M, R_R, R_X, R_M,
 R_R, R_X, R_F, R_F,
 R_R, R_M, R_X
 };

static const uint32 r2_type[] = {
 R_X, R_R, R_R, R_X,
 R_X, R_X, R_F, R_X,
 R_M, R_X, R_X
 };

static const char *opcode[] = {
"BER", "BNER","BZR", "BNZR",
"BPR", "BNPR","BLR", "BNLR",
"BMR", "BNMR","BOR", "BNOR",
"BCR", "BNCR","BR",
"BES", "BNES","BZS", "BNZS",
"BPS", "BNPS","BLS", "BNLS",
"BMS", "BNMS","BOS", "BNOS",
"BCS", "BNCS","BS",
"BE",  "BNE", "BZ",  "BNZ",
"BP",  "BNP", "BL",  "BNL",
"BM",  "BNM", "BO",  "BNO",
"BC",  "BNC", "B",
       "BALR","BTCR","BFCR",
"NHR", "CLHR","OHR", "XHR",
"LHR", "CHR", "AHR", "SHR",
"MHR", "DHR", "ACHR","SCHR",
                     "SETMR",
"BTBS","BTFS","BFBS","BFFS",
"LIS", "LCS", "AIS", "SIS",
"LER", "CER", "AER", "SER",
"MER", "DER", "FXR", "FLR",
                     "LPSR",
"LDR", "CDR", "ADR", "SDR",
"MDR", "DDR", "FXDR","FLDR",
"STH", "BAL", "BTC", "BFC",
"NH",  "CLH", "OH",  "XH",
"LH",  "CH",  "AH",  "SH",
"MH",  "DH",  "ACH", "SCH",
                     "SETM",
"STE", "AHM",
"ATL", "ABL", "RTL", "RBL",
"LE",  "CE",  "AE",  "SE",
"ME",  "DE",
"STD", "STME","LME", "LPS",
"LD",  "CD",  "AD",  "SD",
"MD",  "DD",  "STMD","LMD",
"SRLS","SLLS","STBR","LBR",
"EXBR","EPSR","WBR", "RBR",
"WHR", "RHR", "WDR", "RDR",
"MHUR","SSR", "OCR", "AIR",
"BXH", "BXLE","LPSW","THI",
"NHI", "CLHI","OHI", "XHI",
"LHI", "CHI", "AHI", "SHI",
"SRHL","SLHL","SRHA","SLHA",
"STM", "LM",  "STB", "LB",
"CLB", "AL",  "WB",  "RB",
"WH",  "RH",  "WD",  "RD",
"MHU", "SS",  "OC",  "AI",
       "SVC", "SINT",
              "RRL", "RLL",
"SRL", "SLL", "SRA", "SLA",
NULL
};

static const uint32 opc_val[] = {
0x0330+I_R,  0x0230+I_R,  0x0330+I_R,  0x0230+I_R,
0x0220+I_R,  0x0320+I_R,  0x0280+I_R,  0x0380+I_R,
0x0210+I_R,  0x0310+I_R,  0x0240+I_R,  0x0340+I_R,
0x0280+I_R,  0x0380+I_R,  0x0300+I_R,
0x2230+I_SX, 0x2030+I_SX, 0x2230+I_SX, 0x2030+I_SX,
0x2020+I_SX, 0x2220+I_SX, 0x2080+I_SX, 0x2280+I_SX,
0x2010+I_SX, 0x2210+I_SX, 0x2040+I_SX, 0x2240+I_SX,
0x2080+I_SX, 0x2280+I_SX, 0x2200+I_SX,
0x4330+I_X,  0x4230+I_X,  0x4330+I_X,  0x4230+I_X,
0x4220+I_X,  0x4320+I_X,  0x4280+I_X,  0x4380+I_X,
0x4210+I_X,  0x4310+I_X,  0x4240+I_X,  0x4340+I_X,
0x4280+I_X,  0x4380+I_X,  0x4300+I_X,
             0x0100+I_RR, 0x0200+I_MR, 0x0300+I_MR,
0x0400+I_RR, 0x0500+I_RR, 0x0600+I_RR, 0x0700+I_RR,
0x0800+I_RR, 0x0900+I_RR, 0x0A00+I_RR, 0x0B00+I_RR,
0x0C00+I_RR, 0x0D00+I_RR, 0x0E00+I_RR, 0x0F00+I_RR,
                                       0x1300+I_RR,
0x2000+I_SB, 0x2100+I_SB, 0x2200+I_SB, 0x2300+I_SB,
0x2400+I_SI, 0x2500+I_SI, 0x2600+I_SI, 0x2700+I_SI,
0x2800+I_FF, 0x2900+I_FF, 0x2A00+I_FF, 0x2B00+I_FF,
0x2C00+I_FF, 0x2D00+I_FF, 0x2E00+I_RR, 0x2F00+I_RR,
                                       0x3300+I_R,
0x3800+I_FF, 0x3900+I_FF, 0x3A00+I_FF, 0x3B00+I_FF,
0x3C00+I_FF, 0x3D00+I_FF, 0x3E00+I_RR, 0x3F00+I_RR,
0x4000+I_RX, 0x4100+I_RX, 0x4200+I_MX, 0x4300+I_MX,
0x4400+I_RX, 0x4500+I_RX, 0x4600+I_RX, 0x4700+I_RX,
0x4800+I_RX, 0x4900+I_RX, 0x4A00+I_RX, 0x4B00+I_RX,
0x4C00+I_RX, 0x4D00+I_RX, 0x4E00+I_RX, 0x4F00+I_RX,
                                       0x5300+I_RX,
0x6000+I_RX, 0x6100+I_RX,
0x6400+I_RX, 0x6500+I_RX, 0x6600+I_RX, 0x6700+I_RX,
0x6800+I_FX, 0x6900+I_FX, 0x6A00+I_FX, 0x6B00+I_FX,
0x6C00+I_FX, 0x6D00+I_FX,
0x7000+I_FX, 0x7100+I_FX, 0x7200+I_FX, 0x7300+I_X,
0x7800+I_FX, 0x7900+I_FX, 0x7A00+I_FX, 0x7B00+I_FX,
0x7C00+I_FX, 0x7D00+I_FX, 0x7E00+I_FX, 0x7F00+I_FX,
0x9000+I_SI, 0x9100+I_SI, 0x9200+I_RR, 0x9300+I_RR,
0x9400+I_RR, 0x9500+I_RR, 0x9600+I_RR, 0x9700+I_RR,
0x9800+I_RR, 0x9900+I_RR, 0x9A00+I_RR, 0x9B00+I_RR,
0x9C00+I_RR, 0x9D00+I_RR, 0x9E00+I_RR, 0x9F00+I_RR,
0xC000+I_RX, 0xC100+I_RX, 0xC200+I_X,  0xC300+I_RX,
0xC400+I_RX, 0xC500+I_RX, 0xC600+I_RX, 0xC700+I_RX,
0xC800+I_RX, 0xC900+I_RX, 0xCA00+I_RX, 0xCB00+I_RX,
0xCC00+I_RX, 0xCD00+I_RX, 0xCE00+I_RX, 0xCF00+I_RX,
0xD000+I_RX, 0xD100+I_RX, 0xD200+I_RX, 0xD300+I_RX,
0xD400+I_RX, 0xD500+I_X,  0xD600+I_RX, 0xD700+I_RX,
0xD800+I_RX, 0xD900+I_RX, 0xDA00+I_RX, 0xDB00+I_RX,
0xDC00+I_RX, 0xDD00+I_RX, 0xDE00+I_RX, 0xDF00+I_RX,
             0xE100+I_RX, 0xE200+I_RX,
                          0xEA00+I_RX, 0xEB00+I_RX,
0xEC00+I_RX, 0xED00+I_RX, 0xEE00+I_RX, 0xEF00+I_RX,
0xFFFF
};

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       values to decode
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       if >= 0, error code
                        if < 0, number of extra bytes retired
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 bflag, c1, c2, rdx;
t_stat r;
DEVICE *dptr;

if (uptr == NULL)                                       /* anon = CPU */
    uptr = &cpu_unit;
dptr = find_dev_from_unit (uptr);                       /* find dev */
if (dptr == NULL)
    return SCPE_IERR;
if (dptr->dwidth < 16)                                  /* 8b dev? */
    bflag = 1;
else bflag = 0;                                         /* assume 16b */
if (sw & SWMASK ('D'))                                  /* get radix */
    rdx = 10;
else if (sw & SWMASK ('O'))
    rdx = 8;
else if (sw & SWMASK ('H'))
    rdx = 16;
else rdx = dptr->dradix;

if (sw & SWMASK ('A')) {                                /* ASCII char? */
    if (bflag)
        c1 = val[0] & 0x7F;
    else c1 = (val[0] >> ((addr & 1)? 0: 8)) & 0x7F;    /* get byte */
    fprintf (of, (c1 < 0x20)? "<%02X>": "%c", c1);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    if (bflag)
        c1 = val[0] & 0xFF;
    else c1 = (val[0] >> ((addr & 1)? 0: 8)) & 0xFF;    /* get byte */
    fprint_val (of, c1, rdx, 8, PV_RZRO);
    return 0;
    }
if (bflag)                                              /* 16b only */
    return SCPE_ARG;

if (sw & SWMASK ('C')) {                                /* string? */
    c1 = (val[0] >> 8) & 0x7F;
    c2 = val[0] & 0x7F;
    fprintf (of, (c1 < 0x20)? "<%02X>": "%c", c1);
    fprintf (of, (c2 < 0x20)? "<%02X>": "%c", c2);
    return -1;
    }
if (sw & SWMASK ('F')) {                                /* fullword? */
    fprint_val (of, (val[0] << 16) | val[1], rdx, 32, PV_RZRO);
    return -3;
    }
if (sw & SWMASK ('M')) {                                /* inst format? */
    r = fprint_sym_m (of, addr, val);                   /* decode inst */
    if (r <= 0)
        return r;
    }

fprint_val (of, val[0], rdx, 16, PV_RZRO);
return -1;
}

/* Symbolic decode for -m

   Inputs:
        of      =       output stream
        addr    =       current PC
        *val    =       values to decode
   Outputs:
        return  =       if >= 0, error code
                        if < 0, number of extra bytes retired
*/

t_stat fprint_sym_m (FILE *of, t_addr addr, t_value *val)
{
uint32 i, j, inst, r1, r2, ea;

inst = val[0];                                          /* first 16b */
ea = val[1];                                            /* second 16b */
for (i = 0; opcode[i] != NULL; i++) {                   /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((opc_val[i] & 0xFFFF) == (inst & masks[j])) {   /* match? */
        r1 = (inst >> 4) & 0xF;
        r2 = inst & 0xF;
        fprintf (of, "%s ", opcode[i]);                 /* print opcode */
        switch (j) {                                    /* case on class */

        case I_V_MR:                                    /* mask-register */
            fprintf (of, "%-X,R%d", r1, r2);
            return -1;

        case I_V_RR:                                    /* register-register */
        case I_V_FF:                                    /* floating-floating */
            fprintf (of, "R%d,R%d", r1, r2);
            return -1;

        case I_V_SI:                                    /* short immediate */
            fprintf (of, "R%d,%-X", r1, r2);
            return -1;

        case I_V_SB:                                    /* short branch */
            fprintf (of, "%-X,", r1);
        case I_V_SX:                                    /* ext short branch */
            fprintf (of, "%-X", ((inst & MSK_SBF)?
                (addr + r2 + r2): (addr - r2 - r2)));
            return -1;

        case I_V_R:                                     /* register */
            fprintf (of, "R%d", r2);
            return -1;

        case I_V_MX:                                    /* mask-memory */
            fprintf (of, "%-X,%-X", r1, ea);
            break;

        case I_V_RX:                                    /* register-memory */
        case I_V_FX:                                    /* floating-memory */
            fprintf (of, "R%d,%-X", r1, ea);
            break;

        case I_V_X:                                     /* memory */
            fprintf (of, "%-X", ea);
            break;
            }                                           /* end case */

        if (r2)
            fprintf (of, "(R%d)", r2);  
        return -3;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;                                        /* no match */
}

/* Register number

   Inputs:
        *cptr   =       pointer to input string
        **optr  =       pointer to pointer to next char
        rtype   =       mask, integer, or float
   Outputs:
        rnum    =       output register number, -1 if error
*/

int32 get_reg (char *cptr, char **optr, int32 rtype)
{
int32 reg;

if ((*cptr == 'R') || (*cptr == 'r')) {                 /* R? */
    cptr++;                                             /* skip */
    if (rtype == R_M)                                    /* cant be mask */
        return -1;
    }
if ((*cptr >= '0') && (*cptr <= '9')) {
    reg = *cptr++ - '0';
    if ((*cptr >= '0') && (*cptr <= '9'))
        reg = (reg * 10) + (*cptr - '0'); 
    else --cptr;
    if (reg > 0xF)
        return -1;
    }
else if ((*cptr >= 'a') && (*cptr <= 'f'))
    reg = (*cptr - 'a') + 10;
else if ((*cptr >= 'A') && (*cptr <= 'F'))
    reg = (*cptr - 'A') + 10;
else return -1;
if ((rtype == R_F) && (reg & 1))
    return -1;
*optr = cptr + 1;
return reg;
}

/* Address

   Inputs:
        *cptr   =       pointer to input string
        **tptr  =       pointer to moved pointer
        *ea     =       effective address
        addr    =       base address
   Outputs:
        status  =       SCPE_OK if ok, else error code
*/

t_stat get_addr (char *cptr, char **tptr, t_addr *ea, t_addr addr)
{
int32 sign = 1;

if (*cptr == '.') {                                     /* relative? */
    cptr++;
    *ea = addr;
    if (*cptr == '+')                                   /* .+? */
        cptr++;
    else if (*cptr == '-') {                            /* .-? */
        sign = -1;
        cptr++;
        }
    else return SCPE_OK;
    }
else *ea = 0;
errno = 0;
*ea = *ea + (sign * ((int32) strtoul (cptr, tptr, 16)));
if (errno || (cptr == *tptr))
    return SCPE_ARG;
return SCPE_OK;
}

/* Symbolic input */

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 bflag, by, rdx, num;
t_stat r;
DEVICE *dptr;

if (uptr == NULL)                                       /* anon = CPU */
    uptr = &cpu_unit;
dptr = find_dev_from_unit (uptr);                       /* find dev */
if (dptr == NULL)
    return SCPE_IERR;
if (dptr->dwidth < 16)                                  /* 8b dev? */
    bflag = 1;
else bflag = 0;                                         /* assume 16b */
if (sw & SWMASK ('D'))                                  /* get radix */
    rdx = 10;
else if (sw & SWMASK ('O'))
    rdx = 8;
else if (sw & SWMASK ('H'))
    rdx = 16;
else rdx = dptr->dradix;

if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    if (bflag)
        val[0] = (t_value) cptr[0];
    else val[0] = (addr & 1)?
        (val[0] & ~0xFF) | ((t_value) cptr[0]):
        (val[0] & 0xFF) | (((t_value) cptr[0]) << 8);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    by = get_uint (cptr, rdx, DMASK8, &r);              /* get byte */
    if (r != SCPE_OK)
        return SCPE_ARG;
    if (bflag)
        val[0] = by;
    else val[0] = (addr & 1)?
        (val[0] & ~0xFF) | by:
        (val[0] & 0xFF) | (by << 8);
    return 0;
    }
if (bflag)                                              /* 16b only */
    return SCPE_ARG;

if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII chars? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = ((t_value) cptr[0] << 8) | (t_value) cptr[1];
    return -1;
    }
if (sw & SWMASK ('F')) {
    num = (int32) get_uint (cptr, rdx, DMASK32, &r);    /* get number */
    if (r != SCPE_OK)
        return r;
    val[0] = (num >> 16) & DMASK16;
    val[1] = num & DMASK16;
    return -3;
    }

r = parse_sym_m (cptr, addr, val);                      /* try to parse inst */
if (r <= 0)
    return r;
val[0] = (int32) get_uint (cptr, rdx, DMASK16, &r);     /* get number */
if (r != SCPE_OK)
    return r;
return -1;
}

/* Symbolic input for -m

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *val    =       pointer to output values
        cf      =       true if parsing for CPU
   Outputs:
        status  =       > 0   error code
                        <= 0  -number of extra words
*/

t_stat parse_sym_m (char *cptr, t_addr addr, t_value *val)
{
uint32 i, j, t, df, db, inst;
int32 r1, r2;
t_stat r;
char *tptr, gbuf[CBUFSIZE];

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
inst = opc_val[i] & 0xFFFF;                             /* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;                    /* get class */
if (r1_type[j]) {                                       /* any R1 field? */
    cptr = get_glyph (cptr, gbuf, ',');                 /* get R1 field */
    if ((r1 = get_reg (gbuf, &tptr, r1_type[j])) < 0)
        return SCPE_ARG;
    if (*tptr != 0)                                     /* all done? */
        return SCPE_ARG;
    inst = inst | (r1 << 4);                            /* or in R1 */
    }

cptr = get_glyph (cptr, gbuf, 0);                       /* get operand */
if (*cptr)                                              /* should be end */
    return SCPE_ARG;
switch (j) {                                            /* case on class */

    case I_V_FF: case I_V_SI:                           /* flt-flt, sh imm */
    case I_V_MR: case I_V_RR:                           /* mask/reg-reg */
    case I_V_R:                                         /* register */
        if ((r2 = get_reg (gbuf, &tptr, r2_type[j])) < 0)
            return SCPE_ARG;
        if (*tptr != 0)                                 /* all done? */
            return SCPE_ARG;
        inst = inst | r2;                               /* or in R2 */
        break;

    case I_V_FX:                                        /* float-memory */
    case I_V_MX: case I_V_RX:                           /* mask/reg-mem */
    case I_V_X:                                         /* memory */
        r = get_addr (gbuf, &tptr, &t, addr);           /* get addr */
        if ((r != SCPE_OK) || (t > PAMASK16))
            return SCPE_ARG;
        if (*tptr == '(') {                             /* index? */
            if ((r2 = get_reg (tptr + 1, &tptr, R_R)) < 0)
                return SCPE_ARG;
            if (*tptr++ != ')')
                return SCPE_ARG;
            inst = inst | r2;                           /* or in R2 */
            }
        if (*tptr != 0)
            return SCPE_ARG;
        val[0] = inst;
        val[1] = t;
        return -3;

    case I_V_SB: case I_V_SX:                           /* short branches */
        r = get_addr (gbuf, &tptr, &t, addr);           /* get addr */
        if ((r != SCPE_OK) || (t & 1) || *tptr)         /* error if odd */
            return SCPE_ARG;
        db = (addr - t) & 0x1F;                         /* back displ */
        df = (t - addr) & 0x1F;                         /* fwd displ */
        if ((t == ((addr - db) & VAMASK16)) &&          /* back work and */
            ((j == I_V_SX) || !(inst & MSK_SBF)))       /* ext or back br? */
            inst = inst | (db >> 1);                    /* or in back displ */
        else if ((t == ((addr + df) & VAMASK16)) &&     /* fwd work and */
            ((j == I_V_SX) || (inst & MSK_SBF)))        /* ext or fwd br? */
            inst = inst | (df >> 1) | MSK_SBF;          /* or in fwd displ */
        else return SCPE_ARG;
        break;
        }                                               /* end case */

val[0] = inst;
return -1;
}
