/*  altairz80_sys.c: MITS Altair system interface

    Copyright (c) 2002-2014, Peter Schorn

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
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
    IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
    CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of Peter Schorn shall not
    be used in advertising or otherwise to promote the sale, use or other dealings
    in this Software without prior written authorization from Peter Schorn.

    Based on work by Charles E Owen (c) 1997
    Disassembler from Marat Fayzullin ((c) 1995, 1996, 1997 - Commercial use prohibited)

    03/27/14 -- MWD Add MITS Hard Disk device (mhdsk_dev)
*/

#include "m68k.h"
#include <ctype.h>

#define SIM_EMAX 6

extern UNIT cpu_unit;
extern REG cpu_reg[];
extern DEVICE cpu_dev;
extern DEVICE sio_dev;
extern DEVICE simh_device;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE dsk_dev;
extern DEVICE mhdsk_dev;
extern DEVICE hdsk_dev;
extern DEVICE net_dev;

extern DEVICE mfdc_dev;
extern DEVICE fw2_dev;
extern DEVICE fif_dev;
extern DEVICE vfdhd_dev;
extern DEVICE mdsa_dev;
extern DEVICE mdsad_dev;
extern DEVICE nsfpb_dev;
extern DEVICE disk1a_dev;
extern DEVICE disk2_dev;
extern DEVICE disk3_dev;
extern DEVICE selchan_dev;
extern DEVICE ss1_dev;
extern DEVICE if3_dev;
extern DEVICE i8272_dev;
extern DEVICE mdriveh_dev;
extern DEVICE switchcpu_dev;

extern DEVICE adcs6_dev;
extern DEVICE hdc1001_dev;

extern DEVICE tarbell_dev;

extern DEVICE cromfdc_dev;
extern DEVICE wd179x_dev;
extern DEVICE n8vem_dev;
extern DEVICE wdi2_dev;

extern DEVICE scp300f_dev;

extern long disasm (unsigned char *data, char *output, int segsize, long offset);

void prepareMemoryAccessMessage(const t_addr loc);
void prepareInstructionMessage(const t_addr loc, const uint32 op);
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw);
t_stat parse_sym(CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw);

t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* SCP data structures
    sim_name            simulator name string
    sim_PC              pointer to saved PC register descriptor
    sim_emax            number of words needed for examine
    sim_devices         array of pointers to simulated devices
    sim_stop_messages   array of pointers to stop messages
*/

char        sim_name[]      = "Altair 8800 (Z80)";
REG         *sim_PC         = &cpu_reg[CPU_INDEX_8080];
int32       sim_emax        = SIM_EMAX;
DEVICE      *sim_devices[]  = {
    /* AltairZ80 Devices */
    &cpu_dev, &sio_dev, &simh_device, &ptr_dev, &ptp_dev, &dsk_dev,
    &mhdsk_dev, &hdsk_dev, &net_dev,
    /* Advanced Digital (ADC) Devices */
    &adcs6_dev,
    &hdc1001_dev,
    /* Compupro Devices */
    &disk1a_dev, &disk2_dev, &disk3_dev, &ss1_dev, &mdriveh_dev, &selchan_dev, &if3_dev,
    /* Cromemco Devices */
    &cromfdc_dev,
    /* IMSAI Devices */
    &fif_dev,
    /* Micropolis Devices */
    &mfdc_dev,
    /* North Star Devices */
    &mdsa_dev, &mdsad_dev,
    /* Seattle Computer Products Devices */
    &scp300f_dev,
    /* Tarbell Devices */
    &tarbell_dev,
    /* Vector Graphic Devices */
    &fw2_dev, &vfdhd_dev,
    /* Single-Board Computers */
    &n8vem_dev,
    /* Floppy Controller Cores */
    &i8272_dev, &wd179x_dev,
    NULL
};

static char memoryAccessMessage[256];
static char instructionMessage[256];
const char *sim_stop_messages[] = {
    "HALT instruction",
    "Breakpoint",
    memoryAccessMessage,
    instructionMessage,
    "Invalid Opcode"
};

static const char *const Mnemonics8080[] = {
/*  0/8             1/9             2/A             3/B             4/C             5/D             6/E             7/F                         */
    "NOP",          "LXI B,#h",     "STAX B",       "INX B",        "INR B",        "DCR B",        "MVI B,*h",     "RLC",          /*  00-07   */
    "_NOP",         "DAD B",        "LDAX B",       "DCX B",        "INR C",        "DCR C",        "MVI C,*h",     "RRC",          /*  08-0f   */
    "_NOP",         "LXI D,#h",     "STAX D",       "INX D",        "INR D",        "DCR D",        "MVI D,*h",     "RAL",          /*  10-17   */
    "_NOP",         "DAD D",        "LDAX D",       "DCX D",        "INR E",        "DCR E",        "MVI E,*h",     "RAR",          /*  18-1f   */
    "_NOP",         "LXI H,#h",     "SHLD #h",      "INX H",        "INR H",        "DCR H",        "MVI H,*h",     "DAA",          /*  20-27   */
    "_NOP",         "DAD H",        "LHLD #h",      "DCX H",        "INR L",        "DCR L",        "MVI L,*h",     "CMA",          /*  28-2f   */
    "_NOP",         "LXI SP,#h",    "STA #h",       "INX SP",       "INR M",        "DCR M",        "MVI M,*h",     "STC",          /*  30-37   */
    "_NOP",         "DAD SP",       "LDA #h",       "DCX SP",       "INR A",        "DCR A",        "MVI A,*h",     "CMC",          /*  38-3f   */
    "MOV B,B",      "MOV B,C",      "MOV B,D",      "MOV B,E",      "MOV B,H",      "MOV B,L",      "MOV B,M",      "MOV B,A",      /*  40-47   */
    "MOV C,B",      "MOV C,C",      "MOV C,D",      "MOV C,E",      "MOV C,H",      "MOV C,L",      "MOV C,M",      "MOV C,A",      /*  48-4f   */
    "MOV D,B",      "MOV D,C",      "MOV D,D",      "MOV D,E",      "MOV D,H",      "MOV D,L",      "MOV D,M",      "MOV D,A",      /*  50-57   */
    "MOV E,B",      "MOV E,C",      "MOV E,D",      "MOV E,E",      "MOV E,H",      "MOV E,L",      "MOV E,M",      "MOV E,A",      /*  58-5f   */
    "MOV H,B",      "MOV H,C",      "MOV H,D",      "MOV H,E",      "MOV H,H",      "MOV H,L",      "MOV H,M",      "MOV H,A",      /*  60-67   */
    "MOV L,B",      "MOV L,C",      "MOV L,D",      "MOV L,E",      "MOV L,H",      "MOV L,L",      "MOV L,M",      "MOV L,A",      /*  68-6f   */
    "MOV M,B",      "MOV M,C",      "MOV M,D",      "MOV M,E",      "MOV M,H",      "MOV M,L",      "HLT",          "MOV M,A",      /*  70-77   */
    "MOV A,B",      "MOV A,C",      "MOV A,D",      "MOV A,E",      "MOV A,H",      "MOV A,L",      "MOV A,M",      "MOV A,A",      /*  78-7f   */
    "ADD B",        "ADD C",        "ADD D",        "ADD E",        "ADD H",        "ADD L",        "ADD M",        "ADD A",        /*  80-87   */
    "ADC B",        "ADC C",        "ADC D",        "ADC E",        "ADC H",        "ADC L",        "ADC M",        "ADC A",        /*  88-8f   */
    "SUB B",        "SUB C",        "SUB D",        "SUB E",        "SUB H",        "SUB L",        "SUB M",        "SUB A",        /*  90-97   */
    "SBB B",        "SBB C",        "SBB D",        "SBB E",        "SBB H",        "SBB L",        "SBB M",        "SBB A",        /*  98-9f   */
    "ANA B",        "ANA C",        "ANA D",        "ANA E",        "ANA H",        "ANA L",        "ANA M",        "ANA A",        /*  a0-a7   */
    "XRA B",        "XRA C",        "XRA D",        "XRA E",        "XRA H",        "XRA L",        "XRA M",        "XRA A",        /*  a8-af   */
    "ORA B",        "ORA C",        "ORA D",        "ORA E",        "ORA H",        "ORA L",        "ORA M",        "ORA A",        /*  b0-b7   */
    "CMP B",        "CMP C",        "CMP D",        "CMP E",        "CMP H",        "CMP L",        "CMP M",        "CMP A",        /*  b8-bf   */
    "RNZ",          "POP B",        "JNZ #h",       "JMP #h",       "CNZ #h",       "PUSH B",       "ADI *h",       "RST 0",        /*  c0-c7   */
    "RZ",           "RET",          "JZ #h",        "_JMP #h",      "CZ #h",        "CALL #h",      "ACI *h",       "RST 1",        /*  c8-cf   */
    "RNC",          "POP D",        "JNC #h",       "OUT *h",       "CNC #h",       "PUSH D",       "SUI *h",       "RST 2",        /*  d0-d7   */
    "RC",           "_RET",         "JC #h",        "IN *h",        "CC #h",        "_CALL #h",     "SBI *h",       "RST 3",        /*  d8-df   */
    "RPO",          "POP H",        "JPO #h",       "XTHL",         "CPO #h",       "PUSH H",       "ANI *h",       "RST 4",        /*  e0-e7   */
    "RPE",          "PCHL",         "JPE #h",       "XCHG",         "CPE #h",       "_CALL #h",     "XRI *h",       "RST 5",        /*  e8-ef   */
    "RP",           "POP PSW",      "JP #h",        "DI",           "CP #h",        "PUSH PSW",     "ORI *h",       "RST 6",        /*  f0-f7   */
    "RM",           "SPHL",         "JM #h",        "EI",           "CM #h",        "_CALL #h",     "CPI *h",       "RST 7"         /*  f8-ff   */
};

static const char *const MnemonicsZ80[256] = {
/*  0/8             1/9             2/A             3/B             4/C             5/D             6/E             7/F                         */
    "NOP",          "LD BC,#h",     "LD (BC),A",    "INC BC",       "INC B",        "DEC B",        "LD B,*h",      "RLCA",         /*  00-07   */
    "EX AF,AF'",    "ADD HL,BC",    "LD A,(BC)",    "DEC BC",       "INC C",        "DEC C",        "LD C,*h",      "RRCA",         /*  08-0f   */
    "DJNZ $h",      "LD DE,#h",     "LD (DE),A",    "INC DE",       "INC D",        "DEC D",        "LD D,*h",      "RLA",          /*  10-17   */
    "JR $h",        "ADD HL,DE",    "LD A,(DE)",    "DEC DE",       "INC E",        "DEC E",        "LD E,*h",      "RRA",          /*  18-1f   */
    "JR NZ,$h",     "LD HL,#h",     "LD (#h),HL",   "INC HL",       "INC H",        "DEC H",        "LD H,*h",      "DAA",          /*  20-27   */
    "JR Z,$h",      "ADD HL,HL",    "LD HL,(#h)",   "DEC HL",       "INC L",        "DEC L",        "LD L,*h",      "CPL",          /*  28-2f   */
    "JR NC,$h",     "LD SP,#h",     "LD (#h),A",    "INC SP",       "INC (HL)",     "DEC (HL)",     "LD (HL),*h",   "SCF",          /*  30-37   */
    "JR C,$h",      "ADD HL,SP",    "LD A,(#h)",    "DEC SP",       "INC A",        "DEC A",        "LD A,*h",      "CCF",          /*  38-3f   */
    "LD B,B",       "LD B,C",       "LD B,D",       "LD B,E",       "LD B,H",       "LD B,L",       "LD B,(HL)",    "LD B,A",       /*  40-47   */
    "LD C,B",       "LD C,C",       "LD C,D",       "LD C,E",       "LD C,H",       "LD C,L",       "LD C,(HL)",    "LD C,A",       /*  48-4f   */
    "LD D,B",       "LD D,C",       "LD D,D",       "LD D,E",       "LD D,H",       "LD D,L",       "LD D,(HL)",    "LD D,A",       /*  50-57   */
    "LD E,B",       "LD E,C",       "LD E,D",       "LD E,E",       "LD E,H",       "LD E,L",       "LD E,(HL)",    "LD E,A",       /*  58-5f   */
    "LD H,B",       "LD H,C",       "LD H,D",       "LD H,E",       "LD H,H",       "LD H,L",       "LD H,(HL)",    "LD H,A",       /*  60-67   */
    "LD L,B",       "LD L,C",       "LD L,D",       "LD L,E",       "LD L,H",       "LD L,L",       "LD L,(HL)",    "LD L,A",       /*  68-6f   */
    "LD (HL),B",    "LD (HL),C",    "LD (HL),D",    "LD (HL),E",    "LD (HL),H",    "LD (HL),L",    "HALT",         "LD (HL),A",    /*  70-77   */
    "LD A,B",       "LD A,C",       "LD A,D",       "LD A,E",       "LD A,H",       "LD A,L",       "LD A,(HL)",    "LD A,A",       /*  78-7f   */
    "ADD A,B",      "ADD A,C",      "ADD A,D",      "ADD A,E",      "ADD A,H",      "ADD A,L",      "ADD A,(HL)",   "ADD A,A",      /*  80-87   */
    "ADC A,B",      "ADC A,C",      "ADC A,D",      "ADC A,E",      "ADC A,H",      "ADC A,L",      "ADC A,(HL)",   "ADC A,A",      /*  88-8f   */
    "SUB B",        "SUB C",        "SUB D",        "SUB E",        "SUB H",        "SUB L",        "SUB (HL)",     "SUB A",        /*  90-97   */
    "SBC A,B",      "SBC A,C",      "SBC A,D",      "SBC A,E",      "SBC A,H",      "SBC A,L",      "SBC A,(HL)",   "SBC A,A",      /*  98-9f   */
    "AND B",        "AND C",        "AND D",        "AND E",        "AND H",        "AND L",        "AND (HL)",     "AND A",        /*  a0-a7   */
    "XOR B",        "XOR C",        "XOR D",        "XOR E",        "XOR H",        "XOR L",        "XOR (HL)",     "XOR A",        /*  a8-af   */
    "OR B",         "OR C",         "OR D",         "OR E",         "OR H",         "OR L",         "OR (HL)",      "OR A",         /*  b0-b7   */
    "CP B",         "CP C",         "CP D",         "CP E",         "CP H",         "CP L",         "CP (HL)",      "CP A",         /*  b8-bf   */
    "RET NZ",       "POP BC",       "JP NZ,#h",     "JP #h",        "CALL NZ,#h",   "PUSH BC",      "ADD A,*h",     "RST 00h",      /*  c0-c7   */
    "RET Z",        "RET",          "JP Z,#h",      "PFX_CB",       "CALL Z,#h",    "CALL #h",      "ADC A,*h",     "RST 08h",      /*  c8-cf   */
    "RET NC",       "POP DE",       "JP NC,#h",     "OUT (*h),A",   "CALL NC,#h",   "PUSH DE",      "SUB *h",       "RST 10h",      /*  d0-d7   */
    "RET C",        "EXX",          "JP C,#h",      "IN A,(*h)",    "CALL C,#h",    "PFX_DD",       "SBC A,*h",     "RST 18h",      /*  d8-df   */
    "RET PO",       "POP HL",       "JP PO,#h",     "EX (SP),HL",   "CALL PO,#h",   "PUSH HL",      "AND *h",       "RST 20h",      /*  e0-e7   */
    "RET PE",       "LD PC,HL",     "JP PE,#h",     "EX DE,HL",     "CALL PE,#h",   "PFX_ED",       "XOR *h",       "RST 28h",      /*  e8-ef   */
    "RET P",        "POP AF",       "JP P,#h",      "DI",           "CALL P,#h",    "PUSH AF",      "OR *h",        "RST 30h",      /*  f0-f7   */
    "RET M",        "LD SP,HL",     "JP M,#h",      "EI",           "CALL M,#h",    "PFX_FD",       "CP *h",        "RST 38h"       /*  f8-ff   */
};

static const char *const MnemonicsCB[256] = {
/*  0/8             1/9             2/A             3/B             4/C             5/D             6/E             7/F                         */
    "RLC B",        "RLC C",        "RLC D",        "RLC E",        "RLC H",        "RLC L",        "RLC (HL)",     "RLC A",        /*  00-07   */
    "RRC B",        "RRC C",        "RRC D",        "RRC E",        "RRC H",        "RRC L",        "RRC (HL)",     "RRC A",        /*  08-0f   */
    "RL B",         "RL C",         "RL D",         "RL E",         "RL H",         "RL L",         "RL (HL)",      "RL A",         /*  10-17   */
    "RR B",         "RR C",         "RR D",         "RR E",         "RR H",         "RR L",         "RR (HL)",      "RR A",         /*  18-1f   */
    "SLA B",        "SLA C",        "SLA D",        "SLA E",        "SLA H",        "SLA L",        "SLA (HL)",     "SLA A",        /*  20-27   */
    "SRA B",        "SRA C",        "SRA D",        "SRA E",        "SRA H",        "SRA L",        "SRA (HL)",     "SRA A",        /*  28-2f   */
    "SLL B",        "SLL C",        "SLL D",        "SLL E",        "SLL H",        "SLL L",        "SLL (HL)",     "SLL A",        /*  30-37   */
    "SRL B",        "SRL C",        "SRL D",        "SRL E",        "SRL H",        "SRL L",        "SRL (HL)",     "SRL A",        /*  38-3f   */
    "BIT 0,B",      "BIT 0,C",      "BIT 0,D",      "BIT 0,E",      "BIT 0,H",      "BIT 0,L",      "BIT 0,(HL)",   "BIT 0,A",      /*  40-47   */
    "BIT 1,B",      "BIT 1,C",      "BIT 1,D",      "BIT 1,E",      "BIT 1,H",      "BIT 1,L",      "BIT 1,(HL)",   "BIT 1,A",      /*  48-4f   */
    "BIT 2,B",      "BIT 2,C",      "BIT 2,D",      "BIT 2,E",      "BIT 2,H",      "BIT 2,L",      "BIT 2,(HL)",   "BIT 2,A",      /*  50-57   */
    "BIT 3,B",      "BIT 3,C",      "BIT 3,D",      "BIT 3,E",      "BIT 3,H",      "BIT 3,L",      "BIT 3,(HL)",   "BIT 3,A",      /*  58-5f   */
    "BIT 4,B",      "BIT 4,C",      "BIT 4,D",      "BIT 4,E",      "BIT 4,H",      "BIT 4,L",      "BIT 4,(HL)",   "BIT 4,A",      /*  60-67   */
    "BIT 5,B",      "BIT 5,C",      "BIT 5,D",      "BIT 5,E",      "BIT 5,H",      "BIT 5,L",      "BIT 5,(HL)",   "BIT 5,A",      /*  68-6f   */
    "BIT 6,B",      "BIT 6,C",      "BIT 6,D",      "BIT 6,E",      "BIT 6,H",      "BIT 6,L",      "BIT 6,(HL)",   "BIT 6,A",      /*  70-77   */
    "BIT 7,B",      "BIT 7,C",      "BIT 7,D",      "BIT 7,E",      "BIT 7,H",      "BIT 7,L",      "BIT 7,(HL)",   "BIT 7,A",      /*  78-7f   */
    "RES 0,B",      "RES 0,C",      "RES 0,D",      "RES 0,E",      "RES 0,H",      "RES 0,L",      "RES 0,(HL)",   "RES 0,A",      /*  80-87   */
    "RES 1,B",      "RES 1,C",      "RES 1,D",      "RES 1,E",      "RES 1,H",      "RES 1,L",      "RES 1,(HL)",   "RES 1,A",      /*  88-8f   */
    "RES 2,B",      "RES 2,C",      "RES 2,D",      "RES 2,E",      "RES 2,H",      "RES 2,L",      "RES 2,(HL)",   "RES 2,A",      /*  90-97   */
    "RES 3,B",      "RES 3,C",      "RES 3,D",      "RES 3,E",      "RES 3,H",      "RES 3,L",      "RES 3,(HL)",   "RES 3,A",      /*  98-9f   */
    "RES 4,B",      "RES 4,C",      "RES 4,D",      "RES 4,E",      "RES 4,H",      "RES 4,L",      "RES 4,(HL)",   "RES 4,A",      /*  a0-a7   */
    "RES 5,B",      "RES 5,C",      "RES 5,D",      "RES 5,E",      "RES 5,H",      "RES 5,L",      "RES 5,(HL)",   "RES 5,A",      /*  a8-af   */
    "RES 6,B",      "RES 6,C",      "RES 6,D",      "RES 6,E",      "RES 6,H",      "RES 6,L",      "RES 6,(HL)",   "RES 6,A",      /*  b0-b7   */
    "RES 7,B",      "RES 7,C",      "RES 7,D",      "RES 7,E",      "RES 7,H",      "RES 7,L",      "RES 7,(HL)",   "RES 7,A",      /*  b8-bf   */
    "SET 0,B",      "SET 0,C",      "SET 0,D",      "SET 0,E",      "SET 0,H",      "SET 0,L",      "SET 0,(HL)",   "SET 0,A",      /*  c0-c7   */
    "SET 1,B",      "SET 1,C",      "SET 1,D",      "SET 1,E",      "SET 1,H",      "SET 1,L",      "SET 1,(HL)",   "SET 1,A",      /*  c8-cf   */
    "SET 2,B",      "SET 2,C",      "SET 2,D",      "SET 2,E",      "SET 2,H",      "SET 2,L",      "SET 2,(HL)",   "SET 2,A",      /*  d0-d7   */
    "SET 3,B",      "SET 3,C",      "SET 3,D",      "SET 3,E",      "SET 3,H",      "SET 3,L",      "SET 3,(HL)",   "SET 3,A",      /*  d8-df   */
    "SET 4,B",      "SET 4,C",      "SET 4,D",      "SET 4,E",      "SET 4,H",      "SET 4,L",      "SET 4,(HL)",   "SET 4,A",      /*  e0-e7   */
    "SET 5,B",      "SET 5,C",      "SET 5,D",      "SET 5,E",      "SET 5,H",      "SET 5,L",      "SET 5,(HL)",   "SET 5,A",      /*  e8-ef   */
    "SET 6,B",      "SET 6,C",      "SET 6,D",      "SET 6,E",      "SET 6,H",      "SET 6,L",      "SET 6,(HL)",   "SET 6,A",      /*  f0-f7   */
    "SET 7,B",      "SET 7,C",      "SET 7,D",      "SET 7,E",      "SET 7,H",      "SET 7,L",      "SET 7,(HL)",   "SET 7,A"       /*  f8-ff   */
};

static const char *const MnemonicsED[256] = {
/*  0/8             1/9             2/A             3/B             4/C             5/D             6/E             7/F                         */
    "DB EDh,00h",   "DB EDh,01h",   "DB EDh,02h",   "DB EDh,03h",   "DB EDh,04h",   "DB EDh,05h",   "DB EDh,06h",   "DB EDh,07h",   /*  00-07   */
    "DB EDh,08h",   "DB EDh,09h",   "DB EDh,0Ah",   "DB EDh,0Bh",   "DB EDh,0Ch",   "DB EDh,0Dh",   "DB EDh,0Eh",   "DB EDh,0Fh",   /*  08-0f   */
    "DB EDh,10h",   "DB EDh,11h",   "DB EDh,12h",   "DB EDh,13h",   "DB EDh,14h",   "DB EDh,15h",   "DB EDh,16h",   "DB EDh,17h",   /*  10-17   */
    "DB EDh,18h",   "DB EDh,19h",   "DB EDh,1Ah",   "DB EDh,1Bh",   "DB EDh,1Ch",   "DB EDh,1Dh",   "DB EDh,1Eh",   "DB EDh,1Fh",   /*  18-1f   */
    "DB EDh,20h",   "DB EDh,21h",   "DB EDh,22h",   "DB EDh,23h",   "DB EDh,24h",   "DB EDh,25h",   "DB EDh,26h",   "DB EDh,27h",   /*  20-27   */
    "DB EDh,28h",   "DB EDh,29h",   "DB EDh,2Ah",   "DB EDh,2Bh",   "DB EDh,2Ch",   "DB EDh,2Dh",   "DB EDh,2Eh",   "DB EDh,2Fh",   /*  28-2f   */
    "DB EDh,30h",   "DB EDh,31h",   "DB EDh,32h",   "DB EDh,33h",   "DB EDh,34h",   "DB EDh,35h",   "DB EDh,36h",   "DB EDh,37h",   /*  30-37   */
    "DB EDh,38h",   "DB EDh,39h",   "DB EDh,3Ah",   "DB EDh,3Bh",   "DB EDh,3Ch",   "DB EDh,3Dh",   "DB EDh,3Eh",   "DB EDh,3Fh",   /*  38-3f   */
    "IN B,(C)",     "OUT (C),B",    "SBC HL,BC",    "LD (#h),BC",   "NEG",          "RETN",         "IM 0",         "LD I,A",       /*  40-47   */
    "IN C,(C)",     "OUT (C),C",    "ADC HL,BC",    "LD BC,(#h)",   "DB EDh,4Ch",   "RETI",         "DB EDh,4Eh",   "LD R,A",       /*  48-4f   */
    "IN D,(C)",     "OUT (C),D",    "SBC HL,DE",    "LD (#h),DE",   "DB EDh,54h",   "DB EDh,55h",   "IM 1",         "LD A,I",       /*  50-57   */
    "IN E,(C)",     "OUT (C),E",    "ADC HL,DE",    "LD DE,(#h)",   "DB EDh,5Ch",   "DB EDh,5Dh",   "IM 2",         "LD A,R",       /*  58-5f   */
    "IN H,(C)",     "OUT (C),H",    "SBC HL,HL",    "LD (#h),HL",   "DB EDh,64h",   "DB EDh,65h",   "DB EDh,66h",   "RRD",          /*  60-67   */
    "IN L,(C)",     "OUT (C),L",    "ADC HL,HL",    "LD HL,(#h)",   "DB EDh,6Ch",   "DB EDh,6Dh",   "DB EDh,6Eh",   "RLD",          /*  68-6f   */
    "IN F,(C)",     "DB EDh,71h",   "SBC HL,SP",    "LD (#h),SP",   "DB EDh,74h",   "DB EDh,75h",   "DB EDh,76h",   "DB EDh,77h",   /*  70-77   */
    "IN A,(C)",     "OUT (C),A",    "ADC HL,SP",    "LD SP,(#h)",   "DB EDh,7Ch",   "DB EDh,7Dh",   "DB EDh,7Eh",   "DB EDh,7Fh",   /*  78-7f   */
    "DB EDh,80h",   "DB EDh,81h",   "DB EDh,82h",   "DB EDh,83h",   "DB EDh,84h",   "DB EDh,85h",   "DB EDh,86h",   "DB EDh,87h",   /*  80-87   */
    "DB EDh,88h",   "DB EDh,89h",   "DB EDh,8Ah",   "DB EDh,8Bh",   "DB EDh,8Ch",   "DB EDh,8Dh",   "DB EDh,8Eh",   "DB EDh,8Fh",   /*  88-8f   */
    "DB EDh,90h",   "DB EDh,91h",   "DB EDh,92h",   "DB EDh,93h",   "DB EDh,94h",   "DB EDh,95h",   "DB EDh,96h",   "DB EDh,97h",   /*  90-97   */
    "DB EDh,98h",   "DB EDh,99h",   "DB EDh,9Ah",   "DB EDh,9Bh",   "DB EDh,9Ch",   "DB EDh,9Dh",   "DB EDh,9Eh",   "DB EDh,9Fh",   /*  98-9f   */
    "LDI",          "CPI",          "INI",          "OUTI",         "DB EDh,A4h",   "DB EDh,A5h",   "DB EDh,A6h",   "DB EDh,A7h",   /*  a0-a7   */
    "LDD",          "CPD",          "IND",          "OUTD",         "DB EDh,ACh",   "DB EDh,ADh",   "DB EDh,AEh",   "DB EDh,AFh",   /*  a8-af   */
    "LDIR",         "CPIR",         "INIR",         "OTIR",         "DB EDh,B4h",   "DB EDh,B5h",   "DB EDh,B6h",   "DB EDh,B7h",   /*  b0-b7   */
    "LDDR",         "CPDR",         "INDR",         "OTDR",         "DB EDh,BCh",   "DB EDh,BDh",   "DB EDh,BEh",   "DB EDh,BFh",   /*  b8-bf   */
    "DB EDh,C0h",   "DB EDh,C1h",   "DB EDh,C2h",   "DB EDh,C3h",   "DB EDh,C4h",   "DB EDh,C5h",   "DB EDh,C6h",   "DB EDh,C7h",   /*  c0-c7   */
    "DB EDh,C8h",   "DB EDh,C9h",   "DB EDh,CAh",   "DB EDh,CBh",   "DB EDh,CCh",   "DB EDh,CDh",   "DB EDh,CEh",   "DB EDh,CFh",   /*  c8-cf   */
    "DB EDh,D0h",   "DB EDh,D1h",   "DB EDh,D2h",   "DB EDh,D3h",   "DB EDh,D4h",   "DB EDh,D5h",   "DB EDh,D6h",   "DB EDh,D7h",   /*  d0-d7   */
    "DB EDh,D8h",   "DB EDh,D9h",   "DB EDh,DAh",   "DB EDh,DBh",   "DB EDh,DCh",   "DB EDh,DDh",   "DB EDh,DEh",   "DB EDh,DFh",   /*  d8-df   */
    "DB EDh,E0h",   "DB EDh,E1h",   "DB EDh,E2h",   "DB EDh,E3h",   "DB EDh,E4h",   "DB EDh,E5h",   "DB EDh,E6h",   "DB EDh,E7h",   /*  e0-e7   */
    "DB EDh,E8h",   "DB EDh,E9h",   "DB EDh,EAh",   "DB EDh,EBh",   "DB EDh,ECh",   "DB EDh,EDh",   "DB EDh,EEh",   "DB EDh,EFh",   /*  e8-ef   */
    "DB EDh,F0h",   "DB EDh,F1h",   "DB EDh,F2h",   "DB EDh,F3h",   "DB EDh,F4h",   "DB EDh,F5h",   "DB EDh,F6h",   "DB EDh,F7h",   /*  f0-f7   */
    "DB EDh,F8h",   "DB EDh,F9h",   "DB EDh,FAh",   "DB EDh,FBh",   "DB EDh,FCh",   "DB EDh,FDh",   "DB EDh,FEh",   "DB EDh,FFh"    /*  f8-ff   */
};

static const char *const MnemonicsXX[256] = {
/*  0/8             1/9             2/A             3/B             4/C             5/D             6/E             7/F                         */
    "NOP",          "LD BC,#h",     "LD (BC),A",    "INC BC",       "INC B",        "DEC B",        "LD B,*h",      "RLCA",         /*  00-07   */
    "EX AF,AF'",    "ADD I%,BC",    "LD A,(BC)",    "DEC BC",       "INC C",        "DEC C",        "LD C,*h",      "RRCA",         /*  08-0f   */
    "DJNZ $h",      "LD DE,#h",     "LD (DE),A",    "INC DE",       "INC D",        "DEC D",        "LD D,*h",      "RLA",          /*  10-17   */
    "JR $h",        "ADD I%,DE",    "LD A,(DE)",    "DEC DE",       "INC E",        "DEC E",        "LD E,*h",      "RRA",          /*  18-1f   */
    "JR NZ,$h",     "LD I%,#h",     "LD (#h),I%",   "INC I%",       "INC I%H",      "DEC I%H",      "LD I%H,*h",    "DAA",          /*  20-27   */
    "JR Z,$h",      "ADD I%,I%",    "LD I%,(#h)",   "DEC I%",       "INC I%L",      "DEC I%L",      "LD I%L,*h",    "CPL",          /*  28-2f   */
    "JR NC,$h",     "LD SP,#h",     "LD (#h),A",    "INC SP",       "INC (I%+^h)",  "DEC (I%+^h)",  "LD (I%+^h),*h","SCF",          /*  30-37   */
    "JR C,$h",      "ADD I%,SP",    "LD A,(#h)",    "DEC SP",       "INC A",        "DEC A",        "LD A,*h",      "CCF",          /*  38-3f   */
    "LD B,B",       "LD B,C",       "LD B,D",       "LD B,E",       "LD B,I%H",     "LD B,I%L",     "LD B,(I%+^h)", "LD B,A",       /*  40-47   */
    "LD C,B",       "LD C,C",       "LD C,D",       "LD C,E",       "LD C,I%H",     "LD C,I%L",     "LD C,(I%+^h)", "LD C,A",       /*  48-4f   */
    "LD D,B",       "LD D,C",       "LD D,D",       "LD D,E",       "LD D,I%H",     "LD D,I%L",     "LD D,(I%+^h)", "LD D,A",       /*  50-57   */
    "LD E,B",       "LD E,C",       "LD E,D",       "LD E,E",       "LD E,I%H",     "LD E,I%L",     "LD E,(I%+^h)", "LD E,A",       /*  58-5f   */
    "LD I%H,B",     "LD I%H,C",     "LD I%H,D",     "LD I%H,E",     "LD I%H,I%H",   "LD I%H,I%L",   "LD H,(I%+^h)", "LD I%H,A",     /*  60-67   */
    "LD I%L,B",     "LD I%L,C",     "LD I%L,D",     "LD I%L,E",     "LD I%L,I%H",   "LD I%L,I%L",   "LD L,(I%+^h)", "LD I%L,A",     /*  68-6f   */
    "LD (I%+^h),B", "LD (I%+^h),C", "LD (I%+^h),D", "LD (I%+^h),E", "LD (I%+^h),H", "LD (I%+^h),L", "HALT",         "LD (I%+^h),A", /*  70-77   */
    "LD A,B",       "LD A,C",       "LD A,D",       "LD A,E",       "LD A,I%H",     "LD A,I%L",     "LD A,(I%+^h)", "LD A,A",       /*  78-7f   */
    "ADD A,B",      "ADD A,C",      "ADD A,D",      "ADD A,E",      "ADD A,I%H",    "ADD A,I%L",    "ADD A,(I%+^h)","ADD A,A",      /*  80-87   */
    "ADC A,B",      "ADC A,C",      "ADC A,D",      "ADC A,E",      "ADC A,I%H",    "ADC A,I%L",    "ADC A,(I%+^h)","ADC A,A",      /*  88-8f   */
    "SUB B",        "SUB C",        "SUB D",        "SUB E",        "SUB I%H",      "SUB I%L",      "SUB (I%+^h)",  "SUB A",        /*  90-97   */
    "SBC A,B",      "SBC A,C",      "SBC A,D",      "SBC A,E",      "SBC A,I%H",    "SBC A,I%L",    "SBC A,(I%+^h)","SBC A,A",      /*  98-9f   */
    "AND B",        "AND C",        "AND D",        "AND E",        "AND I%H",      "AND I%L",      "AND (I%+^h)",  "AND A",        /*  a0-a7   */
    "XOR B",        "XOR C",        "XOR D",        "XOR E",        "XOR I%H",      "XOR I%L",      "XOR (I%+^h)",  "XOR A",        /*  a8-af   */
    "OR B",         "OR C",         "OR D",         "OR E",         "OR I%H",       "OR I%L",       "OR (I%+^h)",   "OR A",         /*  b0-b7   */
    "CP B",         "CP C",         "CP D",         "CP E",         "CP I%H",       "CP I%L",       "CP (I%+^h)",   "CP A",         /*  b8-bf   */
    "RET NZ",       "POP BC",       "JP NZ,#h",     "JP #h",        "CALL NZ,#h",   "PUSH BC",      "ADD A,*h",     "RST 00h",      /*  c8-cf   */
    "RET Z",        "RET",          "JP Z,#h",      "PFX_CB",       "CALL Z,#h",    "CALL #h",      "ADC A,*h",     "RST 08h",      /*  c8-cf   */
    "RET NC",       "POP DE",       "JP NC,#h",     "OUT (*h),A",   "CALL NC,#h",   "PUSH DE",      "SUB *h",       "RST 10h",      /*  d0-d7   */
    "RET C",        "EXX",          "JP C,#h",      "IN A,(*h)",    "CALL C,#h",    "PFX_DD",       "SBC A,*h",     "RST 18h",      /*  d8-df   */
    "RET PO",       "POP I%",       "JP PO,#h",     "EX (SP),I%",   "CALL PO,#h",   "PUSH I%",      "AND *h",       "RST 20h",      /*  e0-e7   */
    "RET PE",       "LD PC,I%",     "JP PE,#h",     "EX DE,I%",     "CALL PE,#h",   "PFX_ED",       "XOR *h",       "RST 28h",      /*  e8-ef   */
    "RET P",        "POP AF",       "JP P,#h",      "DI",           "CALL P,#h",    "PUSH AF",      "OR *h",        "RST 30h",      /*  f0-f7   */
    "RET M",        "LD SP,I%",     "JP M,#h",      "EI",           "CALL M,#h",    "PFX_FD",       "CP *h",        "RST 38h"       /*  f8-ff   */
};

static const char *const MnemonicsXCB[256] = {
/*0/8               1/9             2/A             3/B             4/C             5/D             6/E             7/F                         */
    "RLC B",        "RLC C",        "RLC D",        "RLC E",        "RLC H",        "RLC L",        "RLC (I%@h)",   "RLC A",        /*  00-07   */
    "RRC B",        "RRC C",        "RRC D",        "RRC E",        "RRC H",        "RRC L",        "RRC (I%@h)",   "RRC A",        /*  08-0f   */
    "RL B",         "RL C",         "RL D",         "RL E",         "RL H",         "RL L",         "RL (I%@h)",    "RL A",         /*  10-17   */
    "RR B",         "RR C",         "RR D",         "RR E",         "RR H",         "RR L",         "RR (I%@h)",    "RR A",         /*  18-1f   */
    "SLA B",        "SLA C",        "SLA D",        "SLA E",        "SLA H",        "SLA L",        "SLA (I%@h)",   "SLA A",        /*  20-27   */
    "SRA B",        "SRA C",        "SRA D",        "SRA E",        "SRA H",        "SRA L",        "SRA (I%@h)",   "SRA A",        /*  28-2f   */
    "SLL B",        "SLL C",        "SLL D",        "SLL E",        "SLL H",        "SLL L",        "SLL (I%@h)",   "SLL A",        /*  30-37   */
    "SRL B",        "SRL C",        "SRL D",        "SRL E",        "SRL H",        "SRL L",        "SRL (I%@h)",   "SRL A",        /*  38-3f   */
    "BIT 0,B",      "BIT 0,C",      "BIT 0,D",      "BIT 0,E",      "BIT 0,H",      "BIT 0,L",      "BIT 0,(I%@h)", "BIT 0,A",      /*  40-47   */
    "BIT 1,B",      "BIT 1,C",      "BIT 1,D",      "BIT 1,E",      "BIT 1,H",      "BIT 1,L",      "BIT 1,(I%@h)", "BIT 1,A",      /*  48-4f   */
    "BIT 2,B",      "BIT 2,C",      "BIT 2,D",      "BIT 2,E",      "BIT 2,H",      "BIT 2,L",      "BIT 2,(I%@h)", "BIT 2,A",      /*  50-57   */
    "BIT 3,B",      "BIT 3,C",      "BIT 3,D",      "BIT 3,E",      "BIT 3,H",      "BIT 3,L",      "BIT 3,(I%@h)", "BIT 3,A",      /*  58-5f   */
    "BIT 4,B",      "BIT 4,C",      "BIT 4,D",      "BIT 4,E",      "BIT 4,H",      "BIT 4,L",      "BIT 4,(I%@h)", "BIT 4,A",      /*  60-67   */
    "BIT 5,B",      "BIT 5,C",      "BIT 5,D",      "BIT 5,E",      "BIT 5,H",      "BIT 5,L",      "BIT 5,(I%@h)", "BIT 5,A",      /*  68-6f   */
    "BIT 6,B",      "BIT 6,C",      "BIT 6,D",      "BIT 6,E",      "BIT 6,H",      "BIT 6,L",      "BIT 6,(I%@h)", "BIT 6,A",      /*  70-77   */
    "BIT 7,B",      "BIT 7,C",      "BIT 7,D",      "BIT 7,E",      "BIT 7,H",      "BIT 7,L",      "BIT 7,(I%@h)", "BIT 7,A",      /*  78-7f   */
    "RES 0,B",      "RES 0,C",      "RES 0,D",      "RES 0,E",      "RES 0,H",      "RES 0,L",      "RES 0,(I%@h)", "RES 0,A",      /*  80-87   */
    "RES 1,B",      "RES 1,C",      "RES 1,D",      "RES 1,E",      "RES 1,H",      "RES 1,L",      "RES 1,(I%@h)", "RES 1,A",      /*  88-8f   */
    "RES 2,B",      "RES 2,C",      "RES 2,D",      "RES 2,E",      "RES 2,H",      "RES 2,L",      "RES 2,(I%@h)", "RES 2,A",      /*  90-97   */
    "RES 3,B",      "RES 3,C",      "RES 3,D",      "RES 3,E",      "RES 3,H",      "RES 3,L",      "RES 3,(I%@h)", "RES 3,A",      /*  98-9f   */
    "RES 4,B",      "RES 4,C",      "RES 4,D",      "RES 4,E",      "RES 4,H",      "RES 4,L",      "RES 4,(I%@h)", "RES 4,A",      /*  a0-a7   */
    "RES 5,B",      "RES 5,C",      "RES 5,D",      "RES 5,E",      "RES 5,H",      "RES 5,L",      "RES 5,(I%@h)", "RES 5,A",      /*  a8-af   */
    "RES 6,B",      "RES 6,C",      "RES 6,D",      "RES 6,E",      "RES 6,H",      "RES 6,L",      "RES 6,(I%@h)", "RES 6,A",      /*  b0-b7   */
    "RES 7,B",      "RES 7,C",      "RES 7,D",      "RES 7,E",      "RES 7,H",      "RES 7,L",      "RES 7,(I%@h)", "RES 7,A",      /*  b8-bf   */
    "SET 0,B",      "SET 0,C",      "SET 0,D",      "SET 0,E",      "SET 0,H",      "SET 0,L",      "SET 0,(I%@h)", "SET 0,A",      /*  c0-c7   */
    "SET 1,B",      "SET 1,C",      "SET 1,D",      "SET 1,E",      "SET 1,H",      "SET 1,L",      "SET 1,(I%@h)", "SET 1,A",      /*  c8-cf   */
    "SET 2,B",      "SET 2,C",      "SET 2,D",      "SET 2,E",      "SET 2,H",      "SET 2,L",      "SET 2,(I%@h)", "SET 2,A",      /*  d0-d7   */
    "SET 3,B",      "SET 3,C",      "SET 3,D",      "SET 3,E",      "SET 3,H",      "SET 3,L",      "SET 3,(I%@h)", "SET 3,A",      /*  d8-df   */
    "SET 4,B",      "SET 4,C",      "SET 4,D",      "SET 4,E",      "SET 4,H",      "SET 4,L",      "SET 4,(I%@h)", "SET 4,A",      /*  e0-e7   */
    "SET 5,B",      "SET 5,C",      "SET 5,D",      "SET 5,E",      "SET 5,H",      "SET 5,L",      "SET 5,(I%@h)", "SET 5,A",      /*  e8-ef   */
    "SET 6,B",      "SET 6,C",      "SET 6,D",      "SET 6,E",      "SET 6,H",      "SET 6,L",      "SET 6,(I%@h)", "SET 6,A",      /*  f0-f7   */
    "SET 7,B",      "SET 7,C",      "SET 7,D",      "SET 7,E",      "SET 7,H",      "SET 7,L",      "SET 7,(I%@h)", "SET 7,A"       /*  f8-ff   */
};

void prepareMemoryAccessMessage(const t_addr loc) {
    sprintf(memoryAccessMessage, "Memory access breakpoint [%05xh]", loc);
}

void prepareInstructionMessage(const t_addr loc, const uint32 op) {
    sprintf(instructionMessage, "Instruction \"%s\" breakpoint [%05xh]", chiptype == CHIP_TYPE_8080 ?  Mnemonics8080[op & 0xff] :
            (chiptype == CHIP_TYPE_Z80 ? MnemonicsZ80[op & 0xff] : "???"), loc);
}

/* Ensure that hex number starts with a digit when printed */
static void printHex2(char* string, const uint32 value) {
    sprintf(string, (value <= 0x9f ? "%02X" : "%03X"), value);
}

static void printHex4(char* string, const uint32 value) {
    sprintf(string, (value <= 0x9fff ? "%04X" : "%05X"), value);
}

/*  Symbolic disassembler

    Inputs:
        *val            =   instructions to disassemble
        useZ80Mnemonics =   > 0 iff Z80 mnemonics are to be used
        addr            =   current PC
    Outputs:
        *S              =   output text
        return          =   length of instruction in bytes

    DAsm is Copyright (C) Marat Fayzullin 1995,1996,1997
        You are not allowed to distribute this software
        commercially.

*/

static int32 DAsm(char *S, const uint32 *val, const int32 useZ80Mnemonics, const int32 addr) {
    char R[128], H[10], C = '\0', *P;
    const char *T, *T1;
    uint8 J = 0, Offset = 0;
    uint16 B = 0;

    if (useZ80Mnemonics)
        switch(val[B]) {

            case 0xcb:
                B++;
                T = MnemonicsCB[val[B++]];
                break;

            case 0xed:
                B++;
                T = MnemonicsED[val[B++]];
                break;

            case 0xdd:

            case 0xfd:
                C = (val[B++] == 0xdd) ? 'X' : 'Y';
                if (val[B] == 0xcb) {
                    B++;
                    Offset = val[B++];
                    J = 1;
                    T = MnemonicsXCB[val[B++]];
                }
                else
                    T = MnemonicsXX[val[B++]];
                break;

            default:
                T = MnemonicsZ80[val[B++]];
        }
    else
        T = Mnemonics8080[val[B++]];

    if ( (T1 = strchr(T, '^')) ) {
        strncpy(R, T, T1 - T);
        R[T1 - T] = '\0';
        printHex2(H, val[B++]);
        strlcat(R, H, sizeof (R));
        strlcat(R, T1 + 1, sizeof (R)); /* ok, since T1 is a short sub-string coming from one of the tables */
    }
    else
        strlcpy(R, T, sizeof (R)); /* ok, since T is a short string coming from one of the tables */
    if ( (P = strchr(R, '%')) ) {
        *P = C;
        if ( (P = strchr(P + 1, '%')) )
            *P = C;
    }

    if ( (P = strchr(R, '*')) ) {
        strncpy(S, R, P - R);
        S[P - R] = '\0';
        printHex2(H, val[B++]);
        strcat(S, H);
        strcat(S, P + 1);
    }
    else if ( (P = strchr(R, '@')) ) {
        strncpy(S, R, P - R);
        S[P - R] = '\0';
        if (!J)
            Offset = val[B++];
        strcat(S, Offset & 0x80 ? "-" : "+");
        J = Offset & 0x80 ? 256 - Offset : Offset;
        printHex2(H, J);
        strcat(S, H);
        strcat(S, P + 1);
    }
    else if ( (P = strchr(R, '$')) ) {
        strncpy(S, R, P - R);
        S[P - R] = '\0';
        Offset = val[B++];
        printHex4(H, (addr + 2 + (Offset & 0x80 ? (Offset - 256) : Offset)) & 0xFFFF);
        strcat(S, H);
        strcat(S, P + 1);
    }
    else if ( (P = strchr(R, '#')) ) {
        strncpy(S, R, P - R);
        S[P - R] = '\0';
        printHex4(H, val[B] + 256 * val[B + 1]);
        strcat(S, H);
        strcat(S, P + 1);
        B += 2;
    }
    else
        strcpy(S, R);
    return(B);
}

/*  Symbolic output

    Inputs:
        *of     =   output stream
        addr    =   current PC
        *val    =   pointer to values
        *uptr   =   pointer to unit
        sw      =   switches
    Outputs:
        status  =   error code
*/

t_stat fprint_sym(FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw) {
    char disasm_result[128];
    int32 ch = val[0] & 0x7f;
    long r = 1;
    unsigned char vals[SIM_EMAX];
    int32 i;
    if (sw & (SWMASK('A') | SWMASK('C'))) {
        fprintf(of, ((0x20 <= ch) && (ch < 0x7f)) ? "'%c'" : "%02x", ch);
        return SCPE_OK;
    }
    if (!(sw & SWMASK('M')))
        return SCPE_ARG;
    switch (chiptype) {
        case CHIP_TYPE_8080:
            r = DAsm(disasm_result, val, FALSE, addr);
            break;

        case CHIP_TYPE_Z80:
            r = DAsm(disasm_result, val, TRUE, addr);
            break;

        case CHIP_TYPE_8086:
            for (i = 0; i < SIM_EMAX; i++)
                vals[i] = val[i] & 0xff;
            r = disasm(vals, disasm_result, 16, addr);
            break;

        case CHIP_TYPE_M68K:
            r = m68k_disassemble(disasm_result, addr, M68K_CPU_TYPE_68000);
            break;

        default:
            return SCPE_IERR;
            
    }
    fprintf(of, "%s", disasm_result);
    return 1 - r;
}

/*  checkbase determines the base of the number (ch, *numString)
    and returns FALSE if the number is bad */
static int32 checkbase(char ch, const char *numString) {
    int32 decimal = (ch <= '9');
    if (toupper(ch) == 'H')
        return FALSE;
    while (isxdigit(ch = *numString++))
        if (ch > '9')
            decimal = FALSE;
    return toupper(ch) == 'H' ? 16 : (decimal ? 10 : FALSE);
}

static int32 numok(char ch, const char **numString, const int32 minvalue,
        const int32 maxvalue, const int32 requireSign, int32 *result) {
    int32 sign = 1, value = 0, base;
    if (requireSign) {
        if (ch == '+')
            ch = *(*numString)++;
        else if (ch == '-') {
            sign = -1;
            ch = *(*numString)++;
        }
        else
            return FALSE;
    }
    if (!(base = checkbase(ch, *numString)))
        return FALSE;
    while (isxdigit(ch)) {
        value = base * value + ((ch <= '9') ? (ch - '0') : (toupper(ch) - 'A' + 10));
        ch = *(*numString)++;
    }
    if (toupper(ch) != 'H')
        (*numString)--;
    *result = value * sign;
    return (minvalue <= value) && (value <= maxvalue);
}

static int32 match(const char *pattern, const char *input, char *xyFirst, char *xy, int32 *number, int32 *star,
        int32 *at, int32 *hat, int32 *dollar) {
    char pat = *pattern++;
    char inp = *input++;
    while ((pat) && (inp)) {
        switch(pat) {

            case '_': /* patterns containing '_' should never match */
                return FALSE;

            case ',':
                if (inp == ' ') {
                    inp = *input++;
                    continue;
                }   /* otherwise fall through */

            case ' ':
                if (inp != pat)
                    return FALSE;
                pat = *pattern++;
                inp = *input++;
                while (inp == ' ')
                    inp = *input++;
                continue;

            case '%':
                inp = toupper(inp);
                if ((inp == 'X') || (inp == 'Y'))
                    if (*xyFirst)   /* make sure that second '%' corresponds to first */
                        if (*xyFirst == inp)
                            *xy = inp;
                        else
                            return FALSE;
                    else { /* take note of first '%' for later */
                        *xyFirst = inp;
                        *xy = inp;
                    }
                else
                    return FALSE;
                break;

            case '#':
                if (numok(inp, &input, 0, 65535, FALSE, number))
                    pattern++; /* skip h */
                else
                    return FALSE;
                break;

            case '*':
                if (numok(inp, &input, 0, 255, FALSE, star))
                    pattern++;     /* skip h */
                else
                    return FALSE;
                break;

            case '@':
                if (numok(inp, &input, -128, 65535, TRUE, at))
                    pattern++;   /* skip h */
                else
                    return FALSE;
                break;

            case '$':
                if (numok(inp, &input, 0, 65535, FALSE, dollar))
                    pattern++; /* skip h */
                else
                    return FALSE;
                break;

            case '^':
                if (numok(inp, &input, 0, 255, FALSE, hat))
                    pattern++;      /* skip h */
                else
                    return FALSE;
                break;

            default:
                if (toupper(pat) != toupper(inp))
                    return FALSE;
        }
        pat = *pattern++;
        inp = *input++;
    }
    while (inp == ' ')
        inp = *input++;
    return (pat == 0) && (inp == 0);
}

static int32 checkXY(const char xy) {
    return xy == 'X' ? 0xdd : 0xfd; /* else is 'Y' */
}

static int32 parse_X80(const char *cptr, const int32 addr, uint32 *val, const char *const Mnemonics[]) {
    char xyFirst = 0, xy;
    int32 op, number, star, at, hat, dollar;
    for (op = 0; op < 256; op++) {
        number = star = at = dollar = -129;
        if (match(Mnemonics[op], cptr, &xyFirst, &xy, &number, &star, &at, &hat, &dollar)) {
            val[0] = op;
            if (number >= 0) {
                val[1] = (0xff) & number;
                val[2] = (0xff) & (number >> 8);
                return -2;              /* two additional bytes returned    */
            }
            else if (star >= 0) {
                val[1] = (0xff) & star;
                return -1;              /* one additional byte returned     */
            }
            else if (at > -129)
                if ((-128 <= at) && (at <= 127)) {
                    val[1] = (int8)(at);
                    return -1;          /* one additional byte returned     */
                }
                else
                    return SCPE_ARG;
            else if (dollar >= 0) {
                dollar -= addr + 2;     /* relative translation             */
                if ((-128 <= dollar) && (dollar <= 127)) {
                    val[1] = (int8)(dollar);
                    return -1;          /* one additional byte returned     */
                }
                else
                    return SCPE_ARG;
            }
            else
                return SCPE_OK;
        }
    }
    if (Mnemonics == Mnemonics8080)
        return SCPE_ARG;

    for (op = 0; op < 256; op++)
        if (match(MnemonicsCB[op], cptr, &xyFirst, &xy, &number, &star, &at, &hat, &dollar)) {
            val[0] = 0xcb;
            val[1] = op;
            return -1;                  /* one additional byte returned     */
        }

    for (op = 0; op < 256; op++) {
        number = -1;
        if (match(MnemonicsED[op], cptr, &xyFirst, &xy, &number, &star, &at, &hat, &dollar)) {
            val[0] = 0xed;
            val[1] = op;
            if (number >= 0) {
                val[2] = (0xff) & number;
                val[3] = (0xff) & (number >> 8);
                return -3;              /* three additional bytes returned  */
            }
            else
                return -1;              /* one additional byte returned     */
        }
    }

    for (op = 0; op < 256; op++) {
        number = star = hat = -1;
        xy = 0;
        if (match(MnemonicsXX[op], cptr, &xyFirst, &xy, &number, &star, &at, &hat, &dollar)) {
            /* all matches must have contained a '%' character */
            if (!(val[0] = checkXY(xy)))
                return SCPE_ARG;
            val[1] = op;
            if (number >= 0) {
                val[2] = (0xff) & number;
                val[3] = (0xff) & (number >> 8);
                return -3;              /* three additional bytes returned  */
            }
            else if ((star >= 0) && (hat >= 0)) {
                val[2] = (0xff) & hat;
                val[3] = (0xff) & star;
                return -3;              /* three additional bytes returned  */
            }
            else if (star >= 0) {
                val[2] = (0xff) & star;
                return -2;              /* two additional bytes returned    */
            }
            else if (hat >= 0) {
                val[2] = (0xff) & hat;
                return -2;              /* two additional bytes returned    */
            }
            else
                return -1;              /* one additional byte returned     */
        }
    }

    for (op = 0; op < 256; op++) {
        at = -129;
        xy = 0;
        if (match(MnemonicsXCB[op], cptr, &xyFirst, &xy, &number, &star, &at, &hat, &dollar)) {
            /* all matches must have contained a '%' character */
            if (!(val[0] = checkXY(xy)))
                return SCPE_ARG;
            val[1] = 0xcb;
            if (at > -129)
                val[2] = (int8) (at);
            else {
                sim_printf("Offset expected.\n");
                return SCPE_ARG;
            }
            val[3] = op;
            return -3;                  /* three additional bytes returned  */
        }
    }
    return SCPE_ARG;
}


/*  Symbolic input

    Inputs:
        *cptr   =   pointer to input string
        addr    =   current PC
        *uptr   =   pointer to unit
        *val    =   pointer to output values
        sw      =   switches
    Outputs:
        status  =   error status
*/
t_stat parse_sym(CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw) {
    static t_bool symbolicInputNotImplementedMessage8086 = FALSE;
    static t_bool symbolicInputNotImplementedMessageM68K = FALSE;
    if ((sw & (SWMASK('M'))) && (chiptype == CHIP_TYPE_8086)) {
        if (!symbolicInputNotImplementedMessage8086) {
            sim_printf("Symbolic input is not supported for the 8086.\n");
            symbolicInputNotImplementedMessage8086 = TRUE;
        }
        return SCPE_NOFNC;
    }
    if ((sw & (SWMASK('M'))) && (chiptype == CHIP_TYPE_M68K)) {
        if (!symbolicInputNotImplementedMessageM68K) {
            sim_printf("Symbolic input is not supported for the M68K.\n");
            symbolicInputNotImplementedMessageM68K = TRUE;
        }
        return SCPE_NOFNC;
    }
    while (isspace(*cptr))
        cptr++;                 /* absorb spaces            */
    if ((sw & (SWMASK('A') | SWMASK('C'))) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
        if (cptr[0] == 0)
            return SCPE_ARG;    /* must have one char       */
        val[0] = (uint32) cptr[0];
        return SCPE_OK;
    }
    return parse_X80(cptr, addr, val, chiptype == CHIP_TYPE_Z80 ? MnemonicsZ80 : Mnemonics8080);
}

/* Set Memory Base Address routine */
t_stat set_membase(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr;
    PNP_INFO *pnp;
    uint32 newba;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    pnp = (PNP_INFO *) dptr->ctxt;
    if (pnp == NULL)
        return SCPE_IERR;

    newba = get_uint (cptr, 16, 0xFFFF, &r);
    if (r != SCPE_OK)
        return r;

    if ((newba > 0xFFFF) || (newba % pnp->mem_size))
        return SCPE_ARG;

    if (dptr->flags & DEV_DIS) {
        sim_printf("device not enabled yet.\n");
        pnp->mem_base = newba & ~(pnp->mem_size-1);
    } else {
        dptr->flags |= DEV_DIS;
        dptr->reset(dptr);
        pnp->mem_base = newba & ~(pnp->mem_size-1);
        dptr->flags &= ~DEV_DIS;
        dptr->reset(dptr);
    }

    return SCPE_OK;
}

/* Show Base Address routine */
t_stat show_membase(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE *dptr;
    PNP_INFO *pnp;

    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    pnp = (PNP_INFO *) dptr->ctxt;
    if (pnp == NULL)
        return SCPE_IERR;

    fprintf(st, "MEM=0x%04X-0x%04X", pnp->mem_base, pnp->mem_base+pnp->mem_size-1);
    return SCPE_OK;
}

/* Set Memory Base Address routine */
t_stat set_iobase(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    DEVICE *dptr;
    PNP_INFO *pnp;
    uint32 newba;
    t_stat r;

    if (cptr == NULL)
        return SCPE_ARG;
    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    pnp = (PNP_INFO *) dptr->ctxt;
    if (pnp == NULL)
        return SCPE_IERR;

    newba = get_uint (cptr, 16, 0xFF, &r);
    if (r != SCPE_OK)
        return r;

    if ((newba > 0xFF) ||
        (newba % pnp->io_size))
        return SCPE_ARG;

    if (dptr->flags & DEV_DIS) {
        sim_printf("device not enabled yet.\n");
        pnp->io_base = newba & ~(pnp->io_size-1);
    } else {
        dptr->flags |= DEV_DIS;
        dptr->reset(dptr);
        pnp->io_base = newba & ~(pnp->io_size-1);
        dptr->flags &= ~DEV_DIS;
        dptr->reset(dptr);
    }

    return SCPE_OK;
}

/* Show I/O Base Address routine */
t_stat show_iobase(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    DEVICE *dptr;
    PNP_INFO *pnp;

    if (uptr == NULL)
        return SCPE_IERR;
    dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    pnp = (PNP_INFO *) dptr->ctxt;
    if (pnp == NULL)
        return SCPE_IERR;

    fprintf(st, "I/O=0x%02X-0x%02X", pnp->io_base, pnp->io_base+pnp->io_size-1);
    return SCPE_OK;
}

