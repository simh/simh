/* i1620_sys.c: IBM 1620 simulator interface

   Copyright (c) 2002-2017, Robert M. Supnik

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

   25-May-17    RMS     Tweaks and corrections from Tom McBride
   18-May-17    RMS     Changed fprint_val to handle undefined opcodes on stops
   19-Mar-12    RMS     Fixed declaration of CCT (Mark Pizzolato)
*/

#include "i1620_defs.h"
#include <ctype.h>

#define LINE_LNT        50

extern DEVICE cpu_dev, tty_dev;
extern DEVICE ptr_dev, ptp_dev;
extern DEVICE lpt_dev;
extern DEVICE cdr_dev, cdp_dev;
extern DEVICE dp_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint8 M[MAXMEMSIZE];

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "IBM 1620";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = LINE_LNT;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &tty_dev,
    &ptr_dev,
    &ptp_dev,
    &cdr_dev,
    &cdp_dev,
    &lpt_dev,
    &dp_dev,
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "HALT instruction",
    "Breakpoint",
    "Invalid instruction",
    "Invalid digit",
    "Invalid character",
    "Invalid indicator",
    "Invalid digit in P address",
    "Invalid P address",
    "P address exceeds indirect address limit",
    "Invalid digit in Q address",
    "Invalid Q address",
    "Q address exceeds indirect address limit",
    "Invalid IO device",
    "Invalid return register",
    "Invalid IO function",
    "Instruction address must be even",
    "Invalid select code",
    "Index instruction with no band selected",
    "P address must be odd",
    "DCF address must be even",
    "Invalid disk drive",
    "Invalid disk sector address",
    "Invalid disk sector count",
    "Invalid disk buffer address",
    "Disk address compare error",
    "Disk write check error",
    "Disk cylinder overflow error",
    "Disk wrong length record error",
    "Invalid CCT",
    "Field exceeds memory",
    "Record exceeds memory",
    "No card in reader",
    "Overflow check",
    "Exponent check",
    "Write address function disabled",
    "Floating point mantissa too long",
    "Floating point mantissa lengths unequal",
    "Floating point exponent flag missing",
    "Floating point divide by zero"
    };

/* Binary loader -- load carriage control tape

   A carriage control tape consists of entries of the form

        (repeat count) column number,column number,column number,...

   The CCT entries are stored in cct[0:lnt-1], cctlnt contains the
   number of entries
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
uint32 col, mask, cctbuf[CCT_LNT];
int32 ptr, rpt;
t_stat r;
extern int32 cct_lnt, cct_ptr;
extern uint32 cct[CCT_LNT];
char cbuf[CBUFSIZE], gbuf[CBUFSIZE];

if ((*cptr != 0) || (flag != 0))
    return SCPE_ARG;
ptr = 0;
for ( ; (cptr = fgets (cbuf, CBUFSIZE, fileref)) != NULL; ) { /* until eof */
    mask = 0;
    if (*cptr == '(') {                                 /* repeat count? */
        cptr = get_glyph (cptr + 1, gbuf, ')');         /* get 1st field */
        rpt = get_uint (gbuf, 10, CCT_LNT, &r);         /* repeat count */
        if (r != SCPE_OK)
            return SCPE_FMT;
        }
    else rpt = 1;
    while (*cptr != 0) {                                /* get col no's */
        cptr = get_glyph (cptr, gbuf, ',');             /* get next field */
        col = get_uint (gbuf, 10, 12, &r);              /* column number */
        if (r != SCPE_OK)
            return SCPE_FMT;
        mask = mask | (1 << col);                       /* set bit */
        }
    for ( ; rpt > 0; rpt--) {                           /* store vals */
        if (ptr >= CCT_LNT)
            return SCPE_FMT;
        cctbuf[ptr++] = mask;
        }
    }
if (ptr == 0)
    return SCPE_FMT;
cct_lnt = ptr;
cct_ptr = 0;
for (rpt = 0; rpt < cct_lnt; rpt++)
    cct[rpt] = cctbuf[rpt];
return SCPE_OK;
}

/* Symbol table */

struct opc {
    const char          *str;                           /* mnemonic */
    uint32              opv;                            /* opcode & flags */
    uint32              qv;                             /* q field */
    };

#define I_V_FL          16                              /* flags */
#define I_M_QX          0x01                            /* Q indexable */
#define I_M_QM          0x02                            /* Q immediate */
#define I_M_QNP         0x00                            /* Q no print */
#define I_M_QCP         0x04                            /* Q cond print */
#define I_M_QP          0x08                            /* Q print */
#define I_M_PCP         0x00                            /* P cond print */
#define I_M_PP          0x10                            /* P print */
#define I_GETQF(x)      (((x) >> I_V_FL) & 0x03)
#define I_GETQP(x)      (((x) >> I_V_FL) & 0x0C)
#define I_GETPP(x)      (((x) >> I_V_FL) & 0x10)

#define I_2             ((I_M_PP | I_M_QP | I_M_QX) << I_V_FL)
#define I_2M            ((I_M_PP | I_M_QP | I_M_QM) << I_V_FL)
#define I_2X            ((I_M_PP | I_M_QP | I_M_QX | I_M_QM) << I_V_FL)
#define I_2S            ((I_M_PP | I_M_QP) << I_V_FL)
#define I_1             ((I_M_PP | I_M_QCP) << I_V_FL)
#define I_1E            ((I_M_PP | I_M_QNP) << I_V_FL)
#define I_0             ((I_M_PCP | I_M_QCP) << I_V_FL)
#define I_0E            ((I_M_PCP | I_M_QNP) << I_V_FL)

struct opc opcode[] = {
    { "RNTY", 36+I_1E,  100 }, { "RATY", 37+I_1E,  100 },
    { "WNTY", 38+I_1E,  100 }, { "WATY", 39+I_1E,  100 },
    { "DNTY", 35+I_1E,  100 }, { "SPTY", 34+I_0E,  101 },
    { "RCTY", 34+I_0E,  102 }, { "BKTY", 34+I_0E,  103 },
    { "IXTY", 34+I_0E,  104 }, { "TBTY", 34+I_0E,  108 },
    { "RNPT", 36+I_1E,  300 }, { "RAPT", 37+I_1E,  300 },
    { "WNPT", 38+I_1E,  200 }, { "WAPT", 39+I_1E,  200 },
    { "DNPT", 35+I_1E,  200 },
    { "RNCD", 36+I_1E,  500 }, { "RACD", 37+I_1E,  500 },
    { "WNCD", 38+I_1E,  400 }, { "WACD", 39+I_1E,  400 },
    { "DNCD", 35+I_1E,  400 },
    { "PRN",  38+I_1E,  900 }, { "PRNS", 38+I_1E,  901 },
    { "PRA",  39+I_1E,  900 }, { "PRAS", 39+I_1E,  901 },
    { "PRD",  35+I_1E,  900 }, { "PRDS", 35+I_1E,  901 },
    { "SK",   34+I_1E,  701 },
    { "RDGN", 36+I_1E,  700 }, { "CDGN", 36+I_1E,  701 },
    { "RDN",  36+I_1E,  702 }, { "CDN",  36+I_1E,  703 },
    { "RTGN", 36+I_1E,  704 }, { "CTGN", 36+I_1E,  705 },
    { "RTN",  36+I_1E,  706 }, { "CTN",  36+I_1E,  707 },
    { "WDGN", 38+I_1E,  700 }, { "WDN",  38+I_1E,  702 },
    { "WTGN", 38+I_1E,  704 }, { "WTN",  38+I_1E,  706 },
    { "RBPT", 37+I_1E, 3300 }, { "WBPT", 39+I_1E, 3200 },
    { "BC1",  46+I_1E,  100 }, { "BNC1", 47+I_1E,  100 },
    { "BC2",  46+I_1E,  200 }, { "BNC2", 47+I_1E,  200 },
    { "BC3",  46+I_1E,  300 }, { "BNC3", 47+I_1E,  300 },
    { "BC4",  46+I_1E,  400 }, { "BNC4", 47+I_1E,  400 },
    { "BLC",  46+I_1E,  900 }, { "BNLC", 47+I_1E,  900 },
    { "BH",   46+I_1E, 1100 }, { "BNH",  47+I_1E, 1100 },
    { "BP",   46+I_1E, 1100 }, { "BNP",  47+I_1E, 1100 },
    { "BE",   46+I_1E, 1200 }, { "BNE",  47+I_1E, 1200 },
    { "BZ",   46+I_1E, 1200 }, { "BNZ",  47+I_1E, 1200 },
    { "BNL",  46+I_1E, 1300 }, { "BL",   47+I_1E, 1300 },
    { "BNN",  46+I_1E, 1300 }, { "BN",   47+I_1E, 1300 },
    { "BV",   46+I_1E, 1400 }, { "BNV",  47+I_1E, 1400 },
    { "BXV",  46+I_1E, 1500 }, { "BNXV", 47+I_1E, 1500 },
    { "BA",   46+I_1E, 1900 }, { "BNA",  47+I_1E, 1900 },
    { "BNBS", 46+I_1E, 3000 }, { "BEBS", 47+I_1E, 3000 },
    { "BBAS", 46+I_1E, 3100 }, { "BANS", 47+I_1E, 3100 },
    { "BBBS", 46+I_1E, 3200 }, { "BBNS", 47+I_1E, 3200 },
    { "BCH9", 46+I_1E, 3300 },
    { "BCOV", 46+I_1E, 3400 },
    { "BSNX", 60+I_1E,    0 }, { "BSBA", 60+I_1E,    1 },
    { "BSBB", 60+I_1E,    2 },
    { "BSNI", 60+I_1E,    8 }, { "BSIA", 60+I_1E,    9 },

    { "FADD",  1+I_2,  0 }, { "FSUB",  2+I_2,  0 },
    { "FMUL",  3+I_2,  0 }, { "FSL",   5+I_2,  0 },
    { "TFL",   6+I_2,  0 }, { "BTFL",  7+I_2,  0 },
    { "FSR",   8+I_2,  0 }, { "FDIV",  9+I_2,  0 },
    { "BTAM", 10+I_2M, 0 }, { "AM",   11+I_2M, 0 },
    { "SM",   12+I_2M, 0 }, { "MM",   13+I_2M, 0 },
    { "CM",   14+I_2M, 0 }, { "TDM",  15+I_2S, 0 },
    { "TFM",  16+I_2M, 0 }, { "BTM",  17+I_2M, 0 },
    { "LDM",  18+I_2M, 0 }, { "DM",   19+I_2M, 0 },
    { "BTA",  20+I_2,  0 }, { "A",    21+I_2,  0 },
    { "S",    22+I_2,  0 }, { "M",    23+I_2,  0 },
    { "C",    24+I_2,  0 }, { "TD",   25+I_2,  0 },
    { "TF",   26+I_2,  0 }, { "BT",   27+I_2,  0 },
    { "LD",   28+I_2,  0 }, { "D",    29+I_2,  0 },
    { "TRNM", 30+I_2,  0 }, { "TR",   31+I_2,  0 },
    { "SF",   32+I_1,  0 }, { "CF",   33+I_1,  0 },
    { "K",    34+I_2S, 0 }, { "DN",   35+I_2S, 0 },
    { "RN",   36+I_2S, 0 }, { "RA",   37+I_2S, 0 },
    { "WN",   38+I_2S, 0 }, { "WA",   39+I_2S, 0 },
    { "NOP",  41+I_0,  0 }, { "BB",   42+I_0,  0 },
    { "BD",   43+I_2,  0 }, { "BNF",  44+I_2,  0 },
    { "BNR",  45+I_2,  0 }, { "BI",   46+I_2S, 0 },
    { "BNI",  47+I_2S, 0 }, { "H",    48+I_0,  0 },
    { "B",    49+I_1,  0 }, { "BNG",  55+I_2,  0 },
    { "BS",   60+I_2S, 0 }, { "BX",   61+I_2,  0 },
    { "BXM",  62+I_2X, 0 }, { "BCX",  63+I_2,  0 },
    { "BCXM", 64+I_2X, 0 }, { "BLX",  65+I_2,  0 },
    { "BLXM", 66+I_2X, 0 }, { "BSX",  67+I_2,  0 },
    { "MA",   70+I_2,  0 }, { "MF",   71+I_2,  0 },
    { "TNS",  72+I_2,  0 }, { "TNF",  73+I_2,  0 },
    { "BBT",  90+I_2,  0 }, { "BMK",  91+I_2,  0 },
    { "ORF",  92+I_2,  0 }, { "ANDF", 93+I_2,  0 },
    { "CPLF", 94+I_2,  0 }, { "EORF", 95+I_2,  0 },
    { "OTD",  96+I_2,  0 }, { "DTO",  97+I_2,  0 },
    { NULL,   0, 0 }
    };

/* Print an address from five characters */

void fprint_addr (FILE *of, int32 spc, t_value *dig, t_bool flg)
{
int32 i, idx;

fputc (spc, of);                                        /* spacer */
if (dig[ADDR_LEN - 1] & FLAG) {                         /* signed? */
    fputc ('-', of);                                    /* print minus */
    dig[ADDR_LEN - 1] = dig[ADDR_LEN - 1] & ~FLAG;
    }
for (i = 0; i < ADDR_LEN; i++)                          /* print digits */
    fprintf (of, "%X", dig[i] & DIGIT);
if ((cpu_unit.flags & IF_IDX) && flg) {                 /* indexing? */
    for (i = idx = 0; i < ADDR_LEN - 2; i++) {          /* get index reg */
        if (dig[ADDR_LEN - 2 - i] & FLAG)
            idx = idx | (1 << i);
        dig[ADDR_LEN - 2 - i] = dig[ADDR_LEN - 2 - i] & ~FLAG;
        }
    if (idx)                                            /* print */
        fprintf (of, "(%d)", idx);
    }
return;
}

/* Look up an opcode

   Inputs:
        op      =       opcode (decimal)
        qv      =       Q value (full 5 digits)
    Outputs:
        *opcst  =       pointer to opcode string
                        (NULL if not found)
        *fl     =       opcode flags (optional)
*/

const char *opc_lookup (uint32 op, uint32 qv, uint32 *fl)
{
uint32 i, opfl;

for (i = 0; opcode[i].str != NULL; i++) {               /* find opcode */
    opfl = opcode[i].opv & 0xFF0000;                    /* get flags */
    if ((op == (opcode[i].opv & 0xFF)) &&               /* op match? */
        ((qv == opcode[i].qv) ||                        /* q match or */
        ((opfl != I_1E) && (opfl != I_0E)))) {          /* not needed? */
            if (fl != NULL)
                *fl = opfl;
            return opcode[i].str;
        }
    }
return NULL;
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current address
        *val    =       values to decode
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       if >= 0, error code
                        if < 0, number of extra words retired
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 pmp, qmp, i, c, d, any;
uint32 op, qv, opfl;
const char *opstr;

if (uptr == NULL)
    uptr = &cpu_unit;
if (sw & SWMASK ('C')) {                                /* character? */
    if (uptr->flags & UNIT_BCD) {
        if (addr & 1)                                   /* must be even */
            return SCPE_ARG;
        c = ((val[0] & DIGIT) << 4) | (val[1] & DIGIT);
        if (alp_to_cdp[c] > 0)
            fprintf (of, "%c", alp_to_cdp[c]);
        else fprintf (of, "<%02x>", c);
        return -1;
        } 
    else fprintf (of, FMTASC (val[0] & 0177));
    return SCPE_OK;
    }
if ((uptr->flags & UNIT_BCD) == 0)                      /* CPU or disk? */
    return SCPE_ARG;
if (sw & SWMASK ('D')) {                                /* dump? */
    for (i = d = 0; i < LINE_LNT; i++)
        d = d | val[i];
    if (d & FLAG) {                                     /* any flags? */
        for (i = 0; i < LINE_LNT; i++)                  /* print flags */
            fprintf (of, (val[i] & FLAG)? "_": " ");
        fprintf (of, "\n\t");
        }
    for (i = 0; i < LINE_LNT; i++)                      /* print digits */
        fprintf (of, "%X", val[i] & DIGIT) ;
    return -(i - 1);
    }
if (sw & SWMASK ('S')) {                                /* string? */
    if (addr & 1)                                       /* must be even */
        return SCPE_ARG;
    for (i = 0; i < LINE_LNT; i = i + 2) {
        c = ((val[i] & DIGIT) << 4) | (val[i + 1] & DIGIT);
        if (alp_to_cdp[c] < 0)
            break;
        fprintf (of, "%c", alp_to_cdp[c]);
        }
    if (i == 0) {
        fprintf (of, "<%02X>", c);
        return -1;
        }
    return -(i - 1);
    }
if ((sw & SWMASK ('M')) == 0)
    return SCPE_ARG;

if (addr & 1)                                           /* must be even */
    return SCPE_ARG;
op = ((val[0] & DIGIT) * 10) + (val[1] & DIGIT);        /* get opcode */
for (i = qv = pmp = qmp = 0; i < ADDR_LEN; i++) {       /* test addr */
    if (val[I_P + i])
        pmp = 1;
    if (val[I_Q + i])
        qmp = 1;
    qv = (qv * 10) + (val[I_Q + i] & DIGIT);
    }
if ((val[0] | val[1]) & FLAG)                           /* flags force */
    pmp = qmp = 1;
opstr = opc_lookup (op, qv, &opfl);                     /* find opcode */

if (opstr == NULL) {                                    /* invalid opcode */
    if ((sw & SIM_SW_STOP) != 0) {                      /* stop message? */
        fprintf (of, "%02d", op);                       /* print numeric opcode */
        return -(INST_LEN - 1);                         /* report success */
        }
    return SCPE_ARG;
    }
if (I_GETQP (opfl) == I_M_QNP)                          /* Q no print? */
    qmp = 0;

fprintf (of, ((sw & SIM_SW_STOP)? "%s": "%-4s"), opstr);/* print opcode */
if (I_GETPP (opfl) == I_M_PP)                           /* P required? */
    fprint_addr (of, ' ', &val[I_P], I_M_QX);
else if ((I_GETPP (opfl) == I_M_PCP) && (pmp || qmp))   /* P opt & needed? */
    fprint_addr (of, ' ', &val[I_P], 0);
if (I_GETQP (opfl) == I_M_QP) {                         /* Q required? */
    fprint_addr (of, ',', &val[I_Q], I_GETQF (opfl));
    if (I_GETQF (opfl) & I_M_QM)                        /* immediate? */
        val[I_Q] = val[I_Q] & ~FLAG;                    /* clr hi Q flag */
    }
else if ((I_GETQP (opfl) == I_M_QCP) && (pmp || qmp))   /* Q opt & needed? */
    fprint_addr (of, ',', &val[I_Q], 0);
for (i = any = 0; i < INST_LEN; i++) {                  /* print rem flags */
    if (val[i] & FLAG) {
        if (!any)
            fputc (',', of);
        any = 1;
        fprintf (of, "%d", i);
        }
    }
return -(INST_LEN - 1);
}

/* parse_addr - get sign + address + index */

t_stat parse_addr (char *cptr, t_value *val, int32 flg)
{
int32 i, sign = 0, addr, index;
static int32 idx_tst[ADDR_LEN] = { 0, 4, 2, 1, 0 };
char *tptr;

if (*cptr == '+')                                       /* +? skip */
    cptr++;
else if (*cptr == '-') {                                /* -? skip, flag */
    sign = 1;
    cptr++;
    }
errno = 0;                                              /* get address */
addr = strtoul (cptr, &tptr, 16);
if (errno || (cptr == tptr) || (addr > 0xFFFFF))        /* err or too big? */
    return SCPE_ARG;
if ((cpu_unit.flags & IF_IDX) && (flg & I_M_QX) &&      /* index allowed? */
    (*tptr == '(')) {                                   /* index specified */
        errno = 0;
        index = strtoul (cptr = tptr + 1, &tptr, 10);   /* get index */
        if (errno || (cptr == tptr) || (index > 7))     /* err or too big? */
            return SCPE_ARG;
        if (*tptr++ != ')')
            return SCPE_ARG;
        }
else index = 0;
if (*tptr != 0)                                         /* all done? */
    return SCPE_ARG;
for (i = ADDR_LEN - 1; i >= 0; i--) {                   /* cvt addr to dig */
    val[i] = (addr & 0xF) | ((index & idx_tst[i])? FLAG: 0);
    addr = addr >> 4;
    }
if (sign)                                               /* set sign */
    val[ADDR_LEN - 1] = val[ADDR_LEN - 1] | FLAG;
if (flg & I_M_QM)                                       /* set immediate */
    val[0] = val[0] | FLAG;
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
        status  =       > 0   error code
                        <= 0  -number of extra words
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 i, qv, opfl, last;
char la, *fptr, gbuf[CBUFSIZE];
int8 t;

while (isspace (*cptr))                                 /* absorb spaces */
    cptr++;
if ((sw & SWMASK ('C')) || ((*cptr == '\'') && cptr++)) { /* character? */
    if ((t = *cptr & 0x7F) == 0)                        /* get char */
        return SCPE_ARG;
    if (uptr->flags & UNIT_BCD) {                       /* BCD? */
        if (addr & 1)
            return SCPE_ARG;
        t = cdr_to_alp[t];                              /* convert */
        if (t < 0)                                      /* invalid? */
            return SCPE_ARG;
        val[0] = (t >> 4) & DIGIT;                      /* store */
        val[1] = t & DIGIT;
        return -1;
        }
    else val[0] = t;                                    /* store ASCII */
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_BCD) == 0)                      /* CPU or disk? */
    return SCPE_ARG;
if ((sw & SWMASK ('S')) || ((*cptr == '"') && cptr++)) { /* string? */
    if (addr & 1)                                       /* must be even */
        return SCPE_ARG;
    for (i = 0; (i < sim_emax) && (*cptr != 0); i = i + 2) {
        t = *cptr++ & 0x7F;                             /* get character */
        t = cdr_to_alp[t];                              /* convert */
        if (t < 0)                                      /* invalid? */
            return SCPE_ARG;
        val[i] = (t >> 4) & DIGIT;                      /* store */
        val[i + 1] = t & DIGIT;
        }
    if (i == 0)                                         /* final check */
        return SCPE_ARG;
    return -(i - 1);
    }

if (addr & 1)                                           /* even addr? */
    return SCPE_ARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; opcode[i].str != NULL; i++) {               /* look it up */
    if (strcmp (gbuf, opcode[i].str) == 0)
        break;
    }
if (opcode[i].str == NULL)                              /* successful? */
    return SCPE_ARG;
opfl = opcode[i].opv & 0xFF0000;                        /* get flags */
val[0] = (opcode[i].opv & 0xFF) / 10;                   /* store opcode */
val[1] = (opcode[i].opv & 0xFF) % 10;
qv = opcode[i].qv;
for (i = ADDR_LEN - 1; i >= 0; i--) {                   /* set P,Q fields */
    val[I_P + i] = 0;
    val[I_Q + i] = qv % 10;
    qv = qv /10;
    }

cptr = get_glyph (cptr, gbuf, ',');                     /* get P field */
if (gbuf[0]) {                                          /* any? */
    if (parse_addr (gbuf, &val[I_P], (I_GETPP (opfl)? I_M_QX: 0)))
        return SCPE_ARG;
    }
else if (I_GETPP (opfl) == I_M_PP)
    return SCPE_ARG;

if (I_GETQP (opfl) != I_M_QNP) {                        /* Q field allowed? */
    cptr = get_glyph (cptr, gbuf, ',');                 /* get Q field */
    if (gbuf[0]) {                                      /* any? */
        if (parse_addr (gbuf, &val[I_Q], I_GETQF (opfl)))
            return SCPE_ARG;
        }
    else if (I_GETQP (opfl) == I_M_QP)
        return SCPE_ARG;
    }

cptr = get_glyph (cptr, fptr = gbuf, ' ');              /* get flag field */
last = -1;                                              /* none yet */
while ((t = *fptr++)) {                                 /* loop through */
    if ((t < '0') || (t > '9'))                         /* must be digit */
        return SCPE_ARG;
    t = t - '0';                                        /* convert */
    if (t == 1) {                                       /* ambiguous? */
        la = *fptr++;                                   /* get next */
        if (la == '0')                                  /* 10? */
            t = 10;
        else if ((la == '1') && (*fptr == 0))           /* 11 & end field? */
            t = 11;
        else --fptr;                                    /* dont lookahead */
        }
    if (t <= last)                                      /* in order? */
        return SCPE_ARG;
    val[t] = val[t] | FLAG;                             /* set flag */
    last = t;                                           /* continue */
    }

if (*cptr != 0)
    return SCPE_ARG;
return -(INST_LEN - 1);
}
