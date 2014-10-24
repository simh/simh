/*
 * Dos/PC Emulator
 * Copyright (C) 1991 Jim Hudgens
 *
 *
 * The file is part of GDE.
 *
 * GDE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 1, or (at your option)
 * any later version.
 *
 * GDE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GDE; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "altairz80_defs.h"
#include "i86.h"

extern uint32 GetBYTEExtended(register uint32 Addr);
extern void PutBYTEExtended(register uint32 Addr, const register uint32 Value);
extern int32 AX_S;      /* AX register (8086)                           */
extern int32 BX_S;      /* BX register (8086)                           */
extern int32 CX_S;      /* CX register (8086)                           */
extern int32 DX_S;      /* DX register (8086)                           */
extern int32 CS_S;      /* CS register (8086)                           */
extern int32 DS_S;      /* DS register (8086)                           */
extern int32 ES_S;      /* ES register (8086)                           */
extern int32 SS_S;      /* SS register (8086)                           */
extern int32 DI_S;      /* DI register (8086)                           */
extern int32 SI_S;      /* SI register (8086)                           */
extern int32 BP_S;      /* BP register (8086)                           */
extern int32 SPX_S;     /* SP register (8086)                           */
extern int32 IP_S;      /* IP register (8086)                           */
extern int32 FLAGS_S;   /* flags register (8086)                        */
extern int32 PCX_S;     /* PC register (8086), 20 bit                   */
extern uint32 PCX;      /* external view of PC                          */
extern UNIT cpu_unit;

void i86_intr_raise(PC_ENV *m,uint8 intrnum);
void cpu8086reset(void);
t_stat sim_instr_8086(void);
void cpu8086_intr(uint8 intrnum);

/* $Log: $
 * Revision 0.05  1992/04/12  23:16:42  hudgens
 * Many changes.   Added support for the QUICK_FETCH option,
 * so that memory accesses are faster.  Now compiles with gcc -Wall
 * and gcc -traditional and Sun cc.
 *
 * Revision 0.04  1991/07/30  01:59:56  hudgens
 * added copyright.
 *
 * Revision 0.03  1991/06/03  01:02:09  hudgens
 * fixed minor problems due to unsigned to signed short integer
 * promotions.
 *
 * Revision 0.02  1991/03/31  01:29:39  hudgens
 * Fixed segment handling (overrides, default segment accessed) in
 * routines  decode_rmXX_address and the {fetch,store}_data_{byte,word}.
 *
 * Revision 0.01  1991/03/30  21:59:49  hudgens
 * Initial checkin.
 *
 *
 */

/* this file includes subroutines which do:
   stuff involving decoding instruction formats.
   stuff involving accessess of immediate data via IP.
   etc.
*/

static void i86_intr_handle(PC_ENV *m)
{   uint16 tmp;
    uint8  intno;
    if (intr & INTR_SYNCH)   /* raised by something */
    {
        intno = m->intno;
        {
            tmp = m->R_FLG;
            push_word(m, tmp);
            CLEAR_FLAG(m, F_IF);
            CLEAR_FLAG(m, F_TF);
            /* [JCE] If we're interrupting between a segment override (or REP override)
            * and the following instruction, decrease IP to get back to the prefix */
            if (m->sysmode & (SYSMODE_SEGMASK | SYSMODE_PREFIX_REPE | SYSMODE_PREFIX_REPNE))
            {
                --m->R_IP;
            }
            /* [JCE] CS and IP were the wrong way round... */
            push_word(m, m->R_CS);
            push_word(m, m->R_IP);
            tmp = mem_access_word(m, intno * 4);
            m->R_IP = tmp;
            tmp = mem_access_word(m, intno * 4 + 2);
            m->R_CS = tmp;
        }
        intr &= ~INTR_SYNCH;    /* [JCE] Dealt with, reset flag */
    }
    /* The interrupt code can't pick up the segment override status. */
    DECODE_CLEAR_SEGOVR(m);
}

void i86_intr_raise(PC_ENV *m,uint8 intrnum)
{
    m->intno = intrnum;
    intr |= INTR_SYNCH;
}

static PC_ENV cpu8086;

void cpu8086_intr(uint8 intrnum)
{
    i86_intr_raise(&cpu8086, intrnum);
}

static void setViewRegisters(void) {
    FLAGS_S = cpu8086.R_FLG;
    AX_S = cpu8086.R_AX;
    BX_S = cpu8086.R_BX;
    CX_S = cpu8086.R_CX;
    DX_S = cpu8086.R_DX;
    SPX_S = cpu8086.R_SP;
    BP_S = cpu8086.R_BP;
    SI_S = cpu8086.R_SI;
    DI_S = cpu8086.R_DI;
    ES_S = cpu8086.R_ES;
    CS_S = cpu8086.R_CS;
    SS_S = cpu8086.R_SS;
    DS_S = cpu8086.R_DS;
    IP_S = cpu8086.R_IP;
}

static void setCPURegisters(void) {
    cpu8086.R_FLG = FLAGS_S;
    cpu8086.R_AX = AX_S;
    cpu8086.R_BX = BX_S;
    cpu8086.R_CX = CX_S;
    cpu8086.R_DX = DX_S;
    cpu8086.R_SP = SPX_S;
    cpu8086.R_BP = BP_S;
    cpu8086.R_SI = SI_S;
    cpu8086.R_DI = DI_S;
    cpu8086.R_ES = ES_S;
    cpu8086.R_CS = CS_S;
    cpu8086.R_SS = SS_S;
    cpu8086.R_DS = DS_S;
    cpu8086.R_IP = IP_S;
}

void cpu8086reset(void) {
    cpu8086.R_AX = 0x1961;
    if ((cpu8086.R_AH != 0x19) || (cpu8086.R_AL != 0x61)) {
        sim_printf("Fatal endian error - make sure to compile with '#define LOWFIRST %i'\n", 1 - LOWFIRST);
        exit(1);
    }
    /* 16 bit registers */
    cpu8086.R_AX = 0;
    cpu8086.R_BX = 0;
    cpu8086.R_CX = 0;
    cpu8086.R_DX = 0;
    /* special registers */
    cpu8086.R_SP = 0;
    cpu8086.R_BP = 0;
    cpu8086.R_SI = 0;
    cpu8086.R_DI = 0;
    cpu8086.R_IP = 0;
    cpu8086.R_FLG = F_ALWAYS_ON;
    /* segment registers */
    cpu8086.R_CS = 0;
    cpu8086.R_DS = 0;
    cpu8086.R_SS = 0;
    cpu8086.R_ES = 0;
    setViewRegisters();
}

static uint32 getFullPC(void) {
    return cpu8086.R_IP + (cpu8086.R_CS << 4);
}

extern int32 switch_cpu_now; /* hharte */

t_stat sim_instr_8086(void) {
    t_stat reason = SCPE_OK;
    uint8 op1;
    int32 newIP;
    setCPURegisters();
    intr = 0;
    newIP = PCX_S - 16 * CS_S;
    switch_cpu_now = TRUE; /* hharte */
    if ((0 <= newIP) && (newIP <= 0xffff))
        cpu8086.R_IP = newIP;
    else {
        if (CS_S != ((PCX_S & 0xf0000) >> 4)) {
            cpu8086.R_CS = (PCX_S & 0xf0000) >> 4;
            if (cpu_unit.flags & UNIT_CPU_VERBOSE)
                sim_printf("CPU: " ADDRESS_FORMAT " Segment register CS set to %04x" NLP, PCX, cpu8086.R_CS);
        }
        cpu8086.R_IP = PCX_S & 0xffff;
    }
    while (switch_cpu_now == TRUE) {                        /* loop until halted    */
        if (sim_interval <= 0) {                            /* check clock queue    */
#if !UNIX_PLATFORM
            if ((reason = sim_poll_kbd()) == SCPE_STOP)     /* poll on platforms without reliable signalling */
                break;
#endif
            if ( (reason = sim_process_event()) )
                break;
        }
        if (sim_brk_summ && sim_brk_test(getFullPC(), SWMASK('E'))) {   /* breakpoint?      */
            reason = STOP_IBKPT;                                        /* stop simulation  */
            break;
        }
        PCX = getFullPC();
        op1 = GetBYTEExtended((((uint32)cpu8086.R_CS<<4) + cpu8086.R_IP) & 0xFFFFF);
        if (sim_brk_summ && sim_brk_test(op1, (1u << SIM_BKPT_V_SPC) | SWMASK('I'))) {  /* instruction breakpoint?  */
            reason = STOP_IBKPT;                                                        /* stop simulation          */
            break;
        }
        sim_interval--;
        cpu8086.R_IP++;
        (*(i86_optab[op1]))(&cpu8086);
        if (intr & INTR_HALTED) {
            reason = STOP_HALT;
            intr &= ~INTR_HALTED;
            break;
        }
        if (intr & INTR_ILLEGAL_OPCODE) {
            intr &= ~INTR_ILLEGAL_OPCODE;
            if (cpu_unit.flags & UNIT_CPU_OPSTOP) {
                reason = STOP_OPCODE;
                break;
            }
        }
        if (((intr & INTR_SYNCH) && (cpu8086.intno == 0 || cpu8086.intno == 2)) ||
            (ACCESS_FLAG(&cpu8086, F_IF))) {
            /* [JCE] Reversed the sense of this ACCESS_FLAG; it's set for interrupts
            enabled, not interrupts blocked i.e. either not blockable (intr 0 or 2)
            or the IF flag not set so interrupts not blocked */
            /* hharte: if a segment override exists, then treat that as "atomic" and do not handle
             * an interrupt until the override is cleared.
             * Not sure if this is the way an 8086 really works, need to find out for sure.
             * Also, what about the REPE prefix?
             */
            if ((cpu8086.sysmode & SYSMODE_SEGMASK) == 0) {
                i86_intr_handle(&cpu8086);
            }
        }
    }
    /* It we stopped processing instructions because of a switch to the other
     * CPU, then fixup the reason code.
     */
    if (switch_cpu_now == FALSE) {
        reason = SCPE_OK;
        PCX += 2;
        PCX_S = PCX;
    } else {
        PCX_S = (reason == STOP_HALT) | (reason == STOP_OPCODE) ? PCX : getFullPC();
    }

    setViewRegisters();
    return reason;
}

void halt_sys(PC_ENV *m)
{
    intr |= INTR_HALTED;
}

/* once the instruction is fetched, an optional byte follows which
   has 3 fields encoded in it.  This routine  fetches the byte
   and breaks into the three fields.
   This has been changed, in an attempt to reduce the amount of
   executed code for this frequently executed subroutine.  If this
   works, then it may pay to somehow inline it.
   */

#ifdef NOTDEF
/* this code generated the following table */
main()
{    int i;
    sim_printf("\n\nstruct modrm{ uint8 mod,rh,rl;} modrmtab[] = {\n");
    for (i=0; i<256; i++)
    {
       sim_printf("{%d,%d,%d}, ",((i&0xc0)>>6),((i&0x38)>>3),(i&0x07));
           if (i%4==3)
         sim_printf("/* %d to %d */\n",i&0xfc,i);
    }
    sim_printf("};\n\n");
}
#endif

struct modrm { uint16 mod, rh, rl; };
static struct modrm modrmtab[] = {
    {0,0,0}, {0,0,1}, {0,0,2}, {0,0,3}, /* 0 to 3 */
    {0,0,4}, {0,0,5}, {0,0,6}, {0,0,7}, /* 4 to 7 */
    {0,1,0}, {0,1,1}, {0,1,2}, {0,1,3}, /* 8 to 11 */
    {0,1,4}, {0,1,5}, {0,1,6}, {0,1,7}, /* 12 to 15 */
    {0,2,0}, {0,2,1}, {0,2,2}, {0,2,3}, /* 16 to 19 */
    {0,2,4}, {0,2,5}, {0,2,6}, {0,2,7}, /* 20 to 23 */
    {0,3,0}, {0,3,1}, {0,3,2}, {0,3,3}, /* 24 to 27 */
    {0,3,4}, {0,3,5}, {0,3,6}, {0,3,7}, /* 28 to 31 */
    {0,4,0}, {0,4,1}, {0,4,2}, {0,4,3}, /* 32 to 35 */
    {0,4,4}, {0,4,5}, {0,4,6}, {0,4,7}, /* 36 to 39 */
    {0,5,0}, {0,5,1}, {0,5,2}, {0,5,3}, /* 40 to 43 */
    {0,5,4}, {0,5,5}, {0,5,6}, {0,5,7}, /* 44 to 47 */
    {0,6,0}, {0,6,1}, {0,6,2}, {0,6,3}, /* 48 to 51 */
    {0,6,4}, {0,6,5}, {0,6,6}, {0,6,7}, /* 52 to 55 */
    {0,7,0}, {0,7,1}, {0,7,2}, {0,7,3}, /* 56 to 59 */
    {0,7,4}, {0,7,5}, {0,7,6}, {0,7,7}, /* 60 to 63 */
    {1,0,0}, {1,0,1}, {1,0,2}, {1,0,3}, /* 64 to 67 */
    {1,0,4}, {1,0,5}, {1,0,6}, {1,0,7}, /* 68 to 71 */
    {1,1,0}, {1,1,1}, {1,1,2}, {1,1,3}, /* 72 to 75 */
    {1,1,4}, {1,1,5}, {1,1,6}, {1,1,7}, /* 76 to 79 */
    {1,2,0}, {1,2,1}, {1,2,2}, {1,2,3}, /* 80 to 83 */
    {1,2,4}, {1,2,5}, {1,2,6}, {1,2,7}, /* 84 to 87 */
    {1,3,0}, {1,3,1}, {1,3,2}, {1,3,3}, /* 88 to 91 */
    {1,3,4}, {1,3,5}, {1,3,6}, {1,3,7}, /* 92 to 95 */
    {1,4,0}, {1,4,1}, {1,4,2}, {1,4,3}, /* 96 to 99 */
    {1,4,4}, {1,4,5}, {1,4,6}, {1,4,7}, /* 100 to 103 */
    {1,5,0}, {1,5,1}, {1,5,2}, {1,5,3}, /* 104 to 107 */
    {1,5,4}, {1,5,5}, {1,5,6}, {1,5,7}, /* 108 to 111 */
    {1,6,0}, {1,6,1}, {1,6,2}, {1,6,3}, /* 112 to 115 */
    {1,6,4}, {1,6,5}, {1,6,6}, {1,6,7}, /* 116 to 119 */
    {1,7,0}, {1,7,1}, {1,7,2}, {1,7,3}, /* 120 to 123 */
    {1,7,4}, {1,7,5}, {1,7,6}, {1,7,7}, /* 124 to 127 */
    {2,0,0}, {2,0,1}, {2,0,2}, {2,0,3}, /* 128 to 131 */
    {2,0,4}, {2,0,5}, {2,0,6}, {2,0,7}, /* 132 to 135 */
    {2,1,0}, {2,1,1}, {2,1,2}, {2,1,3}, /* 136 to 139 */
    {2,1,4}, {2,1,5}, {2,1,6}, {2,1,7}, /* 140 to 143 */
    {2,2,0}, {2,2,1}, {2,2,2}, {2,2,3}, /* 144 to 147 */
    {2,2,4}, {2,2,5}, {2,2,6}, {2,2,7}, /* 148 to 151 */
    {2,3,0}, {2,3,1}, {2,3,2}, {2,3,3}, /* 152 to 155 */
    {2,3,4}, {2,3,5}, {2,3,6}, {2,3,7}, /* 156 to 159 */
    {2,4,0}, {2,4,1}, {2,4,2}, {2,4,3}, /* 160 to 163 */
    {2,4,4}, {2,4,5}, {2,4,6}, {2,4,7}, /* 164 to 167 */
    {2,5,0}, {2,5,1}, {2,5,2}, {2,5,3}, /* 168 to 171 */
    {2,5,4}, {2,5,5}, {2,5,6}, {2,5,7}, /* 172 to 175 */
    {2,6,0}, {2,6,1}, {2,6,2}, {2,6,3}, /* 176 to 179 */
    {2,6,4}, {2,6,5}, {2,6,6}, {2,6,7}, /* 180 to 183 */
    {2,7,0}, {2,7,1}, {2,7,2}, {2,7,3}, /* 184 to 187 */
    {2,7,4}, {2,7,5}, {2,7,6}, {2,7,7}, /* 188 to 191 */
    {3,0,0}, {3,0,1}, {3,0,2}, {3,0,3}, /* 192 to 195 */
    {3,0,4}, {3,0,5}, {3,0,6}, {3,0,7}, /* 196 to 199 */
    {3,1,0}, {3,1,1}, {3,1,2}, {3,1,3}, /* 200 to 203 */
    {3,1,4}, {3,1,5}, {3,1,6}, {3,1,7}, /* 204 to 207 */
    {3,2,0}, {3,2,1}, {3,2,2}, {3,2,3}, /* 208 to 211 */
    {3,2,4}, {3,2,5}, {3,2,6}, {3,2,7}, /* 212 to 215 */
    {3,3,0}, {3,3,1}, {3,3,2}, {3,3,3}, /* 216 to 219 */
    {3,3,4}, {3,3,5}, {3,3,6}, {3,3,7}, /* 220 to 223 */
    {3,4,0}, {3,4,1}, {3,4,2}, {3,4,3}, /* 224 to 227 */
    {3,4,4}, {3,4,5}, {3,4,6}, {3,4,7}, /* 228 to 231 */
    {3,5,0}, {3,5,1}, {3,5,2}, {3,5,3}, /* 232 to 235 */
    {3,5,4}, {3,5,5}, {3,5,6}, {3,5,7}, /* 236 to 239 */
    {3,6,0}, {3,6,1}, {3,6,2}, {3,6,3}, /* 240 to 243 */
    {3,6,4}, {3,6,5}, {3,6,6}, {3,6,7}, /* 244 to 247 */
    {3,7,0}, {3,7,1}, {3,7,2}, {3,7,3}, /* 248 to 251 */
    {3,7,4}, {3,7,5}, {3,7,6}, {3,7,7}, /* 252 to 255 */
};

void fetch_decode_modrm(PC_ENV *m, uint16 *mod, uint16 *regh, uint16 *regl)
{    uint8 fetched;
    register struct modrm *p;
    /* do the fetch in real mode.  Shift the CS segment register
       over by 4 bits, and add in the IP register.  Index into
       the system memory.
       */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
    fetched = GetBYTEExtended(((m->R_CS << 4) + (m->R_IP++)) & 0xFFFFF);

#ifdef NOTDEF
    *mod = ((fetched&0xc0)>>6);
    *regh= ((fetched&0x38)>>3);
    *regl= (fetched&0x7);
#else
    p = modrmtab + fetched;
    *mod = p->mod;
    *regh= p->rh;
    *regl= p->rl;
#endif

}

/*
    return a pointer to the register given by the R/RM field of
    the modrm byte, for byte operands.
    Also enables the decoding of instructions.
*/
uint8 *decode_rm_byte_register(PC_ENV *m, int reg)
{
    switch(reg)
    {
     case 0:
       return &m->R_AL;
       break;
     case 1:
       return &m->R_CL;
       break;
     case 2:
       return &m->R_DL;
       break;
     case 3:
       return &m->R_BL;
       break;
     case 4:
       return &m->R_AH;
       break;
     case 5:
       return &m->R_CH;
       break;
     case 6:
       return &m->R_DH;
       break;
     case 7:
       return &m->R_BH;
       break;
    }
    halt_sys(m);
    return NULL; /* NOT REACHED OR REACHED ON ERROR */
}

/*
    return a pointer to the register given by the R/RM field of
    the modrm byte, for word operands.
    Also enables the decoding of instructions.
*/
uint16 *decode_rm_word_register(PC_ENV *m, int reg)
{
    switch(reg)
    {
     case 0:
       return &m->R_AX;
       break;
     case 1:
       return &m->R_CX;
       break;
     case 2:
       return &m->R_DX;
       break;
     case 3:
       return &m->R_BX;
       break;
     case 4:
       return &m->R_SP;
       break;
     case 5:
       return &m->R_BP;
       break;
     case 6:
       return &m->R_SI;
       break;
     case 7:
       return &m->R_DI;
       break;
    }
    halt_sys(m);
    return NULL; /* NOTREACHED OR REACHED ON ERROR*/
}

/*
    return a pointer to the register given by the R/RM field of
    the modrm byte, for word operands, modified from above
    for the weirdo special case of segreg operands.
    Also enables the decoding of instructions.
*/
uint16 *decode_rm_seg_register(PC_ENV *m, int reg)
{
    switch(reg)
    {
     case 0:
       return &m->R_ES;
       break;
     case 1:
       return &m->R_CS;
       break;
     case 2:
       return &m->R_SS;
       break;
     case 3:
       return &m->R_DS;
       break;
     case 4:
     case 5:
     case 6:
     case 7:
       break;
    }
    halt_sys(m);
    return NULL;  /* NOT REACHED OR REACHED ON ERROR */
}

/* once the instruction is fetched, an optional byte follows which
    has 3 fields encoded in it.  This routine  fetches the byte
    and breaks into the three fields.
*/
uint8 fetch_byte_imm(PC_ENV *m)
{
    uint8 fetched;
    /* do the fetch in real mode.  Shift the CS segment register
       over by 4 bits, and add in the IP register.  Index into
       the system memory.
       */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
    fetched = GetBYTEExtended((((uint32)m->R_CS << 4) + (m->R_IP++)) & 0xFFFFF);
    return fetched;
}

uint16 fetch_word_imm(PC_ENV *m)
{
    uint16 fetched;
    /* do the fetch in real mode.  Shift the CS segment register
       over by 4 bits, and add in the IP register.  Index into
       the system PC_ENVory.
       */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
    fetched = GetBYTEExtended((((uint32)m->R_CS << 4) + (m->R_IP++)) & 0xFFFFF);
    fetched |= (GetBYTEExtended((((uint32)m->R_CS << 4) + (m->R_IP++)) & 0xFFFFF) << 8);
    return fetched;
}

/*
    return the offset given by mod=00 addressing.
    Also enables the decoding of instructions.
*/
uint16 decode_rm00_address(PC_ENV *m, int rm)
{
    uint16 offset;
    /* note the code which specifies the corresponding segment (ds vs ss)
       below in the case of [BP+..].  The assumption here is that at the
       point that this subroutine is called, the bit corresponding to
       SYSMODE_SEG_DS_SS will be zero.  After every instruction
       except the segment override instructions, this bit (as well
       as any bits indicating segment overrides) will be clear.  So
       if a SS access is needed, set this bit.  Otherwise, DS access
       occurs (unless any of the segment override bits are set).
       */
    switch(rm)
    {
     case 0:
       return (int16)m->R_BX + (int16)m->R_SI;
       break;
     case 1:
       return (int16)m->R_BX + (int16)m->R_DI;
       break;
     case 2:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + (int16)m->R_SI;
       break;
     case 3:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + (int16)m->R_DI;
       break;
     case 4:
       return m->R_SI;
       break;
     case 5:
       return m->R_DI;
       break;
     case 6:
       offset = (int16)fetch_word_imm(m);
       return offset;
       break;
     case 7:
       return m->R_BX;
    }
    halt_sys(m);
    return 0;
}

/*
    return the offset given by mod=01 addressing.
    Also enables the decoding of instructions.
*/
uint16 decode_rm01_address(PC_ENV *m, int rm)
{
    int8 displacement;
    /* note comment on decode_rm00_address above */
    displacement = (int8)fetch_byte_imm(m); /* !!!! Check this */
    switch(rm)
    {
     case 0:
       return (int16)m->R_BX + (int16)m->R_SI + displacement;
       break;
     case 1:
       return (int16)m->R_BX + (int16)m->R_DI + displacement;
       break;
     case 2:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + (int16)m->R_SI + displacement;
       break;
     case 3:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + (int16)m->R_DI + displacement;
       break;
     case 4:
       return (int16)m->R_SI + displacement;
       break;
     case 5:
       return (int16)m->R_DI + displacement;
       break;
     case 6:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + displacement;
       break;
     case 7:
       return (int16)m->R_BX + displacement;
       break;
    }
    halt_sys(m);
    return 0;     /* SHOULD NOT HAPPEN */
}

/*
    return the offset given by mod=01 addressing.
    Also enables the decoding of instructions.
*/
uint16 decode_rm10_address(PC_ENV *m, int rm)
{
    int16 displacement;
    /* note comment on decode_rm00_address above */
    displacement = (int16)fetch_word_imm(m);
    switch(rm)
    {
     case 0:
       return (int16)m->R_BX + (int16)m->R_SI + displacement;
       break;
     case 1:
       return (int16)m->R_BX + (int16)m->R_DI + displacement;
       break;
     case 2:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + (int16)m->R_SI + displacement;
       break;
     case 3:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + (int16)m->R_DI + displacement;
       break;
     case 4:
       return (int16)m->R_SI + displacement;
       break;
     case 5:
       return (int16)m->R_DI + displacement;
       break;
     case 6:
       m->sysmode |= SYSMODE_SEG_DS_SS;
       return (int16)m->R_BP + displacement;
       break;
     case 7:
       return (int16)m->R_BX + displacement;
       break;
    }
    halt_sys(m);
    return 0;
    /*NOTREACHED */
}

/* fetch a byte of data, given an offset, the current register set,
    and a descriptor for memory.
*/
uint8 fetch_data_byte(PC_ENV *m, uint16 offset)
{
    register uint8 value;
    /* this code originally completely broken, and never showed
       up since the DS segments === SS segment in all test cases.
       It had been originally assumed, that all access to data would
       involve the DS register unless there was a segment override.
       Not so.   Address modes such as -3[BP] or 10[BP+SI] all
       refer to addresses relative to the SS.  So, at the minimum,
       all decodings of addressing modes would have to set/clear
       a bit describing whether the access is relative to DS or SS.
       That is the function of the cpu-state-varible  m->sysmode.
       There are several potential states:
       repe prefix seen  (handled elsewhere)
       repne prefix seen  (ditto)
       cs segment override
       ds segment override
       es segment override
       ss segment override
       ds/ss select (in absense of override)
       Each of the above 7 items are handled with a bit in the sysmode
       field.
       The latter 5 can be implemented as a simple state machine:
       */
    switch(m->sysmode & SYSMODE_SEGMASK)
    {
     case 0:
       /* default case: use ds register */
       value = GetBYTEExtended(((uint32)m->R_DS<<4) + offset);
       break;
     case SYSMODE_SEG_DS_SS:
       /* non-overridden, use ss register */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
       value  = GetBYTEExtended((((uint32)m->R_SS << 4) +  offset) & 0xFFFFF);
       break;
     case SYSMODE_SEGOVR_CS:
       /* ds overridden */
     case SYSMODE_SEGOVR_CS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use cs register */
        /* [JCE] Wrap at 1Mb (the A20 gate) */
       value  = GetBYTEExtended((((uint32)m->R_CS << 4) + offset) & 0xFFFFF);
       break;
     case SYSMODE_SEGOVR_DS:
       /* ds overridden --- shouldn't happen, but hey. */
     case SYSMODE_SEGOVR_DS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ds register */
        /* [JCE] Wrap at 1Mb (the A20 gate) */
       value  = GetBYTEExtended((((uint32)m->R_DS << 4) + offset) & 0xFFFFF);
       break;
     case SYSMODE_SEGOVR_ES:
       /* ds overridden */
     case SYSMODE_SEGOVR_ES|SYSMODE_SEG_DS_SS:
       /* ss overridden, use es register */
        /* [JCE] Wrap at 1Mb (the A20 gate) */
       value  = GetBYTEExtended((((uint32)m->R_ES << 4) + offset) & 0xFFFFF);
       break;
     case SYSMODE_SEGOVR_SS:
       /* ds overridden */
     case SYSMODE_SEGOVR_SS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ss register === should not happen */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
       value  =  GetBYTEExtended((((uint32)m->R_SS << 4) + offset) & 0xFFFFF);
       break;
     default:
       sim_printf("error: should not happen:  multiple overrides. " NLP);
       value = 0;
       halt_sys(m);
    }
    return value;
}

/* fetch a byte of data, given an offset, the current register set,
    and a descriptor for memory.
*/
uint8 fetch_data_byte_abs(PC_ENV *m, uint16 segment, uint16 offset)
{
    register uint8 value;
    uint32 addr;
    /* note, cannot change this, since we do not know the ID of the segment. */
/* [JCE] Simulate wrap at top of memory (the A20 gate) */
/*    addr = (segment << 4) + offset; */
    addr = ((segment << 4) + offset) & 0xFFFFF;
    value = GetBYTEExtended(addr);
    return value;
}

/* fetch a byte of data, given an offset, the current register set,
    and a descriptor for memory.
*/
uint16 fetch_data_word(PC_ENV *m, uint16 offset)
{
    uint16 value;
    /* See note above in fetch_data_byte. */
    switch(m->sysmode & SYSMODE_SEGMASK)
    {
     case 0:
       /* default case: use ds register */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
       value = GetBYTEExtended((((uint32)m->R_DS << 4) + offset) & 0xFFFFF)
         | (GetBYTEExtended((((uint32)m->R_DS << 4) +
                (uint16)(offset + 1)) & 0xFFFFF) << 8);
       break;
     case SYSMODE_SEG_DS_SS:
       /* non-overridden, use ss register */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
       value = GetBYTEExtended((((uint32)m->R_SS << 4) + offset) & 0xFFFFF)
         | (GetBYTEExtended((((uint32)m->R_SS << 4)
                + (uint16)(offset + 1)) & 0xFFFFF) << 8);
       break;
     case SYSMODE_SEGOVR_CS:
       /* ds overridden */
     case SYSMODE_SEGOVR_CS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use cs register */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
       value = GetBYTEExtended((((uint32)m->R_CS << 4) + offset) & 0xFFFFF)
         | (GetBYTEExtended((((uint32)m->R_CS << 4)
                + (uint16)(offset + 1)) & 0xFFFFF) << 8);
       break;
     case SYSMODE_SEGOVR_DS:
       /* ds overridden --- shouldn't happen, but hey. */
     case SYSMODE_SEGOVR_DS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ds register */
    /* [JCE] Wrap at 1Mb (the A20 gate) */
       value = GetBYTEExtended((((uint32)m->R_DS << 4) + offset) & 0xFFFFF)
         | (GetBYTEExtended((((uint32)m->R_DS << 4)
                + (uint16)(offset + 1)) & 0xFFFFF) << 8);
       break;
     case SYSMODE_SEGOVR_ES:
       /* ds overridden */
     case SYSMODE_SEGOVR_ES|SYSMODE_SEG_DS_SS:
       /* ss overridden, use es register */
       value = GetBYTEExtended((((uint32)m->R_ES << 4) + offset) & 0xFFFFF)
         | (GetBYTEExtended((((uint32)m->R_ES << 4) +
                (uint16)(offset + 1)) & 0xFFFFF) << 8);
       break;
     case SYSMODE_SEGOVR_SS:
       /* ds overridden */
     case SYSMODE_SEGOVR_SS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ss register === should not happen */
       value = GetBYTEExtended((((uint32)m->R_SS << 4) + offset) & 0xFFFFF)
         | (GetBYTEExtended((((uint32)m->R_SS << 4)
                + (uint16)(offset + 1)) & 0xFFFFF) << 8);
       break;
     default:
       sim_printf("error: should not happen:  multiple overrides. " NLP);
       value = 0;
       halt_sys(m);
    }
    return value;
}

/* fetch a byte of data, given an offset, the current register set,
    and a descriptor for memory.
*/
uint16 fetch_data_word_abs(PC_ENV *m, uint16 segment, uint16 offset)
{
    uint16 value;
    uint32 addr;
/* [JCE] Simulate wrap at top of memory (the A20 gate) */
/*    addr = (segment << 4) + offset; */
    addr = ((segment << 4) + offset) & 0xFFFFF;
    value = GetBYTEExtended(addr) | (GetBYTEExtended(addr + 1) << 8);
    return value;
}

/* Store a byte of data, given an offset, the current register set,
    and a descriptor for memory.
*/
void store_data_byte(PC_ENV *m, uint16 offset, uint8 val)
{
    /* See note above in fetch_data_byte. */
    uint32             addr;
    register uint16 segment;
    switch(m->sysmode & SYSMODE_SEGMASK)
    {
     case 0:
       /* default case: use ds register */
       segment = m->R_DS;
       break;
     case SYSMODE_SEG_DS_SS:
       /* non-overridden, use ss register */
       segment = m->R_SS;
       break;
     case SYSMODE_SEGOVR_CS:
       /* ds overridden */
     case SYSMODE_SEGOVR_CS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use cs register */
       segment = m->R_CS;
       break;
     case SYSMODE_SEGOVR_DS:
       /* ds overridden --- shouldn't happen, but hey. */
     case SYSMODE_SEGOVR_DS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ds register */
       segment = m->R_DS;
       break;
     case SYSMODE_SEGOVR_ES:
       /* ds overridden */
     case SYSMODE_SEGOVR_ES|SYSMODE_SEG_DS_SS:
       /* ss overridden, use es register */
       segment = m->R_ES;
       break;
     case SYSMODE_SEGOVR_SS:
       /* ds overridden */
     case SYSMODE_SEGOVR_SS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ss register === should not happen */
       segment = m->R_SS;
       break;
     default:
       sim_printf("error: should not happen:  multiple overrides. " NLP);
       segment = 0;
       halt_sys(m);
    }
/* [JCE] Simulate wrap at top of memory (the A20 gate) */
/*    addr = (segment << 4) + offset; */
    addr = (((uint32)segment << 4) + offset) & 0xFFFFF;
    PutBYTEExtended(addr, val);
}

void store_data_byte_abs(PC_ENV *m, uint16 segment, uint16 offset, uint8 val)
{
    register uint32 addr;
/* [JCE] Simulate wrap at top of memory (the A20 gate) */
/*    addr = (segment << 4) + offset; */
    addr = (((uint32)segment << 4) + offset) & 0xFFFFF;
    PutBYTEExtended(addr, val);
}

/* Store  a word of data, given an offset, the current register set,
    and a descriptor for memory.
*/
void store_data_word(PC_ENV *m, uint16 offset, uint16 val)
{
    register uint32 addr;
    register uint16 segment;
    /* See note above in fetch_data_byte. */
    switch(m->sysmode & SYSMODE_SEGMASK)
    {
     case 0:
       /* default case: use ds register */
       segment = m->R_DS;
       break;
     case SYSMODE_SEG_DS_SS:
       /* non-overridden, use ss register */
       segment = m->R_SS;
       break;
     case SYSMODE_SEGOVR_CS:
       /* ds overridden */
     case SYSMODE_SEGOVR_CS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use cs register */
       segment = m->R_CS;
       break;
     case SYSMODE_SEGOVR_DS:
       /* ds overridden --- shouldn't happen, but hey. */
     case SYSMODE_SEGOVR_DS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ds register */
       segment = m->R_DS;
       break;
     case SYSMODE_SEGOVR_ES:
       /* ds overridden */
     case SYSMODE_SEGOVR_ES|SYSMODE_SEG_DS_SS:
       /* ss overridden, use es register */
       segment = m->R_ES;
       break;
     case SYSMODE_SEGOVR_SS:
       /* ds overridden */
     case SYSMODE_SEGOVR_SS|SYSMODE_SEG_DS_SS:
       /* ss overridden, use ss register === should not happen */
       segment = m->R_SS;
       break;
     default:
       sim_printf("error: should not happen:  multiple overrides." NLP);
       segment = 0;
       halt_sys(m);
    }
/* [JCE] Simulate wrap at top of memory (the A20 gate) */
/*    addr = (segment << 4) + offset; */
    addr = (((uint32)segment << 4) + offset) & 0xFFFFF;
    PutBYTEExtended(addr, val & 0xff);
    PutBYTEExtended(addr + 1, val >> 8);
}

void store_data_word_abs(PC_ENV *m, uint16 segment, uint16 offset, uint16 val)
{
    register uint32 addr;
    /* [JCE] Wrap at top of memory */
    addr = ((segment << 4) + offset) & 0xFFFFF;
    PutBYTEExtended(addr, val & 0xff);
    PutBYTEExtended(addr + 1, val >> 8);
}
