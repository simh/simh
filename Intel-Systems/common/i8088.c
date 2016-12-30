/* i8088.c: Intel 8086/8088 CPU simulator

    Copyright (C) 1991 Jim Hudgens

    The file is part of GDE.

    GDE is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 1, or (at your option)
    any later version.

    GDE is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with GDE; see the file COPYING.  If not, write to
    the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
    
    This software was modified by Bill Beech, Mar 2011, from the software GDE
    of Jim Hudgens as provided with the SIMH AltairZ80 emulation package. 
    I modified it to allow emulation of Intel iSBC Single Board Computers.

    Copyright (c) 2011, William A. Beech

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
    WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of William A. Beech shall not be
    used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from William A. Beech.

    cpu          8088 CPU

    17 Mar 11    WAB     Original code

    The register state for the 8088 CPU is:

    AX<0:15>             AH/AL Register Pair
    BX<0:15>             BH/BL Register Pair
    CX<0:15>             CH/CL Register Pair
    DX<0:15>             DH/DL Register Pair
    SI<0:15>             Source Index Register
    DI<0:15>             Destination Index Register
    BP<0:15>             Base Pointer
    SP<0:15>             Stack Pointer
    CS<0:15>             Code Segment Register
    DS<0:15>             Date Segment Register
    SS<0:15>             Stack Segment Register
    ES<0:15>             Extra Segment Register
    IP<0:15>             Program Counter

    PSW<0:15>            Program Status Word - Contains the following flags:

    AF                   Auxillary Flag
    CF                   Carry Flag
    OF                   Overflow Flag
    SF                   Sign Flag
    PF                   Parity Flag
    ZF                   Zero Flag
    DF                   Direction Flag
    IF                   Interrupt Enable Flag
    TF                   Trap Flag

                        in bit positions:
    15                    8  7                    0
    |--|--|--|--|OF|DF|IF|TF|SF|ZF|--|AF|--|PF|--|CF|

    The 8088 is an 8-bit CPU, which uses 16-bit offset and segment registers 
    in combination with a dedicated adder to address up to 1MB of memory directly.
    The offset register is added to the segment register shifted left 4 places
    to obtain the 20-bit address.

    The CPU utilizes two separate processing units - the Execution Unit (EU) and 
    the Bus Interface Unit (BIU).  The EU executes instructions.  The BIU fetches 
    instructions, reads operands and writes results.  The two units can operate 
    independently of one another and are able, under most circumstances, to 
    extensively overlap instruction fetch with execution.

    The BIUs of the 8086 and 8088 are functionally identical, but are implemented 
    differently to match the structure and performance characteristics of their 
    respective buses.

    The almost 300 instructions come in 1, 2, 3, 4, 5, 6 and 7-byte flavors.

    This routine is the simulator for the 8088.  It is called from the 
    simulator control program to execute instructions in simulated memory, 
    starting at the simulated IP. It runs until 'reason' is set non-zero.

    General notes:

    1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        I/O error in I/O simulator
        Invalid OP code (if ITRAP is set on CPU)

    2. Interrupts.
        There are 256 possible levels of interrupt, and in effect they
        do a hardware CALL instruction to one of 256 possible low
        memory addresses.

    3. Non-existent memory.  On the 8088, reads to non-existent memory
        return 0FFh, and writes are ignored. 

    Some algorithms were pulled from the GDE Dos/IP Emulator by Jim Hudgens

*/
/* 
  This algorithm was pulled from the GDE Dos/IP Emulator by Jim Hudgens

CARRY CHAIN CALCULATION.
   This represents a somewhat expensive calculation which is
   apparently required to emulate the setting of the OF and
   AF flag.  The latter is not so important, but the former is.
   The overflow flag is the XOR of the top two bits of the
   carry chain for an addition (similar for subtraction).
   Since we do not want to simulate the addition in a bitwise
   manner, we try to calculate the carry chain given the
   two operands and the result.

   So, given the following table, which represents the
   addition of two bits, we can derive a formula for
   the carry chain.

       a   b   cin   r     cout
       0   0   0     0     0
       0   0   1     1     0
       0   1   0     1     0
       0   1   1     0     1
       1   0   0     1     0
       1   0   1     0     1
       1   1   0     0     1
       1   1   1     1     1

    Construction of table for cout:

               ab
         r  \  00   01   11  10
            |------------------
         0  |   0    1    1   1
         1  |   0    0    1   0

    By inspection, one gets:  cc = ab +  r'(a + b)

    That represents alot of operations, but NO CHOICE....

BORROW CHAIN CALCULATION.
   The following table represents the
   subtraction of two bits, from which we can derive a formula for
   the borrow chain.

       a   b   bin   r     bout
       0   0   0     0     0
       0   0   1     1     1
       0   1   0     1     1
       0   1   1     0     1
       1   0   0     1     0
       1   0   1     0     0
       1   1   0     0     0
       1   1   1     1     1

    Construction of table for cout:

               ab
         r  \  00   01   11  10
            |------------------
         0  |   0    1    0   0
         1  |   1    1    1   0

    By inspection, one gets:  bc = a'b +  r(a' + b)

    Segment register selection and overrides are handled as follows:
        If there is a segment override, the register number is stored
        in seg_ovr. If there is no override, seg_ovr is zero.  Seg_ovr
        is set to zero after each instruction except segment override 
        instructions.

        Get_ea sets the value of seg_reg to the override if present 
        otherwise to the default value for the registers used in the
        effective address calculation.

        The get/put_smword/byte routines use the register number in 
        seg_reg to obtain the segment value to calculate the absolute
        memory address for the operation.
 */

#include <stdio.h>
#include "multibus_defs.h"

#define UNIT_V_OPSTOP   (UNIT_V_UF)             /* Stop on Invalid OP? */
#define UNIT_OPSTOP     (1 << UNIT_V_OPSTOP)
#define UNIT_V_CHIP     (UNIT_V_UF+1)           /* 8088 or 8086 */
#define UNIT_CHIP       (1 << UNIT_V_CHIP)

/* Flag values to set proper positions in PSW */
#define CF             0x0001
#define PF             0x0004
#define AF             0x0010
#define ZF             0x0040
#define SF             0x0080
#define TF             0x0100
#define IF             0x0200
#define DF             0x0400
#define OF             0x0800

/* Macros to handle the flags in the PSW 
    8088 has top 4 bits of the flags set to 1
    also, bit#1 is set.  This is (not well) documented  behavior. */
#define PSW_ALWAYS_ON       (0xF002)        /* for 8086 */
#define PSW_MSK (CF|PF|AF|ZF|SF|TF|IF|DF|OF)
#define TOGGLE_FLAG(FLAG)   (PSW ^= FLAG)
#define SET_FLAG(FLAG)      (PSW |= FLAG)
#define CLR_FLAG(FLAG)      (PSW &= ~FLAG)
#define GET_FLAG(FLAG)      (PSW & FLAG)
#define CONDITIONAL_SET_FLAG(COND,FLAG) \
    if (COND) SET_FLAG(FLAG); else CLR_FLAG(FLAG)

/* union of byte and word registers */
union   {                       
    uint8   b[2];                           /* two bytes */
    uint16  w;                              /* one word */
}   A, B, C, D;                             /* storage for AX, BX, CX and DX */

/* macros for byte registers */
#define AH              (A.b[1])
#define AL              (A.b[0])
#define BH              (B.b[1])
#define BL              (B.b[0])
#define CH              (C.b[1])
#define CL              (C.b[0])
#define DH              (D.b[1])
#define DL              (D.b[0])

/* macros for word registers */
#define AX              (A.w)
#define BX              (B.w)
#define CX              (C.w)
#define DX              (D.w)

/* macros for handling IP and SP */
#define INC_IP1         (++IP & ADDRMASK20) /* increment IP one byte */
#define INC_IP2         ((IP += 2) & ADDRMASK20) /* increment IP two bytes */

/* storage for the rest of the registers */
int32 DI;                               /* Source Index Register */
int32 SI;                               /* Destination Index Register */
int32 BP;                               /* Base Pointer Register */
int32 SP;                               /* Stack Pointer Register */
int32 CS;                               /* Code Segment Register */
int32 DS;                               /* Data Segment Register */
int32 SS;                               /* Stack Segment Register */
int32 ES;                               /* Extra Segment Register */
int32 IP;                               /* Program Counter */
int32 PSW;                              /* Program Status Word (Flags) */
int32 saved_PC = 0;                     /* saved program counter */
int32 int_req = 0;                      /* Interrupt request */
int32 chip = 0;                         /* 0 = 8088 chip, 1 = 8086 chip */
#define CHIP_8088   0                   /* processor types */
#define CHIP_8086   1
#define CHIP_80188  2
#define CHIP_80186  3
#define CHIP_80286  4

int32 seg_ovr = 0;                      /* segment override register */
int32 seg_reg = 0;                      /* segment register to use for EA */
#define SEG_NONE    0                   /* segmenr override register values */
#define SEG_CS      8
#define SEG_DS      9
#define SEG_ES      10
#define SEG_SS      11

int32 PCX;                              /* External view of IP */

/* handle prefix instructions */
uint32 sysmode = 0;                     /* prefix flags */
#define SYSMODE_SEG_DS_SS   0x01
#define SYSMODE_SEGOVR_CS   0x02
#define SYSMODE_SEGOVR_DS   0x04
#define SYSMODE_SEGOVR_ES   0x08
#define SYSMODE_SEGOVR_SS   0x10
#define SYSMODE_SEGMASK  (SYSMODE_SEG_DS_SS | SYSMODE_SEGOVR_CS |   \
    SYSMODE_SEGOVR_DS | SYSMODE_SEGOVR_ES | SYSMODE_SEGOVR_SS)
#define SYSMODE_PREFIX_REPE     0x20
#define SYSMODE_PREFIX_REPNE    0x40

/* function prototypes */
int32 sign_ext(int32 val);
int32 fetch_byte(int32 flag);
int32 fetch_word(void);
int32 parity(int32 val);
void i86_intr_raise(uint8 num);
uint32 get_rbyte(uint32 reg);
uint32 get_rword(uint32 reg);
void put_rbyte(uint32 reg, uint32 val);
void put_rword(uint32 reg, uint32 val);
uint32 get_ea(uint32 mrr);
void set_segreg(uint32 reg);
void get_mrr_dec(uint32 mrr, uint32 *mod, uint32 *reg, uint32 *rm);

/* emulator primitives */
uint8 aad_word(uint16 d);
uint16 aam_word(uint8 d);
uint8 adc_byte(uint8 d, uint8 s);
uint16 adc_word(uint16 d, uint16 s);
uint8 add_byte(uint8 d, uint8 s);
uint16 add_word(uint16 d, uint16 s);
uint8 and_byte(uint8 d, uint8 s);
uint16 cmp_word(uint16 d, uint16 s);
uint8 cmp_byte(uint8 d, uint8 s);
uint16 and_word(uint16 d, uint16 s);
uint8 dec_byte(uint8 d);
uint16 dec_word(uint16 d);
void div_byte(uint8 s);
void div_word(uint16 s);
void idiv_byte(uint8 s);
void idiv_word(uint16 s);
void imul_byte(uint8 s);
void imul_word(uint16 s);
uint8 inc_byte(uint8 d);
uint16 inc_word(uint16 d);
void mul_byte(uint8 s);
void mul_word(uint16 s);
uint8 neg_byte(uint8 s);
uint16 neg_word(uint16 s);
uint8 not_byte(uint8 s);
uint16 not_word(uint16 s);
uint8 or_byte(uint8 d, uint8 s);
uint16 or_word(uint16 d, uint16 s);
void push_word(uint16 val);
uint16 pop_word(void);
uint8 rcl_byte(uint8 d, uint8 s);
uint16 rcl_word(uint16 d, uint16 s);
uint8 rcr_byte(uint8 d, uint8 s);
uint16 rcr_word(uint16 d, uint16 s);
uint8 rol_byte(uint8 d, uint8 s);
uint16 rol_word(uint16 d, uint16 s);
uint8 ror_byte(uint8 d, uint8 s);
uint16 ror_word(uint16 d, uint16 s);
uint8 shl_byte(uint8 d, uint8 s);
uint16 shl_word(uint16 d, uint16 s);
uint8 shr_byte(uint8 d, uint8 s);
uint16 shr_word(uint16 d, uint16 s);
uint8 sar_byte(uint8 d, uint8 s);
uint16 sar_word(uint16 d, uint16 s);
uint8 sbb_byte(uint8 d, uint8 s);
uint16 sbb_word(uint16 d, uint16 s);
uint8 sub_byte(uint8 d, uint8 s);
uint16 sub_word(uint16 d, uint16 s);
void test_byte(uint8 d, uint8 s);
void test_word(uint16 d, uint16 s);
uint8 xor_byte(uint8 d, uint8 s);
uint16 xor_word(uint16 d, uint16 s);
int32 get_smbyte(int32 segreg, int32 addr);
int32 get_smword(int32 segreg, int32 addr);
void put_smbyte(int32 segreg, int32 addr, int32 val);
void put_smword(int32 segreg, int32 addr, int32 val);

/* simulator routines */
int32 sim_instr(void);
t_stat i8088_reset (DEVICE *dptr);
t_stat i8088_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat i8088_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);

/* external references */
//extern t_stat i8088_reset (DEVICE *dptr);

/* Multibus memory read and write absolute address routines */
extern int32 get_mbyte(int32 addr);
extern int32 get_mword(int32 addr);
extern void put_mbyte(int32 addr, int32 val);
extern void put_mword(int32 addr, int32 val);

extern int32 sim_int_char;
extern int32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */

/* This is the I/O configuration table.  There are 65536 possible
device addresses, if a device is plugged to a port it's routine
address is here, 'nulldev' means no device is available
*/

struct idev {
    int32 (*routine)();
};
extern struct idev dev_table[];

/* 8088 CPU data structures

   i8088_dev      CPU device descriptor
   i8088_unit     CPU unit descriptor
   i8088_reg      CPU register list
   i8088_mod      CPU modifiers list
*/

UNIT i8088_unit = { UDATA (NULL, 0, 0) };

REG i8088_reg[] = {
    { HRDATA (IP, saved_PC, 16) },                  /* must be first for sim_PC */
    { HRDATA (AX, AX, 16) },
    { HRDATA (BX, BX, 16) },
    { HRDATA (CX, CX, 16) },
    { HRDATA (DX, DX, 16) },
    { HRDATA (DI, DI, 16) },
    { HRDATA (SI, SI, 16) },
    { HRDATA (BP, BP, 16) },
    { HRDATA (SP, SP, 16) },
    { HRDATA (CS, CS, 16) },
    { HRDATA (DS, DS, 16) },
    { HRDATA (SS, SS, 16) },
    { HRDATA (ES, ES, 16) },
    { HRDATA (PSW, PSW, 16) },
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
};

MTAB i8088_mod[] = {
    { UNIT_CHIP, UNIT_CHIP, "8086", "8086", NULL },
    { UNIT_CHIP, 0, "8088", "8088", NULL },
    { UNIT_OPSTOP, UNIT_OPSTOP, "ITRAP", "ITRAP", NULL },
    { UNIT_OPSTOP, 0, "NOITRAP", "NOITRAP", NULL },
    { 0 }
};

DEBTAB i8088_debug[] = {
    { "ALL", DEBUG_all },
    { "FLOW", DEBUG_flow },
    { "READ", DEBUG_read },
    { "WRITE", DEBUG_write },
    { "LEV1", DEBUG_level1 },
    { "LEV2", DEBUG_level2 },
    { "REG", DEBUG_reg },
    { "ASM", DEBUG_asm },
    { NULL }
};

DEVICE i8088_dev = {
    "I8088",             //name
    &i8088_unit,        //units
    i8088_reg,          //registers
    i8088_mod,          //modifiers
    1,                  //numunits
    16,                 //aradix 
    20,                 //awidth 
    1,                  //aincr 
    16,                 //dradix 
    8,                  //dwidth
    &i8088_ex,          //examine 
    &i8088_dep,         //deposit 
    &i8088_reset,       //reset
    NULL,               //boot
    NULL,               //attach 
    NULL,               //detach
    NULL,               //ctxt
    DEV_DEBUG,          //flags 
    0,                  //dctrl 
    i8088_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

uint8 xor_3_tab[] = { 0, 1, 1, 0 };

int32 IP;

static const char *opcode[] = {
"ADD ", "ADD ", "ADD ", "ADD ",                 /* 0x00 */
"ADD AL,", "ADD AX,", "PUSH ES", "POP ES",
"OR ", "OR ", "OR ", "OR ", 
"OR AL,", "OR AX,", "PUSH CS", "???", 
"ADC ", "ADC ", "ADC ", "ADC ",                 /* 0x10 */
"ADC AL,", "ADC AX,", "PUSH SS", "RPOP SS",
"SBB ", "SBB ", "SBB ", "SBB ", 
"SBB AL,", "SBB AX,", "PUSH DS", "POP DS",
"AND ", "AND ", "AND ", "AND ",                 /* 0x20 */
"AND AL,", "AND AX,", "ES:", "DAA",
"SUB ", "SUB ", "SUB ", "SUB ", 
"SUB AL,", "SUB AX,", "CS:", "DAS",
"XOR ", "XOR ", "XOR ", "XOR ",                 /* 0x30 */
"XOR AL,", "XOR AX,", "SS:", "AAA",
"CMP ", "CMP ", "CMP ", "CMP ", 
"CMP AL,", "CMP AX,", "DS:", "AAS",
"INC AX", "INC CX", "INC DX", "INC BX",         /* 0x40 */
"INC SP", "INC BP", "INC SI", "INC DI",
"DEC AX", "DEC CX", "DEC DX", "DEC BX",
"DEC SP", "DEC BP", "DEC SI", "DEC DI",
"PUSH AX", "PUSH CX", "PUSH DX", "PUSH BX",     /* 0x50 */
"PUSH SP", "PUSH BP", "PUSH SI", "PUSH DI",
"POP AX", "POP CX", "POP DX", "POP BX",
"POP SP", "POP BP", "POP SI", "POP DI",
"???", "???", "???", "???",                     /* 0x60 */
"???", "???", "???", "???",
"PUSH ", "IMUL ", "PUSH ", "IMUL ",
"INSB", "INSW", "OUTSB", "OUTSW",
"JO ", "JNO ", "JC ", "JNC",                    /* 0x70 */
"JZ ", "JNZ ", "JNA ", "JA",
"JS ", "JNS ", "JP ", "JNP ",
"JL ", "JNL ", "JLE ", "JNLE",
"???", "???", "???", "???",                     /* 0x80 */
"TEST ", "TEST ", "XCHG ", "XCHG ",
"MOV ", "MOV ", "MOV ", "MOV ",
"MOV ", "LEA ", "MOV ", "POP ",
"NOP", "XCHG AX,CX", "XCHG AX,DX", "XCHG AX,BX",/* 0x90 */
"XCHG AX,SP", "XCHG AX,BP", "XCHG AX,SI", "XCHG AX,DI",
"CBW", "CWD", "CALL ", "WAIT",
"PUSHF", "POPF", "SAHF", "LAHF",
"MOV AL,", "MOV AX,", "MOV ", "MOV ",           /* 0xA0 */
"MOVSB", "MOVSW", "CMPSB", "CMPSW",
"TEST AL,", "TEST AX,", "STOSB", "STOSW",
"LODSB", "LODSW", "SCASB", "SCASW",
"MOV AL,", "MOV CL,", "MOV DL,", "MOV BL,",     /* 0xB0 */
"MOV AH,", "MOV CH,", "MOV DH,", "MOV BH,",
"MOV AX,", "MOV CX,", "MOV DX,", "MOV BX,",
"MOV SP,", "MOV BP,", "MOV SI,", "MOV DI,"
" ", " ", "RET ", "RET ",                       /* 0xC0 */
"LES ", "LDS ", "MOV ", "MOV ",
"???", "???", "RET ", "RET",
"INT 3", "INT ", "INTO", "IRET",
" ", " ", " ", " ",                             /* 0xD0 */
"AAM", "AAD", "???", "XLATB",
"ESC ", "ESC ", "ESC ", "ESC ", 
"ESC ", "ESC ", "ESC ", "ESC ", 
"LOOPNZ ", "LOOPZ ", "LOOP", "JCXZ",            /* 0xE0 */
"IN AL,", "IN AX,", "OUT ", "OUT ",
"CALL ", "JMP ", "JMP ", "JMP ",
"IN AL,DX", "IN AX,DX", "OUT DX,AL", "OUT DX,AX",
"LOCK", "???", "REPNZ", "REPZ",                 /* 0xF0 */
"HLT", "CMC", " ", " ",
"CLC", "STC", "CLI", "STI",
"CLD", "STD", "???", "???"
 };

int32 oplen[256] = {
1,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,
0,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,
0,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,
0,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,3,3,3,1,2,1,1,1,3,0,3,3,2,1,
1,1,3,2,3,1,2,1,1,0,3,2,3,0,2,1,
1,1,3,1,3,1,2,1,1,1,3,1,3,0,2,1,
1,1,3,1,3,1,2,1,1,1,3,1,3,0,2,1
};

int32 sim_instr (void)
{
    extern int32 sim_interval;
    int32 IR, OP, DAR, reason, hi, lo, carry, i, adr;
    int32 MRR, REG, EA, MOD, RM, DISP, VAL, DATA, OFF, SEG, INC, VAL1;

    IP = saved_PC & ADDRMASK16;         /* load local IP */
    reason = 0;                         /* clear stop reason */

    /* Main instruction fetch/decode loop */

    while (reason == 0) {               /* loop until halted */
        if (i8088_dev.dctrl & DEBUG_asm) 
            sim_printf("\n");
        if (i8088_dev.dctrl & DEBUG_reg) {
            sim_printf("Regs: AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X IP=%04X\n",
                AX, BX, CX, DX, SP, BP, SI, DI, IP);
            sim_printf("Segs: CS=%04X DS=%04X ES=%04X SS=%04X ", CS, DS, ES, SS);
            sim_printf("Flags: %04X\n", PSW);
        }

        if (sim_interval <= 0) {        /* check clock queue */
            if (reason = sim_process_event ()) break;
        }

        if (int_req > 0) {              /* interrupt? */

        /* 8088 interrupts not implemented yet. */

        }                               /* end interrupt */

        if (sim_brk_summ &&
            sim_brk_test (IP, SWMASK ('E'))) {  /* breakpoint? */
            reason = STOP_IBKPT;        /* stop simulation */
            break;
        }

        sim_interval--;                 /* countdown clock */
        PCX = IP;
        IR = OP = fetch_byte(0);           /* fetch instruction */

        /* Handle below all operations which refer to registers or
          register pairs.  After that, a large switch statement
          takes care of all other opcodes */

        /* data transfer instructions */
        
        /* arithmetic instructions */

        /* bit manipulation instructions */
        /* string manipulation instructions */
        /* control transfer instructions */
        /* processor control instructions */
        /* The Big Instruction Decode Switch */

        switch (IR) {

        /* data transfer instructions */
        /* arithmetic instructions */

            case 0x00:                  /* ADD byte - REG = REG + (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = add_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = add_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x01:                  /* ADD word - (EA) = (EA) + REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = add_word(get_rword(REG), get_smword(seg_reg, EA)); /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = add_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rword(REG, VAL); /* store result */
                }
                break;

            case 0x02:                  /* ADD byte - REG = REG + (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = add_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = add_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x03:                  /* ADD word - (EA) = (EA) + REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = adc_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = adc_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x04:                  /* ADD byte - AL = AL + DATA */
                DATA = fetch_byte(1);
                VAL = add_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x05:                  /* ADD word - (EA) = (EA) + REG */
                DATA = fetch_word();
                VAL = add_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x06:                  /* PUSH ES */
                push_word(ES);
                break;

            case 0x07:                  /* POP ES */
                ES = pop_word();
                break;

            case 0x08:                  /* OR byte - REG = REG OR (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = or_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = or_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x09:                  /* OR word - (EA) = (EA) OR REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = or_word(get_rword(REG), get_smword(seg_reg, EA)); /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = or_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rword(REG, VAL); /* store result */
                }
                break;

            case 0x0A:                  /* OR byte - REG = REG OR (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = or_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = or_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x0B:                  /* OR word - (EA) = (EA) OR REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = or_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = or_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x0C:                  /* OR byte - AL = AL OR DATA */
                DATA = fetch_byte(1);
                VAL = or_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x0D:                  /* OR word - (EA) = (EA) OR REG */
                DATA = fetch_word();
                VAL = or_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x0E:                  /* PUSH CS */
                push_word(CS);
                break;

            /* 0x0F - Not implemented on 8086/8088 */

            case 0x10:                  /* ADC byte - REG = REG + (EA) + CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = adc_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = adc_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x11:                  /* ADC word - (EA) = (EA) + REG + CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = adc_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = adc_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x12:                  /* ADC byte - REG = REG + (EA) + CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = adc_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = adc_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x13:                  /* ADC word - (EA) = (EA) + REG + CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = adc_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = adc_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x14:                  /* ADC byte - AL = AL + DATA + CF */
                DATA = fetch_byte(1);
                VAL = adc_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x15:                  /* ADC word - (EA) = (EA) + REG + CF */
                DATA = fetch_word();
                VAL = adc_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x16:                  /* PUSH SS */
                push_word(SS);
                break;

            case 0x17:                  /* POP SS */
                SS = pop_word();
                break;

            case 0x18:                  /* SBB byte - REG = REG - (EA) - CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sbb_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sbb_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x19:                  /* SBB word - (EA) = (EA) - REG - CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sbb_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sbb_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x1A:                  /* SBB byte - REG = REG - (EA) - CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sbb_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sbb_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x1B:                  /* SBB word - (EA) = (EA) - REG - CF */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sbb_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sbb_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x1C:                  /* SBB byte - AL = AL - DATA - CF */
                DATA = fetch_byte(1);
                VAL = sbb_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x1D:                  /* SBB word - (EA) = (EA) - REG - CF */
                DATA = fetch_word();
                VAL = sbb_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x1E:                  /* PUSH DS */
                push_word(DS);
                break;

            case 0x1F:                  /* POP DS */
                DS = pop_word();
                break;

            case 0x20:                  /* AND byte - REG = REG AND (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = and_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = and_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x21:                  /* AND word - (EA) = (EA) AND REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = and_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = and_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x22:                  /* AND byte - REG = REG AND (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = and_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = and_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x23:                  /* AND word - (EA) = (EA) AND REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = and_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = and_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x24:                  /* AND byte - AL = AL AND DATA */
                DATA = fetch_byte(1);
                VAL = and_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x25:                  /* AND word - (EA) = (EA) AND REG */
                DATA = fetch_word();
                VAL = and_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x26:                  /* ES: - segment override prefix */
                seg_ovr = SEG_ES;
                sysmode |= SYSMODE_SEGOVR_ES;
                break;

            case 0x27:                  /* DAA */
                if (((AL & 0xF) > 9) || GET_FLAG(AF)) {
                    AL += 6;
                    SET_FLAG(AF);
                }
                if ((AL > 0x9F) || GET_FLAG(CF)) {
                    AL += 0x60;
                    SET_FLAG(CF);
                }
                break;

            case 0x28:                  /* SUB byte - REG = REG - (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sub_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sub_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x29:                  /* SUB word - (EA) = (EA) - REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sub_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sub_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x2A:                  /* SUB byte - REG = REG - (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sub_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sub_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x2B:                  /* SUB word - (EA) = (EA) - REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = sub_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = sub_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x2C:                  /* SUB byte - AL = AL - DATA */
                DATA = fetch_byte(1);
                VAL = sub_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x2D:                  /* SUB word - (EA) = (EA) - REG */
                DATA = fetch_word();
                VAL = sub_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x2E:                  /* DS: - segment override prefix */
                seg_ovr = SEG_DS;
                sysmode |= SYSMODE_SEGOVR_DS;
                break;

            case 0x2F:                  /* DAS */
                if (((AL & 0xF) > 9) || GET_FLAG(AF)) {
                    AL -= 6;
                    SET_FLAG(AF);
                }
                if ((AL > 0x9F) || GET_FLAG(CF)) {
                    AL -= 0x60;
                    SET_FLAG(CF);
                }
                break;

            case 0x30:                  /* XOR byte - REG = REG XOR (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x31:                  /* XOR word - (EA) = (EA) XOR REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x32:                  /* XOR byte - REG = REG XOR (EA) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x33:                  /* XOR word - (EA) = (EA) XOR REG */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x34:                  /* XOR byte - AL = AL XOR DATA */
                DATA = fetch_byte(1);
                VAL = xor_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x35:                  /* XOR word - (EA) = (EA) XOR REG */
                DATA = fetch_word();
                VAL = xor_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x36:                  /* SS: - segment override prefix */
                seg_ovr = SEG_SS;
                sysmode |= SYSMODE_SEGOVR_SS;
                break;

            case 0x37:                  /* AAA */
                if (((AL & 0xF) > 9) || GET_FLAG(AF)) {
                    AL += 6;
                    AH++;
                    SET_FLAG(AF);
                }
                CONDITIONAL_SET_FLAG(GET_FLAG(AF), CF);
                AL &= 0xF;
                break;

            case 0x38:                  /* CMP byte - CMP (REG, (EA)) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x39:                  /* CMP word - CMP ((EA), REG) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smword(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x3A:                  /* CMP byte - CMP (REG, (EA)) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_byte(get_rbyte(REG), get_smbyte(seg_reg, EA));  /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_byte(get_rbyte(REG), get_rbyte(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x3B:                  /* CMP word - CMP ((EA), REG) */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = xor_word(get_rword(REG), get_smword(seg_reg, EA));  /* do operation */
                    put_smbyte(seg_reg, EA, VAL); /* store result */
                } else {                /* RM is second register */
                    VAL = xor_word(get_rword(REG), get_rword(RM)); /* do operation */
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x3C:                  /* CMP byte - CMP (AL, DATA) */
                DATA = fetch_byte(1);
                VAL = xor_byte(AL, DATA);  /* do operation */
                AL = VAL;               /* store result */
                break;

            case 0x3D:                  /* CMP word - CMP ((EA), REG) */
                DATA = fetch_word();
                VAL = xor_word(AX, DATA);  /* do operation */
                AX = VAL;               /* store result */
                break;

            case 0x3E:                  /* DS: - segment override prefix */
                seg_ovr = SEG_DS;
                sysmode |= SYSMODE_SEGOVR_DS;
                break;

            case 0x3F:                  /* AAS */
                if (((AL & 0xF) > 9) || GET_FLAG(AF)) {
                    AL -= 6;
                    AH--;
                    SET_FLAG(AF);
                }
                CONDITIONAL_SET_FLAG(GET_FLAG(AF), CF);
                AL &= 0xF;
                break;

            case 0x40:                  /* INC AX */
                AX = inc_word(AX);
                break;

            case 0x41:                  /* INC CX */
                CX = inc_word(CX);
                break;

            case 0x42:                  /* INC DX */
                DX = inc_word(DX);
                break;

            case 0x43:                  /* INC BX */
                BX = inc_word(BX);
                break;

            case 0x44:                  /* INC SP */
                SP = inc_word(SP);
                break;

            case 0x45:                  /* INC BP */
                BP = inc_word(BP);
                break;

            case 0x46:                  /* INC SI */
                SI = inc_word(SI);
                break;

            case 0x47:                  /* INC DI */
                DI = inc_word(DI);
                break;

            case 0x48:                  /* DEC AX */
                AX = dec_word(AX);
                break;

            case 0x49:                  /* DEC CX */
                CX = dec_word(CX);
                break;

            case 0x4A:                  /* DEC DX */
                DX = dec_word(DX);
                break;

            case 0x4B:                  /* DEC BX */
                BX = dec_word(BX);
                break;

            case 0x4C:                  /* DEC SP */
                SP = dec_word(SP);
                break;

            case 0x4D:                  /* DEC BP */
                BP = dec_word(BP);
                break;

            case 0x4E:                  /* DEC SI */
                SI = dec_word(SI);
                break;

            case 0x4F:                  /* DEC DI */
                DI = dec_word(DI);
                break;
 
            case 0x50:                  /* PUSH AX */
                push_word(AX);
                break;

            case 0x51:                  /* PUSH CX */
                push_word(CX);
                break;

            case 0x52:                  /* PUSH DX */
                push_word(DX);
                break;

            case 0x53:                  /* PUSH BX */
                push_word(BX);
                break;

            case 0x54:                  /* PUSH SP */
                push_word(SP);
                break;

            case 0x55:                  /* PUSH BP */
                push_word(BP);
                break;

            case 0x56:                  /* PUSH SI */
                push_word(SI);
                break;

            case 0x57:                  /* PUSH DI */
                push_word(DI);
                break;

            case 0x58:                  /* POP AX */
                AX = pop_word();
                break;

            case 0x59:                  /* POP CX */
                CX = pop_word();
                break;

            case 0x5A:                  /* POP DX */
                DX = pop_word();
                break;

            case 0x5B:                  /* POP BX */
                BX = pop_word();
                break;

            case 0x5C:                  /* POP SP */
                SP = pop_word();
                break;

            case 0x5D:                  /* POP BP */
                BP = pop_word();
                break;

            case 0x5E:                  /* POP SI */
                SI = pop_word();
                break;

            case 0x5F:                  /* POP DI */
                DI = pop_word();
                break;

            /* 0x60 - 0x6F - Not implemented on 8086/8088 */

            case 0x70:                  /* JO short label */
                /* jump to byte offset if overflow flag is set */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (GET_FLAG(OF))
                    IP = EA;
                break;

            case 0x71:                  /* JNO short label */
                /* jump to byte offset if overflow flag is clear */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (!GET_FLAG(OF))
                    IP = EA;
                break;

            case 0x72:                  /* JB/JNAE/JC short label */
                /* jump to byte offset if carry flag is set. */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (GET_FLAG(CF))
                    IP = EA;
                break;

            case 0x73:                  /* JNB/JAE/JNC short label */
                /* jump to byte offset if carry flsg is clear */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (!GET_FLAG(CF))
                    IP = EA;
                break;

            case 0x74:                  /* JE/JZ short label */
                /* jump to byte offset if zero flag is set */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (GET_FLAG(ZF))
                    IP = EA;
                break;

            case 0x75:                  /* JNE/JNZ short label */
                /* jump to byte offset if zero flag is clear */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (!GET_FLAG(ZF))
                    IP = EA;
                break;

            case 0x76:                  /* JBE/JNA short label */
                /* jump to byte offset if carry flag is set or if the zero
                    flag is set. */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (GET_FLAG(CF) || GET_FLAG(ZF))
                    IP = EA;
                break;

            case 0x77:                  /* JNBE/JA short label */
                /* jump to byte offset if carry flag is clear and if the zero
                    flag is clear */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (!(GET_FLAG(CF) || GET_FLAG(ZF)))
                    IP = EA;
                break;

            case 0x78:                  /* JS short label */
                /* jump to byte offset if sign flag is set */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (GET_FLAG(SF))
                    IP = EA;
                break;

            case 0x79:                  /* JNS short label */
                /* jump to byte offset if sign flag is clear */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (!GET_FLAG(SF))
                    IP = EA;
                break;

            case 0x7A:                  /* JP/JPE short label */
                /* jump to byte offset if parity flag is set (even) */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (GET_FLAG(PF))
                    IP = EA;
                break;

            case 0x7B:                  /* JNP/JPO short label */
                /* jump to byte offset if parity flsg is clear (odd) */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (!GET_FLAG(PF))
                    IP = EA;
                break;

            case 0x7C:                  /* JL/JNGE short label */
                /* jump to byte offset if sign flag not equal to overflow flag. */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if ((GET_FLAG(SF) != 0) ^ (GET_FLAG(OF) != 0))
                    IP = EA;
                break;

            case 0x7D:                  /* JNL/JGE short label */
                /* jump to byte offset if sign flag equal to overflow flag. */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if ((GET_FLAG(SF) != 0) == (GET_FLAG(OF) != 0))
                    IP = EA;
                break;

            case 0x7E:                  /* JLE/JNG short label */
                /* jump to byte offset if sign flag not equal to overflow flag
                    or the zero flag is set */
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (((GET_FLAG(SF) != 0) ^ (GET_FLAG(OF) != 0)) || GET_FLAG(ZF))
                    IP = EA;
                break;

            case 0x7F:                  /* JNLE/JG short label */
                /* jump to byte offset if sign flag equal to overflow flag.
                    and the zero flag is clear*/
                OFF = sign_ext(fetch_byte(1));
                EA = (IP + OFF) & ADDRMASK16;
                if (((GET_FLAG(SF) != 0) == (GET_FLAG(OF) != 0)) || !GET_FLAG(ZF))
                    IP = EA;
                break;

            case 0x80:                  /* byte operands */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    DATA = fetch_byte(1); /* must be done after DISP is collected */
                    switch(REG) {
                        case 0:
                            VAL = add_byte(get_smbyte(seg_reg, EA), DATA);  /* ADD mem8, immed8 */
                            break;
                        case 1:
                            VAL = or_byte(get_smbyte(seg_reg, EA), DATA);  /* OR mem8, immed8 */
                            break;
                        case 2:
                            VAL = adc_byte(get_smbyte(seg_reg, EA), DATA);  /* ADC mem8, immed8 */
                            break;
                        case 3:
                            VAL = sbb_byte(get_smbyte(seg_reg, EA), DATA);  /* SBB mem8, immed8 */
                            break;
                        case 4:
                            VAL = and_byte(get_smbyte(seg_reg, EA), DATA);  /* AND mem8, immed8 */
                            break;
                        case 5:
                            VAL = sub_byte(get_smbyte(seg_reg, EA), DATA);  /* SUB mem8, immed8 */
                            break;
                        case 6:
                            VAL = xor_byte(get_smbyte(seg_reg, EA), DATA);  /* XOR mem8, immed8 */
                            break;
                        case 7:
                            VAL = cmp_byte(get_smbyte(seg_reg, EA), DATA);  /* CMP mem8, immed8 */
                            break;
                    }
                    put_rbyte(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = add_byte(get_rbyte(RM), DATA);  /* ADD REG8, immed8 */
                            break;
                        case 1:
                            VAL = or_byte(get_rbyte(RM), DATA);  /* OR REG8, immed8 */
                            break;
                        case 2:
                            VAL = adc_byte(get_rbyte(RM), DATA);  /* ADC REG8, immed8 */
                            break;
                        case 3:
                            VAL = sbb_byte(get_rbyte(RM), DATA);  /* SBB REG8, immed8 */
                            break;
                        case 4:
                            VAL = and_byte(get_rbyte(RM), DATA);  /* AND REG8, immed8 */
                            break;
                        case 5:
                            VAL = sub_byte(get_rbyte(RM), DATA);  /* SUB REG8, immed8 */
                            break;
                        case 6:
                            VAL = xor_byte(get_rbyte(RM), DATA);  /* XOR REG8, immed8 */
                            break;
                        case 7:
                            VAL = cmp_byte(get_rbyte(RM), DATA);  /* CMP REG8, immed8 */
                            break;
                    }
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x81:                  /* word operands */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    DATA = fetch_byte(1) << 8; /* must be done after DISP is collected */
                    DATA |= fetch_byte(1);
                    switch(REG) {
                        case 0:
                            VAL = add_word(get_smword(seg_reg, EA), DATA);  /* ADD mem16, immed16 */
                            break;
                        case 1:
                            VAL = or_word(get_smword(seg_reg, EA), DATA);  /* OR mem16, immed16 */
                            break;
                        case 2:
                            VAL = adc_word(get_smword(seg_reg, EA), DATA);  /* ADC mem16, immed16 */
                            break;
                        case 3:
                            VAL = sbb_word(get_smword(seg_reg, EA), DATA);  /* SBB mem16, immed16 */
                            break;
                        case 4:
                            VAL = and_word(get_smword(seg_reg, EA), DATA);  /* AND mem16, immed16 */
                            break;
                        case 5:
                            VAL = sub_word(get_smword(seg_reg, EA), DATA);  /* SUB mem16, immed16 */
                            break;
                        case 6:
                            VAL = xor_word(get_smword(seg_reg, EA), DATA);  /* XOR mem16, immed16 */
                            break;
                        case 7:
                            VAL = cmp_word(get_smword(seg_reg, EA), DATA);  /* CMP mem16, immed16 */
                            break;
                    }
                    put_rword(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = add_word(get_rword(RM), DATA);  /* ADD reg16, immed16 */
                            break;
                        case 1:
                            VAL = or_word(get_rword(RM), DATA);  /* OR reg16, immed16 */
                            break;
                        case 2:
                            VAL = adc_word(get_rword(RM), DATA);  /* ADC reg16, immed16 */
                            break;
                        case 3:
                            VAL = sbb_word(get_rword(RM), DATA);  /* SBB reg16, immed16 */
                            break;
                        case 4:
                            VAL = and_word(get_rword(RM), DATA);  /* AND reg16, immed16 */
                            break;
                        case 5:
                            VAL = sub_word(get_rword(RM), DATA);  /* SUB reg16, immed16 */
                            break;
                        case 6:
                            VAL = xor_word(get_rword(RM), DATA);  /* XOR reg16, immed16 */
                            break;
                        case 7:
                            VAL = cmp_word(get_rword(RM), DATA);  /* CMP reg16, immed16 */
                            break;
                    }
                    put_rword(RM, VAL); /* store result */
                }
                break;

            case 0x82:                  /* byte operands */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    DATA = fetch_byte(1); /* must be done after DISP is collected */
                    switch(REG) {
                        case 0:
                            VAL = add_byte(get_smbyte(seg_reg, EA), DATA);  /* ADD mem8, immed8 */
                            break;
                        case 2:
                            VAL = adc_byte(get_smbyte(seg_reg, EA), DATA);  /* ADC mem8, immed8 */
                            break;
                        case 3:
                            VAL = sbb_byte(get_smbyte(seg_reg, EA), DATA);  /* SBB mem8, immed8 */
                            break;
                        case 5:
                            VAL = sub_byte(get_smbyte(seg_reg, EA), DATA);  /* SUB mem8, immed8 */
                            break;
                        case 7:
                            VAL = cmp_byte(get_smbyte(seg_reg, EA), DATA);  /* CMP mem8, immed8 */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = add_byte(get_rbyte(RM), DATA);  /* ADD reg8, immed8 */
                            break;
                        case 2:
                            VAL = adc_byte(get_rbyte(RM), DATA);  /* ADC reg8, immed8 */
                            break;
                        case 3:
                            VAL = sbb_byte(get_rbyte(RM), DATA);  /* SBB reg8, immed8 */
                            break;
                        case 5:
                            VAL = sub_byte(get_rbyte(RM), DATA);  /* SUB reg8, immed8 */
                            break;
                        case 7:
                            VAL = cmp_byte(get_rbyte(RM), DATA);  /* CMP reg8, immed8 */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(REG, VAL); /* store result */
                }
                break;

            case 0x83:                  /* word operands */
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    DATA = fetch_byte(1) << 8; /* must be done after DISP is collected */
                    if (DATA & 0x80)
                        DATA |= 0xFF00;
                    else
                        DATA &= 0xFF;
                    switch(REG) {
                        case 0:
                            VAL = add_word(get_smword(seg_reg, EA), DATA);  /* ADD mem16, immed8-SX */
                            break;
                        case 2:
                            VAL = adc_word(get_smword(seg_reg, EA), DATA);  /* ADC mem16, immed8-SX */
                            break;
                        case 3:
                            VAL = sbb_word(get_smword(seg_reg, EA), DATA);  /* SBB mem16, immed8-SX */
                            break;
                        case 5:
                            VAL = sub_word(get_smword(seg_reg, EA), DATA);  /* SUB mem16, immed8-SX */
                            break;
                        case 7:
                            VAL = cmp_word(get_smword(seg_reg, EA), DATA);  /* CMP mem16, immed8-SX */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rword(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = add_word(get_rword(RM), DATA);  /* ADD reg16, immed8-SX */
                            break;
                        case 2:
                            VAL = adc_word(get_rword(RM), DATA);  /* ADC reg16, immed8-SX */
                            break;
                        case 3:
                            VAL = sbb_word(get_rword(RM), DATA);  /* SBB reg16, immed8-SX */
                            break;
                        case 5:
                            VAL = sub_word(get_rword(RM), DATA);  /* CUB reg16, immed8-SX */
                            break;
                        case 7:
                            VAL = cmp_word(get_rword(RM), DATA);  /* CMP reg16, immed8-SX */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rword(RM, VAL); /* store result */
                }
                break;

            case 0x84:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    test_byte(get_smbyte(seg_reg, EA),get_rbyte(REG));  /* TEST mem8, reg8 */
                } else {
                    test_byte(get_rbyte(REG),get_rbyte(RM));  /* TEST reg8, reg8 */
                }
                break;

            case 0x85:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    test_word(get_smword(seg_reg, EA),get_rword(REG));  /* TEST mem16, reg16 */
                } else {
                    test_word(get_rword(REG),get_rword(RM));  /* TEST reg16, reg16 */
                }
                break;

            case 0x86:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = get_rbyte(REG);/* XCHG mem8, reg8 */
                    put_rbyte(REG, get_smbyte(seg_reg, EA));
                    put_smbyte(seg_reg, EA, VAL);
                } else {
                    VAL = get_rbyte(RM);/* XCHG reg8, reg8 */
                    put_rbyte(RM, get_rbyte(REG));
                    put_rbyte(REG, VAL);
                }
                break;

            case 0x87:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    VAL = get_rword(REG);/* XCHG mem16, reg16 */
                    put_rword(REG, get_smword(seg_reg, EA));
                    put_smword(seg_reg, EA, VAL);
                } else {
                    VAL = get_rword(RM);/* XCHG reg16, reg16 */
                    put_rword(RM, get_rword(REG));
                    put_rword(REG, VAL);
                }
                break;

            case 0x88:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_smbyte(seg_reg, EA, get_rbyte(REG)); /* MOV mem8, reg8 */
                } else
                    put_rbyte(REG, get_rbyte(RM)); /* MOV reg8, reg8 */
                break;

            case 0x89:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_smword(seg_reg, EA, get_rword(REG)); /* MOV mem16, reg16 */
                } else
                    put_rword(REG, get_rword(RM)); /* MOV reg16, reg16 */
                break;

            case 0x8A:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_rbyte(REG, get_smbyte(seg_reg, EA)); /* MOV reg8, mem8 */
                } else
                    put_rbyte(REG, get_rbyte(RM)); /* MOV reg8, reg8 */
                break;

            case 0x8B:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_rword(REG, get_smword(seg_reg, EA)); /* MOV reg16, mem16 */
                } else
                    put_rword(REG, get_rword(RM)); /* MOV reg16, reg16 */
                break;

            case 0x8C:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:          /* MOV mem16, ES */ 
                            put_smword(seg_reg, EA, ES);
                            break;
                        case 1:          /* MOV mem16, CS */ 
                            put_smword(seg_reg, EA, CS);
                            break;
                        case 2:          /* MOV mem16, SS */ 
                            put_smword(seg_reg, EA, SS);
                            break;
                        case 3:          /* MOV mem16, DS */ 
                            put_smword(seg_reg, EA, DS);
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                } else {
                    switch(REG) {
                        case 0:          /* MOV reg16, ES */ 
                            put_rword(RM, ES);
                            break;
                        case 1:          /* MOV reg16, CS */ 
                            put_rword(RM, CS);
                            break;
                        case 2:          /* MOV reg16, SS */ 
                            put_rword(RM, SS);
                            break;
                        case 3:          /* MOV reg16, DS */ 
                            put_rword(RM, DS);
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                }
                break;

            case 0x8D:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_rword(REG, EA); /* LEA reg16, mem16 */
                } else {
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                break;

            case 0x8E:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:          /* MOV ES, mem16 */ 
                            ES = get_smword(seg_reg, EA);
                            break;
                        case 1:          /* MOV CS, mem16 */ 
                            CS = get_smword(seg_reg, EA);
                            break;
                        case 2:          /* MOV SS, mem16 */ 
                            SS = get_smword(seg_reg, EA);
                            break;
                        case 3:          /* MOV DS, mem16 */ 
                            DS = get_smword(seg_reg, EA);
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                } else {
                    switch(REG) {
                        case 0:          /* MOV ES, reg16 */ 
                            ES = get_rword(RM);
                            break;
                        case 1:          /* MOV CS, reg16 */ 
                            CS = get_rword(RM);
                            break;
                        case 2:          /* MOV SS, reg16 */ 
                            SS = get_rword(RM);
                            break;
                        case 3:          /* MOV DS, reg16 */ 
                            DS = get_rword(RM);
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                }
                break;

            case 0x8f:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_smword(seg_reg, EA, pop_word());
                } else {
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                break;

            case 0x90:                  /* NOP */
                break;

            case 0x91:                  /* XCHG AX, CX */
                VAL = AX;
                AX = CX;
                CX = VAL;
                break;

            case 0x92:                  /* XCHG AX, DX */
                VAL = AX;
                AX = DX;
                DX = VAL;
                break;

            case 0x93:                  /* XCHG AX, BX */
                VAL = AX;
                AX = BX;
                BX = VAL;
                break;

            case 0x94:                  /* XCHG AX, SP */
                VAL = AX;
                AX = SP;
                SP = VAL;
                break;

            case 0x95:                  /* XCHG AX, BP */
                VAL = AX;
                AX = BP;
                BP = VAL;
                break;

            case 0x96:                  /* XCHG AX, SI */
                VAL = AX;
                AX = SI;
                SI = VAL;
                break;

            case 0x97:                  /* XCHG AX, DI */
                VAL = AX;
                AX = DI;
                DI = VAL;
                break;

            case 0x98:                  /* cbw */
               if (AL & 0x80)
                  AH = 0xFF;
               else
                  AH = 0;
                break;

            case 0x99:                  /* cbw */
               if (AX & 0x8000)
                  DX = 0xffff;
               else
                  DX = 0x0;
                break;

            case 0x9A:                  /* CALL FAR proc */
                OFF = fetch_word();  /* do operation */
                SEG = fetch_word();
                push_word(CS);
                CS = SEG;
                push_word(IP);
                IP = OFF;
                break;

            case 0x9B:                  /* WAIT */
                break;

            case 0x9C:                  /* PUSHF */
                VAL = PSW;
                VAL &= PSW_MSK;
                VAL |= PSW_ALWAYS_ON;
                push_word(VAL);
                break;

            case 0x9D:                  /* POPF */
                PSW = pop_word();
                break;

            case 0x9E:                  /* SAHF */
                PSW &= 0xFFFFFF00;
                PSW |= AH;
                break;

            case 0x9F:                  /* LAHF */
                AH = PSW & 0xFF;
                AH |= 0x2;
                break;

            case 0xA0:                  /* MOV AL, mem8 */
                OFF = fetch_word();
                set_segreg(SEG_DS);     /* to allow segment override */
                AL = get_smbyte(seg_reg, OFF);
                break;

            case 0xA1:                  /* MOV AX, mem16 */
                OFF = fetch_word();
                set_segreg(SEG_DS);     /* to allow segment override */
                AX = get_smword(seg_reg, OFF);
                break;

            case 0xA2:                  /* MOV mem8, AL */
                OFF = fetch_word();
                set_segreg(SEG_DS);     /* to allow segment override */
                put_smbyte(seg_reg, OFF, AL);
                break;

            case 0xA3:                  /* MOV mem16, AX */
                OFF = fetch_word();
                set_segreg(SEG_DS);     /* to allow segment override */
                put_smword(seg_reg, OFF, AX);
                break;

            case 0xA4:                  /* MOVS dest-str8, src-str8 */
                if (GET_FLAG(DF))       /* down */
                    INC = -1;
                else
                    INC = 1;
                while (CX != 0) {
                    VAL = get_smbyte(seg_reg, SI);
                    put_smbyte(ES, DI, VAL);
                    CX--;
                    SI += INC;
                    DI += INC;
                }
                break;

            case 0xA5:                  /* MOVS dest-str16, src-str16 */
                if (GET_FLAG(DF))       /* down */
                    INC = -2;
                else
                    INC = 2;
                while (CX != 0) {
                    VAL = get_smword(seg_reg, SI);
                    put_smword(ES, DI, VAL);
                    CX--;
                    SI += INC;
                    DI += INC;
                }
                break;

            case 0xA6:                  /* CMPS dest-str8, src-str8 */
                if (GET_FLAG(DF))       /* down */
                    INC = -1;
                else
                    INC = 1;
                while (CX != 0) {
                    VAL = get_smbyte(seg_reg, SI);
                    VAL1 = get_smbyte(ES, DI);
                    cmp_byte(VAL, VAL1);
                    CX--;
                    SI += INC;
                    DI += INC;
                    if (GET_FLAG(ZF) == 0)
                        break;
                }
                break;

            case 0xA7:                  /* CMPS dest-str16, src-str16 */
                if (GET_FLAG(DF))       /* down */
                    INC = -2;
                else
                    INC = 2;
                while (CX != 0) {
                    VAL = get_smword(seg_reg, SI);
                    VAL1 = get_smword(ES, DI);
                    cmp_word(VAL, VAL1);
                    CX--;
                    SI += INC;
                    DI += INC;
                    if (GET_FLAG(ZF) == 0)
                        break;
                }
                break;

            case 0xA8:                  /* TEST AL, immed8 */
                VAL = fetch_byte(1);
                test_byte(AL, VAL);
                break;

            case 0xA9:                  /* TEST AX, immed8 */
                VAL = fetch_word();
                test_word(AX, VAL);
                break;

            case 0xAA:                  /* STOS dest-str8 */
                if (GET_FLAG(DF))       /* down */
                    INC = -1;
                else
                    INC = 1;
                if (sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
                    while (CX != 0) {
                        put_smbyte(ES, DI, AL);
                        CX--;
                        DI += INC;
                    }
                sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
                } else {
                    put_smbyte(ES, DI, AL);
                    DI += INC;
                }
                break;

            case 0xAB:                  /* STOS dest-str16 */
                if (GET_FLAG(DF))       /* down */
                    INC = -1;
                else
                    INC = 1;
                if (sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
                    while (CX != 0) {
                        put_smword(ES, DI, AX);
                        CX--;
                        DI += INC;
                    }
                sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
                } else {
                    put_smword(ES, DI, AL);
                    DI += INC;
                }
                break;

            case 0xAC:                  /* LODS dest-str8 */
                if (GET_FLAG(DF))       /* down */
                    INC = -1;
                else
                    INC = 1;
                set_segreg(SEG_DS); /* allow overrides */
                if (sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
                    while (CX != 0) {
                        AL = get_smbyte(seg_reg, SI);
                        CX--;
                        SI += INC;
                    }
                sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
                } else {
                    AL = get_smbyte(seg_reg, SI);
                    SI += INC;
                }
                break;

            case 0xAD:                  /* LODS dest-str16 */
                if (GET_FLAG(DF)) /* down */
                    INC = -1;
                else
                    INC = 1;
                set_segreg(SEG_DS); /* allow overrides */
                if (sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
                    while (CX != 0) {
                        AX = get_smword(seg_reg, SI);
                        CX--;
                        SI += INC;
                    }
                sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
                } else {
                    AX = get_smword(seg_reg, SI);
                    SI += INC;
                }
                break;

            case 0xAE:                  /* SCAS dest-str8 */
                if (GET_FLAG(DF))       /* down */
                    INC = -1;
                else
                    INC = 1;
                if (sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
                    while (CX != 0) {
                        VAL = get_smbyte(ES, DI);
                        cmp_byte(AL, VAL);
                        CX--;
                        DI += INC;
                        if (GET_FLAG(ZF) == 0)
                            break;
                    }
                sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
                } else {
                    VAL = get_smbyte(ES, DI);
                    cmp_byte(AL, VAL);
                    DI += INC;
                }
                break;

            case 0xAF:                  /* SCAS dest-str16 */
                if (GET_FLAG(DF))       /* down */
                    INC = -2;
                else
                    INC = 2;
                if (sysmode & (SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE)) {
                    while (CX != 0) {
                        VAL = get_smword(ES, DI);
                        cmp_word(AX, VAL);
                        CX--;
                        DI += INC;
                        if (GET_FLAG(ZF) == 0)
                            break;
                    }
                sysmode &= ~(SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE);
                } else {
                    VAL = get_smword(ES, DI);
                    cmp_byte(AL, VAL);
                    DI += INC;
                }
                break;

            case 0xB0:                  /* MOV AL,immed8 */
                AL = fetch_byte(1);
                break;

            case 0xB1:                  /* MOV CL,immed8 */
                CL = fetch_byte(1);
                break;

            case 0xB2:                  /* MOV DL,immed8 */
                DL = fetch_byte(1);
                break;

            case 0xB3:                  /* MOV BL,immed8 */
                BL = fetch_byte(1);
                break;

            case 0xB4:                  /* MOV AH,immed8 */
                AH = fetch_byte(1);
                break;

            case 0xB5:                  /* MOV CH,immed8 */
                CH = fetch_byte(1);
                break;

            case 0xB6:                  /* MOV DH,immed8 */
                DH = fetch_byte(1);
                break;

            case 0xB7:                  /* MOV BH,immed8 */
                BH = fetch_byte(1);
                break;

            case 0xB8:                  /* MOV AX,immed16 */
                AX = fetch_word();
                break;

            case 0xB9:                  /* MOV CX,immed16 */
                CX = fetch_word();
                break;

            case 0xBA:                  /* MOV DX,immed16 */
                DX = fetch_word();
                break;

            case 0xBB:                  /* MOV BX,immed16 */
                BX = fetch_word();
                break;

            case 0xBC:                  /* MOV SP,immed16 */
                SP = fetch_word();
                break;

            case 0xBD:                  /* MOV BP,immed16 */
                BP = fetch_word();
                break;

            case 0xBE:                  /* MOV SI,immed16 */
                SI = fetch_word();
                break;

            case 0xBF:                  /* MOV DI,immed16 */
                DI = fetch_word();
                break;

            /* 0xC0 - 0xC1 - Not implemented on 8086/8088 */

            case 0xC2:                  /* RET immed16 (intrasegment) */
                OFF = fetch_word();
                IP = pop_word();
                SP += OFF;
                break;

            case 0xC3:                  /* RET (intrasegment) */
                IP = pop_word();
                break;

            case 0xC4: 
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_rword(REG, get_smword(seg_reg, EA)); /* LES mem16 */
                    ES = get_smword(seg_reg, EA + 2);
                } else {
//                    put_rword(REG, get_rword(RM)); /* LES reg16 */
//                    ES = get_rword(RM) + 2;
                    /* not defined for 8086 */
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                break;

            case 0xC5: 
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    put_rword(REG, get_smword(seg_reg, EA)); /* LDS mem16 */
                    DS = get_smword(seg_reg, EA + 2);
                } else {
//                    put_rword(REG, get_rword(RM)); /* LDS reg16 */
//                    DS = get_rword(RM) + 2;
                    /* not defined for 8086 */
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                break;

            case 0xC6: 
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (REG) {              /* has to be 0 */
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    DATA = fetch_byte(1); /* has to be after DISP */
                    put_smbyte(seg_reg, EA, DATA); /* MOV mem8, immed8 */
                } else {
                    put_rbyte(RM, DATA); /* MOV reg8, immed8 */
                }
                break;

            case 0xC7: 
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (REG) {              /* has to be 0 */
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    DATA = get_mword(IP); /* has to be after DISP */
                    put_smword(seg_reg, EA, DATA); /* MOV mem16, immed16 */
                } else {
                    put_rword(RM, DATA); /* MOV reg16, immed16 */
                }
                break;

            /* 0xC8 - 0xC9 - Not implemented on 8086/8088 */

            case 0xCA:                  /* RET immed16 (intersegment) */
                OFF = fetch_word();
                IP = pop_word();
                CS = pop_word();
                SP += OFF;
                break;

            case 0xCB:                  /* RET (intersegment) */
                IP = pop_word();
                CS = pop_word();
                break;

            case 0xCC:                  /* INT 3 */
                push_word(PSW);
                CLR_FLAG(IF);
                CLR_FLAG(TF);
                push_word(CS);
                push_word(IP);
                CS = get_mword(14);
                IP = get_mword(12); 
                break;

            case 0xCD:                  /* INT immed8 */
                OFF = fetch_byte(1);
                push_word(PSW);
                CLR_FLAG(IF);
                CLR_FLAG(TF);
                push_word(CS);
                push_word(IP);
                CS = get_mword((OFF * 4) + 2);
                IP = get_mword(OFF * 4); 
                break;

            case 0xCE:                  /* INTO */
                push_word(PSW);
                CLR_FLAG(IF);
                CLR_FLAG(TF);
                push_word(CS);
                push_word(IP);
                CS = get_mword(18);
                IP = get_mword(16); 
                break;

            case 0xCF:                  /* IRET */
                IP = pop_word();
                CS = pop_word();
                PSW = pop_word();
                break;

            case 0xD0:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:
                            VAL = rol_byte(get_smbyte(seg_reg, EA), 1);  /* ROL mem8, 1 */
                            break;
                        case 1:
                            VAL = ror_byte(get_smbyte(seg_reg, EA), 1);  /* ROR mem8, 1 */
                            break;
                        case 2:
                            VAL = rcl_byte(get_smbyte(seg_reg, EA), 1);  /* RCL mem8, 1 */
                            break;
                        case 3:
                            VAL = rcr_byte(get_smbyte(seg_reg, EA), 1);  /* RCR mem8, 1 */
                            break;
                        case 4:
                            VAL = shl_byte(get_smbyte(seg_reg, EA), 1);  /* SAL/SHL mem8, 1 */
                            break;
                        case 5:
                            VAL = shr_byte(get_smbyte(seg_reg, EA), 1);  /* SHR mem8, 1 */
                            break;
                        case 7:
                            VAL = sar_byte(get_smbyte(seg_reg, EA), 1);  /* SAR mem8, 1 */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = rol_byte(get_rbyte(RM), 1);  /* RCL reg8, 1 */
                            break;
                        case 1:
                            VAL = ror_byte(get_rbyte(RM), 1);  /* ROR reg8, 1 */
                            break;
                        case 2:
                            VAL = rcl_byte(get_rbyte(RM), 1);  /* RCL reg8, 1 */
                            break;
                        case 3:
                            VAL = rcr_byte(get_rbyte(RM), 1);  /* RCR reg8, 1 */
                            break;
                        case 4:
                            VAL = shl_byte(get_rbyte(RM), 1);  /* SHL/SAL reg8, 1*/
                            break;
                        case 5:
                            VAL = shr_byte(get_rbyte(RM), 1);  /* SHR reg8, 1 */
                            break;
                        case 7:
                            VAL = sar_byte(get_rbyte(RM), 1);  /* SAR reg8, 1 */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;

            case 0xD1:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:
                            VAL = rol_word(get_smword(seg_reg, EA), 1);  /* ROL mem16, 1 */
                            break;
                        case 1:
                            VAL = ror_word(get_smword(seg_reg, EA), 1);  /* ROR mem16, 1 */
                            break;
                        case 2:
                            VAL = rcl_word(get_smword(seg_reg, EA), 1);  /* RCL mem16, 1 */
                            break;
                        case 3:
                            VAL = rcr_word(get_smword(seg_reg, EA), 1);  /* RCR mem16, 1 */
                            break;
                        case 4:
                            VAL = shl_word(get_smword(seg_reg, EA), 1);  /* SAL/SHL mem16, 1 */
                            break;
                        case 5:
                            VAL = shr_word(get_smword(seg_reg, EA), 1);  /* SHR mem16, 1 */
                            break;
                        case 7:
                            VAL = sar_word(get_smword(seg_reg, EA), 1);  /* SAR mem16, 1 */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rword(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = rol_word(get_rword(RM), 1);  /* RCL reg16, 1 */
                            break;
                        case 1:
                            VAL = ror_word(get_rword(RM), 1);  /* ROR reg16, 1 */
                            break;
                        case 2:
                            VAL = rcl_word(get_rword(RM), 1);  /* RCL reg16, 1 */
                            break;
                        case 3:
                            VAL = rcr_word(get_rword(RM), 1);  /* RCR reg16, 1 */
                            break;
                        case 4:
                            VAL = shl_word(get_rword(RM), 1);  /* SHL/SAL reg16, 1 */
                            break;
                        case 5:
                            VAL = shr_word(get_rword(RM), 1);  /* SHR reg16, 1 */
                            break;
                        case 7:
                            VAL = sar_word(get_rword(RM), 1);  /* SAR reg16, 1 */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;

            case 0xD2:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:
                            VAL = rol_byte(get_smbyte(seg_reg, EA), CL);  /* ROL mem8, CL */
                            break;
                        case 1:
                            VAL = ror_byte(get_smbyte(seg_reg, EA), CL);  /* ROR mem8, CL */
                            break;
                        case 2:
                            VAL = rcl_byte(get_smbyte(seg_reg, EA), CL);  /* RCL mem8, CL */
                            break;
                        case 3:
                            VAL = rcr_byte(get_smbyte(seg_reg, EA), CL);  /* RCR mem8, CL */
                            break;
                        case 4:
                            VAL = shl_byte(get_smbyte(seg_reg, EA), CL);  /* SAL/SHL mem8, CL */
                            break;
                        case 5:
                            VAL = shr_byte(get_smbyte(seg_reg, EA), CL);  /* SHR mem8, CL */
                            break;
                        case 7:
                            VAL = sar_byte(get_smbyte(seg_reg, EA), CL);  /* SAR mem8, CL */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = rol_byte(get_rbyte(RM), CL);  /* RCL reg8, CL */
                            break;
                        case 1:
                            VAL = ror_byte(get_rbyte(RM), CL);  /* ROR reg8, CL */
                            break;
                        case 2:
                            VAL = rcl_byte(get_rbyte(RM), CL);  /* RCL reg8, CL */
                            break;
                        case 3:
                            VAL = rcr_byte(get_rbyte(RM), CL);  /* RCR reg8, CL */
                            break;
                        case 4:
                            VAL = shl_byte(get_rbyte(RM), CL);  /* SHL/SAL reg8, CL*/
                            break;
                        case 5:
                            VAL = shr_byte(get_rbyte(RM), CL);  /* SHR reg8, CL */
                            break;
                        case 7:
                            VAL = sar_byte(get_rbyte(RM), CL);  /* SAR reg8, CL */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;

            case 0xD3:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:
                            VAL = rol_word(get_smword(seg_reg, EA), CL);  /* ROL mem16, CL */
                            break;
                        case 1:
                            VAL = ror_word(get_smword(seg_reg, EA), CL);  /* ROR mem16, CL */
                            break;
                        case 2:
                            VAL = rcl_word(get_smword(seg_reg, EA), CL);  /* RCL mem16, CL */
                            break;
                        case 3:
                            VAL = rcr_word(get_smword(seg_reg, EA), CL);  /* RCR mem16, CL */
                            break;
                        case 4:
                            VAL = shl_word(get_smword(seg_reg, EA), CL);  /* SAL/SHL mem16, CL */
                            break;
                        case 5:
                            VAL = shr_word(get_smword(seg_reg, EA), CL);  /* SHR mem16, CL */
                            break;
                        case 7:
                            VAL = sar_word(get_smword(seg_reg, EA), CL);  /* SAR mem16, CL */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rword(EA, VAL);  /* store result */
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = rol_word(get_rword(RM), CL);  /* RCL reg16, CL */
                            break;
                        case 1:
                            VAL = ror_word(get_rword(RM), CL);  /* ROR reg16, CL */
                            break;
                        case 2:
                            VAL = rcl_word(get_rword(RM), CL);  /* RCL reg16, CL */
                            break;
                        case 3:
                            VAL = rcr_word(get_rword(RM), CL);  /* RCR reg16, CL */
                            break;
                        case 4:
                            VAL = shl_word(get_rword(RM), CL);  /* SHL/SAL reg16, CL */
                            break;
                        case 5:
                            VAL = shr_word(get_rword(RM), CL);  /* SHR reg16, CL */
                            break;
                        case 7:
                            VAL = sar_word(get_rword(RM), CL);  /* SAR reg16, CL */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;

            case 0xD4:                  /* AAM */
                VAL = fetch_word();
                if (VAL != 10) {
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                /* note the type change here --- returning AL and AH in AX. */
                AX = aam_word(AL);
                break;

            case 0xD5:                  /* AAD */
                VAL = fetch_word();
                if (VAL != 10) {
                    reason = STOP_OPCODE;
                    IP -= 2;
                }
                AX = aad_word(AX);
                break;

            /* 0xD6 - Not implemented on 8086/8088 */

            case 0xD7:                  /* XLAT */
                OFF = BX + (uint8)AL;
                AL = get_smbyte(SEG_CS, OFF);
                break;

            case 0xD8:                  /* ESC */
            case 0xD9:
            case 0xDA:
            case 0xDB:
            case 0xDC:
            case 0xDD:
            case 0xDE:
            case 0xDF:
                /* for now, do nothing, NOP for 8088 */
                break;

            case 0xE0:                  /* LOOPNE label */
                OFF = fetch_byte(1);
                OFF += (int16)IP;
                CX -= 1;
                if (CX != 0 && !GET_FLAG(ZF))  /* CX != 0 and !ZF */
                    IP = OFF;
                break;

            case 0xE1:                  /* LOOPE label */
                OFF = fetch_byte(1);
                OFF += (int16)IP;
                CX -= 1;
                if (CX != 0 && GET_FLAG(ZF))  /* CX != 0 and ZF */
                    IP = OFF;
                break;

            case 0xE2:                  /* LOOP label */
                OFF = fetch_byte(1);
                OFF += (int16)IP;
                CX -= 1;
                if (CX != 0)            /* CX != 0 */
                    IP = OFF;
                break;

            case 0xE3:                  /* JCXZ label */
                OFF = fetch_byte(1);
                OFF += (int16)IP;
                if (CX == 0)            /* CX != 0 */
                    IP = OFF;
                break;

            case 0xE4:                  /* IN AL,immed8 */
                OFF = fetch_byte(1);
                AL = dev_table[OFF].routine(0, 0);
                break;

            case 0xE5:                  /* IN AX,immed8 */
                OFF = fetch_byte(1);
                AH = dev_table[OFF].routine(0, 0);
                AL = dev_table[OFF+1].routine(0, 0);
                break;

            case 0xE6:                  /* OUT AL,immed8 */
                OFF = fetch_byte(1);
                dev_table[OFF].routine(1, AL);
                break;

            case 0xE7:                  /* OUT AX,immed8 */
                OFF = fetch_byte(1);
                dev_table[OFF].routine(1, AH);
                dev_table[OFF+1].routine(1, AL);
                break;

            case 0xE8:                  /* CALL NEAR proc */
                OFF = fetch_word();
                push_word(IP);
                IP = (OFF + IP) & ADDRMASK16;
                break;

            case 0xE9:                  /* JMP NEAR label */
                OFF = fetch_word();
                IP = (OFF + IP) & ADDRMASK16;
                break;

            case 0xEA:                  /* JMP FAR label */
                OFF = fetch_word();
                SEG = fetch_word();
                CS = SEG;
                IP = OFF;
                break;

            case 0xEB:                  /* JMP short-label */
                OFF = fetch_byte(1);
                if (OFF & 0x80)         /* if negative, sign extend */
                    OFF |= 0XFF00;
                IP = (IP + OFF) & ADDRMASK16;
                break;

            case 0xEC:                  /* IN AL,DX */
                AL = dev_table[DX].routine(0, 0);
                break;

            case 0xED:                  /* IN AX,DX */
                AH = dev_table[DX].routine(0, 0);
                AL = dev_table[DX+1].routine(0, 0);
                break;

            case 0xEE:                  /* OUT AL,DX */
                dev_table[DX].routine(1, AL);
                break;

            case 0xEF:                  /* OUT AX,DX */
                dev_table[DX].routine(1, AH);
                dev_table[DX+1].routine(1, AL);
                break;

            case 0xF0:                  /* LOCK */
                /* do nothing for now */
                break;

            /* 0xF1 - Not implemented on 8086/8088 */

            case 0xF2:                  /* REPNE/REPNZ */
                sysmode |= SYSMODE_PREFIX_REPNE;
                break;

            case 0xF3:                  /* REP/REPE/REPZ */
                sysmode |= SYSMODE_PREFIX_REPE;
                break;

            case 0xF4:                  /* HLT */
                reason = STOP_HALT;
                IP--;
                break;

            case 0xF5:                  /* CMC */
                TOGGLE_FLAG(CF);
                break;

            case 0xF6:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:         /* TEST mem8, immed8 */
                            DATA = fetch_byte(1);
                            test_byte(get_smbyte(seg_reg, EA), DATA);
                            break;
                        case 2:         /* NOT mem8 */
                            VAL = not_byte(get_smbyte(seg_reg, EA));
                            put_smbyte(seg_reg, EA, VAL);  /* store result */
                            break;
                        case 3:         /* NEG mem8 */
                            VAL = neg_byte(get_smbyte(seg_reg, EA));
                            put_smbyte(seg_reg, EA, VAL);  /* store result */
                            break;
                        case 4:         /* MUL mem8 */
                            mul_byte(get_smbyte(seg_reg, EA));
                            break;
                        case 5:         /* IMUL mem8 */
                            imul_byte(get_smbyte(seg_reg, EA));
                            break;
                        case 6:         /* DIV mem8 */
                            div_byte(get_smbyte(seg_reg, EA));
                            break;
                        case 7:         /* IDIV mem8 */
                            idiv_byte(get_smbyte(seg_reg, EA));
                            break;
                        default:        /* bad opcode */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:         /* TEST reg8, immed8 */
                            DATA = fetch_byte(1);
                            test_byte(get_rbyte(RM), DATA);
                            break;
                        case 2:         /* NOT reg8 */
                            VAL = not_byte(get_rbyte(RM));
                            put_rbyte(RM, VAL);  /* store result */
                            break;
                        case 3:         /* NEG reg8 */
                            VAL = neg_byte(get_rbyte(RM));
                            put_rbyte(RM, VAL);  /* store result */
                            break;
                        case 4:         /* MUL reg8 */
                            mul_byte(get_rbyte(RM));
                            break;
                        case 5:         /* IMUL reg8 */
                            imul_byte(get_rbyte(RM));
                            break;
                        case 6:         /* DIV reg8 */
                            div_byte(get_rbyte(RM));
                            break;
                        case 7:         /* IDIV reg8 */
                            idiv_byte(get_rbyte(RM));
                            break;
                        default:        /* bad opcode */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;

            case 0xF7:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:         /* TEST mem16, immed16 */
                            DATA = fetch_word();
                            test_word(get_smword(seg_reg, EA), DATA);
                            break;
                        case 2:         /* NOT mem16 */
                            VAL = not_word(get_smword(seg_reg, EA));
                            put_smword(seg_reg, EA, VAL);  /* store result */
                            break;
                        case 3:         /* NEG mem16 */
                            VAL = neg_word(get_smword(seg_reg, EA));
                            put_smword(seg_reg, EA, VAL);  /* store result */
                            break;
                        case 4:         /* MUL mem16 */
                            mul_word(get_smword(seg_reg, EA));
                            break;
                        case 5:         /* IMUL mem16 */
                            imul_word(get_smword(seg_reg, EA));
                            break;
                        case 6:         /* DIV mem16 */
                            div_word(get_smword(seg_reg, EA));
                            break;
                        case 7:         /* IDIV mem16 */
                            idiv_word(get_smword(seg_reg, EA));
                            break;
                        default:        /* bad opcode */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:         /* TEST reg16, immed16 */
                            DATA = fetch_word();
                            test_word(get_rword(RM), DATA);
                            break;
                        case 2:         /* NOT reg16 */
                            VAL = not_word(get_rword(RM));
                            put_rword(RM, VAL);  /* store result */
                            break;
                        case 3:         /* NEG reg16 */
                            VAL = neg_word(get_rword(RM));
                            put_rword(RM, VAL);  /* store result */
                            break;
                        case 4:         /* MUL reg16 */
                            mul_word(get_rword(RM));
                            break;
                        case 5:         /* IMUL reg16 */
                            imul_word(get_rword(RM));
                            break;
                        case 6:         /* DIV reg16 */
                            div_word(get_rword(RM));
                            break;
                        case 7:         /* IDIV reg16 */
                            idiv_word(get_rword(RM));
                            break;
                        default:        /* bad opcode */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;

            case 0xF8:                  /* CLC */
                CLR_FLAG(CF);
                break;

            case 0xF9:                  /* STC */
                SET_FLAG(CF);
                break;

            case 0xFA:                  /* CLI */
                CLR_FLAG(IF);
                break;

            case 0xFB:                  /* STI */
                SET_FLAG(IF);
                break;

            case 0xFC:                  /* CLD */
                CLR_FLAG(DF);
                break;

            case 0xFD:                  /* STD */
                SET_FLAG(DF);
                break;

            case 0xFE:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:         /* INC mem16 */
                            VAL = inc_byte(get_smbyte(seg_reg, EA));  /* do operation */
                            put_smbyte(seg_reg, EA, VAL);  /* store result */
                            break;
                        case 1:         /* DEC mem16 */
                            VAL = dec_byte(get_smbyte(seg_reg, EA));  /* do operation */
                            put_smbyte(seg_reg, EA, VAL);  /* store result */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                } else {                /* RM is second register */
                    switch(REG) {
                        case 0:
                            VAL = inc_byte(get_rbyte(RM));  /* do operation */
                            put_rbyte(RM, VAL);  /* store result */
                            break;
                        case 1:
                            VAL = dec_byte(get_rbyte(RM));  /* do operation */
                            put_rbyte(RM, VAL);  /* store result */
                            break;
                        default:        /* bad opcodes */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;

            case 0xFF:
                MRR = fetch_byte(1);
                get_mrr_dec(MRR, &MOD, &REG, &RM);
                if (MOD != 0x3) {       /* based, indexed, or based indexed addressing */
                    EA = get_ea(MRR);   /* get effective address */
                    switch(REG) {
                        case 0:         /* INC mem16 */
                            VAL = inc_word(get_smword(seg_reg, EA));  /* do operation */
                            put_smword(seg_reg, EA, VAL);  /* store result */
                            break;
                        case 1:         /* DEC mem16 */
                            VAL = dec_word(get_smword(seg_reg, EA));  /* do operation */
                            put_smword(seg_reg, EA, VAL);  /* store result */
                            break;
                        case 2:         /* CALL NEAR mem16 */
                            OFF = get_smword(SEG_CS, EA);  /* do operation */
                            push_word(IP);
                            IP = OFF;
                            break;
                        case 3:         /* CALL FAR mem16 */
                            OFF = get_smword(SEG_CS, EA);  /* do operation */
                            SEG = get_smword(SEG_CS, EA + 2);
                            push_word(CS);
                            CS = SEG;
                            push_word(IP);
                            IP = OFF;
                            break;
                        case 4:         /* JMP NEAR mem16 */
                            OFF = get_smword(SEG_CS, EA);  /* do operation */
                            IP = OFF;
                            break;
                        case 5:         /* JMP FAR mem16 */
                            OFF = get_smword(SEG_CS, EA);  /* do operation */
                            SEG = get_smword(SEG_CS, EA + 2);
                            CS = SEG;
                            IP = OFF;
                            break;
                        case 6:         /* PUSH mem16 */
                            VAL = get_smword(seg_reg, EA);  /* do operation */
                            push_word(VAL);
                            break;
                        case 7:         /* bad opcode */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                } else {                /* RM is second register */
                    switch(REG) {
                        case 2:         /* CALL NEAR reg16 */
                            OFF = get_rword(RM);  /* do operation */
                            push_word(IP);
                            IP = OFF;
                            break;
                        case 4:         /* JMP NEAR reg16 */
                            OFF = get_rword(RM);  /* do operation */
                            IP = OFF;
                            break;
                        default:        /* bad opcode */
                            reason = STOP_OPCODE;
                            IP -= 2;
                            break;
                    }
                    put_rbyte(RM, VAL); /* store result */
                }
                break;


            default: 
//                if (i8088_unit.flags & UNIT_OPSTOP) {
                    reason = STOP_OPCODE;
                    IP--;
//                }
                break;
            }
            /* not segment override */
            if ((IR == 0x26) || (IR == 0x2E) || (IR == 0x36) || (IR == 0x3E)) {
                seg_ovr = SEG_NONE;     /* clear segment override */
            sysmode &= 0x0000001E;      /* clear flags */
            sysmode |= 0x00000001;
            }
    }

/* Simulation halted */

    saved_PC = IP;
    return reason;
}

/* emulation subfunctions */

int32 sign_ext(int32 val)
{
    int32 res;

    res = val;
    if (val & 0x80)
        res |= 0xFF00;
    return res;
}


int32 fetch_byte(int32 flag)
{
    uint8 val;

    val = get_smbyte(SEG_CS, IP) & 0xFF; /* fetch byte */
    if (i8088_dev.dctrl & DEBUG_asm) {  /* display source code */
        switch (flag) {
            case 0:                     /* opcode fetch */
//                sim_printf("%04X:%04X %s", CS, IP, opcode[val]);
                sim_printf("%04X:%04X %02X", CS, IP, val);
                break;
            case 1:                     /* byte operand fetch */
                sim_printf(" %02X", val);
                break;
        }
    }
    IP = INC_IP1;                       /* increment IP */
    return val;
}

int32 fetch_word(void)
{
    uint16 val;

    val = get_smbyte(SEG_CS, IP) & 0xFF; /* fetch low byte */
    val |= get_smbyte(SEG_CS, IP + 1) << 8; /* fetch high byte */
    if (i8088_dev.dctrl & DEBUG_asm)
//        sim_printf("0%04XH", val);
        sim_printf(" %04X", val);
    IP = INC_IP2;                       /* increment IP */
    return val;
}

/* calculate parity on a 8- or 16-bit value */

int32 parity(int32 val)
{
    int32 bc = 0;

    if (val & 0x0001) bc++;
    if (val & 0x0002) bc++;
    if (val & 0x0004) bc++;
    if (val & 0x0008) bc++;
    if (val & 0x0010) bc++;
    if (val & 0x0020) bc++;
    if (val & 0x0040) bc++;
    if (val & 0x0080) bc++;
    if (val & 0x0100) bc++;
    if (val & 0x0200) bc++;
    if (val & 0x0400) bc++;
    if (val & 0x0800) bc++;
    if (val & 0x1000) bc++;
    if (val & 0x2000) bc++;
    if (val & 0x4000) bc++;
    if (val & 0x8000) bc++;
    return bc & 1;
}

void i86_intr_raise(uint8 num)
{
    /* do nothing for now */
}

/* return byte register */

uint32 get_rbyte(uint32 reg)
{
    uint32 val;

    switch(reg) {
        case 0: val = AL; break;
        case 1: val = CL; break;
        case 2: val = DL; break;
        case 3: val = BL; break;
        case 4: val = AH; break;
        case 5: val = CH; break;
        case 6: val = DH; break;
        case 7: val = BH; break;
    }
    return val;
}

/* return word register - added segment registers as 8-11 */

uint32 get_rword(uint32 reg)
{
    uint32 val;

    switch(reg) {
        case 0: val = AX; break;
        case 1: val = CX; break;
        case 2: val = DX; break;
        case 3: val = BX; break;
        case 4: val = SP; break;
        case 5: val = BP; break;
        case 6: val = SI; break;
        case 7: val = DI; break;
        case 8: val = CS; break;
        case 9: val = DS; break;
        case 10: val = ES; break;
        case 11: val = SS; break;
    }
    return val;
}

/* set byte register */

void put_rbyte(uint32 reg, uint32 val)
{
    val &= 0xFF;                            /* force byte */
    switch(reg){
        case 0: AL = val; break;
        case 1: CL = val; break;
        case 2: DL = val; break;
        case 3: BL = val; break;
        case 4: AH = val; break;
        case 5: CH = val; break;
        case 6: DH = val; break;
        case 7: BH = val; break;
    }
}

/* set word register */

void put_rword(uint32 reg, uint32 val)
{
    val &= 0xFFFF;                          /* force word */
    switch(reg){
        case 0: AX = val; break;
        case 1: CX = val; break;
        case 2: DX = val; break;
        case 3: BX = val; break;
        case 4: SP = val; break;
        case 5: BP = val; break;
        case 6: SI = val; break;
        case 7: DI = val; break;
    }
}

/* set seg_reg as required for EA */

void set_segreg(uint32 reg)
{
    if (seg_ovr)
        seg_reg = seg_ovr;
    else
        seg_ovr = reg;
}

/* return effective address from mrr - also set seg_reg */

uint32 get_ea(uint32 mrr)
{
    uint32 MOD, REG, RM, DISP, EA;

    get_mrr_dec(mrr, &MOD, &REG, &RM);
    switch(MOD) {
        case 0:             /* DISP = 0 */
            DISP = 0;
            switch(RM) {
                case 0:
                    EA = BX + SI;
                    set_segreg(SEG_DS);
                    break;
                case 1:
                    EA = BX + DI; 
                    set_segreg(SEG_DS);
                    break;
                case 2:
                    EA = BP + SI;
                    set_segreg(SEG_SS);
                    break;
                case 3:
                    EA = BP + DI;
                    set_segreg(SEG_SS);
                    break;
                case 4:
                    EA = SI;
                    set_segreg(SEG_DS);
                    break;
                case 5:
                    EA = DI;
                    set_segreg(SEG_ES);
                    break;
                case 6:
                    DISP = fetch_word();
                    EA = DISP;
                    set_segreg(SEG_DS);
                    break;
                case 7:
                    EA = BX;
                    set_segreg(SEG_DS);
                    break;
            }
            break;
        case 1:             /* DISP is byte */
            DISP = fetch_byte(1);
            switch(RM) {
                case 0:
                    EA = BX + SI + DISP;
                    set_segreg(SEG_DS);
                    break;
                case 1:
                    EA = BX + DI + DISP; 
                    set_segreg(SEG_DS);
                    break;
                case 2:
                    EA = BP + SI + DISP;
                    set_segreg(SEG_SS);
                    break;
                case 3:
                    EA = BP + DI + DISP;
                    set_segreg(SEG_SS);
                    break;
                case 4:
                    EA = SI + DISP;
                    set_segreg(SEG_DS);
                    break;
                case 5:
                    EA = DI + DISP;
                    set_segreg(SEG_ES);
                    break;
                case 6:
                    EA = BP + DISP;
                    set_segreg(SEG_SS);
                    break;
                case 7:
                    EA = BX + DISP;
                    set_segreg(SEG_DS);
                    break;
            }
            break;
        case 2:             /* DISP is word */
            DISP = fetch_word();
            switch(RM) {
                case 0:
                    EA = BX + SI + DISP;
                    set_segreg(SEG_DS);
                    break;
                case 1:
                    EA = BX + DI + DISP; 
                    set_segreg(SEG_DS);
                    break;
                case 2:
                    EA = BP + SI + DISP;
                    set_segreg(SEG_SS);
                    break;
                case 3:
                    EA = BP + DI + DISP;
                    set_segreg(SEG_SS);
                    break;
                case 4:
                    EA = SI + DISP;
                    set_segreg(SEG_DS);
                    break;
                case 5:
                    EA = DI + DISP;
                    set_segreg(SEG_ES);
                    break;
                case 6:
                    EA = BP + DISP;
                    set_segreg(SEG_SS);
                    break;
                case 7:
                    EA = BX + DISP;
                    set_segreg(SEG_SS);
                    break;
            }
            break;
        case 3:             /* RM is register field */
            break;
    }
    if (i8088_dev.dctrl & DEBUG_level1) 
        sim_printf("get_ea: MRR=%02X MOD=%02X REG=%02X R/M=%02X DISP=%04X EA=%04X\n",
            mrr, MOD, REG, RM, DISP, EA);
    return EA;
}
/*  return mod, reg and rm field from mrr */

void get_mrr_dec(uint32 mrr, uint32 *mod, uint32 *reg, uint32 *rm)
{
    *mod = (mrr >> 6) & 0x3;
    *reg = (mrr >> 3) & 0x7;
    *rm = mrr & 0x7;
}

/* 
  Most of the primitive algorythms were pulled from the GDE Dos/IP Emulator by Jim Hudgens
*/

/* aad primitive */
uint8 aad_word(uint16 d)
{
    uint16 VAL;
    uint8  HI, LOW;

    HI = (d >> 8) & 0xFF;
    LOW = d & 0xFF;
    VAL = LOW + 10 * HI;
    CONDITIONAL_SET_FLAG(VAL & 0x80, SF);
    CONDITIONAL_SET_FLAG(VAL == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(VAL & 0xFF), PF);
    return (uint8) VAL;
}

/* aam primitive */
uint16 aam_word(uint8 d)
{
    uint16 VAL, HI;

    HI = d / 10;
    VAL = d % 10;
    VAL |= (HI << 8);
    CONDITIONAL_SET_FLAG(VAL & 0x80, SF);
    CONDITIONAL_SET_FLAG(VAL == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(VAL & 0xFF), PF);
    return VAL;
}

/* add with carry byte primitive */
uint8 adc_byte(uint8 d, uint8 s)
{
    register uint16 res;
    register uint16 cc;

    if (GET_FLAG(CF))
        res = 1 + d + s;
    else
        res =  d + s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x0100, CF);
    CONDITIONAL_SET_FLAG((res & 0xff) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(parity(res & 0xff), PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    /* set the flags based on the carry chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(cc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(cc & 0x08, AF);
    return (uint8) res;
}

/* add with carry word primitive */
uint16 adc_word(uint16 d, uint16 s)
{
    register uint32 res;
    register uint32 cc;

    if (GET_FLAG(CF))
        res = 1 + d + s;
    else
        res =  d + s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x10000, CF);
    CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    /* set the flags based on the carry chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(cc >> 14) & 0x3], OF);
    CONDITIONAL_SET_FLAG(cc & 0x08, AF);
    return res;
}

/* add byte primitive */
uint8 add_byte(uint8 d, uint8 s)
{
    register uint16 res;
    register uint16 cc;

    res = d + s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x0100, CF);
    CONDITIONAL_SET_FLAG((res & 0xFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    /* set the flags based on the carry chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(cc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(cc & 0x08, AF);
    return (uint8) res;
}

/* add word primitive */
uint16 add_word(uint16 d, uint16 s)
{
    register uint32 res;
    register uint32 cc;

    res = d + s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x10000, CF);
    CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(parity(res & 0xff), PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (s & d) | ((~res) & (s | d));
    /* set the flags based on the carry chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(cc >> 14) & 0x3], OF);
    CONDITIONAL_SET_FLAG(cc & 0x08, AF);
    return res;
}

/* and byte primitive */
uint8 and_byte(uint8 d, uint8 s)
{
    register uint8 res;
    res = d & s;

    /* clear flags  */
    CLR_FLAG(OF);
    CLR_FLAG(CF);
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res), PF);
    return res;
}

/* and word primitive */
uint16 and_word(uint16 d, uint16 s)
{
    register uint16 res;
    res = d & s;

    /* clear flags  */
    CLR_FLAG(OF);
    CLR_FLAG(CF);
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    return res;
}

/* cmp byte primitive */
uint8 cmp_byte(uint8 d, uint8 s)
{
    register uint32 res;
    register uint32 bc;

    res = d - s;
    /* clear flags  */
    CLR_FLAG(CF);
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG((res & 0xFF)==0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(bc & 0x80, CF);
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
    return d;  /* long story why this is needed.  Look at opcode
          0x80 in ops.c, for an idea why this is necessary.*/
}

/* cmp word primitive */
uint16 cmp_word(uint16 d, uint16 s)
{
    register uint32 res; 
    register uint32 bc;

    res = d - s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the borrow chain.  See note at top */
    bc = (res & (~d | s)) | (~d  &s);
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(bc & 0x8000, CF);
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 14) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
    return d;  /* long story why this is needed.  Look at opcode
          0x80 in ops.c, for an idea why this is necessary.*/
}

/* dec byte primitive */
uint8 dec_byte(uint8 d)
{
    register uint32 res;
    register uint32 bc;

    res = d - 1;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG((res & 0xff)==0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xff), PF);
    /* calculate the borrow chain.  See note at top */
    /* based on sub_byte, uses s=1.  */
    bc = (res & (~d | 1)) | (~d & 1);
    /* carry flag unchanged */
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
    return res;
}

/* dec word primitive */
uint16 dec_word(uint16 d)
{
    register uint32 res;
    register uint32 bc;

    res = d - 1;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG((res & 0xffff) == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xff), PF);
    /* calculate the borrow chain.  See note at top */
    /* based on the sub_byte routine, with s==1 */
    bc = (res & (~d | 1)) | (~d & 1);
    /* carry flag unchanged */
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 14) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
    return res;
}

/* div byte primitive */
void div_byte(uint8 s)
{
    uint32 dvd, dvs, div, mod;

    dvs = s;
    dvd =  AX;
    if (s == 0) {
       i86_intr_raise(0);
       return;
    }
    div = dvd / dvs;
    mod = dvd % dvs;
    if (abs(div) > 0xFF) {
       i86_intr_raise(0);
       return;
    }
    /* Undef --- Can't hurt */
    CLR_FLAG(SF);
    CONDITIONAL_SET_FLAG(div == 0, ZF);
    AL = (uint8)div;
    AH = (uint8)mod;
}

/* div word primitive */
void div_word(uint16 s)
{
    uint32 dvd, dvs, div, mod;

    dvd = DX;
    dvd = (dvd << 16) | AX;
    dvs = s;
    if (dvs == 0) {
       i86_intr_raise(0);
       return;
    }
    div = dvd / dvs;
    mod = dvd % dvs;
    if (abs(div) > 0xFFFF) {
       i86_intr_raise(0);
       return;
    }
    /* Undef --- Can't hurt */
    CLR_FLAG(SF);
    CONDITIONAL_SET_FLAG(div == 0, ZF);
    AX = div;
    DX = mod;
}

/* idiv byte primitive */
void idiv_byte(uint8 s)
{
    int32 dvd, div, mod;

    dvd = (int16)AX;
    if (s == 0) {
       i86_intr_raise(0);
       return;
    }
    div = dvd / (int8)s;
    mod = dvd % (int8)s;
    if (abs(div) > 0x7F) {
       i86_intr_raise(0);
       return;
    }
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(div & 0x80, SF);
    CONDITIONAL_SET_FLAG(div == 0, ZF);
    AL = (int8)div;
    AH = (int8)mod;
}

/* idiv word primitive */
void idiv_word(uint16 s)
{
    int32 dvd, dvs, div, mod;

    dvd = DX;
    dvd = (dvd << 16) | AX;
    if (s == 0) {
       i86_intr_raise(0);
       return;
    }
    dvs = (int16)s;
    div = dvd / dvs;
    mod = dvd % dvs;
    if (abs(div) > 0x7FFF) {
       i86_intr_raise(0);
       return;
    }
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(div & 0x8000, SF);
    CONDITIONAL_SET_FLAG(div == 0, ZF);
    AX = div;
    DX = mod;
}

/* imul byte primitive */
void imul_byte(uint8 s)
{
    int16 res;

    res = (int8)AL * (int8)s;
    AX = res;
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    if ( AH == 0 ||  AH == 0xFF) {
       CLR_FLAG(CF);
       CLR_FLAG(OF);
    } else {
       SET_FLAG(CF);
       SET_FLAG(OF);
    }
}

/* imul word primitive */
void imul_word(uint16 s)
{
    int32 res;

    res = (int16)AX * (int16)s;
    AX = res & 0xFFFF;
    DX = (res >> 16) & 0xFFFF;
    /* Undef --- Can't hurt */
    CONDITIONAL_SET_FLAG(res & 0x80000000, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    if (DX == 0 || DX == 0xFFFF) {
       CLR_FLAG(CF);
       CLR_FLAG(OF);
    } else {
       SET_FLAG(CF);
       SET_FLAG(OF);
    }
}

/* inc byte primitive */
uint8 inc_byte(uint8 d)
{
    register uint32 res;
    register uint32 cc;

    res = d + 1;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG((res & 0xFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  ((1 & d) | (~res)) & (1 | d);
    /* set the flags based on the carry chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(cc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(cc & 0x8, AF);
    return  res;
}

/* inc word primitive */
uint16 inc_word(uint16 d)
{
    register uint32 res;
    register uint32 cc;

    res = d + 1;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000,  SF);
    CONDITIONAL_SET_FLAG(parity(res & 0xff), PF);
    /* calculate the carry chain  SEE NOTE AT TOP.*/
    cc =  (1 & d) | ((~res) & (1 | d));
    /* set the flags based on the carry chain */
    CONDITIONAL_SET_FLAG(xor_3_tab[(cc >> 14) & 0x3], OF);
    CONDITIONAL_SET_FLAG(cc & 0x8, AF);
    return res ;
}

/* mul byte primitive */
void mul_byte(uint8 s)
{
    uint16 res;

    res = AL * s;
    AX = res;
    /* Undef --- Can't hurt */
    CLR_FLAG(SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    if (AH == 0) {
       CLR_FLAG(CF);
       CLR_FLAG(OF);
    } else {
       SET_FLAG(CF);
       SET_FLAG(OF);
    }
}

/* mul word primitive */
void mul_word(uint16 s)
{
    uint32 res;

    res = AX * s;
    /* Undef --- Can't hurt */
    CLR_FLAG(SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    AX = res & 0xFFFF;
    DX = (res >> 16) & 0xFFFF;
    if (DX == 0) {
       CLR_FLAG(CF);
       CLR_FLAG(OF);
    } else {
       SET_FLAG(CF);
       SET_FLAG(OF);
    }
}

/* neg byte primitive */
uint8 neg_byte(uint8 s)
{
    register uint8 res;
    register uint8 bc;

    CONDITIONAL_SET_FLAG(s != 0, CF);
    res = -s;
    CONDITIONAL_SET_FLAG((res & 0xff) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(parity(res), PF);
    /* calculate the borrow chain --- modified such that d=0. */
    bc= res | s;
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
    return res;
}

/* neg word primitive */
uint16 neg_word(uint16 s)
{
    register uint16 res;
    register uint16 bc;

    CONDITIONAL_SET_FLAG(s != 0, CF);
    res = -s;
    CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the borrow chain --- modified such that d=0 */
    bc= res | s;
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
    return res;
}

/* not byte primitive */
uint8 not_byte(uint8 s)
{
    return ~s;
}

/* not word primitive */
uint16 not_word(uint16 s)
{
    return ~s;
}

/* or byte primitive */
uint8 or_byte(uint8 d, uint8 s)
{
    register uint8 res;

    res = d | s;
    /* clear flags  */
    CLR_FLAG(OF);
    CLR_FLAG(CF);
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res), PF);
    return res;
}

/* or word primitive */
uint16 or_word(uint16 d, uint16 s)
{
    register uint16 res;

    res = d | s;
    /* clear flags  */
    CLR_FLAG(OF);
    CLR_FLAG(CF);
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    return res;
}

/* push word primitive */
void push_word(uint16 val)
{
    SP--;
    put_smbyte(SS, SP, val >> 8);
    SP--;
    put_smbyte(SS, SP, val & 0xFF);
}

/* pop word primitive */
uint16 pop_word(void)
{
    register uint16 res;

    res = get_smbyte(SS, SP);
    SP++;
    res |= (get_smbyte(SS, SP) << 8);
    SP++;
    return res;
}

/* rcl byte primitive */
uint8 rcl_byte(uint8 d, uint8 s)
{
    register uint32  res, cnt, mask, cf;

    res = d;
    if ((cnt = s % 9)) 
    {
        cf =  (d >> (8-cnt)) & 0x1;
        res = (d << cnt) & 0xFF;
        mask = (1<<(cnt-1)) - 1;
        res |= (d >> (9-cnt)) & mask;
        if (GET_FLAG(CF))
            res |=  1 << (cnt-1);
        CONDITIONAL_SET_FLAG(cf, CF);
        CONDITIONAL_SET_FLAG(cnt == 1 && xor_3_tab[cf + ((res >> 6) & 0x2)], OF);
    }
    return res & 0xFF;
}

/* rcl word primitive */
uint16  rcl_word(uint16 d, uint16 s)
{
    register uint32  res, cnt, mask, cf;

    res = d;
    if ((cnt = s % 17))
    {
        cf =  (d >> (16-cnt)) & 0x1;
        res = (d << cnt) & 0xFFFF;
        mask = (1<<(cnt-1)) - 1;
        res |= (d >> (17-cnt)) & mask;
        if (GET_FLAG(CF))
            res |=  1 << (cnt-1);
        CONDITIONAL_SET_FLAG(cf, CF);
        CONDITIONAL_SET_FLAG(cnt == 1 && xor_3_tab[cf + ((res >> 14) & 0x2)], OF);
    }
    return res & 0xFFFF;
}

/* rcr byte primitive */
uint8 rcr_byte(uint8 d, uint8 s)
{
    uint8 res, cnt;
    uint8 mask, cf, ocf = 0;

    res = d;

    if ((cnt = s % 9)) {
        if (cnt == 1) {
            cf = d & 0x1;
            ocf = GET_FLAG(CF) != 0;
        } else
            cf =  (d >> (cnt - 1)) & 0x1;
        mask = (1 <<( 8 - cnt)) - 1;
        res = (d >> cnt) & mask;
        res |= (d << (9-cnt));
        if (GET_FLAG(CF))
            res |=  1 << (8 - cnt);
        CONDITIONAL_SET_FLAG(cf, CF);
        if (cnt == 1)
            CONDITIONAL_SET_FLAG(xor_3_tab[ocf + ((d >> 6) & 0x2)], OF);
    }
    return res;
}

/* rcr word primitive */
uint16 rcr_word(uint16 d, uint16 s)
{
    uint16 res, cnt;
    uint16 mask, cf, ocf = 0;

    res = d;
    if ((cnt = s % 17)) {
        if (cnt == 1) {
            cf = d & 0x1;
            ocf = GET_FLAG(CF) != 0;
        } else
            cf =  (d >> (cnt-1)) & 0x1;
        mask = (1 <<( 16 - cnt)) - 1;
        res = (d >> cnt) & mask;
        res |= (d << (17 - cnt));
        if (GET_FLAG(CF))
            res |=  1 << (16 - cnt);
        CONDITIONAL_SET_FLAG(cf, CF);
        if (cnt == 1)
            CONDITIONAL_SET_FLAG(xor_3_tab[ocf + ((d >> 14) & 0x2)], OF);
    }
    return res;
}

/* rol byte primitive */
uint8 rol_byte(uint8 d, uint8 s)
{
    register uint32  res, cnt, mask;

    res =d;

    if ((cnt = s % 8)) {
       res = (d << cnt);
       mask = (1 << cnt) - 1;
       res |= (d >> (8-cnt)) & mask;
       CONDITIONAL_SET_FLAG(res & 0x1, CF);
       CONDITIONAL_SET_FLAG(cnt == 1 && xor_3_tab[(res & 0x1) + ((res >> 6) & 0x2)], OF);
    }
    return res & 0xFF;
}

/* rol word primitive */
uint16 rol_word(uint16 d, uint16 s)
{
    register uint32  res, cnt, mask;

    res = d;
    if ((cnt = s % 16)) {
       res = (d << cnt);
       mask = (1 << cnt) - 1;
       res |= (d >> (16 - cnt)) & mask;
       CONDITIONAL_SET_FLAG(res & 0x1, CF);
       CONDITIONAL_SET_FLAG(cnt == 1 && xor_3_tab[(res & 0x1) + ((res >> 14) & 0x2)], OF);
    }
    return res&0xFFFF;
}

/* ror byte primitive */
uint8 ror_byte(uint8 d, uint8 s)
{
    register uint32  res, cnt, mask;

    res = d;
    if ((cnt = s % 8)) {
       res = (d << (8-cnt));
       mask = (1 << (8-cnt)) - 1;
       res |= (d >> (cnt)) & mask;
       CONDITIONAL_SET_FLAG(res & 0x80, CF);
       CONDITIONAL_SET_FLAG(cnt == 1 && xor_3_tab[(res >> 6) & 0x3], OF);
    }
    return res & 0xFF;
}

/* ror word primitive */
uint16 ror_word(uint16 d, uint16 s)
{
    register uint32  res, cnt, mask;

    res = d;
    if ((cnt = s % 16)) {
       res = (d << (16-cnt));
       mask = (1 << (16-cnt)) - 1;
       res |= (d >> (cnt)) & mask;
       CONDITIONAL_SET_FLAG(res & 0x8000, CF);
       CONDITIONAL_SET_FLAG(cnt == 1  && xor_3_tab[(res >> 14) & 0x3], OF);
    }
    return res & 0xFFFF;
}

/* shl byte primitive */
uint8 shl_byte(uint8 d, uint8 s)
{
    uint32 cnt, res, cf;

    if (s < 8) {
        cnt = s % 8;
        if (cnt > 0) {
            res = d << cnt;
            cf = d & (1 << (8 - cnt));
            CONDITIONAL_SET_FLAG(cf, CF);
            CONDITIONAL_SET_FLAG((res & 0xFF)==0, ZF);
            CONDITIONAL_SET_FLAG(res & 0x80, SF);
            CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
        } else
            res = (uint8) d;
        if (cnt == 1)
            CONDITIONAL_SET_FLAG((((res & 0x80) == 0x80) ^ 
                (GET_FLAG( CF) != 0)), OF);
        else
            CLR_FLAG(OF);
    } else {
        res = 0;
        CONDITIONAL_SET_FLAG((s == 8) && (d & 1), CF);
        CLR_FLAG(OF);
        CLR_FLAG(SF);
        CLR_FLAG(PF);
        SET_FLAG(ZF);
    }
    return res & 0xFF;
}

/* shl word primitive */
uint16 shl_word(uint16 d, uint16 s)
{
    uint32 cnt, res, cf;

    if (s < 16) {
        cnt = s % 16;
        if (cnt > 0) {
            res = d << cnt;
            cf = d & (1<<(16-cnt));
            CONDITIONAL_SET_FLAG(cf, CF);
            CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
            CONDITIONAL_SET_FLAG(res & 0x8000, SF);
            CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
        } else
            res = (uint16) d;
        if (cnt == 1)
            CONDITIONAL_SET_FLAG((((res & 0x8000) == 0x8000) ^
            (GET_FLAG(CF) != 0)), OF);
        else
            CLR_FLAG(OF);
    } else {
        res = 0;
        CONDITIONAL_SET_FLAG((s == 16) && (d & 1), CF);
        CLR_FLAG(OF);
        SET_FLAG(ZF);
        CLR_FLAG(SF);
        CLR_FLAG(PF);
    }
    return res & 0xFFFF;
}

/* shr byte primitive */
uint8 shr_byte(uint8 d, uint8 s)
{
    uint32 cnt, res, cf, mask;

    if (s < 8) {
        cnt = s % 8;
        if (cnt > 0) {
            mask = (1 << (8 - cnt)) - 1;
            cf = d & (1 << (cnt - 1));
            res = (d >> cnt) & mask;
            CONDITIONAL_SET_FLAG(cf, CF);
            CONDITIONAL_SET_FLAG((res & 0xFF) == 0, ZF);
            CONDITIONAL_SET_FLAG(res & 0x80, SF);
            CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
        } else
            res = (uint8) d;
        if (cnt == 1)
            CONDITIONAL_SET_FLAG(xor_3_tab[(res >> 6) & 0x3], OF);
        else
            CLR_FLAG(OF);
    } else {
        res = 0;
        CONDITIONAL_SET_FLAG((s == 8) && (d & 0x80), CF);
        CLR_FLAG(OF);
        SET_FLAG(ZF);
        CLR_FLAG(SF);
        CLR_FLAG(PF);
    }
    return res & 0xFF;
}

/* shr word primitive */
uint16 shr_word(uint16 d, uint16 s)
{
    uint32 cnt, res, cf, mask;

    res = d;
    if (s < 16) {
        cnt = s % 16;
        if (cnt > 0) {
            mask = (1 << (16 - cnt)) - 1;
            cf = d & (1 << (cnt - 1));
            res = (d >> cnt) & mask;
            CONDITIONAL_SET_FLAG(cf, CF);
            CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
            CONDITIONAL_SET_FLAG(res & 0x8000, SF);
            CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
        } else
            res = d;
        if (cnt == 1)
            CONDITIONAL_SET_FLAG(xor_3_tab[(res >> 14) & 0x3], OF);
        else
             CLR_FLAG(OF);
    } else {
        res = 0;
        CONDITIONAL_SET_FLAG((s == 16) && (d & 0x8000), CF);
        CLR_FLAG(OF);
        SET_FLAG(ZF);
        CLR_FLAG(SF);
        CLR_FLAG(PF);
    }
    return res & 0xFFFF;
}

/* sar byte primitive */
uint8 sar_byte(uint8 d, uint8 s)
{
    uint32 cnt, res, cf, mask, sf;

    res = d;
    sf = d & 0x80;
    cnt = s % 8;
    if (cnt > 0 && cnt < 8) {
        mask = (1 << (8 - cnt)) - 1;
        cf = d & (1 << (cnt -1 ));
        res = (d >> cnt) & mask;
        CONDITIONAL_SET_FLAG(cf, CF);
        if (sf)
             res |= ~mask;
        CONDITIONAL_SET_FLAG((res & 0xFF)==0, ZF);
        CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
        CONDITIONAL_SET_FLAG(res & 0x80, SF);
    } else if (cnt >= 8) {
        if (sf) {
            res = 0xFF;
            SET_FLAG(CF);
            CLR_FLAG(ZF);
            SET_FLAG(SF);
            SET_FLAG(PF);
        } else {
            res = 0;
            CLR_FLAG(CF);
            SET_FLAG(ZF);
            CLR_FLAG(SF);
            CLR_FLAG(PF);
        }
    }
    return res & 0xFF;
}

/* sar word primitive */
uint16 sar_word(uint16 d, uint16 s)
{
    uint32 cnt, res, cf, mask, sf;

    sf = d & 0x8000;
    cnt = s % 16;
    res = d;
    if (cnt > 0 && cnt < 16) {
        mask = (1 << (16 - cnt)) - 1;
        cf = d & (1 << (cnt - 1));
        res = (d >> cnt) & mask;
        CONDITIONAL_SET_FLAG(cf, CF);
        if (sf)
            res |= ~mask;
        CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
        CONDITIONAL_SET_FLAG(res & 0x8000, SF);
        CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    } else if (cnt >= 16) {
        if (sf) {
            res = 0xFFFF;
            SET_FLAG(CF);
            CLR_FLAG(ZF);
            SET_FLAG(SF);
            SET_FLAG(PF);
        } else {
            res = 0;
            CLR_FLAG(CF);
            SET_FLAG(ZF);
            CLR_FLAG(SF);
            CLR_FLAG(PF);
        }
    }
    return res & 0xFFFF;
}

/* sbb byte primitive */
uint8 sbb_byte(uint8 d, uint8 s)
{
    register uint32 res;
    register uint32 bc;

    if (GET_FLAG(CF))
        res = d - s - 1;
    else
        res =  d - s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG((res & 0xFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(bc & 0x80, CF);
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
//    return res & 0x0FF;
    return (uint8) res;
}

/* sbb word primitive */
uint16 sbb_word(uint16 d, uint16 s)
{
    register uint32 res;
    register uint32 bc;

    if (GET_FLAG(CF))
        res = d - s - 1;
    else
        res =  d - s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG((res & 0xFFFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(bc & 0x8000, CF);
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 14) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
//    return res & 0xFFFF;
    return (uint16) res;
}

/* sub byte primitive */
uint8 sub_byte(uint8 d, uint8 s)
{
    register uint32 res;
    register uint32 bc;

    res = d - s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG((res & 0xFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(bc & 0x80, CF);
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 6) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
//    return res & 0xff;
    return (uint8) res;
}

/* sub word primitive */
uint16 sub_word(uint16 d, uint16 s)
{
    register uint32 res;
    register uint32 bc;

    res = d - s;
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res  &0x8000, SF);
    CONDITIONAL_SET_FLAG((res  &0xFFFF) == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* calculate the borrow chain.  See note at top */
    bc= (res&(~d|s))|(~d&s);
    /* set flags based on borrow chain */
    CONDITIONAL_SET_FLAG(bc & 0x8000, CF);
    CONDITIONAL_SET_FLAG(xor_3_tab[(bc >> 14) & 0x3], OF);
    CONDITIONAL_SET_FLAG(bc & 0x8, AF);
//    return res & 0xffff;
    return (uint16) res;
}

/* test byte primitive */
void test_byte(uint8 d, uint8 s)
{
    register uint32 res;

    res = d & s;
    CLR_FLAG(OF);
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    /* AF == dont care*/
    CLR_FLAG(CF);
}

/* test word primitive */
void test_word(uint16 d, uint16 s)
{
    register uint32 res;

    res = d & s;
    CLR_FLAG(OF);
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xff), PF);
    /* AF == dont care*/
    CLR_FLAG(CF);
}

/* xor byte primitive */
uint8 xor_byte(uint8 d, uint8 s)
{
    register uint8 res;
    res = d ^ s;

    /* clear flags  */
    CLR_FLAG(OF);
    CLR_FLAG(CF);
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x80, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res), PF);
    return res;
}

/* xor word primitive */
uint16 xor_word(uint16 d, uint16 s)
{
    register uint16 res;

    res = d ^ s;
    /* clear flags  */
    CLR_FLAG(OF);
    CLR_FLAG(CF);
    /* set the flags based on the result */
    CONDITIONAL_SET_FLAG(res & 0x8000, SF);
    CONDITIONAL_SET_FLAG(res == 0, ZF);
    CONDITIONAL_SET_FLAG(parity(res & 0xFF), PF);
    return res;
}

/*  memory routines.  These use the segment register (segreg) value and offset
    (addr) to calculate the proper source or destination memory address */

/*  get a byte from memory */

int32 get_smbyte(int32 segreg, int32 addr)
{
    int32 abs_addr, val;

    abs_addr = addr + (get_rword(segreg) << 4);
    val = get_mbyte(abs_addr);
//    sim_printf("get_smbyte: seg=%04X addr=%04X abs_addr=%08X get_mbyte=%02X\n",
//        get_rword(segreg), addr, abs_addr, val);
    return val;
}

/*  get a word from memory using addr and segment register */

int32 get_smword(int32 segreg, int32 addr)
{
    int32 val;

    val = get_smbyte(segreg, addr);
    val |= (get_smbyte(segreg, addr+1) << 8);
    return val;
}

/*  put a byte to memory using addr and segment register */

void put_smbyte(int32 segreg, int32 addr, int32 val)
{
    int32 abs_addr;

    abs_addr = addr + (get_rword(segreg) << 4);
    put_mbyte(abs_addr, val);
}

/*  put a word to memory using addr and segment register */

void put_smword(int32 segreg, int32 addr, int32 val)
{
    put_smbyte(segreg, addr, val);
    put_smbyte(segreg, addr+1, val << 8);
}

/* Reset routine using addr and segment register */

t_stat i8088_reset (DEVICE *dptr)
{
    PSW = 0;
    CS = 0xFFFF;
    DS = 0;
    SS = 0;
    ES = 0;
    saved_PC = 0;
    int_req = 0;
    sim_brk_types = sim_brk_dflt = SWMASK ('E');
    sim_printf("   8088 Reset\n");
    return SCPE_OK;
}

/* Memory examine */

t_stat i8088_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE20) 
        return SCPE_NXM;
    if (vptr != NULL) 
        *vptr = get_mbyte(addr);
    return SCPE_OK;
}

/* Memory deposit */

t_stat i8088_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE20) 
        return SCPE_NXM;
    put_mbyte(addr, val);
    return SCPE_OK;
}

/* This is the binary loader.  The input file is considered to be
   a string of literal bytes with no special format. The load 
   starts at the current value of the PC.
*/

int32 sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
    int32 i, addr = 0, cnt = 0;

    if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
    addr = saved_PC;
    while ((i = getc (fileref)) != EOF) {
        put_mbyte(addr, i);
        addr++;
        cnt++;
    }                                                       /* end while */
    sim_printf ("%d Bytes loaded.\n", cnt);
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

int32 fprint_sym (FILE *of, int32 addr, uint32 *val,
    UNIT *uptr, int32 sw)
{
    int32 cflag, c1, c2, inst, adr;

    cflag = (uptr == NULL) || (uptr == &i8088_unit);
    c1 = (val[0] >> 8) & 0177;
    c2 = val[0] & 0177;
    if (sw & SWMASK ('A')) {
        fprintf (of, (c2 < 040)? "<%02X>": "%c", c2);
        return SCPE_OK;
    }
    if (sw & SWMASK ('C')) {
        fprintf (of, (c1 < 040)? "<%02X>": "%c", c1);
        fprintf (of, (c2 < 040)? "<%02X>": "%c", c2);
        return SCPE_OK;
    }
    if (!(sw & SWMASK ('M'))) return SCPE_ARG;
    inst = val[0];
    fprintf (of, "%s", opcode[inst]);
    if (oplen[inst] == 2) {
        if (strchr(opcode[inst], ' ') != NULL)
            fprintf (of, ",");
        else fprintf (of, " ");
        fprintf (of, "%h", val[1]);
    }
    if (oplen[inst] == 3) {
        adr = val[1] & 0xFF;
        adr |= (val[2] << 8) & 0xff00;
        if (strchr(opcode[inst], ' ') != NULL)
            fprintf (of, ",");
        else fprintf (of, " ");
        fprintf (of, "%h", adr);
    }
    return -(oplen[inst] - 1);
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

int32 parse_sym (char *cptr, int32 addr, UNIT *uptr, uint32 *val, int32 sw)
{
    int32 cflag, i = 0, j, r;
    char gbuf[CBUFSIZE];

    cflag = (uptr == NULL) || (uptr == &i8088_unit);
    while (isspace (*cptr)) cptr++;                         /* absorb spaces */
    if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
        if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
        val[0] = (uint32) cptr[0];
        return SCPE_OK;
    }
    if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
        if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
        val[0] = ((uint32) cptr[0] << 8) + (uint32) cptr[1];
        return SCPE_OK;
    }

/* An instruction: get opcode (all characters until null, comma,
   or numeric (including spaces).
*/

    while (1) {
        if (*cptr == ',' || *cptr == '\0' ||
             isdigit(*cptr))
                break;
        gbuf[i] = toupper(*cptr);
        cptr++;
        i++;
    }

/* Allow for RST which has numeric as part of opcode */

    if (toupper(gbuf[0]) == 'R' &&
        toupper(gbuf[1]) == 'S' &&
        toupper(gbuf[2]) == 'T') {
        gbuf[i] = toupper(*cptr);
        cptr++;
        i++;
    }

/* Allow for 'MOV' which is only opcode that has comma in it. */

    if (toupper(gbuf[0]) == 'M' &&
        toupper(gbuf[1]) == 'O' &&
        toupper(gbuf[2]) == 'V') {
        gbuf[i] = toupper(*cptr);
        cptr++;
        i++;
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
    for (j = 0; j < 256; j++) {
        if (strcmp(gbuf, opcode[j]) == 0)
            break;
    }
    if (j > 255)                                            /* not found */
        return SCPE_ARG;

    val[0] = j;                                             /* store opcode */
    if (oplen[j] < 2)                                       /* if 1-byter we are done */
        return SCPE_OK;
    if (*cptr == ',') cptr++;
    cptr = get_glyph(cptr, gbuf, 0);                        /* get address */
    sscanf(gbuf, "%o", &r);
    if (oplen[j] == 2) {
        val[1] = r & 0xFF;
        return (-1);
    }
    val[1] = r & 0xFF;
    val[2] = (r >> 8) & 0xFF;
    return (-2);
}
