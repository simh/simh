/* i8080.c: Intel 8080/8085 CPU simulator

   Copyright (c) 1997-2005, Charles E. Owen

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
   CHARLES E. OWEN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

   This software was modified by Bill Beech, Nov 2010, to allow emulation of Intel
   iSBC Single Board Computers.

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

   cpu          8080 CPU

   08 Oct 02    RMS     Tied off spurious compiler warnings
   23 Nov 10    WAB     Modified for iSBC emulation
   04 Dec 12    WAB     Added 8080 interrupts
   14 Dec 12    WAB     Added 8085 interrupts

   The register state for the 8080 CPU is:

   A<0:7>               Accumulator
   BC<0:15>             BC Register Pair
   DE<0:15>             DE Register Pair
   HL<0:15>             HL Register Pair 
   PSW<0:7>             Program Status Word (Flags)
   PC<0:15>             Program counter
   SP<0:15>             Stack Pointer

   The 8080 is an 8-bit CPU, which uses 16-bit registers to address
   up to 64KB of memory.

   The 78 basic instructions come in 1, 2, and 3-byte flavors.

   This routine is the instruction decode routine for the 8080.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        I/O error in I/O simulator
        Invalid OP code (if ITRAP is set on CPU)

   2. Interrupts.
      There are 8 possible levels of interrupt, and in effect they
      do a hardware CALL instruction to one of 8 possible low
      memory addresses.

   3. Non-existent memory.  On the 8080, reads to non-existent memory
      return 0FFh, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:
      i8080.c - add I/O service routines to dev_table
      isys80XX_sys.c - add pointer to data structures in sim_devices
      system_defs.h - to define devices and addresses assigned to devices

    ?? ??? 11 - Original file.
    16 Dec 12 - Modified to use isbc_80_10.cfg file to set base and size.
    20 Dec 12 - Modified for basic interrupt function.
    03 Mar 13 - Added trace function.
    04 Mar 13 - Modified all instructions to truncate the affected register
                at the end of the routine.
    17 Mar 13 - Modified to enable/disable trace based on start and stop
                addresses.
*/

#include "system_defs.h"

#define UNIT_V_OPSTOP   (UNIT_V_UF)     /* Stop on Invalid OP? */
#define UNIT_OPSTOP     (1 << UNIT_V_OPSTOP)
#define UNIT_V_8085     (UNIT_V_UF+1)   /* 8080/8085 switch */
#define UNIT_8085       (1 << UNIT_V_8085)
#define UNIT_V_TRACE    (UNIT_V_UF+2)   /*  Trace switch */
#define UNIT_TRACE      (1 << UNIT_V_TRACE)
#define UNIT_V_XACK     (UNIT_V_UF+3)   /*  XACK switch */
#define UNIT_XACK       (1 << UNIT_V_XACK)

/* Flag values to set proper positions in PSW */
#define CF      0x01
#define PF      0x04
#define AF      0x10
#define ZF      0x40
#define SF      0x80

/* Macros to handle the flags in the PSW 
    8080 has bit #1 always set.  This is (not well) documented  behavior. */
#define PSW_ALWAYS_ON       (0x02)        /* for 8080 */
#define PSW_MSK (CF|PF|AF|ZF|SF)
#define TOGGLE_FLAG(FLAG)   (PSW ^= FLAG)
#define SET_FLAG(FLAG)      (PSW |= FLAG)
#define CLR_FLAG(FLAG)      (PSW &= ~FLAG)
#define GET_FLAG(FLAG)      (PSW & FLAG)
#define COND_SET_FLAG(COND,FLAG) \
    if (COND) SET_FLAG(FLAG); else CLR_FLAG(FLAG)

#define SET_XACK(VAL)       (xack = VAL)
#define GET_XACK(FLAG)      (xack &= FLAG)

/* values for IM bits */
#define ITRAP   0x100
#define SID     0x80
#define SOD     0x80
#define SDE     0x40
#define R75     0x10
#define IE      0x08
#define MSE     0x08
#define M75     0x04
#define M65     0x02
#define M55     0x01

/* register masks */
#define BYTE_R  0xFF
#define WORD_R  0xFFFF

/* storage for the rest of the registers */
uint32 PSW = 0;                         /* program status word */
uint32 A = 0;                           /* accumulator */
uint32 BC = 0;                          /* BC register pair */
uint32 DE = 0;                          /* DE register pair */
uint32 HL = 0;                          /* HL register pair */
uint32 SP = 0;                          /* Stack pointer */
uint32 saved_PC = 0;                    /* program counter */
uint32 IM = 0;                          /* Interrupt Mask Register */
uint8  xack = 0;                        /* XACK signal */
uint32 int_req = 0;                     /* Interrupt request */
uint8 INTA = 0;                         // interrupt acknowledge
uint16 PCX;                              /* External view of PC */
uint16 PCY;                              /* Internal view of PC */
uint16 PC;
UNIT *uptr;
uint16 port;                            //port used in any IN/OUT
uint16 addr;                            //addr used for operand fetch
uint32 IR;
uint16 devnum = 0;

/* function prototypes */

void    set_cpuint(int32 int_num);
void    dumpregs(void);
int32   fetch_byte(int32 flag);
int32   fetch_word(void);
uint16  pop_word(void);
void    push_word(uint16 val);
void    setarith(int32 reg);
void    setlogical(int32 reg);
void    setinc(int32 reg);
int32   getreg(int32 reg);
void    putreg(int32 reg, int32 val);
int32   getpair(int32 reg);
int32   getpush(int32 reg);
void    putpush(int32 reg, int32 data);
void    putpair(int32 reg, int32 val);
void    parity(int32 reg);
int32   cond(int32 con);
t_stat  i8080_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat  i8080_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat  i8080_reset (DEVICE *dptr);

/* external function prototypes */

extern t_stat i8080_reset (DEVICE *dptr);
extern uint8 get_mbyte(uint16 addr);
extern uint16 get_mword(uint16 addr);
extern void put_mbyte(uint16 addr, uint8 val);
extern void put_mword(uint16 addr, uint16 val);
extern int32 sim_int_char;
extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */

struct idev {
    uint8 (*routine)(t_bool, uint8, uint8);
    uint8 port;
    uint8 devnum;
};

/* This is the I/O configuration table.  There are 256 possible
device addresses, if a device is plugged to a port it's routine
address is here, 'nulldev' means no device is available
*/

extern struct idev dev_table[];

/* CPU data structures
   i8080_dev      CPU device descriptor
   i8080_unit     CPU unit descriptor
   i8080_reg      CPU register list
   i8080_mod      CPU modifiers list
*/

UNIT i8080_unit = { UDATA (NULL, 0, 65535) }; /* default 8080 */

REG i8080_reg[] = {
    { HRDATA (PC, saved_PC, 16) },      /* must be first for sim_PC */
    { HRDATA (PSW, PSW, 8) },
    { HRDATA (A, A, 8) },
    { HRDATA (BC, BC, 16) },
    { HRDATA (DE, DE, 16) },
    { HRDATA (HL, HL, 16) },
    { HRDATA (SP, SP, 16) },
    { HRDATA (IM, IM, 8) },
    { HRDATA (XACK, xack, 8) },
    { HRDATA (INTR, int_req, 32) },
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
};

MTAB i8080_mod[] = {
    { UNIT_8085, 0, "8080", "8080", NULL },
    { UNIT_8085, UNIT_8085, "8085", "8085", NULL },
    { UNIT_OPSTOP, 0, "ITRAP", "ITRAP", NULL },
    { UNIT_OPSTOP, UNIT_OPSTOP, "NOITRAP", "NOITRAP", NULL },
    { UNIT_TRACE, 0, "NOTRACE", "NOTRACE", NULL },
    { UNIT_TRACE, UNIT_TRACE, "TRACE", "TRACE", NULL },
    { UNIT_XACK, 0, "NOXACK", "NOXACK", NULL },
    { UNIT_XACK, UNIT_XACK, "XACK", "XACK", NULL },
    { 0 }
};

DEBTAB i8080_debug[] = {
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

DEVICE i8080_dev = {
    "I8080",                            //name
    &i8080_unit,                        //units
    i8080_reg,                          //registers
    i8080_mod,                          //modifiers
    1,                                  //numunits
    16,                                 //aradix 
    16,                                 //awidth 
    1,                                  //aincr 
    16,                                 //dradix 
    8,                                  //dwidth
    &i8080_ex,                          //examine 
    &i8080_dep,                         //deposit 
    NULL,                               //reset
    NULL,                               //boot
    NULL,                               //attach 
    NULL,                               //detach
    NULL,                               //context
    DEV_DEBUG,                          //flags 
    0,                                  //dctrl 
    i8080_debug,                        //debflags
    NULL,                               //msize
    NULL                                //lname
};

/* tables for the disassembler */
const char *opcode[] = {                      
"NOP", "LXI B,", "STAX B", "INX B",             /* 0x00 */
"INR B", "DCR B", "MVI B,", "RLC",
"???", "DAD B", "LDAX B", "DCX B",
"INR C", "DCR C", "MVI C,", "RRC",
"???", "LXI D,", "STAX D", "INX D",             /* 0x10 */
"INR D", "DCR D", "MVI D,", "RAL",
"???", "DAD D", "LDAX D", "DCX D",
"INR E", "DCR E", "MVI E,", "RAR",
"RIM", "LXI H,", "SHLD ", "INX H",              /* 0x20 */
"INR H", "DCR H", "MVI H,", "DAA",
"???", "DAD H", "LHLD ", "DCX H",
"INR L", "DCR L", "MVI L", "CMA",
"SIM", "LXI SP,", "STA ", "INX SP",             /* 0x30 */
"INR M", "DCR M", "MVI M,", "STC",
"???", "DAD SP", "LDA ", "DCX SP",
"INR A", "DCR A", "MVI A,", "CMC",
"MOV B,B", "MOV B,C", "MOV B,D", "MOV B,E",     /* 0x40 */
"MOV B,H", "MOV B,L", "MOV B,M", "MOV B,A",
"MOV C,B", "MOV C,C", "MOV C,D", "MOV C,E",
"MOV C,H", "MOV C,L", "MOV C,M", "MOV C,A",
"MOV D,B", "MOV D,C", "MOV D,D", "MOV D,E",     /* 0x50 */
"MOV D,H", "MOV D,L", "MOV D,M", "MOV D,A",
"MOV E,B", "MOV E,C", "MOV E,D", "MOV E,E",
"MOV E,H", "MOV E,L", "MOV E,M", "MOV E,A",
"MOV H,B", "MOV H,C", "MOV H,D", "MOV H,E",     /* 0x60 */
"MOV H,H", "MOV H,L", "MOV H,M", "MOV H,A",
"MOV L,B", "MOV L,C", "MOV L,D", "MOV L,E",
"MOV L,H", "MOV L,L", "MOV L,M", "MOV L,A",
"MOV M,B", "MOV M,C", "MOV M,D", "MOV M,E",     /* 0x70 */
"MOV M,H", "MOV M,L", "HLT", "MOV M,A",
"MOV A,B", "MOV A,C", "MOV A,D", "MOV A,E",
"MOV A,H", "MOV A,L", "MOV A,M", "MOV A,A",
"ADD B", "ADD C", "ADD D", "ADD E",             /* 0x80 */
"ADD H", "ADD L", "ADD M", "ADD A",
"ADC B", "ADC C", "ADC D", "ADC E",
"ADC H", "ADC L", "ADC M", "ADC A",
"SUB B", "SUB C", "SUB D", "SUB E",             /* 0x90 */
"SUB H", "SUB L", "SUB M", "SUB A",
"SBB B", "SBB C", "SBB D", "SBB E",
"SBB H", "SBB L", "SBB M", "SBB A",
"ANA B", "ANA C", "ANA D", "ANA E",             /* 0xA0 */
"ANA H", "ANA L", "ANA M", "ANA A",
"XRA B", "XRA C", "XRA D", "XRA E",
"XRA H", "XRA L", "XRA M", "XRA A",
"ORA B", "ORA C", "ORA D", "ORA E",             /* 0xB0 */
"ORA H", "ORA L", "ORA M", "ORA A",
"CMP B", "CMP C", "CMP D", "CMP E",
"CMP H", "CMP L", "CMP M", "CMP A",
"RNZ", "POP B", "JNZ ", "JMP ",                 /* 0xC0 */
"CNZ ", "PUSH B", "ADI ", "RST 0",
"RZ", "RET", "JZ ", "???",
"CZ ", "CALL ", "ACI ", "RST 1",
"RNC", "POP D", "JNC ", "OUT ",                 /* 0xD0 */
"CNC ", "PUSH D", "SUI ", "RST 2",
"RC", "???", "JC ", "IN ",
"CC ", "???", "SBI ", "RST 3",
"RPO", "POP H", "JPO ", "XTHL",                 /* 0xE0 */
"CPO ", "PUSH H", "ANI ", "RST 4",
"RPE", "PCHL", "JPE ", "XCHG",
"CPE ", "???", "XRI ", "RST 5",
"RP", "POP PSW", "JP ", "DI",                   /* 0xF0 */
"CP ", "PUSH PSW", "ORI ", "RST 6",
"RM", "SPHL", "JM ", "EI",
"CM ", "???", "CPI ", "RST 7",
};

int32 oplen[256] = {
1,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,
0,3,1,1,1,1,2,1,0,1,1,1,1,1,2,1,
1,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,
1,3,3,1,1,1,2,1,0,1,3,1,1,1,2,1,
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

void set_cpuint(int32 int_num)
{
    int_req |= int_num;
}


/* instruction simulator */
int32 sim_instr(void)
{
    extern int32 sim_interval;
    uint32 OP, DAR, reason, adr, onetime = 0;

    PC = saved_PC & WORD_R;             /* load local PC */
    reason = 0;

    uptr = i8080_dev.units;

    if (onetime++ == 0) {
        if (uptr->flags & UNIT_8085)
            sim_printf("CPU = 8085\n");
        else
            sim_printf("CPU = 8080\n");
        sim_printf("    i8080:\n");
    }
    
    /* Main instruction fetch/decode loop */

    while (reason == 0) {               /* loop until halted */

        if (i8080_dev.dctrl & DEBUG_reg) {
            dumpregs();
            sim_printf("\n");
        }

        if (sim_interval <= 0) {        /* check clock queue */
            if ((reason = sim_process_event()))
                break;
        }

        if (int_req > 0) {              /* interrupt? */
            if (uptr->flags & UNIT_8085) { /* 8085 */
                if (int_req & ITRAP) {  /* int */
                    push_word(PC);
                    PC = 0x0024;
                    int_req &= ~ITRAP;
                } else if (IM & IE) {
                    if (int_req & I75 && IM & M75) { /* int 7.5 */
                        push_word(PC);
                        PC = 0x003C;
                        int_req &= ~I75;
                    } else if (int_req & I65 && IM & M65) { /* int 6.5 */
                        push_word(PC);
                        PC = 0x0034;
                        int_req &= ~I65;
                    } else if (int_req & I55 && IM & M55) { /* int 5.5 */
                        push_word(PC);
                        PC = 0x002C;
                        int_req &= ~I55;
                    } else if (int_req & INT_R) { /* intr */
                        push_word(PC);      /* do an RST 7 */
                        PC = 0x0038;
                        int_req &= ~INT_R;
                    }
                } 
            } else {                    /* 8080 */
                if (IM & IE) {          /* enabled? */
                    INTA = 1;
                    push_word(PC);      /* do an RST 2 */
                    PC = 0x0010;
                    int_req = 0;
//                    sim_printf("8080 Interrupt\n");
                }
            }
        }                               /* end interrupt */

        if (sim_brk_summ &&
            sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
            reason = STOP_IBKPT;        /* stop simulation */
            break;
        }

        if (uptr->flags & UNIT_TRACE) {
            dumpregs();
//            sim_printf("\n");
        }

        sim_interval--;                 /* countdown clock */
        PCY = PCX = PC;
        
        IR = OP = fetch_byte(0);        /* instruction fetch */

        /*
        if (GET_XACK(1) == 0) {         // no XACK for instruction fetch
            reason = STOP_XACK;
//        if (uptr->flags & UNIT_XACK) 
            sim_printf("Failed XACK for Instruction Fetch from %04X\n", PCX);
            continue;
         }
         */

        // first instruction decode
        if (OP == 0x76) {               /* HLT Instruction*/
            reason = STOP_HALT;
            PC--;
            continue;
        }

        /* Handle below all operations which refer to registers or
          register pairs.  After that, a large switch statement
          takes care of all other opcodes */

        if ((OP & 0xC0) == 0x40) {      /* MOV */
            DAR = getreg(OP & 0x07);
            putreg((OP >> 3) & 0x07, DAR);
            goto loop_end;
        }

        if ((OP & 0xC7) == 0x06) {      /* MVI */
            putreg((OP >> 3) & 0x07, fetch_byte(1));
            goto loop_end;
        }

        if ((OP & 0xCF) == 0x01) {      /* LXI */
            DAR = fetch_word();
            putpair((OP >> 4) & 0x03, DAR);
            goto loop_end;
        }

        if ((OP & 0xEF) == 0x0A) {      /* LDAX */
            DAR = getpair((OP >> 4) & 0x03);
            putreg(7, get_mbyte(DAR));
            goto loop_end;
        }

        if ((OP & 0xEF) == 0x02) {      /* STAX */
            DAR = getpair((OP >> 4) & 0x03);
            put_mbyte(DAR, getreg(7));
            goto loop_end;
        }

        if ((OP & 0xF8) == 0xB8) {      /* CMP */
            DAR = A;
            DAR -= getreg(OP & 0x07);
            setarith(DAR);
            A &= BYTE_R;                //required ***
            goto loop_end;
        }

        if ((OP & 0xC7) == 0xC2) {      /* JMP <condition> */
            adr = fetch_word();
            if (cond((OP >> 3) & 0x07))
                PC = adr;
            goto loop_end;
        }

        if ((OP & 0xC7) == 0xC4) {      /* CALL <condition> */
            adr = fetch_word();
            if (cond((OP >> 3) & 0x07)) {
                push_word(PC);
                PC = adr;
            }
            goto loop_end;
        }

        if ((OP & 0xC7) == 0xC0) {      /* RET <condition> */
            if (cond((OP >> 3) & 0x07)) {
                PC = pop_word();
            }
            goto loop_end;
        }

        if ((OP & 0xC7) == 0xC7) {      /* RST */
            push_word(PC);
            PC = OP & 0x38;
            goto loop_end;
        }

        if ((OP & 0xCF) == 0xC5) {      /* PUSH */
            DAR = getpush((OP >> 4) & 0x03);
            push_word(DAR);
            goto loop_end;
        }

        if ((OP & 0xCF) == 0xC1) {      /* POP */
            DAR = pop_word();
            putpush((OP >> 4) & 0x03, DAR);
            goto loop_end;
        }

        if ((OP & 0xF8) == 0x80) {      /* ADD */
            A += getreg(OP & 0x07);
            setarith(A);
            A &= BYTE_R;                //required
            goto loop_end;
        }

        if ((OP & 0xF8) == 0x88) {      /* ADC */
            A += getreg(OP & 0x07);
            if (GET_FLAG(CF))
                A++;
            setarith(A);
            A &= BYTE_R;                //required
            goto loop_end;
        }

        if ((OP & 0xF8) == 0x90) {      /* SUB */
            A -= getreg(OP & 0x07);
            setarith(A);
            A &= BYTE_R;                //required
            goto loop_end;
        }

        if ((OP & 0xF8) == 0x98) {      /* SBB */
            A -= getreg(OP & 0x07);
            if (GET_FLAG(CF))
                A--;
            setarith(A);
            A &= BYTE_R;                //required
            goto loop_end;
        }

        if ((OP & 0xC7) == 0x04) {      /* INR */
            DAR = getreg((OP >> 3) & 0x07);
            DAR++;
            setinc(DAR);
            DAR &= BYTE_R;              //required
            putreg((OP >> 3) & 0x07, DAR);
            goto loop_end;
        }

        if ((OP & 0xC7) == 0x05) {      /* DCR */
            DAR = getreg((OP >> 3) & 0x07);
            DAR--;
            setinc(DAR);
            DAR &= BYTE_R;              //required
            putreg((OP >> 3) & 0x07, DAR);
            goto loop_end;
        }

        if ((OP & 0xCF) == 0x03) {      /* INX */
            DAR = getpair((OP >> 4) & 0x03);
            DAR++;
            DAR &= WORD_R;              //required
            putpair((OP >> 4) & 0x03, DAR);
            goto loop_end;
        }

        if ((OP & 0xCF) == 0x0B) {      /* DCX */
            DAR = getpair((OP >> 4) & 0x03);
            DAR--;
            DAR &= WORD_R;              //required
            putpair((OP >> 4) & 0x03, DAR);
            goto loop_end;
        }

        if ((OP & 0xCF) == 0x09) {      /* DAD */
            HL += getpair((OP >> 4) & 0x03);
            COND_SET_FLAG(HL & 0x10000, CF);
            HL &= WORD_R;               //required
            goto loop_end;
        }

        if ((OP & 0xF8) == 0xA0) {      /* ANA */
            A &= getreg(OP & 0x07);
            setlogical(A);
            goto loop_end;
        }

        if ((OP & 0xF8) == 0xA8) {      /* XRA */
            A ^= getreg(OP & 0x07);
            setlogical(A);
            goto loop_end;
        }

        if ((OP & 0xF8) == 0xB0) {      /* ORA */
            A |= getreg(OP & 0x07);
            setlogical(A);
            goto loop_end;
        }

        /* The Big Instruction Decode Switch */

        switch (IR) {

        /* 8085 instructions only */
        case 0x20:                  /* RIM */
            if (i8080_unit.flags & UNIT_8085) { /* 8085 */
                A = IM;
            } else {                /* 8080 */
                reason = STOP_OPCODE;
                PC--;
            }
            break;

        case 0x30:                  /* SIM */
            if (i8080_unit.flags & UNIT_8085) { /* 8085 */
                if (A & MSE) {
                    IM &= 0xF8;
                    IM |= A & 0x07;
                }
                if (A & I75) {      /* reset RST 7.5 FF */
                }
            } else {                /* 8080 */
                reason = STOP_OPCODE;
                PC--;
            }
            break;

    /* Logical instructions */

        case 0xFE:                  /* CPI */
            DAR = A;
            DAR -= fetch_byte(1);
            setarith(DAR);
            break;

        case 0xE6:                  /* ANI */
            A &= fetch_byte(1);
            setlogical(A);
            break;

        case 0xEE:                  /* XRI */
            A ^= fetch_byte(1);
            setlogical(A);
            break;

        case 0xF6:                  /* ORI */
            A |= fetch_byte(1);
            setlogical(A);
            break;

        /* Jump instructions */

        case 0xC3:                  /* JMP */
            PC = fetch_word();
            break;

        case 0xE9:                  /* PCHL */
            PC = HL;
            break;

        case 0xCD:                  /* CALL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0xC9:                  /* RET */
            PC = pop_word();
            break;

        /* Data Transfer Group */

        case 0x32:                  /* STA */
            DAR = fetch_word();
            put_mbyte(DAR, A);
            break;

        case 0x3A:                  /* LDA */
            DAR = fetch_word();
            A = get_mbyte(DAR);
            break;

        case 0x22:                  /* SHLD */
            DAR = fetch_word();
            put_mword(DAR, HL);
            break;

        case 0x2A:                  /* LHLD */
            DAR = fetch_word();
            HL = get_mword(DAR);
            break;

        case 0xEB:                  /* XCHG */
            DAR = HL;
            HL = DE;
            HL &= WORD_R;           //required
            DE = DAR;
            break;

        /* Arithmetic Group */

        case 0xC6:                  /* ADI */
            A += fetch_byte(1);
            setarith(A);
            A &= BYTE_R;            //required
            break;

        case 0xCE:                  /* ACI */
            A += fetch_byte(1);
            if (GET_FLAG(CF))
                A++;
            setarith(A);
            A &= BYTE_R;            //required
            break;

        case 0xD6:                  /* SUI */
            A -= fetch_byte(1);
            setarith(A);
            A &= BYTE_R;            //required
            break;

        case 0xDE:                  /* SBI */
            A -= fetch_byte(1);
            if (GET_FLAG(CF))
                A--;
            A &= BYTE_R;            //required
            break;

        case 0x27:                  /* DAA */
            DAR = A & 0x0F;
            if (DAR > 9 || GET_FLAG(AF)) {
                DAR += 6;
                A &= 0xF0;
                A |= DAR & 0x0F;
                COND_SET_FLAG(DAR & 0x10, AF);
            }
            DAR = (A >> 4) & 0x0F;
            if (DAR > 9 || GET_FLAG(AF)) {
                DAR += 6;
                if (GET_FLAG(AF)) DAR++;
                A &= 0x0F;
                A |= (DAR << 4);
            }
            COND_SET_FLAG(DAR & 0x10, CF);
            COND_SET_FLAG(A & 0x80, SF);
            COND_SET_FLAG((A & 0xFF) == 0, ZF);
            A &= BYTE_R;            //required
            parity(A);
            break;

        case 0x07:                  /* RLC */
            COND_SET_FLAG(A & 0x80, CF);
            A = A << 1;
            if (GET_FLAG(CF))
                A |= 0x01;
            A &= BYTE_R;            //required
            break;

        case 0x0F:                  /* RRC */
            COND_SET_FLAG(A & 0x01, CF);
            A = A >> 1;
            if (GET_FLAG(CF))
                A |= 0x80;
            A &= BYTE_R;            //required
            break;

        case 0x17:                  /* RAL */
            DAR = GET_FLAG(CF);
            COND_SET_FLAG(A & 0x80, CF);
            A = A << 1;
            if (DAR)
                A |= 0x01;
            A &= BYTE_R;            //required
            break;

        case 0x1F:                  /* RAR */
            DAR = GET_FLAG(CF);
            COND_SET_FLAG(A & 0x01, CF);
            A = A >> 1;
            if (DAR)
                A |= 0x80;
            A &= BYTE_R;            //required
            break;

        case 0x2F:                  /* CMA */
            A = ~A;
            A &= BYTE_R;            //required
            break;

        case 0x3F:                  /* CMC */
            TOGGLE_FLAG(CF);
            break;

        case 0x37:                  /* STC */
            SET_FLAG(CF);
            break;

        /* Stack, I/O & Machine Control Group */

        case 0x00:                  /* NOP */
            break;

        case 0xE3:                  /* XTHL */
            DAR = pop_word();
            push_word(HL);
            HL = DAR;
            break;

        case 0xF9:                  /* SPHL */
            SP = HL;
            break;

        case 0xFB:                  /* EI */
            IM |= IE;
            break;

        case 0xF3:                  /* DI */
            IM &= ~IE;
            break;

        case 0xDB:                  /* IN */
            port = fetch_byte(1);
            A = dev_table[port].routine(0, 0, dev_table[port].devnum);
            SET_XACK(1);                /* good I/O address */
            break;

        case 0xD3:                  /* OUT */
            port = fetch_byte(1);
            dev_table[port].routine(1, A, dev_table[port].devnum);
            SET_XACK(1);                /* good I/O address */
            break;

        default:                    /* undefined opcode */ 
            if (i8080_unit.flags & UNIT_OPSTOP) {
                reason = STOP_OPCODE;
                PC--;
            }
            break;
        }
loop_end:

        /*
        if (GET_XACK(1) == 0) {     // no XACK for operand fetch
            reason = STOP_XACK;
            if (OP == 0xD3 || OP == 0xDB) {
//                if (uptr->flags & UNIT_XACK) 
                    sim_printf("Failed XACK for Port %02X Fetch from %04X\n", port, PCX);
            } else {
//                if (uptr->flags & UNIT_XACK) 
                    sim_printf("Failed XACK for Operand %04X Fetch from %04X\n", addr, PCX);
            continue;
            }
        }
        */;
    }

/* Simulation halted */

    saved_PC = PC;
    return reason;
}

/* dump the registers */
void dumpregs(void)
{
    sim_printf("  PC=%04X A=%02X BC=%04X DE=%04X HL=%04X SP=%04X IM=%02X XACK=%d",
        PCY, A, BC, DE, HL, SP, IM, xack);
    sim_printf(" IR=%02X addr=%04X", IR, addr);
    sim_printf(" CF=%d ZF=%d AF=%d SF=%d PF=%d\n", 
    GET_FLAG(CF) ? 1 : 0,
    GET_FLAG(ZF) ? 1 : 0,
    GET_FLAG(AF) ? 1 : 0,
    GET_FLAG(SF) ? 1 : 0,
    GET_FLAG(PF) ? 1 : 0);
}

/* fetch an instruction or byte */
int32 fetch_byte(int32 flag)
{
    uint32 val;

    val = get_mbyte(PC) & 0xFF;         /* fetch byte */
    PC = (PC + 1) & ADDRMASK;           /* increment PC */
    addr = val & 0xff;
    return val;
}

/* fetch a word */
int32 fetch_word(void)
{
    uint16 val;

    val = get_mbyte(PC) & BYTE_R;       /* fetch low byte */
    val |= get_mbyte(PC + 1) << 8;      /* fetch high byte */
//    if (i8080_dev.dctrl & DEBUG_asm || uptr->flags & UNIT_TRACE)   /* display source code */
//        sim_printf("0%04XH", val);
    PC = (PC + 2) & ADDRMASK;           /* increment PC */
    val &= WORD_R;
    addr = val;
    return val;
}

/* push a word to the stack */
void push_word(uint16 val)
{
    SP--;
    put_mbyte(SP, (val >> 8));
    SP--;
    put_mbyte(SP, val & 0xFF);
}

/* pop a word from the stack */
uint16 pop_word(void)
{
    register uint16 res;

    res = get_mbyte(SP);
    SP++;
    res |= get_mbyte(SP) << 8;
    SP++;
    return res;
}

/* Test an 8080 flag condition and return 1 if true, 0 if false */
int32 cond(int32 con)
{
    switch (con) {
    case 0:                         /* NZ */
        if (GET_FLAG(ZF) == 0) return 1;
        break;
    case 1:                         /* Z */
        if (GET_FLAG(ZF)) return 1;
        break;
    case 2:                         /* NC */
        if (GET_FLAG(CF) == 0) return 1;
        break;
    case 3:                         /* C */
        if (GET_FLAG(CF)) return 1;
        break;
    case 4:                         /* PO */
        if (GET_FLAG(PF) == 0) return 1;
        break;
    case 5:                         /* PE */
        if (GET_FLAG(PF)) return 1;
        break;
    case 6:                         /* P */
        if (GET_FLAG(SF) == 0) return 1;
        break;
    case 7:                         /* M */
        if (GET_FLAG(SF)) return 1;
        break;
    default:
        break;
    }
    return 0;
}

/* Set the <C>arry, <S>ign, <Z>ero and <P>arity flags following
   an arithmetic operation on 'reg'.
*/

void setarith(int32 reg)
{
    COND_SET_FLAG(reg & 0x100, CF);
    COND_SET_FLAG(reg & 0x80, SF);
    COND_SET_FLAG((reg & BYTE_R) == 0, ZF);
    CLR_FLAG(AF);
    parity(reg);
}

/* Set the <C>arry, <S>ign, <Z>ero amd <P>arity flags following
   a logical (bitwise) operation on 'reg'.
*/

void setlogical(int32 reg)
{
    CLR_FLAG(CF);
    COND_SET_FLAG(reg & 0x80, SF);
    COND_SET_FLAG((reg & BYTE_R) == 0, ZF);
    CLR_FLAG(AF);
    parity(reg);
}

/* Set the Parity (P) flag based on parity of 'reg', i.e., number
   of bits on even: P=0200000, else P=0
*/

void parity(int32 reg)
{
    int32 bc = 0;

    if (reg & 0x01) bc++;
    if (reg & 0x02) bc++;
    if (reg & 0x04) bc++;
    if (reg & 0x08) bc++;
    if (reg & 0x10) bc++;
    if (reg & 0x20) bc++;
    if (reg & 0x40) bc++;
    if (reg & 0x80) bc++;
    if (bc & 0x01)
        CLR_FLAG(PF);
    else
        SET_FLAG(PF);
}

/* Set the <S>ign, <Z>ero and <P>arity flags following
   an INR/DCR operation on 'reg'.
*/

void setinc(int32 reg)
{
    COND_SET_FLAG(reg & 0x80, SF);
    COND_SET_FLAG((reg & BYTE_R) == 0, ZF);
    parity(reg);
}

/* Get an 8080 register and return it */
int32 getreg(int32 reg)
{
    switch (reg) {
    case 0:                         /* reg B */
        return ((BC >>8) & BYTE_R);
    case 1:                         /* reg C */
        return (BC & BYTE_R);
    case 2:                         /* reg D */
        return ((DE >>8) & BYTE_R);
    case 3:                         /* reg E */
        return (DE & BYTE_R);
    case 4:                         /* reg H */
        return ((HL >>8) & BYTE_R);
    case 5:                         /* reg L */
        return (HL & BYTE_R);
    case 6:                         /* reg M */
        return (get_mbyte(HL));
    case 7:                         /* reg A */
        return (A);
    default:
        break;
    }
    return 0;
}

/* Put a value into an 8-bit 8080 register from memory */
void putreg(int32 reg, int32 val)
{
    switch (reg) {
    case 0:                         /* reg B */
        BC = BC & BYTE_R;
        BC = BC | (val <<8);
        break;
    case 1:                         /* reg C */
        BC = BC & 0xFF00;
        BC = BC | val;
        break;
    case 2:                         /* reg D */
        DE = DE & BYTE_R;
        DE = DE | (val <<8);
        break;
    case 3:                         /* reg E */
        DE = DE & 0xFF00;
        DE = DE | val;
        break;
    case 4:                         /* reg H */
        HL = HL & BYTE_R;
        HL = HL | (val <<8);
        break;
    case 5:                         /* reg L */
        HL = HL & 0xFF00;
        HL = HL | val;
        break;
    case 6:                         /* reg M */
        put_mbyte(HL, val);
        break;
    case 7:                         /* reg A */
        A = val & BYTE_R;
    default:
        break;
    }
}

/* Return the value of a selected register pair */
int32 getpair(int32 reg)
{
    switch (reg) {
    case 0:                         /* reg BC */
        return (BC);
    case 1:                         /* reg DE */
        return (DE);
    case 2:                         /* reg HL */
        return (HL);
    case 3:                         /* reg SP */
        return (SP);
    default:
        break;
    }
    return 0;
}

/* Return the value of a selected register pair, in PUSH
   format where 3 means A & flags, not SP */
int32 getpush(int32 reg)
{
    int32 stat;

    switch (reg) {
    case 0:                         /* reg BC */
        return (BC);
    case 1:                         /* reg DE */
        return (DE);
    case 2:                         /* reg HL */
        return (HL);
    case 3:                         /* reg (A << 8) | PSW */
        stat = A << 8 | PSW;
        return (stat);
    default:
        break;
    }
    return 0;
}


/* Place data into the indicated register pair, in PUSH
   format where 3 means A & flags, not SP */
void putpush(int32 reg, int32 data)
{
    switch (reg) {
    case 0:                         /* reg BC */
        BC = data;
        break;
    case 1:                         /* reg DE */
        DE = data;
        break;
    case 2:                         /* reg HL */
        HL = data;
        break;
    case 3:                         /* reg (A << 8) | PSW */
        A = (data >> 8) & BYTE_R;
        PSW = data & BYTE_R;
        break;
    default:
        break;
    }
}


/* Put a value into an 8080 register pair */
void putpair(int32 reg, int32 val)
{
    switch (reg) {
    case 0:                         /* reg BC */
        BC = val;
        break;
    case 1:                         /* reg DE */
        DE = val;
        break;
    case 2:                         /* reg HL */
        HL = val;
        break;
    case 3:                         /* reg SP */
        SP = val;
        break;
    default:
        break;
    }
}

/* Reset routine */

t_stat i8080_reset (DEVICE *dptr)
{
    PSW = PSW_ALWAYS_ON;
    CLR_FLAG(CF);
    CLR_FLAG(ZF);
    saved_PC = 0;
    int_req = 0;
    IM = 0;
    INTA = 0;
    sim_brk_types = sim_brk_dflt = SWMASK ('E');
    return SCPE_OK;
}

/* Memory examine */

t_stat i8080_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE) 
        return SCPE_NXM;
    if (vptr != NULL) 
        *vptr = get_mbyte(addr);
    return SCPE_OK;
}

/* Memory deposit */

t_stat i8080_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE) 
        return SCPE_NXM;
    put_mbyte(addr, val);
    return SCPE_OK;
}

/* This is the binary loader.  The input file is considered to be
   a string of literal bytes with no special format. The load 
   starts at the current value of the PC.
*/

int32 sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
    int32 i, addr = 0, cnt = 0;

    if ((*cptr != 0)) return SCPE_ARG;
    if (flag == 0) {                     //load
//        addr = saved_PC;
        while ((i = getc (fileref)) != EOF) {
            put_mbyte(addr, i);
            addr++;
            cnt++;
        }                               /* end while */
        sim_printf ("%d Bytes loaded.\n", cnt);
        return (SCPE_OK);
    } else {                            //dump
//        addr = saved_PC;
        while (addr <= 0xffff) {
            i = get_mbyte(addr);
            putc(i, fileref);
            addr++;
            cnt++;
        }
    }
    return (SCPE_OK);
}

/* Symbolic output - working
   Inputs:
        *of     =       output stream
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
    int32 cflag, c1, c2, inst, adr;

    cflag = (uptr == NULL) || (uptr == &i8080_unit);
    c1 = (val[0] >> 8) & 0x7F;
    c2 = val[0] & 0x7F;
    if (sw & SWMASK ('A')) {
        fprintf (of, (c2 < 0x20)? "<%02X>": "%c", c2);
        return SCPE_OK;
    }
    if (sw & SWMASK ('C')) {
        fprintf (of, (c1 < 0x20)? "<%02X>": "%c", c1);
        fprintf (of, (c2 < 0x20)? "<%02X>": "%c", c2);
        return SCPE_OK;
    }
    if (!(sw & SWMASK ('M'))) return SCPE_ARG;
    inst = val[0];
    fprintf (of, "%s", opcode[inst]);
    if (oplen[inst] == 2) {
//        if (strchr(opcode[inst], ' ') != NULL)
//            fprintf (of, ",");
//        else fprintf (of, " ");
        fprintf (of, "%02X", val[1]);
    }
    if (oplen[inst] == 3) {
        adr = val[1] & 0xFF;
        adr |= (val[2] << 8) & 0xff00;
//        if (strchr(opcode[inst], ' ') != NULL)
//            fprintf (of, ",");
//        else fprintf (of, " ");
        fprintf (of, "%04X", adr);
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

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
    int32 cflag, i = 0, j, r;
    char gbuf[CBUFSIZE];

    memset (gbuf, 0, sizeof (gbuf));
    cflag = (uptr == NULL) || (uptr == &i8080_unit);
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

    while (i < sizeof (gbuf) - 4) {
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

/* end of i8080.c */
