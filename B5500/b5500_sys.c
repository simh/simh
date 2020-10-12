/* b5500_sys.c: Burroughs 5500 Simulator system interface.

   Copyright (c) 2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sim_defs.h"
#include "b5500_defs.h"
#include "sim_card.h"
#include <ctype.h>

t_stat  parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw);

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char                sim_name[] = "B5500";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &chan_dev,
#if NUM_DEVS_CDR > 0
    &cdr_dev,
#endif
#if NUM_DEVS_CDP > 0
    &cdp_dev,
#endif
#if NUM_DEVS_LPR > 0
    &lpr_dev,
#endif
#if NUM_DEVS_CON > 0
    &con_dev,
#endif
#if NUM_DEVS_MT > 0
    &mt_dev,
#endif
#if NUM_DEVS_DR > 0
    &drm_dev,
#endif
#if NUM_DEVS_DSK > 0
    &esu_dev,
    &dsk_dev,
#endif
#if NUM_DEVS_DTC > 0
    &dtc_dev,
#endif
    NULL
};

/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    0,
};

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD, "Show command execution to devices"},
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
    {0, 0}
};


uint8                parity_table[64] = {
    /* 0    1    2    3    4    5    6    7 */
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0000, 0100, 0100, 0000, 0100, 0000, 0000, 0100,
    0100, 0000, 0000, 0100, 0000, 0100, 0100, 0000
};

uint8           mem_to_ascii[64] = {
   /* x0   x1   x2   x3   x4   x5   x6   x7 */
     '0', '1', '2', '3', '4', '5', '6', '7',     /* 0x */
     '8', '9', '#', '@', '?', ':', '>', '}',     /* 1x */
     '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',     /* 2x */
     'H', 'I', '.', '[', '&', '(', '<', '~',     /* 3x */
     '|', 'J', 'K', 'L', 'M', 'N', 'O', 'P',     /* 4x */
     'Q', 'R', '$', '*', '-', ')', ';', '{',     /* 5x */
     ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',     /* 6x */
     'Y', 'Z', ',', '%', '!', '=', ']', '"'      /* 7x */
};

const char          con_to_ascii[64] = {
    '?', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '#', '@', ':', '>', '}',    /* 17 = box */
    ' ', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', '!', ',', '%', '=', ']', '"',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '|', '$', '*', ')', ';', '{',     /* 57 = triangle */
    '&', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '+', '.', '[', '(', '<', '~',     /* 37 = stop code */
};                              /* 72 = rec mark */
                                /* 75 = squiggle, 77 = del */

const char          ascii_to_con[128] = {
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,     /* 0 - 37 */
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /*sp    !    "    #    $    %    &    ' */
    020, 032, 037, 013, 053, 034, 060, 014,     /* 40 - 77 */
   /* (    )    *    +    ,    -    .    / */
    075, 055, 054, 072, 033, 040, 073, 021,
   /* 0    1    2    3    4    5    6    7 */
    012, 001, 002, 003, 004, 005, 006, 007,
   /* 8    9    :    ;    <    =    >    ? */
    010, 011, 015, 056, 076, 035, 016, 000,
   /* @    A    B    C    D    E    F    G */
    014, 061, 062, 063, 064, 065, 066, 067,     /* 100 - 137 */
   /* H    I    J    K    L    M    N    O */
    070, 071, 041, 042, 043, 044, 045, 046,
   /* P    Q    R    S    T    U    V    W */
    047, 050, 051, 022, 023, 024, 025, 026,
   /* X    Y    Z    [    \    ]    ^    _ */
    027, 030, 031, 074, 036, 036, 057, 000,
   /* `    a    b    c    d    e    f    g */
    035, 061, 062, 063, 064, 065, 066, 067,     /* 140 - 177 */
   /* h    i    j    k    l    m    n    o */
    070, 071, 041, 042, 043, 044, 045, 046,
   /* p    q    r    s    t    u    v    w */
    047, 050, 051, 022, 023, 024, 025, 026,
   /* x    y    z    {    |    }    ~   del*/
    027, 030, 031, 057, 052, 017,  -1,  -1
};


/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
   /* Currently not implimented until I know format of load files */
    return SCPE_NOFNC;
}

#define TYPE_A  1       /* Full 12 bit opcode */
#define TYPE_B  2       /* 6 Bit Opcode with 6 bit field */
#define TYPE_C  3       /* 8 Bit opcode with 4 bit field */
#define TYPE_D  4       /* 2 bit opcode, 10 bit field */

/* Opcodes */
t_opcode  word_ops[] = {
/* Word mode opcodes */
       { WMOP_LITC,      TYPE_D, "LITC",},  /* Load literal */
       { WMOP_OPDC,      TYPE_D, "OPDC",},  /* Load operand */
       { WMOP_DESC,      TYPE_D, "DESC",},  /* Load Descriptor */
       { WMOP_DEL,       TYPE_A, "DEL", },  /* Delete top of stack */
       { WMOP_NOP,       TYPE_A, "NOP", },  /* Nop operation */
       { WMOP_XRT,       TYPE_A, "XRT", },  /* Set Variant */
       { WMOP_ADD,       TYPE_A, "ADD", },  /* Add */
       { WMOP_DLA,       TYPE_A, "DLA", },  /* Double Precision Add */
       { WMOP_PRL,       TYPE_A, "PRL", },  /* Program Release */
       { WMOP_LNG,       TYPE_A, "LNG", },  /* Logical Negate */
       { WMOP_CID,       TYPE_A, "CID", },  /* Conditional Integer Store Destructive */
       { WMOP_GEQ,       TYPE_A, "GEQ", },  /* B greater than or equal to A */
       { WMOP_BBC,       TYPE_A, "BBC", },  /* Branch Backward Conditional */
       { WMOP_BRT,       TYPE_A, "BRT", },  /* Branch Return */
       { WMOP_INX,       TYPE_A, "INX", },  /* Index */
       { WMOP_ITI,       TYPE_A, "ITI", },  /* Interrogate interrupt */
       { WMOP_LOR,       TYPE_A, "LOR", },  /* Logical Or */
       { WMOP_CIN,       TYPE_A, "CIN", },  /* Conditional Integer Store non-destructive */
       { WMOP_GTR,       TYPE_A, "GTR", },  /* B Greater than A */
       { WMOP_BFC,       TYPE_A, "BFC", },  /* Branch Forward Conditional */
       { WMOP_RTN,       TYPE_A, "RTN", },  /* Return normal */
       { WMOP_COC,       TYPE_A, "COC", },  /* Construct Operand Call */
       { WMOP_SUB,       TYPE_A, "SUB", },  /* Subtract */
       { WMOP_DLS,       TYPE_A, "DLS", },  /* Double Precision Subtract */
       { WMOP_MUL,       TYPE_A, "MUL", },  /* Multiply */
       { WMOP_DLM,       TYPE_A, "DLM", },  /* Double Precision Multiply */
       { WMOP_RTR,       TYPE_A, "RTR", },  /* Read Timer */
       { WMOP_LND,       TYPE_A, "LND", },  /* Logical And */
       { WMOP_STD,       TYPE_A, "STD", },  /* B Store Destructive */
       { WMOP_NEQ,       TYPE_A, "NEQ", },  /* B Not equal to A */
       { WMOP_SSN,       TYPE_A, "SSN", },  /* Set Sign Bit */
       { WMOP_XIT,       TYPE_A, "XIT", },  /* Exit */
       { WMOP_MKS,       TYPE_A, "MKS", },  /* Mark Stack */
       { WMOP_DIV,       TYPE_A, "DIV", },  /* Divide */
       { WMOP_DLD,       TYPE_A, "DLD", },  /* Double Precision Divide */
       { WMOP_COM,       TYPE_A, "COM", },  /* Communication operator */
       { WMOP_LQV,       TYPE_A, "LQV", },  /* Logical Equivalence */
       { WMOP_SND,       TYPE_A, "SND", },  /* B Store Non-destructive */
       { WMOP_XCH,       TYPE_A, "XCH", },  /* Exchange */
       { WMOP_CHS,       TYPE_A, "CHS", },  /* Change sign bit */
       { WMOP_RTS,       TYPE_A, "RTS", },  /* Return Special */
       { WMOP_CDC,       TYPE_A, "CDC", },  /* Construct descriptor call */
       { WMOP_FTC,       TYPE_A, "FTC", },  /* Transfer F Field to Core Field */
       { WMOP_MOP,       TYPE_A, "MOP", },  /* Reset Flag bit */
       { WMOP_LOD,       TYPE_A, "LOD", },  /* Load */
       { WMOP_DUP,       TYPE_A, "DUP", },  /* Duplicate */
       { WMOP_TOP,       TYPE_A, "TOP", },  /* Test Flag Bit */
       { WMOP_IOR,       TYPE_A, "IOR", },  /* I/O Release */
       { WMOP_LBC,       TYPE_A, "LBC", },  /* Word Branch Backward Conditional */
       { WMOP_SSF,       TYPE_A, "SSF", },  /* Set or Store S or F registers */
       { WMOP_HP2,       TYPE_A, "HP2", },  /* Halt P2 */
       { WMOP_LFC,       TYPE_A, "LFC", },  /* Word Branch Forward Conditional */
       { WMOP_ZP1,       TYPE_A, "ZP1", },  /* Conditional Halt */
       { WMOP_TUS,       TYPE_A, "TUS", },  /* Interrogate Peripheral Status */
       { WMOP_LLL,       TYPE_A, "LLL", },  /* Link List Look-up */
       { WMOP_IDV,       TYPE_A, "IDV", },  /* Integer Divide Integer */
       { WMOP_SFI,       TYPE_A, "SFI", },  /* Store for Interrupt */
       { WMOP_SFT,       TYPE_A, "SFT", },  /* Store for Test */
       { WMOP_FTF,       TYPE_A, "FTF", },  /* Transfer F Field to F Field */
       { WMOP_MDS,       TYPE_A, "MDS", },  /* Set Flag Bit */
       { WMOP_IP1,       TYPE_A, "IP1", },  /* Initiate P1 */
       { WMOP_ISD,       TYPE_A, "ISD", },  /* Interger Store Destructive */
       { WMOP_LEQ,       TYPE_A, "LEQ", },  /* B Less Than or Equal to A */
       { WMOP_BBW,       TYPE_A, "BBW", },  /* Banch Backward Conditional */
       { WMOP_IP2,       TYPE_A, "IP2", },  /* Initiate P2 */
       { WMOP_ISN,       TYPE_A, "ISN", },  /* Integer Store Non-Destructive */
       { WMOP_LSS,       TYPE_A, "LSS", },  /* B Less Than A */
       { WMOP_BFW,       TYPE_A, "BFW", },  /* Branch Forward Unconditional */
       { WMOP_IIO,       TYPE_A, "IIO", },  /* Initiate I/O */
       { WMOP_EQL,       TYPE_A, "EQL", },  /* B Equal A */
       { WMOP_SSP,       TYPE_A, "SSP", },  /* Reset Sign Bit */
       { WMOP_CMN,       TYPE_A, "CMN", },  /* Enter Character Mode In Line */
       { WMOP_IFT,       TYPE_A, "IFT", },  /* Test Initiate */
       { WMOP_CTC,       TYPE_A, "CTC", },  /* Transfer Core Field to Core Field */
       { WMOP_LBU,       TYPE_A, "LBU", },  /* Word Branch Backward Unconditional */
       { WMOP_LFU,       TYPE_A, "LFU", },  /* Word Branch Forward Unconditional */
       { WMOP_TIO,       TYPE_A, "TIO", },  /* Interrogate I/O Channels */
       { WMOP_RDV,       TYPE_A, "RDV", },  /* Remainder Divide */
       { WMOP_FBS,       TYPE_A, "FBS", },  /* Flag Bit Search */
       { WMOP_CTF,       TYPE_A, "CTF", },  /* Transfer Core Field to F Field */
       { WMOP_ISO,       TYPE_B, "ISO", },  /* Variable Field Isolate XX */
       { WMOP_CBD,       TYPE_C, "CBD", },  /* Non-Zero Field Branch Backward Destructive Xy */
       { WMOP_CBN,       TYPE_C, "CBN", },  /* Non-Zero Field Branch Backward Non-Destructive Xy */
       { WMOP_CFD,       TYPE_C, "CFD", },  /* Non-Zero Field Branch Forward Destructive Xy */
       { WMOP_CFN,       TYPE_B, "CFN", },  /* Non-Zero Field Branch Forward Non-Destructive Xy */
       { WMOP_DIA,       TYPE_B, "DIA", },  /* Dial A XX */
       { WMOP_DIB,       TYPE_B, "DIB", },  /* Dial B XX Upper 6 not Zero */
       { WMOP_TRB,       TYPE_B, "TRB", },  /* Transfer Bits XX */
       { WMOP_FCL,       TYPE_B, "FCL", },  /* Compare Field Low XX */
       { WMOP_FCE,       TYPE_B, "FCE", },  /* Compare Field Equal XX */
       { 0,              0,      NULL,  }
};

t_opcode  char_ops[] = {
/* Character Mode */
       { CMOP_EXC,       TYPE_A, "EXC", },  /* Exit Character Mode */
       { CMOP_CMX,       TYPE_A, "CMX", },  /* Exit Character Mode In Line */
       { CMOP_BSD,       TYPE_B, "BSD", },  /* Skip Bit Destiniation */
       { CMOP_BSS,       TYPE_B, "BSS", },  /* SKip Bit Source */
       { CMOP_RDA,       TYPE_B, "RDA", },  /* Recall Destination Address */
       { CMOP_TRW,       TYPE_B, "TRW", },  /* Transfer Words */
       { CMOP_SED,       TYPE_B, "SED", },  /* Set Destination Address */
       { CMOP_TDA,       TYPE_B, "TDA", },  /* Transfer Destination Address */
       { CMOP_TBN,       TYPE_B, "TBN", },  /* Transfer Blanks for Non-Numerics */
       { WMOP_ITI,       TYPE_A, "ITI", },  /* Interrogate interrupt */
       { WMOP_SFI,       TYPE_A, "SFI", },  /* Store for Interrupt */
       { WMOP_SFT,       TYPE_A, "SFT", },  /* Store for Test */
       { WMOP_ZP1,       TYPE_A, "ZP1", },  /* Conditional Halt */
       { WMOP_HP2,       TYPE_A, "HP2", },  /* Halt P2 */
       { CMOP_SDA,       TYPE_B, "SDA", },  /* Store Destination Address */
       { CMOP_SSA,       TYPE_B, "SSA", },  /* Store Source Address */
       { CMOP_SFD,       TYPE_B, "SFD", },  /* Skip Forward Destination */
       { CMOP_SRD,       TYPE_B, "SRD", },  /* Skip Reverse Destination */
       { CMOP_SES,       TYPE_B, "SES", },  /* Set Source Address */
       { CMOP_TEQ,       TYPE_B, "TEQ", },  /* Test for Equal */
       { CMOP_TNE,       TYPE_B, "TNE", },  /* Test for Not-Equal */
       { CMOP_TEG,       TYPE_B, "TEG", },  /* Test for Greater Or Equal */
       { CMOP_TGR,       TYPE_B, "TGR", },  /* Test For Greater */
       { CMOP_SRS,       TYPE_B, "SRS", },  /* Skip Reverse Source */
       { CMOP_SFS,       TYPE_B, "SFS", },  /* Skip Forward Source */
       { CMOP_TEL,       TYPE_B, "TEL", },  /* Test For Equal or Less */
       { CMOP_TLS,       TYPE_B, "TLS", },  /* Test For Less */
       { CMOP_TAN,       TYPE_B, "TAN", },  /* Test for Alphanumeric */
       { CMOP_BIT,       TYPE_B, "BIT", },  /* Test Bit */
       { CMOP_INC,       TYPE_B, "INC", },  /* Increase Tally */
       { CMOP_STC,       TYPE_B, "STC", },  /* Store Tally */
       { CMOP_SEC,       TYPE_B, "SEC", },  /* Set Tally */
       { CMOP_CRF,       TYPE_B, "CRF", },  /* Call repeat Field */
       { CMOP_JNC,       TYPE_B, "JNC", },  /* Jump Out Of Loop Conditional */
       { CMOP_JFC,       TYPE_B, "JFC", },  /* Jump Forward Conditional */
       { CMOP_JNS,       TYPE_B, "JNS", },  /* Jump out of loop unconditional */
       { CMOP_JFW,       TYPE_B, "JFW", },  /* Jump Forward Unconditional */
       { CMOP_RCA,       TYPE_B, "RCA", },  /* Recall Control Address */
       { CMOP_ENS,       TYPE_B, "ENS", },  /* End Loop */
       { CMOP_BNS,       TYPE_B, "BNS", },  /* Begin Loop */
       { CMOP_RSA,       TYPE_B, "RSA", },  /* Recall Source Address */
       { CMOP_SCA,       TYPE_B, "SCA", },  /* Store Control Address */
       { CMOP_JRC,       TYPE_B, "JRC", },  /* Jump Reverse Conditional */
       { CMOP_TSA,       TYPE_B, "TSA", },  /* Transfer Source Address */
       { CMOP_JRV,       TYPE_B, "JRV", },  /* Jump Reverse Unconditional */
       { CMOP_CEQ,       TYPE_B, "CEQ", },  /* Compare Equal */
       { CMOP_CNE,       TYPE_B, "CNE", },  /* COmpare for Not Equal */
       { CMOP_CEG,       TYPE_B, "CEG", },  /* Compare For Greater Or Equal */
       { CMOP_CGR,       TYPE_B, "CGR", },  /* Compare For Greater */
       { CMOP_BIS,       TYPE_B, "BIS", },  /* Set Bit */
       { CMOP_BIR,       TYPE_B, "BIR", },  /* Reet Bit */
       { CMOP_OCV,       TYPE_B, "OCV", },  /* Output Convert */
       { CMOP_ICV,       TYPE_B, "ICV", },  /* Input Convert */
       { CMOP_CEL,       TYPE_B, "CEL", },  /* Compare For Equal or Less */
       { CMOP_CLS,       TYPE_B, "CLS", },  /* Compare for Less */
       { CMOP_FSU,       TYPE_B, "FSU", },  /* Field Subtract */
       { CMOP_FAD,       TYPE_B, "FAD", },  /* Field Add */
       { CMOP_TRP,       TYPE_B, "TRP", },  /* Transfer Program Characters */
       { CMOP_TRN,       TYPE_B, "TRN", },  /* Transfer Numeric */
       { CMOP_TRZ,       TYPE_B, "TRZ", },  /* Transfer Zones */
       { CMOP_TRS,       TYPE_B, "TRS", },  /* Transfer Source Characters */
       { 0,              0,      NULL,  }
};


/* Print out an instruction */
void
print_opcode(FILE * of, uint16 val, int chr_mode)
{
    uint16      op;
    t_opcode   *tab = (chr_mode) ? char_ops: word_ops;

    op = val;
    while (tab->name != NULL) {
        switch(tab->type) {
        case TYPE_A:
                if (op != tab->op)
                   break;
                fputs(tab->name, of);
                fputs("       ",of);
                return;
        case TYPE_B:
                if ((op & 077) != tab->op)
                   break;
                fputs(tab->name, of);
                fputc(' ',of);
                fputc(' ',of);
                fprint_val(of, (val >> 6), 8, 6, 0);
                fputs("   ",of);
                return;
        case TYPE_C:
                if ((op & 0377) != tab->op)
                   break;
                fputs(tab->name, of);
                fputc(' ',of);
                fprint_val(of, (val >> 8), 8, 4, 0);
                fputs("   ",of);
                return;
        case TYPE_D:
                if ((op & 03) != tab->op)
                   break;
                fputs(tab->name, of);
                fputc(' ',of);
                fprint_val(of, (val >> 2), 8, 10, 0);
                fputc(' ',of);
                return;
        }
        tab++;
    }
    fprintf(of, "*%04o uuo ", op);
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

t_stat
fprint_sym(FILE * of, t_addr addr, t_value * val, UNIT * uptr, int32 sw)
{
    t_value             inst = *val;
    int                 i;

    fputc(' ', of);
    fprint_val(of, inst, 8, 48, PV_RZRO);

    if (sw & SWMASK('W')) {     /* Word mode opcodes */
        fputs("   ", of);
        for (i = 36; i >= 0; i-=12) {
                uint16     op = (uint16)(inst >> i) & 07777;
                print_opcode(of, op, 0);
        }
    }
    if (sw & SWMASK('C')) {     /* Char mode opcodes */
        fputs("   ", of);
        for (i = 36; i >= 0; i-=12) {
                uint16     op = (uint16)(inst >> i) & 07777;
                print_opcode(of, op, 1);
        }
    }
    if (sw & SWMASK('B')) {     /* BCD mode */
        fputs("   '", of);
        for (i = 42; i >= 0; i-=6) {
            int                 ch;

            ch = (int)(inst >> i) & 077;
            fputc(mem_to_ascii[ch], of);
        }
        fputc('\'', of);
    }
    if (sw & SWMASK('F')) {     /* Floating point/Descriptor */
    }
    return SCPE_OK;
}

t_opcode           *
find_opcode(char *op, t_opcode * tab)
{
    while (tab->name != NULL) {
        if (*tab->name != '\0' && strcmp(op, tab->name) == 0)
            return tab;
        tab++;
    }
    return NULL;
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

t_stat
parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw)
{
    int                 i;
    t_value             d;
    int                 opr;
    char                opcode[100];

    while (isspace(*cptr))
        cptr++;
    d = 0;
    if (sw & (SWMASK('W')|SWMASK('C'))) {
        t_opcode           *op;

        /* Grab opcode */
        cptr = get_glyph(cptr, opcode, 0);

        op = 0;
        opr = -1;
        if((op = find_opcode(opcode,
                (SWMASK('W') ? word_ops : char_ops))) == 0) {
            return SCPE_UNK;
        }

        while (*cptr == ' ' || *cptr == '\t')
            cptr++;
        /* Collect first argument if there is one */
        while (*cptr >= '0' && *cptr <= '7')
            opr = (opr << 3) + (*cptr++ - '0');
        /* Skip blanks */
        while (*cptr == ' ' || *cptr == '\t')
            cptr++;
        switch (op->type) {
        case TYPE_A:
                if (opr >= 0)
                   return SCPE_2MARG;
                *val = op->op;
                return SCPE_OK;

        case TYPE_B:
                if (opr < 0 || opr > 64)
                   return SCPE_ARG;
                *val = (opr << 6) | op->op;
                return SCPE_OK;

        case TYPE_C:
                if (opr < 0 || opr > 16)
                   return SCPE_ARG;
                *val = (opr << 8) | op->op;
                return SCPE_OK;

        case TYPE_D:
                if (opr < 0 || opr > 1024)
                   return SCPE_ARG;
                *val = (opr << 2) | op->op;
                return SCPE_OK;
        }
    } else if (sw & SWMASK('B')) {
        i = 0;
        while (*cptr != '\0' && i < 8) {
            d <<= 6;
            d |= (int)sim_ascii_to_six[0177 & *cptr];
            cptr++;
            i++;
        }
        d <<= 6 * (8 - i);
    } else {
        while (*cptr >= '0' && *cptr <= '7') {
            d <<= 3;
            d |= *cptr++ - '0';
        }
    }
    *val = d;
    return SCPE_OK;
}
