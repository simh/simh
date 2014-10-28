/* s3_sys.c: IBM System/3 system interface

   Copyright (c) 2001-2012, Charles E. Owen

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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

   19-Mar-12    RMS     Fixed declaration of conversion tables (Mark Pizzolato)
*/

#include <ctype.h>
#include "s3_defs.h"

extern DEVICE cpu_dev;
extern DEVICE pkb_dev;
extern DEVICE cdr_dev;
extern DEVICE cdp_dev;
extern DEVICE stack_dev;
extern DEVICE lpt_dev;
extern DEVICE r1_dev;
extern DEVICE f1_dev;
extern DEVICE r2_dev;
extern DEVICE f2_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern unsigned char M[];
extern int32 saved_PC, IAR[];
extern unsigned char ebcdic_to_ascii[];
char *parse_addr(char *cptr,  char *gbuf, t_addr *addr, int32 *addrtype);

int32 printf_sym (FILE *of, char *strg, t_addr addr, uint32 *val,
    UNIT *uptr, int32 sw);

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "System/3";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 6;

DEVICE *sim_devices[] = {
    &cpu_dev, 
    &pkb_dev,
    &cdr_dev,
    &cdp_dev,
    &stack_dev,
    &lpt_dev,
    &r1_dev,
    &f1_dev,
    &r2_dev,
    &f2_dev,
    NULL
};

const char *sim_stop_messages[] = {
    "Unknown error",
    "Unknown I/O Instruction",
    "HALT instruction",
    "Breakpoint",
    "Invalid Opcode",
    "Invalid Qbyte",
    "Invalid Address",
    "Invalid Device Command",
    "ATTN Card Reader"
};

/* This is the opcode master defintion table.  Each possible opcode mnemonic
   is defined here, with enough information to translate to and from
   symbolic to binary machine code.  
   First field is the opcode's mnemonic
   Second field is the hex of the right nybble of the binary opcode
   Third field is the Q code for those with implicit Q codes
   Fourth field is the symbolic format of the operands:
        0 - (Q-byte),(R-byte)
        1 - (Q-byte),(Address)
        2 - (Address),(Address),(Qbyte)
        3 - (Address),(Qbyte)
        4 - (device),(modifier),(function) -- these 3 make up qbyte
        5 - (device),(modifier),(function),(control)
        6 - (device),(modifier),(function),(Address)
        7 - (displacement) -- Q byte is implicit in opcode
        8 - (address) -- Qbyte is implicit in opcode
        9 - (Address),(Address) -- Qbyte is implicit in opcode
   Fifth Field is the group number:
        0 - Command Group (left op nybble is F)
        1 - One Address Operations A (Left Nybble C, D, or E)
        2 - Two Address Operations (Left Nybble 0,1,2,4,5,6,8,9, or A)
        3 - One Address Operations B (left Nybble 3, 7, or B)

        There is duplication in this table -- IBM defines different opcodes
        that resolve to the same binary machine instruction -- e.g. JE and
        JZ.  On input this is no problem, on output, define the one you
        want to appear first, the second will never appear on output.
*/   

int32 nopcode = 75;
struct opdef opcode[75] = {
    {"HPL",  0x00,0,0,0},                                /** Halt Program Level */
    {"A",    0x06,0,1,3},                                /** Add to Register: A R,AADD */
    {"ST",   0x04,0,1,3},                                /** Store Register */
    {"L",    0x05,0,1,3},                                /** Load Register */
    {"LA",   0x02,0,1,1},                                /** Load Address */
    {"ZAZ",  0x04,0,2,2},                                /** Zero and Add Zoned */
    {"AZ",   0x06,0,2,2},                                /** Add Zoned Decimal */
    {"SZ",   0x07,0,2,2},                                /** Subtract Zoned Decimal */
    {"ALC",  0x0E,0,2,2},                                /** Add Logical:  ALC BADD,AADD,LEN */
    {"SLC",  0x0F,0,2,2},                                /** Sub Logical:  SLC BADD,AADD,LEN */
    {"MVC",  0x0C,0,2,2},                                /** Move Chars MVX BADD,AADD,LEN */
    {"ED",   0x0A,0,2,2},                                /** Edit: ED BADD,AADD,LEN */
    {"ITC",  0x0B,0,2,2},                                /** Insert Chars: ITC BADD,AADD,LEN */
    {"CLC",  0x0D,0,2,2},                                /** Compare Logical: CLC BADD,AADD,LEN */
    {"MVI",  0x0C,0,3,3},                                /** Move Immediate */
    {"SBN",  0x0A,0,3,3},                                /** Set Bits On */
    {"SBF",  0x0B,0,3,3},                                /** Set Bits Off */
    {"CLI",  0x0D,0,3,3},                                /** Compare Immediate */
    {"TBN",  0x08,0,3,3},                                /** Test Bits On */
    {"TBF",  0x09,0,3,3},                                /** Test Bits Off */
    {"APL",  0x01,0,4,0},                                /** Advance Program Level */
    {"SIO",  0x03,0,5,0},                                /** Start I/O */
    {"SNS",  0x00,0,6,3},                                /** Sense I/O */
    {"LIO",  0x01,0,6,3},                                /** Load I/O */
    {"TIO",  0x01,0,6,1},                                /** Test I/O */
    {"J",    0x02,0,7,0},                                /** Jump Unconditional */
    {"J",    0x02,0x87,7,0},                             /* Alternate J */
    {"JH",   0x02,132,7,0},                              /* Jump if High */
    {"JL",   0x02,130,7,0},                              /* Jump if Low */
    {"JE",   0x02,129,7,0},                              /* Jump if Equal */
    {"JNH",  0x02,4,7,0},                                /** Jump if Not High */
    {"JNL",  0x02,2,7,0},                                /** Jump if Not Low */
    {"JNE",  0x02,1,7,0},                                /** Jump if Not Equal */
    {"JOZ",  0x02,136,7,0},                              /* Jump if Overflow Zoned */
    {"JOL",  0x02,160,7,0},                              /* Jump if Overflow Logical */
    {"JNOZ", 0x02,8,7,0},                                /** Jump if No Overflow Zoned */
    {"JNOL", 0x02,32,7,0},                               /* Jump if No Overflow Logical */
    {"JT",   0x02,16,7,0},                               /* Jump if True */
    {"JF",   0x02,144,7,0},                              /* Jump if False */
    {"JP",   0x02,132,7,0},                              /* Jump if Plus */
    {"JM",   0x02,130,7,0},                              /* Jump if Minus */
    {"JZ",   0x02,129,7,0},                              /* Jump if Zero */
    {"JNP",  0x02,4,7,0},                                /** Jump if Not Plus */
    {"JNM",  0x02,2,7,0},                                /** Jump if Not Minus */
    {"JNZ",  0x02,1,7,0},                                /** Jump if Not Zero */
    {"NOPJ", 0x02,0x80,7,0},                              /* Never Jump - NOP */
    {"B",    0x00,0x00,8,1},                              /* Branch Unconditional */
    {"B",    0x00,0x87,8,1},                              /* Alternate B */
    {"BH",   0x00,0x84,8,1},                              /* Branch if High */
    {"BL",   0x00,0x82,8,1},                              /* Branch if Low */
    {"BE",   0x00,0x81,8,1},                              /* Branch if Equal */
    {"BNH",  0x00,0x04,8,1},                              /* Branch if Not High */
    {"BNL",  0x00,0x02,8,1},                              /* Branch if Not Low */
    {"BNE",  0x00,0x01,8,1},                              /* Branch if Not Equal */
    {"BOZ",  0x00,0x88,8,1},                              /* Branch if Overflow Zoned */
    {"BOL",  0x00,0xA0,8,1},                              /* Branch if Overflow Logical */
    {"BNOZ", 0x00,0x08,8,1},                              /* Branch if No Overflow Zoned */
    {"BNOL", 0x00,0x20,8,1},                              /* Branch if No Overflow Logical */
    {"BT",   0x00,0x10,8,1},                              /* Branch if True */
    {"BF",   0x00,0x90,8,1},                              /* Branch if False */
    {"BP",   0x00,0x84,8,1},                              /* Branch if Plus */
    {"BM",   0x00,0x82,8,1},                              /* Branch if Minus */
    {"BZ",   0x00,0x81,8,1},                              /* Branch if Zero */
    {"BNP",  0x00,0x04,8,1},                              /* Branch if Not Plus */
    {"BNM",  0x00,0x02,8,1},                              /* Branch if Not Minus */
    {"BNZ",  0x00,0x01,8,1},                              /* Branch if Not Zero */
    {"NOPB", 0x00,0x80,8,1},                              /* Never Branch - NOP */
    {"MZZ",  0x08,0,9,2},                                /** Move Zone to Zone */
    {"MNZ",  0x08,1,9,2},                                /** Move Numeric to Zone */
    {"MZN",  0x08,2,9,2},                                /** Move Zone to Numeric */
    {"MNN",  0x08,3,9,2},                                /** Move Numeric to Numeric */
    {"MVX",  0x08,0,2,2},                                /** Move Hex: MVX BADD,AADD,CODE */
    {"JC",   0x02,0,3,0},                                /** Jump on Specified Condition bits */
    {"BC",   0x00,0,3,1},                                /** Branch on Specified Condition */
    {"***",  0x00,0,0,0}
};

int32 regcode[15] = {   0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
            0x80, 0xC0, 0xA0, 0x90, 0x88, 0x84, 0x82, 0x81
};
            
char regname[15][8] =  {    "(P2IAR)",
                "(P1IAR)",
                "(IAR)",
                "(ARR)",
                "(PSR)",
                "(XR2)",
                "(XR1)",
                "(IAR0)",
                "(IAR1)",
                "(IAR2)",
                "(IAR3)",
                "(IAR4)",
                "(IAR5)",
                "(IAR6)",
                "(IAR7)"
};             

/* This is the binary loader.  The input file is considered to be
   a string of literal bytes with no special format. The
   load starts at the current value of the P1IAR.
*/

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
int32 i, addr = 0, cnt = 0;

if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
addr = IAR[8];
while ((i = getc (fileref)) != EOF) {
    M[addr] = i & 0xff;
    addr++;
    cnt++;
}   /* end while */
printf ("%d Bytes loaded.\n", cnt);
return (SCPE_OK);
}

/* Symbolic output

   Inputs:
        *of   = output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        status  =       error code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
    int32 r;
    char strg[256];
    
    strcpy(strg, "");
    r = printf_sym(of, strg, addr, val, uptr, sw);
    if (sw & SWMASK ('A'))
        strcpy(strg, "");
        else
        fprintf(of, "%s", strg);
    return (r);
}

int32 printf_sym (FILE *of, char *strg, t_addr addr, uint32 *val,
    UNIT *uptr, int32 sw)
{
int32 c1, c2, group, len1, len2, inst, aaddr, baddr;
int32 oplen, groupno, i, j, vpos, qbyte, da, m, n;
char bld[128], bldaddr[32], boperand[32], aoperand[32];
int32 blk[16], blt[16];
int32 blkadd;

c1 = val[0] & 0xff;
if (sw & SWMASK ('A')) {
    for (i = 0; i < 16; i++) {
        blkadd = addr + (i*16);
        for (j = 0; j < 16; j++) {
            blk[j] = M[blkadd+j] & 0xff;
            c2 = ebcdic_to_ascii[blk[j]];
            if (c2 < 040 || c2 > 0177 || blk[j] == 07) {
                blt[j] = '.';
            } else {    
                blt[j] = c2;
            }
        }
        if (i == 0) {
            fprintf(of, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X  [%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c]\n ",
                    blk[0], blk[1], blk[2], blk[3], blk[4], blk[5], blk[6], blk[7],
                    blk[8], blk[9], blk[10], blk[11], blk[12], blk[13], blk[14], blk[15],
                    blt[0], blt[1], blt[2], blt[3], blt[4], blt[5], blt[6], blt[7],
                    blt[8], blt[9], blt[10], blt[11], blt[12], blt[13], blt[14], blt[15]);
        } else {
            fprintf(of, "%X\t%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X  [%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c]\n ",
                    blkadd, blk[0], blk[1], blk[2], blk[3], blk[4], blk[5], blk[6], blk[7],
                    blk[8], blk[9], blk[10], blk[11], blk[12], blk[13], blk[14], blk[15],
                    blt[0], blt[1], blt[2], blt[3], blt[4], blt[5], blt[6], blt[7],
                    blt[8], blt[9], blt[10], blt[11], blt[12], blt[13], blt[14], blt[15]);
        }           
    }       
    return SCPE_OK;  }
if (sw & SWMASK ('C')) {
    c2 = ebcdic_to_ascii[c1];
    if (c2 < 040 || c2 > 0177) {
        sprintf(strg, "<%02X>", c1 & 0xff);
    } else {    
        sprintf (strg, "%c", c2 & 0xff);
    }
    return SCPE_OK;  }
if (!(sw & SWMASK ('M'))) return SCPE_ARG;

inst = val[0] & 0x0f;
len1 = (val[0] >> 6) & 3;
len2 = (val[0] >> 4) & 3;
group = (val[0] >> 4) & 0x0f;
qbyte = val[1];

/* Get total length of instruction */

if (group == 0x0f) {
    oplen = 3;
} else {
    oplen = 2;    
    if (len1 == 0) oplen += 2;
    if (len1 == 1 || len1 == 2) oplen++;
    if (len2 == 0) oplen += 2;
    if (len2 == 1 || len2 == 2) oplen++;
}

/* Find which group it belongs to */

switch (group) {
    case 0x0f:  
        groupno = 0;
        break;
    case 0x0c:
    case 0x0d:
    case 0x0e:
        groupno = 1;
        break;
    case 0x03:
    case 0x07:
    case 0x0b:
        groupno = 3;
        break;
    default:
        groupno = 2;
        break;
}                   

/* find the table entry */

for (i = 0; i < nopcode; i++) {
    if (opcode[i].form < 7) {                           /* Explicit Q */
        if (opcode[i].group == groupno &&
            opcode[i].opmask == inst) break;
        } else {                                        /* Implicit Q */
        if (opcode[i].group == groupno &&
            opcode[i].opmask == inst &&
            opcode[i].q == qbyte) break;
        }       
}

/* print the opcode */

if (i >= nopcode) {
    sprintf(strg, "%02X", val[0]);
    oplen = 1;
} else {
    sprintf(bld, "%s ", opcode[i].op);

    /* Extract the addresses into aaddr and baddr */

    strcpy(aoperand, "ERROR");
    strcpy(boperand, "ERROR");
    vpos = 2;
    aaddr = baddr = 0;
    switch (len1) {
        case 0:
            baddr = ((val[vpos] << 8) & 0xff00) | (val[vpos + 1] & 0x00ff);
            sprintf(boperand, "%04X", baddr);
            vpos = 4;
            break;
        case 1:
            baddr = val[vpos] & 255;
            sprintf(boperand, "(%02X,XR1)", baddr);
            vpos = 3;
            break;
        case 2:
            baddr = val[vpos] & 255;
            sprintf(boperand, "(%02X,XR2)", baddr);
            vpos = 3;
            break;
        default:
            baddr = 0;
            break;
    }           
    switch (len2) {
        case 0:
            aaddr = ((val[vpos] << 8) & 0xff00) | (val[vpos + 1] & 0x00ff);
            if (group == 0x0C || group == 0x0D || group == 0x0E)
                sprintf(boperand, "%04X", aaddr);
                else
                sprintf(aoperand, "%04X", aaddr);
            break;  
        case 1:
            aaddr = val[vpos] & 255;
            if (group == 0x0C || group == 0x0D || group == 0x0E)
                sprintf(boperand, "(%02X,XR1)", aaddr);
                else
                sprintf(aoperand, "(%02X,XR1)", aaddr);
            break;
        case 2:
            aaddr = val[vpos] & 255;
            if (group == 0x0C || group == 0x0D || group == 0x0E)
                sprintf(boperand, "(%02X,XR2)", aaddr);
                else
                sprintf(aoperand, "(%02X,XR2)", aaddr);
            break;
        default:
            aaddr = 0;
            break;
    }           

    /* Display the operands in the correct format */

    da = (qbyte >> 4) & 0x0f;   
    m = (qbyte >> 3) & 0x01;
    n = (qbyte) & 0x07;

    switch (opcode[i].form) {
        case 0:
            sprintf(bldaddr, "%02X,%02X", qbyte, val[2]);
            break;
        case 1:
            if (inst == 2 || inst == 4 || inst == 5 || inst == 6) {
                for (i = 0; i < 16; i++) {
                    if (regcode[i] == qbyte)
                        break;
                }
                if (i < 16) {
                    sprintf(bldaddr, "%s,%s", regname[i], boperand);
                } else {
                    sprintf(bldaddr, "%02X,%s", qbyte, boperand);
                }           
            } else {
                sprintf(bldaddr, "%02X,%s", qbyte, boperand);
            }
            break;
        case 2:
            if (inst > 9 || inst == 4 || inst == 6 || inst == 7)
                 qbyte++;                               /* special +1 for length display */
            sprintf(bldaddr, "%s,%s,%d", boperand, aoperand, qbyte);
            break;
        case 3:
            if (strcmp(opcode[i].op, "JC") == 0) {
                sprintf(bldaddr, "%04X,%02X", addr+oplen+val[2], qbyte);
            } else {    
                sprintf(bldaddr, "%s,%02X", boperand, qbyte);
            }   
            break;
        case 4: 
            sprintf(bldaddr, "%d,%d,%d", da, m, n);
            break;
        case 5:
            sprintf(bldaddr, "%d,%d,%d,%02X", da, m, n, val[2]);
            break;
        case 6:
            sprintf(bldaddr, "%d,%d,%d,%s", da, m, n, boperand);
            break;
        case 7:
            sprintf(bldaddr, "%04X", addr+oplen+val[2]);
            break;
        case 8:
            sprintf(bldaddr, "%s", boperand);   
            break;
        default:
            sprintf(bldaddr, "%s,%s", boperand, aoperand);
            break;
    }                                               
    sprintf(strg, "%s%s", bld, bldaddr);
} 

return -(oplen - 1);
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

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, i = 0, j, r, oplen, addtyp, saveaddr, vptr;
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;                         /* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (unsigned int) cptr[0];
    return SCPE_OK;
}
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = ((unsigned int) cptr[0] << 8) + (unsigned int) cptr[1];
    return SCPE_OK;
}

/* An instruction: get opcode (all characters until null, comma, left paren,
   or numeric (including spaces).
*/

while (1) {
    if (*cptr == ',' || *cptr == '\0' || *cptr == '(' ||
         isdigit(*cptr))
            break;
    gbuf[i] = toupper(*cptr);
    cptr++;
    i++;
}

/* kill trailing spaces if any */
gbuf[i] = '\0';
for (j = i - 1; gbuf[j] == ' '; j--) {
    gbuf[j] = '\0';
}

/* find opcode in table */
for (j = 0; j < nopcode; j++) {
    if (strcmp(gbuf, opcode[j].op) == 0)
        break;
}
if (j >= nopcode)                                       /* not found */
    return SCPE_ARG;

oplen = 2;                                              /* start with op & q */

val[0] = opcode[j].opmask;                              /* store opcode right nybble */

switch (opcode[j].form) {                               /* Get operands based on operand format */
    case 0:                                             /* Single Byte Operand */
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');              /* Get Q Byte */
        sscanf(gbuf, "%x", &r);
        val[1] = r;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, 0);                /* Get R Byte */
        sscanf(gbuf, "%x", &r);
        val[2] = r;
        oplen = 3;
        val[0] = 0xf0 | opcode[j].opmask;
        break;
    case 1:
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        if (opcode[j].opmask == 2 ||
            opcode[j].opmask == 4 ||
            opcode[j].opmask == 5 ||
            opcode[j].opmask == 6) {
            if (isdigit(gbuf[0])) {
                sscanf(gbuf, "%x", &r);
            } else {
                for (i = 0; i < 16; i++) {
                    if (strcmp(gbuf, regname[i]) == 0)
                        break;
                }
                if (i < 16) {
                    r = regcode[i];
                } else {
                    return SCPE_ARG;
                }       
            }   
        } else {
            sscanf(gbuf, "%x", &r);
        }   
        if (r > 255) return SCPE_ARG;
        val[1] = r;
        if (*cptr == ',') cptr++;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0: 
                val[2] = (addr >> 8) & 0x00ff;
                val[3] = addr & 0xff;
                oplen = 4;
                if (opcode[j].group == 1)
                    val[0] = 0xC0 | opcode[j].opmask;
                    else
                    val[0] = 0x30 | opcode[j].opmask;
                break;
            case 1: 
                val[2] = addr & 0xff;
                oplen = 3;
                if (opcode[j].group == 1)
                    val[0] = 0xD0 | opcode[j].opmask;
                    else 
                    val[0] = 0x70 | opcode[j].opmask;
                break;
            case 2:
                val[2] = addr & 0xff;
                oplen = 3;
                if (opcode[j].group == 1)
                    val[0] = 0xE0 | opcode[j].opmask;
                    else
                    val[0] = 0xB0 | opcode[j].opmask;
                break;
            default:
                return SCPE_ARG;
                break;
        }               
        break;
    case 2:
        oplen = 2;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0: 
                val[2] = (addr >> 8) & 0xff;
                val[3] = addr & 0xff;
                oplen += 2;
                vptr = 4;
                val[0] = 0x00 | opcode[j].opmask;
                break;
            case 1: 
                val[2] = addr & 0xff;
                oplen += 1;
                vptr = 3;
                val[0] = 0x40 | opcode[j].opmask;
                break;
            case 2:
                val[2] = addr & 0xff;
                oplen += 1;
                vptr = 3;
                val[0] = 0x80 | opcode[j].opmask;
                break;
            default:
                return SCPE_ARG;
                break;
        }
        if (*cptr == ',') cptr++;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0: 
                val[vptr] = (addr >> 8) & 0xff;
                val[vptr+1] = addr & 0xff;
                oplen += 2;
                break;
            case 1: 
                val[vptr] = addr & 0xff;
                oplen += 1;
                val[0] = 0x10 | val[0];
                break;
            case 2:
                val[vptr] = addr & 0xff;
                oplen += 1;
                val[0] = 0x20 | val[0];
                break;
            default:
                return SCPE_ARG;
                break;
        }                   
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, 0);
        sscanf(gbuf, "%d", &r);
        if (opcode[j].opmask > 9 ||
            opcode[j].opmask == 4 ||
            opcode[j].opmask == 6 ||
            opcode[j].opmask == 7) r--;                 /* special: length -1 */
        val[1] = r;
        if (*cptr == ',') cptr++;
        break;
    case 3:
        saveaddr = addr;
        if (*cptr == ',') cptr++;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0:
                if (opcode[j].group == 0) {             /* Group 0 form 3 is JC with explicit Q */
                    if (*cptr == ',') cptr++;
                    cptr = get_glyph(cptr, gbuf, 0);
                    sscanf(gbuf, "%x", &r);
                    if ((addr - (saveaddr+3)) > 255 || (addr - (saveaddr+3)) < 1)
                         return SCPE_ARG;
                    val[2] = addr - (saveaddr+3);
                    val[1] = r;
                    val[0] = 0xf0 | opcode[j].opmask;
                    oplen = 3;
                    
                } else {
                    val[2] = (addr >> 8) & 0x00ff;
                    val[3] = addr & 0xff;
                    oplen = 4;
                    if (opcode[j].group == 1)
                        val[0] = 0xC0 | opcode[j].opmask;
                        else
                        val[0] = 0x30 | opcode[j].opmask;
                }       
                break;
            case 1: 
                val[2] = addr & 0xff;
                oplen = 3;
                if (opcode[j].group == 1)
                    val[0] = 0xD0 | opcode[j].opmask;
                    else
                    val[0] = 0x70 | opcode[j].opmask;
                break;
            case 2:
                val[2] = addr & 0xff;
                oplen = 3;
                if (opcode[j].group == 1)
                    val[0] = 0xE0 | opcode[j].opmask;
                    else
                    val[0] = 0xB0 | opcode[j].opmask;
                break;
            default:
                return SCPE_ARG;
                break;
        }                   
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, 0);
        sscanf(gbuf, "%x", &r);
        if (r > 255) return SCPE_ARG;
        val[1] = r;
        break;
    case 4:
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 15) return SCPE_ARG;
        val[1] = (r << 4) & 0xf0;
        val[0] = 0xf0 | opcode[j].opmask;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 1) return SCPE_ARG;
        val[1] |= (r << 3) & 0x08;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, 0);
        sscanf(gbuf, "%d", &r);
        if (r > 7) return SCPE_ARG;
        val[1] |= r & 0x07;
        val[2] = 0;
        oplen = 3;
        break;
    case 5:
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 15) return SCPE_ARG;
        val[1] = (r << 4) & 0xf0;
        val[0] = 0xf0 | opcode[j].opmask;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 1) return SCPE_ARG;
        val[1] |= (r << 3) & 0x08;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 7) return SCPE_ARG;
        val[1] |= r & 0x07;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, 0);
        sscanf(gbuf, "%x", &r);
        if (r > 255) return SCPE_ARG;
        val[2] = r;
        oplen = 3;
        break;
    case 6:
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 15) return SCPE_ARG;
        val[1] = (r << 4) & 0xf0;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 1) return SCPE_ARG;
        val[1] |= (r << 3) & 0x08;
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, ',');
        sscanf(gbuf, "%d", &r);
        if (r > 7) return SCPE_ARG;
        val[1] |= r & 0x07;
        if (*cptr == ',') cptr++;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0: 
                val[2] = (addr >> 8) & 0x00ff;
                val[3] = addr & 0xff;
                oplen = 4;
                if (opcode[j].group == 1)
                    val[0] = 0xC0 | opcode[j].opmask;
                    else
                    val[0] = 0x30 | opcode[j].opmask;
                break;
            case 1: 
                val[2] = addr & 0xff;
                oplen = 3;
                if (opcode[j].group == 1)
                    val[0] = 0xD0 | opcode[j].opmask;
                    else
                    val[0] = 0x70 | opcode[j].opmask;
                break;
            case 2:
                val[2] = addr & 0xff;
                oplen = 3;
                if (opcode[j].group == 1)
                    val[0] = 0xE0 | opcode[j].opmask;
                    else
                    val[0] = 0xB0 | opcode[j].opmask;
                break;
            default:
                return SCPE_ARG;
                break;
        }                   
        break;
    case 7:
        if (*cptr == ',') cptr++;
        cptr = get_glyph(cptr, gbuf, 0);
        sscanf(gbuf, "%x", &r);
        if ((r - (addr+3)) > 255 || (r - (addr+3)) < 1) return SCPE_ARG;
        val[2] = r - (addr+3);
        val[1] = opcode[j].q;
        val[0] = 0xf0 | opcode[j].opmask;
        oplen = 3;
        break;
        
    case 8:
        if (*cptr == ',') cptr++;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0: 
                val[2] = (addr >> 8) & 0x00ff;
                val[3] = addr & 0xff;
                oplen = 4;
                val[0] = 0xC0 | opcode[j].opmask;
                break;
            case 1: 
                val[2] = addr & 0xff;
                oplen = 3;
                val[0] = 0xD0 | opcode[j].opmask;
                break;
            case 2:
                val[2] = addr & 0xff;
                oplen = 3;
                val[0] = 0xE0 | opcode[j].opmask;
                break;
            default:
                return SCPE_ARG;
                break;
        }                   
        val[1] = opcode[j].q;
        break;
    case 9:
        oplen = 2;
        val[0] = 0;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0: 
                val[2] = (addr >> 8) & 0xff;
                val[3] = addr & 0xff;
                oplen += 2;
                vptr = 4;
                val[0] = 0x00 | opcode[j].opmask;
                break;
            case 1: 
                val[2] = addr & 0xff;
                oplen += 1;
                vptr = 3;
                val[0] = 0x40 | opcode[j].opmask;
                break;
            case 2:
                val[2] = addr & 0xff;
                oplen += 1;
                vptr = 3;
                val[0] = 0x80 | opcode[j].opmask;
                break;
            default:
                return SCPE_ARG;
                break;
        }
        if (*cptr == ',') cptr++;
        cptr = parse_addr(cptr, gbuf, &addr, &addtyp);
        switch(addtyp) {
            case 0: 
                val[vptr] = (addr >> 8) & 0xff;
                val[vptr+1] = addr & 0xff;
                oplen += 2;
                break;
            case 1: 
                val[vptr] = addr & 0xff;
                oplen += 1;
                val[0] = 0x10 | val[0];
                break;
            case 2:
                val[vptr] = addr & 0xff;
                oplen += 1;
                val[0] = 0x20 | val[0];
                break;
            default:
                return SCPE_ARG;
                break;
        }                   
        val[1] = opcode[j].q;
        break;
    default:
        break;
}


return (-(oplen-1));
}

char *parse_addr(char *cptr,  char *gbuf, t_addr *addr, int32 *addrtype)
{
int32 nybble = 0;
char temp[32];

cptr = get_glyph(cptr, gbuf, ',');
if (gbuf[0] == '(') {                                   /* XR relative */
    strcpy(temp, gbuf+1);
    sscanf(temp, "%x", addr);
    if (*cptr == ',') cptr++;
    cptr = get_glyph(cptr, gbuf, ',');
    nybble = -1;
    if (strcmp(gbuf, "XR1)") == 0)
        nybble = 1;
    if (strcmp(gbuf, "XR2)") == 0)
        nybble = 2;
} else {                                                /* Direct */
    sscanf(gbuf, "%x", addr);
    nybble = 0; 
}
*addrtype = nybble;
return cptr;
}

