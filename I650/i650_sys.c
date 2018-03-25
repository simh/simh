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
//XXX    &mta_dev,
    NULL
};

/* Device addressing words */

DIB  cdr_dib = { 1, &cdr_cmd, NULL };
DIB  cdp_dib = { 3, &cdp_cmd, NULL };
//XXX DIB  mt_dib = { CH_TYP_76XX, NUM_UNITS_MT, 0000, 0000, &mt_cmd, &mt_ini };

/* Simulator stop codes */
const char         *sim_stop_messages[] = {
    "Unknown error",
    "HALT instruction",
    "Breakpoint",
    "Unknown Opcode",
    "Card Read/Punch Error",
    "Programmed Stop",
    "Overflow",
    "Opcode Execution Error",
    "Address Error",
    0
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

// code of char in IBM 650 memory
char    mem_to_ascii[100] = {
/* 00 */  ' ', '~', '~', '~', '~', '~', '~', '~', '~', '~',
/* 10 */  '~', '~', '~', '~', '~', '~', '~', '~', '.', ')',
/* 20 */  '+', '~', '~', '~', '~', '~', '~', '~', '$', '*',
/* 30 */  '-', '/', '~', '~', '~', '~', '~', '~', ',', '(',
/* 40 */  '~', '~', '~', '~', '~', '~', '~', '~', '=', '-',
/* 50 */  '~', '~', '~', '~', '~', '~', '~', '~', '~', '~',
/* 60 */  '~', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
/* 70 */  '~', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
/* 80 */  '~', '~', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
/* 90 */  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

// representation of word digit 0-9 in card including Y(12) and X(11) punchs
char    digits_ascii[40] = {
          '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',   /* 0-9 */  
          '?', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',   /* 0-9 w/HiPunch Y(12) */
          '!', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',   /* 0-9 w/Negative Punch X(11) */
          '&', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '#'    /* 0-9 with botch Negative Punch X(11) and HiPunch Y(12)*/
};

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
    0x604, 0x602, 0x601, 0x406, 0x806,0x0006,0x0005,0xf000
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
    uint8               bReadData;      // =1 if inst fetchs data from memory
}
t_opcode;

/* Opcodes */
t_opcode  base_ops[] = {
        {OP_AABL,     "AABL",       1},
        {OP_AL,       "AL",         1},
        {OP_AU,       "AU",         1},
        {OP_BRNZ,     "BRNZ",       0},
        {OP_BRMIN,    "BRMIN",      0},
        {OP_BRNZU,    "BRNZU",      0},
        {OP_BROV,     "BROV",       0},
        {OP_BRD1,     "BRD1",       0},
        {OP_BRD2,     "BRD2",       0},
        {OP_BRD3,     "BRD3",       0},
        {OP_BRD4,     "BRD4",       0},
        {OP_BRD5,     "BRD5",       0},
        {OP_BRD6,     "BRD6",       0},
        {OP_BRD7,     "BRD7",       0},
        {OP_BRD8,     "BRD8",       0},
        {OP_BRD9,     "BRD9",       0},
        {OP_BRD10,    "BRD10",      0},
        {OP_DIV,      "DIV",        1},
        {OP_DIVRU,    "DIVRU",      1},
        {OP_LD,       "LD",         1},
        {OP_MULT,     "MULT",       1},
        {OP_NOOP,     "NOOP",       0},
        {OP_PCH,      "PCH",        0},
        {OP_RD,       "RD",         0},
        {OP_RAABL,    "RAABL",      1},
        {OP_RAL,      "RAL",        1},
        {OP_RAU,      "RAU",        1},
        {OP_RSABL,    "RSABL",      1},
        {OP_RSL,      "RSL",        1},
        {OP_RSU,      "RSU",        1},
        {OP_SLT,      "SLT",        0},
        {OP_SCT,      "SCT",        0},
        {OP_SRT,      "SRT",        0},
        {OP_SRD,      "SRD",        0},
        {OP_STOP,     "STOP",       0},
        {OP_STD,      "STD",        0},
        {OP_STDA,     "STDA",       0},
        {OP_STIA,     "STIA",       0},
        {OP_STL,      "STL",        0},
        {OP_STU,      "STU",        0},
        {OP_SABL,     "SABL",       1},
        {OP_SL,       "SL",         1},
        {OP_SU,       "SU",         1},
        {OP_TLU,      "TLU",        0},
        {0,           NULL,         0}
};

/* Print out an instruction */
void
print_opcode(FILE * of, t_int64 val, t_opcode * tab)
{

    int sgn;
    int IA; 
    int DA; 
    int op;
    int n;

    if (val < 0) {sgn = -1; val = -val;} else sgn = 1;
    op = Shift_Digits(&val, 2);          // opcode
    DA = Shift_Digits(&val, 4);          // data address
    IA = Shift_Digits(&val, 4);          // intruction address

    while (tab->name != NULL) {
        if (tab->opbase == op) {
            fputs(tab->name, of);
            n = strlen(tab->name);
            while (n++<6) fputc(' ', of);
            fprintf(of, "%04d ", DA);
            fputc(' ', of);
            fprintf(of, "%04d ", IA);
            return;
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
    t_int64            inst;
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

        fputs("   '", of);
        for (i=0;i<5;i++) {
            ch = Shift_Digits(&inst, 2);
            fputc(mem_to_ascii[ch], of);
        }
        fputc('\'', of);
    }

    if (sw & SWMASK('M')) {
        fputs("   ", of);
        inst = AbsWord(inst);
        print_opcode(of, inst, base_ops);
    }
    return SCPE_OK;
}

t_opcode *
find_opcode(char *op, t_opcode * tab)
{
    while (tab->name != NULL) {
        if (*tab->name != '\0' && strcmp(op, tab->name) == 0)
            return tab;
        tab++;
    }
    return NULL;
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
    int                 da, ia;
    char                ch, opcode[100];
    t_opcode            *op;
    int                 i;
    int neg, IsNeg;

    while (isspace(*cptr)) cptr++;
    d = 0; IsNeg = 0;
    if (sw & SWMASK('M')) {
        /* Grab opcode */
        cptr = parse_sgn(&neg, cptr);
        if (neg) IsNeg = 1;

        cptr = get_glyph(cptr, opcode, 0);

        op = find_opcode(opcode, base_ops);
        if (op == 0) return STOP_UUO;

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
        d = op->opbase * (t_int64) D8 + da * (t_int64) D4 + (t_int64) ia;
    } else if (sw & SWMASK('C')) {
        d = 0;
        for(i=0; i<5;i++) {
            d = d * 100;
            ch = *cptr; 
            if (ch == '\0') continue;
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

// get data for opcode
// return pointer to opcode name if opcode found, else NULL
const char * get_opcode_data(int opcode, int * bReadData)
{
    t_opcode * tab = base_ops; 

    *bReadData = 0;
    while (tab->name != NULL) {
        if (tab->opbase == opcode) {
            *bReadData  = tab->bReadData;
            return tab->name;
        }
        tab++;
    }
    return NULL;
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



