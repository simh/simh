/* i8008.c: Intel 8008 CPU simulator

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

   cpu          8008 CPU

   The register state for the 8008 CPU is:

   A<0:7>               Accumulator
   B<0:7>               B Register
   C<0:7>               C Register
   D<0:7>               D Register
   E<0:7>               E Register
   H<0:7>               H Register
   L<0:7>               L Register
   PC<0:13>             Program counter

   The 8008 is an 8-bit CPU, which uses 14-bit registers to address
   up to 16KB of memory.

   The 57 basic instructions come in 1, 2, and 3-byte flavors.

   This routine is the instruction decode routine for the 8008.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HLT instruction
        I/O error in I/O simulator
        Invalid OP code (if ITRAP is set on CPU)

   2. Interrupts.
      There are 8 possible levels of interrupt, and in effect they
      do a hardware CALL instruction to one of 8 possible low
      memory addresses.

   3. Non-existent memory.  On the 8008, reads to non-existent memory
      return 0FFh, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

    15 Feb 15 - Original file.

*/

#include "system_defs.h"

#define UNIT_V_OPSTOP   (UNIT_V_UF)     /* Stop on Invalid OP? */
#define UNIT_OPSTOP     (1 << UNIT_V_OPSTOP)
#define UNIT_V_TRACE    (UNIT_V_UF+1)   /*  Trace switch */
#define UNIT_TRACE      (1 << UNIT_V_TRACE)

/* register masks */
#define BYTE_R  0xFF
#define WORD_R14  0x3FFF

/* storage for the rest of the registers */
uint32 A = 0;                           /* accumulator */
uint32 B = 0;                           /* B register */
uint32 C = 0;                           /* C register */
uint32 D = 0;                           /* D register */
uint32 E = 0;                           /* E register */
uint32 H = 0;                           /* H register */
uint32 L = 0;                           /* L register */
uint32 CF = 0;                           /* C - carry flag */
uint32 PF = 0;                           /* P - parity flag */
uint32 ZF = 0;                           /* Z - zero flag */
uint32 SF = 0;                           /* S - sign flag */
uint32 SP = 0;                           /* stack frame pointer */
uint32 saved_PC = 0;                    /* program counter */
uint32 int_req = 0;                     /* Interrupt request */

int32 PCX;                              /* External view of PC */
int32 PC;
UNIT *uptr;

/* function prototypes */
void    store_m(unit32 val);
unit32  fetch_m(void);
void    set_cpuint(int32 int_num);
void    dumpregs(void);
int32   fetch_byte(int32 flag);
int32   fetch_word(void);
uint16  pop_word(void);
void    push_word(uint16 val);
void    setflag4(int32 reg);
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
t_stat  i8008_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat  i8008_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat  i8008_reset (DEVICE *dptr);

/* external function prototypes */

extern t_stat i8008_reset (DEVICE *dptr);
extern int32 get_mbyte(int32 addr);
extern int32 get_mword(int32 addr);
extern void put_mbyte(int32 addr, int32 val);
extern void put_mword(int32 addr, int32 val);
extern int32 sim_int_char;
extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */


struct idev {
    int32 (*routine)();
};

/* This is the I/O configuration table.  There are 256 possible
device addresses, if a device is plugged to a port it's routine
address is here, 'nulldev' means no device is available
*/

extern struct idev dev_table[];

/* CPU data structures

   i8008_dev      CPU device descriptor
   i8008_unit     CPU unit descriptor
   i8008_reg      CPU register list
   i8008_mod      CPU modifiers list
*/

UNIT i8008_unit = { UDATA (NULL, 0, 65535) }; /* default 8008 */

REG i8008_reg[] = {
    { HRDATA (PC, saved_PC, 16) },      /* must be first for sim_PC */
    { HRDATA (A, A, 8) },
    { HRDATA (B, B, 8) },
    { HRDATA (C, C, 8) },
    { HRDATA (D, D, 8) },
    { HRDATA (E, E, 8) },
    { HRDATA (H, H, 8) },
    { HRDATA (L, L, 8) },
    { HRDATA (CF, CF, 1) },
    { HRDATA (PF, PF, 1) },
    { HRDATA (ZF, SF, 1) },
    { HRDATA (SF, SF, 1) },
    { HRDATA (INTR, int_req, 32) },
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
};

MTAB i8008_mod[] = {
    { UNIT_OPSTOP, 0, "ITRAP", "ITRAP", NULL },
    { UNIT_OPSTOP, UNIT_OPSTOP, "NOITRAP", "NOITRAP", NULL },
    { UNIT_TRACE, 0, "NOTRACE", "NOTRACE", NULL },
    { UNIT_TRACE, UNIT_TRACE, "TRACE", "TRACE", NULL },
    { 0 }
};

DEBTAB i8008_debug[] = {
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

DEVICE i8008_dev = {
    "I8008",                              //name
    &i8008_unit,                        //units
    i8008_reg,                          //registers
    i8008_mod,                          //modifiers
    1,                                  //numunits
    16,                                 //aradix 
    16,                                 //awidth 
    1,                                  //aincr 
    16,                                 //dradix 
    8,                                  //dwidth
    &i8008_ex,                          //examine 
    &i8008_dep,                         //deposit 
//    &i8008_reset,                       //reset
    NULL,                               //reset
    NULL,                               //boot
    NULL,                               //attach 
    NULL,                               //detach
    NULL,                               //ctxt
    DEV_DEBUG,                          //flags 
    0,                                  //dctrl 
    i8008_debug,                        //debflags
    NULL,                               //msize
    NULL                                //lname
};

/* tables for the disassembler */
char *opcode[] = {                      
"*HLT", "*HLT", "RLC", "RFC",           /* 0x00 */
"ADI ", "RST 0", "LAI ,", "RET",
"INB", "DCB", "RRC", "RFZ",
"ACI ", "RST 1", "LBI ", "*RET",
"INC", "DCC", "RAL", "RFS",             /* 0x10 */
"SUI ", "RST 2", "LCI ", "*RET",
"IND", "DCD", "RAR", "RFP",
"SBI ", "RST 3", "LDI ", "*RET",
"INE", "DCE", "???", "RTC",             /* 0x20 */
"NDI ", "RST 4", "LEI ", "*RET",
"INH", "DCH", "???", "RTZ",
"XRI ", "RST 5", "LHI ", "*RET",
"INL", "DCL", "???", "RTS",             /* 0x30 */
"ORI ", "RST 6", "LLI ", "*RET",
"???", "???", "???", "RTP",
"CPI ", "RST 7", "LMI ", "*RET",
"JFC ", "INP ", "CFC ", "INP ",         /* 0x40 */
"JMP ", "INP ", "CAL ", "INP ",
"JFZ ", "INP ", "CFZ ", "INP ",
"*JMP ", "INP ", "*CAL ", "INP ",
"JFS", "OUT ", "CFS ", "OUT ",          /* 0x50 */
"*JMP ", "OUT ", "*CAL ", "OUT ",
"JFP ", "OUT ", "CFP ", "OUT ",
"*JMP ", "OUT ", "*CAL ", "OUT ",
"JTC ", "OUT ", "CTC ", "OUT ",         /* 0x60 */
"*JMP ", "OUT ", "*CAL ", "OUT ",
"JTZ ", "OUT ", "CTZ", "OUT ",
"*JMP ", "OUT ", "*CAL", "OUT ",
"JTS ", "OUT ", "CTS ", "OUT ",         /* 0x70 */
"*JMP ", "OUT ", "*CAL ", "OUT ",
"JTP ", "OUT ", "CTP", "OUT ",
"*JMP ", "OUT ", "*CAL ", "OUT ",
"ADA", "ADB", "ADC", "ADD",             /* 0x80 */
"ADE", "ADH", "ADL", "ADM",
"ACA", "ACB", "ACC", "ACD",
"ACE", "ACH", "ACL", "ACM",
"SUA", "SUB", "SUC", "SUD",             /* 0x90 */
"SUE", "SUH", "SUL", "SUM",
"SBA", "SBB", "SBC", "SBD",
"SBE", "SBH", "SBL", "SBM",
"NDA", "NDB", "NDC", "NDD",             /* 0xA0 */
"NDE", "NDH", "NDL", "NDM",
"XRA", "XRB", "XRC", "XRD",
"XRE", "XRH", "XRL", "XRM",
"ORA", "ORB", "ORC", "ORD",             /* 0xB0 */
"ORE", "ORH", "ORL", "ORM",
"CPA", "CPB", "CPC", "CPD",
"CPE", "CPH", "CPL", "CPM",
"NOP", "LAB", "LAC", "LAD",             /* 0xC0 */
"LAE", "LAH", "LAL", "LAM",
"LBA", "LBB", "LBC", "LBD",
"LBE", "LBH ", "LBL", "LBM",
"LCA", "LCB", "LCC", "LCD",             /* 0xD0 */
"LCE", "LCH", "LCL", "LCM",
"LDA", "LDB", "LDC", "LDD",
"LDE", "LDH", "LDL", "LDM",
"LEA", "LEB", "LEC", "LED",             /* 0xE0 */
"LEE", "LEH", "LEL", "LEM",
"LHA", "LHB", "LHC", "LHD",
"LHE", "LHH", "LHL", "LHM",
"LLA", "LLB", "LLC", "LLD",             /* 0xF0 */
"LLE", "LLH", "LLL", "LLM",
"LMA", "LMB", "LMC", "LMD",
"LME", "LMH", "LML", "HLT",
 };

int32 oplen[256] = {
/*
    0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, 2, 1, /* 0X */
    1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, 2, 1, /* 1X */
    1, 1, 0, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, 2, 1, /* 2X */
    1, 1, 0, 1, 2, 1, 2, 1, 0, 0, 0, 1, 2, 1, 2, 1, /* 3X */
    3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, /* 4X */
    3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, /* 5X */
    3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, /* 6X */
    3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, /* 7X */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 8X */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 9X */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* AX */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* BX */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* CX */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* DX */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* EX */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1  /* FX */
};

uint16 stack_frame[7];                 /* processor stack frame */

void set_cpuint(int32 int_num)
{
    int_req |= int_num;
}

/* instruction simulator */
int32 sim_instr (void)
{
    extern int32 sim_interval;
    uint32 IR, OP, DAR, reason, hi, lo, i, adr, val;

    PC = saved_PC & WORD_R14;             /* load local PC */
    reason = 0;

    uptr = i8008_dev.units;

    /* Main instruction fetch/decode loop */

    while (reason == 0) {               /* loop until halted */

//        if (PC == 0x1000) {             /* turn on debugging */
//            i8008_dev.dctrl = DEBUG_asm + DEBUG_reg; 
//            reason = STOP_HALT;
//        }
        if (i8008_dev.dctrl & DEBUG_reg) {
            dumpregs();
            sim_printf("\n");
        }

        if (sim_interval <= 0) {        /* check clock queue */
            if (reason = sim_process_event())
                break;
        }

        if (int_req > 0) {              /* interrupt? */
//            sim_printf("\ni8008: int_req=%04X", int_req);
            ;
        } else {                    /* 8008 */
            if (IE) {          /* enabled? */
                push_word(PC);      /* do an RST 7 */
                PC = 0x0038;
                int_req &= ~INT_R;
//                    sim_printf("\ni8008: int_req=%04X", int_req);
            }
        }                               /* end interrupt */

        if (sim_brk_summ &&
            sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
            reason = STOP_IBKPT;        /* stop simulation */
            break;
        }

        sim_interval--;                 /* countdown clock */
        PCX = PC;

        if (uptr->flags & UNIT_TRACE) {
            dumpregs();
            sim_printf("\n");
        }
        IR = OP = fetch_byte(0);        /* instruction fetch */

        if (OP == 0x00 || OP == 0x01 || OP == 0xFF) { /* HLT Instruction*/
            reason = STOP_HALT;
            PC--;
            continue;
        }

        /* The Big Instruction Decode Switch */

        switch (IR) {

        case 0x02:                  /* RLC */
            if (A & 0x80)
                CF = 1;
            else
                CF = 0;
            A = (A << 1) & 0xFF;
            if (CF)
                A |= 0x01;
            A &= BYTE_R;
            break;

        case 0x03:                  /* RFC */
            if (CF)
                ;
            else
                PC = pop_word();
            break;

        case 0x04:                  /* ADI */
            A += fetch_byte(1);
            setflag3(A);
            A &= BYTE_R;
            break;

        case 0x05:                  /* RST 0 */
        case 0x0D:                  /* RST 1 */
        case 0x15:                  /* RST 2 */
        case 0x1D:                  /* RST 3 */
        case 0x25:                  /* RST 4 */
        case 0x2D:                  /* RST 5 */
        case 0x35:                  /* RST 6 */
        case 0x3D:                  /* RST 7 */
            val = fetch_byte();
            push_word(PC);
            PC = val << 3;
            break;

        case 0x06:                  /* LAI */
            A = fetch_byte(1);
            A &= BYTE_R;
            break;

        case 0x07:                  /* RET */
            PC = pop_word();
            break;

        case 0x08:                  /* INB */
            B++;
            setflag3(B);
            B &= BYTE_R;
            break;

        case 0x09:                  /* DCB */
            B--;
            setflag3(B);
            B &= BYTE_R;
            break;

        case 0x0A:                  /* RRC */
            if (A & 0x01)
                CF = 1;
            else
                CF = 0;
            A = (A >> 1) & 0xFF;
            if (CF)
                A |= 0x80;
            A &= BYTE_R;
            break;

        case 0x0B:                  /* RFZ */
            if (ZF)
                ;
            else
                PC = pop_word();
            break;

        case 0x0C:                  /* ACI */
            A += fetch_byte(1);
            if (CF)
                A++;
            setflag3(A);
            A &= BYTE_R;
            break;

        case 0x0E:                  /* LBI */
            B = fetch_byte(1);
            B &= BYTE_R;
            break;

        case 0x0F:                  /* *RET */
            PC = pop_word();
            break;

        case 0x10:                  /* INC */
            C++;
            setflag3(C);
            C &= BYTE_R;
            break;

        case 0x11:                  /* DCC */
            C--;
            setflag3(C);
            C &= BYTE_R;
            break;

        case 0x12:                  /* RAL */
            if (A & 0x80)
                CF = 1;
            else
                CF = 0;
            A = (A << 1) & 0xFF;
            if (CF)
                A |= 0x01;
            A &= BYTE_R;
            break;

        case 0x13:                  /* RFS */
            if (SF)
                ;
            else
                PC = pop_word();
            break;

        case 0x14:                  /* SUI */
            A -= fetch_byte(1);
            setflag3(A);
            A &= BYTE_R;
            break;

        case 0x16:                  /* LCI */
            C = fetch_byte(1);
            C &= BYTE_R;
            break;

        case 0x17:                  /* *RET */
            PC = pop_word();
            break;

        case 0x18:                  /* IND */
            D++;
            setflag3(D);
            D &= BYTE_R;
            break;

        case 0x19:                  /* DCD */
            D--;
            setflag3(D);
            D &= BYTE_R;
            break;

        case 0x1A:                  /* RAR */
            if (A & 0x01)
                CF = 1;
            else
                CF = 0;
            A = (A >> 1) & 0xFF;
            if (CF)
                A |= 0x80;
            A &= BYTE_R;
            break;

        case 0x1B:                  /* RFP */
            if (PF)
                ;
            else
                PC = pop_word();
            break;

        case 0x1C:                  /* SBI */
            A -= fetch_byte(1);
            if (CF)
                A--;
            setflag3(A);
            A &= BYTE_R;
            break;

        case 0x1E:                  /* LDI */
            D = fetch_byte(1);
            D &= BYTE_R;
            break;

        case 0x1F:                  /* *RET */
            PC = pop_word();
            break;

        case 0x20:                  /* INE */
            E++;
            setflag3(E);
            E &= BYTE_R;
            break;

        case 0x21:                  /* DCE */
            E--;
            setflag3(E);
            E &= BYTE_R;
            break;

        case 0x23:                  /* RTC */
            if (CF)
                PC = pop_word();
            break;

        case 0x24:                  /* NDI */
            A &= fetch_byte(1);
            setflag3(A);
            A &= BYTE_R;
            break;

        case 0x26:                  /* LEI */
            E = fetch_byte(1);
            E &= BYTE_R;
            break;

        case 0x27:                  /* *RET */
            PC = pop_word();
            break;

        case 0x28:                  /* INH */
            H++;
            setflag3(H);
            H &= BYTE_R;
            break;

        case 0x29:                  /* DCH */
            H--;
            setflag3(H);
            H &= BYTE_R;
            break;

        case 0x2B:                  /* RTZ */
            if (ZF)
                PC = pop_word();
            break;

        case 0x2C:                  /* XRI */
            A ^= fetch_byte(1);
            setflag3(A);
            break;

        case 0x2E:                  /* LHI */
            H = fetch_byte(1);
            H &= BYTE_R;
            break;

        case 0x2F:                  /* *RET */
            PC = pop_word();
            break;

        case 0x30:                  /* INL */
            L++;
            setflag3(L);
            L &= BYTE_R;
            break;

        case 0x31:                  /* DCL */
            L--;
            setflag3(L);
            L &= BYTE_R;
            break;

        case 0x33:                  /* RTS */
            if (SF)
                PC = pop_word();
            break;

        case 0x34:                  /* ORI */
            A |= fetch_byte(1);
            setflag3(A);
            A &= BYTE_R;
            break;

        case 0x36:                  /* LLI */
            L = fetch_byte(1);
            L &= BYTE_R;
            break;

        case 0x37:                  /* *RET */
            PC = pop_word();
            break;

        case 0x3B:                  /* RTP */
            if (PF)
                PC = pop_word();
            break;

        case 0x3C:                  /* CPI */
            DAR = A;
            DAR -= fetch_byte(1);
            setflag3(DAR);
            break;

        case 0x3E:                  /* LMI */
            val = fetch_byte(1);
            store_m(val);
            break;

        case 0x3F:                  /* *RET */
            PC = pop_word();
            break;

        case 0x40:                  /* JFC */
            DAR = fetch_word();
            if (CF)
                ;
            else
                PC = DAR;
            break;

        case 0x41:                  /* INP 0 */
        case 0x43:                  /* INP 1 */
        case 0x45:                  /* INP 2 */
        case 0x47:                  /* INP 3 */
        case 0x49:                  /* INP 4 */
        case 0x4B:                  /* INP 5 */
        case 0x4D:                  /* INP 6 */
        case 0x4F:                  /* INP 7 */
            /**** fix me! */
            break;

        case 0x42:                  /* CFC */
            adr = fetch_word();
            if (CF)
                ;
            else {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x44:                  /* JMP */
            PC = fetch_word();
            break;

        case 0x46:                  /* CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x48:                  /* JFZ */
            DAR = fetch_word();
            if (ZF)
                ;
            else
                PC = DAR;
            break;

        case 0x4A:                  /* CFZ */
            adr = fetch_word();
            if (ZF)
                ;
            else {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x4C:                  /* *JMP */
            PC = fetch_word();
            break;

        case 0x4E:                  /* *CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x50:                  /* JFS */
            DAR = fetch_word();
            if (SF)
                ;
            else
                PC = DAR;
            break;

        case 0x51:                  /* OUT 8 */
        case 0x53:                  /* OUT 9 */
        case 0x55:                  /* OUT 10 */
        case 0x57:                  /* OUT 11 */
        case 0x59:                  /* OUT 12 */
        case 0x5B:                  /* OUT 13 */
        case 0x5D:                  /* OUT 14 */
        case 0x5E:                  /* OUT 15 */
        case 0x61:                  /* OUT 16 */
        case 0x63:                  /* OUT 17 */
        case 0x65:                  /* OUT 18 */
        case 0x67:                  /* OUT 19 */
        case 0x69:                  /* OUT 20 */
        case 0x6B:                  /* OUT 21 */
        case 0x6D:                  /* OUT 22 */
        case 0x6E:                  /* OUT 23 */
        case 0x71:                  /* OUT 24 */
        case 0x73:                  /* OUT 25 */
        case 0x75:                  /* OUT 26 */
        case 0x77:                  /* OUT 27 */
        case 0x79:                  /* OUT 28 */
        case 0x7B:                  /* OUT 29 */
        case 0x7D:                  /* OUT 30 */
        case 0x7E:                  /* OUT 31 */
            /**** fix me! */
            break;

        case 0x52:                  /* CFS */
            adr = fetch_word();
            if (SF)
                ;
            else {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x54:                  /* *JMP */
            PC = fetch_word();
            break;

        case 0x56:                  /* *CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x58:                  /* JFP */
            DAR = fetch_word();
            if (PF)
                ;
            else
                PC = DAR;
            break;

        case 0x5A:                  /* CFP */
            adr = fetch_word();
            if (PF)
                ;
            else {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x5C:                  /* *JMP */
            PC = fetch_word();
            break;

        case 0x5E:                  /* *CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x60:                  /* JTC */
            DAR = fetch_word();
            if (CF)
                PC = DAR;
            break;

        case 0x62:                  /* CTC */
            adr = fetch_word();
            if (CF) {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x64:                  /* *JMP */
            PC = fetch_word();
            break;

        case 0x66:                  /* *CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x68:                  /* JTZ */
            DAR = fetch_word();
            if (ZF)
                PC = DAR;
            break;

        case 0x6A:                  /* CTZ */
            adr = fetch_word();
            if (ZF) {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x6C:                  /* *JMP */
            PC = fetch_word();
            break;

        case 0x6E:                  /* *CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x70:                  /* JTS */
            DAR = fetch_word();
            if (SF)
                PC = DAR;
            break;

        case 0x72:                  /* CTS */
            adr = fetch_word();
            if (SF) {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x74:                  /* *JMP */
            PC = fetch_word();
            break;

        case 0x76:                  /* *CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x78:                  /* JTP */
            DAR = fetch_word();
            if (PF)
                PC = DAR;
            break;

        case 0x7A:                  /* CTP */
            adr = fetch_word();
            if (PF) {
                push_word(PC);
                PC = adr;
            }
            break;

        case 0x7C:                  /* *JMP */
            PC = fetch_word();
            break;

        case 0x7E:                  /* *CAL */
            adr = fetch_word();
            push_word(PC);
            PC = adr;
            break;

        case 0x80:                  /* ADA */
            A += A;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x81:                  /* ADB */
            A += B;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x82:                  /* ADC */
            A += C;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x83:                  /* ADD */
            A += D;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x84:                  /* ADE */
            A += E;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x85:                  /* ADH */
            A += H;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x86:                  /* ADL */
            A += L;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x87:                  /* ADM */
            A += fetch_m();
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x88:                  /* ACA */
            A += A;
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x89:                  /* ACB */
            A += B;
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x8A:                  /* ACC */
            A += C;
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x8B:                  /* ACD */
            A += D;
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x8C:                  /* ACE */
            A += E;
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x8D:                  /* ACH */
            A += H;
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x8E:                  /* ACL */
            A += L;
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x8F:                  /* ACM */
            A += fetch_m();
            if (CF)
                A++;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x90:                  /* SUA */
            A -= A;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x91:                  /* SUB */
            A -= B;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x92:                  /* SUC */
            A -= C;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x93:                  /* SUD */
            A -= D;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x94:                  /* SUE */
            A -= E;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x95:                  /* SUH */
            A -= H;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x96:                  /* SUL */
            A -= L;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x97:                  /* SUM */
            A -= fetch_m();
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x98:                  /* SBA */
            A -= A;
            if (CF)
                A--;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x99:                  /* SBB */
            A -= B;
            if (CF)
                A--;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x9A:                  /* SBC */
            A -= C;
            if (CF)
                A--;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x9B:                  /* SBD */
            A -= D;
            if (CF)
                A--;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x9C:                  /* SBE */
            A -= E;
            if (CF)
                A--;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x9D:                  /* SBH */
            A -= H;
            if (CF)
                A--;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x9E:                  /* SBL */
            A -= L;
            if (CF)
                A--;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0x9F:                  /* SBM */
            A -= fetch_m();
            if (CF)
                A - ;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA0:                  /* NDA */
            A &= A;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA1:                  /* NDB */
            A &= B;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA2:                  /* NDC */
            A &= C;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA3:                  /* NDD */
            A &= D;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA4:                  /* NDE */
            A &= E;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA5:                  /* NDH */
            A &= H;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA6:                  /* NDL */
            A &= L;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA7:                  /* NDM */
            A &= fetch_m();
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA8:                  /* XRA */
            A ^= A;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xA9:                  /* XRB */
            A ^= B;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xAA:                  /* XRC */
            A ^= C;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xAB:                  /* XRD */
            A ^= D;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xAC:                  /* XRE */
            A ^= E;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xAD:                  /* XRH */
            A ^= H;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xAE:                  /* XRL */
            A ^= L;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xAF:                  /* XRM */
            A |= fetch_m();
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB0:                  /* ORA */
            A |= A;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB1:                  /* ORB */
            A |= B;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB2:                  /* ORC */
            A |= C;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB3:                  /* ORD */
            A |= D;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB4:                  /* ORE */
            A |= E;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB5:                  /* ORH */
            A |= H;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB6:                  /* ORL */
            A |= L;
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB7:                  /* ORM */
            A |= fetch_m();
            setflag4(A);
            A &= BYTE_R;
            break;

        case 0xB8:                  /* CPA */
            DAR -= A;
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xB9:                  /* CPB */
            DAR -= B;
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xBA:                  /* CPC */
            DAR -= C;
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xBB:                  /* CPD */
            DAR -= D;
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xBC:                  /* CPE */
            DAR -= E;
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xBD:                  /* CPH */
            DAR -= H;
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xBE:                  /* CPL */
            DAR -= L;
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xBF:                  /* CPM */
            DAR -= fetch_m();
            setflag4(DAR);
            A &= BYTE_R;
            break;

        case 0xC0:                  /* NOP */
            break;

        case 0xC1:                  /* LAB */
            A = B;
            A &= BYTE_R;
            break;

        case 0xC2:                  /* LAC */
            A = C;
            A &= BYTE_R;
            break;

        case 0xC3:                  /* LAD */
            A = D;
            A &= BYTE_R;
            break;

        case 0xC4:                  /* LAE */
            A = E;
            A &= BYTE_R;
            break;

        case 0xC5:                  /* LAH */
            A = H;
            A &= BYTE_R;
            break;

        case 0xC6:                  /* LAL */
            A = L;
            A &= BYTE_R;
            break;

        case 0xC7:                  /* LAM */
            A = FETCH_M();
            A &= BYTE_R;
            break;

        case 0xC8:                  /* LBA */
            B = A;
            B &= BYTE_R;
            break;

        case 0xC9:                  /* LBB */
            B = B;
            B &= BYTE_R;
            break;

        case 0xCA:                  /* LBC */
            B = C;
            B &= BYTE_R;
            break;

        case 0xCB:                  /* LBD */
            B = D;
            B &= BYTE_R;
            break;

        case 0xCC:                  /* LBE */
            B = E;
            B &= BYTE_R;
            break;

        case 0xCD:                  /* LBH */
            B = H;
            B &= BYTE_R;
            break;

        case 0xCE:                  /* LBL */
            B = L;
            B &= BYTE_R;
            break;

        case 0xCF:                  /* LBM */
            B = FETCH_M();
            B &= BYTE_R;
            break;

        case 0xD0:                  /* LCA */
            C = A;
            C &= BYTE_R;
            break;

        case 0xD1:                  /* LCB */
            C = B;
            C &= BYTE_R;
            break;

        case 0xD2:                  /* LCC */
            C = C;
            C &= BYTE_R;
            break;

        case 0xD3:                  /* LCD */
            C = D;
            C &= BYTE_R;
            break;

        case 0xD4:                  /* LCE */
            C = E;
            C &= BYTE_R;
            break;

        case 0xD5:                  /* LCH */
            C = H;
            C &= BYTE_R;
            break;

        case 0xD6:                  /* LCL */
            C = L;
            C &= BYTE_R;
            break;

        case 0xD7:                  /* LCM */
            C = FETCH_M();
            C &= BYTE_R;
            break;

        case 0xD8:                  /* LDA */
            D = A;
            D &= BYTE_R;
            break;

        case 0xD9:                  /* LDB */
            D = B;
            D &= BYTE_R;
            break;

        case 0xDA:                  /* LDC */
            D = C;
            D &= BYTE_R;
            break;

        case 0xDB:                  /* LDD */
            D = D;
            D &= BYTE_R;
            break;

        case 0xDC:                  /* LDE */
            D = E;
            D &= BYTE_R;
            break;

        case 0xDD:                  /* LDH */
            D = H;
            D &= BYTE_R;
            break;

        case 0xDE:                  /* LDL */
            D = L;
            D &= BYTE_R;
            break;

        case 0xDF:                  /* LDM */
            D = FETCH_M();
            D &= BYTE_R;
            break;

        case 0xE0:                  /* LEA */
            E = A;
            E &= BYTE_R;
            break;

        case 0xE1:                  /* LEB */
            E = B;
            E &= BYTE_R;
            break;

        case 0xE2:                  /* LEC */
            E = C;
            E &= BYTE_R;
            break;

        case 0xE3:                  /* LED */
            E = D;
            E &= BYTE_R;
            break;

        case 0xE4:                  /* LEE */
            E = E;
            E &= BYTE_R;
            break;

        case 0xE5:                  /* LEH */
            E = H;
            E &= BYTE_R;
            break;

        case 0xE6:                  /* LEL */
            E = L;
            E &= BYTE_R;
            break;

        case 0xE7:                  /* LEM */
            E = FETCH_M();
            E &= BYTE_R;
            break;

        case 0xE8:                  /* LHA */
            H = A;
            H &= BYTE_R;
            break;

        case 0xE9:                  /* LHB */
            H = B;
            H &= BYTE_R;
            break;

        case 0xEA:                  /* LHC */
            H = C;
            H &= BYTE_R;
            break;

        case 0xEB:                  /* LHD */
            H = D;
            H &= BYTE_R;
            break;

        case 0xEC:                  /* LHE */
            H = E;
            H &= BYTE_R;
            break;

        case 0xED:                  /* LHH */
            H = H;
            H &= BYTE_R;
            break;

        case 0xEE:                  /* LHL */
            H = L;
            H &= BYTE_R;
            break;

        case 0xEF:                  /* LHM */
            H = FETCH_M();
            H &= BYTE_R;
            break;

        case 0xF0:                  /* LLA */
            L = A;
            L &= BYTE_R;
            break;

        case 0xF1:                  /* LLB */
            L = B;
            L &= BYTE_R;
            break;

        case 0xF2:                  /* LLC */
            L = C;
            L &= BYTE_R;
            break;

        case 0xF3:                  /* LLD */
            L = D;
            L &= BYTE_R;
            break;

        case 0xF4:                  /* LLE */
            L = E;
            L &= BYTE_R;
            break;

        case 0xF5:                  /* LLH */
            L = H;
            L &= BYTE_R;
            break;

        case 0xF6:                  /* LLL */
            L = L;
            L &= BYTE_R;
            break;

        case 0xF7:                  /* LLM */
            L = FETCH_M();
            L &= BYTE_R;
            break;

        case 0xF8:                  /* LMA */
            store_m(A);
            break;

        case 0xF9:                  /* LMB */
            store_m(B);
            break;

        case 0xFA:                  /* LMC */
            store_m(C);
            break;

        case 0xFB:                  /* LMD */
            store_m(D);
            break;

        case 0xFC:                  /* LME */
            store_m(E);
            break;

        case 0xFD:                  /* LMH */
            store_m(H);
            break;

        case 0xFE:                  /* LML */
            store_m(L);
            break;

        case 0xFF:                  /* LMM */
            val = FETCH_M();
            store_m(val);
            break;

        default:                    /* undefined opcode */ 
            if (i8008_unit.flags & UNIT_OPSTOP) {
                reason = STOP_OPCODE;
                PC--;
            }
            break;
        }
    }

/* Simulation halted */

    saved_PC = PC;
    return reason;
}

/* store byte to (HL) */
void store_m(uint32 val)
{
    DAR = (H << 8) + L;
    DAR &= WORD_R14;
    ret get_mword(DAR);
}

/* get byte from (HL) */
uint32 fetch_m(void)
{
    DAR = (H << 8) + L;
    DAR &= WORD_R14;
    put_mword(DAR, val);
}

/* dump the registers */
void dumpregs(void)
{
    sim_printf("  A=%02X B=%02X C=%02X D=%04X E=%02X H=%04X L=%02X\n",
    A, B, C, D, E, H, L);
    sim_printf("    CF=%d ZF=%d SF=%d PF=%d\n",
        CF, ZF, SF, PF);
}

/* fetch an instruction or byte */
int32 fetch_byte(int32 flag)
{
    uint32 val;

    val = get_mbyte(PC) & 0xFF;         /* fetch byte */
    if (i8008_dev.dctrl & DEBUG_asm || uptr->flags & UNIT_TRACE) {  /* display source code */
        switch (flag) {
            case 0:                     /* opcode fetch */
                sim_printf("OP=%02X        %04X %s", val,  PC, opcode[val]);
                break;
            case 1:                     /* byte operand fetch */
                sim_printf("0%02XH", val);
                break;
        }
    }
    PC = (PC + 1) & ADDRMASK;           /* increment PC */
    val &= BYTE_R;
    return val;
}

/* fetch a word */
int32 fetch_word(void)
{
    uint16 val;

    val = get_mbyte(PC) & BYTE_R;       /* fetch low byte */
    val |= get_mbyte(PC + 1) << 8;      /* fetch high byte */
    if (i8008_dev.dctrl & DEBUG_asm || uptr->flags & UNIT_TRACE)   /* display source code */
        sim_printf("0%04XH", val);
    PC = (PC + 2) & ADDRMASK;           /* increment PC */
    val &= WORD_R14;
    return val;
}

/* push a word to the stack frame */
void push_word(uint16 val)
{
    stack_frame[SP] = val;
    SP++;
    if (SP == 8)
        SP = 0;
}

/* pop a word from the stack frame */
uint16 pop_word(void)
{
    SP--;
    if (SP < 0)
        SP = 7;
    return stack_frame[SP];
}


/* Set the <C>arry, <S>ign, <Z>ero and <O>verflow flags following
   an operation on 'reg'.
*/

void setflag4(int32 reg)
{
    if (reg & 0x100)
        CF = 1;
    else
        CF = 0;
    if (reg & 0x80)
        SF = 0;
    else
        SF = 1;
    if ((reg & BYTE_R) == 0)
        ZF = 1;
    else
        ZF = 0;
    parity(reg);
}

/* Set the <C>arry, <S>ign and <Z>ero flags following
   an operation on 'reg'.
*/

void setflag3(int32 reg)
{
    CF = 0;
    if (reg & 0x80)
        SF = 0;
    else
        SF = 1;
    if ((reg & BYTE_R) == 0)
        ZF = 1;
    else
        ZF = 0;
    parity(reg);
}

/* Set the Parity (PF) flag based on parity of 'reg', i.e., number
of bits on even: PF=1, else PF=0
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
        PF = 0;
    else
        PF = 1;
}



/* Reset routine */

t_stat i8008_reset (DEVICE *dptr)
{
    int i;

    CF = SF = ZF = PF = 0;
    saved_PC = 0;
    int_req = 0;
    for (i = 0; i < 7; i++)
        stack_frame[i] = 0;
    sim_brk_types = sim_brk_dflt = SWMASK ('E');
    sim_printf("   8008: Reset\n");
    return SCPE_OK;
}

/* Memory examine */

t_stat i8008_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE) 
        return SCPE_NXM;
    if (vptr != NULL) 
        *vptr = get_mbyte(addr);
    return SCPE_OK;
}

/* Memory deposit */

t_stat i8008_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
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

int32 sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
    int32 i, addr = 0, cnt = 0;

    if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
    addr = saved_PC;
    while ((i = getc (fileref)) != EOF) {
        put_mbyte(addr, i);
        addr++;
        cnt++;
    }                                   /* end while */
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

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
    int32 cflag, c1, c2, inst, adr;

    cflag = (uptr == NULL) || (uptr == &i8008_unit);
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

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
    int32 cflag, i = 0, j, r;
    char gbuf[CBUFSIZE];

    cflag = (uptr == NULL) || (uptr == &i8008_unit);
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
