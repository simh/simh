/* i7080_cpu.c: IBM 7080 CPU simulator

   Copyright (c) 2006-2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   cpu          705 central processor

   The system state for the IBM 705 is:

   IC<0:15>             program counter
   SW<0:6>              sense switches
   AC<0:6>[0:512]       AC

   The 705 has one instruction format.

     Char
        1    2    3    4    5
      opc   addh add  add   addl


   This routine is the instruction decode routine for the 705.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until a stop condition occurs.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        illegal instruction
        illegal I/O operation for device
        breakpoint encountered
        divide check
        I/O error in I/O simulator

   2. Arithmetic.  The 705 uses decimal arithmetic.

   4. Adding I/O devices.  These modules must be modified:

        i705_defs.h    add device definitions
        i705_sys.c     add sim_devices table entry
*/

#include "i7080_defs.h"
#include "sim_card.h"
#include <math.h>

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (017 << UNIT_V_MSIZE)
#define UNIT_V_CPUMODEL (UNIT_V_UF + 4)
#define UNIT_MODEL      (0x3 << UNIT_V_CPUMODEL)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_CPUMODEL) & 0x3)
#define MODEL(x)        (x << UNIT_V_CPUMODEL)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define UNIT_EMU        (UNIT_V_CPUMODEL + 2)
#define EMULATE3        (1 << UNIT_EMU)
#define EMULATE2        (2 << UNIT_EMU)
#define UNIT_V_NONSTOP  (UNIT_EMU + 2)
#define NONSTOP         (1 << UNIT_V_NONSTOP)

#define CPU_702         0x0
#define CPU_705         0x1
#define CPU_7053        0x2
#define CPU_7080        0x3

#define HIST_XCT        1       /* instruction */
#define HIST_INT        2       /* interrupt cycle */
#define HIST_TRP        3       /* trap cycle */
#define HIST_MIN        64
#define HIST_MAX        65536
#define HIST_NOEA       0x40000000
#define HIST_PC         0x80000

struct InstHistory
{
    uint32              ic;
    uint32              ea;
    uint32              inst;
    uint8               reg;
    uint8               op;
    uint16              flags;
    uint8               store[256];
};

t_stat              cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr,
                           int32 sw);
t_stat              cpu_dep(t_value val, t_addr addr, UNIT * uptr,
                            int32 sw);
t_stat              cpu_reset(DEVICE * dptr);
t_stat              cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_show_hist(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *cpu_description (DEVICE *dptr);

uint32 read_addr(uint8 *reg, uint8 *zone);
void write_addr(uint32 addr, uint8 reg, uint8 zone);
uint32 load_addr(int loc);
void store_addr(uint32 addr, int loc);
void store_cpu(uint32 addr, int full);
void load_cpu(uint32 addr, int full);
uint16 get_acstart(uint8 reg);
t_stat do_addsub(int mode, int reg, int smt, uint16 fmsk);
t_stat do_mult(int reg, uint16 fmsk);
t_stat do_divide(int reg, uint16 fmsk);
t_stat do_compare(int reg, int tluop);
void mem_init(void);


uint16              bstarts[16] = {
            /*  1    2    3    4    5    6    7 */
           0, 512, 528, 544, 560, 576, 592, 608,
         624, 640, 656, 672, 688, 704, 720, 736,
};

uint8   bcd_bin[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                                 1, 2, 3, 4, 5};
uint8   bin_bcd[21] = { 10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
uint32  dig2[11] = { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90,0 };
uint32  dig3[11] = { 0, 100, 200, 300, 400, 500, 600, 700, 800, 900,0 };
uint32  dig4[11] = { 0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000,0};
uint32  dig_zone[16] = {0, 10000, 20000, 30000,
                        80000, 90000, 100000, 110000,
                        40000, 50000, 60000, 70000,
                        120000, 130000, 140000, 150000
        };
uint8   zone_dig[16] = {0x0, 0x4, 0x8, 0xc,
                        0x2, 0x6, 0xa, 0xe,
                        0x1, 0x5, 0x9, 0xd,
                        0x3, 0x7, 0xb, 0xf
        };

/* Flip BA bits of low order zone for LDA */
uint8   lda_flip[16] = {0x0, 0x1, 0x2, 0x3,
                        0x8, 0x9, 0xa, 0xb,
                        0x4, 0x5, 0x6, 0x7,
                        0xc, 0xd, 0xe, 0xf
        };

                      /* 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 */
uint8   comp_bcd[16] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 9, 6, 5, 4, 3, 2 };

#define I       0x80

uint8   digit_addone[16] = {
    0,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x10,0x01,0x0b,0x0c,0x0d,0x0e,
    0x0f};

uint8  cmp_order[0100] = {
        077, 42, 43, 44, 45, 46, 47, 48, 49, 50, 41, 10, 11, 077, 077, 077,
          0,  7, 33, 34, 35, 36, 37, 38, 39, 40, 32,  8,  9, 077, 077, 077,
          4, 23, 24, 25, 26, 27, 28, 29, 30, 31, 22,  5,  6, 077, 077, 077,
          1, 13, 14, 15, 16, 17, 18, 19, 20, 21, 12,  2,  3, 077, 077, 077
};

/* Flags */
#define ASIGN           0x0001                  /* ACC Minus */
#define BSIGN           0x0002                  /* ASU Minus */
#define AZERO           0x0004                  /* ACC A Zero */
#define BZERO           0x0008                  /* ASU Zero */
#define INSTFLAG        0x0010                  /* Instruction error */
#define MCHCHK          0x0020                  /* Machine check */
#define IOCHK           0x0040                  /* I/O Check */
#define RECCHK          0x0080                  /* Record check */
#define ACOFLAG         0x0100                  /* AC Overflow flag */
#define SGNFLAG         0x0200                  /* Sign mismatch */
#define ANYFLAG         0x0400                  /* Anyflag set */
#define EIGHTMODE       0x0800                  /* 7080 mode */
#define HIGHFLAG        0x1000                  /* High comparison */
#define LOWFLAG         0x2000                  /* Low comparison */
#define CMPFLAG         0x3000                  /* Comparison flags */

/* If stop_flags is set to 1 (Automatic Mode) the sim stops if the flag is
   set. If the stop_flags is set to 0 (Program mode) the sim continues */

#define SIGN            (ASIGN|BSIGN)
#define ZERO            (AZERO|BZERO)
#define IRQFLAGS        (INSTFLAG|MCHCHK|IOCHK|RECCHK|ACOFLAG|SGNFLAG)

uint8               M[MAXMEMSIZE] = { 0 };      /* memory */
uint32              EMEMSIZE;                   /* Physical memory size */
uint8               AC[6*256];                  /* store registers */
uint16              flags;                      /* Flags */
uint16              spc;                        /* Reg start point */
uint16              spcb;                       /* Reg start point b */
uint32              IC;                         /* program counter */
uint8               SL;                         /* Sense lights */
uint32              MA;                         /* Memory address */
uint32              MAC;                        /* Memory address */
uint32              MAC2;                       /* Second memory address */
uint8               SW = 0;                     /* Sense switch */
uint8               indflag;                    /* Indirect flag */
uint8               intmode;                    /* Interupt mode */
uint8               intprog;                    /* Interupt program */
uint16              stop_flags = 0;             /* Stop on error */
uint16              selreg;                     /* Last select address */
uint16              selreg2;                    /* RWW select address */
int                 chwait;                     /* Channel wait register */
uint8               ioflags[5000/8] = {0};      /* IO Error flags */
uint16              irqflags;                   /* IRQ Flags */
uint8               lpr_chan9[NUM_CHAN];        /* Line printer Channel 9 flag */
uint8               bkcmp = 0;                  /* Backwords compare */
uint8               cpu_type;                   /* Current CPU type */
int                 cycle_time = 45;            /* Cycle time is 4.5us */

/* History information */
int32               hst_p = 0;                  /* History pointer */
int32               hst_lnt = 0;                /* History length */
struct InstHistory *hst = NULL;                 /* History stack */
extern uint32       drum_addr;
extern UNIT         chan_unit[];
void (*sim_vm_init) (void) = &mem_init;


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
    { UDATA(NULL, MODEL(CPU_7053) | MEMAMOUNT(3) | NONSTOP, MAXMEMSIZE) };

REG                 cpu_reg[] = {
    {DRDATAD(IC, IC, 32, "Instruction register")},
    {"A", &AC, 8, 8, 0, 256, "A Register", NULL, REG_VMIO|REG_CIRC, 0, },
    {"ASU1", &AC[256], 8, 8, 256, 16, "ASU1 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU2", &AC[256], 8, 8, 256, 16, "ASU2 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU3", &AC[256], 8, 8, 256, 16, "ASU3 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU4", &AC[256], 8, 8, 256, 16, "ASU4 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU5", &AC[256], 8, 8, 256, 16, "ASU5 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU6", &AC[256], 8, 8, 256, 16, "ASU6 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU7", &AC[256], 8, 8, 256, 16, "ASU7 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU8", &AC[256], 8, 8, 256, 16, "ASU8 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU9", &AC[256], 8, 8, 256, 16, "ASU9 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU10", &AC[256], 8, 8, 256, 16, "ASU10 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU11", &AC[256], 8, 8, 256, 16, "ASU11 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU12", &AC[256], 8, 8, 256, 16, "ASU12 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU13", &AC[256], 8, 8, 256, 16, "ASU13 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU14", &AC[256], 8, 8, 256, 16, "ASU14 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {"ASU15", &AC[256], 8, 8, 256, 32, "ASU15 Register", NULL, REG_VMIO|REG_CIRC, 0},
    {BRDATA(SW, &SW, 2, 6, 1), REG_FIT},
    {FLDATA(SW911, SW, 0), REG_FIT},
    {FLDATA(SW912, SW, 1), REG_FIT},
    {FLDATA(SW913, SW, 2), REG_FIT},
    {FLDATA(SW914, SW, 3), REG_FIT},
    {FLDATA(SW915, SW, 4), REG_FIT},
    {FLDATA(SW916, SW, 5), REG_FIT},
    {GRDATA(STOP, stop_flags, 2, 6, 4), REG_FIT},
    {FLDATA(STOP0, stop_flags, 4), REG_FIT},
    {FLDATA(STOP1, stop_flags, 5), REG_FIT},
    {FLDATA(STOP2, stop_flags, 6), REG_FIT},
    {FLDATA(STOP3, stop_flags, 7), REG_FIT},
    {FLDATA(STOP4, stop_flags, 8), REG_FIT},
    {FLDATA(STOP5, stop_flags, 9), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MODEL, MODEL(CPU_702), "702", "702", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(CPU_705), "705", "705", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(CPU_7053), "7053", "7053", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(CPU_7080), "7080", "7080", NULL, NULL, NULL},
    {UNIT_MSIZE, MEMAMOUNT(0), "10K", "10K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(1), "20K", "20K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(3), "40K", "40K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(7), "80K", "80K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(11), "120K", "120K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(15), "160K", "160K", &cpu_set_size},
    {EMULATE2, 0, NULL, "NOEMU40K", NULL, NULL, NULL},
    {EMULATE2, EMULATE2, "EMU40K", "EMU40K", NULL, NULL, NULL},
    {EMULATE3, 0, "EMU705", "EMU705", NULL, NULL, NULL},
    {EMULATE3, EMULATE3, "EMU7053", "EMU7053", NULL, NULL, NULL},
    {NONSTOP, 0, "PROGRAM", "PROGRAM", NULL, NULL, NULL},
    {NONSTOP, NONSTOP, "NONSTOP", "NONSTOP", NULL, NULL, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 18, 1, 8, 8,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
};


/* Quick ways to wrap addresses */
uint16          next_addr[6 * 256];     /* Next storage location */
uint16          prev_addr[6 * 256];     /* Previous storage location */
uint16          next_half[6 * 256];     /* Forward half loop locations */

/*#define ReadP(addr)   M[(addr) % EMEMSIZE] */
#define WriteP(addr, data) M[(addr) % EMEMSIZE] = data

#define Next(reg)       if (reg == 0) reg = EMEMSIZE; reg--
#define Prev5(reg)      reg += 5; if (reg > EMEMSIZE) reg -= EMEMSIZE
#define Prev10(reg)     reg += 10; if (reg > EMEMSIZE) reg -= EMEMSIZE
#define Prev(reg)       reg++; if (reg == EMEMSIZE) reg = 0

/* Read 1 character from memory, checking for reducancy error. */
uint8   ReadP(uint32 addr, uint16 flag) {
    uint8       value;
    value = M[(addr) % EMEMSIZE];
    if (value & 0100) {
        if (flag == 0)
            return value;
        flags |= flag|ANYFLAG;
    } else if (value == 0) {
        flags |= flag|ANYFLAG;
    }
    return value & 077;
}

/* Read 5 characters from memory starting at addr */
uint32  Read5(uint32 addr, uint16 flag) {
    uint32      value;

    value =  ReadP(addr-4, flag) << (4 * 6);
    value |= ReadP(addr-3, flag) << (3 * 6);
    value |= ReadP(addr-2, flag) << (2 * 6);
    value |= ReadP(addr-1, flag) << (1 * 6);
    value |= ReadP(addr, flag);
    return value;
}

/* Write 5 characters from memory starting at addr */
void  Write5(uint32 addr, uint32 value) {
    WriteP(addr-4, 077 & (value >> (4 * 6)));
    WriteP(addr-3, 077 & (value >> (3 * 6)));
    WriteP(addr-2, 077 & (value >> (2 * 6)));
    WriteP(addr-1, 077 & (value >> (1 * 6)));
    WriteP(addr  , 077 & (value));
}


t_stat
sim_instr(void)
{
    t_stat              reason;
    int                 opcode;
    uint8               reg;
    uint16              fmsk;
    uint8               zone;
    uint8               sign;
    uint8               zero;
    uint8               at;
    uint8               carry;
    uint8               t;
    uint8               cr1, cr2;
    int                 temp;
    uint32              addr;
    uint8               iowait = 0;
    int                 instr_count = 0; /* Number of instructions to execute */

    if (sim_step != 0) {
        instr_count = sim_step;
        sim_cancel_step();
    }


    cpu_type = CPU_MODEL;
    /* Adjust max memory and flags based on emulation mode */
    EMEMSIZE = MEMSIZE;
    switch (cpu_type) {
    case CPU_7080:
        if ((flags & EIGHTMODE) == 0) {
           cpu_type = (cpu_unit.flags & EMULATE3)?CPU_7053:CPU_705;
           EMEMSIZE = MEMSIZE;
           if (cpu_unit.flags & EMULATE2 && EMEMSIZE > 40000)
              EMEMSIZE = 40000;
           if (cpu_type == CPU_705 && (cpu_unit.flags & EMULATE2) == 0
                        && EMEMSIZE > 20000)
               EMEMSIZE = 20000;
           if (EMEMSIZE > 80000)
               EMEMSIZE = 80000;
        }
        break;
    case CPU_7053:
        if (EMEMSIZE > 80000)
            EMEMSIZE = 80000;
        if (cpu_unit.flags & EMULATE2 && EMEMSIZE > 40000)
                EMEMSIZE = 40000;
        break;
    case CPU_705:
        if (cpu_unit.flags & EMULATE2 && EMEMSIZE > 40000)
                EMEMSIZE = 40000;
        else if (EMEMSIZE > 20000)
            EMEMSIZE = 20000;
        break;
    case CPU_702:
        EMEMSIZE = 10000;
        break;
    }
    reason = 0;

    while (reason == 0) {       /* loop until halted */

        chan_proc();
        if (chwait != 0) {
            if (chan_active(chwait - 1)) {
                sim_interval = 0;
            } else {
                chwait = 0;
            }
        }

stop_cpu:
        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason != SCPE_OK)
                break;      /* process */
        }

        if (sim_brk_summ && sim_brk_test(IC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

        /* Make sure instruction is on 4 or 9 boundary */
        if (((IC + 1) % 5) != 0) {
            flags |= INSTFLAG|ANYFLAG;
        }

        /* Check stop conditions */
        if ((cpu_unit.flags & NONSTOP) && (intprog == 0) && intmode != 0 &&
                selreg2 == 0 && (IRQFLAGS & flags)) {
             /* Process as interrupt */
             Next(IC);          /* Back up to start of instruction */
             Next(IC);
             Next(IC);
             Next(IC);
             Next(IC);
             store_cpu(0x3E0, 1);
             load_cpu(0x2A0, 0);
             intprog = 1;
             spc = 0x200;
        } else if (((cpu_unit.flags & NONSTOP) == 0 || intprog == 0) &&
                (stop_flags & flags))    {
           /* Issue sim halt */
           if (stop_flags & flags & INSTFLAG) {
                reason = STOP_UUO;
                flags &= ~ (INSTFLAG|ANYFLAG);
                break;
           }
           if (stop_flags & flags & MCHCHK) {
                reason = STOP_MMTRP;
                flags &= ~(MCHCHK|ANYFLAG);
                break;
           }
           if (stop_flags & flags & IOCHK) {
                reason = STOP_IOCHECK;
                flags &= ~(IOCHK|ANYFLAG);
                break;
           }
           if (stop_flags & flags & RECCHK) {
                reason = STOP_RECCHK;
                flags &= ~(RECCHK|ANYFLAG);
                break;
           }
           if (stop_flags & flags & ACOFLAG) {
                reason = STOP_ACOFL;
                flags &= ~(ACOFLAG|ANYFLAG);
                break;
           }
           if (stop_flags & flags & SGNFLAG) {
                reason = STOP_SIGN;
                flags &= ~(SGNFLAG|ANYFLAG);
                break;
           }
        } else if (cpu_unit.flags & NONSTOP && intprog && (IRQFLAGS & flags)) {
           /* Issue sim halt */
           if (flags & INSTFLAG) {
                reason = STOP_UUO;
                flags &= ~ (INSTFLAG|ANYFLAG);
                break;
           }
           if (flags & MCHCHK) {
                reason = STOP_MMTRP;
                flags &= ~(MCHCHK|ANYFLAG);
                break;
           }
           if (flags & IOCHK) {
                reason = STOP_IOCHECK;
                flags &= ~(IOCHK|ANYFLAG);
                break;
           }
           if (flags & RECCHK) {
                reason = STOP_RECCHK;
                flags &= ~(RECCHK|ANYFLAG);
                break;
           }
           if (flags & ACOFLAG) {
                reason = STOP_ACOFL;
                flags &= ~(ACOFLAG|ANYFLAG);
                break;
           }
           if (flags & SGNFLAG) {
                reason = STOP_SIGN;
                flags &= ~(SGNFLAG|ANYFLAG);
                break;
           }
        }


        /* If we are waiting on I/O, don't fetch */
        if (!chwait) {
             if (!iowait) {
                 if (indflag == 0 && bkcmp == 0 && intprog == 0 &&
                      intmode != 0 && irqflags != 0) {
                     /* Process as interrupt */
                     store_cpu(0x3E0, 1);
                     addr = 0x200;
                     temp = 2;       /* Start channel 20 */
                     while((temp & irqflags) == 0) {
                           temp <<= 1;
                           addr += 32;
                           if (temp == 0x20) /* Channel 40 */
                               addr = 0x400;
                     }
                     sim_debug(DEBUG_TRAP, &cpu_dev, "Trap on channel %x\n", addr);
                     irqflags &= ~temp;
                     load_cpu(addr, 0);
                     intprog = 1;
                     spc = 0x200;
                     sim_debug(DEBUG_TRAP, &cpu_dev, "Trap to addr %d\n", IC);
                 }
                 /* Make sure IC is on correct boundry */
                 if ((IC % 5) != 4) {
                     flags |= INSTFLAG|ANYFLAG;
                     sim_interval--;
                     goto stop_cpu;
                 }
                 /* Split out current instruction */
                 MA = IC;
                 MAC = read_addr(&reg, &zone);
                 opcode = ReadP(MA, INSTFLAG);       /* Finaly read opcode */
                 MA = MAC;
                 IC += 5;
                 switch (CPU_MODEL) {
                 case CPU_7080:
                     temp = 160000;
                     if ((flags & EIGHTMODE) == 0) {
                         temp = 80000;
                         if (cpu_unit.flags & EMULATE2)
                             temp = 40000;
                         else if (cpu_type == CPU_705)
                             temp = 20000;
                     }
                     break;
                 case CPU_7053:
                     temp = 80000;
                     if (cpu_unit.flags & EMULATE2)
                         temp = 40000;
                     break;
                 case CPU_705:
                     temp = 20000;
                     if (cpu_unit.flags & EMULATE2)
                         temp = 40000;
                     break;
                  case CPU_702:
                     temp = 10000;
                     break;
                 }
                 while (IC >= (uint32)temp)
                     IC -= temp;
                 /* Resolve full address and register based on cpu mode */
                 switch (cpu_type) {
                 case CPU_705:  /* 705 */
                 case CPU_702:  /* 702 */
                         break;
                 case CPU_7080:  /* 7080 */
                         if (indflag) {
                             indflag = 0;
                             if ((MA % 5) != 4) {
                                flags |= INSTFLAG|ANYFLAG;
                                goto stop_cpu;
                             }
                             MAC = read_addr(&t, &zone);
                             MA = MAC;
                         }
                         break;
                 case CPU_7053:  /* 705-iii */
                         if (zone & 04) {    /* Check indirect */
                             if ((MA % 5) != 4) {
                                flags |= INSTFLAG|ANYFLAG;
                                goto stop_cpu;
                             }
                             MAC = read_addr(&t, &zone);
                             MA = MAC;
                         }
                         break;
                 }

                 if (hst_lnt) {      /* history enabled? */
                      hst_p = (hst_p + 1);   /* next entry */
                      if (hst_p >= hst_lnt)
                          hst_p = 0;
                      hst[hst_p].ic = (IC - 5) | HIST_PC;
                      hst[hst_p].op = opcode;
                      hst[hst_p].ea = MAC;
                      hst[hst_p].reg = reg;
                      hst[hst_p].inst = Read5(IC-5, 0);
#if 0
                      addr = get_acstart(reg);
                      for (t = 0; t < 32; t++) {
                             hst[hst_p].store[t] = AC[addr];
                             addr = next_addr[addr];
                             if (hst[hst_p].store[t] == 0)
                                break;
                      }
#endif
                 }
             }

             fmsk = (reg)?(BSIGN|BZERO):(ASIGN|AZERO);
             iowait = 0;
             sim_interval -= 5;      /* count down */
             switch (opcode) {
             case OP_TR:             /* TR */
                     if ((MAC % 5) != 4) {
                        flags |= INSTFLAG|ANYFLAG;
                        break;
                     }
                     /* 7080, reg = 1, TSL */
                     if (cpu_type >= CPU_7053 && reg == 1) {
                        /* MAC2 <- IC+5 */
                        MA = MAC2+4;
                        write_addr(IC, 0, 0);
                        sim_interval -= 4;   /* count down */
                     }
                     IC = MAC;
                     break;

             case OP_HLT:    /* STOP */
                     if ((cpu_unit.flags & NONSTOP) && (intprog == 0)
                              && intmode != 0) {
                          /* Process as interrupt */
                          store_cpu(0x3E0, 1);
                          load_cpu(0x2A0, 0);
                          intprog = 1;
                          spc = 0x200;
                     } else
                          reason = STOP_HALT;
                     break;

             case OP_TRH:    /* TR HI */
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     if (flags & HIGHFLAG) {
                         IC = MAC;
                     }
                     break;

             case OP_TRE:    /* TR EQ */
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     if ((flags & CMPFLAG) == 0) {
                         IC = MAC;
                     }
                     break;

             case OP_TRP:    /* TR + */
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     if ((flags & SIGN & fmsk) == 0) {
                         IC = MAC;
                     }
                     break;

             case OP_TRZ:    /* TR 0 */
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     if (flags & ZERO & fmsk) {
                         IC = MAC;
                     }
                     break;

             case OP_TRS:    /* TR SIG */
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     temp = selreg & 0xff;
                     t = 0;
                     if (cpu_type >= CPU_7053 && reg != 0) {
                         switch (reg) {
                         case 1:     /* TRR */
                             switch (chan_cmd(selreg, IO_TRS << 8, 0)) {
                             case SCPE_OK:
                                 t = 1;
                                 break;
                             case SCPE_BUSY:
                             case SCPE_NODEV:
                             case SCPE_IOERR:
                                 break;
                             }
                             break;
                         case 2:     /* TTC */
                             temp = chan_mapdev(selreg);
                             if (temp > 0 && chan_test(temp, CHS_ERR))
                                 t = 1;
                             break;
                         case 3:     /* TSA */
                             temp = chan_mapdev(selreg);
                             addr = (selreg & 0xf) +((selreg >> 8) & 0xff0);
                             if (temp > 0 && chan_active(temp)) {
                                 chwait = temp + 1;
                                 IC -= 5;
                             } else if (temp > 0 && chan_test(temp, CHS_ERR))
                                 t = 1;
                             else if (ioflags[selreg/8] & 1<<(selreg & 07))
                                 t = 1;
                             else if (ioflags[addr/8] & 1<<(addr & 07))
                                 t = 1;
                             break;
                         case 10:/* TIC */   /* Instruction error */
                         case 11:/* TMC */   /* Machine check */
                         case 12:/* TRC */   /* I/O Check */
                         case 13:/* TEC */   /* Record check */
                         case 14:/* TOC */   /* AC Overflow flag */
                         case 15:/* TSC */   /* Sign mismatch */
                              temp = 1 << (reg - 6);
                              if (flags & temp)
                                 t = 1;
                              flags &= ~temp;
                              break;
                         default:
                              break;
                         }
                     } else {
                         switch((selreg >> 8) & 0xff) {
                         case 20:            /* Tape DS */
                         case 21:
                         case 22:
                         case 23:
                              if (ioflags[selreg/8] & 1<<(selreg & 07))
                                 t = 1;
                             /* Handle tapes at either location */
                              temp = (selreg & 0xf) +((selreg >> 8) & 0xff0);
                              if (ioflags[temp/8] & 1<<(temp & 07))
                                 t = 1;
                              break;
                         case 2:             /* Tape EOF */
                              if (ioflags[selreg/8] & 1<<(selreg & 07))
                                 t = 1;
                             /* Handle tapes at either location */
                              temp = (selreg & 0xf) +((selreg << 8) & 0xff0);
                              if (temp < 2400) {
                                  if (ioflags[temp/8] & 1<<(temp & 07))
                                     t = 1;
                              }
                              break;
                         case 1:             /* Card Reader */
                              if (ioflags[selreg/8] & 1<<(selreg & 07))
                                 t = 1;
                              break;
                         case 9:             /* Special signals */
                              switch(temp) {
                              case 0:                /* Instruction error */
                              case 1:                /* Machine check */
                              case 2:                /* I/O Check */
                              case 3:                /* Record check */
                              case 4:                /* AC Overflow flag */
                              case 5:                /* Sign mismatch */
                                  temp = 1 << (temp + 4);
                                  if (flags & temp)
                                     t = 1;
                                  flags &= ~temp;
                                  break;
                              case 0x11: case 0x12: case 0x13: case 0x14:
                              case 0x15: case 0x16: case 0x17: case 0x18:
                              case 0x19:
                                  if(SW & (1 << ((temp & 0xf) - 1)))
                                     t = 1;
                                  break;
                              }
                              break;
                         case 4:             /* Printer */
                             /* Check channel 12 end of page */
                         /* Devices never signals */
                         case 3:             /* Card punch */
                         case 5:             /* Typewriter */
                        /* Invalid digits */
                         case 0:             /* Nothing */
                         case 6:             /* ???? */
                         case 7:             /* ???? */
                         case 8:             /* ???? */
                         default:    /* Drum */
                              break;
                         }
                     }
                     if (t) {
                        IC = MAC;
                     }
                     break;

             case OP_TRA:    /* TRA */
                     t = 0;
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     switch (cpu_type) {
                     case CPU_7080:  /* 7080 */
                     case CPU_7053:  /* 705-iii */
                         if (reg > 0 && reg < 7) {
                             /* Test sense switch */
                             if (SW & (1<<(reg - 1)))
                                 t = 1;
                             break;
                         } else if (reg == 7) {
                             /* Transfer if Non-stop */
                             if (cpu_unit.flags & NONSTOP)
                                 t = 1;
                             break;
                         } else if (reg > 7) {
                             /* Nop */
                             break;
                         }
                     case CPU_705:   /* 705 */
                     case CPU_702:   /* 702 */
                         if (flags & ANYFLAG)
                            t = 1;
                         flags &= ~ANYFLAG;
                         break;
                     }
                     if (t) {
                        IC = MAC;
                     }
                     break;

             case OP_TZB:            /* TZB */
                     /* transfer bit zero addr = MAC2 */
                     if (CPU_MODEL < CPU_7053) {
                          flags |= INSTFLAG|ANYFLAG;
                          break;
                     }
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     t = ReadP(MAC2, MCHCHK);
                     /* Undocumented, but diags seem to indicate this */
/*                   if (t == CHR_RM) t = 0; */
                     switch(reg) {
                     case 7:      /* C */
                             /* Develop parity */
                             t = sim_parity_table[t & 077];
                             t ^= M[MAC % EMEMSIZE] & 0100; /* C654 */
                             if (t == 0)
                                 IC = MA;
                             break;

                     case 1:      /* 1 */
                     case 2:      /* 2 */
                     case 3:      /* 4 */
                     case 4:      /* 8 */
                     case 5:      /* A */
                     case 6:      /* B */
                             if ((t & (1<<(reg-1))) == 0)
                                 IC = MA;
                             break;
                     case 0:
                     case 8:
                     case 9:
                     case 10:
                     case 11:
                     case 12:
                     case 13:
                     case 14:
                     case 15:
                             break;
                     }
                     sim_interval --;        /* count down */
                     break;

             case OP_NOP:    /* NOP */
                     break;

             case OP_CMP:    /* CMP */
                     do_compare(reg, 0);
                     break;

             case OP_UNL:    /* UNL */
                     addr = get_acstart(reg);
                     cr2 = AC[addr];
                     while(cr2 != 0) {
                         WriteP(MA, cr2);
                         Next(MA);
                         addr = next_addr[addr];
                         cr2 = AC[addr];
                         sim_interval--;     /* count down */
                     }
                     break;

             case OP_LOD:    /* LOD */
                     addr = get_acstart(reg);
                     flags |= ZERO & fmsk;
                     /* Clear sign */
                     flags &= ~(SIGN & fmsk);
                     while(AC[addr] != 0) {
                         cr1 = ReadP(MA, MCHCHK);
                         AC[addr] = cr1;
                         if ((cr1 & 0xf) != 10)
                             flags &= ~(ZERO & fmsk);
                         Next(MA);
                         addr = next_addr[addr];
                         sim_interval--;     /* count down */
                     }
                     break;

             case OP_ST:     /* ST */
                     addr = get_acstart(reg);
                     sim_interval--; /* count down */
                     at = 1; /* Use to indicate first cycle */
                     while ((cr2 = AC[addr]) != 0) {
                         if (at) {
                             cr2 &= 0xf;
                             if (flags & fmsk & SIGN)
                                 cr2 |= 040; /* Minus */
                             else
                                 cr2 |= 060; /* Plus */
                             at = 0;
                         } else {
                             if ((cr2 & 0xf) == 0) {
                                cr2 &= 060;
                                cr2 |= 012;
                             }
                             if ((cr2 & 060) == 040 || (cr2 & 060) == 020)
                                cr2 |= 0100;
                         }
                         WriteP(MA, cr2);
                         Next(MA);
                         addr = next_addr[addr];
                         sim_interval--;     /* count down */
                     }
                     /* Adjust next character */
                     cr1 = ReadP(MA, MCHCHK);
                     if (at == 0 && cr1 == 10)
                         cr1 = 0;
                     if ((cr1 & 060) == 0)
                         cr1 |= 060;
                     WriteP(MA, cr1);
                     sim_interval--; /* count down */
                     break;
             case OP_SGN:    /* SGN */
                     cr1 = ReadP(MA, MCHCHK);
                     /* Adjust memory to zero zone or blank */
                     if (cr1 & 017) {
                        WriteP(MA, cr1 & 017);
                     } else {
                        WriteP(MA, 020);
                     }
                     sim_interval--; /* count down */
                     /* Make AC either + or - */
                     flags &= ~fmsk;
                     cr1 &= 060;
                     if (cr1 == 040)
                         flags |= SIGN & fmsk;
                     else
                         cr1 |= 060;
                     /* One char in AC */
                     addr = get_acstart(reg);
                     AC[addr] = cr1;
                     addr = next_addr[addr];
                     AC[addr] = 0;
                     break;

             case OP_NTR:    /* NORM TR */
                     if ((MAC % 5) != 4) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     addr = get_acstart(reg);
                     at = 1;
                     zero = 0;
                     /* Space to end storage */
                     while(AC[addr] != 0) {
                         addr = next_addr[addr];
                         if (at) {
                             zero = 1;
                             at = 0;
                         } else
                             zero = 0;
                         sim_interval--;     /* count down */
                     }
                     /* Zero or one digit, exit */
                     if (at || zero)
                         break;
                     /* Back up one */
                     addr = prev_addr[addr];
                     if (AC[addr] == 10) {
                         AC[addr] = 0;
                         IC = MA;
                         sim_interval --;    /* count down */
                     }
                     break;

             case OP_SET:    /* SET */
                     addr = get_acstart(reg);
                     flags |= (fmsk & ZERO); /* Might be zero */
                     at = 0;                 /* No smt yet */
                     /* Scan for mark */
                     while (MAC != 0) {
                         if (at)
                             AC[addr] = 10;  /* Zero fill */
                         else if (AC[addr] == 0) {
                             at = 1; /* Indicate that we found smt */
                             AC[addr] = 10;
                         } else if (AC[addr] != 10)
                             flags &= ~(ZERO & fmsk); /* No zero, adjust flag */
                         MAC--;
                         addr = next_addr[addr];
                         sim_interval --;    /* count down */
                         if (sim_interval <= 0) {        /* event queue? */
                             reason = sim_process_event();
                             if (reason != 0)
                                break;
                             chan_proc();
                         }
                     }
                     /* Insert a mark at new end */
                     AC[addr] = 0;
                     /* Clear sign if zero */
                     flags &= ~(((flags & fmsk) >> 2) & SIGN);
                     break;

             case OP_SHR:    /* SHR */
                     if (cpu_type != CPU_702 && reg != 0) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     addr = get_acstart(reg);
                     while (MA != 0) {
                         MA--;
                         addr = next_addr[addr];
                         sim_interval --;    /* count down */
                     }
                     if (cpu_type == CPU_702 && reg != 0) {
                        spcb = addr;
                     } else {
                        if (cpu_type == CPU_702)
                           spc = addr;
                        else if (reg == 0)
                           spc = (spc & 0x700) | (addr & 0xff);
                     }
                     flags |= (fmsk & ZERO); /* Might be zero */
                     /* Check if zero */
                     while (AC[addr] != 0) {
                          if (AC[addr] != 10) {
                             flags &= ~(ZERO & fmsk);
                             break;
                          }
                         addr = next_addr[addr];
                         sim_interval --;    /* count down */
                     }
                     /* Clear sign if zero */
                     flags &= ~(((flags & fmsk) >> 2) & SIGN);
                     break;

             case OP_LEN:    /* LEN */
                     if (cpu_type != CPU_702 && reg != 0) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     addr = get_acstart(reg);
                     addr = prev_addr[addr];
                     while(MA != 0) {
                         AC[addr] = 10;
                         addr = prev_addr[addr];
                         MA--;
                         sim_interval --;    /* count down */
                     }
                     AC[addr] = 0;
                     addr = next_addr[addr]; /* Back up one */
                     if (cpu_type == CPU_702 && reg != 0)
                        spcb = addr;
                     else {
                        if (cpu_type == CPU_702)
                           spc = addr;
                        else if (reg == 0)
                           spc = (spc & 0x700) | (addr & 0xff);
                     }
                     break;

             case OP_RND:    /* RND */
                     if (cpu_type != CPU_702 && reg != 0) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     addr = get_acstart(reg);
                     flags |= (fmsk & ZERO); /* Might be zero */
                     if (MA != 0) {
                         int smt = 0;
                         /* Adjust Address */
                         while (MA != 0) {
                             MA--;
                             addr = next_addr[addr];
                             sim_interval --;        /* count down */
                         }
                         /* Adjust start pointer */
                         if (cpu_type == CPU_702 && reg != 0) {
                            spcb = addr;
                         } else {
                            if (cpu_type == CPU_702)
                               spc = addr;
                            else if (reg == 0)
                               spc = (spc & 0x700) | (addr & 0xff);
                         }
                         addr = prev_addr[addr];     /* Back up one */
                         /* Process while valid digit in memory */
                         t = 5;
                         do {
                             uint8   cr1;
                             if (AC[addr] == 0) {
                                 smt = 1;
                                 cr1 = t;
                                 t = 0;
                             } else {
                                 cr1 = bcd_bin[AC[addr]&0xf] + t;
                             }
                             if (t != 5 && cr1 != 0)
                                 flags &= ~(ZERO & fmsk);
                             t = cr1 >= 10;
                             AC[addr] = (AC[addr] & 060) | bin_bcd[cr1];
                             addr = next_addr[addr];
                             sim_interval --;        /* count down */
                         } while (t != 0);   /* Loop while carry */
                         /* If we overflowed, set flag */
                         if (smt) {
                             flags |= ACOFLAG|ANYFLAG;
                             AC[addr] = 0;   /* Write storage mark */
                         }
                     }

                     /* Check if zero */
                     while (AC[addr] != 0) {
                          if (AC[addr] != 10) {
                             flags &= ~(ZERO & fmsk);
                             break;
                          }
                         addr = next_addr[addr];
                         sim_interval --;    /* count down */
                     }
                     /* Clear sign if zero */
                     flags &= ~(((flags & fmsk) >> 2) & SIGN);
                     break;

             case OP_SPR:            /* ST PR */
                     addr = get_acstart(reg);
                     sign = ((reg)?(flags >> 1): flags) & ASIGN;
                     WriteP(MA, (sign)?040:020);
                     sim_interval --;        /* count down */
                     while(AC[addr] != 0) {
                         Next(MA);
                         cr1 = ReadP(MA, MCHCHK);
                         if (cr1 != CHR_COM && cr1 != CHR_DOT) {
                             cr2 = AC[addr];
                             WriteP(MA, cr2);
                             addr = next_addr[addr];
                         }
                         sim_interval --;    /* count down */
                     }
                     while (1) {
                         cr1 = ReadP(MA, MCHCHK);
                         sim_interval --;    /* count down */
                         if (cr1 == CHR_COM || cr1 == 10) {
                             WriteP(MA, 020);
                         } else
                             break;
                         Prev(MA);
                     }
                     break;

             case OP_ADM:            /* ADM */
                     /* Cycle 1 */
                     addr = get_acstart(reg);
                     zero = 1;
                     cr1 = ReadP(MA, MCHCHK);
                     cr2 = AC[addr];
                     sim_interval --;        /* count down */
                     /* Set sign to sign of Ac */
                     sign = (flags & fmsk & SIGN)?1:0;
                     carry = 0;
                     /* Check sign if not valid then treat as 0 */
                     if (cr1 & 040) {
                         int smt = 1;
                         int met = 1;
                         int msign;

                         /* Numeric */
                         /* Check sign */
                         msign = (cr1 & 020)? 0: 1; /* + - */
                        /* Compliment if signs differ */
                         t = (msign != sign)? 1: 0; /* -+,+- --,++ */
                         carry = t;
                         if (cr2 == 0) {     /* Check for storage mark */
                             smt = 0;
                             cr2 = 10;
                         }
                         cr1 &= 0xf;
                         temp = (t)? comp_bcd[cr2 & 0xf]:bcd_bin[cr2 & 0xf];
                         temp = bcd_bin[cr1 & 0xf] + temp + carry;
                         carry = temp >= 10;
                         WriteP(MA, (msign? 040:060) | bin_bcd[temp]);
                         Next(MA);
                         addr = next_addr[addr];
                         do {
                             if (smt) {
                                cr2 = AC[addr];
                                if (cr2 == 0)
                                     smt = 0;
                             } else
                                 cr2 = 10;
                             cr1 = ReadP(MA, MCHCHK);
                             if (cr1 < 1 || cr1 > 10) {
                                 met = 0;
                             } else {
                                 temp = (t)? comp_bcd[cr2 & 0xf]:bcd_bin[cr2 & 0xf];
                                 temp = bcd_bin[cr1 & 0xf] + temp + carry;
                                 carry = temp >= 10;
                                 WriteP(MA, bin_bcd[temp]);
                                 sim_interval --;    /* count down */
                                 addr = next_addr[addr];
                                 Next(MA);
                                 cr1 = ReadP(MA, MCHCHK);
                             }
                         } while (met);

                     /* Recompliment */
                         if (t && carry == 0) {
                             MA = MAC;
                             cr1 = ReadP(MA, MCHCHK);
                             sim_interval --;        /* count down */
                             cr1 ^= 020;             /* Compliment sign */
                             temp = comp_bcd[cr1 & 0xf] + 1;
                             carry = temp >= 10;
                             WriteP(MA, (cr1 & 060) | bin_bcd[temp]);
                             Next(MA);
                             while(1) {
                                  cr1 = ReadP(MA, MCHCHK);
                                  if (cr1 < 1 || cr1 > 10)
                                     break;
                                  temp = comp_bcd[cr1 & 0xf] + carry;
                                  carry = temp >= 10;
                                  WriteP(MA, bin_bcd[temp]);
                                  sim_interval --;   /* count down */
                                  Next(MA);
                             }
                         }
                     } else {
                         int zcarry = 0;

                         /* Non-numeric */
                         while (cr2 != 0) {
                             temp = bcd_bin[(cr2 & 0xf)] + bcd_bin[(cr1 & 0xf)] + carry;
                             carry = temp >= 10;
                             if (temp > 10)
                                 temp -= 10;
                             t = (cr2 & 0x30) + (cr1 & 0x30) + zcarry; /* Zone add */
                             zcarry = (t & 0x40)?0x10:0;
                             addr = next_addr[addr];
                             cr2 = AC[addr];
                             if (cr2 == 0 && carry)
                                  t += 0x10;
                             temp = (temp & 0xf) | (t & 0x30);
                             if (temp == 0)
                                 temp = 10;
                             WriteP(MA, temp);
                             Next(MA);
                             cr1 = ReadP(MA, MCHCHK);
                             sim_interval --;        /* count down */
                         }
                     }
                     break;

             case OP_SUB:    /* SUB */
                     do_addsub(1, reg, 0, fmsk);
                     break;

             case OP_ADD:    /* ADD */
                     do_addsub(0, reg, 0, fmsk);
                     break;

             case OP_RSU:    /* R SUB */
                     do_addsub(1, reg, 1, fmsk);
                     break;

             case OP_RAD:    /* R ADD */
                     do_addsub(0, reg, 1, fmsk);
                     break;

             case OP_MPY:    /* MPY */
                     do_mult(reg, fmsk);
                     break;

             case OP_DIV:    /* DIV */
                     do_divide(reg, fmsk);
                     break;


             case OP_RCV:    /* RCV  705 only */
                     if (cpu_type == CPU_702)
                         flags |= INSTFLAG|ANYFLAG;
                     else
                         MAC2 = MAC;
                     break;

             case OP_TMT:            /* TMT  705 only */
                     if (cpu_type == CPU_702) {
                         flags |= INSTFLAG|ANYFLAG;
                         break;
                     }
                     if (reg == 0) {
                         /* Copy in blocks of 5 characters */
                         if ((MAC2 % 5) != 4 || (MAC % 5) != 4) {
                             flags |= INSTFLAG|ANYFLAG;
                             break;
                         }
                         do {
                             addr = Read5(MAC, MCHCHK);
                             Write5(MAC2, addr);
                             Prev5(MAC2);
                             Prev5(MAC);
                             sim_interval -= 10;     /* count down */
                         } while ((addr & 077) != CHR_RM);
                     } else {
                        /* One char at a time */
                         addr = get_acstart(reg);
                         while(AC[addr] != 0) {
                             cr1 = ReadP(MAC, MCHCHK);
                             WriteP(MAC2, cr1);
                             Prev(MAC);
                             Prev(MAC2);
                             addr = next_addr[addr];
                             sim_interval -= 2;      /* count down */
                         }
                     }
                     break;

             case OP_SEL:            /* SEL */
                     /* Convert device to hex number */
                     selreg = MAC % 10;
                     MAC /= 10;
                     selreg |= (MAC % 10) << 4;
                     MAC /= 10;
                     selreg |= (MAC % 10) << 8;
                     MAC /= 10;
                     selreg |= (MAC % 10) << 12;
                     MAC /= 10;
                     break;

             case OP_CTL:            /* CTL */
                     temp = 0;
                     if (reg > 1) {
                         /* Process ASU modes non-zero */
                         switch (reg) {
                         /* 7080 */
                         case 12:    /* ECB */
                             /* Enable backwards compare */
                             if (CPU_MODEL == CPU_7080 && cpu_type > CPU_705)
                                 bkcmp = 1;
                             else
                                 flags |= INSTFLAG|ANYFLAG;
                             break;

                         case 13: /* CHR */
                             /* Clear io error flags */
                             chan_chr_13();
                             memset(ioflags, 0, sizeof(ioflags));
                             flags &= ~(IRQFLAGS);
                             break;

                         case 14:    /* EEM */
                             /* Enter 80 mode */
                             if (CPU_MODEL == CPU_7080) {
                                 flags |= EIGHTMODE;
                                 EMEMSIZE = MEMSIZE;
                                 cpu_type = CPU_7080;
                             } else
                                 flags |= INSTFLAG|ANYFLAG;
                             break;

                         case 15:    /* LEM */
                             /* Leave 80 mode */
                             if (CPU_MODEL == CPU_7080) {
                                 flags &= ~EIGHTMODE;
                                 cpu_type = (cpu_unit.flags & EMULATE3)?
                                             CPU_7053:CPU_705;
                                 EMEMSIZE = MEMSIZE;
                                 if (cpu_unit.flags & EMULATE2 &&
                                              EMEMSIZE > 40000)
                                   EMEMSIZE = 40000;
                                 if (cpu_type == CPU_705 &&
                                     (cpu_unit.flags & EMULATE2) == 0 &&
                                      EMEMSIZE > 20000)
                                     EMEMSIZE = 20000;
                                 if (EMEMSIZE > 80000)
                                     EMEMSIZE = 80000;
                             } else
                                 flags |= INSTFLAG|ANYFLAG;
                             break;
                         }
                         break;
                     }

                     switch (MAC) {
                     case 0: /* IOF */
                             ioflags[selreg/8] &= ~(1<<(selreg&07));
                             if ((selreg & 0xff00) == 0x200) {
                             /* Handle tapes at either location */
                                temp = (selreg & 0xf) +
                                      ((selreg & 0xff0) << 8);
                                if (temp < 0x2400)
                                    ioflags[temp/8] &= ~(1<<(temp & 07));
                             }
                             if ((selreg & 0xf000) == 0x2000) {
                                  temp = (selreg & 0xf) +
                                     ((selreg >> 8) & 0xff0);
                                  ioflags[temp/8] &= ~(1<<(temp & 07));
                             }
                             temp = 0;
                             break;

                     case 1: /* WTM */
                             temp = IO_WEF << 8;
                             break;
                     case 2: /* REW */
                             if (cpu_type > CPU_705 && reg == 1)
                                 temp = IO_RUN << 8;
                             else
                                 temp = IO_REW << 8;
                             break;

                     case 3: /* ION */
                             ioflags[selreg/8] |= 1<<(selreg&07);
                             if ((selreg & 0xff00) == 0x200) {
                             /* Handle tapes at either location */
                                temp = (selreg & 0xf) +
                                      ((selreg & 0xff0) << 8);
                                if (temp < 0x2400)
                                      ioflags[temp/8] |=
                                             1<<(temp & 07);
                             }
                             if ((selreg & 0xf000) == 0x2000) {
                               temp = (selreg & 0xf) +
                                             ((selreg >> 8) & 0xff0);
                               ioflags[temp/8] |= 1<<(temp & 07);
                             }
                             temp = 0;
                             break;

                     case 4: /* BSR */
                             if (cpu_type >= CPU_7053 && reg == 1)
                                 temp = IO_BSF << 8;
                             else
                                 temp = IO_BSR << 8;
                             break;
                     case 5:
                     case 9: /* SKP */
                             temp = IO_ERG << 8;
                             break;
                     case 37: /* SDL */
                             temp = IO_SDL << 8;
                             break;
                     case 38: /* SDH */
                             temp = IO_SDH << 8;
                             break;
                     default:
                             flags |= ANYFLAG|INSTFLAG;
                             break;
                     }
                     if (temp != 0) {
                         switch (chan_cmd(selreg, temp, 0)) {
                         case SCPE_OK:
                             break;
                         case SCPE_BUSY:
                             iowait = 1;
                             break;
                         case SCPE_NODEV:
                             reason = STOP_IOCHECK;
                             break;
                         case SCPE_IOERR:
                             flags |= ANYFLAG|INSTFLAG;
                             break;
                         }
                     }
                     break;

             case OP_RD:     /* READ */
                     temp = (IO_RDS << 8) | reg;
                     switch (chan_cmd(selreg, temp, MAC)) {
                     case SCPE_OK:
                         break;
                     case SCPE_BUSY:
                         iowait = 1;
                         break;
                     case SCPE_NODEV:
                         reason = STOP_IOCHECK;
                         break;
                     case SCPE_IOERR:
                         flags |= ANYFLAG|INSTFLAG;
                         break;
                     }
                     break;

             case OP_WR:     /* WRITE */
                     temp = (IO_WRS << 8) | reg;
                     switch (chan_cmd(selreg, temp, MAC)) {
                     case SCPE_OK:
                         break;
                     case SCPE_BUSY:
                         iowait = 1;
                         break;
                     case SCPE_NODEV:
                         reason = STOP_IOCHECK;
                         break;
                     case SCPE_IOERR:
                         flags |= ANYFLAG|INSTFLAG;
                         break;
                     }
                     break;

             case OP_WRE:    /* WR ER */
                     temp = (IO_WRS << 8) | reg | CHAN_ZERO;
                     switch (chan_cmd(selreg, temp, MAC)){
                     case SCPE_OK:
                         break;
                     case SCPE_BUSY:
                         iowait = 1;
                         break;
                     case SCPE_NODEV:
                         reason = STOP_IOCHECK;
                         break;
                     case SCPE_IOERR:
                         flags |= ANYFLAG|INSTFLAG;
                         break;
                     }
                     break;

             case OP_RWW:    /* RWW  705 only */
                     MAC2 = MAC;
                     selreg2 = selreg | 0x8000;
                     break;

             /* 7080 opcodes */
             case OP_CTL2:
                     if (cpu_type != CPU_7080) {
                          flags |= ANYFLAG|INSTFLAG;
                          break;
                     }
                     switch(reg) {
                     case 0:         /* SPC */
                     /* Set starting point */
                             /* Selects on char of 8 char words */
                             temp = (MA % 10) & 7;   /* Units digit */
                             MA /= 10;
                             t = MA % 10;    /* Tens digit */
                             temp += (t&3) << 3; /* One of words */
                             MA /= 10;
                             t = MA % 10;    /* Hundreds */
                             temp += (t&7) << 5; /* One of four word sets */
                             MA /= 10;       /* Thousands */
                             t = MA % 10;
                             temp += (t&7) << 8;      /* Bank */
                             spc = temp;
                             break;

                     case 2:         /* LFC */
                     /* load four chars */
                             addr = spc;
                             do {
                                t = ReadP(MA, MCHCHK);
                                if (t == CHR_LESS)
                                   t = 0;
                                AC[addr] = t;
                                addr = next_addr[addr];
                                Next(MA);
                                sim_interval --;     /* count down */
                             } while((MA % 5) != 0);
                             break;

                     case 3:         /* UFC */
                     /* unload four chars */
                             addr = spc;
                             do {
                                t = AC[addr];
                                addr = next_addr[addr];
                                if (t == 0)
                                   t = CHR_LESS;
                                WriteP(MA, t);
                                Next(MA);
                                sim_interval --;     /* count down */
                             } while((MA % 5) != 0);
                             break;

                     case 4:         /* LSB */
                     /* Load storage bank */
                             addr = spc & 0x700;
                             temp = 256;
                             while(temp-- > 0) {
                                t = ReadP(MA, MCHCHK);
                                if (t == CHR_LESS)
                                   t = 0;
                                AC[addr] = t;
                                addr = next_addr[addr];
                                Next(MA);
                                sim_interval --;     /* count down */
                             }
                             break;

                     case 5:         /* USB */
                     /* Unload storage bank */
                             addr = spc & 0x700;
                             temp = 256;
                             while(temp-- > 0) {
                                t = AC[addr];
                                addr = next_addr[addr];
                                if (t == 0)
                                   t = CHR_LESS;
                                WriteP(MA, t);
                                Next(MA);
                                sim_interval --;     /* count down */
                             }
                             break;

                     case 6:         /* EIM */
                     /* Enter interrupt mode */
                         intmode = 1;
                         break;

                     case 7:         /* LIM */
                     /* Leave interrupt mode */
                         intmode = 0;
                         break;

                     case 8:         /* TCT */
                     /* Ten char transmit */
                         /* Copy in blocks of 10 characters */
                         if ((MAC2 % 10) != 9 || (MAC % 10) != 9) {
                             flags |= INSTFLAG|ANYFLAG;
                             break;
                         }
                         do {
                             addr = Read5(MAC-5, MCHCHK);
                             Write5(MAC2-5, addr);
                             addr = Read5(MAC, MCHCHK);
                             Write5(MAC2, addr);
                             Prev10(MAC);
                             Prev10(MAC2);
                             sim_interval -= 20;     /* count down */
                         } while ((addr & 077) != CHR_RM);
                         break;
                     case 10:        /* EIA */
                     /* Enable indirect address */
                         indflag = 1;
                         break;

                     case 11:        /* CNO */
                     /* Nop */
                         break;

                     case 12:        /* TLU */
                     /* Table lookup equal. */
                         /* Walk backward in memory until equal, or GM */
                         do {
                             do_compare(0, 1);
                             if ((flags & CMPFLAG) == 0)
                                 break;
                             while ((cr1 = ReadP(MA, MCHCHK)) != CHR_RM ||
                                     cr1 != CHR_GM)
                                { Next(MA); }
                         } while(cr1 != CHR_GM);
                         MAC2 = MA;
                         break;
                     case 13:        /* TLU */
                     /* Table lookup equal or hi */
                         /* Walk backward in memory until equal, or GM */
                         do {
                             do_compare(0, 1);
                             if ((flags & LOWFLAG) == 0)
                                 break;
                             while ((cr1 = ReadP(MA, MCHCHK)) != CHR_RM ||
                                     cr1 != CHR_GM) {
                                 Next(MA);
                             }
                         } while(cr1 != CHR_GM);
                         MAC2 = MA;
                         break;
                     case 14:        /* TIP */
                     /* Transfer to interrupt program */
                          if ((MAC % 5) != 4) {
                              flags |= INSTFLAG|ANYFLAG;
                              break;
                          }
                          store_cpu(0x3E0, 1);
                          intprog = 1;
                          spc = 0x200;
                          IC = MA;
                          flags &= ~IRQFLAGS;
                          break;
                     case 15:        /* LIP */
                     /* Leave interrupt program */
                          if (MA != 9) {
                             /* Selects on char of 8 char words */
                             temp = (MA % 10) & 7;   /* Units digit */
                             MA /= 10;
                             t = MA % 10;    /* Tens digit */
                             temp += (t&3) << 3; /* One of words */
                             MA /= 10;
                             t = MA % 10;    /* Hundreds */
                             temp += (t&7) << 5; /* One of four word sets */
                             MA /= 10;       /* Thousands */
                             t = MA % 10;
                             temp += (t&7) << 8;      /* Bank */
                             store_cpu(temp, 0);
                          }
                     /* Fully load new context */
                          load_cpu(0x3E0, 1);
                          intprog = 0;
                          break;
                     }
                     break;

             case OP_CTL3:
                     if (cpu_type != CPU_7080) {
                          flags |= ANYFLAG|INSTFLAG;
                          break;
                     }
                     addr = get_acstart(reg);
                     switch(reg) {
                     case 8:         /* TCR */
                         /* Ten char recieve */
                         /* Copy in blocks of 10 characters */
                         if ((MAC2 % 10) != 9 || (MAC % 10) != 9) {
                             flags |= INSTFLAG|ANYFLAG;
                             break;
                         }
                         do {
                             addr = Read5(MAC2-5, MCHCHK);
                             Write5(MAC-5, addr);
                             addr = Read5(MAC2, MCHCHK);
                             Write5(MAC, addr);
                             Prev10(MAC);
                             Prev10(MAC2);
                             sim_interval -= 2;      /* count down */
                         } while ((addr & 077) != CHR_RM);
                         break;
                             break;
                     case 14:        /* SMT */
                             write_addr(MAC2, 0, 0);
                             WriteP(MA, 10); /* Finish with zero */
                             store_addr(MAC2, addr);
                             sim_interval -= 10;
                             break;
                     }
                     break;

             case OP_AAM:            /* AAM */
                     /* Add address in store to memory */
                     if (CPU_MODEL < CPU_7053 || (MAC % 5) != 4) {
                          flags |= INSTFLAG|ANYFLAG;
                          break;
                     }
                     addr = get_acstart(reg);
                     t = ReadP(MA, MCHCHK);   /* Read low order digit */
                     sim_interval --;        /* count down */
                     if (AC[addr] != 0) {
                         temp = AC[addr];
                         addr = next_addr[addr];
                     } else
                         temp = 10;
                     temp = bcd_bin[temp & 0xf] + bcd_bin[t & 0xf];
                     carry = temp > 9;
                     if (carry)
                         temp -= 10;
                     t = (t & 060) | temp;
                     if (t == 0)
                        t = 10;
                     WriteP(MA, t);
                     Next(MA);
                     t = ReadP(MA, MCHCHK);   /* Read tens order digit */
                     sim_interval --;        /* count down */
                     if (AC[addr] != 0) {
                         temp = AC[addr];
                         addr = next_addr[addr];
                     } else
                         temp = 10;
                     at = (t & 060) + (temp & 060);
                     temp = bcd_bin[temp & 0xf] + bcd_bin[t & 0xf] + carry;
                     carry = temp > 9;
                     if (carry)
                         temp -= 10;
                     t = (at & 060) | temp;
                     if (t == 0)
                        t = 10;
                     WriteP(MA, t);
                     Next(MA);
                     t = ReadP(MA, MCHCHK);   /* Read hundreds order digit */
                     sim_interval --;        /* count down */
                     if (AC[addr] != 0) {
                         temp = AC[addr];
                         addr = next_addr[addr];
                     } else
                         temp = 10;
                     at = ((at & 0100) >> 2) + (t & 060) + (temp & 060);
                     temp = bcd_bin[temp & 0xf] + bcd_bin[t & 0xf] + carry;
                     carry = temp > 9;
                     if (carry)
                         temp -= 10;
                     t = (at & 060) | temp;
                     if (t == 0)
                        t = 10;
                     WriteP(MA, t);
                     Next(MA);
                     t = ReadP(MA, MCHCHK);   /* Read thousands order digit */
                     sim_interval --;        /* count down */
                     if (AC[addr] != 0) {
                         temp = AC[addr];
                         addr = next_addr[addr];
                     } else
                         temp = 10;
                     temp = bcd_bin[temp & 0xf] + bcd_bin[t & 0xf] + carry;
                     carry = (temp > 9)?0x10:0;
                     if (carry)
                         temp -= 10;
                     t = (t & 060) | temp;
                     temp = 0;
                     /* Decode digits 5 and 6 */
                     if (AC[addr] != 0) {
                         temp = bcd_bin[AC[addr] & 0xf];
                         addr = next_addr[addr];
                         if (AC[addr] != 0 && CPU_MODEL  == CPU_7080 &&
                             flags & EIGHTMODE)
                             temp += (1 & bcd_bin[(AC[addr] & 0xf)]) * 10;
                         temp &= 0xf;
                     }
                     /* Add zone bits for top digit */
                     t += ((temp & 3) << 4) + carry;
                     carry = (t & 0100) != 0;
                     t &= 077;
                     if ((t & 0xf) == 10)
                         t &= 060;
                     if (t == 0)
                         t = 10;
                     WriteP(MA, t);
                     /* Merge high order bits into units if needed */
                     switch (CPU_MODEL) {
                     case CPU_7080:  /* 7080 */
                             if (flags & EIGHTMODE) {
                                 t = ReadP(MAC, MCHCHK);
                                 temp = (temp >> 2) + carry;
                                 if (t & 040)
                                     temp++;
                                 if (t & 020)
                                     temp += 2;
                                 t = (t & 0xf) | ((temp & 0x1) << 5) |
                                       ((temp & 0x2) << 3);
                                 if ((t & 0xf) == 10)
                                     t &= 060;
                                 if (t == 0)
                                     t = 10;
                                 WriteP(MAC, t);
                                 sim_interval --;    /* count down */
                                 break;
                             } else if ((cpu_unit.flags & EMULATE3) == 0)
                                 break;
                     case CPU_7053:  /* 705-iii */
                             if ((cpu_unit.flags & EMULATE2) == 0) {
                                 t = ReadP(MAC, MCHCHK);
                                 temp = ((temp >> 2) & 1) + carry;
                                 if (t & 040)
                                     temp++;
                                 t = (t & 0x1f) | ((temp & 0x1) << 5);
                                 if ((t & 0xf) == 10)
                                     t &= 060;
                                 if (t == 0)
                                     t = 10;
                                 WriteP(MAC, t);
                                 sim_interval --;    /* count down */
                             }
                             break;
                     case CPU_705:   /* 705 */
                     case CPU_702:   /* 702 */
                             break;
                     }
                     break;

             case OP_LDA:            /* LDA */
                     /* Load address */
                     if (CPU_MODEL < CPU_7053 || (MAC % 5) != 4) {
                          flags |= INSTFLAG|ANYFLAG;
                          break;
                     }
                     flags |= ZERO & fmsk;
                     fmsk = ~(ZERO | fmsk);
                     addr = get_acstart(reg);
                     t = ReadP(MA, MCHCHK);   /* Read low order digit */
                     temp = (t & 060) >> 2;
                     t &= 0xf;
                     if (t == 0)
                         t = 10;
                     else if (t > 10)
                         flags |= INSTFLAG|ANYFLAG;
                     else if (t != 10)
                         flags &= fmsk;      /* Clear zero */
                     AC[addr] = t;
                     addr = next_addr[addr];
                     Next(MA);
                     t = ReadP(MA, MCHCHK);  /*  next digit */
                     t &= 0xf;
                     if (t == 0)
                         t = 10;
                     else if (t > 10)
                         flags |= INSTFLAG|ANYFLAG;
                     else if (t != 10)
                         flags &= fmsk;      /* Clear zero */
                     AC[addr] = t;
                     addr = next_addr[addr];
                     Next(MA);
                     t = ReadP(MA, MCHCHK);          /* Read third digit */
                     t &= 0xf;
                     if (t == 0)
                         t = 10;
                     else if (t > 10)
                         flags |= INSTFLAG|ANYFLAG;
                     else if (t != 10)
                         flags &= fmsk;      /* Clear zero */
                     AC[addr] = t;
                     addr = next_addr[addr];
                     Next(MA);
                     t = ReadP(MA, MCHCHK);          /* Save High order address */
                     temp |= (t & 060) >> 4;
                     t &= 0xf;
                     if (t == 0)
                         t = 10;
                     else if (t > 10)
                         flags |= INSTFLAG|ANYFLAG;
                     else if (t != 10)
                         flags &= fmsk;      /* Clear zero */
                     AC[addr] = t;
                     addr = next_addr[addr];
                     temp = lda_flip[temp];
                     switch (CPU_MODEL) {
                     case CPU_702:   /* 702 */
                         break;
                     case CPU_7080:  /* 7080 */
                         if (flags & EIGHTMODE) {
                             if (temp > 10) {
                                AC[addr] = bin_bcd[temp - 10];
                                addr = next_addr[addr];
                                AC[addr] = 1;
                             } else {
                                AC[addr] = bin_bcd[temp];
                                addr = next_addr[addr];
                                AC[addr] = 10;
                             }
                             break;
                         } else if ((cpu_unit.flags & EMULATE3) == 0)
                             temp &= 03;
                     case CPU_7053:  /* 705-iii */
                         temp &= 07;
                         AC[addr] = bin_bcd[temp];
                         if (AC[addr] != 10)
                             zero = 0;
                         break;
                     case CPU_705:   /* 705 */
                         temp &= 03;
                         AC[addr] = bin_bcd[temp];
                         if (AC[addr] != 10)
                             zero = 0;
                         break;
                     }
                     if (temp != 0)
                         flags &= fmsk;      /* Clear zero */
                     addr = next_addr[addr];
                     AC[addr] = 0;
                     sim_interval -= 5;      /* count down */
                     break;

             case OP_ULA:            /* ULA */
                     /* Unload address */
                     if (CPU_MODEL < CPU_7053 || (MAC % 5) != 4) {
                          flags |= INSTFLAG|ANYFLAG;
                          break;
                     }
                     addr = get_acstart(reg);
                     t = ReadP(MA, MCHCHK) & 0360;   /* Read unitsr digit */
                     if (AC[addr] == 0) {
                         t |= 10;
                     } else {
                         t |= AC[addr] & 0xf;
                         addr = next_addr[addr];
                     }
                     if ((t & 0xf) == 10)
                         t &= 0360;
                     if (t == 0)
                         t = 10;
                     WriteP(MA, t);
                     Next(MA);
                     t = ReadP(MA, MCHCHK) & 0360;   /*  next digit */
                     if (AC[addr] == 0) {
                         t |= 10;
                     } else {
                         t |= AC[addr] & 0xf;
                         addr = next_addr[addr];
                     }
                     if ((t & 0xf) == 10)
                         t &= 0360;
                     if (t == 0)
                         t = 10;
                     WriteP(MA, t);
                     Next(MA);
                     t = ReadP(MA, MCHCHK) & 0360;   /* Read third digit */
                     if (AC[addr] == 0) {
                         t |= 10;
                     } else {
                         t |= AC[addr] & 0xf;
                         addr = next_addr[addr];
                     }
                     if ((t & 0xf) == 10)
                         t &= 0360;
                     if (t == 0)
                         t = 10;
                     WriteP(MA, t);
                     Next(MA);
                     t = ReadP(MA, MCHCHK) & 0360;   /* Save High order address */
                     if (AC[addr] == 0) {
                         t |= 10;
                     } else {
                         t |= AC[addr] & 0xf;
                         addr = next_addr[addr];
                     }
                     temp = 0;
                     /* Decode digits 5 and 6 */
                     if (AC[addr] != 0) {
                         temp = bcd_bin[AC[addr] & 0xf];
                         addr = next_addr[addr];
                         if (AC[addr] != 0 && cpu_type == CPU_7080)
                             temp += (1 & bcd_bin[(AC[addr] & 0xf)]) * 10;
                     }
                     /* Add zone bits for top digit */
                     temp = zone_dig[temp & 0xf];
                     t &= 0xf;
                     t |= (temp & 0xc) << 2;
                     if ((t & 0xf) == 10)
                         t &= 0360;
                     if (t == 0)
                         t = 10;
                     WriteP(MA, t);
                     /* Merge high order bits into units if needed */
                     switch (cpu_type) {
                     case CPU_7080:  /* 7080 */
                             t = ReadP(MAC, MCHCHK) & 0xf;
                             t |= (temp & 0x3) << 4;
                             if ((t & 0xf) == 10)
                                     t &= 0360;
                             if (t == 0)
                                     t = 10;
                             WriteP(MAC, t);
                             break;
                     case CPU_7053:  /* 705-iii */
                             /* Check if 80K machine */
                             if ((cpu_unit.flags & EMULATE2) == 0) {
                                 t = ReadP(MAC, MCHCHK) & 0x1f;
                                 t |= (temp & 0x2) << 4;
                                 if ((t & 0xf) == 10)
                                     t &= 0360;
                                 if (t == 0)
                                     t = 10;
                                 WriteP(MAC, t);
                             }
                             break;
                     case CPU_705:   /* 705 */
                     case CPU_702:   /* 702 , Illegal on this machine */
                             break;
                     }
                     sim_interval -= 5;      /* count down */
                     break;

             case OP_SND:            /* SND */
                     /* Only on 705/3 and above */
                     /* Addresses must be on 5 char boundry */
                     if (CPU_MODEL < CPU_7053 || (MAC2 % 5) != 4 || (MAC % 5) != 4) {
                          flags |= INSTFLAG|ANYFLAG;
                          selreg2 = 0;
                          break;
                     }
                     /* If RWW pending, SND does a memory check until end of block */
                     if (selreg2 != 0) {
                         selreg2 = 0;
                         while((MAC % 200000) != 19999) {
                             (void)Read5(MAC, MCHCHK);
                             Prev5(MAC);
                             sim_interval -= 5;      /* count down */
                         }
                         break;
                     }
                     addr = get_acstart(reg);
                     while(AC[addr] != 0) {
                         uint32      v;
                         v = Read5(MAC, MCHCHK);
                         Write5(MAC2, v);
                         Prev5(MAC2);
                         Prev5(MAC);
                         addr = next_addr[addr];
                         sim_interval -= 5;  /* count down */
                     }
                     break;

             case OP_BLM:            /* BLM */
                     /* Blank memory */ /* ASU 0 5 char, ASU 1 1 char */
                     if (CPU_MODEL < CPU_7053) {
                          flags |= INSTFLAG|ANYFLAG;
                          break;
                     }
                     /* Blank in blocks of 5 characters */
                     if (reg == 0) {
                         if ((MAC2 % 5) != 4) {
                               flags |= INSTFLAG|ANYFLAG;
                               break;
                         }
                         while(MAC > 0) {
                             Write5(MAC2, CHR_BLANK << (4 * 6)|
                                  CHR_BLANK << (3 * 6)|CHR_BLANK << (2 * 6)|
                                  CHR_BLANK << (1 * 6)|CHR_BLANK);
                             Prev5(MAC2);
                             MAC--;
                             sim_interval -= 5;      /* count down */
                         }
                     } else if (reg == 1) {
                         while(MAC > 0) {
                             WriteP(MAC2, CHR_BLANK);
                             Prev(MAC2);
                             MAC--;
                             sim_interval --;        /* count down */
                         }
                     } else {
                          flags |= INSTFLAG|ANYFLAG;
                     }
                     break;

             case OP_SBZ:            /* SBZ|A|R|N */
                     /* reg 1-6: bit# <- 0 */
                     /* reg 7: bitA ^= 1 */
                     /* reg 8: bit error ^= 1 */
                     /* reg 9-14: bit# <- 1 */
                     if (CPU_MODEL < CPU_7053) {
                          flags |= INSTFLAG|ANYFLAG;
                          break;
                     }
                     t = ReadP(MA, 0);
                     if (t & 0100)
                          flags |= MCHCHK|ANYFLAG;
                     sim_interval --;        /* count down */
                     switch(reg) {
                     case 0:     /* Nop */
                             break;
                     case 1:     /* 1 */
                     case 2:     /* 2 */
                     case 3:     /* 4 */
                     case 4:     /* 8 */
                     case 5:     /* A */
                     case 6:     /* B */
                             t &= ~(1<<(reg-1));
                             break;
                     case 7:     /* Reverse A */
                             t ^= 020;
                             break;
                     case 8:     /* Reverse C */
                             t = M[MA % EMEMSIZE] ^ 0100;
                             break;
                     case 9:      /* 1 */
                     case 10:     /* 2 */
                     case 11:     /* 4 */
                     case 12:     /* 8 */
                     case 13:     /* A */
                     case 14:     /* B */
                             t |= 1<<(reg-9);
                             break;
                     }
                     WriteP(MA, t);
                     break;

             default:
                     flags |= ANYFLAG|INSTFLAG;
                     break;
             }
             if (hst_lnt) {  /* history enabled? */
                  hst[hst_p].flags = flags;
                  addr = get_acstart(reg);
                  for (t = 0; t < 254; t++) {
                     hst[hst_p].store[t] = AC[addr];
                     addr = next_addr[addr];
                     if (hst[hst_p].store[t] == 0)
                        break;
                  }
             }
        }
        if (instr_count != 0 && --instr_count == 0)
            return SCPE_STEP;
    }                           /* end while */

/* Simulation halted */
    return reason;
}


/* Read and convert address of instruction */
uint32 read_addr(uint8 *reg, uint8 *zone) {
    uint8       t;
    uint32      addr;

    t = ReadP(MA, INSTFLAG);   /* Read low order digit */
    *zone = (t & 060) >> 2;
    addr = bcd_bin[t & 0xf];
    if ((t & 0xf) > 10)         /* Check valid numeric */
        flags |= INSTFLAG|ANYFLAG;
    MA--;
    t = ReadP(MA, INSTFLAG);    /*  next digit */
    *reg = (t & 060) >> 4;
    if ((t & 0xf) > 10)
        flags |= INSTFLAG|ANYFLAG;
    addr += dig2[t & 0xf];
    MA--;
    t = ReadP(MA, INSTFLAG);            /* Read third digit */
    *reg |= (t & 060) >> 2;
    if ((t & 0xf) > 10)
        flags |= INSTFLAG|ANYFLAG;
    addr += dig3[t & 0xf];
    MA--;
    t = ReadP(MA, INSTFLAG);            /* Save High order address */
    *zone |= (t & 060) >> 4;
    if ((t & 0xf) > 10)
        flags |= INSTFLAG|ANYFLAG;
    addr += dig4[t & 0xf];
    MA--;
    switch (cpu_type) {
    case CPU_7080:      /* 7080 */
        addr += dig_zone[*zone];
        *zone = 0;
        break;
     case CPU_7053:     /* 705-iii */
        addr += dig_zone[*zone & 013];
        *zone &= 04;
        break;
    case CPU_705:       /* 705 */
        /* Can't have any value */
        addr += dig_zone[*zone & 03];
        *zone &= 014;
        break;
    case CPU_702:       /* 702 */
        if (*zone == 02) {      /* B bit in highest digit select AC B */
            *reg = 1;
        } else if (*zone != 0)
            flags |= INSTFLAG|ANYFLAG;
        *zone = 0;
        break;
    }
    return addr;
}

/* Write converted address of instruction */
void write_addr(uint32 addr, uint8 reg, uint8 zone) {
    uint8       value[4];
    int         i;

    if ((MA % 5) != 0) {
        flags |= INSTFLAG|ANYFLAG;
        return;
    }
   /* Convert address into BCD first */
    for(i = 0; i < 4; i++) {
        value[i] = bin_bcd[addr % 10];
        addr /= 10;
    }

    addr = zone_dig[addr & 0xf];
    /* Decode extra addresses and ASU setting */
    switch (cpu_type) {
    case CPU_7080:      /* 7080 */
        value[0] |= (addr & 03) << 4;
        value[3] |= (addr & 0xc) << 2;
        break;
    case CPU_7053:      /* 705-iii */
        /* If 80k emulation */
        if ((cpu_unit.flags & EMULATE2) == 0)
            value[0] |= (addr & 02) << 4;
        value[3] |= (addr & 0xc) << 2;
        break;
    case CPU_705:       /* 705 */
        /* If doing 40k machine */
        if ((cpu_unit.flags & EMULATE2))
            value[3] |= (addr & 0xc) << 2;
        else /* 20k */
            value[3] |= (addr & 0x8) << 2;
        break;
    case CPU_702:       /* 702 */
        if (reg == 1)
           value[3] |= 040;     /* Set minus */
        reg = 0;
        break;
    }
    value[2] |= (reg & 03) << 4;
    value[1] |= (reg & 014) << 2;

   /* Or in zone values */
    value[0] |= (zone & 014) << 2;
    value[3] |= (zone & 03) << 4;

   /* Write it out to memory backwards */
    for(i = 0; i< 4; i++) {
       MA--;
       if ((value[i] & 0xf) == 10)
         value[i] &= 0360;
       if (value[i] == 0)
         value[i] = 10;
       WriteP(MA, value[i]);
    }
}

/* Store converted address in storage */
void store_addr(uint32 addr, int loc) {
    uint8       value[4];
    int         i;

   /* Convert address into BCD first */
    value[0] = bin_bcd[addr % 10];
    addr /= 10;
    value[1] = bin_bcd[addr % 10];
    addr /= 10;
    value[2] = bin_bcd[addr % 10];
    addr /= 10;
    value[3] = bin_bcd[addr % 10];
    addr /= 10;
    addr = zone_dig[addr & 0xf];
    switch (cpu_type) {
    case CPU_7080:      /* 7080 */
        value[0] |= (addr & 03) << 4;
        value[3] |= (addr & 0xc) << 2;
        break;
    case CPU_7053:      /* 705-iii */
        /* If 80k emulation */
        if ((cpu_unit.flags & EMULATE2) == 0)
            value[0] |= (addr & 02) << 4;
        value[3] |= (addr & 0xc) << 2;
        break;
    case CPU_705:       /* 705 */
        /* If doing 40k machine */
        if ((cpu_unit.flags & EMULATE2))
            value[3] |= (addr & 0xc) << 2;
        else /* 20k */
            value[3] |= (addr & 0x8) << 2;
        break;
    case CPU_702:       /* 702 */
        break;
    }

   /* Write it out to storage backwards */
    for(i = 0; i< 4; i++) {
       if ((value[i] & 0xf) == 10)
         value[i] &= 0360;
       if (value[i] == 0)
         value[i] = 10;
       AC[loc] = value[i];
       loc = next_addr[loc];
    }
}


/* Read address from storage */
uint32 load_addr(int loc) {
    uint8       t;
    uint8       zone;
    uint32      addr;

    t = AC[loc];                /* First digit */
    loc = next_addr[loc];
    zone = (t & 060) >> 2;
    addr = bcd_bin[t & 0xf];
    t = AC[loc];                /* Second digit */
    loc = next_addr[loc];
    addr += dig2[bcd_bin[t & 0xf]];
    t = AC[loc];                /* Read third digit */
    loc = next_addr[loc];
    addr += dig3[bcd_bin[t & 0xf]];
    t = AC[loc];                /* Save High order address */
    loc = next_addr[loc];
    zone |= (t & 060) >> 4;
    addr += dig4[bcd_bin[t & 0xf]];
    switch (cpu_type) {
    case CPU_7080:      /* 7080 */
        break;
    case CPU_7053:      /* 705-iii */
        /* If doing 40k */
        if (cpu_unit.flags & EMULATE2)
            zone &= 3;  /* 40k */
        else
            zone &= 013; /* 80k */
        break;
    case CPU_705:       /* 705 */
        if (cpu_unit.flags & EMULATE2)
            zone &= 3;  /* 40K */
        else
            zone &= 1;  /* 20k */
        break;
    case CPU_702:       /* 702 */
        zone = 0;       /* 10k Memory */
        break;
    }
    addr += dig_zone[zone];
    return addr;
}

/* Store converted hex address in storage */
void store_hex(uint32 addr, int loc) {
   /* Convert address into BCD first */
    AC[loc] = bin_bcd[addr & 0xf];
    loc = next_addr[loc];
    AC[loc] = bin_bcd[(addr >> 4) & 0xf];
    loc = next_addr[loc];
    AC[loc] = bin_bcd[(addr >> 8) & 0xf];
    loc = next_addr[loc];
    AC[loc] = bin_bcd[(addr >> 12) & 0xf];
}

/* Read hex address from storage */
uint32 load_hex(int loc) {
    uint8       t;
    uint32      addr;

    t = AC[loc];                /* First digit */
    addr = bcd_bin[t & 0xf];
    loc = next_addr[loc];
    t = AC[loc];                /* Second digit */
    addr += bcd_bin[t & 0xf] << 4;
    loc = next_addr[loc];
    t = AC[loc];                /* Read third digit */
    addr += bcd_bin[t & 0xf] << 8;
    loc = next_addr[loc];
    t = AC[loc];                /* Save High order address */
    addr += bcd_bin[t & 0xf] << 12;
    return addr;
}


/* Compute starting point in Storage for accumulator */
uint16 get_acstart(uint8 reg) {
    if (reg == 0)
        return spc;
    if (cpu_type == CPU_702) {
        return spcb;
    } else {
        uint16 addr;
        addr = (spc & 0x700) | 0x100 | ((reg - 1) << 4);
        if (addr > 0x4ff)
           addr &= 0x4ff;
        return addr;
    }
}

/* Store CPU state in CASU 15 */
void store_cpu(uint32 addr, int full) {
     uint8      t;

     store_addr(IC, addr);
     addr = next_addr[addr];
     addr = next_addr[addr];
     addr = next_addr[addr];
     addr = next_addr[addr];
    /* Save status characters */
     t = flags & 0xf;
     AC[addr] = 040 | ((t + 8) & 027);
     addr = next_addr[addr];
     t = (flags >> 4) & 0xf;
     AC[addr] = 040 | ((t + 8) & 027);
     addr = next_addr[addr];
     t = (flags >> 8) & 0xf;
     AC[addr] = 040 | ((t + 8) & 027);
     addr = next_addr[addr];
     t = (flags >> 12) & 0x3;
     AC[addr] = 040 | t;
     if (full) {
         addr = next_addr[addr];
         AC[addr] = bin_bcd[spc & 7];
         addr = next_addr[addr];
         AC[addr] = bin_bcd[(spc >> 3) & 3];
         addr = next_addr[addr];
         AC[addr] = bin_bcd[(spc >> 5) & 7];
         addr = next_addr[addr];
         AC[addr] = bin_bcd[(spc >> 8) & 7];
         addr = next_addr[addr];
         for(; addr < 0x3F8; addr++)
            AC[addr] = 10;
         for(; addr < 0x400; addr++)
            AC[addr] = 0;
         store_addr(MAC2, 0x3F0);
         store_hex(selreg, 0x3F8);
     }
}

/* Load CPU State from storage */
void load_cpu(uint32 addr, int full) {
    uint8       t;

    IC = load_addr(addr);
    addr = next_addr[addr];
    addr = next_addr[addr];
    addr = next_addr[addr];
    addr = next_addr[addr];
    flags = 0;
    t = AC[addr++];
    flags |= (t & 0x7) | ((t >> 1) & 0x8);
    t = AC[addr++];
    flags |= ((t & 0x7) | ((t >> 1) & 0x8)) << 4;
    t = AC[addr++];
    flags |= ((t & 0x7) | ((t >> 1) & 0x8)) << 8;
    t = AC[addr++];
    flags |= (t & 0x3) << 12;
    /* Adjust Max memory if mode changed */
    EMEMSIZE = MEMSIZE;
    if (flags & EIGHTMODE) {
        cpu_type = CPU_7080;
    } else {
        cpu_type = (cpu_unit.flags & EMULATE3)?  CPU_7053:CPU_705;
        EMEMSIZE = MEMSIZE;
        if (cpu_unit.flags & EMULATE2 && EMEMSIZE > 40000)
            EMEMSIZE = 40000;
        if (cpu_type == CPU_705 && (cpu_unit.flags & EMULATE2) == 0
                        && EMEMSIZE > 20000)
            EMEMSIZE = 20000;
        if (EMEMSIZE > 80000)
            EMEMSIZE = 80000;
    }
    if (full) {
        spc = bcd_bin[AC[addr++]] & 07;      /* Units digit */
        /* One of words */
        spc += (bcd_bin[AC[addr++]] & 3) << 3; /* Tens digit */
        /* One of four word sets */
        spc += (bcd_bin[AC[addr++]] & 7) << 5;  /* Hundreds */
        /* Bank */
        spc += (bcd_bin[AC[addr++]] & 7) << 8;  /* Thousands */
        addr += 4;
        MAC2 = load_addr(addr);
        addr += 8;
        selreg = load_hex(addr);
    }
}

/* Do add or subtract instruction.
   mode == 1 for subtract
   mode == 0 for addition.
   Register is ASU or zero for A.
   smt == 0 if ADD/SUB
   smt == 1 if RSU/RAD
   fmsk is the flags mask to set or clear */
t_stat do_addsub(int mode, int reg, int smt, uint16 fmsk) {
    uint8               cr1, cr2;
    int                 sign;
    int                 msign;
    int                 carry;
    uint32              addr;
    int                 met = 1;
    int                 addsub;

    addr = get_acstart(reg);
    cr1 = ReadP(MA, MCHCHK);
    Next(MA);
    sim_interval --;    /* count down */
    /* Check sign if not valid then treat as 0 */
    msign = 0;
    switch(cr1 & 060) {
    case 000:
    case 020:
             flags |= SGNFLAG|ANYFLAG;
    case 060:
             break;
    case 040:
             msign = 1;
             break;
    }
    /* Fix cr1 to decimal */
    cr1 &= 0xf;

    /* Set sign to sign of Ac */
    sign = (flags & fmsk & SIGN)?1:0;

    /* Set Zero and clear Sign */
    flags |= fmsk & ZERO;
    flags &= ~(fmsk & SIGN);

    /* Decide mode of operation */
    addsub = 0;
    if (smt) {
        sign = (mode)?(!msign):msign;   /* Fix sign */
        cr2 = 0;        /* After end, force zero */
    } else {
        if(mode) {      /* Decide mode based on signs */
            if (sign == msign)
                addsub = 1;
        } else {
            if (sign != msign)
                addsub = 1;
        }
        cr2 = AC[addr];
        if (cr2 == 0)  /* Check for storage mark */
             smt = 0;   /* Done storage */
    }

    smt = !smt;
    carry = addsub;

    /* Process while valid digit in memory */
    while(smt || met) {
        cr2 &= 0xf;
        cr1 = bcd_bin[cr1&0xf] + ((addsub)? comp_bcd[cr2]: bcd_bin[cr2])
                 + carry;
        carry = cr1 >= 10;
        AC[addr] = bin_bcd[cr1];
        /* Update zero flag */
        if (cr1 != 0 && cr1 != 10)
            flags &= ~(fmsk & ZERO);
        addr = next_addr[addr];
        if (met) {
            cr1 = ReadP(MA, MCHCHK);
            if (cr1 == 0 || cr1 > 10) {
                met = 0;  /* End of memory */
                cr1 = 0; /* zero */
            }
            Next(MA);
        } else {
            cr1 = 0;    /* Force to zero */
        }
        /* Grab storage value */
        if (smt) {
            cr2 = AC[addr];
            if (cr2 == 0)  /* Check for storage mark */
                smt = 0;        /* Done storage */
        } else {
            cr2 = 0;    /* After end, force zero */
        }
        sim_interval --;        /* count down */
    }
    AC[addr] = 0;       /* Force storage mark */

    /* Handle last digit */
    if (carry) {
        if (addsub) {
            sign = !sign;
        } else {
        /* Overflow, extend by one digit */
            AC[addr] = 1;
            addr = next_addr[addr];
            AC[addr] = 0;       /* Storage mark */
            flags |= ACOFLAG|ANYFLAG;
            flags &= ~(fmsk & ZERO);
        }
    } else {
        if (addsub) {
        /* Recomplement storage */
            addr = get_acstart(reg);
            carry = 1;
            flags |= fmsk & ZERO;
            while ( AC[addr] != 0) {
                  cr2 = AC[addr];
                  cr2 = comp_bcd[cr2] + carry;
                  carry = cr2 >= 10;            /* Update carry */
                  AC[addr] = bin_bcd[cr2];
                  /* Update zero flag */
                  if (cr2 != 0 && cr2 != 10)
                      flags &= ~(fmsk & ZERO);
                  addr = next_addr[addr];
                  sim_interval --;      /* count down */
             }
        }
    }

    /* Update sign and zero */
    flags |= (fmsk & SIGN) & (sign | (sign << 1));
    flags &= ~(((flags & ZERO) >> 2) & fmsk);
    return SCPE_OK;
}

/* Multiply memory to AC */
t_stat
do_mult(int reg, uint16 fmsk)
{
    uint8               t;
    uint8               at;
    uint8               cr1, cr2;
    uint16              addr;
    uint16              prod;
    int                 mult;
    int                 msign = 0;

    /* Type I cycle */
    addr = get_acstart(reg);
    mult = AC[addr];
    AC[addr] &= 0xf;
    if (AC[addr] == 0)  /* If initial storage mark, replace */
        AC[addr] = 10;  /* With zero */
    prod = next_half[addr];
    flags |= fmsk & ZERO;
    t = 1;
    at = 0;
    /* Check for mark */
    while (mult != 0) {
        /* Type II */
         /* Check signs of B and A. */
         cr1 = ReadP(MA, MCHCHK);
         sim_interval --;       /* count down */
         Next(MA);
         /* Compute sign */
         if (t) {
             switch(cr1 & 060) {
             case 000:
             case 020:
                      flags |= SGNFLAG|ANYFLAG;
             case 060:
                      break;
             case 040:
                      msign = fmsk & SIGN;
                      break;
             }
             t = 0;
             cr1 = bin_bcd[cr1 & 0xf];
         }
         mult = bcd_bin[mult & 0xf];
        /* Type III */
         cr2 = 0;
         while(cr1 >= 1 && cr1 <= 10 ) {
             cr2 += mult * bcd_bin[cr1];
             if (at)
                cr2 += bcd_bin[AC[prod]];
             AC[prod] = bin_bcd[cr2 % 10];
             if (AC[prod] != 10)
                flags &= ~(fmsk & ZERO);
             cr2 /= 10;
             prod = next_addr[prod];
             cr1 = ReadP(MA, MCHCHK);
             Next(MA);
             sim_interval --;   /* count down */
         }
         if (cr2 != 0)
            flags &= ~(fmsk & ZERO);
         AC[prod] = bin_bcd[cr2];
         prod = next_addr[prod];
         AC[prod] = 0;          /* Set storage mark */
        /* Type IV */
         at = 1;                /* Parcial product exists */
         addr = next_addr[addr];
         prod = next_half[addr];        /* Were to put results */
         mult = AC[addr];       /* Grab next digit */
         AC[addr] &= 0xf;       /* Clear zone */
         MA = MAC;              /* Back to start of field */
         t = 1;                 /* Set to handle sign */
    }

    /* Type V */
    /* Update position */
    addr = get_acstart(reg);
    addr = next_half[addr];     /* Adjust pointer */

    if (CPU_MODEL == CPU_702 && reg != 0) {
        spcb = addr;
    } else {
        if (CPU_MODEL == CPU_702)
            spc = addr;
        else if (reg == 0)
            spc = (spc & 0x700) | (addr & 0xff);
    }

    /* Update sign and zero */
    flags ^= msign;
    flags &= ~(((flags & ZERO) >> 2) & fmsk);
    return SCPE_OK;
}

t_stat
do_divide(int reg, uint16 fmsk)
{
    int                 cr1;
    int                 cr2;
    int                 tsac;
    int                 tspc;
    int                 at;
    int                 smt;
    int                 msign;
    int                 remtrig;
    int                 carry;
    int                 dzt;

   /* Step I, put storage mark before start of AC */
    at = 0;
    tspc = get_acstart(reg);
    AC[prev_addr[tspc]] = 0;
    smt = 1;
    carry = 0;
    msign = 0;

   /* Step II, step address until we find storage mark */
step2:
    while(AC[tspc] != 0) {
        AC[tspc] &= 0xf;        /* Make all numeric */
        tspc = next_addr[tspc];
        sim_interval --;        /* count down */
    }

    tsac = next_half[tspc];
    tspc = prev_addr[tspc];
   /* Step III, step second address 128/256 locations. */
    dzt = 1;
    if (at) {
        tspc = next_half[tspc];
        goto done;
    }
    AC[tsac] = 0;
    at = 1;
    smt = 0;
    tsac = tspc;
    sim_interval --;    /* count down */

   /* Step IV, back up first address while advancing MA */
    do {
        sim_interval --;        /* count down */
        cr1 = ReadP(MA, MCHCHK);
        if (AC[tsac] == 0) {    /* Short */
            tsac = next_addr[tsac];
            tspc = tsac;
            goto done;
        }
        if (at) {
             switch(cr1 & 060) {
             case 000:
             case 020:
                     flags |= SGNFLAG|ANYFLAG;
                     /* Fall through */
             case 060:
                     msign = 0;
                     break;
             case 040:
                     msign = (fmsk & SIGN);
                     break;
            }
            at = 0;
        } else if (cr1 == 0 || cr1 > 10) {      /* Next sign digit */
            at = 1;
            MA = MAC;
            tspc = tsac;
            goto step5;
        }
        tsac = prev_addr[tsac];
        Next(MA);
    } while(1); /* Next sign digit */

    /* Type V, perform first subtract */
step5:
    remtrig = 0;
    MA = MAC;
    while (1) {
       /* Step V, subtract Memory from storage */
        cr1 = ReadP(MA, MCHCHK);
        cr2 = AC[tsac];
        sim_interval --;        /* count down */
        if (cr2 == 0) {
            tspc = next_addr[tspc];
            goto step9;
        } else if (at) {
            carry = 1;
            cr1 &= 017;
            at = 0;
        } else if (cr1 == 0 || cr1 > 10) {
            cr1 = comp_bcd[cr2] + carry;
            carry = cr1 >= 10;
            AC[tsac] = bin_bcd[cr1];
            MA = MAC;
            tsac = next_half[tsac];
            at = 1;
            goto step6;
        }
        Next(MA);
        cr1 = comp_bcd[cr2] + bcd_bin[cr1] + carry;
        carry = cr1 >= 10;
        AC[tsac] = bin_bcd[cr1];
        if (AC[tsac] != 10)
            remtrig = 1;
        tsac = next_addr[tsac];
    }

step6:
    cr2 = AC[tsac];
    cr1 = 1;
    if (carry) {
            smt = 0;
            if (remtrig) {
                if (at) {
                   AC[tsac] = 10;
                } else {
                   at = 1;
                }
                tsac = tspc;
                goto step8;
            } else {
                int t;
                if (at)
                   cr2 = 0;
                else
                   cr2 = bin_bcd[cr2];
                t = cr2 + 1;
                AC[tsac] = bin_bcd[t];
                tsac = tspc;
                if (t >= 10) {
                   flags |= ACOFLAG|ANYFLAG;
                   at = 1;
                   goto step2;
                }
                dzt = 0;
                at = 0;
                goto step9;
            }
     } else {
            int t;
            if (at)
                cr2 = 0;
            else
                cr2 = bcd_bin[cr2];
            t = cr2 + 1;
            AC[tsac] = bin_bcd[t];
            tsac = tspc;
            remtrig = 0;
            at = 1;
            if (t >= 10) {
                flags |= ACOFLAG|ANYFLAG;
                goto step2;
            }
            dzt = 0;
     }
     smt = 0;
     while(!smt) {
        cr1 = ReadP(MA, MCHCHK);
        Next(MA);
        sim_interval --;        /* count down */
        cr2 = AC[tsac];
        if (cr2 == 0)
            goto step6;
        if (at) {
            cr1 &= 017;
            at = 0;
        } else if (cr1 == 0 || cr1 > 10) {
            cr2 = bcd_bin[cr2] + carry;
            carry = cr2 >= 10;
            AC[tsac] = bin_bcd[cr2];
            if (AC[tsac] != 10)
                remtrig = 1;
            MA = MAC;
            tsac = next_half[tsac];
            break;               /* goto step6; */
        }
        cr2 = bcd_bin[cr2] + bcd_bin[cr1] + carry;
        carry = cr2 >= 10;
        AC[tsac] = bin_bcd[cr2];
        if (AC[tsac] != 10)
            remtrig = 1;
        tsac = next_addr[tsac];
    }
    goto step6;

step8:
     smt = 0;
     while(!smt) {
        cr1 = ReadP(MA, MCHCHK);
        Next(MA);
        sim_interval --;        /* count down */
        cr2 = AC[tsac];
        if (cr2 == 0)
            smt = 1;
        if (at) {
            at = 0;
            cr1 &= 017;
            carry = 1;
        } else {
            if (cr1 == 0 || cr1 > 10) {
                cr2 = comp_bcd[cr2] + carry;
                carry = cr2 >= 10;
                AC[tsac] = bin_bcd[cr2];
                MA = MAC;
                tsac = tspc;
                goto step9;
            }
        }
        cr2 = comp_bcd[cr2] + bcd_bin[cr1] + carry;
        carry = cr2 >= 10;
        AC[tsac] = bin_bcd[cr2];
        tsac = next_addr[tsac];
    };

    /* Step 9 */
step9:
    if (at) {
        tspc = next_half[tspc];
        Next(MA);
        goto step10;
    } else {
        tsac = prev_addr[tsac];
        tspc = prev_addr[tspc];
        remtrig = 0;
        at = 1;
        goto step5;
    }

   /* Step X */
step10:
    do {
        cr1 = ReadP(MA, MCHCHK);
        Next(MA);
        sim_interval --;        /* count down */
        tspc = next_addr[tspc];
    } while (cr1 > 0 && cr1 <= 10);
done:
   if (CPU_MODEL == CPU_702)
       spc = tspc;
   else
       spc = (spc & 0x700) | (tspc & 0xff);

   if (dzt)
        flags |= (fmsk & ZERO);
   else
        flags &= ~(fmsk & ZERO);

   /* Update sign and zero */
   flags ^= msign;
   flags &= ~(((flags & ZERO) >> 2) & fmsk);
   return SCPE_OK;
}

t_stat
do_compare(int reg, int tluop) {
    int         addr;
    uint8       cr1;
    uint8       cr2;


    addr = get_acstart(reg);
    flags &= ~CMPFLAG;
    while(AC[addr] != 0) {
        int         sup8;
    cmpnext:
        cr2 = AC[addr];
        if (cr2 == 0)
            break;
        cr1 = ReadP(MA, MCHCHK);
        sim_interval--; /* count down */
        /* Stop table opcode on GM or RM */
        if (tluop && (cr1 == CHR_GM || cr1 == CHR_RM)) {
            bkcmp = 0;
            return SCPE_OK;
        }
        if ((cr1 & 0xf) > 10)
            sup8 = 007;
        else
            sup8 = 017;
        if(bkcmp) {
            Prev(MA);
        } else {
            Next(MA);
        }
        addr = next_addr[addr];
        if (cr1 == CHR_BLANK) {
            if (cr2 != CHR_BLANK) {
               flags &= ~CMPFLAG;
               flags |= HIGHFLAG;
            }
            goto cmpnext;
        }
        if (cr2 == CHR_BLANK) {
           flags &= ~CMPFLAG;
           flags |= LOWFLAG;
        } else {
           int t1 = cr1 & 017;
           int t2 = cr2 & 017;
           if ((t1 == 11) || (t1 == 12)) { /* CR1 Special? */
               if ((t2 != 11) && (t2 != 12)) { /* CR2 not special */
                  flags &= ~CMPFLAG;
                  flags |= HIGHFLAG;
                  goto cmpnext;
               }
           } else if ((t2 == 11) || (t2 == 12)) {/* CR2 special */
               if ((t1 != 11) && (t1 != 12)) { /* CR1 not special */
                  flags &= ~CMPFLAG;
                  flags |= LOWFLAG;
                  goto cmpnext;
               }
           }
           if ((cr1 & 060) != (cr2 & 060)) {  /* Check zones */
                 flags &= ~CMPFLAG;
                 t1 = (cr1 & 060) + (060 ^ (cr2 & 060));
                 flags |= (t1 & 0100) ? HIGHFLAG:LOWFLAG;
           } else {     /* Zones same */
                 if ((cr1 ==  040) || (cr1 == 060)) {
                    if ((cr2 != 040) && (cr2 != 060))  {
                        flags &= ~CMPFLAG;
                        flags |= LOWFLAG;
                        goto cmpnext;
                    }
                 } else if ((cr2 == 040) || (cr2 == 060)) {
                    flags &= ~CMPFLAG;
                    flags |= HIGHFLAG;
                    goto cmpnext;
                 }
                /* Compare actual digits */
                 t1 = bcd_bin[t1 & sup8] + comp_bcd[t2] + 1;
                 if (t1 != 10) {
                     flags &= ~CMPFLAG;
                     flags |= (t1 <= 10)?HIGHFLAG:LOWFLAG;
                }
            }
        }
    }
    bkcmp = 0;
    return SCPE_OK;
}


/* Initialize memory to all blank */
void
mem_init() {
    int                 i;
    /* Force memory to be blanks on load */
    for(i = 0; i < (MAXMEMSIZE-1); i++)
        M[i] = CHR_BLANK;
    MEMSIZE = (((cpu_unit.flags & UNIT_MSIZE) >> UNIT_V_MSIZE) + 1) * 10000;
    EMEMSIZE = MEMSIZE;
}


/* Reset routine */
t_stat
cpu_reset(DEVICE * dptr)
{
    int                 i;
    int                 n,p,h;

    /* Set next and previous address arrays based on CPU type */
    if (CPU_MODEL == CPU_702) {
        for(i = 0; i < 512; i++) {
           n = (i + 1) & 0777;          /* A */
           p = (i - 1) & 0777;
           h = (i + 256) & 0777;
           next_addr[i] = n;            /* A */
           prev_addr[i] = p;
           next_half[i] = h;
           next_addr[i+512] = 512 + n;  /* B */
           prev_addr[i+512] = 512 + p;
           next_half[i+512] = 512 + h;
        }
        cpu_reg[1].depth = 512;
        cpu_reg[2].offset = 512;
        cpu_reg[2].depth = 512;
        cpu_reg[2].loc = &AC[512];
     } else {
        for(i = 0; i < 256; i++) {
           n = next_addr[i] = (i + 1) & 0377;           /* A */
           p = prev_addr[i] = (i - 1) & 0377;
           h = next_half[i] = (i + 128) & 0377;
           next_addr[i+256] = 256 + n;          /* Bank 1 */
           prev_addr[i+256] = 256 + p;
           next_half[i+256] = 256 + h;
           next_addr[i+512] = 512 + n;          /* Bank 2 */
           prev_addr[i+512] = 512 + p;
           next_half[i+512] = 512 + h;
           next_addr[i+768] = 768 + n;          /* Bank 3 */
           prev_addr[i+768] = 768 + p;
           next_half[i+768] = 768 + h;
           next_addr[i+1024] = 1024 + n;        /* Bank 4 */
           prev_addr[i+1024] = 1024 + p;
           next_half[i+1024] = 1024 + h;
           next_addr[i+1280] = 1280 + n;        /* Bank 5 */
           prev_addr[i+1280] = 1280 + p;
           next_half[i+1280] = 1280 + h;
        }
        cpu_reg[1].depth = 256;
        cpu_reg[2].offset = 256;
        for(i = 0; i < 15; i++) {
            cpu_reg[i+2].loc = &AC[256 + 16*i];
            cpu_reg[i+2].depth = 256;
        }
    }

    /* Clear io error flags */
    memset(ioflags, 0, sizeof(ioflags));
    /* Clear accumulators to storage mark */
    memset(AC, 0, sizeof(AC));
    flags = 0;
    intmode = 0;
    intprog = 0;
    irqflags = 0;
    selreg = 0;
    selreg2 = 0;
    IC = 4;
    sim_brk_types = sim_brk_dflt = SWMASK('E');
    return SCPE_OK;
}

/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MEMSIZE)
        return SCPE_NXM;
    if (vptr != NULL)
        *vptr = M[addr] & 077;

    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MEMSIZE)
        return SCPE_NXM;
    M[addr] = val & 077;
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_uint64            mc = 0;
    uint32              size;
    uint32              i;

    size = val >> UNIT_V_MSIZE;
    size++;
    size *= 10000;
    if (size > MAXMEMSIZE)
        return SCPE_ARG;
    for (i = size-1; i < MEMSIZE; i++) {
        if (M[i] != CHR_BLANK) {
           mc = 1;
           break;
        }
    }
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    cpu_unit.flags &= ~UNIT_MSIZE;
    cpu_unit.flags |= val;
    EMEMSIZE = MEMSIZE = size;
    for (i = MEMSIZE - 1; i < (MAXMEMSIZE-1); i++)
        M[i] = CHR_BLANK;
    return SCPE_OK;
}

/* Handle execute history */

/* Set history */
t_stat
cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    int32               i, lnt;
    t_stat              r;

    if (cptr == NULL) {
        for (i = 0; i < hst_lnt; i++)
            hst[i].ic = 0;
        hst_p = 0;
        return SCPE_OK;
    }
    lnt = (int32) get_uint(cptr, 10, HIST_MAX, &r);
    if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
        return SCPE_ARG;
    hst_p = 0;
    if (hst_lnt) {
        free(hst);
        hst_lnt = 0;
        hst = NULL;
    }
    if (lnt) {
        hst = (struct InstHistory *)calloc(sizeof(struct InstHistory), lnt);

        if (hst == NULL)
            return SCPE_MEM;
        hst_lnt = lnt;
    }
    return SCPE_OK;
}

/* Show history */

t_stat
cpu_show_hist(FILE * st, UNIT * uptr, int32 val, CONST void *desc)
{
    int32               k, di, lnt;
    char               *cptr = (char *) desc;
    int                 len;
    t_stat              r;
    t_value             sim_eval[50];
    struct InstHistory *h;

    if (hst_lnt == 0)
        return SCPE_NOFNC;      /* enabled? */
    if (cptr) {
        lnt = (int32) get_uint(cptr, 10, hst_lnt, &r);
        if ((r != SCPE_OK) || (lnt == 0))
            return SCPE_ARG;
    } else
        lnt = hst_lnt;
    di = hst_p - lnt;           /* work forward */
    if (di < 0)
        di = di + hst_lnt;
    fprintf(st, "IC      OP   MA      REG\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->ic & HIST_PC) {  /* instruction? */
            fprintf(st, "%06d %c %06d %02d ", h->ic & 0x3ffff,
                        mem_to_ascii[h->op], h->ea, h->reg);
            sim_eval[0] = (h->inst >> (4 * 6)) & 077;
            sim_eval[1] = (h->inst >> (3 * 6)) & 077;
            sim_eval[2] = (h->inst >> (2 * 6)) & 077;
            sim_eval[3] = (h->inst >> (1 * 6)) & 077;
            sim_eval[4] = h->inst & 077;
            (void)fprint_sym (st, h->ic, sim_eval, &cpu_unit, SWMASK('M'));
            for(len = 0; len < 256 && (h->store[len] & 077) != 0; len++);
            fprintf(st, "\t%-2d %c%c %c%c %c@", len,
                    (h->flags & AZERO)?'Z':' ', (h->flags & ASIGN)?'-':'+',
                    (h->flags & BZERO)?'Z':' ', (h->flags & BSIGN)?'-':'+',
                    (h->flags & LOWFLAG)? 'l' :
                        ((h->flags & HIGHFLAG) ? 'h' : 'e'));

            for(len--; len >= 0; len--)
                fputc(mem_to_ascii[h->store[len] & 077], st);
            fputc('@', st);
            if (h->flags & 0x7f0) {
                int     i;
                fputc(' ', st);
                for (i = 0; i < 7; i++) {
                    if (h->flags & (0x10 << i))
                        fputc('0' + i, st);
                }
            }
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr)
{
       return "IBM 7080 CPU";
}

t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The CPU can be set to a IBM 702, IBM 705, IBM 705/3 or IBM 7080\n");
    fprintf (st, "The type of CPU can be set by one of the following commands\n\n");
    fprintf (st, "   sim> set CPU 702         sets IBM 704 emulation\n");
    fprintf (st, "   sim> set CPU 705         sets IBM 705 emulation\n");
    fprintf (st, "   sim> set CPU 7053        sets IBM 705/3 emulation\n");
    fprintf (st, "   sim> set CPU 7080        sets IBM 7080 emulation\n\n");
    fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\n\n");
    fprintf (st, "      -c      examine/deposit characters\n");
    fprintf (st, "      -s      examine 50 characters\n");
    fprintf (st, "      -d      examine 50 characters\n");
    fprintf (st, "      -m      examine/deposit IBM 7080 instructions\n\n");
    fprintf (st, "The memory of the CPU can be set in 10K incrememts from 10K to 160K with the\n\n");
    fprintf (st, "   sim> SET CPU xK\n\n");
    fprintf (st, "For the IBM 7080 the following options can be enabled\n\n");
    fprintf (st, "   sim> SET CPU EMU40K      enables memory above 40K\n");
    fprintf (st, "   sim> SET CPU NOEMU40K    disables memory above 40K\n\n");
    fprintf (st, "   sim> SET CPU EMU705     enables IBM7080 to support 705 Emulation.\n");
    fprintf (st, "   sim> SET CPU NOEMU705   disables IBM7080 to support 705 Emulation.\n\n");
    fprintf (st, "   sim> SET CPU NOSTOP    CPU will not stop on invalid conditions\n");
    fprintf (st, "   sim> SET CPU PRORAM    CPU stop under program control\n\n");
    fprintf (st, "The CPU can maintain a history of the most recently executed instructions.\n");
    fprintf (st, "This is controlled by the SET CPU HISTORY and SHOW CPU HISTORY commands:\n\n");
    fprintf (st, "   sim> SET CPU HISTORY                 clear history buffer\n");
    fprintf (st, "   sim> SET CPU HISTORY=0               disable history\n");
    fprintf (st, "   sim> SET CPU HISTORY=n{:file}        enable history, length = n\n");
    fprintf (st, "   sim> SHOW CPU HISTORY                print CPU history\n\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);

    return SCPE_OK;
}

