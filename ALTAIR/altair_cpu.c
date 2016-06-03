/* altair_cpu.c: MITS Altair Intel 8080 CPU simulator

   Copyright (c) 1997-2012, Charles E. Owen

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

   cpu          8080 CPU

   02-Feb-15    RSB     Fixed initialization of "uninstalled" memory
   19-Mar-12    RMS     Fixed data type for breakpoint variables
   08-Oct-02    RMS     Tied off spurious compiler warnings

   The register state for the 8080 CPU is:

   A<0:7>               Accumulator
   BC<0:15>             BC Register Pair
   DE<0:15>             DE Register Pair
   HL<0:15>             HL Register Pair                                
   C                    carry flag
   Z                    zero flag
   S                    Sign bit
   AC                   Aux carry
   P                    Parity bit
   PC<0:15>             program counter
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
      return 0377, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to 0377.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        altair_cpu.c    add I/O service routines to dev_table
        altair_sys.c    add pointer to data structures in sim_devices
*/


#include <stdio.h>

#include "altair_defs.h"

#define UNIT_V_OPSTOP   (UNIT_V_UF)                     /* Stop on Invalid OP? */
#define UNIT_OPSTOP     (1 << UNIT_V_OPSTOP)
#define UNIT_V_CHIP     (UNIT_V_UF+1)                   /* 8080 or Z80 */
#define UNIT_CHIP       (1 << UNIT_V_CHIP)
#define UNIT_V_MSIZE    (UNIT_V_UF+2)                   /* Memory Size */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

unsigned char M[MAXMEMSIZE];                            /* memory */
int32 A = 0;                                            /* accumulator */
int32 BC = 0;                                           /* BC register pair */
int32 DE = 0;                                           /* DE register pair */
int32 HL = 0;                                           /* HL register pair */
int32 SP = 0;                                           /* Stack pointer */
int32 C = 0;                                            /* carry flag */
int32 Z = 0;                                            /* Zero flag */
int32 AC = 0;                                           /* Aux carry */
int32 S = 0;                                            /* sign flag */
int32 P = 0;                                            /* parity flag */
int32 saved_PC = 0;                                     /* program counter */
int32 SR = 0;                                           /* switch register */
int32 INTE = 0;                                         /* Interrupt Enable */
int32 int_req = 0;                                      /* Interrupt request */
int32 chip = 0;                                         /* 0 = 8080 chip, 1 = z80 chip */

int32 PCX;                                              /* External view of PC */

/* function prototypes */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void setarith(int32 reg);
void setlogical(int32 reg);
void setinc(int32 reg);
int32 getreg(int32 reg);
void putreg(int32 reg, int32 val);
int32 getpair(int32 reg);
int32 getpush(int32 reg);
void putpush(int32 reg, int32 data);
void putpair(int32 reg, int32 val);
void parity(int32 reg);
int32 cond(int32 con);

extern int32 sio0s(int32 io, int32 data);
extern int32 sio0d(int32 io, int32 data);
extern int32 sio1s(int32 io, int32 data);
extern int32 sio1d(int32 io, int32 data);
extern int32 dsk10(int32 io, int32 data);
extern int32 dsk11(int32 io, int32 data);
extern int32 dsk12(int32 io, int32 data);
int32 nulldev(int32 io, int32 data);

/* This is the I/O configuration table.  There are 255 possible
device addresses, if a device is plugged to a port it's routine
address is here, 'nulldev' means no device is available
*/
struct idev {
    int32 (*routine)(int32, int32);
};
struct idev dev_table[256] = {
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 000 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 004 */
{&dsk10},   {&dsk11},   {&dsk12},   {&nulldev},         /* 010 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 014 */
{&sio0s},   {&sio0d},   {&sio1s},   {&sio1d},           /* 020 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 024 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 030 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 034 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 040 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 044 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 050 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 054 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 060 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 064 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 070 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 074 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 100 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 104 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 110 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 114 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 120 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 124 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 130 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 134 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 140 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 144 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 150 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 154 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 160 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 164 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 170 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 174 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 200 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 204 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 210 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 214 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 220 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 224 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 230 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 234 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 240 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 244 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 250 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 254 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 260 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 264 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 270 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 274 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 300 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 304 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 310 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 314 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 320 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 324 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 330 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 334 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 340 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 344 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 350 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 354 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 360 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 364 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev},         /* 370 */
{&nulldev}, {&nulldev}, {&nulldev}, {&nulldev}          /* 374 */
};

/* Altair MITS standard BOOT EPROM, fits in upper 256K of memory */

int32 bootrom[256] = {
    0041, 0000, 0114, 0021, 0030, 0377, 0016, 0346,
    0032, 0167, 0023, 0043, 0015, 0302, 0010, 0377,
    0303, 0000, 0114, 0000, 0000, 0000, 0000, 0000,
    0363, 0061, 0142, 0115, 0257, 0323, 0010, 0076,     /* 46000 */
    0004, 0323, 0011, 0303, 0031, 0114, 0333, 0010,     /* 46010 */
    0346, 0002, 0302, 0016, 0114, 0076, 0002, 0323,     /* 46020 */
    0011, 0333, 0010, 0346, 0100, 0302, 0016, 0114,
    0021, 0000, 0000, 0006, 0000, 0333, 0010, 0346,
    0004, 0302, 0045, 0114, 0076, 0020, 0365, 0325,
    0305, 0325, 0021, 0206, 0200, 0041, 0324, 0114,
    0333, 0011, 0037, 0332, 0070, 0114, 0346, 0037,
    0270, 0302, 0070, 0114, 0333, 0010, 0267, 0372,
    0104, 0114, 0333, 0012, 0167, 0043, 0035, 0312,
    0132, 0114, 0035, 0333, 0012, 0167, 0043, 0302,
    0104, 0114, 0341, 0021, 0327, 0114, 0001, 0200,
    0000, 0032, 0167, 0276, 0302, 0301, 0114, 0200,
    0107, 0023, 0043, 0015, 0302, 0141, 0114, 0032,
    0376, 0377, 0302, 0170, 0114, 0023, 0032, 0270,
    0301, 0353, 0302, 0265, 0114, 0361, 0361, 0052,
    0325, 0114, 0325, 0021, 0000, 0377, 0315, 0316,
    0114, 0321, 0332, 0276, 0114, 0315, 0316, 0114,
    0322, 0256, 0114, 0004, 0004, 0170, 0376, 0040,
    0332, 0054, 0114, 0006, 0001, 0312, 0054, 0114,
    0333, 0010, 0346, 0002, 0302, 0240, 0114, 0076,
    0001, 0323, 0011, 0303, 0043, 0114, 0076, 0200,
    0323, 0010, 0303, 0000, 0000, 0321, 0361, 0075,
    0302, 0056, 0114, 0076, 0103, 0001, 0076, 0117,
    0001, 0076, 0115, 0107, 0076, 0200, 0323, 0010,
    0170, 0323, 0001, 0303, 0311, 0114, 0172, 0274,
    0300, 0173, 0275, 0311, 0204, 0000, 0114, 0044,
    0026, 0126, 0026, 0000, 0000, 0000, 0000, 0000
};

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATA (PC, saved_PC, 16) },
    { ORDATA (A, A, 8) },
    { ORDATA (BC, BC, 16) },
    { ORDATA (DE, DE, 16) },
    { ORDATA (HL, HL, 16) },
    { ORDATA (SP, SP, 16) },
    { FLDATA (C, C, 16) },
    { FLDATA (Z, Z, 16) },
    { FLDATA (AC, AC, 16) },
    { FLDATA (S, S, 16) },
    { FLDATA (P, P, 16) },
    { FLDATA (INTE, INTE, 16) },
    { ORDATA (SR, SR, 16) },
    { ORDATA (WRU, sim_int_char, 8) },
    { NULL }
};

MTAB cpu_mod[] = {
    { UNIT_CHIP, UNIT_CHIP, "Z80", "Z80", NULL },
    { UNIT_CHIP, 0, "8080", "8080", NULL },
    { UNIT_OPSTOP, UNIT_OPSTOP, "ITRAP", "ITRAP", NULL },
    { UNIT_OPSTOP, 0, "NOITRAP", "NOITRAP", NULL },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
    { UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size },
    { UNIT_MSIZE, 65535, NULL, "64K", &cpu_set_size },
    { 0 }
};

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 16, 1, 8, 8,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
};

t_stat sim_instr (void)
{
    int32 PC, IR, OP, DAR, reason, hi, lo, carry, i;

    PC = saved_PC & ADDRMASK;                           /* load local PC */
    C = C & 0200000;
    reason = 0;

    /* Main instruction fetch/decode loop */

    while (reason == 0) {                               /* loop until halted */
        if (sim_interval <= 0) {                        /* check clock queue */
            if ((reason = sim_process_event ())) break;
        }

        if (int_req > 0) {                              /* interrupt? */

        /* 8080 interrupts not implemented yet.  None were used,
           on a standard Altair 8800. All I/O is programmed. */

        }                                               /* end interrupt */

    if (sim_brk_summ &&
        sim_brk_test (PC, SWMASK ('E'))) {              /* breakpoint? */
            reason = STOP_IBKPT;                        /* stop simulation */
            break;
        }

        if (PC == 0177400) {                            /* BOOT PROM address */
            for (i = 0; i < 250; i++) {
                M[i + 0177400] = bootrom[i] & 0xFF;
            }
        }

        PCX = PC;

        IR = OP = M[PC];                                /* fetch instruction */

        PC = (PC + 1) & ADDRMASK;                       /* increment PC */

        sim_interval--;

        if (OP == 0166) {                               /* HLT Instruction*/
            reason = STOP_HALT;
            PC--;
            continue;
        }

       /* Handle below all operations which refer to registers or
          register pairs.  After that, a large switch statement
          takes care of all other opcodes */

        if ((OP & 0xC0) == 0x40) {                      /* MOV */
            DAR = getreg(OP & 0x07);
            putreg((OP >> 3) & 0x07, DAR);
            continue;
        }
        if ((OP & 0xC7) == 0x06) {                      /* MVI */
            putreg((OP >> 3) & 0x07, M[PC]);
            PC++;
            continue;
        }
        if ((OP & 0xCF) == 0x01) {                      /* LXI */
            DAR = M[PC] & 0x00ff;
            PC++;
            DAR = DAR | ((M[PC] <<8) & 0xFF00);
            putpair((OP >> 4) & 0x03, DAR);
            PC++;
            continue;
        }
        if ((OP & 0xEF) == 0x0A) {                      /* LDAX */
            DAR = getpair((OP >> 4) & 0x03);
            putreg(7, M[DAR]);
            continue;
        }
        if ((OP & 0xEF) == 0x02) {                      /* STAX */
            DAR = getpair((OP >> 4) & 0x03);
            M[DAR] = getreg(7);
            continue;
        }

        if ((OP & 0xF8) == 0xB8) {                      /* CMP */
            DAR = A & 0xFF;
            DAR -= getreg(OP & 0x07);
            setarith(DAR);
            continue;
        }
        if ((OP & 0xC7) == 0xC2) {                      /* JMP <condition> */
            if (cond((OP >> 3) & 0x07) == 1) {
                lo = M[PC];
                PC++;
                hi = M[PC];
                PC++;
                PC = (hi << 8) + lo;
            } else {
                PC += 2;
            }
            continue;
        }
        if ((OP & 0xC7) == 0xC4) {                      /* CALL <condition> */
            if (cond((OP >> 3) & 0x07) == 1) {
                lo = M[PC];
                PC++;
                hi = M[PC];
                PC++;
                SP--;
                M[SP] = (PC >> 8) & 0xff;
                SP--;
                M[SP] = PC & 0xff;
                PC = (hi << 8) + lo;
            } else {
                PC += 2;
            }
            continue;
        }
        if ((OP & 0xC7) == 0xC0) {                      /* RET <condition> */
            if (cond((OP >> 3) & 0x07) == 1) {
                PC = M[SP];
                SP++;
                PC |= (M[SP] << 8) & 0xff00;
                SP++;
            }
            continue;
        }
        if ((OP & 0xC7) == 0xC7) {                      /* RST */
            SP--;
            M[SP] = (PC >> 8) & 0xff;
            SP--;
            M[SP] = PC & 0xff;
            PC = OP & 0x38;
            continue;
        }

        if ((OP & 0xCF) == 0xC5) {                      /* PUSH */
            DAR = getpush((OP >> 4) & 0x03);
            SP--;
            M[SP] = (DAR >> 8) & 0xff;
            SP--;
            M[SP] = DAR & 0xff;
            continue;
        }
        if ((OP & 0xCF) == 0xC1) {                      /*POP */
            DAR = M[SP];
            SP++;
            DAR |= M[SP] << 8;
            SP++;
            putpush((OP >> 4) & 0x03, DAR);
            continue;
        }
        if ((OP & 0xF8) == 0x80) {                      /* ADD */
            A += getreg(OP & 0x07);
            setarith(A);
            A = A & 0xFF;
            continue;
        }
        if ((OP & 0xF8) == 0x88) {                      /* ADC */
            carry = 0;
            if (C) carry = 1;
            A += getreg(OP & 0x07);
            A += carry;
            setarith(A);
            A = A & 0xFF;
            continue;
        }
        if ((OP & 0xF8) == 0x90) {                      /* SUB */
            A -= getreg(OP & 0x07);
            setarith(A);
            A = A & 0xFF;
            continue;
        }
        if ((OP & 0xF8) == 0x98) {                      /* SBB */
            carry = 0;
            if (C) carry = 1;
            A -= (getreg(OP & 0x07)) + carry ;
            setarith(A);
            A = A & 0xFF;
            continue;
        }
        if ((OP & 0xC7) == 0x04) {                      /* INR */
            DAR = getreg((OP >> 3) & 0x07);
            DAR++;
            setinc(DAR);
            DAR = DAR & 0xFF;
            putreg((OP >> 3) & 0x07, DAR);
            continue;
        }
        if ((OP & 0xC7) == 0x05) {                      /* DCR */
            DAR = getreg((OP >> 3) & 0x07);
            DAR--;
            setinc(DAR);
            DAR = DAR & 0xFF;
            putreg((OP >> 3) & 0x07, DAR);
            continue;
        }
        if ((OP & 0xCF) == 0x03) {                      /* INX */
            DAR = getpair((OP >> 4) & 0x03);
            DAR++;
            DAR = DAR & 0xFFFF;
            putpair((OP >> 4) & 0x03, DAR);
            continue;
        }
        if ((OP & 0xCF) == 0x0B) {                      /* DCX */
            DAR = getpair((OP >> 4) & 0x03);
            DAR--;
            DAR = DAR & 0xFFFF;
            putpair((OP >> 4) & 0x03, DAR);
            continue;
        }
        if ((OP & 0xCF) == 0x09) {                      /* DAD */
            HL += getpair((OP >> 4) & 0x03);
            C = 0;
            if (HL & 0x10000)
                C = 0200000;
            HL = HL & 0xFFFF;
            continue;
        }
        if ((OP & 0xF8) == 0xA0) {                      /* ANA */
            A &= getreg(OP & 0x07);
            C = 0;
            setlogical(A);
            A &= 0xFF;
            continue;
        }
        if ((OP & 0xF8) == 0xA8) {                      /* XRA */
            A ^= getreg(OP & 0x07);
            C = 0;
            setlogical(A);
            A &= 0xFF;
            continue;
        }
        if ((OP & 0xF8) == 0xB0) {                      /* ORA */
            A |= getreg(OP & 0x07);
            C = 0;
            setlogical(A);
            A &= 0xFF;
            continue;
        }



        /* The Big Instruction Decode Switch */

        switch (IR) {

        /* Logical instructions */

        case 0376: {                                    /* CPI */
            DAR = A & 0xFF;
            DAR -= M[PC];
            PC++;
            setarith(DAR);
            break;
        }
        case 0346: {                                    /* ANI */
            A &= M[PC];
            PC++;
            C = AC = 0;
            setlogical(A);
            A &= 0xFF;
            break;
        }
        case 0356: {                                    /* XRI */
            A ^= M[PC];
            PC++;
            C = AC = 0;
            setlogical(A);
            A &= 0xFF;
            break;
        }
        case 0366: {                                    /* ORI */
            A |= M[PC];
            PC++;
            C = AC = 0;
            setlogical(A);
            A &= 0xFF;
            break;
        }

        /* Jump instructions */

        case 0303: {                                    /* JMP */
            lo = M[PC];
            PC++;
            hi = M[PC];
            PC++;
            PC = (hi << 8) + lo;
            break;
        }
        case 0351: {                                    /* PCHL */
            PC = HL;
            break;
        }
        case 0315: {                                    /* CALL */
            lo = M[PC];
            PC++;
            hi = M[PC];
            PC++;
            SP--;
            M[SP] = (PC >> 8) & 0xff;
            SP--;
            M[SP] = PC & 0xff;
            PC = (hi << 8) + lo;
            break;
        }
        case 0311: {                                    /* RET */
            PC = M[SP];
            SP++;
            PC |= (M[SP] << 8) & 0xff00;
            SP++;
            break;
        }

        /* Data Transfer Group */

        case 062: {                                     /* STA */
            lo = M[PC];
            PC++;
            hi = M[PC];
            PC++;
            DAR = (hi << 8) + lo;
            M[DAR] = A;
            break;
        }
        case 072: {                                     /* LDA */
            lo = M[PC];
            PC++;
            hi = M[PC];
            PC++;
            DAR = (hi << 8) + lo;
            A = M[DAR];
            break;
        }
        case 042: {                                     /* SHLD */
            lo = M[PC];
            PC++;
            hi = M[PC];
            PC++;
            DAR = (hi << 8) + lo;
            M[DAR] = HL;
            DAR++;
            M[DAR] = (HL >>8) & 0x00ff;
            break;
        }
        case 052: {                                     /* LHLD */
            lo = M[PC];
            PC++;
            hi = M[PC];
            PC++;
            DAR = (hi << 8) + lo;
            HL = M[DAR];
            DAR++;
            HL = HL | (M[DAR] <<8);
            break;
        }
        case 0353: {                                    /* XCHG */
            DAR = HL;
            HL = DE;
            DE = DAR;
            break;
        }

        /* Arithmetic Group */

        case 0306: {                                    /* ADI */
            A += M[PC];
            PC++;
            setarith(A);
            A = A & 0xFF;
            break;
        }
        case 0316: {                                    /* ACI */
            carry = 0;
            if (C) carry = 1;
            A += M[PC];
            A += carry;
            PC++;
            setarith(A);
            A = A & 0xFF;
            break;
        }
        case 0326: {                                    /* SUI */
            A -= M[PC];
            PC++;
            setarith(A);
            A = A & 0xFF;
            break;
        }
        case 0336: {                                    /* SBI */
            carry = 0;
            if (C) carry = 1;
            A -= (M[PC] + carry);
            PC++;
            setarith(A);
            A = A & 0xFF;
            break;
        }
        case 047: {                                     /* DAA */
            DAR = A & 0x0F;
            if (DAR > 9 || AC > 0) {
                DAR += 6;
                A &= 0xF0;
                A |= DAR & 0x0F;
                if (DAR & 0x10)
                    AC = 0200000;
                   else
                    AC = 0;
            }
            DAR = (A >> 4) & 0x0F;
            if (DAR > 9 || AC > 0) {
                DAR += 6;
                if (AC) DAR++;
                A &= 0x0F;
                A |= (DAR << 4);
            }
            if ((DAR << 4) & 0x100)
                C = 0200000;
               else
                C = 0;
            if (A & 0x80) {
                S = 0200000;
            } else {
                S = 0;
            }
            if ((A & 0xff) == 0)
                Z = 0200000;
              else
                Z = 0;
            parity(A);
            A = A & 0xFF;
            break;
        }
        case 07: {                                      /* RLC */
            C = 0;
            C = (A << 9) & 0200000;
            A = (A << 1) & 0xFF;
            if (C)
                A |= 0x01;
            break;
        }
        case 017: {                                     /* RRC */
            C = 0;
            if ((A & 0x01) == 1)
                C |= 0200000;
            A = (A >> 1) & 0xFF;
            if (C)
                A |= 0x80;
            break;
        }
        case 027: {                                     /* RAL */
            DAR = C;
            C = 0;
            C = (A << 9) & 0200000;
            A = (A << 1) & 0xFF;
            if (DAR)
                A |= 1;
              else
                A &= 0xFE;
            break;
        }
        case 037: {                                     /* RAR */
            DAR = C;
            C = 0;
            if ((A & 0x01) == 1)
                C |= 0200000;
            A = (A >> 1) & 0xFF;
            if (DAR)
                A |= 0x80;
              else
                A &= 0x7F;
            break;
        }
        case 057: {                                     /* CMA */
            A = ~ A;
            A &= 0xFF;
            break;
        }
        case 077: {                                     /* CMC */
            C = ~ C;
            C &= 0200000;
            break;
        }
        case 067: {                                     /* STC */
            C = 0200000;
            break;
        }

        /* Stack, I/O & Machine Control Group */

        case 0: {                                       /* NOP */
            break;
        }
        case 0343: {                                    /* XTHL */
            lo = M[SP];
            hi = M[SP + 1];
            M[SP] = HL & 0xFF;
            M[SP + 1] = (HL >> 8) & 0xFF;
            HL = (hi << 8) + lo;
            break;
        }
        case 0371: {                                    /* SPHL */
            SP = HL;
            break;
        }
        case 0373: {                                    /* EI */
            INTE = 0200000;
            break;
        }
        case 0363: {                                    /* DI */
            INTE = 0;
            break;
        }
        case 0333: {                                    /* IN */
            DAR = M[PC] & 0xFF;
            PC++;
            if (DAR == 0xFF) {
                A = (SR >> 8) & 0xFF;
            } else {
                A = dev_table[DAR].routine(0, 0);
            }
            break;
        }
        case 0323: {                                    /* OUT */
            DAR = M[PC] & 0xFF;
            PC++;
            dev_table[DAR].routine(1, A);
            break;
        }

        default: {
            if (cpu_unit.flags & UNIT_OPSTOP) {
                reason = STOP_OPCODE;
                PC--;
            }
            break;
        }
    }
}

/* Simulation halted */

saved_PC = PC;
return reason;
}

/* Test an 8080 flag condition and return 1 if true, 0 if false */
int32 cond(int32 con)
{
    switch (con) {
        case 0:
            if (Z == 0) return (1);
            break;
        case 1:
            if (Z != 0) return (1);
            break;
        case 2:
            if (C == 0) return (1);
            break;
        case 3:
            if (C != 0) return (1);
            break;
        case 4:
            if (P == 0) return (1);
            break;
        case 5:
            if (P != 0) return (1);
            break;
        case 6:
            if (S == 0) return (1);
            break;
        case 7:
            if (S != 0) return (1);
            break;
        default:
            break;
    }
    return (0);
}

/* Set the <C>arry, <S>ign, <Z>ero and <P>arity flags following
   an arithmetic operation on 'reg'.
*/

void setarith(int32 reg)
{
    int32 bc = 0;

    if (reg & 0x100)
        C = 0200000;
      else
        C = 0;
    if (reg & 0x80) {
        bc++;
        S = 0200000;
    } else {
        S = 0;
    }
    if ((reg & 0xff) == 0)
        Z = 0200000;
      else
        Z = 0;
    AC = 0;
    if (cpu_unit.flags & UNIT_CHIP) {
        P = 0;              /* parity is zero for *all* arith ops on Z80 */
    } else {
        parity(reg);
    }
}

/* Set the <C>arry, <S>ign, <Z>ero amd <P>arity flags following
   a logical (bitwise) operation on 'reg'.
*/

void setlogical(int32 reg)
{
    C = 0;
    if (reg & 0x80) {
        S = 0200000;
    } else {
        S = 0;
    }
    if ((reg & 0xff) == 0)
        Z = 0200000;
      else
        Z = 0;
    AC = 0;
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
    P = ~(bc << 16);
    P &= 0200000;
}

/* Set the <S>ign, <Z>ero amd <P>arity flags following
   an INR/DCR operation on 'reg'.
*/

void setinc(int32 reg)
{
    int32 bc = 0;

    if (reg & 0x80) {
        bc++;
        S = 0200000;
    } else {
        S = 0;
    }
    if ((reg & 0xff) == 0)
        Z = 0200000;
      else
        Z = 0;
    if (cpu_unit.flags & UNIT_CHIP) {
        P = 0;              /* parity is zero for *all* arith ops on Z80 */
    } else {
        parity(reg);
    }
}

/* Get an 8080 register and return it */
int32 getreg(int32 reg)
{
    switch (reg) {
        case 0:
            return ((BC >>8) & 0x00ff);
        case 1:
            return (BC & 0x00FF);
        case 2:
            return ((DE >>8) & 0x00ff);
        case 3:
            return (DE & 0x00ff);
        case 4:
            return ((HL >>8) & 0x00ff);
        case 5:
            return (HL & 0x00ff);
        case 6:
            return (M[HL]);
        case 7:
            return (A);
        default:
            break;
    }
    return 0;
}

/* Put a value into an 8080 register from memory */
void putreg(int32 reg, int32 val)
{
    switch (reg) {
        case 0:
            BC = BC & 0x00FF;
            BC = BC | (val <<8);
            break;
        case 1:
            BC = BC & 0xFF00;
            BC = BC | val;
            break;
        case 2:
            DE = DE & 0x00FF;
            DE = DE | (val <<8);
            break;
        case 3:
            DE = DE & 0xFF00;
            DE = DE | val;
            break;
        case 4:
            HL = HL & 0x00FF;
            HL = HL | (val <<8);
            break;
        case 5:
            HL = HL & 0xFF00;
            HL = HL | val;
            break;
        case 6:
            M[HL] = val & 0xff;
            break;
        case 7:
            A = val & 0xff;
        default:
            break;
    }
}

/* Return the value of a selected register pair */
int32 getpair(int32 reg)
{
    switch (reg) {
        case 0:
            return (BC);
        case 1:
            return (DE);
        case 2:
            return (HL);
        case 3:
            return (SP);
        default:
            break;
    }
    return 0;
}

/* Return the value of a selected register pair, in PUSH
   format where 3 means A& flags, not SP */
int32 getpush(int32 reg)
{
    int32 stat;

    switch (reg) {
        case 0:
            return (BC);
        case 1:
            return (DE);
        case 2:
            return (HL);
        case 3:
            stat = A << 8;
            if (S) stat |= 0x80;
            if (Z) stat |= 0x40;
            if (AC) stat |= 0x10;
            if (P) stat |= 0x04;
            stat |= 0x02;
            if (C) stat |= 0x01;
            return (stat);
        default:
            break;
    }
    return 0;
}


/* Place data into the indicated register pair, in PUSH
   format where 3 means A& flags, not SP */
void putpush(int32 reg, int32 data)
{
    switch (reg) {
        case 0:
            BC = data;
            break;
        case 1:
            DE = data;
            break;
        case 2:
            HL = data;
            break;
        case 3:
            A = (data >> 8) & 0xff;
            S = Z = AC = P = C = 0;
            if (data & 0x80) S = 0200000;
            if (data & 0x40) Z = 0200000;
            if (data & 0x10) AC = 0200000;
            if (data & 0x04) P = 0200000;
            if (data & 0x01) C = 0200000;
            break;
        default:
            break;
    }
}


/* Put a value into an 8080 register pair */
void putpair(int32 reg, int32 val)
{
    switch (reg) {
        case 0:
            BC = val;
            break;
        case 1:
            DE = val;
            break;
        case 2:
            HL = val;
            break;
        case 3:
            SP = val;
            break;
        default:
            break;
    }
}


/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
C = 0;
Z = 0;
saved_PC = 0;
int_req = 0;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE) return SCPE_NXM;
if (vptr != NULL) *vptr = M[addr] & 0377;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE) return SCPE_NXM;
    M[addr] = val & 0377;
    return SCPE_OK;
}

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0377;
return SCPE_OK;
}

int32 nulldev(int32 flag, int32 data)
{
    if (flag == 0)
        return (0377);
    return 0;
}

