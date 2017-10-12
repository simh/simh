/* i8008.c: Intel 8008 CPU simulator.

   Copyright (c) 2017, Hans-Ake Lund

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
   HANS-AKE LUND BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Hans-Ake Lund shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Hans-Ake Lund.

   cpu          8008 CPU

   The register state for the 8008 CPU is:

   A<0:7>               Accumulator
   B<0:7>               B Register
   C<0:7>               C Register
   D<0:7>               D Register
   E<0:7>               E Register
   HL<0:15>             HL Register Pair
   CF                   Carry flag
   ZF                   Zero flag
   SF                   Sign bit
   PF                   Parity bit
   PC<0:13>             Program Counter
   SP<0:2>              Stack Pointer into return stack with 7 levels
                        (the SP register is not available for 8008 programs)

   The 8008 is an 8-bit CPU, which uses 14 bits of 16-bit registers
   to address up to 16KB of memory.

   The basic instructions come in 1, 2, and 3-byte flavors.

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
      Hardware external to the CPU supplies an instruction
      this could be an RST instruction making a call to one
      of 8 possibe memory addresses.

   3. Non-existent memory.  On the 8008, reads to non-existent memory
      return 0377, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to 0377.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        <systen name>_io.c    add I/O service routines to dev_table
        <system name>_sys.c   add pointer to data structures in sim_devices

   CPU documentation: http://www.classiccmp.org/8008/8008UM.pdf

   04-Sep-17    HAL     Working version of CPU simulator for SCELBI computer
   12-Sep-17    HAL     Modules restructured in "Intel-Systems" directory

*/

#include <ctype.h>
#include "system_defs.h"

#define UNIT_V_OPSTOP   (UNIT_V_UF)             /* Stop on Invalid OP? */
#define UNIT_OPSTOP     (1 << UNIT_V_OPSTOP)
#define UNIT_V_CHIP     (UNIT_V_UF+1)           /* 8008 */
#define UNIT_CHIP       (1 << UNIT_V_CHIP)
#define UNIT_V_MSIZE    (UNIT_V_UF+2)           /* Memory Size */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

unsigned char Mem[MAXMEMSIZE];                  /* Memory */
unsigned int Smem[8];                           /* Stack memory with 7 levels
                                                   (TODO: and program counter) */
int32 Areg = 0;                                 /* accumulator */
int32 Breg = 0;                                 /* B register */
int32 Creg = 0;                                 /* C register */
int32 Dreg = 0;                                 /* D register */
int32 Ereg = 0;                                 /* E register */
int32 HLreg = 0;                                /* HL register pair */
int32 SPreg = 0;                                /* Stack pointer 3 bits */
int32 Cflag = 0;                                /* Carry flag */
int32 Zflag = 0;                                /* Zero flag */
int32 Sflag = 0;                                /* Sign flag */
int32 Pflag = 0;                                /* Parity flag */
int32 saved_PCreg = 0;                          /* Program Counter */
int32 INTEflag = 0;                             /* Interrupt Enable */
int32 int_req = 0;                              /* Interrupt Request */

int32 PCXreg;                                   /* External view of PC */

/* Function prototypes */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw);
t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw);

void setarith(int32 reg);
void setlogical(int32 reg);
void setinc(int32 reg);
int32 getreg(int32 reg);
void putreg(int32 reg, int32 val);
void parity(int32 reg);
int32 cond(int32 con);

extern struct idev dev_table[32];

/* 8008 CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATA (PC, saved_PCreg, 16) },
    { ORDATA (A, Areg, 8) },
    { ORDATA (B, Breg, 8) },
    { ORDATA (C, Creg, 8) },
    { ORDATA (D, Dreg, 8) },
    { ORDATA (E, Ereg, 8) },
    { ORDATA (HL, HLreg, 16) },
    { ORDATA (SP, SPreg, 16) },
    { FLDATA (CF, Cflag, 16) },
    { FLDATA (ZF, Zflag, 16) },
    { FLDATA (SF, Sflag, 16) },
    { FLDATA (PF, Pflag, 16) },
    { FLDATA (INTE, INTEflag, 16) },
    { ORDATA (WRU, sim_int_char, 8) },
    { NULL }
};

MTAB cpu_mod[] = {
    { UNIT_CHIP, 0, "8008", "8008", NULL },
    { UNIT_OPSTOP, UNIT_OPSTOP, "ITRAP", "ITRAP", NULL },
    { UNIT_OPSTOP, 0, "NOITRAP", "NOITRAP", NULL },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { 0 }
};

DEVICE cpu_dev = {
    "I8008", &cpu_unit, cpu_reg, cpu_mod, // name, units, registers, modifiers
    1, 8, 16, 1,                          // numunits, aradix, awidth, aincr
    8, 8,                                 // dradix, dwidth
    &cpu_ex, &cpu_dep, &cpu_reset,        // examine, deposit, reset
    NULL, NULL, NULL                      // boot, attach, detach
};

/* Intel 8008 opcodes
 */
static const char *opcode[] = {
"HLT", "HLT", "RLC", "RFC",                             /* 000-003 */
"ADI", "RST0", "LAI", "RET",                            /* 004-007 */
"INB", "DCB", "RRC", "RFZ",                             /* 010-013 */
"ACI", "RST1", "LBI", "RET",                            /* 014-017 */
"INC", "DCC", "RAL", "RFS",                             /* 020-023 */
"SUI", "RST2", "LCI", "RET",                            /* 024-027 */
"IND", "DCD", "RAR", "RFP",                             /* 030-033 */
"SBI", "RST3", "LDI", "RET",                            /* 034-037 */
"INE", "DCE", "???", "RTC",                             /* 040-043 */
"NDI", "RST4", "LEI", "RET",                            /* 044-047 */
"ICH", "DCH", "???", "RTZ",                             /* 050-053 */
"XRI", "RST5", "LHI", "RET",                            /* 054-057 */
"INL", "DCL", "???", "RTS",                             /* 060-063 */
"ORI", "RST6", "LLI", "RET",                            /* 064-067 */
"???", "???", "???", "RTP",                             /* 070-073 */
"CPI", "RST7", "LMI", "RET",                            /* 074-077 */
"JFC", "INP", "CFC", "INP",                             /* 100-103 */
"JMP", "INP", "CAL", "INP",                             /* 104-107 */
"JFZ", "INP", "CFZ", "INP",                             /* 110-113 */
"JMP", "INP", "CAL", "INP",                             /* 114-117 */
"JFS", "OUT", "CFS", "OUT",                             /* 120-123 */
"JMP", "OUT", "CAL", "OUT",                             /* 124-127 */
"JFP", "OUT", "CFP", "OUT",                             /* 130-133 */
"JMP", "OUT", "CAL", "OUT",                             /* 134-137 */
"JTC", "OUT", "CTC", "OUT",                             /* 140-143 */
"JMP", "OUT", "CAL", "OUT",                             /* 144-147 */
"JTZ", "OUT", "CTZ", "OUT",                             /* 150-153 */
"JMP", "OUT", "CAL", "OUT",                             /* 154-157 */
"JTS", "OUT", "CTS", "OUT",                             /* 160-163 */
"JMP", "OUT", "CAL", "OUT",                             /* 164-167 */
"JTP", "OUT", "CTP", "OUT",                             /* 170-173 */
"JMP", "OUT", "CAL", "OUT",                             /* 174-177 */
"ADA", "ADB", "ADC", "ADD",                             /* 200-203 */
"ADE", "ADH", "ADL", "ADM",                             /* 204-207 */
"ACA", "ACB", "ACC", "ACD",                             /* 210-213 */
"ACE", "ACH", "ACL", "ACM",                             /* 214-217 */
"SUA", "SUB", "SUC", "SUD",                             /* 220-223 */
"SUE", "SUH", "SUL", "SUM",                             /* 224-227 */
"SBA", "SBB", "SBC", "SBD",                             /* 230-233 */
"SBE", "SBH", "SBL", "SBM",                             /* 234-237 */
"NDA", "NDB", "NDC", "NDD",                             /* 240-243 */
"NDE", "NDH", "NDL", "NDM",                             /* 244-247 */
"XRA", "XRB", "XRC", "XRD",                             /* 250-253 */
"XRE", "XRH", "XRL", "XRM",                             /* 254-257 */
"ORA", "ORB", "ORC", "ORD",                             /* 260-263 */
"ORE", "ORH", "ORL", "ORM",                             /* 264-267 */
"CPA", "CPB", "CPC", "CPD",                             /* 270-273 */
"CPE", "CPH", "CPL", "CPM",                             /* 274-277 */
"LAA", "LAB", "LAC", "LAD",                             /* 300-303 */
"LAE", "LAH", "LAL", "LAM",                             /* 304-307 */
"LBA", "LBB", "LBC", "LBD",                             /* 310-313 */
"LBE", "LBH", "LBL", "LBM",                             /* 314-317 */
"LCA", "LCB", "LCC", "LCD",                             /* 320-323 */
"LCE", "LCH", "LCL", "LCM",                             /* 324-327 */
"LDA", "LDB", "LDC", "LDD",                             /* 330-333 */
"LDE", "LDH", "LDL", "LDM",                             /* 334-337 */
"LEA", "LEB", "LEC", "LED",                             /* 340-343 */
"LEE", "LEH", "LEL", "LEM",                             /* 344-347 */
"LHA", "LHB", "LHC", "LHD",                             /* 350-353 */
"LHE", "LHH", "LHL", "LHM",                             /* 354-357 */
"LLA", "LLB", "LLC", "LLD",                             /* 360-363 */
"LLE", "LLH", "LLL", "LLM",                             /* 364-367 */
"LMA", "LMB", "LMC", "LMD",                             /* 370-373 */
"LME", "LMH", "LML", "HLT"                              /* 374-377 */
 };

/* Intel 8008 opcode lengths
 */
int32 oplen[256] = {
1,1,1,1,2,1,2,1,                    /* 000 - 007 */
1,1,1,1,2,1,2,1,                    /* 010 - 017 */
1,1,1,1,2,1,2,1,                    /* 020 - 027 */
1,1,1,1,2,1,2,1,                    /* 030 - 037 */
1,1,0,1,2,1,2,1,                    /* 040 - 047 */
1,1,0,1,2,1,2,1,                    /* 050 - 057 */
1,1,0,1,2,1,2,1,                    /* 060 - 067 */
0,0,0,1,2,1,2,1,                    /* 070 - 077 */
3,1,3,1,3,1,3,1,                    /* 100 - 107 */
3,1,3,1,3,1,3,1,                    /* 110 - 117 */
3,1,3,1,3,1,3,1,                    /* 120 - 127 */
3,1,3,1,3,1,3,1,                    /* 130 - 137 */
3,1,3,1,3,1,3,1,                    /* 140 - 147 */
3,1,3,1,3,1,3,1,                    /* 150 - 157 */
3,1,3,1,3,1,3,1,                    /* 160 - 167 */
3,1,3,1,3,1,3,1,                    /* 170 - 177 */
1,1,1,1,1,1,1,1,                    /* 200 - 207 */
1,1,1,1,1,1,1,1,                    /* 210 - 217 */
1,1,1,1,1,1,1,1,                    /* 220 - 227 */
1,1,1,1,1,1,1,1,                    /* 230 - 237 */
1,1,1,1,1,1,1,1,                    /* 240 - 247 */
1,1,1,1,1,1,1,1,                    /* 250 - 257 */
1,1,1,1,1,1,1,1,                    /* 260 - 267 */
1,1,1,1,1,1,1,1,                    /* 270 - 277 */
1,1,1,1,1,1,1,1,                    /* 300 - 307 */
1,1,1,1,1,1,1,1,                    /* 310 - 317 */
1,1,1,1,1,1,1,1,                    /* 320 - 327 */
1,1,1,1,1,1,1,1,                    /* 330 - 337 */
1,1,1,1,1,1,1,1,                    /* 340 - 347 */
1,1,1,1,1,1,1,1,                    /* 350 - 357 */
1,1,1,1,1,1,1,1,                    /* 360 - 367 */
1,1,1,1,1,1,1,1                     /* 370 - 377 */
};

/* Decode instructions
 */
t_stat sim_instr (void)
{
    int32 PC, IR, OP, DAR, reason, hi, lo, carry, states;
    /* states (Machine States) are recorded for each instruction
       but not used yet */

    PC = saved_PCreg & ADDRMASK;                        /* load local PC */
    Cflag = Cflag & 0200000;
    reason = 0;

    /* Main instruction fetch/decode loop */

    while (reason == 0) {                               /* loop until halted */
        if (sim_interval <= 0) {                        /* check clock queue */
            if ((reason = sim_process_event ())) break;
        }

        if (int_req > 0) {                              /* interrupt? */

        /* 8008 interrupts not implemented yet. */

        }                                               /* end interrupt */

    if (sim_brk_summ &&
        sim_brk_test (PC, SWMASK ('E'))) {              /* breakpoint? */
            reason = STOP_IBKPT;                        /* stop simulation */
            break;
        }

        PCXreg = PC;

        IR = OP = Mem[PC];                              /* fetch instruction */

        PC = (PC + 1) & ADDRMASK;                       /* increment PC */

        sim_interval--;

        if ((OP == 0377) || ((OP & 0376) == 0)) {       /* HLT Instructions */
            reason = STOP_HALT;
            PC--;
            states = 4;
            continue;
        }

       /* Handle below all operations which refer to registers, also
          handle jump, call, return and i/o.
          After that, a large switch statement takes care of all other opcodes.
          The original mnemonics for 8008 published 1972 are used.
          For the instructions: "s" source register, "d" destination register.
          Octal notation is used in most cases just like in the
          original documentation.
        */

        if ((OP & 0307) == 0307) {                      /* LdM */
            if (HLreg & 0xC000) {
              sim_printf("LdM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            DAR = Mem[HLreg];
            DAR = DAR & 0377;
            putreg((OP >> 3) & 07, DAR);
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0370) {                      /* LMs */
            if (HLreg & 0xC000) {
              sim_printf("LMs addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            DAR = getreg(OP & 07);
            DAR = DAR & 0377;
            Mem[HLreg] = DAR;
            states = 7;
            continue;
        }
        if ((OP & 0300) == 0300) {                      /* Lds */
            DAR = getreg(OP & 07);
            DAR = DAR & 0377;
            putreg((OP >> 3) & 07, DAR);
            states = 5;
            continue;
        }
        if (OP ==  0076) {                             /* LMI */
            if (HLreg & 0xc000) {
              sim_printf("LMI addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            DAR = Mem[PC];
            PC++;
            Mem[HLreg] = DAR;
            states = 9;
            continue;
        }
        if ((OP & 0307) == 0006) {                      /* LdI */
            putreg((OP >> 3) & 07, Mem[PC]);
            PC++;
            states = 8;
            continue;
        }
        if ((OP & 0307) == 0000) {                      /* INd */
            DAR = getreg((OP >> 3) & 07);
            DAR++;
            setinc(DAR);
            DAR = DAR & 0377;
            putreg((OP >> 3) & 07, DAR);
            states = 5;
            continue;
        }
        if ((OP & 0307) == 0001) {                      /* DCd */
            DAR = getreg((OP >> 3) & 07);
            DAR--;
            setinc(DAR);
            DAR = DAR & 0377;
            putreg((OP >> 3) & 07, DAR);
            states = 5;
            continue;
        }
        if (OP ==  0207) {                              /* ADM */
            if (HLreg & 0xC000) {
              sim_printf("LDM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            Areg += Mem[HLreg];
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0200) {                      /* ADs */
            Areg += getreg(OP & 07);
            setarith(Areg);
            Areg = Areg & 0377;
            states = 5;
            continue;
        }
        if (OP == 0217) {                               /* ACM */
            if (HLreg & 0xC000) {
              sim_printf("ACM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            carry = 0;
            if (Cflag) carry = 1;
            Areg += Mem[HLreg];
            Areg += carry;
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0210) {                      /* ACs */
            carry = 0;
            if (Cflag) carry = 1;
            Areg += getreg(OP & 07);
            Areg += carry;
            setarith(Areg);
            Areg = Areg & 0377;
            states = 5;
            continue;
        }
        if (OP == 0227) {                               /* SUM */
            if (HLreg & 0xC000) {
              sim_printf("SUM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            Areg -= Mem[HLreg];
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0220) {                      /* SUs */
            Areg -= getreg(OP & 07);
            setarith(Areg);
            Areg = Areg & 0377;
            states = 5;
            continue;
        }
        if (OP ==  0237) {                              /* SBM */
            if (HLreg & 0xC000) {
              sim_printf("SBM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            carry = 0;
            if (Cflag) carry = 1;
            Areg -= (Mem[HLreg] + carry);
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0230) {                      /* SBs */
            carry = 0;
            if (Cflag) carry = 1;
            Areg -= (getreg(OP & 07)) + carry ;
            setarith(Areg);
            Areg = Areg & 0377;
            states = 5;
            continue;
        }
        if (OP ==  0247) {                              /* NDM */
            if (HLreg & 0xC000) {
              sim_printf("NDM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            Areg &= Mem[HLreg];
            setlogical(Areg);
            Areg = Areg & 0377;
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0240) {                      /* NDs */
            Areg &= getreg(OP & 07);
            setlogical(Areg);
            Areg = Areg & 0377;
            states = 5;
            continue;
        }
        if (OP == 0257) {                               /* XRM */
            if (HLreg & 0xC000) {
              sim_printf("XRM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            Areg ^= Mem[HLreg];
            setlogical(Areg);
            Areg &= 0377;
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0250) {                      /* XRs */
            Areg ^= getreg(OP & 07);
            setlogical(Areg);
            Areg &= 0377;
            continue;
        }
        if (OP ==  0267) {                              /* ORM */
            if (HLreg & 0xC000) {
              sim_printf("ORM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            Areg |= Mem[HLreg];
            setlogical(Areg);
            Areg &= 0377;
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0260) {                      /* ORs */
            Areg |= getreg(OP & 07);
            setlogical(Areg);
            Areg &= 0377;
            states = 5;
            continue;
        }
        if (OP ==  0277) {                              /* CPM */
            if (HLreg & 0xC000) {
              sim_printf("CPM addr > 16K: %o", HLreg);
              PC--;
              reason = SCPE_STOP;
              continue;
            }
            DAR = Areg & 0377;
            DAR -= Mem[HLreg];
            setarith(DAR);
            states = 8;
            continue;
        }
        if ((OP & 0370) == 0270) {                      /* CPs */
            DAR = Areg & 0377;
            DAR -= getreg(OP & 07);
            setarith(DAR);
            states = 5;
            continue;
        }
        if ((OP & 0307) == 0104) {                      /* JMP */
            lo = Mem[PC];
            PC++;
            hi = Mem[PC];
            PC++;
            PC = ((hi << 8) + lo) & 0x3fff;
            states = 11;
            continue;
        }
        if ((OP & 0347) == 0100) {                      /* JFc */
            if (cond((OP >> 3) & 03) == 0) {
                lo = Mem[PC];
                PC++;
                hi = Mem[PC];
                PC++;
                PC = ((hi << 8) + lo) & 0x3fff;
                states = 11;
            } else {
                PC += 2;
                states = 9;
            }
            continue;
        }
        if ((OP & 0347) == 0140) {                      /* JTc */
            if (cond((OP >> 3) & 03) == 1) {
                lo = Mem[PC];
                PC++;
                hi = Mem[PC];
                PC++;
                PC = ((hi << 8) + lo) & 0x3fff;
                states = 11;
            } else {
                PC += 2;
                states = 9;
            }
            continue;
        }
        if ((OP & 0307) == 0106) {                      /* CAL */
            lo = Mem[PC];
            PC++;
            hi = Mem[PC];
            PC++;
            Smem[SPreg] = PC & 0x3fff;
            SPreg++;
            SPreg = SPreg & 07;
            PC = ((hi << 8) + lo) & 0x3fff;
            states = 11;
            continue;
        }
        if ((OP & 0347) == 0102) {                      /* CFc */
            if (cond((OP >> 3) & 03) == 0) {
                lo = Mem[PC];
                PC++;
                hi = Mem[PC];
                PC++;
                Smem[SPreg] = PC & 0x3fff;
                SPreg++;
                SPreg = SPreg & 07;
                PC = ((hi << 8) + lo) & 0x3fff;
                states = 11;
          } else {
                PC += 2;
                states = 9;
            }
            continue;
        }
        if ((OP & 0347) == 0142) {                      /* CTc */
            if (cond((OP >> 3) & 03) == 1) {
                lo = Mem[PC];
                PC++;
                hi = Mem[PC];
                PC++;
                Smem[SPreg] = PC & 0x3fff;
                SPreg++;
                SPreg = SPreg & 07;
                PC = ((hi << 8) + lo) & 0x3fff;
                states = 11;
            } else {
                PC += 2;
                states = 9;
            }
            continue;
        }
        if ((OP & 0307) == 0007) {                      /* RET */
            SPreg--;
            SPreg = SPreg & 07;
            PC = Smem[SPreg];
            states = 5;
            continue;
        }
        if ((OP & 0347) == 0003) {                      /* RFc */
            if (cond((OP >> 3) & 03) == 0) {
                SPreg--;
                SPreg = SPreg & 07;
                PC = Smem[SPreg];
                states = 5;
            } else {
                states = 3;
            }
            continue;
        }
        if ((OP & 0347) == 0043) {                      /* RTc */
            if (cond((OP >> 3) & 03) == 1) {
                SPreg--;
                SPreg = SPreg & 07;
                PC = Smem[SPreg];
                states = 5;
            } else {
                states = 3;
            }
            continue;
        }
        if ((OP & 0307) == 0005) {                      /* RST */
            Smem[SPreg] = PC & 0x3fff;
            SPreg++;
            SPreg = SPreg & 07;
            PC = OP & 0070;
            states = 5;
            continue;
        }
        if ((OP & 0301) == 0101) {                      /* INP/OUT */
            DAR = (OP & 0076) >> 1;
            if (DAR < 8) /* INP */{
                Areg = dev_table[DAR].routine(0, 0);
                states = 8;
            } else /* OUT */{
                dev_table[DAR].routine(1, Areg);
                states = 6;
            }
            continue;
        }

        /* The Instruction Decode Switch */

        switch (IR) {

        /* Arithmetic Group */

        case 0004: {                                    /* ADI */
            Areg += Mem[PC];
            PC++;
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            break;
        }
        case 0014: {                                    /* ACI */
            carry = 0;
            if (Cflag) carry = 1;
            Areg += Mem[PC];
            Areg += carry;
            PC++;
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            break;
        }
        case 0024: {                                    /* SUI */
            Areg -= Mem[PC];
            PC++;
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            break;
        }
        case 0034: {                                    /* SBI */
            carry = 0;
            if (Cflag) carry = 1;
            Areg -= (Mem[PC] + carry);
            PC++;
            setarith(Areg);
            Areg = Areg & 0377;
            states = 8;
            break;
        }

        /* Logical instructions */

        case 0044: {                                    /* NDI */
            Areg &= Mem[PC];
            PC++;
            setlogical(Areg);
            Areg &= 0377;
            states = 8;
            break;
        }
        case 0054: {                                    /* XRI */
            Areg ^= Mem[PC];
            PC++;
            setlogical(Areg);
            Areg &= 0377;
            states = 8;
            break;
        }
        case 0064: {                                    /* ORI */
            Areg |= Mem[PC];
            PC++;
            setlogical(Areg);
            Areg &= 0377;
            states = 8;
            break;
        }
        case 0074: {                                    /* CPI */
            DAR = Areg & 0377;
            DAR -= Mem[PC];
            PC++;
            setarith(DAR);
            states = 8;
            break;
        }
        case 0002: {                                   /* RLC */
            if (Areg & 0x80)
                Cflag = 0200000;
              else
                Cflag = 0;
            Areg = (Areg << 1) & 0377;
            if (Cflag)
                Areg |= 01;
            states = 5;
            break;
        }
        case 0012: {                                  /* RRC */
            if (Areg & 0x01)
                Cflag = 0200000;
              else
                Cflag = 0;
            Areg = (Areg >> 1) & 0377;
            if (Cflag)
                Areg |= 0x80;
            states = 5;
            break;
        }
        case 0022: {                                  /* RAL */
            DAR = Cflag;
            if (Areg & 0x80)
                Cflag = 0200000;
              else
                Cflag = 0;
            Areg = (Areg << 1) & 0377;
            if (DAR)
                Areg |= 0x01;
              else
                Areg &= 0xFE;
            states = 5;
            break;
        }
        case 0032: {                                  /* RAR */
            DAR = Cflag;
            if (Areg & 0x01)
                Cflag = 0200000;
              else
                Cflag = 0;
            Areg = (Areg >> 1) & 0377;
            if (DAR)
                Areg |= 0x80;
              else
                Areg &= 0x7F;
            states = 5;
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

saved_PCreg = PC;
return reason;
}

/* Test an 8008 flag condition and return 1 if true, 0 if false
 */
int32 cond(int32 con)
{
    switch (con) {
        case 0:  /* carry */
            if (Cflag != 0) return (1);
            break;
        case 1: /* zero */
            if (Zflag != 0) return (1);
            break;
        case 2: /* sign */
            if (Sflag != 0) return (1);
            break;
        case 3: /* parity */ 
            if (Pflag != 0) return (1);
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

    if (reg & 0x100)
        Cflag = 0200000;
      else
        Cflag = 0;
    if (reg & 0x80)
        Sflag = 0200000;
      else
        Sflag = 0;
    if ((reg & 0xff) == 0)
        Zflag = 0200000;
      else
        Zflag = 0;
    parity(reg);
}

/* Set the <C>arry, <S>ign, <Z>ero amd <P>arity flags following
   a logical (bitwise) operation on 'reg'.
 */
void setlogical(int32 reg)
{
    Cflag = 0;
    if (reg & 0x80)
        Sflag = 0200000;
      else
        Sflag = 0;
    if ((reg & 0xff) == 0)
        Zflag = 0200000;
      else
        Zflag = 0;
    parity(reg);
}

/* Set the Parity (P) flag based on parity of 'reg', i.e., number
   of bits on even: P=1, else P=0
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
    if (bc & 1) /* odd number of bits */
        Pflag = 0;
      else
        Pflag = 0200000;
}

/* Set the <S>ign, <Z>ero amd <P>arity flags following
   an INR/DCR operation on 'reg'.
 */
void setinc(int32 reg)
{

    if (reg & 0x80)
        Sflag = 0200000;
      else
        Sflag = 0;
    if ((reg & 0xff) == 0)
        Zflag = 0200000;
      else
        Zflag = 0;
    parity(reg);
}

/* Get an 8008 register and return it
 */
int32 getreg(int32 reg)
{
    switch (reg) {
        case 0:
            return (Areg & 0377);
        case 1:
            return (Breg & 0377);
        case 2:
            return (Creg & 0377);
        case 3:
            return (Dreg & 0377);
        case 4:
            return (Ereg & 0377);
        case 5:
            return ((HLreg >> 8) & 0377);
        case 6:
            return (HLreg & 0377);
        default:
            break;
    }
    return 0;
}

/* Put a value into an 8008 register
 */
void putreg(int32 reg, int32 val)
{
    switch (reg) {
        case 0:
            Areg = val & 0377;
            break;
        case 1:
            Breg = val & 0377;
            break;
        case 2:
            Creg = val & 0377;
            break;
        case 3:
            Dreg = val & 0377;
            break;
        case 4:
            Ereg = val & 0377;
            break;
        case 5:
            HLreg = HLreg & 0x00ff;
            HLreg = HLreg | ((val <<8) & 0xff00);
            break;
        case 6:
            HLreg = HLreg & 0xff00;
            HLreg = HLreg | (val & 0x00ff);
            break;
        default:
            break;
    }
}

/* Reset routine
 */
t_stat cpu_reset (DEVICE *dptr)
{
Cflag = 0;
Zflag = 0;
saved_PCreg = 0;
int_req = 0;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine
 */
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = Mem[addr] & 0377;
return SCPE_OK;
}

/* Memory deposit
 */
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE) return SCPE_NXM;
    Mem[addr] = val & 0377;
    return SCPE_OK;
}

/* Set memory size
 */
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++)
     mc = mc | Mem[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++)
     Mem[i] = 0377;
return SCPE_OK;
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

cflag = (uptr == NULL) || (uptr == &cpu_unit);
c1 = (val[0] >> 8) & 0177;
c2 = val[0] & 0177;
if (sw & SWMASK ('A')) {
    fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
    return SCPE_OK;
}
if (sw & SWMASK ('C')) {
    fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
    fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
    return SCPE_OK;
}
if (!(sw & SWMASK ('M')))
    return SCPE_ARG;
inst = val[0];
fprintf (of, "%s", opcode[inst]);

/* Handle INP/OUT op codes */
if ((inst & 0301) == 0101) {
  fprintf (of, " %o", (inst & 076) >> 1); 
}

if (oplen[inst] == 2) {
    if (strchr(opcode[inst], ' ') != NULL)
        fprintf (of, ",");
    else fprintf (of, " ");
    fprintf (of, "%o", val[1]);
}
if (oplen[inst] == 3) {
    adr = val[1] & 0xFF;
    adr |= (val[2] << 8) & 0xff00;
    if (strchr(opcode[inst], ' ') != NULL)
        fprintf (of, ",");
    else fprintf (of, " ");
    fprintf (of, "%o", adr);
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
int32 opcode_inp = 0;
int32 opcode_out = 0;

memset (gbuf, 0, sizeof (gbuf));
cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr))
    cptr++;                                             /* absorb spaces */
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
   or numeric (including spaces). */
while (i < sizeof (gbuf) - 4) {
    if (*cptr == ',' || *cptr == '\0' ||
         sim_isdigit(*cptr))
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

/* Handle INP and OUT opcodes */
if (toupper(gbuf[0]) == 'I' &&
    toupper(gbuf[1]) == 'N' &&
    toupper(gbuf[2]) == 'P') {
    opcode_inp = 1;
}
if (toupper(gbuf[0]) == 'O' &&
    toupper(gbuf[1]) == 'U' &&
    toupper(gbuf[2]) == 'T') {
    opcode_out = 1;
}

/* kill trailing spaces if any */
gbuf[i] = '\0';
sim_trim_endspc (gbuf);

/* kill trailing spaces if any */
gbuf[i] = '\0';
sim_trim_endspc (gbuf);

/* find opcode in table */
for (j = 0; j < 256; j++) {
    if (strcmp(gbuf, opcode[j]) == 0)
        break;
}
if (j > 255)                                            /* not found */
    return sim_messagef (SCPE_ARG, "No such opcode: %s\n", gbuf);

val[0] = j;                                             /* store opcode */
if ((oplen[j] < 2) && (opcode_inp == 0) && (opcode_out == 0))  /* if 1-byter */
    return SCPE_OK;                                     /* or not INP/OUT we are done */
if (*cptr == ',')
    cptr++;
cptr = get_glyph(cptr, gbuf, 0);                        /* get address */
sscanf(gbuf, "%o", &r);
if (opcode_inp) {
    if (r <= 7) {
        val[0] |= r << 1;
        return SCPE_OK;
    } else {
        return SCPE_ARG;
    }
}
if (opcode_out) {
    if ((8 <= r) && (r <= 31)) {
        val[0] |= r << 1;
        return SCPE_OK;
    } else {
        return SCPE_ARG;
    }
}
if (oplen[j] == 2) {
    val[1] = r & 0xFF;
    return (-1);
}
val[1] = r & 0xFF;
val[2] = (r >> 8) & 0xFF;
return (-2);
}

