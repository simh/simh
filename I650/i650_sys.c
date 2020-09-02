/* i650_sys.c: IBM 650 Simulator system interface.

   Copyright (c) 2018, Roberto Sancho

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
   ROBERTO SANCHO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "i650_defs.h"
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

char                sim_name[] = "IBM 650";

REG                *sim_PC = &cpu_reg[0];

int32               sim_emax = 1;

DEVICE             *sim_devices[] = {
    &cpu_dev,
    &cdr_dev,
    &cdp_dev,
    &mt_dev,
    &dsk_dev,
    NULL
};

/* Device addressing words */

DIB  cdr_dib = { 3, &cdr_cmd, NULL };
DIB  cdp_dib = { 3, &cdp_cmd, NULL };
DIB  mt_dib  = { 5, &mt_cmd, &mt_ini };
DIB  dsk_dib = { 4, &mt_cmd, &dsk_ini };

/* Simulator stop codes */
const char         *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "I/O Error",
    "Programmed Stop",
    "Overflow",
    "Opcode Execution Error",
    "Address Error",
    0
};

static t_stat ibm650_deck_cmd(int32 arg, CONST char *buf);

static CTAB aux_cmds [] = {
/*    Name         Action Routine     Argument   Help String */
/*    ----------   -----------------  ---------  ----------- */
    { "CARDDECK",  &ibm650_deck_cmd,         0,  "Card Deck Operations: Join/Split/Print\n"    },

    { NULL }
    };

/* Simulator debug controls */
DEBTAB              dev_debug[] = {
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {0, 0}
};

DEBTAB              crd_debug[] = {
    {"CMD", DEBUG_CMD},
    {"DATA", DEBUG_DATA},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {0, 0}
};

// simulator available IBM 533 wirings
struct card_wirings wirings[] = {
    {WIRING_8WORD,       "8WORD"},
    {WIRING_RA,          "RA"},
    {WIRING_FDS,         "FDS"},
    {WIRING_SOAP,        "SOAP"}, 
    {WIRING_SOAPA,       "SOAPA"}, 
    {WIRING_SUPERSOAP,   "SUPERSOAP"}, 
    {WIRING_IS,          "IS"}, 
    {WIRING_IT,          "IT"}, 
    {WIRING_FORTRANSIT,  "FORTRANSIT"}, 
    {0, 0},
};


// code of char in IBM 650 memory
char    mem_to_ascii[101] = {
/* 00 */  ' ', '~', '~', '~', '~', '~', '~', '~', '~', '~',
/* 10 */  '~', '~', '~', '~', '~', '~', '~', '~', '.', ')',
/* 20 */  '+', '~', '~', '~', '~', '~', '~', '~', '$', '*',
/* 30 */  '-', '/', '~', '~', '~', '~', '~', '~', ',', '(',
/* 40 */  '~', '~', '~', '~', '~', '~', '~', '~', '=', '-',
/* 50 */  '~', '~', '~', '~', '~', '~', '~', '~', '~', '~',
/* 60 */  '~', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
/* 70 */  '~', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
/* 80 */  '~', '~', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
/* 90 */  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
0};

// representation of word digit 0-9 in card including Y(12) and X(11) punchs
char    digits_ascii[31] = {
          '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',   /* 0-9 */  
          '?', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',   /* 0-9 w/HiPunch Y(12) */
          '!', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',   /* 0-9 w/Negative Punch X(11) */
          0};

uint16          ascii_to_hol[128] = {
   /* Control                              */
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,    /*0-37*/
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*Control*/
    0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,0xf000,
   /*  sp      !      "      #      $      %      &      ' */
   /* none   Y28    78     T28    Y38    T48    XT     48  */
    0x000, 0x600, 0x006, 0x282, 0x442, 0x222, 0xA00, 0x022,     /* 40 - 77 */
   /*   (      )      *      +      ,      -      .      / */
   /* T48    X48    Y48    X      T38    T      X38    T1  */
    0x222, 0x822, 0x422, 0x800, 0x242, 0x400, 0x842, 0x300,
   /*   0      1      2      3      4      5      6      7 */
   /* T      1      2      3      4      5      6      7   */
    0x200, 0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,
   /*   8      9      :      ;      <      =      >      ? */
   /* 8      9      58     Y68    X68    38     68     X28 */
    0x002, 0x001, 0x012, 0x40A, 0x80A, 0x042, 0x00A, 0x882,
   /*   @      A      B      C      D      E      F      G */
   /*  82    X1     X2     X3     X4     X5     X6     X7  */
    0x022, 0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,     /* 100 - 137 */
   /*   H      I      J      K      L      M      N      O */
   /* X8     X9     Y1     Y2     Y3     Y4     Y5     Y6  */
    0x802, 0x801, 0x500, 0x480, 0x440, 0x420, 0x410, 0x408,
   /*   P      Q      R      S      T      U      V      W */
   /* Y7     Y8     Y9     T2     T3     T4     T5     T6  */
    0x404, 0x402, 0x401, 0x280, 0x240, 0x220, 0x210, 0x208,
   /*   X      Y      Z      [      \      ]      ^      _ */
   /* T7     T8     T9     X58    X68    T58    T78     28 */
    0x204, 0x202, 0x201, 0x812, 0x20A, 0x412, 0x406, 0x082,
   /*   `      a      b      c      d      e      f      g */
    0x212, 0xB00, 0xA80, 0xA40, 0xA20, 0xA10, 0xA08, 0xA04,     /* 140 - 177 */
   /*   h      i      j      k      l      m      n      o */
    0xA02, 0xA01, 0xD00, 0xC80, 0xC40, 0xC20, 0xC10, 0xC08,
   /*   p      q      r      s      t      u      v      w */
    0xC04, 0xC02, 0xC01, 0x680, 0x640, 0x620, 0x610, 0x608,
   /*   x      y      z      {      |      }      ~    del */
   /*                     Y78     X78    78     79         */
    0x604, 0x602, 0x601, 0x406, 0x806, 0x006, 0x005,0xf000
};

uint16   sim_ascii_to_hol(char c)
{
    return ascii_to_hol[c & 127];
}

char     sim_hol_to_ascii(uint16 hol)
{
    int c;
    hol = hol & 0x0fff; // ignore extra high bits, if any
    if (hol == 0xa00) return '?'; // +0
    if (hol == 0x600) return '!'; // -0
    for (c=31;c<127;c++) {
        if (ascii_to_hol[c] == hol) {
            // take in consideration the aliases between hol and ascii to return 
            // char as for 026 FORT charset
            // hol = 0x022   -> 8-4   punches -> "-" or "'" or "@".   Must be "-"
            // hol = 0x222   -> 0-8-4 punches -> "(" or "%".          Must be "("  
            if (c == '%') {c = '(';} else
            if (c == '@') {c = '-';} else
            if (c == '\'') {c = '-';};
            return c;
        }
    }
    return '~';
}


/* Initialize vm  */
void
vm_init(void) {
    int i;
    static int inited = 0;

    if (inited == 1)    /* Be sure to only do these things once */
        return;
    inited = 1;

    // Initialize vm memory to all plus zero 
    for(i = 0; i < MAXDRUMSIZE; i++) DRUM[i] = DRUM_NegativeZeroFlag[i] = 0;
    for(i = 0; i < 60; i++) IAS[i] = IAS_NegativeZeroFlag[i] = 0;

    // init specific commands
    sim_vm_cmd = aux_cmds;                       /* set up the auxiliary command table */
}


/* Load a card image file into memory.  */

t_stat
sim_load(FILE * fileref, CONST char *cptr, CONST char *fnam, int flag)
{
   /* Currently not implimented until I know format of load files */
    return SCPE_NOFNC;
}


/* Opcodes */
t_opcode  base_ops[100] = {
        // opcode     name    soap name      R/W? option Valid Data Address Interlock 
        {OP_NOOP,     "NOOP",  "NOP",          0, 0,             vda_DAITS},
        {OP_STOP,     "STOP",  "HLT",          0, 0,             vda_DAITS},
        {OP_UFA,      "FASN",  "UFA",   opReadDA, opStorUnit,    vda_DAIS},
        {OP_RTC,      "RCT",   "RTC",          0, opCntrlUnit,   vda_T,     IL_Tape_and_Unit},
        {OP_RTN,      "RT",    "RTN",          0, opCntrlUnit,   vda_T,     IL_Tape_and_Unit_and_IAS},
        {OP_RTA,      "RAT",   "RTA",          0, opCntrlUnit,   vda_T,     IL_Tape_and_Unit_and_IAS},
        {OP_WTN,      "WT",    "WTN",          0, opCntrlUnit,   vda_T,     IL_Tape_and_Unit_and_IAS},
        {OP_WTA,      "WAT",   "WTA",          0, opCntrlUnit,   vda_T,     IL_Tape_and_Unit_and_IAS},
        {OP_LIB,      "LBB",   "LIB",           0, opStorUnit,    vda_D,     IL_IAS},
        {OP_LDI,      "LB",    "LDI",          0, opStorUnit,    vda_D,     IL_IAS},

        {OP_AU,       "AU",    "AUP",   opReadDA, 0,             vda_DAIS},
        {OP_SU,       "SU",    "SUP",   opReadDA, 0,             vda_DAIS},
        {12,          NULL,    NULL,           0, 0,             0},     
        {13,          NULL,    NULL,           0, 0,             0}, 
        {OP_DIV,      "DIV",   "DIV",   opReadDA, 0,             vda_DAIS},
        {OP_AL,       "AL",    "ALO",   opReadDA, 0,             vda_DAIS},
        {OP_SL,       "SL",    "SLO",   opReadDA, 0,             vda_DAIS},
        {OP_AABL,     "AABL",  "AML",   opReadDA, 0,             vda_DAIS},
        {OP_SABL,     "SABL",  "SML",   opReadDA, 0,             vda_DAIS},
        {OP_MULT,     "MULT",  "MPY",   opReadDA, 0,             vda_DAIS},

        {OP_STL,      "STL",   "STL",  opWriteDA, 0,             vda_DS},
        {OP_STU,      "STU",   "STU",  opWriteDA, 0,             vda_DS},
        {OP_STDA,     "STDA",  "SDA",  opWriteDA, 0,             vda_DS},
        {OP_STIA,     "STIA",  "SIA",  opWriteDA, 0,             vda_DS},
        {OP_STD,      "STD",   "STD",  opWriteDA, 0,             vda_DS},
        {OP_NTS,      "BNTS",  "NTS",          0, opCntrlUnit,   vda_DAIS,  IL_Tape},
        {OP_BIN,      "BIN",   "BIN",          0, opCntrlUnit,   vda_D},
        {OP_SET,      "SET",   "SET",          0, opStorUnit,    vda_S,     IL_IAS},
        {OP_SIB,      "STBB",  "SIB",          0, opStorUnit,    vda_D,     IL_IAS},
        {OP_STI,      "STB",   "STI",          0, opStorUnit,    vda_D,     IL_IAS},
        
        {OP_SRT,      "SRT",   "SRT",          0, 0,             vda_DAITS},
        {OP_SRD,      "SRD",   "SRD",          0, 0,             vda_DAITS},
        {OP_FAD,      "FA",    "FAD",   opReadDA, opStorUnit,    vda_DAIS},
        {OP_FSB,      "FS",    "FSB",   opReadDA, opStorUnit,    vda_DAIS},
        {OP_FDV,      "FD",    "FDV",   opReadDA, opStorUnit,    vda_DAIS},
        {OP_SLT,      "SLT",   "SLT",          0, 0,             vda_DAITS},
        {OP_SCT,      "SCT",   "SCT",          0, 0,             vda_DAITS},
        {OP_FAM,      "FAAB",  "FAM",   opReadDA, opStorUnit,    vda_DAIS},
        {OP_FSM,      "FSAB",  "FSM",   opReadDA, opStorUnit,    vda_DAIS},
        {OP_FMP,      "FM",    "FMP",   opReadDA, opStorUnit,    vda_DAIS},
        
        {OP_NZA,      "BNZA",  "NZA",          0, opStorUnit,    vda_DAIS},
        {OP_BMA,      "BMNA",  "BMA",          0, opStorUnit,    vda_DAIS},
        {OP_NZB,      "BNZB",  "NZB",          0, opStorUnit,    vda_DAIS},
        {OP_BMB,      "BMNB",  "BMB",          0, opStorUnit,    vda_DAIS},
        {OP_BRNZU,    "BRNZU", "NZU",          0, 0,             vda_DAIS},
        {OP_BRNZ,     "BRNZ",  "NZE",          0, 0,             vda_DAIS},
        {OP_BRMIN,    "BRMIN", "BMI",          0, 0,             vda_DAIS},
        {OP_BROV,     "BROV",  "BOV",          0, 0,             vda_DAIS},
        {OP_NZC,      "BNZC",  "NZC",          0, opStorUnit,    vda_DAIS},
        {OP_BMC,      "BMNC",  "BMC",          0, opStorUnit,    vda_DAIS},

        {OP_AXA,      "AA",    "AXA",          0, opStorUnit,    vda_DAS},
        {OP_SXA,      "SA",    "SXA",          0, opStorUnit,    vda_DAS},
        {OP_AXB,      "AB",    "AXB",          0, opStorUnit,    vda_DAS},
        {OP_SXB,      "SB",    "SXB",          0, opStorUnit,    vda_DAS},
        {OP_NEF,      "BRNEF", "NEF",          0, opCntrlUnit,   vda_DAIS,   IL_Tape},
        {OP_RWD,      "RWD",   "RWD",          0, opCntrlUnit,   vda_T,      IL_Tape_and_Unit},
        {OP_WTM,      "WTM",   "WTM",          0, opCntrlUnit,   vda_T,      IL_Tape_and_Unit},
        {OP_BST,      "BSP",   "BST",          0, opCntrlUnit,   vda_T,      IL_Tape_and_Unit},
        {OP_AXC,      "AC",    "AXC",          0, opStorUnit,    vda_DAS},
        {OP_SXC,      "SC",    "SXC",          0, opStorUnit,    vda_DAS},

        {OP_RAU,      "RAU",   "RAU",   opReadDA, 0,             vda_DAIS},
        {OP_RSU,      "RSU",   "RSU",   opReadDA, 0,             vda_DAIS},
        {62,          NULL,    NULL,           0, 0,             0},     
        {OP_TLE,      "TLE",   "TLE",          0, opTLE,         vda_DS},
        {OP_DIVRU,    "DIVRU", "DVR",   opReadDA, 0,             vda_DAIS},
        {OP_RAL,      "RAL",   "RAL",   opReadDA, 0,             vda_DAIS},
        {OP_RSL,      "RSL",   "RSL",   opReadDA, 0,             vda_DAIS},
        {OP_RAABL,    "RAABL", "RAM",   opReadDA, 0,             vda_DAIS},
        {OP_RSABL,    "RSABL", "RSM",   opReadDA, 0,             vda_DAIS},
        {OP_LD,       "LD",    "LDD",   opReadDA, 0,             vda_DAIS},

        {OP_RD,       "RD",    "RD1",          0, 0,             vda_DS,    IL_RD1},
        {OP_PCH,      "PCH",   "WR1",          0, 0,             vda_DS,    IL_WR1},
        {OP_RC1,      "RC1",   "RC1",          0, opStorUnit,    vda_DS,    IL_RD1},
        {OP_RD2,      "RD2",   "RD2",          0, opStorUnit,    vda_DS,    IL_RD23},
        {OP_WR2,      "WR2",   "WR2",          0, opStorUnit,    vda_DS,    IL_WR23},
        {OP_RC2,      "RC2",   "RC2",          0, opStorUnit,    vda_DS,    IL_RD23},
        {OP_RD3,      "RDPRT", "RD3",          0, opStorUnit,    vda_DS,    IL_RD23},
        {OP_WR3,      "PRT",   "WR3",          0, opStorUnit,    vda_DS,    IL_WR23},
        {OP_RC3,      "RCPRT", "RC3",          0, opStorUnit,    vda_DS,    IL_RD23},
        {OP_RPY,      "RPY",   "RPY",          0, opCntrlUnit,   vda_D},

        {OP_RAA,      "RAA",   "RAA",          0, opStorUnit,    vda_DAS},
        {OP_RSA,      "RSA",   "RSA",          0, opStorUnit,    vda_DAS},
        {OP_RAB,      "RAB",   "RAB",          0, opStorUnit,    vda_DAS},
        {OP_RSB,      "RSB",   "RSB",          0, opStorUnit,    vda_DAS},
        {OP_TLU,      "TLU",   "TLU",          0, 0,             vda_DS},
        {OP_SDS,      "SDS",   "SDS",          0, opCntrlUnit,   vda_9000,  IL_RamacUnit_and_Arm},
        {OP_RDS,      "RDS",   "RDS",          0, opCntrlUnit,   vda_9000,  IL_RamacUnit_and_Arm_and_IAS},
        {OP_WDS,      "WDS",   "WDS",          0, opCntrlUnit,   vda_9000,  IL_RamacUnit_and_Arm_and_IAS},
        {OP_RAC,      "RAC",   "RAC",          0, opStorUnit,    vda_DAS},
        {OP_RSC,      "RSC",   "RSC",          0, opStorUnit,    vda_DAS},

        {OP_BRD10,    "BRD10", "BDO",          0, 0,             vda_DAIS},
        {OP_BRD1,     "BRD1",  "BD1",          0, 0,             vda_DAIS},
        {OP_BRD2,     "BRD2",  "BD2",          0, 0,             vda_DAIS},
        {OP_BRD3,     "BRD3",  "BD3",          0, 0,             vda_DAIS},
        {OP_BRD4,     "BRD4",  "BD4",          0, 0,             vda_DAIS},
        {OP_BRD5,     "BRD5",  "BD5",          0, 0,             vda_DAIS},
        {OP_BRD6,     "BRD6",  "BD6",          0, 0,             vda_DAIS},
        {OP_BRD7,     "BRD7",  "BD7",          0, 0,             vda_DAIS},
        {OP_BRD8,     "BRD8",  "BD8",          0, 0,             vda_DAIS},
        {OP_BRD9,     "BRD9",  "BD9",          0, 0,             vda_DAIS}
};

/* Print out an instruction */
void
print_opcode(FILE * of, t_int64 val)
{

    int sgn;
    int IA; 
    int DA; 
    int op;
    int n;
    CONST char * opname;

    if (val < 0) {sgn = -1; val = -val;} else sgn = 1;

    opname = DecodeOpcode(val, &op, &DA, &IA);
    if (opname == NULL) {
       fprintf(of, " %d Unknown opcode", op);
       return; 
    }
    fputs(opname, of);
    n = strlen(opname);
    while (n++<6) fputc(' ', of);
    fprintf(of, "%04d ", DA);
    fputc(' ', of);
    fprintf(of, "%04d ", IA);
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
    t_int64            d, inst;
    int                NegZero;
    int ch;

    if (*val == NEGZERO_value) {
        inst = 0;
        NegZero = 1;
    } else {
        inst = *val; 
        NegZero = 0;
    }

    /* Print value in decimal */
    fputc(' ', of);
    fprintf(of, "%06d%04d%c", printfw(inst,NegZero));    // fprintf 10 digits word n, with sign
    inst = AbsWord(inst);

    if (sw & SWMASK('C') ) {
        int                 i;

        d = inst;
        fputs("   '", of);
        for (i=0;i<5;i++) {
            ch = Shift_Digits(&d, 2);
            fputc(mem_to_ascii[ch], of);
        }
        fputc('\'', of);
    }

    if (sw & SWMASK('M')) {
        fputs("   ", of);
        inst = AbsWord(inst);
        print_opcode(of, inst);
    }
    return SCPE_OK;
}

int 
find_opcode(char *op)
{
    int i;
    if (op == NULL) return -1;
    for (i=0;i<100;i++) {
        if (base_ops[i].name1 == NULL) continue;
        // accept both mnemonic sets: operation manual one (name1) and soap one (name2)
        if ((base_ops[i].name1 != NULL) && (strcmp(op, base_ops[i].name1) == 0))
            return i;
        if ((base_ops[i].name2 != NULL) && (strcmp(op, base_ops[i].name2) == 0))
            return i;
    }
    return -1;
}

/* read n digits, optionally with sign NNNN[+|-]

   Inputs:
        *cptr   =       pointer to input string
        sgnFlag =       1 to allow signed value
   Outputs:
        d       =       parsed value
*/

CONST char * parse_sgn(int *neg, CONST char *cptr)
{
    *neg=0;
    while (isspace(*cptr)) cptr++;
    if (*cptr == '+') {
        cptr++; 
    } else if (*cptr == '-') {
        cptr++; *neg = 1;
    }
    return cptr;
}

CONST char * parse_n(t_int64 *d, CONST char *cptr, int n)
{
    int i = 0;

    *d = 0;
    while (1) {
        if ((n == 10) && (isspace(*cptr))) {
            cptr++;  // on 10 digit words, allow spaces
            continue;
        }
        if (*cptr < '0' || *cptr > '9') break;
        if (i++ > n) {
            cptr++;
        } else {
            *d = (*d * 10) + (*cptr++ - '0');
        }
    }
    if (n ==  4) {*d = *d % D4; } else 
    if (n == 10) {*d = *d % D10;}  
    return cptr;
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

// convert ascii char to two digits IBM 650 code
int ascii_to_NN(int ch)
{
    int i;

    if ((ch >= 'a') && (ch <= 'z')) ch = ch -'a'+'A';
    for (i=0;i<100;i++) if (mem_to_ascii[i] == ch) return i;
    return 0;
}

t_stat parse_sym(CONST char *cptr, t_addr addr, UNIT * uptr, t_value * val, int32 sw)
{
    t_int64             d;
    int                 op, da, ia;
    char                ch, opcode[100];
    int                 i;
    int neg, IsNeg;

    while (isspace(*cptr)) cptr++;
    d = 0; IsNeg = 0;
    if (sw & SWMASK('M')) {
        /* Grab opcode */
        cptr = parse_sgn(&neg, cptr);
        if (neg) IsNeg = 1;

        cptr = get_glyph(cptr, opcode, 0);

        op = find_opcode(opcode);
        if (op < 0) return STOP_UUO;

        if (DecodeOpcode(op * (t_int64) D8, &op, &da, &ia) == NULL) {
            // opcode exists, but not availble because associated hw (Storage Unit or Control Unit) 
            // is not enabled
            return STOP_UUO;
        }

        while (isspace(*cptr)) cptr++;
        /* Collect first argument: da */
        cptr = parse_n(&d, cptr, 4);
        da = (int) d; 

        /* Skip blanks */
        while (isspace(*cptr)) cptr++;
        /* Collect second argument: ia */
        cptr = parse_n(&d, cptr, 4);
        ia = (int) d; 
        // construct inst
        d = op * (t_int64) D8 + da * (t_int64) D4 + (t_int64) ia;
    } else if (sw & SWMASK('C')) {
        d = 0;
        if ((*cptr == 34) || (*cptr == 39)) cptr++; // skip double or single quotes if present
        for(i=0; i<5;i++) {
            d = d * 100;
            ch = *cptr; 
            if (ch == '\0') continue;
            if ((*cptr == 34) || (*cptr == 39)) continue; // double or single quotes mark end of text
            cptr++;
            d = d + ascii_to_NN(ch);
        }
    } else {
        cptr = parse_sgn(&neg, cptr);
        if (neg) IsNeg = 1;
        cptr = parse_n(&d, cptr, 10);
    }
    cptr = parse_sgn(&neg, cptr);
    if (neg) IsNeg = 1;
    if ((IsNeg) && (d == 0)) {
        *val = NEGZERO_value; // val has this special value to represent -0 (minus zero == negative zero) 
    } else {
        if (IsNeg) d=-d;
        *val = (t_value) d;
    }
    return SCPE_OK;
}

/* Helper functions */

// set in buf string ascii chars form word d ( chars: c1c2c3c4c5 )
// starts at char start (1..5), for CharLen chars (0..5)
// to convert the full word use (buf, 1, 5, d)
char * word_to_ascii(char * buf, int CharStart, int CharLen, t_int64 d)
{
    int i,c1,c2;
    char * buf0;

    buf0 = buf; // save start of buffer
    for (i=0;i<5;i++) {        // 5 alpha chars per word
        c1 = Shift_Digits(&d, 2);
        c2 = mem_to_ascii[c1];
        if (i < CharStart-1) continue;
        if (i >= CharStart+CharLen-1) continue;
        *buf++ = c2;
    }
    *buf++ = 0;
    return buf0;
}



// return hi digit (digit 10) al leftmost position in number (no sign)
int Get_HiDigit(t_int64 d)  
{
    return (int) ((AbsWord(d) * 10) / D10);
}

// shift d value for nDigits positions (max 7)
// if nDigit > 0 shift left, if < 0 then shift right
// return value of shifted digits (without sign)
int Shift_Digits(t_int64 * d, int nDigits)  
{
    int i,n;
    int neg = 0;

    if (nDigits == 0) return 0;                           // no shift

    if (*d < 0) {*d=-*d; neg = 1;}

    n = 0;
    if (nDigits > 0) {                                    // shift left
        for (i=0;i<nDigits;i++) {
            n  = n * 10 + (int) (*d / (1000000000L));     // nine digits (9 zeroes)
            *d = (*d % (1000000000L)) * 10;      
        }
    } else {                                              // shift right
        for (i=0;i<-nDigits;i++) {
            n = *d % 10;
            *d = *d / 10;      
        }
    }
    if (neg) *d=-*d;
    return n;
}
/* deck operations 

   carddeck [-q] <operation> <parameters...>

                        allowed operations are split, join, print

                        default format for card files is AUTO, this allow to intermix source decks
                        with different formats. To set the format for carddeck operations use

                           set cpr0 -format xxxx
                        
                        this will apply to all operations, both on reading and writing deck files

   carddeck split       split the deck being punched in IBM 533 device in two separate destination decks

                        carddeck split <count> <dev|file0> <file1> <file2>

                        <dev>    should be cdp1 to cdp3. File must be attached. The cards punched on 
                                 this file are the ones on source deck to split.

                        <file0>  if instead of cdp1, cdp2 or cdp3, a file can be specified containing
                                 the source deck to be splitted

                        <count>  number of cards in each splitted deck. 
                                 If count > 0, indicates the cards on first destination deck file 
                                               remaining cards go to the second destination deck
                                 If count < 0, indicates the cards on second destination deck file 
                                               (so deck 2 contains lasts count cards from source)
                                 if cound is 5cd, file2 received 5 words-per-load-card deck
                                               if file2 has no cards, it is deleted.

                        <file1>  first destination deck file
                        <file2>  second destination deck file
                                 
                        when using <dev> as source both <file1> or <file2> can have same name as the currently 
                        attached file to cdp device. On command execution, cdp gest its file detached.
                        file1 and file are created (overwritten if already exists).

                        when using <file0> as source both <file1> or <file2> can have same name as <file0>.
                        <file0> is completly read by SimH in its internal buffer (room for 10K cards) 
                        and then splitted to <file1> and <file2>. 

   carddeck join        join several deck files into a new one

                        carddeck join <file1> <file2> ... as <file>

                        <file1>  first source deck file
                        <file2>  second source deck file
                        ...
                        <file>   destination deck file

                        any source file <file1>, <file2>, etc can have same name as destination file <file>.
                        Each source file is completly read in turn by SimH in its internal buffer (room for 10K cards) 
                        and then written on destination file. This allos to append une deck on the top/end of 
                        another one.

   carddeck print       print deck on console, and on simulated IBM 407 is any file is attached to cpd0

                        carddeck print <file>                         

   carddeck echolast    echo on console last n cards already read that are in the take hopper

                        carddeck echolasty <count> <dev> 

                        <count>  number of cards to display (upo to 10)

                        <dev>    should be cdr1 to cdr3. Unit for Take hopper


   switches:            if present mut be just after carddeck and before deck operation
    -Q                  quiet return status. 
           
*/

// load card file fn and add its cards to 
// DeckImage array, up to a max of nMaxCards
// increment nCards with the number of added cards
// uses cdr0 device/unit
t_stat deck_load(CONST char *fn, uint16 * DeckImage, int * nCards)
{
    UNIT *              uptr = &cdr_unit[0];
    uint16 image[80];    
    t_stat              r, r2;
    int i;
    uint16 c;

    // set flags for read only
    uptr->flags |= UNIT_RO; 

    // attach file to cdr unit 0
    r = (cdr_dev.attach)(uptr, fn);
    if (r != SCPE_OK) return r;

    // read all cards from file
    while (1) {

        if (*nCards >= MAX_CARDS_IN_DECK) {
            r = sim_messagef (SCPE_IERR, "Too many cards\n");
            break;
        }
        r = sim_read_card(uptr, image);
        if ((r == CDSE_EOF) || (r == CDSE_EMPTY)) {
            r = SCPE_OK; break;             // normal termination on card file read finished
        } else if (r != CDSE_OK) {
            break;                          // abnormal termination on error
        }
        // add card read to deck
        for (i=0; i<80; i++) {
            c = image[i];
            DeckImage[*nCards * 80 + i] = c & 0xFFF;
        }
        *nCards = *nCards + 1;
    }

    // deattach file from cdr unit 0
    r2 = (cdr_dev.detach)(uptr);
    if (r == SCPE_OK) r = r2; 
    if (r != SCPE_OK) return r;

    return SCPE_OK;
}

// write nCards starting at card from DeckImage array to file fn
// uses cdr0 device/unit
t_stat deck_save(CONST char *fn, uint16 * DeckImage, int card, int nCards)
{
    UNIT *              uptr = &cdr_unit[0];
    uint16 image[80];
    t_stat              r;
    int i,nc;

    // set flags for create new file
    uptr->flags &= ~UNIT_RO; 
    sim_switches |= SWMASK ('N');

    // attach file to cdr unit 0
    r = (cdr_dev.attach)(uptr, fn);
    if (r != SCPE_OK) return r;

    // write cards to file
    for  (nc=0;nc<nCards;nc++) {
        if (nc + card >= MAX_CARDS_IN_DECK) {
            r = sim_messagef (SCPE_IERR, "Reading outside of Deck\n");
            break;
        }

        // read card from deck
        for (i=0; i<80; i++) image[i] = DeckImage[(nc + card) * 80 + i];

        r = sim_punch_card(uptr, image);
        if (r != CDSE_OK) break;    // abnormal termination on error
    }

    // deattach file from cdr unit 0
    (cdr_dev.detach)(uptr);

    return r;
}

// echo/print nCards from DeckImage array 
// uses cdp0 device/unit
void deck_print_echo(uint16 * DeckImage, int nCards, int bPrint, int bEcho)
{
    char line[81]; 
    int i,c,nc;
    uint16 hol;

    for (nc=0; nc<nCards; nc++) {
        // read card, check and, store in line
        for (i=0;i<80;i++) {
            hol = DeckImage[nc * 80 + i];
            c = sim_hol_to_ascii(hol);
            c = toupper(c);                             // IBM 407 can only print uppercase
            if ((c == '?') || (c == '!')) c = '0';      // remove Y(12) or X(11) punch on zero 
            if (strchr(mem_to_ascii, c) == 0) c = ' ';  // space if not in IBM 650 character set
            line[i] = c;
        }
        line[80]=0;
        sim_trim_endspc(line); 
        // echo on console (add CR LF)
        if (bEcho) {
            for (i=0;i<(int)strlen(line);i++) sim_putchar(line[i]);     
            sim_putchar(13);sim_putchar(10);
        }
        // printout will be directed to file attached to CDP0 unit, if any
        if ((bPrint) && (cdp_unit[0].flags & UNIT_ATT)) {
            sim_fwrite(line, 1, strlen(line), cdp_unit[0].fileref); // fwrite clears line!
            line[0] = 13; line[1] = 10; line[2] = 0;  
            sim_fwrite(line, 1, 2, cdp_unit[0].fileref); 
        }
    }
}

// carddeck split <count> <dev|file0> <file1> <file2>
// carddeck split   5CD   <dev|file0> <file1> <file2>
// carddeck split   PAT   <dev|file0> <file1> <file2>
static t_stat deck_split_cmd(CONST char *cptr)
{
    char fn0[4*CBUFSIZE];
    char fn1[4*CBUFSIZE];
    char fn2[4*CBUFSIZE];

    char gbuf[4*CBUFSIZE];
    DEVICE *dptr;
    UNIT *uptr;
    t_stat r;
    int bSplit5CD = 0;
    int bSplitPAT = 0;

    uint16 DeckImage[80 * MAX_CARDS_IN_DECK];
    int nCards, nCards1, tail; 

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    if (*cptr == '-') {
        tail = 1;
        cptr++;
    } else {
        tail = 0;
    }
    nCards1 = 0;
    cptr = get_glyph (cptr, gbuf, 0);                       // get cards count param    
    if ((tail == 0) && (strlen(gbuf) == 3) && (strncmp(gbuf, "5CD", 3) == 0)) {
        // split 5-words per card load cards fron deck
        bSplit5CD = 1;
    } else if ((tail == 0) && (strlen(gbuf) == 3) && (strncmp(gbuf, "PAT", 3) == 0)) {
        // split availability table load cards fron deck
        bSplitPAT = 1;
    } else {
        // 
        nCards1 = (int32) get_uint (gbuf, 10, 10000, &r);
        if (r != SCPE_OK) return sim_messagef (SCPE_ARG, "Invalid count value\n");
        if (nCards1 == 0) return sim_messagef (SCPE_ARG, "Count cannot be zero\n");
    }

    get_glyph (cptr, gbuf, 0);                              // get dev param 
    cptr = get_glyph_quoted (cptr, fn0, 0);                 // re-read using get_glyph_quoted to do not 
                                                            // change the capitalization of file name
    if ((strlen(gbuf) != 4) || (strncmp(gbuf, "CDP", 3)) ||
        (gbuf[3] < '1') || (gbuf[3] > '3') ) {
        // is a file
    } else {
        // is cdp1 cdp2 or cdp3 device
        dptr = find_unit (gbuf, &uptr);                     /* locate unit */
        if (dptr == NULL)                                   /* found dev? */
            return SCPE_NXDEV;
        if (uptr == NULL)                                   /* valid unit? */
            return SCPE_NXUN;
        if ((uptr->flags & UNIT_ATT) == 0)                  /* attached? */
            return SCPE_UNATT;
        // get the file name
        strcpy(fn0, uptr->filename);
        sim_card_detach(uptr);                              // detach file from cdp device to be splitted
    }

    // read source deck
    nCards = 0;
    r = deck_load(fn0, DeckImage, &nCards);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot read source deck (%s)\n", fn0);

    // calc nCards1 = cards in first deck
    if (tail) {
        // calc cards remaining when last nCardCount are removed from source deck
        nCards1 = nCards - nCards1;
        if (nCards1 < 0) nCards1 = 0;
    }
    if (nCards1 > nCards) nCards1 = nCards;

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fn1, 0);                 // get next param: filename 1
    if (fn1[0] == 0) return sim_messagef (SCPE_ARG, "Missing first filename\n");
    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fn2, 0);                 // get next param: filename 2
    if (fn2[0] == 0) return sim_messagef (SCPE_ARG, "Missing second filename\n");
    
    if (bSplit5CD ) {
        // separate 5cd deck 
        uint16 DeckImage1[80 * MAX_CARDS_IN_DECK];
        uint16 DeckImage2[80 * MAX_CARDS_IN_DECK]; 
        int i, nc, nc1, nc2, bFound;
        uint16 hol;

        nc1 = nc2 = 0;
        for (nc=0; nc<nCards; nc++) {
            // determnine type of load card: is regular 1 word per card or 5 words per card
            bFound = 0;                         // soap4 5cd cards have non blanks cols 11 to 17
            for (i=10;i<16;i++) {               // soap4 5cd also col 1 = "0" and col2 = "1"
                hol = DeckImage[nc * 80 + i];   
                if (hol != 0) bFound++; 
            }
            if (bSplit5CD) {
                if ((DeckImage[nc * 80 + 0] != 0x200) || (DeckImage[nc * 80 + 1] != 0x100)) bFound = 0; 
            } else {
                if ((DeckImage[nc * 80 + 0] != 0x200) || (DeckImage[nc * 80 + 1] != 0x200)) bFound = 0; 
            }
            hol=0;
            for (i=0;i<6;i++) {
                if (DeckImage[nc * 80 + i] == 0x002) hol++;
            }
            if (hol==6) bFound = 6; // supersoap fiv cards starts with six 8's

            bFound = (bFound == 6) ? 1:0; // is a 5 words-per-card load card?
            // store in appropiate output deck
            for (i=0;i<80;i++) {
                hol = DeckImage[nc * 80 + i];
                if (bFound==0) {
                    DeckImage1[nc1 * 80 + i] = hol;
                } else {
                    DeckImage2[nc2 * 80 + i] = hol;
                }
            }
            if (bFound==0) {
                nc1++;
            } else {
                nc2++;
            }
        }
        // save output decks
        r = deck_save(fn1, DeckImage1, 0, nc1);
        if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck1 (%s)\n", fn0);
        r = deck_save(fn2, DeckImage2, 0, nc2);
        if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck2 (%s)\n", fn0);
        if (nc2 == 0) remove(fn2);  // delete file2 if empty
        if ((sim_switches & SWMASK ('Q')) == 0) {
           sim_messagef (SCPE_OK, "Deck with 5 words-per-card splitted %d/%d cards\n", nc1, nc2);
        }
        return SCPE_OK;
    }

    if (bSplitPAT)  {
        // separate pat deck 
        uint16 DeckImage1[80 * MAX_CARDS_IN_DECK];
        uint16 DeckImage2[80 * MAX_CARDS_IN_DECK]; 
        int i, nc, nc1, nc2, bFound;
        uint16 hol;

        nc1 = nc2 = 0;
        for (nc=0; nc<nCards; nc++) {
            // PAT table has 8 words with hi punch on last digit
            bFound = 0;                         
            for (i=1;i<=8;i++) {    
                hol = DeckImage[nc * 80 + i*10-1]; 
                if (hol & 0x800) bFound++; 
            }
            bFound = (bFound == 8) ? 1:0; // is an availability table load card?
            // store in appropiate output deck
            for (i=0;i<80;i++) {
                hol = DeckImage[nc * 80 + i];
                if (bFound==0) {
                    DeckImage1[nc1 * 80 + i] = hol;
                } else {
                    DeckImage2[nc2 * 80 + i] = hol;
                }
            }
            if (bFound==0) {
                nc1++;
            } else {
                nc2++;
            }
        }
        // save output decks
        r = deck_save(fn1, DeckImage1, 0, nc1);
        if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck1 (%s)\n", fn0);
        r = deck_save(fn2, DeckImage2, 0, nc2);
        if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck2 (%s)\n", fn0);
        if (nc2 == 0) remove(fn2);  // delete file2 if empty
        if ((sim_switches & SWMASK ('Q')) == 0) {
           sim_messagef (SCPE_OK, "Deck with availability-card splitted %d/%d cards\n", nc1, nc2);
        }
        return SCPE_OK;
    }

    // split based on card count
    r = deck_save(fn1, DeckImage, 0, nCards1);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck1 (%s)\n", fn0);

    r = deck_save(fn2, DeckImage, nCards1, nCards-nCards1);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck2 (%s)\n", fn0);

    if ((sim_switches & SWMASK ('Q')) == 0) {
        sim_messagef (SCPE_OK, "Deck splitted to %d/%d cards\n", nCards1, nCards-nCards1);
    }
    return SCPE_OK;

}

// carddeck join <file1> <file2> ... as <file>
static t_stat deck_join_cmd(CONST char *cptr)
{
    char fnSrc[4*CBUFSIZE];
    char fnDest[4*CBUFSIZE];
    CONST char *cptr0;
    CONST char *cptrAS;
    char gbuf[4*CBUFSIZE];
    t_stat r;

    uint16 DeckImage[80 * MAX_CARDS_IN_DECK];
    int i,nDeck, nCards, nCards1;

    cptr0 = cptr;
    // look for "as"
    while (*cptr) {
        while (sim_isspace (*cptr)) cptr++;                 // trim leading spc 
        cptrAS = cptr; // mark position of AS
        cptr = get_glyph_quoted (cptr, gbuf, 0);            // get next param
        if (gbuf[0] == 0) return sim_messagef (SCPE_ARG, "AS <file> not found\n");
        for (i=0;i<2;i++) gbuf[i] = sim_toupper(gbuf[i]);
        if (strcmp(gbuf, "AS") == 0) break;
    }

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fnDest, 0);              // get next param: destination filename 
    if (fnDest[0] == 0) return sim_messagef (SCPE_ARG, "Missing destination filename\n");
    if (*cptr) return sim_messagef (SCPE_ARG, "Extra unknown parameters after destination filename\n");

    cptr = cptr0;                                           // restore cptr to scan source filenames
    nDeck = nCards = 0;
    memset(DeckImage, 0, sizeof(DeckImage));
    while (1) {

        while (sim_isspace (*cptr)) cptr++;                 // trim leading spc 
        if (cptrAS == cptr) break;                          // break if reach "AS"
        cptr = get_glyph_quoted (cptr, fnSrc, 0);           // get next param: source filename 
        if (fnSrc[0] == 0) return sim_messagef (SCPE_ARG, "Missing source filename\n");

        // read source deck
        nCards1 = nCards;
        r = deck_load(fnSrc, DeckImage, &nCards);
        if (r != SCPE_OK) return sim_messagef (r, "Cannot read source deck (%s)\n", fnSrc);
        nDeck++;

        if ((sim_switches & SWMASK ('Q')) == 0) {
            sim_messagef (SCPE_OK, "Source Deck %d has %d cards (%s)\n", nDeck, nCards - nCards1, fnSrc);
        }
    }
    r = deck_save(fnDest, DeckImage, 0, nCards);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot write destination deck (%s)\n", fnDest);

    if ((sim_switches & SWMASK ('Q')) == 0) {
        sim_messagef (SCPE_OK, "Destination Deck has %d cards (%s)\n", nCards, fnDest);
    }
    
    return SCPE_OK;
}

// carddeck print <file> 
static t_stat deck_print_cmd(CONST char *cptr)
{
    char fn[4*CBUFSIZE];
    t_stat r;

    uint16 DeckImage[80 * MAX_CARDS_IN_DECK];
    int nCards;

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 
    cptr = get_glyph_quoted (cptr, fn, 0);                  // get next param: source filename 
    if (fn[0] == 0) return sim_messagef (SCPE_ARG, "Missing filename\n");
    if (*cptr) return sim_messagef (SCPE_ARG, "Extra unknown parameters after filename\n");

    // read deck to be printed (-1 to convert to ascii value, not hol)
    nCards = 0;
    r = deck_load(fn, DeckImage, &nCards);
    if (r != SCPE_OK) return sim_messagef (r, "Cannot read deck to print (%s)\n", fn);

    deck_print_echo(DeckImage, nCards, 1,1);

    if ((sim_switches & SWMASK ('Q')) == 0) {
        sim_messagef (SCPE_OK, "Printed Deck with %d cards (%s)\n", nCards, fn);
    }
    
    return SCPE_OK;
}

// carddeck echolast <dev> <count>
static t_stat deck_echolast_cmd(CONST char *cptr)
{
    char gbuf[4*CBUFSIZE];
    t_stat r;

    uint16 DeckImage[80 * MAX_CARDS_IN_DECK];
    int i,nc,nCards, ic, nh, ncdr;

    while (sim_isspace (*cptr)) cptr++;                     // trim leading spc 

    cptr = get_glyph (cptr, gbuf, 0);                       // get cards count param    
    nCards = (int32) get_uint (gbuf, 10, MAX_CARDS_IN_READ_STAKER_HOPPER, &r);
    if (r != SCPE_OK) return sim_messagef (SCPE_ARG, "Invalid count value\n");
    if (nCards == 0) return sim_messagef (SCPE_ARG, "Count cannot be zero\n");

    cptr = get_glyph (cptr, gbuf, 0);                       // get dev param    
    if ((strlen(gbuf) != 4) || (strncmp(gbuf, "CDR", 3)) ||
        (gbuf[3] < '1') || (gbuf[3] > '3') ) {
        return sim_messagef (SCPE_ARG, "Device should be CDR1 CDR2 or CDR3\n");
    }
    ncdr = gbuf[3] - '1'; // ncdr=0 for cdr1, =1 for cdr2, and so on
    if ((ncdr >= 0) && (ncdr < 3)){
        // safety check
    } else {
        return sim_messagef (SCPE_ARG, "Invalid Device number\n");
    }

    if (*cptr) return sim_messagef (SCPE_ARG, "Extra unknown parameters\n");

    // get nCards form read card take hopper buffer
    // that is, print last nCards read

    // get last nCards cards, so
    // first card to echo is count ones before last one
    nh = ReadStakerLast[ncdr] - (nCards-1);                 
    nh = nh % MAX_CARDS_IN_READ_STAKER_HOPPER;
    for (nc=0; nc<nCards; nc++) {
        // copy card form read hopper buf to deck image
        ic = (ncdr * MAX_CARDS_IN_READ_STAKER_HOPPER + nh) * 80;
        for (i=0;i<80;i++) DeckImage[nc * 80 + i] = ReadStaker[ic + i];
        // get previous read card
        nh = (nh + 1) % MAX_CARDS_IN_READ_STAKER_HOPPER;
    }

    deck_print_echo(DeckImage, nCards, 0,1);

    if ((sim_switches & SWMASK ('Q')) == 0) {
        sim_messagef (SCPE_OK, "Last %d cards from Read take Hopper\n", nCards);
    }
        
    return SCPE_OK;
}

static t_stat ibm650_deck_cmd(int32 arg, CONST char *buf)
{
    char gbuf[4*CBUFSIZE];
    const char *cptr;

    cptr = get_glyph (buf, gbuf, 0);                   // get next param
    if (strcmp(gbuf, "-Q") == 0) {
        sim_switches |= SWMASK ('Q');
        cptr = get_glyph (cptr, gbuf, 0);
    }

    if (strcmp(gbuf, "JOIN") == 0) {
        return deck_join_cmd(cptr);
    }
    if (strcmp(gbuf, "SPLIT") == 0) {
        return deck_split_cmd(cptr);
    }
    if (strcmp(gbuf, "PRINT") == 0) {
        return deck_print_cmd(cptr);
    }
    if (strcmp(gbuf, "ECHOLAST") == 0) {
        return deck_echolast_cmd(cptr);
    }
    return sim_messagef (SCPE_ARG, "Unknown deck command operation\n");
}




