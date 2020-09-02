/* i7070_sys.c: IBM 7070 Simulator system interface.

   Copyright (c) 2006-2016, Richard Cornwell

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

#include "i7070_defs.h"
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

char                sim_name[] = "IBM 7070";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &chan_dev,
#ifdef NUM_DEVS_CDR
    &cdr_dev,
#endif
#ifdef NUM_DEVS_CDP
    &cdp_dev,
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
#if NUM_DEVS_MT > 3
    &mtd_dev,
#endif
#endif
#endif
#endif
#if NUM_DEVS_HT > 0
    &hta_dev,
#if NUM_DEVS_HT > 1
    &htb_dev,
#endif
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
#ifdef NUM_DEVS_CDR
DIB  cdr_dib = { CH_TYP_UREC, 1, 01, 0xF, &cdr_cmd, NULL };
#endif
#ifdef NUM_DEVS_CDP
DIB  cdp_dib = { CH_TYP_UREC, 1, 02, 0xF, &cdp_cmd, &cdp_ini };
#endif
#ifdef NUM_DEVS_LPR
DIB  lpr_dib = { CH_TYP_UREC, 1, 03, 0xF, &lpr_cmd, &lpr_ini };
#endif
#ifdef NUM_DEVS_CON
DIB  con_dib = { CH_TYP_UREC, 1, 00, 0xF, &con_cmd, &con_ini };
#endif
DIB  mt_dib = { CH_TYP_76XX, NUM_UNITS_MT, 0000, 0000, &mt_cmd, &mt_ini };
#ifdef NUM_DEVS_CHRON
DIB  chron_dib = { CH_TYP_76XX, 1, 0000, 0000, &chron_cmd, NULL };
#endif
#ifdef NUM_DEVS_DSK
DIB  dsk_dib = { CH_TYP_79XX, 0, 0, 0, &dsk_cmd, &dsk_ini };
#endif
#ifdef NUM_DEVS_HT
DIB  ht_dib = { CH_TYP_79XX, NUM_UNITS_HT, 0, 0, &ht_cmd, NULL };
#endif
#ifdef NUM_DEVS_COM
DIB  com_dib = { CH_TYP_79XX, 0, 0, 0, &com_cmd, NULL };
#endif

/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "IO device not ready",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "",
    "",
    "I/O Check error",
    "",
    "7750 invalid line number",
    "7750 invalid message",
    "7750 No free output buffers",
    "7750 No free input buffers",
    "Field overflow",
    "Sign change",
    "Divide error",
    "Alpha index word",
    "Error?", "Error2", 0
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

const char          mem_to_ascii[64] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', '0', '=', '\'', ':', '>', 's',
    'b', '/', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'x', ',', '(', '~', '\\', '_',
    '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', '!', '$', '*', ']', ';', '^',
    '+', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', '?', '.', ')', '[', '<', '|',
                      /*Sq*/          /*RM*/
};

char    mem_ascii[256] = {
/* 00 */  ' ', '~', '~', '~', '~', '~', '~', '~',
        /*                                         */
/* 08 */  '~', '~', '~', '~', '~', '~', '~', '~',
        /*                          .    sq    ?   */
/* 10 */  '~', '~', '~', '~', '~', '.', '[', '?',
        /*  \    #                                 */
/* 18 */  '\\', '#', '|', '~', '~', '~', '~', '~',
        /*+/-                        $    *    ?   */
/* 20 */  '+', '~', '~', '~', '~', '$', '*', '?',
        /*  ?  +/-                                 */
/* 28 */  '?', '-', '~', '~', '~', '~', '~', '~',
        /*  -    /                   ,  %/(    ?   */
/* 30 */  '-', '/', '~', '~', '~', ',', '%', '?',
        /*  ?   sm                                 */
/* 38 */  '?', 's', '~', '~', '~', '~', '~', '~',
        /*                         =/#  !/@    ?   */
/* 40 */  '~', '~', '~', '~', '~', '=', '!', '?',
        /*  ?   tm                                 */
/* 48 */  '?', 't', '~', '~', '~', '~', '~', '~',
        /*                                         */
/* 50 */  '~', '~', '~', '~', '~', '~', '~', '~',
        /*                                         */
/* 58 */  '~', '~', '~', '~', '~', '~', '~', '~',
        /* +0    A    B    C    D    E    F    G   */
/* 60 */  '^', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
        /*  H    I                                 */
/* 68 */  'H', 'I', '~', '~', '~', '~', '~', '~',
        /* -0    J    K    L    M    N    O    P   */
/* 70 */  '_', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        /*  Q    R                                 */
/* 78 */  'Q', 'R', '~', '~', '~', '~', '~', '~',
        /* rm         S    T    U    V    W    X   */
/* 80 */  'r', '~', 'S', 'T', 'U', 'V', 'W', 'X',
        /*  Y    Z                                 */
/* 88 */  'Y', 'Z', '~', '~', '~', '~', '~', '~',
        /*  0    1    2    3    4    5    6    7   */
/* 90 */  '0', '1', '2', '3', '4', '5', '6', '7',
        /*  8    9                                 */
/* 98 */  '8', '9', '~', '~', '~', '~', '~', '~'
};


/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
   /* Currently not implimented until I know format of load files */
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

#define TYPE_A  0               /* Basic oo ii ff aaaa */
#define TYPE_S  1               /* Shift control */
#define TYPE_B  2               /* Basic oo ii xx aaaa */
#define TYPE_C  3               /* Alternate switch */
#define TYPE_D  4               /* Electronic switch */
#define TYPE_E  5               /* Extended memory */
#define TYPE_F  6               /* Branch type 2 */
#define TYPE_Z  8               /* Sign control */
#define TYPE_I  9               /* Basic oo ii II aaaa */
#define TYPE_X  10              /* No extra values */
#define TYPE_T  11              /* Tape control */
#define TYPE_U  12              /* unit record control */
#define TYPE_V  13              /* Channel IO */
#define TYPE_G  14              /* Diag SW */
#define TYPE_P1 15              /* Branch priority */
#define TYPE_P2 16              /* Set Priority latch */
#define TYPE_P3 17              /* Clear Priority latch */
#define TYPE_IQ 18              /* Inquiry instruction */
#define TYPE_TB 19              /* Binary Tape operations */

/* Opcodes */
t_opcode  base_ops[] = {
        {OP_HB,         "HB",           TYPE_B},
        {OP_B,          "B",            TYPE_B},
        {OP_BLX,        "BLX",          TYPE_I},
        {OP_CD,         "CD",           TYPE_A},
        {OP_EXMEM,      "EXMEM",        TYPE_E},
        {OP_DIAGC,      "DIAGC",        TYPE_I},
        {OP_DIAGT,      "DIAGT",        TYPE_I},
        {OP_DIAGR,      "DIAGR",        TYPE_I},
        {OP_DIAGS,      "DIAGS",        TYPE_I},
        {OP_BZ1,        "BZ1",          TYPE_B},
        {OP_BV1,        "BV1",          TYPE_B},
        {OP_ST1,        "ST1",          TYPE_A},
        {OP_ZA1,        "ZA1",          TYPE_A},
        {OP_A1,         "A1",           TYPE_A},
        {OP_C1,         "C1",           TYPE_A},
        {OP_ZAA,        "ZAA",          TYPE_A},
        {OP_AA,         "AA",           TYPE_A},
        {OP_AAS1,       "AAS1",         TYPE_A},
        {OP_AS1,        "AS1",          TYPE_A},
        {OP_BZ2,        "BZ2",          TYPE_B},
        {OP_BV2,        "BV2",          TYPE_B},
        {OP_ST2,        "ST2",          TYPE_A},
        {OP_ZA2,        "ZA2",          TYPE_A},
        {OP_A2,         "A2",           TYPE_A},
        {OP_C2,         "C2",           TYPE_A},
        {OP_AS2,        "AS2",          TYPE_A},
        {OP_AAS2,       "AAS2",         TYPE_A},
        {OP_BZ3,        "BZ3",          TYPE_B},
        {OP_BV3,        "BV3",          TYPE_B},
        {OP_ST3,        "ST3",          TYPE_A},
        {OP_ZA3,        "ZA3",          TYPE_A},
        {OP_A3,         "A3",           TYPE_A},
        {OP_C3,         "C3",           TYPE_A},
        {OP_AS3,        "AS3",          TYPE_A},
        {OP_AAS3,       "AAS3",         TYPE_A},
        {OP_BL,         "BL",           TYPE_B},
        {OP_BFLD,       "BFLD",         TYPE_F},
        {OP_BXN,        "BXN",          TYPE_I},
        {OP_XL ,        "XL ",          TYPE_I},
        {OP_XZA,        "XZA",          TYPE_I},
        {OP_XA,         "XA",           TYPE_I},
        {OP_XSN,        "XSN",          TYPE_I},
        {OP_BIX,        "BIX",          TYPE_I},
        {OP_SC,         "SC",           TYPE_S},
        {OP_INQ,        "INQ",          TYPE_IQ},
        {OP_BSWITCH,    "BSWITCH",      TYPE_C},
        {OP_M,          "M",            TYPE_A},
        {OP_PC,         "PC",           TYPE_I},
        {OP_ENA,        "ENA",          TYPE_I},
        {OP_ENB,        "ENB",          TYPE_I},
        {OP_PRTST,      "PRTST",        TYPE_P1},
        {OP_BSW21,      "BES",          TYPE_D},
        {OP_BSW22,      "BES",          TYPE_D},
        {OP_BSW23,      "BES",          TYPE_D},
        {OP_PR,         "PR",           TYPE_B},
        {OP_RS,         "RS",           TYPE_I},
        {OP_LL,         "LL",           TYPE_A},
        {OP_LE,         "LE",           TYPE_A},
        {OP_LEH,        "LEH",          TYPE_A},
        {OP_UREC,       "UREC",         TYPE_U},
        {OP_FBV,        "FBV",          TYPE_B},
        {OP_FR,         "FR",           TYPE_X},
        {OP_FM,         "FM",           TYPE_B},
        {OP_FA,         "FA",           TYPE_B},
        {OP_FZA,        "FZA",          TYPE_B},
        {OP_FAD,        "FAD",          TYPE_B},
        {OP_FAA,        "FAA",          TYPE_B},
        {OP_HP,         "HP",           TYPE_X},
        {OP_NOP,        "NOP",          TYPE_X},
        {OP_CS,         "CS",           TYPE_Z},
        {OP_BM1,        "BM1",          TYPE_B},
        {OP_ZST1,       "ZST1",         TYPE_A},
        {OP_STD1,       "STD1",         TYPE_A},
        {OP_ZS1,        "ZS1",          TYPE_A},
        {OP_S1,         "S1",           TYPE_A},
        {OP_CA,         "CA",           TYPE_A},
        {OP_ZSA,        "ZSA",          TYPE_A},
        {OP_SA,         "SA",           TYPE_A},
        {OP_SS1,        "SS1",          TYPE_A},
        {OP_BM2,        "BM2",          TYPE_B},
        {OP_ZST2,       "ZST2",         TYPE_A},
        {OP_STD2,       "STD2",         TYPE_A},
        {OP_ZS2,        "ZS2",          TYPE_A},
        {OP_S2,         "S2",           TYPE_A},
        {OP_SS2,        "SS2",          TYPE_A},
        {OP_BM3,        "BM3",          TYPE_B},
        {OP_ZST3,       "ZST3",         TYPE_A},
        {OP_STD3,       "STD3",         TYPE_A},
        {OP_ZS3,        "ZS3",          TYPE_A},
        {OP_S3,         "S3",           TYPE_A},
        {OP_SS3,        "SS3",          TYPE_A},
        {OP_BH,         "BH",           TYPE_B},
        {OP_BE,         "BE",           TYPE_B},
        {OP_BCX,        "BCX",          TYPE_I},
        {OP_BXM,        "BXM",          TYPE_I},
        {OP_XU,         "XU",           TYPE_I},
        {OP_XZS,        "XZS",          TYPE_I},
        {OP_XS,         "XS",           TYPE_I},
        {OP_XLIN,       "XLIN",         TYPE_I},
        {OP_BDX,        "BDX",          TYPE_I},
        {OP_CSC,        "CSC",          TYPE_S},
        {OP_D,          "D",            TYPE_A},
        {OP_ENS,        "ENS",          TYPE_I},
        {OP_EAN,        "EAN",          TYPE_I},
        {OP_PRION,      "DCAN",         TYPE_P2},
        {OP_PRIOF,      "DCAF",         TYPE_P3},
        {OP_RG,         "RG",           TYPE_I},
        {OP_FBU,        "FBU",          TYPE_B},
        {OP_FD,         "FD",           TYPE_B},
        {OP_FS,         "FS",           TYPE_B},
        {OP_FDD,        "FDD",          TYPE_B},
        {OP_FADS,       "FADS",         TYPE_B},
        {OP_FSA,        "FSA",          TYPE_B},
        {OP_TRN,        "TRN",          TYPE_TB},
        {OP_TRNP,       "PTRN",         TYPE_TB},
        {OP_TAP1,       "",             TYPE_T},
        {OP_TAP2,       "",             TYPE_T},
        {OP_TAP3,       "",             TYPE_T},
        {OP_TAP4,       "",             TYPE_T},
        {OP_TAPP1,      "P",            TYPE_T},
        {OP_TAPP2,      "P",            TYPE_T},
        {OP_TAPP3,      "P",            TYPE_T},
        {OP_TAPP4,      "P",            TYPE_T},
        {OP_CHN1,       "",             TYPE_V},
        {OP_CHN2,       "",             TYPE_V},
        {OP_CHN3,       "",             TYPE_V},
        {OP_CHN4,       "",             TYPE_V},
        {OP_CHNP1,      "P",            TYPE_V},
        {OP_CHNP2,      "P",            TYPE_V},
        {OP_CHNP3,      "P",            TYPE_V},
        {OP_CHNP4,      "P",            TYPE_V},
        {0,             NULL,           0},
};

t_opcode  sub_ops[] = {
        {0,             "SR",   TYPE_S},
        {1,             "SRR",  TYPE_S},
        {2,             "SL",   TYPE_S},
        {3,             "SLC",  TYPE_S},
        {4,             "SRS",  TYPE_S},
        {5,             "SLS",  TYPE_S},
        {6,             "SRS",  TYPE_S},
        {7,             "SLS",  TYPE_S},
        {0,             "BAS",  TYPE_C},
        {1,             "BCB",  TYPE_C},
        {2,             "BDCB", TYPE_C},
        {0,             "BES",  TYPE_D},
        {1,             "ESN",  TYPE_D},
        {2,             "ESF",  TYPE_D},
        {3,             "BSN",  TYPE_D},
        {4,             "BSF",  TYPE_D},
        {0x00,          "BAL",  TYPE_P1},
        {0x01,          "BUL",  TYPE_P1},
        {0x02,          "BUL",  TYPE_P1},
        {0x03,          "BQL",  TYPE_P1},
        {0x04,          "BQL",  TYPE_P1},
        {0x10,          "BTL",  TYPE_P1},
        {0x20,          "BTL",  TYPE_P1},
        {0x30,          "BTL",  TYPE_P1},
        {0x40,          "BTL",  TYPE_P1},
        {0x80,          "BDCL", TYPE_P1},
        {0x90,          "BDCA", TYPE_P1},
        {0x01,          "ULN",  TYPE_P2},
        {0x02,          "ULN",  TYPE_P2},
        {0x03,          "QLN",  TYPE_P2},
        {0x04,          "QLN",  TYPE_P2},
        {0x10,          "TLN",  TYPE_P2},
        {0x20,          "TLN",  TYPE_P2},
        {0x30,          "TLN",  TYPE_P2},
        {0x40,          "TLN",  TYPE_P2},
        {0x80,          "BDLN", TYPE_P2},
        {0x90,          "BDAN", TYPE_P2},
        {0x01,          "ULF",  TYPE_P3},
        {0x02,          "ULF",  TYPE_P3},
        {0x03,          "QLF",  TYPE_P3},
        {0x04,          "QLF",  TYPE_P3},
        {0x10,          "TLF",  TYPE_P3},
        {0x20,          "TLF",  TYPE_P3},
        {0x30,          "TLF",  TYPE_P3},
        {0x40,          "TLF",  TYPE_P3},
        {0x80,          "BDLF", TYPE_P3},
        {0x90,          "BDAF", TYPE_P3},
        {0,             "BASS", TYPE_E},
        {1,             "ASSN", TYPE_E},
        {2,             "ASSF", TYPE_E},
        {0,             "BFV",  TYPE_F},
        {1,             "SMFV", TYPE_F},
        {2,             "HMFV", TYPE_F},
        {0x30,          "CSA",  TYPE_Z},
        {0x60,          "CSM",  TYPE_Z},
        {0x90,          "CSP",  TYPE_Z},
        {0x31,          "MSA",  TYPE_Z},
        {0x61,          "MSM",  TYPE_Z},
        {0x91,          "MSP",  TYPE_Z},
        {2,             "SMSC", TYPE_Z},
        {3,             "HMSC", TYPE_Z},
        {4,             "BSC",  TYPE_Z},
        {0x10,          "TR",   TYPE_T},
        {0x20,          "TRR",  TYPE_T},
        {0x30,          "TW",   TYPE_T},
        {0x40,          "TWR",  TYPE_T},
        {0x50,          "TWZ",  TYPE_T},
        {0x60,          "TWC",  TYPE_T},
        {0x70,          "TSF",  TYPE_T},
        {0x80,          "TSB",  TYPE_T},
        {0x90,          "TRA",  TYPE_T},
        {0x00,          "TSEL", TYPE_T},
        {0x01,          "TM",   TYPE_T},
        {0x02,          "TRW",  TYPE_T},
        {0x03,          "TRU",  TYPE_T},
        {0x04,          "TRB",  TYPE_T},
        {0x05,          "TSM",  TYPE_T},
        {0x06,          "TSK",  TYPE_T},
        {0x07,          "TEF",  TYPE_T},
        {0x08,          "TSLD", TYPE_T},
        {0x09,          "TSHD", TYPE_T},
        {0,             "US",   TYPE_U},
        {1,             "UR",   TYPE_U},
        {2,             "UW",   TYPE_U},
        {3,             "UWIV", TYPE_U},
        {4,             "TYP",  TYPE_U},
        {1,             "DCP",  TYPE_V},
        {2,             "DCUA", TYPE_V},
        {3,             "DCUR", TYPE_V},
        {4,             "DCPR", TYPE_V},
        {6,             "DCU",  TYPE_V},
        {0,             "QR",   TYPE_IQ},
        {1,             "QW",   TYPE_IQ},
        {0,             NULL,   0},
};


const char *chname[11] = {
    "*", "1", "2", "3", "4", "A", "B", "C", "D"
};

/* Print out an instruction */
void
print_opcode(FILE * of, t_value val, t_opcode * tab)
{
    uint32      MA;
    uint8       f1;
    uint8       f2;
    uint8       IX;
    uint16      op;
    int         type;
    int         t;

    MA = AMASK & val;
    f1 = (val >> 16) & 0xf;
    f2 = (val >> 20) & 0xf;
    IX = (val >> 24) & 0xff;
    op = (val >> 32) & 0xff;
    if ((val & SMASK) == MSIGN)
        op |= 0x100;

    while (tab->name != NULL) {
        if (tab->opbase == op) {
            switch (type = tab->type) {
            case TYPE_X:
                fputs(tab->name, of);
                return;

            case TYPE_A:
                fputs(tab->name, of);
                fputc(' ', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                if (f1 != 0 || f2 != 9) {
                   fprintf(of, "(%d,%d)", f2, f1);
                }
                return;
            case TYPE_S:
                f1 = (MA >> 8) & 0xf;
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f1)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                if (op == OP_SC)
                   fputc('0' + ((MA >> 12) & 0xf), of);
                fputc(' ', of);
                fprint_val(of, MA & 0xff, 16, 8, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_B:
                fputs(tab->name, of);
                fputc(' ', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_C:
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f1)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                fprintf(of, " %d,", f2);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_D:        /* Electronic switch */
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f2)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                fputc(' ', of);
                fputc('0' + (op & 0xf), of);
                fputc('0' + f1, of);
                fputc(',', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_E:        /* Extended memory */
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f1)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                if (f1 == 0) {
                    fputc(' ', of);
                    fprint_val(of, MA, 16, 16, PV_RZRO);
                    if (IX != 0) {
                       fputs("+X", of);
                       fprint_val(of, IX, 16, 8, 0);
                    }
                }
                return;
            case TYPE_F:        /* Branch type 2 */
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f2)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                if (f2 == 0) {
                    fputc(' ', of);
                    fprint_val(of, MA, 16, 16, PV_RZRO);
                    if (IX != 0) {
                       fputs("+X", of);
                       fprint_val(of, IX, 16, 8, 0);
                    }
                }
                return;
            case TYPE_Z:        /* Sign control */
                f1 |= f2 << 4;
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f1)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                fputc(' ', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_TB:       /* Binary Tape operation. */
                fprintf(of, "%s %d,", tab->name, f2);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_I:        /* Indexed operand */
                fprintf(of, "%s %d%d,", tab->name, f2, f1);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_T:        /* Tape Control */
                f1 <<= 4;
                if (f1 == 0)
                   f1 += MA & 0xf;
                fputs(tab->name, of);
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f1)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                fputc(' ', of);
                fputc('0' + (op & 0xf), of);
                fputc('0' + f2, of);
                fputc(',', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_U:        /* Unit Record Control */
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f2)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                fputc(' ', of);
                fputc('0' + f1, of);
                fputc(',', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_V:        /* Channel Control */
                fputs(tab->name, of);
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f1)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fputs(tab->name, of);
                fputc(' ', of);
                fputc('0' + (op & 0xf), of);
                fputc(',', of);
                fputc('0' + f2, of);
                fputc(',', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_P1:       /* Priority Control */
            case TYPE_P2:
            case TYPE_P3:
                if (f2 == 0)
                   t = f1;
                else
                   t = f2 << 4;
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == t)
                        break;
                }
                if (tab->name == NULL) {
                  /* Not found, decode */
                   for(tab = base_ops; tab->name != NULL; tab++) {
                      if (tab->opbase == op)
                        break;
                   }
                }
                fputs(tab->name, of);
                fputc(' ', of);
                switch(f2) {
                case 4:
                case 3:
                case 2: fputc('0' + f2 - 1, of);
                case 1:
                case 8:
                case 9:
                case 0: fputc('0' + f1, of);
                        break;
                }
                fputc(',', of);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            case TYPE_IQ:
                for(tab = sub_ops; tab->name != NULL; tab++) {
                   if (tab->type == type && tab->opbase == f2)
                        break;
                }
                if (tab->name == NULL)
                   break;
                fprintf(of, "%s %d,", tab->name, f2);
                fprint_val(of, MA, 16, 16, PV_RZRO);
                if (IX != 0) {
                   fputs("+X", of);
                   fprint_val(of, IX, 16, 8, 0);
                }
                return;
            default:
                return;
            }
        }
        tab++;
    }
    fprintf(of, " %d Unknown opcode", op);
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
    t_uint64            inst = *val;

/* Print value in decimal first */
    fputc(' ', of);
    switch (inst & SMASK) {
    case PSIGN: fputc('+', of); break;
    case MSIGN: fputc('-', of); break;
    case ASIGN: fputc('@', of); break;
    default: fputc('#', of); break;
    }
    fprint_val(of, inst & DMASK, 16, 40, PV_RZRO);

    if (sw & SWMASK('M')) {
        fputs("   ", of);
        print_opcode(of, inst, base_ops);
    }
    if (sw & SWMASK('C')) {
        int                 i;

        fputs("   '", of);
        for (i = 4; i >= 0; i--) {
            int                 ch;

            ch = (int)(inst >> (8 * i)) & 0xff;
            fputc(mem_ascii[ch], of);
        }
        fputc('\'', of);
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
    int                 idx;
    t_value             a, opr, d;
    int                 sign;
    char                opcode[100];

    while (isspace(*cptr))
        cptr++;
    d = 0;
    if (sw & SWMASK('M')) {
        t_opcode           *op, *op2;

        i = 0;
        sign = 0;
        /* Grab opcode */
        cptr = get_glyph(cptr, opcode, 0);

        op2 = 0;
        if((op = find_opcode(opcode, base_ops)) == 0) {
            if ((op2 = find_opcode(opcode, sub_ops)) != 0) {
                for(op = base_ops; (op->type != op2->type) &&
                                 (op->name != NULL);  op++);
                if (op->name == NULL)
                    return STOP_UUO;
            } else if (opcode[0] == 'P' &&
                        (op2 = find_opcode(&opcode[1], sub_ops)) != 0) {
                for(op = base_ops; (op->type != op2->type) &&
                     (op->name != NULL) && op->name[0] == 'P';  op++);
                if (op->name == NULL)
                    return STOP_UUO;
            }
        }
        if (op == 0)
            return STOP_UUO;

        d = (((t_uint64)op->opbase) << 32) & DMASK;
        d |= (op->opbase & 0x100)? MSIGN:PSIGN;
        if (op->type == TYPE_X) {
            *val = d;
            return SCPE_OK;
        }
        if (op2 != 0 && op2->opbase != 0 && op->type == TYPE_E) {
            d |= ((t_uint64)op2->opbase) << 24;
            *val = d;
            return SCPE_OK;
        }
        if (op2 != 0 && op2->opbase != 0 && op->type == TYPE_F) {
            d |= ((t_uint64)op2->opbase) << 28;
            *val = d;
            return SCPE_OK;
        }
        a = 0;
        idx = 0;
        opr = 0;
        while (*cptr == ' ' || *cptr == '\t')
            cptr++;
        /* Collect first argument if there is one */
        while (*cptr >= '0' && *cptr <= '9')
            opr = (opr << 4) + (*cptr++ - '0');
        /* Skip blanks */
        while (*cptr == ' ' || *cptr == '\t')
            cptr++;
        /* Type A       opc     addr+X#(n,m) */
        /* Type B       opc     addr+X# */
        /* Type S       opcA    addr+X#      f1 = op */
        /* Type C       opc     addr+X#,f2   f1 = op */
        /* Type D       opc     opf2,addr+X */
        /* Type E       opc     *addr+X#      f1 = op */
        /* Type F       opc     *addr+X#      f2 = op*/
        /* Type Z       opc     addr+X#       f1 = op */
        /* Type I       opc     f2f1,addr+X# */
        /* Type T       opc     opf2,addr+X#  f1 = op  */
        /* Type U       opc     f1,addr+X#    f2 = op */
        /* Type V       opc     opf1,addr+X#    f2 = op */
        /* End of opcode, give to address */
        if (*cptr == '\0' || *cptr == '(') {
            a = opr;
            opr = 0;
        }

        /* If plus, then must be address and follow by index */
        if (*cptr == '+') {
            a = opr;
            opr = 0;
            cptr++;
            /* Skip blanks */
            while (*cptr == ' ' || *cptr == '\t')
                cptr++;
            if (*cptr != 'x' && *cptr != 'X')
                return STOP_UUO;
            cptr++;
            while (*cptr >= '0' && *cptr <= '9')
                idx = (idx << 4) + (*cptr++ - '0');
            if (idx >= 0x100)
                return  STOP_UUO;
        }

        /* Comma, first was operand, now get address */
        if (*cptr == ',') {
            a = 0;
            cptr++;
            /* Skip blanks */
            while (*cptr == ' ' || *cptr == '\t')
                cptr++;
            /* Collect second argument if there is one */
            while (*cptr >= '0' && *cptr <= '9')
                a = (a << 4) + (*cptr++ - '0');
            /* Skip blanks */
            while (*cptr == ' ' || *cptr == '\t')
                cptr++;
            if (*cptr == '+') {
                cptr++;
                /* Skip blanks */
                while (*cptr == ' ' || *cptr == '\t')
                    cptr++;
                if (*cptr != 'x' && *cptr != 'X')
                    return STOP_UUO;
                cptr++;
                while (*cptr >= '0' && *cptr <= '9')
                    idx = (idx << 4) + (*cptr++ - '0');
                if (idx >= 0x100)
                    return  STOP_UUO;
            }
        }

        /* If we get a (, then grab field spec */
        if (*cptr == '(') {
            if (op->type != TYPE_A)
                return STOP_UUO;
            cptr++;
            /* Skip blanks */
            while (*cptr == ' ' || *cptr == '\t')
                cptr++;
            /* Collect second argument if there is one */
            if (*cptr >= '0' && *cptr <= '9')
                opr = *cptr++ - '0';
            else
                return STOP_UUO;
            while (*cptr == ' ' || *cptr == '\t')
                cptr++;
            if (*cptr == ',') {
                /* Skip blanks */
                cptr++;
                while (*cptr == ' ' || *cptr == '\t')
                    cptr++;
                /* Collect second argument if there is one */
                if (*cptr >= '0' && *cptr <= '9') {
                    opr <<= 4;
                    opr |= *cptr++ - '0';
                } else
                    return STOP_UUO;
            } else if (*cptr == ')')
                opr |= opr << 4;
            /* Skip blanks */
            while (*cptr == ' ' || *cptr == '\t')
                cptr++;
            if (*cptr++ != ')')
                return STOP_UUO;
        } else if (op->type == TYPE_A)
            opr = 0x09;

        /* Skip blanks */
        while (*cptr == ' ' || *cptr == '\t')
            cptr++;
        if (*cptr != '\0')
            return STOP_UUO;
        d |= ((t_uint64)idx) << 24;
        d |= a;
        switch(op->type) {
        case TYPE_P1:
        case TYPE_P2:
        case TYPE_P3:
                if (op2 == NULL)
                    d |= ((t_uint64)opr) << 16;
                else
                    d |= ((t_uint64)(opr + op2->opbase)) << 16;
                break;
        case TYPE_A:
                d |= ((t_uint64)opr) << 16;
                break;
        case TYPE_E:
        case TYPE_F:
        case TYPE_B:
                break;
        case TYPE_S:
        case TYPE_D:
        case TYPE_V:
                d += ((t_uint64)opr &0xF0) << 28;
                d |= ((t_uint64)opr &0x0F) << 28;
                break;
        case TYPE_Z:
                d |= ((t_uint64)op2->opbase) << 16;
                break;
        case TYPE_TB:
                opr <<= 4;
                opr |= 1;
                /* Fall through */
        case TYPE_I:
                d |= ((t_uint64)opr) << 16;
                break;
        case TYPE_T:
                if (op2->opbase & 0xf0)
                    d |= ((t_uint64)op2->opbase &0xF0) << 12;
                else
                    d |= op2->opbase;
                d |= ((t_uint64)opr & 0xF) << 16;
                d += ((t_uint64)opr &0xF0) << 28;
                break;
        case TYPE_U:
        case TYPE_C:
                d |= ((t_uint64)opr) << 20;
                d |= ((t_uint64)op2->opbase) << 16;
                /* Fall through */
        case TYPE_IQ:
                d |= ((t_uint64)opr) << 20;
                break;
        }
    } else if (sw & SWMASK('C')) {
        extern uint8    bcd_mem[64];
        i = 0;
        while (*cptr != '\0' && i < 5) {
            d <<= 8;
            if (sim_ascii_to_six[0177 & *cptr] != (const char)-1)
                d |= bcd_mem[(int)sim_ascii_to_six[0177 & *cptr]];
            cptr++;
            i++;
        }
        d <<= 8 * (5 - i);
        d |= ASIGN;
    } else {
        switch(*cptr) {
        case '-': sign = -1; cptr++; break;
        case '@': sign = 1; cptr++; break;
        case '+': cptr++;
                  /* Fall through */
        default:
            sign = 0;
        }
        while (*cptr >= '0' && *cptr <= '9') {
            d <<= 4;
            d |= *cptr++ - '0';
        }
        d &= DMASK;
        switch (sign) {
        case  1: d |= ASIGN; break;
        case  0: d |= PSIGN; break;
        case -1: d |= MSIGN; break;
        }
    }
    *val = d;
    return SCPE_OK;
}
