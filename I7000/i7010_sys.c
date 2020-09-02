/* i7010_sys.c: IBM 7010 Simulator system interface.

   Copyright (c) 2005-2016, Richard Cornwell

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

#include "i7010_defs.h"
#include "sim_card.h"
#include <ctype.h>

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char                sim_name[] = "IBM 7010";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 50;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &chan_dev,
#ifdef NUM_DEVS_CDR
    &cdr_dev,
#endif
#ifdef NUM_DEVS_CDP
    &cdp_dev,
#endif
#ifdef STACK_DEV
    &stack_dev,
#endif
#ifdef NUM_DEVS_LPR
    &lpr_dev,
#endif
#ifdef NUM_DEVS_CON
    &con_dev,
#endif
#if NUM_DEVS_MT > 0
    &mta_dev,
#if NUM_DEVS_MT > 1
    &mtb_dev,
#if NUM_DEVS_MT > 2
    &mtc_dev,
#endif
#endif
#endif
#ifdef NUM_DEVS_HD
    &hsdrm_dev,
#endif
#ifdef NUM_DEVS_DR
    &drm_dev,
#endif
#ifdef NUM_DEVS_DSK
    &dsk_dev,
#endif
#ifdef NUM_DEVS_COM
    &coml_dev,
    &com_dev,
#endif
#ifdef NUM_DEVS_CHRON
    &chron_dev,
#endif
    NULL
};

/* Device addressing words */
#ifdef NUM_DEVS_CDP
DIB  cdp_dib = { CH_TYP_UREC, 1, 00400, 07700, &cdp_cmd, &cdp_ini };
#endif
#ifdef NUM_DEVS_CDR
DIB  cdr_dib = { CH_TYP_UREC, 1, 00100, 07700, &cdr_cmd, NULL };
#endif
#ifdef NUM_DEVS_LPR
DIB  lpr_dib = { CH_TYP_UREC, 1, 00200, 07700, &lpr_cmd, &lpr_ini };
#endif
#ifdef NUM_DEVS_CON
DIB  con_dib = { CH_TYP_UREC, 1, 02300, 07700, &con_cmd, &con_ini };
#endif
#ifdef NUM_DEVS_MT
DIB  mt_dib = { CH_TYP_UREC, NUM_UNITS_MT, 02400, 07700, &mt_cmd, &mt_ini };
#endif
#ifdef NUM_DEVS_CHRON
DIB  chron_dib = { CH_TYP_UREC, 1, 02400, 07700, &chron_cmd, NULL };
#endif
#ifdef NUM_DEVS_DSK
DIB  dsk_dib = { CH_TYP_79XX|CH_TYP_UREC, 0, 06600, 07700, &dsk_cmd, &dsk_ini };
#endif
#ifdef NUM_DEVS_COM
DIB  com_dib = { CH_TYP_79XX|CH_TYP_UREC, 0, 04200, 07700, &com_cmd, NULL };
#endif


/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "IO device not ready",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "Error1",   /* Ind limit */ /* Not on 7010 */
    "Error2",   /* XEC limit */ /* Not on 7010 */
    "I/O Check opcode",
    "Error3",   /* MM in trap */ /* Not on 7010 */
    "7750 invalid line number",
    "7750 invalid message",
    "7750 No free output buffers",
    "7750 No free input buffers",
    "Error4",   /* Field overflow */ /* Not on 7010 */
    "Error5",   /* Sign change */ /* Not on 7010 */
    "Divide error",
    "Error6",   /* Alpha index */ /* Not on 7010 */
    "No word mark",
    "Invalid Address",
    "Invalid Lenght Instruction",
    "Program Check",
    "Protect Check",
     0,
};

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CHANNEL", DEBUG_CHAN},
    {"TRAP", DEBUG_TRAP},
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"SENSE", DEBUG_SNS},
    {0, 0}
};

DEBTAB              crd_debug[] = {
    {"CHAN", DEBUG_CHAN},
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"CARD", DEBUG_CARD},
    {0, 0}
};


/* Character conversion tables */
const char          ascii_to_six[128] = {
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,     /* 0 - 37 */
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /* Control                              */
     -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
   /*sp    !    "    #    $    %    &    ' */
    000, 052,  -1, 032, 053, 017, 060, 014,     /* 40 - 77 */
   /* (    )    *    +    ,    -    .    / */
    034, 074, 054, 060, 033, 040, 073, 021,
   /* 0    1    2    3    4    5    6    7 */
    012, 001, 002, 003, 004, 005, 006, 007,
   /* 8    9    :    ;    <    =    >    ? */
    010, 011, 015, 056, 076, 013, 016, 032,
   /* @    A    B    C    D    E    F    G */
    014, 061, 062, 063, 064, 065, 066, 067,     /* 100 - 137 */
   /* H    I    J    K    L    M    N    O */
    070, 071, 041, 042, 043, 044, 045, 046,
   /* P    Q    R    S    T    U    V    W */
    047, 050, 051, 022, 023, 024, 025, 026,
   /* X    Y    Z    [    \    ]    ^    _ */
    027, 030, 031, 075, 036, 055, 057, 020,
   /* `    a    b    c    d    e    f    g */
    035, 061, 062, 063, 064, 065, 066, 067,     /* 140 - 177 */
   /* h    i    j    k    l    m    n    o */
    070, 071, 041, 042, 043, 044, 045, 046,
   /* p    q    r    s    t    u    v    w */
    047, 050, 051, 022, 023, 024, 025, 026,
   /* x    y    z    {    |    }    ~   del*/
    027, 030, 031, 057, 077, 017,  -1,  -1
};


const char          mem_to_ascii[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '=', '\'', ':', '>', 's',
    'b', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'x', ',', '(', '`', '\\', '_',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '^',
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '|',
                      /*Sq*/          /*RM*/
};


/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    return SCPE_NOFNC;
}

/* Symbol tables */
typedef struct _opcode
{
    uint16              opbase;
    const char         *name;
    uint8               type;
}
t_opcode;

#define TYPE_0  0               /* no operands, no options */
#define TYPE_1  1               /* One operand, no options */
#define TYPE_2  2               /* Two operands, no options */
#define TYPE_T  4               /* Tape opcode, option */
#define TYPE_B  5               /* Branch, one operand, option */
#define TYPE_BE 6               /* Branch, three operands */
#define TYPE_BZ 7               /* Branch, three operands */
#define TYPE_CC 8               /* Carrage control */
#define TYPE_IO 9               /* IO select, address, option */
#define TYPE_Y  10              /* one operand, option */
#define TYPE_M  11              /* Move type, two ops, mod */
#define TYPE_BS 12              /* One operand, print mod */

#define MOD(x)  (x<<6)
t_opcode            ops_1401[] = {
    {CHR_A,             "A",            TYPE_2},
    {OP_B|MOD(CHR_9),   "BC9",          TYPE_B},
    {OP_B|MOD(CHR_QUOT),"BCV",          TYPE_B},
    {OP_B|MOD(CHR_Z),   "BAV",          TYPE_B},
    {OP_B|MOD(CHR_S),   "BE",           TYPE_B},
    {OP_B|MOD(CHR_T),   "BH",           TYPE_B},
    {OP_B|MOD(CHR_U),   "BL",           TYPE_B},
    {OP_B|MOD(CHR_SLSH),"BU",           TYPE_B},
    {OP_B|MOD(CHR_A),   "BLC",          TYPE_B},
    {OP_B|MOD(CHR_B),   "BSS",          TYPE_BS},
    {OP_B|MOD(CHR_C),   "BSS",          TYPE_BS},
    {OP_B|MOD(CHR_D),   "BSS",          TYPE_BS},
    {OP_B|MOD(CHR_E),   "BSS",          TYPE_BS},
    {OP_B|MOD(CHR_F),   "BSS",          TYPE_BS},
    {OP_B|MOD(CHR_K),   "BEF",          TYPE_B},
    {OP_B|MOD(CHR_L),   "BER",          TYPE_B},
    {OP_B|MOD(CHR_P),   "BPB",          TYPE_B},
    {OP_B|MOD(CHR_N),   "BIN",          TYPE_BS},
    {OP_B|MOD(CHR_I),   "BIN",          TYPE_BS},
    {OP_B|MOD(CHR_RM),  "BIN",          TYPE_BS},
    {OP_B|MOD(CHR_V),   "BIN",          TYPE_BS},
    {OP_B|MOD(CHR_W),   "BIN",          TYPE_BS},
    {OP_B|MOD(CHR_X),   "BIN",          TYPE_BS},
    {OP_B|MOD(CHR_Z),   "BIN",          TYPE_BS},
    {OP_B|MOD(CHR_STAR),"BIN",          TYPE_BS},
    {OP_B|MOD(CHR_QUEST),"BIN",         TYPE_BS},
    {OP_B|MOD(CHR_RPARN),"BIN",         TYPE_BS},
    {OP_B|MOD(CHR_9),   "BPCB",         TYPE_B},
    {OP_B,              "B",            TYPE_BS},
    {OP_BCE,            "BCE",          TYPE_BE},
    {OP_BBE,            "BBE",          TYPE_BE},
    {OP_BWE,            "BWZ",          TYPE_BE},
    {OP_CC1,            "CC",           TYPE_CC},
    {OP_CS,             "CS",           TYPE_2},
    {OP_CWM,            "CW",           TYPE_2},
    {OP_C,              "C",            TYPE_2},
    {OP_D,              "D",            TYPE_2},
    {OP_M,              "M",            TYPE_2},
    {OP_H,              "H",            TYPE_1},
    {CHR_M,             "MLC",          TYPE_IO},
    {CHR_P,             "MRCM",         TYPE_2},
    {CHR_Z,             "MCS",          TYPE_2},
    {CHR_Y,             "MLZS",         TYPE_2},
    {CHR_E,             "MCE",          TYPE_2},
    {CHR_D,             "MLNS",         TYPE_2},
    {CHR_L,             "MLCWA",        TYPE_IO},
    {CHR_Q,             "SAR",          TYPE_2},
    {CHR_H,             "SBR",          TYPE_2},
    {CHR_1,             "R",            TYPE_1},
    {CHR_2|07400,       "WM",           TYPE_Y},
    {CHR_2,             "W",            TYPE_1},
    {CHR_3,             "WR",           TYPE_1},
    {CHR_4,             "P",            TYPE_1},
    {CHR_5,             "RP",           TYPE_1},
    {CHR_6,             "WP",           TYPE_1},
    {CHR_7,             "WRP",          TYPE_1},
    {CHR_EQ,            "MA",           TYPE_2},
    {OP_NOP,            "NOP",          TYPE_0},
    {OP_SWM,            "SW",           TYPE_2},
    {OP_UC|06100,       "SKF",          TYPE_T},
    {OP_UC|06200,       "BSP",          TYPE_T},
    {OP_UC|06500,       "SKP",          TYPE_T},
    {OP_UC|05100,       "RWD",          TYPE_T},
    {OP_UC|02400,       "RUN",          TYPE_T},
    {OP_UC|04400,       "WTM",          TYPE_T},
    {OP_UC,             "UC",           TYPE_IO},
    {OP_S,              "S",            TYPE_2},
    {OP_SSF1,           "SSF1",         TYPE_CC},
    {OP_SSF2,           "SSF2",         TYPE_CC},
    {OP_ZA,             "ZA",           TYPE_2},
    {OP_ZS,             "ZS",           TYPE_2},
    {0,                 NULL,           TYPE_BE},
};

/* Opcodes */
t_opcode            base_ops[] = {
    {OP_IO1|MOD(077),   "BA1",          TYPE_B},
    {OP_IO1|MOD(001),   "BNR1",         TYPE_B},
    {OP_IO1|MOD(002),   "BCB1",         TYPE_B},
    {OP_IO1|MOD(004),   "BER1",         TYPE_B},
    {OP_IO1|MOD(010),   "BEF1",         TYPE_B},
    {OP_IO1|MOD(020),   "BNT1",         TYPE_B},
    {OP_IO1|MOD(040),   "BWL1",         TYPE_B},
    {OP_IO1|MOD(000),   "BEX1",         TYPE_B},
    {OP_IO1|MOD(000),   "BEX1",         TYPE_BE},
    {OP_IO2|MOD(077),   "BA2",          TYPE_B},
    {OP_IO2|MOD(001),   "BNR2",         TYPE_B},
    {OP_IO2|MOD(002),   "BCB2",         TYPE_B},
    {OP_IO2|MOD(004),   "BER2",         TYPE_B},
    {OP_IO2|MOD(010),   "BEF2",         TYPE_B},
    {OP_IO2|MOD(020),   "BNT2",         TYPE_B},
    {OP_IO2|MOD(040),   "BWL2",         TYPE_B},
    {OP_IO2|MOD(000),   "BEX2",         TYPE_B},
    {OP_IO2|MOD(000),   "BEX2",         TYPE_BE},
    {OP_IO3|MOD(077),   "BA3",          TYPE_B},
    {OP_IO3|MOD(001),   "BNR3",         TYPE_B},
    {OP_IO3|MOD(002),   "BCB3",         TYPE_B},
    {OP_IO3|MOD(004),   "BER3",         TYPE_B},
    {OP_IO3|MOD(010),   "BEF3",         TYPE_B},
    {OP_IO3|MOD(020),   "BNT3",         TYPE_B},
    {OP_IO3|MOD(040),   "BWL3",         TYPE_B},
    {OP_IO3|MOD(000),   "BEX3",         TYPE_B},
    {OP_IO3|MOD(000),   "BEX3",         TYPE_BE},
    {OP_IO4|MOD(077),   "BA4",          TYPE_B},
    {OP_IO4|MOD(001),   "BNR4",         TYPE_B},
    {OP_IO4|MOD(002),   "BCB4",         TYPE_B},
    {OP_IO4|MOD(004),   "BER4",         TYPE_B},
    {OP_IO4|MOD(010),   "BEF4",         TYPE_B},
    {OP_IO4|MOD(020),   "BNT4",         TYPE_B},
    {OP_IO4|MOD(040),   "BWL4",         TYPE_B},
    {OP_IO4|MOD(000),   "BEX4",         TYPE_B},
    {OP_IO4|MOD(000),   "BEX4",         TYPE_BE},
    {OP_A,              "A",            TYPE_2},
    {OP_BBE,            "BBE",          TYPE_BE},
    {OP_BCE,            "BCE",          TYPE_BE},
    {OP_B|04100,        "BPCB",         TYPE_B},
    {OP_B|04300,        "BPCB2",        TYPE_B},
    {OP_B|01000,        "BC9",          TYPE_B},
    {OP_B|05200,        "BC92",         TYPE_B},
    {OP_B|03200,        "BCV",          TYPE_B},
    {OP_B|07400,        "BCV2",         TYPE_B},
    {OP_B|03100,        "BAV",          TYPE_B},
    {OP_B|02200,        "BE",           TYPE_B},
    {OP_B|02400,        "BH",           TYPE_B},
    {OP_B|02300,        "BL",           TYPE_B},
    {OP_B|02100,        "BU",           TYPE_B},
    {OP_B|02600,        "BDV",          TYPE_B},
    {OP_B|05000,        "BNQ",          TYPE_B},
    {OP_B|05400,        "BNQ2",         TYPE_B},
    {OP_B|00100,        "BOL1",         TYPE_B},
    {OP_B|00200,        "BOL2",         TYPE_B},
    {OP_B|00300,        "BOL3",         TYPE_B},
    {OP_B|00400,        "BOL4",         TYPE_B},
    {OP_B|04200,        "BTI",          TYPE_B},
    {OP_B|02500,        "BZ",           TYPE_B},
    {OP_B|02700,        "BXO",          TYPE_B},
    {OP_B|03000,        "BXU",          TYPE_B},
    {OP_BWE|00100,      "BW",           TYPE_BZ},
    {OP_BWE|00300,      "BWZ",          TYPE_BZ},
    {OP_BWE|00200,      "BZN",          TYPE_BZ},
    {OP_BWE|00000,      "BWE",          TYPE_Y},
    {OP_B,              "B",            TYPE_B},
    {OP_B,              "JIO",          TYPE_Y},
    {OP_CC1,            "CC1",          TYPE_CC},
    {OP_CC2,            "CC2",          TYPE_CC},
    {OP_CS,             "CS",           TYPE_2},
    {OP_CWM,            "CW",           TYPE_2},
    {OP_C,              "C",            TYPE_2},
    {OP_D,              "D",            TYPE_2},
    {OP_H,              "H",            TYPE_1},
    {OP_T|00200,        "LE",           TYPE_B},
    {OP_T|00600,        "LEH",          TYPE_B},
    {OP_T|00400,        "LH",           TYPE_B},
    {OP_T|00100,        "LL",           TYPE_B},
    {OP_T|00300,        "LLE",          TYPE_B},
    {OP_T|00500,        "LLH",          TYPE_B},
    {OP_T|00700,        "LA",           TYPE_B},
    {OP_T|00000,        "L",            TYPE_B},
    {OP_MSZ,            "MCS",          TYPE_2},
    {OP_E,              "MCE",          TYPE_2},
    {OP_M,              "M",            TYPE_2},
    {OP_MOV|00100,      "MLNS",         TYPE_M},
    {OP_MOV|00200,      "MLZS",         TYPE_M},
    {OP_MOV|00300,      "MLCS",         TYPE_M},
    {OP_MOV|00400,      "MLWS",         TYPE_M},
    {OP_MOV|00500,      "MLNWS",        TYPE_M},
    {OP_MOV|00600,      "MLZWS",        TYPE_M},
    {OP_MOV|00700,      "MLCWS",        TYPE_M},
    {OP_MOV|01000,      "SCNR",         TYPE_M},
    {OP_MOV|01100,      "MRN",          TYPE_M},
    {OP_MOV|01200,      "MRZ",          TYPE_M},
    {OP_MOV|01300,      "MRC",          TYPE_M},
    {OP_MOV|01400,      "MRW",          TYPE_M},
    {OP_MOV|01500,      "MRNW",         TYPE_M},
    {OP_MOV|01600,      "MRZW",         TYPE_M},
    {OP_MOV|01700,      "MRCW",         TYPE_M},
    {OP_MOV|02000,      "SCNLA",        TYPE_M},
    {OP_MOV|02100,      "MLNA",         TYPE_M},
    {OP_MOV|02200,      "MLZA",         TYPE_M},
    {OP_MOV|02300,      "MLCA",         TYPE_M},
    {OP_MOV|02400,      "MLWA",         TYPE_M},
    {OP_MOV|02500,      "MLNWA",        TYPE_M},
    {OP_MOV|02600,      "MLZWA",        TYPE_M},
    {OP_MOV|02700,      "MLCWA",        TYPE_M},
    {OP_MOV|03000,      "SCNRR",        TYPE_M},
    {OP_MOV|03100,      "MRNR",         TYPE_M},
    {OP_MOV|03200,      "MRZR",         TYPE_M},
    {OP_MOV|03300,      "MRCR",         TYPE_M},
    {OP_MOV|03400,      "MRWR",         TYPE_M},
    {OP_MOV|03500,      "MRNWR",        TYPE_M},
    {OP_MOV|03600,      "MRZWR",        TYPE_M},
    {OP_MOV|03700,      "MRCWR",        TYPE_M},
    {OP_MOV|04000,      "SCNLB",        TYPE_M},
    {OP_MOV|04100,      "MLNB",         TYPE_M},
    {OP_MOV|04200,      "MLZB",         TYPE_M},
    {OP_MOV|04300,      "MLCB",         TYPE_M},
    {OP_MOV|04400,      "MLWB",         TYPE_M},
    {OP_MOV|04500,      "MLNWB",        TYPE_M},
    {OP_MOV|04600,      "MLZWB",        TYPE_M},
    {OP_MOV|04700,      "MLCWB",        TYPE_M},
    {OP_MOV|05000,      "SCNRG",        TYPE_M},
    {OP_MOV|05100,      "MRNG",         TYPE_M},
    {OP_MOV|05200,      "MRZG",         TYPE_M},
    {OP_MOV|05300,      "MRCG",         TYPE_M},
    {OP_MOV|05400,      "MRWG",         TYPE_M},
    {OP_MOV|05500,      "MRNWG",        TYPE_M},
    {OP_MOV|05600,      "MRZWG",        TYPE_M},
    {OP_MOV|05700,      "MRCWG",        TYPE_M},
    {OP_MOV|06000,      "SCNL",         TYPE_M},
    {OP_MOV|06100,      "MLN",          TYPE_M},
    {OP_MOV|06200,      "MLZ",          TYPE_M},
    {OP_MOV|06300,      "MLC",          TYPE_M},
    {OP_MOV|06400,      "MLW",          TYPE_M},
    {OP_MOV|06500,      "MLNW",         TYPE_M},
    {OP_MOV|06600,      "MLZW",         TYPE_M},
    {OP_MOV|06700,      "MLCW",         TYPE_M},
    {OP_MOV|07000,      "SCNRM",        TYPE_M},
    {OP_MOV|07100,      "MRNM",         TYPE_M},
    {OP_MOV|07200,      "MRZM",         TYPE_M},
    {OP_MOV|07300,      "MRCM",         TYPE_M},
    {OP_MOV|07400,      "MRWM",         TYPE_M},
    {OP_MOV|07500,      "MRNWM",        TYPE_M},
    {OP_MOV|07600,      "MRZWM",        TYPE_M},
    {OP_MOV|07700,      "MRCWM",        TYPE_M},
    {OP_MOV|00000,      "SCNLS",        TYPE_M},
    {OP_NOP,            "NOP",          TYPE_0},
    {OP_SWM,            "SW",           TYPE_2},
    {OP_UC|06100,       "SKF",          TYPE_T},
    {OP_UC|06200,       "BSP",          TYPE_T},
    {OP_UC|06500,       "SKP",          TYPE_T},
    {OP_UC|05100,       "RWD",          TYPE_T},
    {OP_UC|02400,       "RUN",          TYPE_T},
    {OP_UC|04400,       "WTM",          TYPE_T},
    {OP_SAR|06100,      "SAR",          TYPE_1},
    {OP_SAR|06200,      "SBR",          TYPE_1},
    {OP_SAR|06500,      "SER",          TYPE_1},
    {OP_SAR|06600,      "SFR",          TYPE_1},
    {OP_SAR|06700,      "SGR",          TYPE_1},
    {OP_SAR|07000,      "SHR",          TYPE_1},
    {OP_SAR|02300,      "STC",          TYPE_1},
    {OP_S,              "S",            TYPE_2},
    {OP_SSF1,           "SSF1",         TYPE_CC},
    {OP_SSF2,           "SSF2",         TYPE_CC},
    {OP_ZA,             "ZA",           TYPE_2},
    {OP_ZS,             "ZS",           TYPE_2},
    {OP_RD|00000,       "MU",           TYPE_IO},
    {OP_RDW|00000,      "LU",           TYPE_IO},
    {OP_STS|00000,      "STATS",        TYPE_Y},
    {OP_FP|05100,       "FRA",          TYPE_B},
    {OP_FP|04300,       "FST",          TYPE_B},
    {OP_FP|06100,       "FA",           TYPE_B},
    {OP_FP|02200,       "FS",           TYPE_B},
    {OP_FP|04400,       "FM",           TYPE_B},
    {OP_FP|06400,       "FD",           TYPE_B},
    {OP_FP|00000,       "FP",           TYPE_Y},
    {OP_PRI|02400,      "BUPR1",        TYPE_B},
    {OP_PRI|06600,      "BUPR2",        TYPE_B},
    {OP_PRI|00100,      "BOPR1",        TYPE_B},
    {OP_PRI|00200,      "BOPR2",        TYPE_B},
    {OP_PRI|00300,      "BOPR3",        TYPE_B},
    {OP_PRI|00400,      "BOPR4",        TYPE_B},
    {OP_PRI|05000,      "BIPR1",        TYPE_B},
    {OP_PRI|05500,      "BIPR2",        TYPE_B},
    {OP_PRI|04500,      "BQPR1",        TYPE_B},
    {OP_PRI|03200,      "BQPR2",        TYPE_B},
    {OP_PRI|02200,      "BSPR1",        TYPE_B},
    {OP_PRI|02300,      "BSPR2",        TYPE_B},
    {OP_PRI|03000,      "BSPR3",        TYPE_B},
    {OP_PRI|03400,      "BSPR4",        TYPE_B},
    {OP_PRI|02700,      "BXPA",         TYPE_B},
    {OP_PRI|06500,      "BEPA",         TYPE_B},
    {OP_PRI|06100,      "BXPR1",        TYPE_B},
    {OP_PRI|06200,      "BXPR2",        TYPE_B},
    {OP_PRI|06300,      "BXPR3",        TYPE_B},
    {OP_PRI|06400,      "BXPR4",        TYPE_B},
    {OP_PRI|00000,      "BPI",          TYPE_Y},
    {0,                 NULL,           TYPE_BE},
};

const char *chname[] = {
    "*", "1", "2", "3", "4"
};


/* Print out a address plus index */
t_stat fprint_addr (FILE *of, uint32 addr) {
    int i;
    int reg;

    reg = ((addr >> 10) & 03) | ((addr >> 14) & 014);
    addr &= 07777171777; /* Mask register bits */
    for(i = 24; i>=0; i -= 6)
        fputc(mem_to_ascii[(addr >> i) & 077], of);
    if (reg != 0)
        fprintf(of, "+X%d", reg);
    return SCPE_OK;
}

/* Print out a 1401 address plus index */
t_stat fprint_addr_1401 (FILE *of, uint32 addr) {
    int reg;
    int v = 0;

    if ((addr & 0170000) != 0120000)
        v += ((addr >> 12) & 017) * 100;
    if ((addr & 01700) != 01200)
        v += ((addr >> 6) & 017) * 10;
    if ((addr & 017) != 012)
        v += addr & 017;
    v += ((addr & 0600000) >> 16) * 1000;
    v += ((addr & 060) >> 4) * 4000;
    reg = (addr >> 10) & 03;
    fprintf(of, "%d", v);
    if (reg != 0)
        fprintf(of, "+X%d", reg);
    return SCPE_OK;
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

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
int32   i, t;
uint32  a, b;
uint8   op, mod, flags;

if (sw & SWMASK ('C')) {                                /* character? */
    t = val[0];
    fprintf (of, (t & WM)? "`%c<%02o> ": " %c<%02o> ", mem_to_ascii[t & 077],
                         t & 077);
    return SCPE_OK;
    }
if ((uptr != NULL) && (uptr != &cpu_unit)) return SCPE_ARG;     /* CPU? */
if (sw & SWMASK ('D')) {                                /* dump? */
    for (i = 0; i < 50; i++) fprintf (of, "%c", mem_to_ascii[val[i]&077]) ;
    fprintf (of, "\n\t");
    for (i = 0; i < 50; i++) fprintf (of, (val[i]&WM)? "1": " ") ;
    return -(i - 1);
    }
if (sw & SWMASK ('S')) {                                /* string? */
    i = 0;
    do {
        t = val[i++];
        fprintf (of, (t & WM)? "`%c": "%c", mem_to_ascii[t & 077]);
    } while ((i < 50) && ((val[i] & WM) == 0));
    return -(i - 1);
    }
if (sw & SWMASK ('N')) {                                /* 1401 machine code? */
    uint16      temp;
    t_opcode    *tab;

    mod = 0;
    flags = 0;
    a = 0;
    b = 0;
    i = 0;
    op = val[i++] & 077;
    /* 1 234 567 8 */
    /* 0 123 456 7 */
    /* o  a   b  m */
    /* 0 123 */
    /* 0 123 4 */
    /* 0 123 456 */
    /* 0 1 */
    if ((val[i] & WM) == 0) {
        /* Grab next value if one */
        /* Grab 3 address digits */
        do {
            a = (a << 6) | (val[i++] & 077);
        } while((val[i] & WM) == 0 && i < 4);
    }
    /* If more then grab B address and or modifier */
    if ((val[i] & WM) == 0) {
        do {
            b = (b << 6) | (val[i++] & 077);
        } while((val[i] & WM) == 0 && i < 7);
    }

    /* Grab modifier */
    if ((val[i] & WM) == 0) {
        mod = val[i++] & 077;
        flags |= 010;
    }

    /* Determine A, B and modifier values */
    if (i == 2) {       /* Only mod */
        mod = a;
        flags |= 010;
    } else if (i == 4) { /* Only A */
        flags |= 002;
    } else if (i == 5) { /* A + mod */
        mod = b;
        flags |= 012;
    } else if (i > 6) { /* A + B */
        flags |= 006;
    }

    /* Modify 2 op branch to BCE or B */
    if (op == CHR_B) {
        switch(i) {
        case 1:
        case 7:
        case 8: op = CHR_B; break;
        default: op = CHR_J; break;
        }
    }

    temp = (mod << 6) | op;
    for(tab = ops_1401; tab->name != NULL; tab++) {
        if (temp == tab->opbase)
            break;
        if ((tab->type == TYPE_BE || tab->type == TYPE_CC) &&
                        (temp & 077) == tab->opbase)
           break;
        if (tab->type == TYPE_BZ && (temp & 0377) == tab->opbase)
           break;
        if ((temp & 077) == tab->opbase)
           break;
    }

    if (tab->type == TYPE_IO && (a & 0770000) == 0340000) {
        fprintf(of, "%cU\t", mem_to_ascii[op]);
        flags &= 075;
        flags |= 020;
    } else if (tab->name == NULL)
        fprintf(of, "%c<%02o>\t", mem_to_ascii[op], op);
    else
        fprintf(of, "%s\t", tab->name);

    switch(tab->type) {
    case TYPE_0:         /* no operands, no options */
    case TYPE_1:         /* One operand, no options */
    case TYPE_2:         /* Two operands, no options */
    case TYPE_B:        /* Branch, one operand, option */
    case TYPE_M:
        if (flags & 02)
                fprint_addr_1401(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr_1401(of, b);
        }
        if (flags & 010) {
                fputc(',', of);
                fputc(mem_to_ascii[mod], of);
        }
        break;
    case TYPE_BS:       /* Branch, one operand, option */
        if (flags & 02)
                fprint_addr_1401(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr_1401(of, b);
        }
        if (flags &010) {
                fputc(',', of);
                fputc(mem_to_ascii[mod], of);
        }
        break;
    case TYPE_IO:        /* Tape opcode or move */
        if (flags & 020)
             for (t = 18; t >= 0; t-=6)
                 fprintf (of, "%c", mem_to_ascii[(a>>t)&077]) ;
        else if (flags & 02)
             fprint_addr_1401(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr_1401(of, b);
        }
        if (flags & 010)
             fprintf (of, ",%c", mem_to_ascii[mod]);
        break;
    case TYPE_T:         /* Tape opcode, option */
        if (flags & 02)
             for (t = 18; t >= 0; t-=6)
                 fprintf (of, "%c", mem_to_ascii[(a>>t)&077]) ;
        if (flags & 04) {
                fputc(',', of);
                fprint_addr_1401(of, b);
        }
        break;

    case TYPE_BZ:       /* Branch, three operands */
        if (flags & 02)
                fprint_addr_1401(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr_1401(of, b);
        }
        if (flags & 010 && mod & 060) {
            fputc(',', of);
            if (mod & 020)
                fputc('A', of);
            if (mod & 040)
                fputc('B', of);
        }
        break;

    case TYPE_BE:       /* Branch, three operands */
    case TYPE_Y:        /* Store special */
    default:
        if (flags & 02)
                fprint_addr_1401(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr_1401(of, b);
        }
        if (flags &010) {
                fputc(',', of);
                fputc(mem_to_ascii[mod], of);
        }
        break;
    case TYPE_CC:
        if (flags & 02) {
                fprint_addr_1401(of, a);
                if (flags & 010)
                    fputc(',', of);
        }
        if (flags &010)
                fputc(mem_to_ascii[mod], of);
        break;
    }
    return -(i - 1);
}
if (sw & SWMASK ('M')) {                                /* machine code? */
    uint16      temp;
    t_opcode    *tab;

    mod = 0;
    flags = 0;
    a = 0;
    b = 0;
    i = 0;
    op = val[i++] & 077;
    if ((val[i] & WM) == 0) {
        /* Grab next value if one */
        if (op == OP_RD || op == OP_RDW || op == OP_UC) {
           /* Three digit IO address */
            do {
                 a = (a << 6) | (val[i++] & 077);
            } while((val[i] & WM) == 0 && i < 4);
            flags = 1;
        } else {
            /* Grab 5 address digits */
            do {
                 a = (a << 6) | (val[i++] & 077);
            } while((val[i] & WM) == 0 && i < 6);
        }
    }
    /* If more then grab B address and or modifier */
    if ((val[i] & WM) == 0) {
        int     j = 0;
        do {
            b = (b << 6) | (val[i++] & 077);
        } while((val[i] & WM) == 0 && ++j < 5);
    }

    /* Determine A, B and modifier values */
    if (i == 2) {
        mod = a;
        flags |= 010;
    } else if ((flags == 1 && i == 5) || (flags == 0 && i == 7)) {
        mod = b;
        flags |= 012;
    } else if ((flags == 1 && i == 4) || (flags == 0 && i == 6)) {
        flags |= 002;
    } else {
        flags |= 006;
        if ((val[i] & WM) == 0) {
            mod = val[i++] & 077;
            flags |= 010;
        }
    }

    temp = (mod << 6) | op;
    for(tab = base_ops; tab->name != NULL; tab++) {
        if (temp == tab->opbase)
            break;
        if ((tab->type == TYPE_BE || tab->type == TYPE_CC) &&
                        (temp & 077) == tab->opbase)
           break;
        if (tab->type == TYPE_BZ && (temp & 0377) == tab->opbase)
           break;
        if ((temp & 077) == tab->opbase)
           break;
    }
    if (tab->name == NULL)
        fprintf(of, "%c<%02o>\t", mem_to_ascii[op], op);
    else
        fprintf(of, "%s\t", tab->name);

    switch(tab->type) {
    case TYPE_0:         /* no operands, no options */
    case TYPE_1:         /* One operand, no options */
    case TYPE_2:         /* Two operands, no options */
    case TYPE_B:        /* Branch, one operand, option */
    case TYPE_M:
        if (flags & 02)
                fprint_addr(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr(of, b);
        }
        break;
    case TYPE_IO:        /* Tape opcode, option */
    case TYPE_T:         /* Tape opcode, option */
        if (flags & 010)
             fprintf (of, "%c", mem_to_ascii[mod]);
        if (flags & 02)
             for (t = 18; t >= 0; t-=6)
                 fprintf (of, "%c", mem_to_ascii[(a>>t)&077]) ;
        if (flags & 04) {
                fputc(',', of);
                fprint_addr(of, b);
        }
        break;

    case TYPE_BZ:       /* Branch, three operands */
        if (flags & 02)
                fprint_addr(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr(of, b);
        }
        if (flags & 010 && mod & 060) {
            fputc(',', of);
            if (mod & 020)
                fputc('A', of);
            if (mod & 040)
                fputc('B', of);
        }
        break;

    case TYPE_BE:       /* Branch, three operands */
    case TYPE_Y:        /* Store special */
    default:
        if (flags & 02)
                fprint_addr(of, a);
        if (flags & 04) {
                fputc(',', of);
                fprint_addr(of, b);
        }
        if (flags &010) {
                fputc(',', of);
                fputc(mem_to_ascii[mod], of);
        }
        break;
    case TYPE_CC:
        if (flags &010)
                fputc(mem_to_ascii[mod], of);
        break;
    }
    return -(i - 1);
}
t = val[0];
fprintf (of, (t & WM)? "~%02o ": " %02o ", t & 077);
return 0;
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
    char                buffer[100];
    int                 wm_seen;

    while (isspace(*cptr))
        cptr++;
    d = 0;
    if (sw & SWMASK('C')) {
        i = 0;
        wm_seen = 0;
        while (*cptr != '\0') {
            if (*cptr == '~' && wm_seen == 0)
                wm_seen = WM;
            else {
                d = ascii_to_six[0177 & *cptr] | wm_seen;
                val[i++] = d;
                wm_seen = 0;
            }
            cptr++;
        }
        if (i == 0 || wm_seen)
            return SCPE_ARG;
        return -(i - 1);
    } else if (sw & SWMASK('M')) {
        t_opcode           *op;
        int                j;
        int32              addr;

        i = 0;
        /* Grab opcode */
        cptr = get_glyph(cptr, buffer, 0);
        if ((op = find_opcode(buffer, base_ops)) == 0)
            return STOP_UUO;
        /* Skip blanks */
        while(isspace(*cptr)) cptr++;
        val[i++] = WM | (op->opbase & 077);
        switch(op->type) {
        case TYPE_0:     /* no operands, no options */
             if (*cptr != '\0')
                 return SCPE_ARG;
             return -(i - 1);

        case TYPE_CC:
            if (*cptr == '\0')
                val[i++] = 10;
            else
                val[i++] = ascii_to_six[(int)*cptr++];
            return -(i - 1);

        case TYPE_IO:    /* Tape opcode, option */
        case TYPE_T:     /* Tape opcode, option */
            if (*cptr == '\0')
                return SCPE_ARG;
            val[i++] = ascii_to_six[(int)*cptr++];
            if (*cptr == '\0')
                return SCPE_ARG;
            val[i++] = ascii_to_six[(int)*cptr++];
            if (*cptr == '\0')
                return SCPE_ARG;
            val[i++] = ascii_to_six[(int)*cptr++];
            /* Go grab address */
            if (op->type == TYPE_T) {
                val[i++] = (op->opbase >> 6) & 077;
                return -(i-1);
            }
            break;

        case TYPE_1:     /* One operand, no options */
        case TYPE_2:     /* Two operands, no options */
        case TYPE_BE:   /* Branch, three operands */
        case TYPE_Y:    /* Store special */
            if (*cptr == '\0')
                return -(i - 1);
            break;

        case TYPE_B:    /* Branch, one operand, option */
        case TYPE_BZ:   /* Branch, three operands */
        case TYPE_M:    /* Move opcode */
        default:
            if (*cptr == '\0') {
                val[i++] = (op->opbase >> 6) & 077;
                return -(i-1);
            }
            break;
        }

        /* Pick up at least one address & possible index */
        addr = 0;
        while(*cptr != '\0') {
            if (*cptr >= '0' && *cptr <= '9')
                addr = (addr * 10) + (*cptr++ - '0');
            else if (*cptr == '+' || *cptr == ',')
                break;
        }

        /* Convert to BCD */
        for(j = 4; j >= 0;j--) {
           buffer[j] = addr % 10;
           if (buffer[j] == 0)
                buffer[j] = 10;
           addr /= 10;
        }

        /* Merge in index bits if any */
        if (*cptr == '+') {
            int n = 0;
            cptr++;
            if (*cptr != 'X' && *cptr != 'x')
                return SCPE_ARG;
            cptr++;
            if (!(*cptr >= '0' && *cptr <= '9'))
                return SCPE_ARG;
            n = *cptr++ - '0';
            if (*cptr >= '0' && *cptr <= '9')
                n = (n * 10) + (*cptr++ - '0');
            if (n > 16)
                return SCPE_ARG;
            buffer[3] |= (n & 3) << 4;
            buffer[2] |= (n & 014) << 2;
        }

        /* Copy over address */
        for(j = 0; j <= 4; j++)
            val[i++] = buffer[j];

        /* Skip blanks */
        while(isspace(*cptr)) cptr++;
        switch(op->type) {
        case TYPE_IO:    /* Tape opcode, option */
        case TYPE_T:     /* Tape opcode, option */
            if (*cptr == ',') {
                val[i++] = ascii_to_six[(int)*++cptr];
                while(isspace(*++cptr));
            }
            if (*cptr == '\0')
                return -(i - 1);
            return SCPE_ARG;

        default:
        case TYPE_1:     /* One operand, no options */
            if (*cptr != '\0')
                return SCPE_ARG;
            return -(i - 1);

        case TYPE_2:     /* Two operands, no options */
        case TYPE_BE:   /* Branch, three operands */
            if (*cptr == '\0')
                return -(i - 1);
            break;

        case TYPE_B:    /* Branch, one operand, option */
            val[i++] = (op->opbase >> 6) & 077;
            return -(i-1);

        case TYPE_Y:    /* Store special */
            if (*cptr == ',') {
                val[i++] = ascii_to_six[(int)*++cptr];
                while(isspace(*++cptr));
            }
            if (*cptr == '\0')
                return -(i - 1);
            return SCPE_ARG;

        case TYPE_BZ:   /* Branch, three operands */
        case TYPE_M:    /* Move opcode */
            if (*cptr == '\0') {
                val[i++] = (op->opbase >> 6) & 077;
                return -(i-1);
            }
            break;
        }

        if (*cptr != ',')
            return SCPE_ARG;
        cptr++;

        /* Skip blanks */
        while(isspace(*cptr)) cptr++;

        /* Pick up at least one address & possible index */
        addr = 0;
        while(*cptr != '\0') {
            if (*cptr >= '0' && *cptr <= '9')
                addr = (addr * 10) + (*cptr++ - '0');
            else if (*cptr == '+' || *cptr == ',')
                break;
        }

        /* Convert to BCD */
        for(j = 4; j >= 0;j--) {
           buffer[j] = addr % 10;
           if (buffer[j] == 0)
                buffer[j] = 10;
           addr /= 10;
        }

        /* Merge in index bits if any */
        if (*cptr == '+') {
            int n = 0;
            cptr++;
            if (*cptr != 'X' && *cptr != 'x')
                return SCPE_ARG;
            cptr++;
            if (!(*cptr >= '0' && *cptr <= '9'))
                return SCPE_ARG;
            n = *cptr++ - '0';
            if (*cptr >= '0' && *cptr <= '9')
                n = (n * 10) + (*cptr++ - '0');
            if (n > 16)
                return SCPE_ARG;
            buffer[3] |= (n & 3) << 4;
            buffer[2] |= (n & 014) << 2;
        }

        /* Copy over address */
        for(j = 0; j <= 4; j++)
            val[i++] = buffer[j];

        /* Skip blanks */
        while(isspace(*cptr)) cptr++;
        switch(op->type) {
        case TYPE_M:    /* Move opcode */
            val[i++] = (op->opbase >> 6) & 077;
            /* fall through */

        default:
        case TYPE_IO:    /* Tape opcode, option */
        case TYPE_T:     /* Tape opcode, option */
        case TYPE_1:     /* One operand, no options */
        case TYPE_B:    /* Branch, one operand, option */
        case TYPE_Y:    /* Store special */
        case TYPE_2:     /* Two operands, no options */
            if (*cptr == '\0')
                return -(i - 1);
            break;

        case TYPE_BE:   /* Branch, three operands */
            if (*cptr == ',') {
                val[i++] = ascii_to_six[(int)*++cptr];
                while(isspace(*++cptr));
            }
            if (*cptr == '\0')
                return -(i - 1);
            break;

        case TYPE_BZ:   /* Branch, three operands */
            if (*cptr == '\0') {
                val[i++] = (op->opbase >> 6) & 077;
                return -(i-1);
            } else if (*cptr == ',') {
                d = (op->opbase >> 6) & 077;
                cptr++;
                /* Skip blanks */
                while(isspace(*cptr)) cptr++;
                while(*cptr != '\0') {
                    if (*cptr == 'A' || *cptr == 'a')
                        d |= 020;
                    else if (*cptr == 'B' || *cptr == 'b')
                        d |= 040;
                    else
                        return SCPE_ARG;
                    cptr++;
                }
                val[i++] = d;
                return -(i-1);
            }
        }
        return SCPE_ARG;
    } else {
        int     sign = 0;
        i = 0;
        wm_seen = 1;
        while (*cptr != '\0') {
            sign = 0;
            /* Skip blanks */
            while(isspace(*cptr))       cptr++;
            if (*cptr == '+') {
                cptr++;
                sign = 1;
            } else if (*cptr == '-') {
                cptr++;
                sign = -1;
            }
            if (!(*cptr >= '0' && *cptr <= '9'))
                return SCPE_ARG;
            while(*cptr >= '0' && *cptr <= '9') {
                d = *cptr++ - '0';
                if (d == 0)
                    d = 10;
                if (wm_seen) {
                    d |= WM;
                    wm_seen = 0;
                }
                val[i++] = d;
            }
            if (*cptr == ',')
                cptr++;
            if (sign != 0)
                val[i-1] |= (sign < 0)?040:060; /* Set sign last digit */
        }
        if (i == 0)
            return SCPE_ARG;
        return -(i - 1);
    }
    return SCPE_OK;
}
