/*
  Fake86: A portable, open-source 8086 PC emulator.
  Copyright (C)2010-2012 Mike Chambers

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* cpu.c: functions to emulate the 8086/V20 CPU in software. the heart of Fake86. */

#include "cpu.h"

/* simulator routines */
void set_cpuint(int32 int_num);
int32 sim_instr(void);
t_stat i8088_reset (DEVICE *dptr);
t_stat i8088_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat i8088_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);

/* memory read and write absolute address routines */
extern uint8 get_mbyte(uint32 addr);
extern uint16 get_mword(uint32 addr);
extern void put_mbyte(uint32 addr, uint8 val);
extern void put_mword(uint32 addr, uint16 val);

extern void do_trace(void);
uint16 port;                            //port called in dev_table[port]

struct idev {
    uint8 (*routine)(t_bool io, uint8 data, uint8 devnum); 
    uint16 port;
    uint16 devnum;
    uint8 dummy;
};

extern struct idev dev_table[];

uint64 curtimer, lasttimer, timerfreq;

uint8 byteregtable[8] = { regal, regcl, regdl, regbl, regah, regch, regdh, regbh };

static const uint8 parity[0x100] = {
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

uint8    RAM[0x100000], readonly[0x100000];
uint8    OP, segoverride, reptype, bootdrive = 0, hdcount = 0;
uint16 segregs[4], SEG, OFF, IP, useseg, oldsp;
uint8    tempcf, oldcf, cf, pf, af, zf, sf, tf, ifl, df, of, MOD, REGX, RM;
uint16 oper1, oper2, res16, disp16, temp16, dummy, stacksize, frametemp;
uint8    oper1b, oper2b, res8, disp8, temp8, nestlev, addrbyte;
uint32 temp1, temp2, temp3, temp4, temp5, temp32, tempaddr32, EA;
int32    result;
uint64 totalexec;

int32 AX, BX, CX, DX, DI, SI, BP, CS, DS, SS, ES, PSW, PCX, SGX, DISP, DATA8, DATA16;
static int32 SP;

//extern uint16    VGA_SC[0x100], VGA_CRTC[0x100], VGA_ATTR[0x100], VGA_GC[0x100];
//extern uint8 updatedscreen;
union _bytewordregs_ regs;

//uint8    portram[0x10000];
uint8    running = 0, debugmode, showcsip, verbose, mouseemu, didbootstrap = 0;
//uint8    ethif;

//extern uint8    vidmode;
extern uint8 verbose;
uint32 saved_PC = 0;                    /* saved program counter */
int32 int_req = 0;                      /* Interrupt request 0x01 = int, 0x02 = NMI*/

//extern void vidinterrupt();

//extern uint8 readVGA (uint32 addr32);

void intcall86 (uint8 intnum);

#define makeflagsword() \
    ( \
    2 | (uint16) cf | ((uint16) pf << 2) | ((uint16) af << 4) | ((uint16) zf << 6) | ((uint16) sf << 7) | \
    ((uint16) tf << 8) | ((uint16) ifl << 9) | ((uint16) df << 10) | ((uint16) of << 11) \
    )

#define decodeflagsword(x) { \
    temp16 = x; \
    cf = temp16 & 1; \
    pf = (temp16 >> 2) & 1; \
    af = (temp16 >> 4) & 1; \
    zf = (temp16 >> 6) & 1; \
    sf = (temp16 >> 7) & 1; \
    tf = (temp16 >> 8) & 1; \
    ifl = (temp16 >> 9) & 1; \
    df = (temp16 >> 10) & 1; \
    of = (temp16 >> 11) & 1; \
    }

//extern void    writeVGA (uint32 addr32, uint8 value);
//extern void    portout (uint16 portnum, uint8 value);
//extern void    portout16 (uint16 portnum, uint16 value);
//extern uint8    portin (uint16 portnum);
//extern uint16 portin16 (uint16 portnum);

/*
void write86 (uint32 addr32, uint8 value) {
    tempaddr32 = addr32 & 0xFFFFF;
    if (readonly[tempaddr32] || (tempaddr32 >= 0xC0000) ) {
            return;
        }

    if ( (tempaddr32 >= 0xA0000) && (tempaddr32 <= 0xBFFFF) ) {
            if ( (vidmode != 0x13) && (vidmode != 0x12) && (vidmode != 0xD) && (vidmode != 0x10) ) {
                    RAM[tempaddr32] = value;
                    updatedscreen = 1;
                }
            else if ( ( (VGA_SC[4] & 6) == 0) && (vidmode != 0xD) && (vidmode != 0x10) && (vidmode != 0x12) ) {
                    RAM[tempaddr32] = value;
                    updatedscreen = 1;
                }
            else {
                    writeVGA (tempaddr32 - 0xA0000, value);
                }

            updatedscreen = 1;
        }
    else {
            RAM[tempaddr32] = value;
        }
}

void writew86 (uint32 addr32, uint16 value) {
    write86 (addr32, (uint8) value);
    write86 (addr32 + 1, (uint8) (value >> 8) );
}

uint8 read86 (uint32 addr32) {
    addr32 &= 0xFFFFF;
    if ( (addr32 >= 0xA0000) && (addr32 <= 0xBFFFF) ) {
            if ( (vidmode == 0xD) || (vidmode == 0xE) || (vidmode == 0x10) ) return (readVGA (addr32 - 0xA0000) );
            if ( (vidmode != 0x13) && (vidmode != 0x12) && (vidmode != 0xD) ) return (RAM[addr32]);
            if ( (VGA_SC[4] & 6) == 0)
                return (RAM[addr32]);
            else
                return (readVGA (addr32 - 0xA0000) );
        }

    if (!didbootstrap) {
            RAM[0x410] = 0x41; //ugly hack to make BIOS always believe we have an EGA/VGA card installed
            RAM[0x475] = hdcount; //the BIOS doesn't have any concept of hard drives, so here's another hack
        }

    return (RAM[addr32]);
}

uint16 readw86 (uint32 addr32) {
    return ( (uint16) read86 (addr32) | (uint16) (read86 (addr32 + 1) << 8) );
}
*/

/* 8088 CPU data structures

   i8088_dev      CPU device descriptor
   i8088_unit     CPU unit descriptor
   i8088_reg      CPU register list
   i8088_mod      CPU modifiers list
*/

UNIT i8088_unit = { UDATA (NULL, 0, 0) };

REG i8088_reg[] = {
    { HRDATA (IP, saved_PC, 16) },                  /* must be first for sim_PC */
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
};

MTAB i8088_mod[] = {
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
    DEBUG_reg+DEBUG_asm,//dctrl 
    i8088_debug,        //debflags
    NULL,               //msize
    NULL                //lname
};

void flag_szp8 (uint8 value) {
    if (!value) {
            zf = 1;
        }
    else {
            zf = 0;    /* set or clear zero flag */
        }

    if (value & 0x80) {
            sf = 1;
        }
    else {
            sf = 0;    /* set or clear sign flag */
        }

    pf = parity[value]; /* retrieve parity state from lookup table */
}

void flag_szp16 (uint16 value) {
    if (!value) {
            zf = 1;
        }
    else {
            zf = 0;    /* set or clear zero flag */
        }

    if (value & 0x8000) {
            sf = 1;
        }
    else {
            sf = 0;    /* set or clear sign flag */
        }

    pf = parity[value & 255];    /* retrieve parity state from lookup table */
}

void flag_log8 (uint8 value) {
    flag_szp8 (value);
    cf = 0;
    of = 0; /* bitwise logic ops always clear carry and overflow */
}

void flag_log16 (uint16 value) {
    flag_szp16 (value);
    cf = 0;
    of = 0; /* bitwise logic ops always clear carry and overflow */
}

void flag_adc8 (uint8 v1, uint8 v2, uint8 v3) {

    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    uint16    dst;

    dst = (uint16) v1 + (uint16) v2 + (uint16) v3;
    flag_szp8 ( (uint8) dst);
    if ( ( (dst ^ v1) & (dst ^ v2) & 0x80) == 0x80) {
            of = 1;
        }
    else {
            of = 0; /* set or clear overflow flag */
        }

    if (dst & 0xFF00) {
            cf = 1;
        }
    else {
            cf = 0; /* set or clear carry flag */
        }

    if ( ( (v1 ^ v2 ^ dst) & 0x10) == 0x10) {
            af = 1;
        }
    else {
            af = 0; /* set or clear auxilliary flag */
        }
}

void flag_adc16 (uint16 v1, uint16 v2, uint16 v3) {

    uint32    dst;

    dst = (uint32) v1 + (uint32) v2 + (uint32) v3;
    flag_szp16 ( (uint16) dst);
    if ( ( ( (dst ^ v1) & (dst ^ v2) ) & 0x8000) == 0x8000) {
            of = 1;
        }
    else {
            of = 0;
        }

    if (dst & 0xFFFF0000) {
            cf = 1;
        }
    else {
            cf = 0;
        }

    if ( ( (v1 ^ v2 ^ dst) & 0x10) == 0x10) {
            af = 1;
        }
    else {
            af = 0;
        }
}

void flag_add8 (uint8 v1, uint8 v2) {
    /* v1 = destination operand, v2 = source operand */
    uint16    dst;

    dst = (uint16) v1 + (uint16) v2;
    flag_szp8 ( (uint8) dst);
    if (dst & 0xFF00) {
            cf = 1;
        }
    else {
            cf = 0;
        }

    if ( ( (dst ^ v1) & (dst ^ v2) & 0x80) == 0x80) {
            of = 1;
        }
    else {
            of = 0;
        }

    if ( ( (v1 ^ v2 ^ dst) & 0x10) == 0x10) {
            af = 1;
        }
    else {
            af = 0;
        }
}

void flag_add16 (uint16 v1, uint16 v2) {
    /* v1 = destination operand, v2 = source operand */
    uint32    dst;

    dst = (uint32) v1 + (uint32) v2;
    flag_szp16 ( (uint16) dst);
    if (dst & 0xFFFF0000) {
            cf = 1;
        }
    else {
            cf = 0;
        }

    if ( ( (dst ^ v1) & (dst ^ v2) & 0x8000) == 0x8000) {
            of = 1;
        }
    else {
            of = 0;
        }

    if ( ( (v1 ^ v2 ^ dst) & 0x10) == 0x10) {
            af = 1;
        }
    else {
            af = 0;
        }
}

void flag_sbb8 (uint8 v1, uint8 v2, uint8 v3) {

    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    uint16    dst;

    v2 += v3;
    dst = (uint16) v1 - (uint16) v2;
    flag_szp8 ( (uint8) dst);
    if (dst & 0xFF00) {
            cf = 1;
        }
    else {
            cf = 0;
        }

    if ( (dst ^ v1) & (v1 ^ v2) & 0x80) {
            of = 1;
        }
    else {
            of = 0;
        }

    if ( (v1 ^ v2 ^ dst) & 0x10) {
            af = 1;
        }
    else {
            af = 0;
        }
}

void flag_sbb16 (uint16 v1, uint16 v2, uint16 v3) {

    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    uint32    dst;

    v2 += v3;
    dst = (uint32) v1 - (uint32) v2;
    flag_szp16 ( (uint16) dst);
    if (dst & 0xFFFF0000) {
            cf = 1;
        }
    else {
            cf = 0;
        }

    if ( (dst ^ v1) & (v1 ^ v2) & 0x8000) {
            of = 1;
        }
    else {
            of = 0;
        }

    if ( (v1 ^ v2 ^ dst) & 0x10) {
            af = 1;
        }
    else {
            af = 0;
        }
}

void flag_sub8 (uint8 v1, uint8 v2) {

    /* v1 = destination operand, v2 = source operand */
    uint16    dst;

    dst = (uint16) v1 - (uint16) v2;
    flag_szp8 ( (uint8) dst);
    if (dst & 0xFF00) {
            cf = 1;
        }
    else {
            cf = 0;
        }

    if ( (dst ^ v1) & (v1 ^ v2) & 0x80) {
            of = 1;
        }
    else {
            of = 0;
        }

    if ( (v1 ^ v2 ^ dst) & 0x10) {
            af = 1;
        }
    else {
            af = 0;
        }
}

void flag_sub16 (uint16 v1, uint16 v2) {

    /* v1 = destination operand, v2 = source operand */
    uint32    dst;

    dst = (uint32) v1 - (uint32) v2;
    flag_szp16 ( (uint16) dst);
    if (dst & 0xFFFF0000) {
            cf = 1;
        }
    else {
            cf = 0;
        }

    if ( (dst ^ v1) & (v1 ^ v2) & 0x8000) {
            of = 1;
        }
    else {
            of = 0;
        }

    if ( (v1 ^ v2 ^ dst) & 0x10) {
            af = 1;
        }
    else {
            af = 0;
        }
}

void op_adc8() {
    res8 = oper1b + oper2b + cf;
    flag_adc8 (oper1b, oper2b, cf);
}

void op_adc16() {
    res16 = oper1 + oper2 + cf;
    flag_adc16 (oper1, oper2, cf);
}

void op_add8() {
    res8 = oper1b + oper2b;
    flag_add8 (oper1b, oper2b);
}

void op_add16() {
    res16 = oper1 + oper2;
    flag_add16 (oper1, oper2);
}

void op_and8() {
    res8 = oper1b & oper2b;
    flag_log8 (res8);
}

void op_and16() {
    res16 = oper1 & oper2;
    flag_log16 (res16);
}

void op_or8() {
    res8 = oper1b | oper2b;
    flag_log8 (res8);
}

void op_or16() {
    res16 = oper1 | oper2;
    flag_log16 (res16);
}

void op_xor8() {
    res8 = oper1b ^ oper2b;
    flag_log8 (res8);
}

void op_xor16() {
    res16 = oper1 ^ oper2;
    flag_log16 (res16);
}

void op_sub8() {
    res8 = oper1b - oper2b;
    flag_sub8 (oper1b, oper2b);
}

void op_sub16() {
    res16 = oper1 - oper2;
    flag_sub16 (oper1, oper2);
}

void op_sbb8() {
    res8 = oper1b - (oper2b + cf);
    flag_sbb8 (oper1b, oper2b, cf);
}

void op_sbb16() {
    res16 = oper1 - (oper2 + cf);
    flag_sbb16 (oper1, oper2, cf);
}

#define modregrm() { \
    addrbyte = getmem8(segregs[regcs], IP); \
    StepIP(1); \
    MOD = addrbyte >> 6; \
    REGX = (addrbyte >> 3) & 7; \
    RM = addrbyte & 7; \
    switch(MOD) \
    { \
    case 0: \
    if(RM == 6) { \
    disp16 = getmem16(segregs[regcs], IP); \
    StepIP(2); \
    } \
    if(((RM == 2) || (RM == 3)) && !segoverride) { \
    useseg = segregs[regss]; \
    } \
    break; \
 \
    case 1: \
    disp16 = signext(getmem8(segregs[regcs], IP)); \
    StepIP(1); \
    if(((RM == 2) || (RM == 3) || (RM == 6)) && !segoverride) { \
    useseg = segregs[regss]; \
    } \
    break; \
 \
    case 2: \
    disp16 = getmem16(segregs[regcs], IP); \
    StepIP(2); \
    if(((RM == 2) || (RM == 3) || (RM == 6)) && !segoverride) { \
    useseg = segregs[regss]; \
    } \
    break; \
 \
    default: \
    disp8 = 0; \
    disp16 = 0; \
    } \
    }

void getea (uint8 rmval) {
    uint32    tempea;

    tempea = 0;
    switch (MOD) {
            case 0:
                switch (rmval) {
                        case 0:
                            tempea = regs.wordregs[regbx] + regs.wordregs[regsi];
                            break;
                        case 1:
                            tempea = regs.wordregs[regbx] + regs.wordregs[regdi];
                            break;
                        case 2:
                            tempea = regs.wordregs[regbp] + regs.wordregs[regsi];
                            break;
                        case 3:
                            tempea = regs.wordregs[regbp] + regs.wordregs[regdi];
                            break;
                        case 4:
                            tempea = regs.wordregs[regsi];
                            break;
                        case 5:
                            tempea = regs.wordregs[regdi];
                            break;
                        case 6:
                            tempea = disp16;
                            break;
                        case 7:
                            tempea = regs.wordregs[regbx];
                            break;
                    }
                break;

            case 1:
            case 2:
                switch (rmval) {
                        case 0:
                            tempea = regs.wordregs[regbx] + regs.wordregs[regsi] + disp16;
                            break;
                        case 1:
                            tempea = regs.wordregs[regbx] + regs.wordregs[regdi] + disp16;
                            break;
                        case 2:
                            tempea = regs.wordregs[regbp] + regs.wordregs[regsi] + disp16;
                            break;
                        case 3:
                            tempea = regs.wordregs[regbp] + regs.wordregs[regdi] + disp16;
                            break;
                        case 4:
                            tempea = regs.wordregs[regsi] + disp16;
                            break;
                        case 5:
                            tempea = regs.wordregs[regdi] + disp16;
                            break;
                        case 6:
                            tempea = regs.wordregs[regbp] + disp16;
                            break;
                        case 7:
                            tempea = regs.wordregs[regbx] + disp16;
                            break;
                    }
                break;
        }

    EA = (tempea & 0xFFFF) + (useseg << 4);
}

void push (uint16 pushval) {
    putreg16 (regsp, getreg16 (regsp) - 2);
    putmem16 (segregs[regss], getreg16 (regsp), pushval);
}

uint16 pop() {

    uint16    tempval;

    tempval = getmem16 (segregs[regss], getreg16 (regsp) );
    putreg16 (regsp, getreg16 (regsp) + 2);
    return tempval;
}

t_stat i8088_reset(DEVICE *dptr) {
    //what about the flags?
    segregs[regcs] = 0xFFFF;
    IP = 0x0000;
    //regs.wordregs[regsp] = 0xFFFE;
    return SCPE_OK;
}

uint16 readrm16 (uint8 rmval) {
    if (MOD < 3) {
            getea (rmval);
//            return read86 (EA) | ( (uint16) read86 (EA + 1) << 8);
            return get_mbyte (EA) | (uint16) get_mbyte ((EA + 1) << 8);
        }
    else {
            return getreg16 (rmval);
        }
}

uint8 readrm8 (uint8 rmval) {
    if (MOD < 3) {
            getea (rmval);
//            return read86 (EA);
            return get_mbyte (EA);
        }
    else {
            return getreg8 (rmval);
        }
}

void writerm16 (uint8 rmval, uint16 value) {
    if (MOD < 3) {
            getea (rmval);
//            write86 (EA, value & 0xFF);
//            write86 (EA + 1, value >> 8);
            put_mbyte (EA, value & 0xFF);
            put_mbyte (EA + 1, value >> 8);
        }
    else {
            putreg16 (rmval, value);
        }
}

void writerm8 (uint8 rmval, uint8 value) {
    if (MOD < 3) {
            getea (rmval);
//            write86 (EA, value);
            put_mbyte (EA, value);
        }
    else {
            putreg8 (rmval, value);
        }
}

uint8 op_grp2_8 (uint8 cnt) {

    uint16    s;
    uint16    shift;
    uint16    oldcf;
    uint16    msb;

    s = oper1b;
    oldcf = cf;
#ifdef CPU_V20 //80186/V20 class CPUs limit shift count to 31
    cnt &= 0x1F;
#endif
    switch (REGX) {
            case 0: /* ROL r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        if (s & 0x80) {
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        s = s << 1;
                        s = s | cf;
                    }

                if (cnt == 1) {
                        of = cf ^ ( (s >> 7) & 1);
                    }
                break;

            case 1: /* ROR r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        cf = s & 1;
                        s = (s >> 1) | (cf << 7);
                    }

                if (cnt == 1) {
                        of = (s >> 7) ^ ( (s >> 6) & 1);
                    }
                break;

            case 2: /* RCL r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        oldcf = cf;
                        if (s & 0x80) {
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        s = s << 1;
                        s = s | oldcf;
                    }

                if (cnt == 1) {
                        of = cf ^ ( (s >> 7) & 1);
                    }
                break;

            case 3: /* RCR r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        oldcf = cf;
                        cf = s & 1;
                        s = (s >> 1) | (oldcf << 7);
                    }

                if (cnt == 1) {
                        of = (s >> 7) ^ ( (s >> 6) & 1);
                    }
                break;

            case 4:
            case 6: /* SHL r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        if (s & 0x80) {
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        s = (s << 1) & 0xFF;
                    }

                if ( (cnt == 1) && (cf == (s >> 7) ) ) {
                        of = 0;
                    }
                else {
                        of = 1;
                    }

                flag_szp8 ( (uint8) s);
                break;

            case 5: /* SHR r/m8 */
                if ( (cnt == 1) && (s & 0x80) ) {
                        of = 1;
                    }
                else {
                        of = 0;
                    }

                for (shift = 1; shift <= cnt; shift++) {
                        cf = s & 1;
                        s = s >> 1;
                    }

                flag_szp8 ( (uint8) s);
                break;

            case 7: /* SAR r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        msb = s & 0x80;
                        cf = s & 1;
                        s = (s >> 1) | msb;
                    }

                of = 0;
                flag_szp8 ( (uint8) s);
                break;
        }

    return s & 0xFF;
}

uint16 op_grp2_16 (uint8 cnt) {

    uint32    s;
    uint32    shift;
    uint32    oldcf;
    uint32    msb;

    s = oper1;
    oldcf = cf;
#ifdef CPU_V20 //80186/V20 class CPUs limit shift count to 31
    cnt &= 0x1F;
#endif
    switch (REGX) {
            case 0: /* ROL r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        if (s & 0x8000) {
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        s = s << 1;
                        s = s | cf;
                    }

                if (cnt == 1) {
                        of = cf ^ ( (s >> 15) & 1);
                    }
                break;

            case 1: /* ROR r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        cf = s & 1;
                        s = (s >> 1) | (cf << 15);
                    }

                if (cnt == 1) {
                        of = (s >> 15) ^ ( (s >> 14) & 1);
                    }
                break;

            case 2: /* RCL r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        oldcf = cf;
                        if (s & 0x8000) {
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        s = s << 1;
                        s = s | oldcf;
                    }

                if (cnt == 1) {
                        of = cf ^ ( (s >> 15) & 1);
                    }
                break;

            case 3: /* RCR r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        oldcf = cf;
                        cf = s & 1;
                        s = (s >> 1) | (oldcf << 15);
                    }

                if (cnt == 1) {
                        of = (s >> 15) ^ ( (s >> 14) & 1);
                    }
                break;

            case 4:
            case 6: /* SHL r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        if (s & 0x8000) {
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        s = (s << 1) & 0xFFFF;
                    }

                if ( (cnt == 1) && (cf == (s >> 15) ) ) {
                        of = 0;
                    }
                else {
                        of = 1;
                    }

                flag_szp16 ( (uint16) s);
                break;

            case 5: /* SHR r/m8 */
                if ( (cnt == 1) && (s & 0x8000) ) {
                        of = 1;
                    }
                else {
                        of = 0;
                    }

                for (shift = 1; shift <= cnt; shift++) {
                        cf = s & 1;
                        s = s >> 1;
                    }

                flag_szp16 ( (uint16) s);
                break;

            case 7: /* SAR r/m8 */
                for (shift = 1; shift <= cnt; shift++) {
                        msb = s & 0x8000;
                        cf = s & 1;
                        s = (s >> 1) | msb;
                    }

                of = 0;
                flag_szp16 ( (uint16) s);
                break;
        }

    return (uint16) s & 0xFFFF;
}

void op_div8 (uint16 valdiv, uint8 divisor) {
    if (divisor == 0) {
            intcall86 (0);
            return;
        }

    if ( (valdiv / (uint16) divisor) > 0xFF) {
            intcall86 (0);
            return;
        }

    regs.byteregs[regah] = valdiv % (uint16) divisor;
    regs.byteregs[regal] = valdiv / (uint16) divisor;
}

void op_idiv8 (uint16 valdiv, uint8 divisor) {

    uint16    s1;
    uint16    s2;
    uint16    d1;
    uint16    d2;
    int    sign;

    if (divisor == 0) {
            intcall86 (0);
            return;
        }

    s1 = valdiv;
    s2 = divisor;
    sign = ( ( (s1 ^ s2) & 0x8000) != 0);
    s1 = (s1 < 0x8000) ? s1 : ( (~s1 + 1) & 0xffff);
    s2 = (s2 < 0x8000) ? s2 : ( (~s2 + 1) & 0xffff);
    d1 = s1 / s2;
    d2 = s1 % s2;
    if (d1 & 0xFF00) {
            intcall86 (0);
            return;
        }

    if (sign) {
            d1 = (~d1 + 1) & 0xff;
            d2 = (~d2 + 1) & 0xff;
        }

    regs.byteregs[regah] = (uint8) d2;
    regs.byteregs[regal] = (uint8) d1;
}

void op_grp3_8() {
    oper1 = signext (oper1b);
    oper2 = signext (oper2b);
    switch (REGX) {
            case 0:
            case 1: /* TEST */
                flag_log8 (oper1b & getmem8 (segregs[regcs], IP) );
                StepIP (1);
                break;

            case 2: /* NOT */
                res8 = ~oper1b;
                break;

            case 3: /* NEG */
                res8 = (~oper1b) + 1;
                flag_sub8 (0, oper1b);
                if (res8 == 0) {
                        cf = 0;
                    }
                else {
                        cf = 1;
                    }
                break;

            case 4: /* MUL */
                temp1 = (uint32) oper1b * (uint32) regs.byteregs[regal];
                putreg16 (regax, temp1 & 0xFFFF);
                flag_szp8 ( (uint8) temp1);
                if (regs.byteregs[regah]) {
                        cf = 1;
                        of = 1;
                    }
                else {
                        cf = 0;
                        of = 0;
                    }
#ifndef CPU_V20
                zf = 0;
#endif
                break;

            case 5: /* IMUL */
                oper1 = signext (oper1b);
                temp1 = signext (regs.byteregs[regal]);
                temp2 = oper1;
                if ( (temp1 & 0x80) == 0x80) {
                        temp1 = temp1 | 0xFFFFFF00;
                    }

                if ( (temp2 & 0x80) == 0x80) {
                        temp2 = temp2 | 0xFFFFFF00;
                    }

                temp3 = (temp1 * temp2) & 0xFFFF;
                putreg16 (regax, temp3 & 0xFFFF);
                if (regs.byteregs[regah]) {
                        cf = 1;
                        of = 1;
                    }
                else {
                        cf = 0;
                        of = 0;
                    }
#ifndef CPU_V20
                zf = 0;
#endif
                break;

            case 6: /* DIV */
                op_div8 (getreg16 (regax), oper1b);
                break;

            case 7: /* IDIV */
                op_idiv8 (getreg16 (regax), oper1b);
                break;
        }
}

void op_div16 (uint32 valdiv, uint16 divisor) {
    if (divisor == 0) {
            intcall86 (0);
            return;
        }

    if ( (valdiv / (uint32) divisor) > 0xFFFF) {
            intcall86 (0);
            return;
        }

    putreg16 (regdx, valdiv % (uint32) divisor);
    putreg16 (regax, valdiv / (uint32) divisor);
}

void op_idiv16 (uint32 valdiv, uint16 divisor) {

    uint32    d1;
    uint32    d2;
    uint32    s1;
    uint32    s2;
    int    sign;

    if (divisor == 0) {
            intcall86 (0);
            return;
        }

    s1 = valdiv;
    s2 = divisor;
    s2 = (s2 & 0x8000) ? (s2 | 0xffff0000) : s2;
    sign = ( ( (s1 ^ s2) & 0x80000000) != 0);
    s1 = (s1 < 0x80000000) ? s1 : ( (~s1 + 1) & 0xffffffff);
    s2 = (s2 < 0x80000000) ? s2 : ( (~s2 + 1) & 0xffffffff);
    d1 = s1 / s2;
    d2 = s1 % s2;
    if (d1 & 0xFFFF0000) {
            intcall86 (0);
            return;
        }

    if (sign) {
            d1 = (~d1 + 1) & 0xffff;
            d2 = (~d2 + 1) & 0xffff;
        }

    putreg16 (regax, d1);
    putreg16 (regdx, d2);
}

void op_grp3_16() {
    switch (REGX) {
            case 0:
            case 1: /* TEST */
                flag_log16 (oper1 & getmem16 (segregs[regcs], IP) );
                StepIP (2);
                break;

            case 2: /* NOT */
                res16 = ~oper1;
                break;

            case 3: /* NEG */
                res16 = (~oper1) + 1;
                flag_sub16 (0, oper1);
                if (res16) {
                        cf = 1;
                    }
                else {
                        cf = 0;
                    }
                break;

            case 4: /* MUL */
                temp1 = (uint32) oper1 * (uint32) getreg16 (regax);
                putreg16 (regax, temp1 & 0xFFFF);
                putreg16 (regdx, temp1 >> 16);
                flag_szp16 ( (uint16) temp1);
                if (getreg16 (regdx) ) {
                        cf = 1;
                        of = 1;
                    }
                else {
                        cf = 0;
                        of = 0;
                    }
#ifndef CPU_V20
                zf = 0;
#endif
                break;

            case 5: /* IMUL */
                temp1 = getreg16 (regax);
                temp2 = oper1;
                if (temp1 & 0x8000) {
                        temp1 |= 0xFFFF0000;
                    }

                if (temp2 & 0x8000) {
                        temp2 |= 0xFFFF0000;
                    }

                temp3 = temp1 * temp2;
                putreg16 (regax, temp3 & 0xFFFF);    /* into register ax */
                putreg16 (regdx, temp3 >> 16);    /* into register dx */
                if (getreg16 (regdx) ) {
                        cf = 1;
                        of = 1;
                    }
                else {
                        cf = 0;
                        of = 0;
                    }
#ifndef CPU_V20
                zf = 0;
#endif
                break;

            case 6: /* DIV */
                op_div16 ( ( (uint32) getreg16 (regdx) << 16) + getreg16 (regax), oper1);
                break;

            case 7: /* DIV */
                op_idiv16 ( ( (uint32) getreg16 (regdx) << 16) + getreg16 (regax), oper1);
                break;
        }
}

void op_grp5() {
    switch (REGX) {
            case 0: /* INC Ev */
                oper2 = 1;
                tempcf = cf;
                op_add16();
                cf = tempcf;
                writerm16 (RM, res16);
                break;

            case 1: /* DEC Ev */
                oper2 = 1;
                tempcf = cf;
                op_sub16();
                cf = tempcf;
                writerm16 (RM, res16);
                break;

            case 2: /* CALL Ev */
                push (IP);
                IP = oper1;
                break;

            case 3: /* CALL Mp */
                push (segregs[regcs]);
                push (IP);
                getea (RM);
//                IP = (uint16) read86 (EA) + (uint16) read86 (EA + 1) * 256;
//                segregs[regcs] = (uint16) read86 (EA + 2) + (uint16) read86 (EA + 3) * 256;
                IP = (uint16) get_mbyte (EA) + (uint16) get_mbyte ((EA + 1) * 256);
                segregs[regcs] = (uint16) get_mbyte (EA + 2) + (uint16) get_mbyte ((EA + 3) * 256);
                break;

            case 4: /* JMP Ev */
                IP = oper1;
                break;

            case 5: /* JMP Mp */
                getea (RM);
//                IP = (uint16) read86 (EA) + (uint16) read86 (EA + 1) * 256;
//                segregs[regcs] = (uint16) read86 (EA + 2) + (uint16) read86 (EA + 3) * 256;
                IP = (uint16) get_mbyte (EA) + (uint16) get_mbyte ((EA + 1) * 256);
                segregs[regcs] = (uint16) get_mbyte (EA + 2) + (uint16) get_mbyte ((EA + 3) * 256);
                break;

            case 6: /* PUSH Ev */
                push (oper1);
                break;
        }
}

uint8 dolog = 0, didintr = 0;
FILE    *logout;
uint8 printops = 0;

//#ifdef NETWORKING_ENABLED
//extern void nethandler();
//#endif
//extern void diskhandler();
//extern void readdisk (uint8 drivenum, uint16 dstseg, uint16 dstoff, uint16 cyl, uint16 sect, uint16 head, uint16 sectcount);

void intcall86 (uint8 intnum) {
    static uint16 lastint10ax;
//    uint16 oldregax;
    didintr = 1;

    if (intnum == 0x19) didbootstrap = 1;

        /*
    switch (intnum) {
            case 0x10:
                updatedscreen = 1;
                if ( (regs.byteregs[regah]==0x00) || (regs.byteregs[regah]==0x10) ) {
                        oldregax = regs.wordregs[regax];
                        vidinterrupt();
                        regs.wordregs[regax] = oldregax;
                        if (regs.byteregs[regah]==0x10) return;
                        if (vidmode==9) return;
                    }
                if ( (regs.byteregs[regah]==0x1A) && (lastint10ax!=0x0100) ) { //the 0x0100 is a cheap hack to make it not do this if DOS EDIT/QBASIC
                        regs.byteregs[regal] = 0x1A;
                        regs.byteregs[regbl] = 0x8;
                        return;
                    }
                lastint10ax = regs.wordregs[regax];
                break;
#ifndef DISK_CONTROLLER_ATA
            case 0x19: //bootstrap
                if (bootdrive<255) { //read first sector of boot drive into 07C0:0000 and execute it
                        regs.byteregs[regdl] = bootdrive;
                        readdisk (regs.byteregs[regdl], 0x07C0, 0x0000, 0, 1, 0, 1);
                        segregs[regcs] = 0x0000;
                        IP = 0x7C00;
                    }
                else {
                        segregs[regcs] = 0xF600;    //start ROM BASIC at bootstrap if requested
                        IP = 0x0000;
                    }
                return;

            case 0x13:
            case 0xFD:
                diskhandler();
                return;
#endif
#ifdef NETWORKING_OLDCARD
            case 0xFC:
#ifdef NETWORKING_ENABLED
                nethandler();
#endif
                return;
#endif
        }
*/
    push (makeflagsword() );
    push (segregs[regcs]);
    push (IP);
    segregs[regcs] = getmem16 (0, (uint16) intnum * 4 + 2);
    IP = getmem16 (0, (uint16) intnum * 4);
    ifl = 0;
    tf = 0;
}
/*
#if defined(NETWORKING_ENABLED)
extern struct netstruct {
    uint8    enabled;
    uint8    canrecv;
    uint16    pktlen;
} net;
#endif
uint64    frametimer = 0, didwhen = 0, didticks = 0;
uint32    makeupticks = 0;
extern float    timercomp;
uint64    timerticks = 0, realticks = 0;
uint64    lastcountertimer = 0, counterticks = 10000;
extern uint8    nextintr();
extern void    timing();
*/

void set_cpuint(int32 int_num)
{
    int_req |= int_num;
}

int32 sim_instr (void) {

//    uint32    loopcount;
    uint32 reason;
    uint8    docontinue;
    static uint16 firstip;
    static uint16 trap_toggle = 0;

//    counterticks = (uint64) ( (double) timerfreq / (double) 65536.0);

//    for (loopcount = 0; loopcount < execloops; loopcount++) {

    reason = 0;                         /* clear stop reason */

    /* Main instruction fetch/decode loop */

    while (reason == 0) {               /* loop until halted */
        if (sim_interval <= 0) {        /* check clock queue */
            if ((reason = sim_process_event())) break;
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
        SGX = CS;

//            if ( (totalexec & 31) == 0) timing();

            if (trap_toggle) {
                    intcall86 (1);
                }

            if (tf) {
                    trap_toggle = 1;
                }
            else {
                    trap_toggle = 0;
                }

//            if (!trap_toggle && (ifl && (i8259.irr & (~i8259.imr) ) ) ) {
//                    intcall86 (nextintr() );    /* get next interrupt from the i8259, if any */
//                }

            reptype = 0;
            segoverride = 0;
            useseg = segregs[regds];
            docontinue = 0;
            firstip = IP;

//            if ( (segregs[regcs] == 0xF000) && (IP == 0xE066) ) didbootstrap = 0; //detect if we hit the BIOS entry point to clear didbootstrap because we've rebooted

            while (!docontinue) {
                    segregs[regcs] = segregs[regcs] & 0xFFFF;
                    IP = IP & 0xFFFF;
                    SEG = segregs[regcs];
                    OFF = IP;
                    OP = getmem8 (segregs[regcs], IP);
                    StepIP (1);

                    switch (OP) {
                                /* segment prefix check */
                            case 0x2E:    /* segment segregs[regcs] */
                                useseg = segregs[regcs];
                                segoverride = 1;
                                break;

                            case 0x3E:    /* segment segregs[regds] */
                                useseg = segregs[regds];
                                segoverride = 1;
                                break;

                            case 0x26:    /* segment segregs[reges] */
                                useseg = segregs[reges];
                                segoverride = 1;
                                break;

                            case 0x36:    /* segment segregs[regss] */
                                useseg = segregs[regss];
                                segoverride = 1;
                                break;

                                /* repetition prefix check */
                            case 0xF3:    /* REP/REPE/REPZ */
                                reptype = 1;
                                break;

                            case 0xF2:    /* REPNE/REPNZ */
                                reptype = 2;
                                break;

                            default:
                                docontinue = 1;
                                break;
                        }
                }

            totalexec++;

            /*
             * if (printops == 1) { printf("%04X:%04X - %s\n", SEG, OFF, oplist[OP]);
             * }
             */
            switch (OP) {
                    case 0x0:    /* 00 ADD Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        op_add8();
                        writerm8 (RM, res8);
                        break;

                    case 0x1:    /* 01 ADD Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        op_add16();
                        writerm16 (RM, res16);
                        break;

                    case 0x2:    /* 02 ADD Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        op_add8();
                        putreg8 (REGX, res8);
                        break;

                    case 0x3:    /* 03 ADD Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        op_add16();
                        putreg16 (REGX, res16);
                        break;

                    case 0x4:    /* 04 ADD regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        op_add8();
                        regs.byteregs[regal] = res8;
                        break;

                    case 0x5:    /* 05 ADD eAX Iv */
                        oper1 = (getreg16 (regax) );
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        op_add16();
                        putreg16 (regax, res16);
                        break;

                    case 0x6:    /* 06 PUSH segregs[reges] */
                        push (segregs[reges]);
                        break;

                    case 0x7:    /* 07 POP segregs[reges] */
                        segregs[reges] = pop();
                        break;

                    case 0x8:    /* 08 OR Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        op_or8();
                        writerm8 (RM, res8);
                        break;

                    case 0x9:    /* 09 OR Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        op_or16();
                        writerm16 (RM, res16);
                        break;

                    case 0xA:    /* 0A OR Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        op_or8();
                        putreg8 (REGX, res8);
                        break;

                    case 0xB:    /* 0B OR Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        op_or16();
                        if ( (oper1 == 0xF802) && (oper2 == 0xF802) ) {
                                sf = 0;    /* cheap hack to make Wolf 3D think we're a 286 so it plays */
                            }

                        putreg16 (REGX, res16);
                        break;

                    case 0xC:    /* 0C OR regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        op_or8();
                        regs.byteregs[regal] = res8;
                        break;

                    case 0xD:    /* 0D OR eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        op_or16();
                        putreg16 (regax, res16);
                        break;

                    case 0xE:    /* 0E PUSH segregs[regcs] */
                        push (segregs[regcs]);
                        break;

                    case 0xF: //0F POP CS
#ifndef CPU_V20
                        segregs[regcs] = pop();
#endif
                        break;

                    case 0x10:    /* 10 ADC Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        op_adc8();
                        writerm8 (RM, res8);
                        break;

                    case 0x11:    /* 11 ADC Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        op_adc16();
                        writerm16 (RM, res16);
                        break;

                    case 0x12:    /* 12 ADC Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        op_adc8();
                        putreg8 (REGX, res8);
                        break;

                    case 0x13:    /* 13 ADC Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        op_adc16();
                        putreg16 (REGX, res16);
                        break;

                    case 0x14:    /* 14 ADC regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        op_adc8();
                        regs.byteregs[regal] = res8;
                        break;

                    case 0x15:    /* 15 ADC eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        op_adc16();
                        putreg16 (regax, res16);
                        break;

                    case 0x16:    /* 16 PUSH segregs[regss] */
                        push (segregs[regss]);
                        break;

                    case 0x17:    /* 17 POP segregs[regss] */
                        segregs[regss] = pop();
                        break;

                    case 0x18:    /* 18 SBB Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        op_sbb8();
                        writerm8 (RM, res8);
                        break;

                    case 0x19:    /* 19 SBB Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        op_sbb16();
                        writerm16 (RM, res16);
                        break;

                    case 0x1A:    /* 1A SBB Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        op_sbb8();
                        putreg8 (REGX, res8);
                        break;

                    case 0x1B:    /* 1B SBB Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        op_sbb16();
                        putreg16 (REGX, res16);
                        break;

                    case 0x1C:    /* 1C SBB regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        op_sbb8();
                        regs.byteregs[regal] = res8;
                        break;

                    case 0x1D:    /* 1D SBB eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        op_sbb16();
                        putreg16 (regax, res16);
                        break;

                    case 0x1E:    /* 1E PUSH segregs[regds] */
                        push (segregs[regds]);
                        break;

                    case 0x1F:    /* 1F POP segregs[regds] */
                        segregs[regds] = pop();
                        break;

                    case 0x20:    /* 20 AND Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        op_and8();
                        writerm8 (RM, res8);
                        break;

                    case 0x21:    /* 21 AND Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        op_and16();
                        writerm16 (RM, res16);
                        break;

                    case 0x22:    /* 22 AND Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        op_and8();
                        putreg8 (REGX, res8);
                        break;

                    case 0x23:    /* 23 AND Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        op_and16();
                        putreg16 (REGX, res16);
                        break;

                    case 0x24:    /* 24 AND regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        op_and8();
                        regs.byteregs[regal] = res8;
                        break;

                    case 0x25:    /* 25 AND eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        op_and16();
                        putreg16 (regax, res16);
                        break;

                    case 0x27:    /* 27 DAA */
                        if ( ( (regs.byteregs[regal] & 0xF) > 9) || (af == 1) ) {
                                oper1 = regs.byteregs[regal] + 6;
                                regs.byteregs[regal] = oper1 & 255;
                                if (oper1 & 0xFF00) {
                                        cf = 1;
                                    }
                                else {
                                        cf = 0;
                                    }

                                af = 1;
                            }
                        else {
                                af = 0;
                            }

                        if ( ( (regs.byteregs[regal] & 0xF0) > 0x90) || (cf == 1) ) {
                                regs.byteregs[regal] = regs.byteregs[regal] + 0x60;
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        regs.byteregs[regal] = regs.byteregs[regal] & 255;
                        flag_szp8 (regs.byteregs[regal]);
                        break;

                    case 0x28:    /* 28 SUB Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        op_sub8();
                        writerm8 (RM, res8);
                        break;

                    case 0x29:    /* 29 SUB Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        op_sub16();
                        writerm16 (RM, res16);
                        break;

                    case 0x2A:    /* 2A SUB Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        op_sub8();
                        putreg8 (REGX, res8);
                        break;

                    case 0x2B:    /* 2B SUB Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        op_sub16();
                        putreg16 (REGX, res16);
                        break;

                    case 0x2C:    /* 2C SUB regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        op_sub8();
                        regs.byteregs[regal] = res8;
                        break;

                    case 0x2D:    /* 2D SUB eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        op_sub16();
                        putreg16 (regax, res16);
                        break;

                    case 0x2F:    /* 2F DAS */
                        if ( ( (regs.byteregs[regal] & 15) > 9) || (af == 1) ) {
                                oper1 = regs.byteregs[regal] - 6;
                                regs.byteregs[regal] = oper1 & 255;
                                if (oper1 & 0xFF00) {
                                        cf = 1;
                                    }
                                else {
                                        cf = 0;
                                    }

                                af = 1;
                            }
                        else {
                                af = 0;
                            }

                        if ( ( (regs.byteregs[regal] & 0xF0) > 0x90) || (cf == 1) ) {
                                regs.byteregs[regal] = regs.byteregs[regal] - 0x60;
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }

                        flag_szp8 (regs.byteregs[regal]);
                        break;

                    case 0x30:    /* 30 XOR Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        op_xor8();
                        writerm8 (RM, res8);
                        break;

                    case 0x31:    /* 31 XOR Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        op_xor16();
                        writerm16 (RM, res16);
                        break;

                    case 0x32:    /* 32 XOR Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        op_xor8();
                        putreg8 (REGX, res8);
                        break;

                    case 0x33:    /* 33 XOR Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        op_xor16();
                        putreg16 (REGX, res16);
                        break;

                    case 0x34:    /* 34 XOR regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        op_xor8();
                        regs.byteregs[regal] = res8;
                        break;

                    case 0x35:    /* 35 XOR eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        op_xor16();
                        putreg16 (regax, res16);
                        break;

                    case 0x37:    /* 37 AAA ASCII */
                        if ( ( (regs.byteregs[regal] & 0xF) > 9) || (af == 1) ) {
                                regs.byteregs[regal] = regs.byteregs[regal] + 6;
                                regs.byteregs[regah] = regs.byteregs[regah] + 1;
                                af = 1;
                                cf = 1;
                            }
                        else {
                                af = 0;
                                cf = 0;
                            }

                        regs.byteregs[regal] = regs.byteregs[regal] & 0xF;
                        break;

                    case 0x38:    /* 38 CMP Eb Gb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getreg8 (REGX);
                        flag_sub8 (oper1b, oper2b);
                        break;

                    case 0x39:    /* 39 CMP Ev Gv */
                        modregrm();
                        oper1 = readrm16 (RM);
                        oper2 = getreg16 (REGX);
                        flag_sub16 (oper1, oper2);
                        break;

                    case 0x3A:    /* 3A CMP Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        flag_sub8 (oper1b, oper2b);
                        break;

                    case 0x3B:    /* 3B CMP Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        flag_sub16 (oper1, oper2);
                        break;

                    case 0x3C:    /* 3C CMP regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        flag_sub8 (oper1b, oper2b);
                        break;

                    case 0x3D:    /* 3D CMP eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        flag_sub16 (oper1, oper2);
                        break;

                    case 0x3F:    /* 3F AAS ASCII */
                        if ( ( (regs.byteregs[regal] & 0xF) > 9) || (af == 1) ) {
                                regs.byteregs[regal] = regs.byteregs[regal] - 6;
                                regs.byteregs[regah] = regs.byteregs[regah] - 1;
                                af = 1;
                                cf = 1;
                            }
                        else {
                                af = 0;
                                cf = 0;
                            }

                        regs.byteregs[regal] = regs.byteregs[regal] & 0xF;
                        break;

                    case 0x40:    /* 40 INC eAX */
                        oldcf = cf;
                        oper1 = getreg16 (regax);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regax, res16);
                        break;

                    case 0x41:    /* 41 INC eCX */
                        oldcf = cf;
                        oper1 = getreg16 (regcx);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regcx, res16);
                        break;

                    case 0x42:    /* 42 INC eDX */
                        oldcf = cf;
                        oper1 = getreg16 (regdx);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regdx, res16);
                        break;

                    case 0x43:    /* 43 INC eBX */
                        oldcf = cf;
                        oper1 = getreg16 (regbx);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regbx, res16);
                        break;

                    case 0x44:    /* 44 INC eSP */
                        oldcf = cf;
                        oper1 = getreg16 (regsp);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regsp, res16);
                        break;

                    case 0x45:    /* 45 INC eBP */
                        oldcf = cf;
                        oper1 = getreg16 (regbp);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regbp, res16);
                        break;

                    case 0x46:    /* 46 INC eSI */
                        oldcf = cf;
                        oper1 = getreg16 (regsi);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regsi, res16);
                        break;

                    case 0x47:    /* 47 INC eDI */
                        oldcf = cf;
                        oper1 = getreg16 (regdi);
                        oper2 = 1;
                        op_add16();
                        cf = oldcf;
                        putreg16 (regdi, res16);
                        break;

                    case 0x48:    /* 48 DEC eAX */
                        oldcf = cf;
                        oper1 = getreg16 (regax);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regax, res16);
                        break;

                    case 0x49:    /* 49 DEC eCX */
                        oldcf = cf;
                        oper1 = getreg16 (regcx);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regcx, res16);
                        break;

                    case 0x4A:    /* 4A DEC eDX */
                        oldcf = cf;
                        oper1 = getreg16 (regdx);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regdx, res16);
                        break;

                    case 0x4B:    /* 4B DEC eBX */
                        oldcf = cf;
                        oper1 = getreg16 (regbx);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regbx, res16);
                        break;

                    case 0x4C:    /* 4C DEC eSP */
                        oldcf = cf;
                        oper1 = getreg16 (regsp);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regsp, res16);
                        break;

                    case 0x4D:    /* 4D DEC eBP */
                        oldcf = cf;
                        oper1 = getreg16 (regbp);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regbp, res16);
                        break;

                    case 0x4E:    /* 4E DEC eSI */
                        oldcf = cf;
                        oper1 = getreg16 (regsi);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regsi, res16);
                        break;

                    case 0x4F:    /* 4F DEC eDI */
                        oldcf = cf;
                        oper1 = getreg16 (regdi);
                        oper2 = 1;
                        op_sub16();
                        cf = oldcf;
                        putreg16 (regdi, res16);
                        break;

                    case 0x50:    /* 50 PUSH eAX */
                        push (getreg16 (regax) );
                        break;

                    case 0x51:    /* 51 PUSH eCX */
                        push (getreg16 (regcx) );
                        break;

                    case 0x52:    /* 52 PUSH eDX */
                        push (getreg16 (regdx) );
                        break;

                    case 0x53:    /* 53 PUSH eBX */
                        push (getreg16 (regbx) );
                        break;

                    case 0x54:    /* 54 PUSH eSP */
                        push (getreg16 (regsp) - 2);
                        break;

                    case 0x55:    /* 55 PUSH eBP */
                        push (getreg16 (regbp) );
                        break;

                    case 0x56:    /* 56 PUSH eSI */
                        push (getreg16 (regsi) );
                        break;

                    case 0x57:    /* 57 PUSH eDI */
                        push (getreg16 (regdi) );
                        break;

                    case 0x58:    /* 58 POP eAX */
                        putreg16 (regax, pop() );
                        break;

                    case 0x59:    /* 59 POP eCX */
                        putreg16 (regcx, pop() );
                        break;

                    case 0x5A:    /* 5A POP eDX */
                        putreg16 (regdx, pop() );
                        break;

                    case 0x5B:    /* 5B POP eBX */
                        putreg16 (regbx, pop() );
                        break;

                    case 0x5C:    /* 5C POP eSP */
                        putreg16 (regsp, pop() );
                        break;

                    case 0x5D:    /* 5D POP eBP */
                        putreg16 (regbp, pop() );
                        break;

                    case 0x5E:    /* 5E POP eSI */
                        putreg16 (regsi, pop() );
                        break;

                    case 0x5F:    /* 5F POP eDI */
                        putreg16 (regdi, pop() );
                        break;

#ifdef CPU_V20
                    case 0x60:    /* 60 PUSHA (80186+) */
                        oldsp = getreg16 (regsp);
                        push (getreg16 (regax) );
                        push (getreg16 (regcx) );
                        push (getreg16 (regdx) );
                        push (getreg16 (regbx) );
                        push (oldsp);
                        push (getreg16 (regbp) );
                        push (getreg16 (regsi) );
                        push (getreg16 (regdi) );
                        break;

                    case 0x61:    /* 61 POPA (80186+) */
                        putreg16 (regdi, pop() );
                        putreg16 (regsi, pop() );
                        putreg16 (regbp, pop() );
                        dummy = pop();
                        putreg16 (regbx, pop() );
                        putreg16 (regdx, pop() );
                        putreg16 (regcx, pop() );
                        putreg16 (regax, pop() );
                        break;

                    case 0x62: /* 62 BOUND Gv, Ev (80186+) */
                        modregrm();
                        getea (RM);
                        if (signext32 (getreg16 (REGX) ) < signext32 ( getmem16 (EA >> 4, EA & 15) ) ) {
                                intcall86 (5); //bounds check exception
                            }
                        else {
                                EA += 2;
                                if (signext32 (getreg16 (REGX) ) > signext32 ( getmem16 (EA >> 4, EA & 15) ) ) {
                                        intcall86(5); //bounds check exception
                                    }
                            }
                        break;

                    case 0x68:    /* 68 PUSH Iv (80186+) */
                        push (getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        break;

                    case 0x69:    /* 69 IMUL Gv Ev Iv (80186+) */
                        modregrm();
                        temp1 = readrm16 (RM);
                        temp2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        if ( (temp1 & 0x8000L) == 0x8000L) {
                                temp1 = temp1 | 0xFFFF0000L;
                            }

                        if ( (temp2 & 0x8000L) == 0x8000L) {
                                temp2 = temp2 | 0xFFFF0000L;
                            }

                        temp3 = temp1 * temp2;
                        putreg16 (REGX, temp3 & 0xFFFFL);
                        if (temp3 & 0xFFFF0000L) {
                                cf = 1;
                                of = 1;
                            }
                        else {
                                cf = 0;
                                of = 0;
                            }
                        break;

                    case 0x6A:    /* 6A PUSH Ib (80186+) */
                        push (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        break;

                    case 0x6B:    /* 6B IMUL Gv Eb Ib (80186+) */
                        modregrm();
                        temp1 = readrm16 (RM);
                        temp2 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if ( (temp1 & 0x8000L) == 0x8000L) {
                                temp1 = temp1 | 0xFFFF0000L;
                            }

                        if ( (temp2 & 0x8000L) == 0x8000L) {
                                temp2 = temp2 | 0xFFFF0000L;
                            }

                        temp3 = temp1 * temp2;
                        putreg16 (REGX, temp3 & 0xFFFFL);
                        if (temp3 & 0xFFFF0000L) {
                                cf = 1;
                                of = 1;
                            }
                        else {
                                cf = 0;
                                of = 0;
                            }
                        break;

                    case 0x6C:    /* 6E INSB */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        putmem8 (useseg, getreg16 (regsi) , portin (regs.wordregs[regdx]) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 1);
                                putreg16 (regdi, getreg16 (regdi) - 1);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 1);
                                putreg16 (regdi, getreg16 (regdi) + 1);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0x6D:    /* 6F INSW */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        putmem16 (useseg, getreg16 (regsi) , portin16 (regs.wordregs[regdx]) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 2);
                                putreg16 (regdi, getreg16 (regdi) - 2);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 2);
                                putreg16 (regdi, getreg16 (regdi) + 2);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0x6E:    /* 6E OUTSB */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        portout (regs.wordregs[regdx], getmem8 (useseg, getreg16 (regsi) ) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 1);
                                putreg16 (regdi, getreg16 (regdi) - 1);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 1);
                                putreg16 (regdi, getreg16 (regdi) + 1);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0x6F:    /* 6F OUTSW */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        portout16 (regs.wordregs[regdx], getmem16 (useseg, getreg16 (regsi) ) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 2);
                                putreg16 (regdi, getreg16 (regdi) - 2);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 2);
                                putreg16 (regdi, getreg16 (regdi) + 2);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;
#endif

                    case 0x70:    /* 70 JO Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (of) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x71:    /* 71 JNO Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (!of) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x72:    /* 72 JB Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (cf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x73:    /* 73 JNB Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (!cf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x74:    /* 74 JZ Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (zf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x75:    /* 75 JNZ Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (!zf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x76:    /* 76 JBE Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (cf || zf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x77:    /* 77 JA Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (!cf && !zf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x78:    /* 78 JS Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (sf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x79:    /* 79 JNS Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (!sf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x7A:    /* 7A JPE Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (pf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x7B:    /* 7B JPO Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (!pf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x7C:    /* 7C JL Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (sf != of) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x7D:    /* 7D JGE Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (sf == of) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x7E:    /* 7E JLE Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if ( (sf != of) || zf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x7F:    /* 7F JG Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (!zf && (sf == of) ) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0x80:
                    case 0x82:    /* 80/82 GRP1 Eb Ib */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        switch (REGX) {
                                case 0:
                                    op_add8();
                                    break;
                                case 1:
                                    op_or8();
                                    break;
                                case 2:
                                    op_adc8();
                                    break;
                                case 3:
                                    op_sbb8();
                                    break;
                                case 4:
                                    op_and8();
                                    break;
                                case 5:
                                    op_sub8();
                                    break;
                                case 6:
                                    op_xor8();
                                    break;
                                case 7:
                                    flag_sub8 (oper1b, oper2b);
                                    break;
                                default:
                                    break;    /* to avoid compiler warnings */
                            }

                        if (REGX < 7) {
                                writerm8 (RM, res8);
                            }
                        break;

                    case 0x81:    /* 81 GRP1 Ev Iv */
                    case 0x83:    /* 83 GRP1 Ev Ib */
                        modregrm();
                        oper1 = readrm16 (RM);
                        if (OP == 0x81) {
                                oper2 = getmem16 (segregs[regcs], IP);
                                StepIP (2);
                            }
                        else {
                                oper2 = signext (getmem8 (segregs[regcs], IP) );
                                StepIP (1);
                            }

                        switch (REGX) {
                                case 0:
                                    op_add16();
                                    break;
                                case 1:
                                    op_or16();
                                    break;
                                case 2:
                                    op_adc16();
                                    break;
                                case 3:
                                    op_sbb16();
                                    break;
                                case 4:
                                    op_and16();
                                    break;
                                case 5:
                                    op_sub16();
                                    break;
                                case 6:
                                    op_xor16();
                                    break;
                                case 7:
                                    flag_sub16 (oper1, oper2);
                                    break;
                                default:
                                    break;    /* to avoid compiler warnings */
                            }

                        if (REGX < 7) {
                                writerm16 (RM, res16);
                            }
                        break;

                    case 0x84:    /* 84 TEST Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        oper2b = readrm8 (RM);
                        flag_log8 (oper1b & oper2b);
                        break;

                    case 0x85:    /* 85 TEST Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        oper2 = readrm16 (RM);
                        flag_log16 (oper1 & oper2);
                        break;

                    case 0x86:    /* 86 XCHG Gb Eb */
                        modregrm();
                        oper1b = getreg8 (REGX);
                        putreg8 (REGX, readrm8 (RM) );
                        writerm8 (RM, oper1b);
                        break;

                    case 0x87:    /* 87 XCHG Gv Ev */
                        modregrm();
                        oper1 = getreg16 (REGX);
                        putreg16 (REGX, readrm16 (RM) );
                        writerm16 (RM, oper1);
                        break;

                    case 0x88:    /* 88 MOV Eb Gb */
                        modregrm();
                        writerm8 (RM, getreg8 (REGX) );
                        break;

                    case 0x89:    /* 89 MOV Ev Gv */
                        modregrm();
                        writerm16 (RM, getreg16 (REGX) );
                        break;

                    case 0x8A:    /* 8A MOV Gb Eb */
                        modregrm();
                        putreg8 (REGX, readrm8 (RM) );
                        break;

                    case 0x8B:    /* 8B MOV Gv Ev */
                        modregrm();
                        putreg16 (REGX, readrm16 (RM) );
                        break;

                    case 0x8C:    /* 8C MOV Ew Sw */
                        modregrm();
                        writerm16 (RM, getsegreg (REGX) );
                        break;

                    case 0x8D:    /* 8D LEA Gv M */
                        modregrm();
                        getea (RM);
                        putreg16 (REGX, EA - segbase (useseg) );
                        break;

                    case 0x8E:    /* 8E MOV Sw Ew */
                        modregrm();
                        putsegreg (REGX, readrm16 (RM) );
                        break;

                    case 0x8F:    /* 8F POP Ev */
                        modregrm();
                        writerm16 (RM, pop() );
                        break;

                    case 0x90:    /* 90 NOP */
                        break;

                    case 0x91:    /* 91 XCHG eCX eAX */
                        oper1 = getreg16 (regcx);
                        putreg16 (regcx, getreg16 (regax) );
                        putreg16 (regax, oper1);
                        break;

                    case 0x92:    /* 92 XCHG eDX eAX */
                        oper1 = getreg16 (regdx);
                        putreg16 (regdx, getreg16 (regax) );
                        putreg16 (regax, oper1);
                        break;

                    case 0x93:    /* 93 XCHG eBX eAX */
                        oper1 = getreg16 (regbx);
                        putreg16 (regbx, getreg16 (regax) );
                        putreg16 (regax, oper1);
                        break;

                    case 0x94:    /* 94 XCHG eSP eAX */
                        oper1 = getreg16 (regsp);
                        putreg16 (regsp, getreg16 (regax) );
                        putreg16 (regax, oper1);
                        break;

                    case 0x95:    /* 95 XCHG eBP eAX */
                        oper1 = getreg16 (regbp);
                        putreg16 (regbp, getreg16 (regax) );
                        putreg16 (regax, oper1);
                        break;

                    case 0x96:    /* 96 XCHG eSI eAX */
                        oper1 = getreg16 (regsi);
                        putreg16 (regsi, getreg16 (regax) );
                        putreg16 (regax, oper1);
                        break;

                    case 0x97:    /* 97 XCHG eDI eAX */
                        oper1 = getreg16 (regdi);
                        putreg16 (regdi, getreg16 (regax) );
                        putreg16 (regax, oper1);
                        break;

                    case 0x98:    /* 98 CBW */
                        if ( (regs.byteregs[regal] & 0x80) == 0x80) {
                                regs.byteregs[regah] = 0xFF;
                            }
                        else {
                                regs.byteregs[regah] = 0;
                            }
                        break;

                    case 0x99:    /* 99 CWD */
                        if ( (regs.byteregs[regah] & 0x80) == 0x80) {
                                putreg16 (regdx, 0xFFFF);
                            }
                        else {
                                putreg16 (regdx, 0);
                            }
                        break;

                    case 0x9A:    /* 9A CALL Ap */
                        oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        push (segregs[regcs]);
                        push (IP);
                        IP = oper1;
                        segregs[regcs] = oper2;
                        break;

                    case 0x9B:    /* 9B WAIT */
                        break;

                    case 0x9C:    /* 9C PUSHF */
                        push (makeflagsword() | 0xF800);
                        break;

                    case 0x9D:    /* 9D POPF */
                        temp16 = pop();
                        decodeflagsword (temp16);
                        break;

                    case 0x9E:    /* 9E SAHF */
                        decodeflagsword ( (makeflagsword() & 0xFF00) | regs.byteregs[regah]);
                        break;

                    case 0x9F:    /* 9F LAHF */
                        regs.byteregs[regah] = makeflagsword() & 0xFF;
                        break;

                    case 0xA0:    /* A0 MOV regs.byteregs[regal] Ob */
                        regs.byteregs[regal] = getmem8 (useseg, getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        break;

                    case 0xA1:    /* A1 MOV eAX Ov */
                        oper1 = getmem16 (useseg, getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        putreg16 (regax, oper1);
                        break;

                    case 0xA2:    /* A2 MOV Ob regs.byteregs[regal] */
                        putmem8 (useseg, getmem16 (segregs[regcs], IP), regs.byteregs[regal]);
                        StepIP (2);
                        break;

                    case 0xA3:    /* A3 MOV Ov eAX */
                        putmem16 (useseg, getmem16 (segregs[regcs], IP), getreg16 (regax) );
                        StepIP (2);
                        break;

                    case 0xA4:    /* A4 MOVSB */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        putmem8 (segregs[reges], getreg16 (regdi), getmem8 (useseg, getreg16 (regsi) ) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 1);
                                putreg16 (regdi, getreg16 (regdi) - 1);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 1);
                                putreg16 (regdi, getreg16 (regdi) + 1);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xA5:    /* A5 MOVSW */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        putmem16 (segregs[reges], getreg16 (regdi), getmem16 (useseg, getreg16 (regsi) ) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 2);
                                putreg16 (regdi, getreg16 (regdi) - 2);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 2);
                                putreg16 (regdi, getreg16 (regdi) + 2);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xA6:    /* A6 CMPSB */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        oper1b = getmem8 (useseg, getreg16 (regsi) );
                        oper2b = getmem8 (segregs[reges], getreg16 (regdi) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 1);
                                putreg16 (regdi, getreg16 (regdi) - 1);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 1);
                                putreg16 (regdi, getreg16 (regdi) + 1);
                            }

                        flag_sub8 (oper1b, oper2b);
                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        if ( (reptype == 1) && !zf) {
                                break;
                            }
                        else if ( (reptype == 2) && (zf == 1) ) {
                                break;
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xA7:    /* A7 CMPSW */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        oper1 = getmem16 (useseg, getreg16 (regsi) );
                        oper2 = getmem16 (segregs[reges], getreg16 (regdi) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 2);
                                putreg16 (regdi, getreg16 (regdi) - 2);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 2);
                                putreg16 (regdi, getreg16 (regdi) + 2);
                            }

                        flag_sub16 (oper1, oper2);
                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        if ( (reptype == 1) && !zf) {
                                break;
                            }

                        if ( (reptype == 2) && (zf == 1) ) {
                                break;
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xA8:    /* A8 TEST regs.byteregs[regal] Ib */
                        oper1b = regs.byteregs[regal];
                        oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        flag_log8 (oper1b & oper2b);
                        break;

                    case 0xA9:    /* A9 TEST eAX Iv */
                        oper1 = getreg16 (regax);
                        oper2 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        flag_log16 (oper1 & oper2);
                        break;

                    case 0xAA:    /* AA STOSB */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        putmem8 (segregs[reges], getreg16 (regdi), regs.byteregs[regal]);
                        if (df) {
                                putreg16 (regdi, getreg16 (regdi) - 1);
                            }
                        else {
                                putreg16 (regdi, getreg16 (regdi) + 1);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
 //                       loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xAB:    /* AB STOSW */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        putmem16 (segregs[reges], getreg16 (regdi), getreg16 (regax) );
                        if (df) {
                                putreg16 (regdi, getreg16 (regdi) - 2);
                            }
                        else {
                                putreg16 (regdi, getreg16 (regdi) + 2);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xAC:    /* AC LODSB */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        regs.byteregs[regal] = getmem8 (useseg, getreg16 (regsi) );
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 1);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 1);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xAD:    /* AD LODSW */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        oper1 = getmem16 (useseg, getreg16 (regsi) );
                        putreg16 (regax, oper1);
                        if (df) {
                                putreg16 (regsi, getreg16 (regsi) - 2);
                            }
                        else {
                                putreg16 (regsi, getreg16 (regsi) + 2);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xAE:    /* AE SCASB */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        oper1b = getmem8 (segregs[reges], getreg16 (regdi) );
                        oper2b = regs.byteregs[regal];
                        flag_sub8 (oper1b, oper2b);
                        if (df) {
                                putreg16 (regdi, getreg16 (regdi) - 1);
                            }
                        else {
                                putreg16 (regdi, getreg16 (regdi) + 1);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        if ( (reptype == 1) && !zf) {
                                break;
                            }
                        else if ( (reptype == 2) && (zf == 1) ) {
                                break;
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xAF:    /* AF SCASW */
                        if (reptype && (getreg16 (regcx) == 0) ) {
                                break;
                            }

                        oper1 = getmem16 (segregs[reges], getreg16 (regdi) );
                        oper2 = getreg16 (regax);
                        flag_sub16 (oper1, oper2);
                        if (df) {
                                putreg16 (regdi, getreg16 (regdi) - 2);
                            }
                        else {
                                putreg16 (regdi, getreg16 (regdi) + 2);
                            }

                        if (reptype) {
                                putreg16 (regcx, getreg16 (regcx) - 1);
                            }

                        if ( (reptype == 1) && !zf) {
                                break;
                            }
                        else if ( (reptype == 2) & (zf == 1) ) {
                                break;
                            }

                        totalexec++;
//                        loopcount++;
                        if (!reptype) {
                                break;
                            }

                        IP = firstip;
                        break;

                    case 0xB0:    /* B0 MOV regs.byteregs[regal] Ib */
                        DATA8 = regs.byteregs[regal] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB1:    /* B1 MOV regs.byteregs[regcl] Ib */
                        DATA8 = regs.byteregs[regcl] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB2:    /* B2 MOV regs.byteregs[regdl] Ib */
                        DATA8 = regs.byteregs[regdl] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB3:    /* B3 MOV regs.byteregs[regbl] Ib */
                        DATA8 = regs.byteregs[regbl] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB4:    /* B4 MOV regs.byteregs[regah] Ib */
                        DATA8 = regs.byteregs[regah] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB5:    /* B5 MOV regs.byteregs[regch] Ib */
                        DATA8 = regs.byteregs[regch] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB6:    /* B6 MOV regs.byteregs[regdh] Ib */
                        DATA8 = regs.byteregs[regdh] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB7:    /* B7 MOV regs.byteregs[regbh] Ib */
                        DATA8 = regs.byteregs[regbh] = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        break;

                    case 0xB8:    /* B8 MOV eAX Iv */
                        DATA16 = oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        putreg16 (regax, oper1);
                        break;

                    case 0xB9:    /* B9 MOV eCX Iv */
                        DATA16 = oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        putreg16 (regcx, oper1);
                        break;

                    case 0xBA:    /* BA MOV eDX Iv */
                        DATA16 = oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        putreg16 (regdx, oper1);
                        break;

                    case 0xBB:    /* BB MOV eBX Iv */
                        DATA16 = oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        putreg16 (regbx, oper1);
                        break;

                    case 0xBC:    /* BC MOV eSP Iv */
                        putreg16 (regsp, DATA16 = getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        break;

                    case 0xBD:    /* BD MOV eBP Iv */
                        putreg16 (regbp, DATA16 = getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        break;

                    case 0xBE:    /* BE MOV eSI Iv */
                        putreg16 (regsi, DATA16 = getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        break;

                    case 0xBF:    /* BF MOV eDI Iv */
                        putreg16 (regdi, DATA16 = getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        break;

                    case 0xC0:    /* C0 GRP2 byte imm8 (80186+) */
                        modregrm();
                        oper1b = readrm8 (RM);
                        DATA8 = oper2b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        writerm8 (RM, op_grp2_8 (oper2b) );
                        break;

                    case 0xC1:    /* C1 GRP2 word imm8 (80186+) */
                        modregrm();
                        oper1 = readrm16 (RM);
                        DATA8 = oper2 = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        writerm16 (RM, op_grp2_16 ( (uint8) oper2) );
                        break;

                    case 0xC2:    /* C2 RET Iw */
                        DATA16 = oper1 = getmem16 (segregs[regcs], IP);
                        IP = pop();
                        putreg16 (regsp, getreg16 (regsp) + oper1);
                        break;

                    case 0xC3:    /* C3 RET */
                        IP = pop();
                        break;

                    case 0xC4:    /* C4 LES Gv Mp */
                        modregrm();
                        getea (RM);
//                        putreg16 (REGX, read86 (EA) + read86 (EA + 1) * 256);
//                        segregs[reges] = read86 (EA + 2) + read86 (EA + 3) * 256;
                        putreg16 (REGX, get_mbyte (EA) + get_mbyte ((EA + 1) * 256));
                        segregs[reges] = get_mbyte (EA + 2) + get_mbyte ((EA + 3) * 256);
                        break;

                    case 0xC5:    /* C5 LDS Gv Mp */
                        modregrm();
                        getea (RM);
//                        putreg16 (REGX, read86 (EA) + read86 (EA + 1) * 256);
//                        segregs[regds] = read86 (EA + 2) + read86 (EA + 3) * 256;
                        putreg16 (REGX, get_mbyte (EA) + get_mbyte ((EA + 1) * 256));
                        segregs[regds] = get_mbyte (EA + 2) + get_mbyte ((EA + 3) * 256);
                        break;

                    case 0xC6:    /* C6 MOV Eb Ib */
                        modregrm();
                        writerm8 (RM, getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        break;

                    case 0xC7:    /* C7 MOV Ev Iv */
                        modregrm();
                        writerm16 (RM, getmem16 (segregs[regcs], IP) );
                        StepIP (2);
                        break;

                    case 0xC8:    /* C8 ENTER (80186+) */
                        stacksize = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        nestlev = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        push (getreg16 (regbp) );
                        frametemp = getreg16 (regsp);
                        if (nestlev) {
                                for (temp16 = 1; temp16 < nestlev; temp16++) {
                                        putreg16 (regbp, getreg16 (regbp) - 2);
                                        push (getreg16 (regbp) );
                                    }

                                push (getreg16 (regsp) );
                            }

                        putreg16 (regbp, frametemp);
                        putreg16 (regsp, getreg16 (regbp) - stacksize);

                        break;

                    case 0xC9:    /* C9 LEAVE (80186+) */
                        putreg16 (regsp, getreg16 (regbp) );
                        putreg16 (regbp, pop() );

                        break;

                    case 0xCA:    /* CA RETF Iw */
                        oper1 = getmem16 (segregs[regcs], IP);
                        IP = pop();
                        segregs[regcs] = pop();
                        putreg16 (regsp, getreg16 (regsp) + oper1);
                        break;

                    case 0xCB:    /* CB RETF */
                        IP = pop();;
                        segregs[regcs] = pop();
                        break;

                    case 0xCC:    /* CC INT 3 */
                        intcall86 (3);
                        break;

                    case 0xCD:    /* CD INT Ib */
                        oper1b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        intcall86 (oper1b);
                        break;

                    case 0xCE:    /* CE INTO */
                        if (of) {
                                intcall86 (4);
                            }
                        break;

                    case 0xCF:    /* CF IRET */
                        IP = pop();
                        segregs[regcs] = pop();
                        decodeflagsword (pop() );

                        /*
                         * if (net.enabled) net.canrecv = 1;
                         */
                        break;

                    case 0xD0:    /* D0 GRP2 Eb 1 */
                        modregrm();
                        oper1b = readrm8 (RM);
                        writerm8 (RM, op_grp2_8 (1) );
                        break;

                    case 0xD1:    /* D1 GRP2 Ev 1 */
                        modregrm();
                        oper1 = readrm16 (RM);
                        writerm16 (RM, op_grp2_16 (1) );
                        break;

                    case 0xD2:    /* D2 GRP2 Eb regs.byteregs[regcl] */
                        modregrm();
                        oper1b = readrm8 (RM);
                        writerm8 (RM, op_grp2_8 (regs.byteregs[regcl]) );
                        break;

                    case 0xD3:    /* D3 GRP2 Ev regs.byteregs[regcl] */
                        modregrm();
                        oper1 = readrm16 (RM);
                        writerm16 (RM, op_grp2_16 (regs.byteregs[regcl]) );
                        break;

                    case 0xD4:    /* D4 AAM I0 */
                        oper1 = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        if (!oper1) {
                                intcall86 (0);
                                break;
                            }    /* division by zero */

                        regs.byteregs[regah] = (regs.byteregs[regal] / oper1) & 255;
                        regs.byteregs[regal] = (regs.byteregs[regal] % oper1) & 255;
                        flag_szp16 (getreg16 (regax) );
                        break;

                    case 0xD5:    /* D5 AAD I0 */
                        oper1 = getmem8 (segregs[regcs], IP);
                        StepIP (1);
                        regs.byteregs[regal] = (regs.byteregs[regah] * oper1 + regs.byteregs[regal]) & 255;
                        regs.byteregs[regah] = 0;
                        flag_szp16 (regs.byteregs[regah] * oper1 + regs.byteregs[regal]);
                        sf = 0;
                        break;

                    case 0xD6:    /* D6 XLAT on V20/V30, SALC on 8086/8088 */
#ifndef CPU_V20
                        regs.byteregs[regal] = cf ? 0xFF : 0x00;
                        break;
#endif

                    case 0xD7:    /* D7 XLAT */
//                        regs.byteregs[regal] = read86(useseg * 16 + (regs.wordregs[regbx]) + regs.byteregs[regal]);
                        regs.byteregs[regal] = get_mbyte(useseg * 16 + (regs.wordregs[regbx]) + regs.byteregs[regal]);
                        break;

                    case 0xD8:
                    case 0xD9:
                    case 0xDA:
                    case 0xDB:
                    case 0xDC:
                    case 0xDE:
                    case 0xDD:
                    case 0xDF:    /* escape to x87 FPU (unsupported) */
                        modregrm();
                        break;

                    case 0xE0:    /* E0 LOOPNZ Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        putreg16 (regcx, getreg16 (regcx) - 1);
                        if ( (getreg16 (regcx) ) && !zf) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0xE1:    /* E1 LOOPZ Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        putreg16 (regcx, (getreg16 (regcx) ) - 1);
                        if ( (getreg16 (regcx) ) && (zf == 1) ) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0xE2:    /* E2 LOOP Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        putreg16 (regcx, (getreg16 (regcx) ) - 1);
                        if (getreg16 (regcx) ) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0xE3:    /* E3 JCXZ Jb */
                        temp16 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        if (! (getreg16 (regcx) ) ) {
                                IP = IP + temp16;
                            }
                        break;

                    case 0xE4:    /* E4 IN regs.byteregs[regal] Ib */
                        port = DATA8 = oper1b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
//                        regs.byteregs[regal] = (uint8) portin (oper1b);
                        regs.byteregs[regal] = dev_table[oper1b].routine(0, 0, dev_table[oper1b].devnum & 0xff);
                        break;

                    case 0xE5:    /* E5 IN eAX Ib */
                        port = DATA8 = oper1b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
//                        putreg16 (regax, portin16 (oper1b) );
//                        putreg16 (regax, 
                        putreg8(regal, dev_table[oper1b+1].routine(0, 0, dev_table[oper1b+1].devnum & 0xff));
                        putreg8(regah, dev_table[oper1b].routine(0, 0, dev_table[oper1b].devnum & 0xff));
                        break;

                    case 0xE6:    /* E6 OUT Ib regs.byteregs[regal] */
                        port = DATA8 = oper1b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
//                        portout (oper1b, regs.byteregs[regal]);
                        dev_table[oper1b].routine(1, regs.byteregs[regal], dev_table[oper1b].devnum & 0xff);
                        break;

                    case 0xE7:    /* E7 OUT Ib eAX */
                        port = DATA8 = oper1b = getmem8 (segregs[regcs], IP);
                        StepIP (1);
//                        portout16 (oper1b, (getreg16 (regax) ) );
                        dev_table[oper1b].routine(1, regs.byteregs[regah], dev_table[oper1b].devnum & 0xff);
                        dev_table[oper1b+1].routine(1, regs.byteregs[regal], dev_table[oper1b+1].devnum & 0xff);
                        break;

                    case 0xE8:    /* E8 CALL Jv */
                        oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        push (IP);
                        IP = IP + oper1;
                        break;

                    case 0xE9:    /* E9 JMP Jv */
                        oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        IP = IP + oper1;
                        break;

                    case 0xEA:    /* EA JMP Ap */
                        oper1 = getmem16 (segregs[regcs], IP);
                        StepIP (2);
                        oper2 = getmem16 (segregs[regcs], IP);
                        IP = oper1;
                        CS = segregs[regcs] = oper2;
                        break;

                    case 0xEB:    /* EB JMP Jb */
                        oper1 = signext (getmem8 (segregs[regcs], IP) );
                        StepIP (1);
                        IP = IP + oper1;
                        break;

                    case 0xEC:    /* EC IN regs.byteregs[regal] regdx */
                        port = oper1 = (getreg16 (regdx) );
//                        regs.byteregs[regal] = (uint8) portin (oper1);
                        regs.byteregs[regal] = dev_table[oper1].routine(0, 0, dev_table[oper1].devnum & 0xff);
                        break;

                    case 0xED:    /* ED IN eAX regdx */
                        port = oper1 = (getreg16 (regdx) );
//                        putreg16 (regax, portin16 (oper1) );
                        regs.byteregs[regah] = dev_table[oper1].routine(0, 0, dev_table[oper1].devnum & 0xff);
                        regs.byteregs[regal] = dev_table[oper1+1].routine(0, 0, dev_table[oper1+1].devnum & 0xff);
                        break;

                    case 0xEE:    /* EE OUT regdx regs.byteregs[regal] */
                        port = oper1 = (getreg16 (regdx) );
//                        portout (oper1, regs.byteregs[regal]);
                        dev_table[oper1].routine(1, regs.byteregs[regal], dev_table[oper1].devnum & 0xff);
                        break;

                    case 0xEF:    /* EF OUT regdx eAX */
                        port = oper1 = (getreg16 (regdx) );
//                        portout16 (oper1, (getreg16 (regax) ) );
                        dev_table[oper1].routine(1, regs.byteregs[regah], dev_table[oper1].devnum & 0xff);
                        dev_table[oper1+1].routine(1, regs.byteregs[regal], dev_table[oper1+1].devnum & 0xff);
                        break;

                    case 0xF0:    /* F0 LOCK */
                        break;

                    case 0xF4:    /* F4 HLT */
                        reason = STOP_HALT;
                        IP--;
                        break;

                    case 0xF5:    /* F5 CMC */
                        if (!cf) {
                                cf = 1;
                            }
                        else {
                                cf = 0;
                            }
                        break;

                    case 0xF6:    /* F6 GRP3a Eb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        op_grp3_8();
                        if ( (REGX > 1) && (REGX < 4) ) {
                                writerm8 (RM, res8);
                            }
                        break;

                    case 0xF7:    /* F7 GRP3b Ev */
                        modregrm();
                        oper1 = readrm16 (RM);
                        op_grp3_16();
                        if ( (REGX > 1) && (REGX < 4) ) {
                                writerm16 (RM, res16);
                            }
                        break;

                    case 0xF8:    /* F8 CLC */
                        cf = 0;
                        break;

                    case 0xF9:    /* F9 STC */
                        cf = 1;
                        break;

                    case 0xFA:    /* FA CLI */
                        ifl = 0;
                        break;

                    case 0xFB:    /* FB STI */
                        ifl = 1;
                        break;

                    case 0xFC:    /* FC CLD */
                        df = 0;
                        break;

                    case 0xFD:    /* FD STD */
                        df = 1;
                        break;

                    case 0xFE:    /* FE GRP4 Eb */
                        modregrm();
                        oper1b = readrm8 (RM);
                        oper2b = 1;
                        if (!REGX) {
                                tempcf = cf;
                                res8 = oper1b + oper2b;
                                flag_add8 (oper1b, oper2b);
                                cf = tempcf;
                                writerm8 (RM, res8);
                            }
                        else {
                                tempcf = cf;
                                res8 = oper1b - oper2b;
                                flag_sub8 (oper1b, oper2b);
                                cf = tempcf;
                                writerm8 (RM, res8);
                            }
                        break;

                    case 0xFF:    /* FF GRP5 Ev */
                        modregrm();
                        oper1 = readrm16 (RM);
                        op_grp5();
                        break;

                    default:
#ifdef CPU_V20
                        intcall86 (6); /* trip invalid OP exception (this occurs on the 80186+, 8086/8088 CPUs treat them as NOPs. */
                                       /* technically they aren't exactly like NOPs in most cases, but for our pursoses, that's accurate enough. */
#endif
                        if (verbose) {
                                printf ("Illegal OP: %02X @ %04X:%04X\n", OP, SEG, OFF);
                            }
                        break;
                }
                if (i8088_dev.dctrl & DEBUG_asm) {
                    AX = (getreg16 (regax) );
                    BX = (getreg16 (regbx) );
                    CX = (getreg16 (regcx) );
                    DX = (getreg16 (regdx) );
                    SP = (getreg16 (regsp) );
                    BP = (getreg16 (regbp) );
                    SI = (getreg16 (regsi) );
                    DI = (getreg16 (regdi) );
                    DISP = temp16;
                    PSW = makeflagsword();
                    do_trace();
                }
                if (i8088_dev.dctrl & DEBUG_reg) {
                    sim_printf("Regs: AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X IP=%04X\n",
                        AX, BX, CX, DX, SP, BP, SI, DI, IP);
                    sim_printf("Segs: CS=%04X DS=%04X ES=%04X SS=%04X Flags: %04X\n", CS, DS, ES, SS, PSW);
                }

//            if (!running) {
//                    return;
//                }
//        }
    }
    saved_PC = IP;
    return reason;
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

t_stat sim_load (FILE *fileref, const char *cptr, const char *fnam, int flag)
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

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
    return (SCPE_OK);
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

t_stat parse_sym (const char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
    return (SCPE_OK);
}

/* end of i8088.c */

