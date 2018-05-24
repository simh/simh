/* b5500_cpu.c: burroughs 5500 cpu simulator

   Copyright (c) 2016, Richard Cornwell

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

   The Burroughs 5500 was a unique machine, first introduced in 1961 as the
   B5000. Later advanced to the B5500 (1964) adding disks and finally the B5700
   (1971) adding solid state drum. It was the first computer to use the stack
   as it's only means of accessing data. The machine could access at most
   32k words of memory.

   The machine used 48 bit numbers, all of which were considered to be floating
   point numbers, integers were represented by a zero exponent. A word could
   also be used to hold up to 8 6-bit characters.

   The differences between the various models were minor. The 5500 added
   the LLL, TUS, FBS and XRT instructions to improve performance of the OS. The
   5700 added a core memory drum instead of spinning disk.

   The 5500 series tagged memory to assist in controlling access.

   The 5000 series did not have many programer accessible registers, all
   operations were done on the stack. It had two modes of operation, character
   and word mode.

   A register 48 bits, held the top of stack.
      AROF flag indicated whether A was full or not.
   B register 48 bits, held the second element of the stack.
      BROF flag indicated whether B was full or not.

   S register 15 bits held a pointer to the top of stack in memory.
   F register 15 bits held the Frame pointer.
   R register 15 bits held a pointer to the per process procedures and
        variables.
   C register 15 bits together with the L register (2 bits) held the
     pointer to the current executing sylable.

   When in character mode the registers changed meaning a bit.

   A held the Source word. GH two 3 bit registers held the character,bit
      offset in the word.
   B held the Destination word. KV two 3 bit registers held the
      character and bit offset in the word.

   The M register used to access memory held the address of the source
     characters.
   The S register held the address of the destination characters.
   The R register held a count register refered to as TALLY.
   The F register held the info need to return back to word mode.

   The generic data word was: Flag = 0.

                 11111111112222222222333333333344444444
   0 1 2 345678 901234567890123456789012345678901234567
  +-+-+-+------+---------------------------------------+
  |F|M|E|Exp   | Mantissa                              |
  |l|s|s|in    |                                       |
  |a|i|i|octant|                                       |
  |g|g|g|      |                                       |
  | |n|n|      |                                       |
  +-+-+-+------+---------------------------------------+

  Also 8 6 bit characters could be used.

  With the Flag bit 1 various data pointers could be constructed.

                 11111111 112222222222333 333333344444444
   0 1 2 345 678901234567 890123456789012 345678901234567
  +-+-+-+---+------------+---------------+---------------+
  |F|D|P|f  | Word count | F Field       | Address       |
  |l|f|r|l  | R Field    |               |               |
  |a|l|e|a  |            |               |               |
  |g|a|s|g  |            |               |               |
  | |g| |s  |            |               |               |
  +-+-+-+---+------------+---------------+---------------+

*/

#include "b5500_defs.h"
#include "sim_timer.h"
#include <math.h>
#include <time.h>

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (7 << UNIT_V_MSIZE)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)

#define TMR_RTC         0

#define HIST_MAX        5000
#define HIST_MIN        64

t_uint64 bit_mask[64] = {
        00000000000000001LL,
        00000000000000002LL,
        00000000000000004LL,
        00000000000000010LL,
        00000000000000020LL,
        00000000000000040LL,
        00000000000000100LL,
        00000000000000200LL,
        00000000000000400LL,
        00000000000001000LL,
        00000000000002000LL,
        00000000000004000LL,
        00000000000010000LL,
        00000000000020000LL,
        00000000000040000LL,
        00000000000100000LL,
        00000000000200000LL,
        00000000000400000LL,
        00000000001000000LL,
        00000000002000000LL,
        00000000004000000LL,
        00000000010000000LL,
        00000000020000000LL,
        00000000040000000LL,
        00000000100000000LL,
        00000000200000000LL,
        00000000400000000LL,
        00000001000000000LL,
        00000002000000000LL,
        00000004000000000LL,
        00000010000000000LL,
        00000020000000000LL,
        00000040000000000LL,
        00000100000000000LL,
        00000200000000000LL,
        00000400000000000LL,
        00001000000000000LL,
        00002000000000000LL,
        00004000000000000LL,
        00010000000000000LL,
        00020000000000000LL,
        00040000000000000LL,
        00100000000000000LL,
        00200000000000000LL,
        00400000000000000LL,
        01000000000000000LL,
        02000000000000000LL,
        04000000000000000LL,
        0
};

uint8 bit_number[64] = {
    /*  00  01  02  03  04  05  06  07 */
        47, 46, 45, 44, 43, 42, 42, 42, /* 00 */
        41, 40, 39, 38, 37, 36, 36, 36, /* 10 */
        35, 34, 33, 32, 31, 30, 30, 30, /* 20 */
        29, 28, 27, 26, 25, 24, 24, 24, /* 30 */
        23, 22, 21, 20, 19, 18, 18, 18, /* 40 */
        17, 16, 15, 14, 13, 12, 12, 12, /* 50 */
        11, 10,  9,  8,  7,  6,  6,  6, /* 60 */
         5,  4,  3,  2,  1,  0,  0,  0, /* 70 */
};

uint8 rank[64] = {
     /* 00  01  02  03  04  05  06  07 */
        53, 54, 55, 56, 57, 58, 59, 60,  /* 00 */
      /* 8   9   #   @   ?   :   >  ge  */
        61, 62, 19, 20, 63, 21, 22, 23,  /* 10 */
      /* +   A   B   C   D   E   F   G */
        24, 25, 26, 27, 28, 29, 30, 31,  /* 20 */
      /* H   I   .   [   &   (   <  ar    */
        32, 33,  1,  2,  6,  3,  4,  5,  /* 30 */
      /* ti  J   K   L   M   N   O   P  */
        34, 35, 36, 37, 38, 39, 40, 41,  /* 40 */
      /* Q   R   $   *   -   )   ;  le  */
        42, 43,  7,  8, 12,  9, 10, 11,  /* 50 */
     /* bl   /   S   T   U   V   W   X  */
         0, 13, 45, 46, 47, 48, 49, 50,  /* 60 */
     /*  Y   Z   ,   %  ne   =   ]   "  */
        51, 52, 14, 15, 44, 16, 17, 18,  /* 70 */
};


int                 cpu_index;                  /* Current running cpu */
t_uint64            M[MAXMEMSIZE] = { 0 };      /* memory */
t_uint64            a_reg[2];                   /* A register */
t_uint64            b_reg[2];                   /* B register */
t_uint64            x_reg[2];                   /* extension to B */
t_uint64            y_reg[2];                   /* extension to A not original */
uint8               arof_reg[2];                /* True if A full */
uint8               brof_reg[2];                /* True if B full */
uint8               gh_reg[2];                  /* G & H source char selectors */
uint8               kv_reg[2];                  /* K & V dest char selectors */
uint16              ma_reg[2];                  /* M memory address regiser */
uint16              s_reg[2];                   /* S Stack pointer */
uint16              f_reg[2];                   /* F MCSV pointer */
uint16              r_reg[2];                   /* R PRT pointer */
t_uint64            p_reg[2];                   /* P insruction buffer */
uint8               prof_reg[2];                /* True if P valid */
uint16              t_reg[2];                   /* T current instruction */
uint8               trof_reg[2];                /* True if T valid */
uint16              c_reg[2];                   /* C program counter */
uint16              l_reg[2];                   /* L current syllable pointer */
uint8               ncsf_reg[2];                /* True if normal state */
uint8               salf_reg[2];                /* True if subrogram mode */
uint8               cwmf_reg[2];                /* True if character mode */
uint8               hltf[2];                    /* True if processor halted */
uint8               msff_reg[2];                /* Mark stack flag Word mode */
#define TFFF MSFF                               /* True state in Char mode */
uint8               varf_reg[2];                /* Variant Flag */
uint8               q_reg[2];                   /* Holds error code */
uint16              IAR;                        /* Interrupt register */
uint32              iostatus;                   /* Hold status of devices */
uint8               RTC;                        /* Real time clock counter */
uint8               loading;                    /* Set when loading */
uint8               HALT;                       /* Set when halt requested */
uint8               P1_run;                     /* Run flag for P1 */
uint8               P2_run;                     /* Run flag for P2 */
uint16              idle_addr = 0;              /* Address of idle loop */


struct InstHistory
{
        uint16          c;
        uint16          op;
        uint16          s;
        uint16          f;
        uint16          r;
        uint16          ma;
        t_uint64        a_reg;
        t_uint64        b_reg;
        t_uint64        x_reg;
        uint8           flags;
        uint8           gh;
        uint8           kv;
        uint16          l;
        uint8           q;
        uint8           cpu;
        uint16          iar;
};

struct InstHistory *hst = NULL;
int32               hst_p = 0;
int32               hst_lnt = 0;

#define F_AROF          00001
#define F_BROF          00002
#define F_CWMF          00004
#define F_NCSF          00010
#define F_SALF          00020
#define F_MSFF          00040
#define F_VARF          00100
#define HIST_PC         0100000

t_stat              cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr,
                           int32 sw);
t_stat              cpu_dep(t_value val, t_addr addr, UNIT * uptr,
                            int32 sw);
t_stat              cpu_reset(DEVICE * dptr);
t_stat              cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_show_size(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_show_hist(FILE * st, UNIT * uptr, int32 val,
                                  CONST void *desc);
t_stat              cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr,
                                 void *desc);
t_stat              cpu_help(FILE *, DEVICE *, UNIT *, int32, const char *);
/* Interval timer */
t_stat              rtc_srv(UNIT * uptr);

int32               rtc_tps = 60 ;


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit[] =
    {{ UDATA(rtc_srv, MEMAMOUNT(7)|UNIT_IDLE, MAXMEMSIZE ), 16667 },
    { UDATA(0, UNIT_DISABLE|UNIT_DIS, 0 ), 0 }};

REG                 cpu_reg[] = {
    {BRDATAD(C, c_reg, 8,15,2,   "Instruction pointer"), REG_FIT},
    {BRDATAD(L, l_reg, 8,2,2,    "Sylable pointer")},
    {BRDATA(A, a_reg, 8,48,2), REG_FIT},
    {BRDATA(B, b_reg, 8,48,2), REG_FIT},
    {BRDATA(X, x_reg, 8,39,2), REG_FIT},
    {BRDATA(GH, gh_reg, 8,6,2)},
    {BRDATA(KV, kv_reg, 8,6,2)},
    {BRDATAD(MA, ma_reg, 8,15,2, "Memory address")},
    {BRDATAD(S, s_reg, 8,15,2,   "Stack pointer")},
    {BRDATAD(F, f_reg, 8,15,2,   "Frame pointer")},
    {BRDATAD(R, r_reg, 8,15,2,   "PRT pointer/Tally")},
    {BRDATAD(P, p_reg, 8,48,2,   "Last code word cache")},
    {BRDATAD(T, t_reg, 8,12,2,   "Current instruction")},
    {BRDATAD(Q, q_reg, 8,9,2,    "Error condition")},
    {BRDATA(AROF, arof_reg, 2,1,2)},
    {BRDATA(BROF, brof_reg, 2,1,2)},
    {BRDATA(PROF, prof_reg, 2,1,2)},
    {BRDATA(TROF, trof_reg, 2,1,2)},
    {BRDATA(NCSF, ncsf_reg, 2,1,2)},
    {BRDATA(SALF, salf_reg, 2,1,2)},
    {BRDATA(CWMF, cwmf_reg, 2,1,2)},
    {BRDATA(MSFF, msff_reg, 2,1,2)},
    {BRDATA(VARF, varf_reg, 2,1,2)},
    {BRDATA(HLTF, hltf, 2,1,2)},
    {ORDATAD(IAR, IAR, 15,      "Interrupt pending")},
    {ORDATAD(TUS, iostatus, 32, "Perpherial ready status")},
    {FLDATA(HALT, HALT, 0)},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(0), NULL, "4K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(1), NULL, "8K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(2), NULL, "12K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(3), NULL, "16K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(4), NULL, "20K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(5), NULL, "24K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(6), NULL, "28K", &cpu_set_size},
    {UNIT_MSIZE|MTAB_VDV, MEMAMOUNT(7), NULL, "32K", &cpu_set_size},
    {MTAB_VDV, 0, "MEMORY", NULL, NULL, &cpu_show_size},
    {MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    {MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", cpu_unit, cpu_reg, cpu_mod,
    2, 8, 15, 1, 8, 48,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help
};



/* Define registers */
#define A       a_reg[cpu_index]
#define B       b_reg[cpu_index]
#define C       c_reg[cpu_index]
#define L       l_reg[cpu_index]
#define X       x_reg[cpu_index]
#define Y       y_reg[cpu_index]
#define Q       q_reg[cpu_index]
#define GH      gh_reg[cpu_index]
#define KV      kv_reg[cpu_index]
#define Ma      ma_reg[cpu_index]
#define S       s_reg[cpu_index]
#define F       f_reg[cpu_index]
#define R       r_reg[cpu_index]
#define P       p_reg[cpu_index]
#define T       t_reg[cpu_index]
#define AROF    arof_reg[cpu_index]
#define BROF    brof_reg[cpu_index]
#define PROF    prof_reg[cpu_index]
#define TROF    trof_reg[cpu_index]
#define NCSF    ncsf_reg[cpu_index]
#define SALF    salf_reg[cpu_index]
#define CWMF    cwmf_reg[cpu_index]
#define MSFF    msff_reg[cpu_index]
#define VARF    varf_reg[cpu_index]
#define HLTF    hltf[cpu_index]

/* Definitions to help extract fields */
#define FF(x)    (uint16)(((x) & FFIELD) >> FFIELD_V)
#define CF(x)    (uint16) ((x) & CORE)
#define LF(x)    (uint16)(((x) & RL) >> RL_V)
#define RF(x)    (uint16)(((x) & RFIELD) >> RFIELD_V)

#define toF(x)   ((((t_uint64)(x)) << FFIELD_V) & FFIELD)
#define toC(x)    (((t_uint64)(x)) & CORE)
#define toL(x)   ((((t_uint64)(x)) << RL_V) & RL)
#define toR(x)   ((((t_uint64)(x)) << RFIELD_V) & RFIELD)

#define replF(y, x)   ((y & ~FFIELD) | toF(x))
#define replC(y, x)   ((y & ~CORE) | toC(x))

#define next_addr(x)   (x = (x + 1) & 077777)
#define prev_addr(x)   (x = (x - 1) & 077777)

/* Definitions to handle building of control words */
#define MSCW     (FLAG | DFLAG | toR(R) | toF(F) | \
                                ((MSFF)?SMSFF:0) | ((SALF)?SSALF:0))
#define ICW      (FLAG | DFLAG | toR(R) | ((VARF)?SVARF:0) | \
                    ((MSFF)?SMSFF:0) | ((SALF)?SSALF:0)) | toC(Ma)
#define Pointer(x)      ((t_uint64)((((x) & 070) >> 3) | ((x & 07) << 8)))
#define RCW(x)   (FLAG | DFLAG | toF(F) | toC(C) | toL(L) | \
                        (Pointer(GH) << RGH_V) | (Pointer(KV) << RKV_V)) | \
                        ((x)?PRESENT:0)
#define LCW(f, x)        toF(f) | toC(C) | toL(L) | \
                                (((t_uint64)(x) << REPFLD_V) & REPFLD)
#define VARIANT(x) ((x) >> 6)


/* E
        2       A = M[S]
        3       B = M[S]
        4       A = M[Ma]
        5       B = M[Ma]
        6       Ma = M[Ma]<18:32>
        10      M[S] = A           1010
        11      M[S] = B           1011
        12      M[Ma] = A          1100
        13      M[Ma] = B          1101

        1       B/A
        2       S
        4       Ma
        8       Write/Read
        16      Fetch
*/

int memory_cycle(uint8 E) {
        uint16 addr = 0;

        sim_interval--;
        if (E & 2)
           addr = S;
        if (E & 4)
           addr = Ma;
        if (E & 020)
           addr = C;
        if (addr > MEMSIZE) {
           Q |= INVALID_ADDR;
           return 1;
        }
        if (NCSF && addr < 01000) {
           Q |= INVALID_ADDR;
           return 1;
        }
        if (E & 020) {
            P = M[addr];
            PROF = 1;
            return 0;
        }
        if (E & 010) {
            if (E & 1)
                M[addr] = B;
            else
                M[addr] = A;
        } else {
            if (E == 6) {
                B = M[addr];
                Ma = FF(B);
            } else if (E & 1) {
                B = M[addr];
                BROF = 1;
            } else {
                A = M[addr];
                AROF = 1;
            }
        }
        return 0;
}


/* Set registers based on MSCW */
void set_via_MSCW(t_uint64 word) {
        F = FF(word);
        R = RF(word);
        MSFF = (word & SMSFF) != 0;
        SALF = (word & SSALF) != 0;
}

/* Set registers based on RCW.
   if no_set_lc is non-zero don't set LC from RCW.
   if no_bits is non-zero don't set GH and KV,
   return BROF flag  */
int  set_via_RCW(t_uint64 word, int no_set_lc, int no_bits) {
        if (!no_set_lc) {
            L = LF(word);
            C = CF(word);
            PROF = 0;
        }
        F = FF(word);
        if (!no_bits) {
            uint16 t;
            t = (uint16)((word & RGH) >> RGH_V);
            GH = ((t << 3) & 070) | ((t >> 8) & 07);
            t = (uint16)((word & RKV) >> RKV_V);
            KV = ((t << 3) & 070) | ((t >> 8) & 07);
        }
        return (word & PRESENT) != 0;
}

/* Set the stack pointer from INCW */
void set_via_INCW(t_uint64 word) {
        S = CF(word);
        CWMF = (word & SCWMF) != 0;
}

/* Set registers from ICW */
void set_via_ICW(t_uint64 word) {
        Ma = CF(word);
        MSFF = (word & SMSFF) != 0;
        SALF = (word & SSALF) != 0;
        VARF = (word & SVARF) != 0;
        R = RF(word);
}

/* Make sure that B is empty */
void B_empty() {
    if (BROF) {
        next_addr(S);
        if (NCSF && (S & 077700) == R) {
           Q |= STK_OVERFL; /* Stack fault */
           return;
        }
        memory_cycle(013);      /* Save B */
        BROF = 0;
    }
}

/* Make sure A is empty, push to B if not */
void A_empty() {
    if (AROF) {
        B_empty();
        B = A;
        AROF = 0;
        BROF = 1;
     }
}

/* Make sure both A and B are empty */
void AB_empty() {
    B_empty();
    if (AROF) {
        next_addr(S);
        if (NCSF && (S & 077700) == R) {
           Q |= STK_OVERFL; /* Stack fault */
           return;
        }
        memory_cycle(012);      /* Save A */
        AROF = 0;
    }
}

/* Make sure that A is valid, copy from B or memory */
void A_valid() {
    if (!AROF) {
        if (BROF) {             /* Transfer B to A */
            A = B;
            AROF = 1;
            BROF = 0;
        } else {
            if (NCSF && (S & 077700) == R) {
               Q |= STK_OVERFL; /* Stack fault */
               return;
            }
            memory_cycle(2);    /* Read A */
            prev_addr(S);
        }
    }
}

/* Make sure both A and B are valid */
void AB_valid() {
    A_valid();
    if (!BROF) {
        if (NCSF && (S & 077700) == R) {
            Q |= STK_OVERFL; /* Stack fault */
            return;
        }
        memory_cycle(3);        /* Read B */
        prev_addr(S);
    }
}

/* Make sure A is empty and B is valid */
void B_valid() {
    A_empty();
    if (!BROF) {
        if (NCSF && (S & 077700) == R) {
            Q |= STK_OVERFL; /* Stack fault */
            return;
        }
        memory_cycle(3);        /* Read B */
        prev_addr(S);
    }
}

/* Make sure B is valid, don't care about A */
void B_valid_and_A() {
    if (!BROF) {
        if (NCSF && (S & 077700) == R) {
            Q |= STK_OVERFL; /* Stack fault */
            return;
        }
        memory_cycle(3);        /* Read B */
        prev_addr(S);
    }
}

/* Saves the top word on the stack into MA */
void save_tos() {
    if (AROF) {
        memory_cycle(014);      /* Store A in Ma */
        AROF = 0;
    } else if (BROF) {
        memory_cycle(015);      /* Store B in Ma */
        BROF = 0;
    } else {                    /* Fetch B then Store */
        A_valid();              /* Use A register since it is quicker */
        memory_cycle(014);
        AROF = 0;
    }
}

/* Enter a subroutine, flag true for descriptor, false for opdc */
void enterSubr(int flag) {
    /* Program descriptor */
    if ((A & ARGF) != 0 && MSFF == 0) {
        return;
    }
    if ((A & MODEF) != 0 && (A & ARGF) == 0) {
        return;
    }
    B_empty();
    /* Check if accidental entry */
    if ((A & ARGF) == 0) {
        B = MSCW;
        BROF = 1;
        B_empty();
        F = S;
    }
    B = RCW(flag);
    BROF = 1;
    B_empty();
    C = CF(A);
    L = 0;
    if ((A & ARGF) == 0) {
        F = FF(A);
    } else {
        F = S;
    }
    AROF = 0;
    BROF = 0;
    SALF = 1;
    MSFF = 0;
    PROF = 0;
    if (A & MODEF) {
       CWMF = 1;
       R = 0;
       X = toF(S);
       S = 0;
    }
}

/* Make B register into an integer, return 1 if failed */
int mkint() {
        int     exp_b;
        int     last_digit;
        int     f = 0;

        /* Extract exponent */
        exp_b = (B & EXPO) >> EXPO_V;
        if (exp_b == 0)
           return 0;
        if (B & ESIGN)
           exp_b = -exp_b;
        if (B & MSIGN)
           f = 1;
        B &= MANT;
        /* Adjust if exponent less then zero */
        last_digit = 0;
        if (exp_b < 0) {
            while (exp_b < 0 && B != 0) {
                last_digit = B & 7;
                B >>= 3;
                exp_b++;
            }
            if (exp_b != 0) {
                B = 0;
                return 0;
            }
            if (f ? (last_digit > 4) : (last_digit >= 4))
                B++;
        } else {
            /* Now handle when exponent plus */
            while(exp_b > 0) {
                if ((B & NORM) != 0)
                    return 1;
                B <<= 3;
                exp_b--;
            }
        }
        if (f && B != 0)
            B |= MSIGN;
        return 0;
}

/* Compute an index word return true if failed. */
int indexWord() {
    if (A & WCOUNT) {
        B_valid_and_A();
        if (mkint()) {
             if (NCSF)
                 Q |= INT_OVER;
             return 1;
        }
        if (B & MSIGN && (B & MANT) != 0) {
            if (NCSF)
                Q |= INDEX_ERROR;
            return 1;
        }
        if ((B & 01777) >= ((A & WCOUNT) >> WCOUNT_V)) {
            if (NCSF)
                Q |= INDEX_ERROR;
            return 1;
        }
        Ma = (A + (B & 01777)) & CORE;
        A &= ~(WCOUNT|CORE);
        A |= Ma;
        BROF = 0;
    } else {
        Ma = CF(A);
    }
    return 0;
}


/* Character mode helper routines */

/* Adjust source bit pointers to point to char */
void adjust_source() {
    if (GH & 07) {
        GH &= 070;
        GH += 010;
        if (GH > 077) {
            AROF = 0;
            GH = 0;
            next_addr(Ma);
        }
    }
}

/* Adjust destination bit pointers to point to char */
void adjust_dest() {
    if (KV & 07) {
        KV &= 070;
        KV += 010;
        if (KV > 075) {
        if (BROF)
           memory_cycle(013);
        BROF = 0;
        KV = 0;
        next_addr(S);
        }
    }
}

/* Advance to next destination bit/char */
void next_dest(int bit) {
    if (bit)
       KV += 1;
    else
       KV |= 7;
    if ((KV & 07) > 5) {
       KV &= 070;
       KV += 010;
    }
    if (KV > 075) {
       if (BROF)
          memory_cycle(013);
       BROF = 0;
       KV = 0;
       next_addr(S);
    }
}

/* Advance to previous destination bit/char */
void prev_dest(int bit) {
    if (bit) {
       if ((KV & 07) == 0) {
          if (KV == 0) {
             if (BROF)
                memory_cycle(013);
             BROF = 0;
         prev_addr(S);
         KV = 076;
          } else {
         KV = ((KV - 010) & 070) | 06;
          }
       }
       KV -= 1;
    } else {
       KV &= 070;
       if (KV == 0) {
          if (BROF)
             memory_cycle(013);
          BROF = 0;
          prev_addr(S);
          KV = 070;
       } else
          KV -= 010;
    }
}

/* Make sure destination have valid data */
void fill_dest() {
    if (BROF == 0) {
       memory_cycle(3);
       BROF = 1;
    }
}

/* Advance source to next bit/char */
void next_src(int bit) {
    if (bit)
       GH += 1;
    else
       GH |= 7;
    if ((GH & 07) > 5) {
       GH &= 070;
       GH += 010;
    }
    if (GH > 075) {
       AROF = 0;
       GH = 0;
       next_addr(Ma);
    }
}

/* Advance source to previous bit/char */
void prev_src(int bit) {
    if (bit) {
       if ((GH & 07) == 0) {
          if (GH == 0) {
             AROF = 0;
         prev_addr(Ma);
         GH = 076;
          } else {
         GH = ((GH - 010) & 070) | 06;
          }
       }
       GH -= 1;
    } else {
       GH &= 070;
       if (GH == 0) {
          AROF = 0;
          prev_addr(Ma);
          GH = 070;
       } else
          GH -= 010;
    }
}

/* Make sure source has valid data */
void fill_src() {
    if (AROF == 0) {
       memory_cycle(4);
       AROF = 1;
    }
}


/* Helper routines for managing processor */

/* Fetch next program sylable */
void next_prog() {
    if (!PROF)
        memory_cycle(020);
    T = (P >> ((3 - L) * 12)) & 07777;
    if ( L++ == 3) {
       C++;
       L = 0;
       PROF = 0;
    }
    TROF = 1;
}

/* Initiate a processor, A must contain the ICW */
void initiate() {
    int brflg, arflg, temp;

    set_via_INCW(A);    /* Set up Stack */
    AROF = 0;
    memory_cycle(3);    /* Fetch IRCW from stack */
    prev_addr(S);
    brflg = set_via_RCW(B, 0, 0);
    memory_cycle(3);    /* Fetch ICW from stack */
    prev_addr(S);
    set_via_ICW(B);
    BROF = 0;           /* Note memory_cycle set this */
    if (CWMF) {
        memory_cycle(3);        /* Fetch LCW from stack */
        prev_addr(S);
        arflg = (B & PRESENT) != 0;
        X = B & MANT;
        if (brflg) {
            memory_cycle(3);    /* Load B via S */
            prev_addr(S);
        }
        if (arflg)  {
            memory_cycle(2);    /* Load A via S */
            prev_addr(S);
        }
        AROF = arflg;
        BROF = brflg;
        temp = S;
        S = FF(X);
        X = replF(X, temp);
    }
    NCSF = 1;
    PROF = 0;
    TROF = 0;
}

/* Save processor state in case of error or halt */
void storeInterrupt(int forced, int test) {
    int         f;
    t_uint64    temp;

    if (forced || test)
        NCSF = 0;
    f = BROF;
    if (CWMF) {
        int i = AROF;
        temp = S;
        S = FF(X);
        X = replF(X, temp);
        if (AROF || test) {     /* Push A First */
             next_addr(S);
             memory_cycle(10);
        }
        if (BROF || test) {     /* Push B second */
             next_addr(S);
             memory_cycle(11);
        }
        /* Make ILCW */
        B = X | ((i)? PRESENT : 0) | FLAG | DFLAG;
        next_addr(S);     /* Save B */
        memory_cycle(11);
    } else {
        if (BROF || test) {     /* Push B First */
             next_addr(S);
             memory_cycle(11);
        }
        if (AROF || test) {     /* Push A Second */
             next_addr(S);
             memory_cycle(10);
        }
    }
    AROF = 0;
    B = ICW;            /* Set ICW into B */
    next_addr(S); /* Save B */
    memory_cycle(11);
    B = RCW(f);         /* Save IRCW */
    next_addr(S); /* Save B */
    memory_cycle(11);
    if (CWMF) {
        /* Get the correct value of R */
        Ma = F;
        memory_cycle(6);        /* Load B via Ma, Indirect */
        memory_cycle(5);        /* Load B via Ma */
        R = RF(B);
        B = FLAG|DFLAG|SCWMF|toC(S);
    } else {
        B = FLAG|DFLAG|toC(S);
    }
    B |= ((t_uint64)Q) << 35;
    Ma = R | 010;
    memory_cycle(015);  /* Store B in Ma */
    R = 0;
    BROF = 0;
    MSFF = 0;
    SALF = 0;
    F = S;
    if (forced || test)
        CWMF = 0;
    PROF = 0;
    if (test) {
        Ma = 0;
        memory_cycle(5);        /* Load location 0 to B. */
        BROF = 0;
        C = CF(B);
        L = 0;
        KV = 0;
        GH = 0;
    } else if (forced) {
        if (cpu_index) {
           P2_run = 0;          /* Clear halt flag */
           hltf[1] = 0;
           cpu_index = 0;
        } else {
           T = WMOP_ITI;
           TROF = 1;
        }
    }
}

/* Check if in idle loop.
   assume that current instruction is ITI */
/* Typical idle loop for MCP is:

  -1   ITI                               0211
  +0   TUS                               2431
  +1   OPDC  address1                    xxx2
  +2   LOR                               0215
  +3   OPDC  address2                    xxx2
  +4   NEQ                               0425
  +5   LITC  010          LITC 1         0040   0004
  +6   BBC                LBC            0131   2131

*/

int check_idle() {
    static uint16  loop_data[7] = {
          WMOP_TUS, WMOP_OPDC, WMOP_LOR, WMOP_OPDC,
          WMOP_NEQ, WMOP_LITC, WMOP_BBC };
    static uint16  loop_mask[7] = {
          07777,    00003,     07777,     00003,
          07777,    07733,     05777};
    t_uint64     data;
    uint16       addr = C;
    int          l = (3 - L) * 12;
    uint16       word;
    int          i;

    /* Quick check to see if not correct location */
    if (idle_addr != 0 && idle_addr != addr)
       return 0;
    /* If address same, then idle loop */
    if (idle_addr == addr)
       return 1;

    /* Not set, see if this could be loop */
    data = M[addr];
    for (i = 0; i < 7; i++) {
        word = (uint16)(data >> l) & 07777;
        if ((word & loop_mask[i]) != loop_data[i])
            return 0;
        if (l == 0) {
            addr++;
            l = 3 * 12;
            data = M[addr];
        } else {
            l -= 12;
        }
    }
    idle_addr = C;
    return 1;
}


/* Math helper routines. */

/* Compare A and B,
        return 1 if B = A.
        return 2 if B > A
        return 4 if B < A
*/
uint8   compare() {
    int         sign_a, sign_b;
    int         exp_a, exp_b;
    t_uint64    ma, mb;

    sign_a = (A & MSIGN) != 0;
    sign_b = (B & MSIGN) != 0;

    /* next grab exponents and mantissa */
    ma = A & MANT;
    mb = B & MANT;
    if (ma == 0) {
        if (mb == 0)
        return 1;
        return (sign_b ? 2 : 4);
    } else {
            /* Extract exponent */
        exp_a = (A & EXPO) >> EXPO_V;
        if (A & ESIGN)
           exp_a = -exp_a;
    }
    if (mb == 0) {
        return (sign_a ? 4 : 2);
        } else {
        exp_b = (B & EXPO) >> EXPO_V;
        if (B & ESIGN)
           exp_b = -exp_b;
    }

    /* If signs are different return differnce */
    if (sign_a != sign_b)
            return (sign_b ? 2 : 4);

    /* Normalize both */
    while((ma & NORM) == 0 && exp_a != exp_b) {
        ma <<= 3;
        exp_a--;
    }

    while((mb & NORM) == 0 && exp_a != exp_b) {
        mb <<= 3;
        exp_b--;
    }

    /* Check exponents first */
        if (exp_a != exp_b) {
        if (exp_b > exp_a) {
        return (sign_b ? 2 : 4);
        } else {
        return (sign_b ? 4 : 2);
        }
    }

    /* Exponents same, check mantissa */
    if (ma != mb) {
       if (mb > ma) {
          return (sign_b ? 2 : 4);
       } else if (mb != ma) {
          return (sign_b ? 4 : 2);
       }
    }

    /* Ok, must be identical */
    return 1;
}

/* Handle addition instruction.
   A & B valid. */
void add(int opcode) {
    int exp_a, exp_b;
    int sa, sb;
    int rnd;

    AB_valid();
    if (opcode == WMOP_SUB)     /* Subtract */
        A ^= MSIGN;
    AROF = 0;
    X = 0;
    /* Check if Either argument already zero */
    if ((A & MANT) == 0) {
       if ((B & MANT) == 0)
          B = 0;
       return;
    }
    if ((B & MANT) == 0) {
       B = A;
       return;
    }

    /* Extract exponent */
    exp_a = (A & EXPO) >> EXPO_V;
    exp_b = (B & EXPO) >> EXPO_V;
    if (A & ESIGN)
       exp_a = -exp_a;
    if (B & ESIGN)
       exp_b = -exp_b;
    /* Larger exponent to A */
    if (exp_b > exp_a) {
        t_uint64 temp;
        temp = A;
        A = B;
        B = temp;
        sa = exp_a;
        exp_a = exp_b;
        exp_b = sa;
    }
    /* Extract signs, clear upper bits */
    sa = (A & MSIGN) != 0;
    A &= MANT;
    sb = (B & MSIGN) != 0;
    B &= MANT;
    /* While the exponents are not equal, adjust */
    while(exp_a != exp_b && (A & NORM) == 0) {
        A <<= 3;
        exp_a--;
    }
    while(exp_a != exp_b && B != 0) {
        X |= (B & 07) << EXPO_V;
        X >>= 3;
        B >>= 3;
        exp_b++;
    }
    if (exp_a != exp_b) {
        exp_b = exp_a;
        B = 0;
        X = 0;
    }
    if (sa) {   /* A is negative. */
       A ^= FWORD;
       A++;
    }
    if (sb) {   /* B is negative */
       X ^= MANT;
       B ^= FWORD;
       X++;
       if (X & EXPO) {
              B++;
          X &= MANT;
       }
    }
    B = A + B;  /* Do final math. */
    if (B & MSIGN) {    /* Result is negative, switch */
       sb = 1;
       X ^= MANT;
       B ^= FWORD;
       X++;
       if (X & EXPO) {
              B++;
          X &= MANT;
       }
    } else
       sb = 0;
    if (B & EXPO) {     /* Handle overflow */
       rnd = B & 07;
       B >>= 3;
       exp_b++;
    } else if ((B & NORM) == 0) {
       if ((X & NORM) == 0) {
        rnd = 0;
       } else {
        X <<= 3;
        B <<= 3;
        B |= (X >> EXPO_V) & 07;
        X &= MANT;
            rnd = X >> (EXPO_V - 3);
            exp_b--;
       }
    } else {
       rnd = X >> (EXPO_V - 3);
    }
    if (rnd >= 4 && B != MANT) {
       B++;
    }

    B &= MANT;
    if ((exp_b != 0) && (exp_b < -64) && (B & NORM) == 0) {
        B <<= 3;
        exp_b--;
    }
    if (B == 0)
        return;
    if (exp_b < 0) {    /* Handle underflow */
       if (exp_b < -64 && NCSF)
            Q |= EXPO_UNDER;
       exp_b = ((-exp_b) & 077)|0100;
    } else {
       if (exp_b > 64 && NCSF)
           Q |= EXPO_OVER;
       exp_b &= 077;
    }
    B = (B & MANT) | ((t_uint64)(exp_b & 0177) << EXPO_V) |
        ((sb) ? MSIGN: 0);
}

/*
 * Perform a 40 bit multiply on A and B, result into B,X
 */
void mult_step(t_uint64 a, t_uint64 *b, t_uint64 *x) {
    t_uint64  u0,u1,v0,v1,t,w1,w2,w3,k;

    /* Split into 32 bit and 8 bit */
    u0 = a >> 32; u1 = a & 0xffffffff;
    v0 = *b >> 32; v1 = *b & 0xffffffff;
    /* Multiply lower halfs to 64 bits */
    t = u1*v1;
    /* Lower 32 bits to w3. */
    w3 = t & 0xffffffff;
    /* Upper 32 bits to k */
    k = t >> 32;
    /* Add in partial product of upper & lower */
    t = u0*v1 + k;
    w2 = t & 0xffffffff;
    w1 = t >> 32;
    t = u1*v0 + w2;
    k = t >> 32;
    /* Put result back together */
    *b = u0*v0 + w1 + k;
    *x = (t << 32) + w3;
    /* Put into 2 40 bit numbers */
    *b <<= 25;
    *b |= (*x >> EXPO_V);
    *x &= MANT;
}

/* Do multiply instruction */
void multiply() {
    int         exp_a, exp_b;
    int         f;
    int         int_f;

    AB_valid();
    AROF = 0;
    /* Check if Either argument already zero */
    if ((A & MANT) == 0 || (B & MANT) == 0) {
       B = 0;
       return;
    }

    /* Extract exponent */
    exp_a = (A & EXPO) >> EXPO_V;
    exp_b = (B & EXPO) >> EXPO_V;
    if (A & ESIGN)
       exp_a = -exp_a;
    if (B & ESIGN)
       exp_b = -exp_b;
    /* Extract signs, clear upper bits */
    f = (A & MSIGN) != 0;
    A &= MANT;
    f ^= ((B & MSIGN) != 0);
    B &= MANT;
    /* Flag if both exponents zero */
    int_f = (exp_a == 0) & (exp_b == 0);
    if (int_f == 0) {
        while ((A & NORM) == 0) {
            A <<= 3;
            exp_a--;
        }
        while ((B & NORM) == 0) {
            B <<= 3;
            exp_b--;
        }
    }

    mult_step(A, &B, &X);

    /* If integer and B is zero */
    if (int_f && B == 0) {
         B = X;
         X = 0;
         exp_b = 0;
    } else {
         exp_b = exp_a + exp_b + 13;
         while ((B & NORM) == 0) {
        if (exp_b < -64)
            break;
        B <<= 3;
        X <<= 3;
        B |= (X >> EXPO_V) & 07;
        X &= MANT;
        exp_b--;
         }
    }
    /* After B is normalize, check high order digit of X */
    if (X & ROUND) {
        B++;
        if (B & EXPO)  {
            B >>= 3;
            exp_b++;
        }
    }
    /* Check for over/underflow */
    if (exp_b < 0) {
        if (exp_b < -64) {
            if (NCSF)
                Q |= EXPO_UNDER;
            B = 0;
        return;
        }
        exp_b = ((-exp_b) & 077) | 0100;
    } else  {
        if (exp_b > 64) {
            if (NCSF)
                Q |= EXPO_OVER;
       }
       exp_b &= 077;
    }
    /* Put the pieces back together */
    B = (B & MANT) | ((t_uint64)(exp_b & 0177) << EXPO_V) | (f? MSIGN: 0);
}


/* Do divide instruction */
void divide(int op) {
    int exp_a, exp_b, q, sa, sb;
    t_uint64 t;

    AB_valid();
    AROF = 0;
    t = B;

    if ((A & MANT) == 0) {       /* if A mantissa is zero */
        if (NCSF)                 /* and we're in Normal State */
            Q |= DIV_ZERO;
        return;
    }

    if ((B & MANT) == 0) { /* otherwise, if B is zero, */
        A = B = 0;                /* result is all zeroes */
        return;
    }

    /* Extract exponent */
    exp_a = (A & EXPO) >> EXPO_V;
    exp_b = (B & EXPO) >> EXPO_V;
    if (A & ESIGN)
       exp_a = -exp_a;
    if (B & ESIGN)
       exp_b = -exp_b;

    /* Extract signs, clear upper bits */
    sb = (B & MSIGN) != 0;
    sa = (A & MSIGN) != 0;
    A &= MANT;
    B &= MANT;
    /* Normalize A and B */
    while ((A & NORM) == 0) {
        A <<= 3;
        exp_a--;
    }
    while ((B & NORM) == 0) {
        B <<= 3;
        exp_b--;
    }

    if (op != WMOP_DIV && exp_a > exp_b) { /* Divisor has greater magnitude */
         /* Quotent is < 1, so set result to zero */
         A = 0;
         B = (op == WMOP_RDV)? (t & FWORD) : 0;
         return;
    }

    if (op != WMOP_RDV)
         sb ^= sa;      /* positive if signs are same, negative if different */
    X = 0;
    /* Now we step through the development of the quotient one octade at a time,
       tallying the shifts in n until the high-order octade of X is non-zero
       (i.e., normalized). The divisor is in ma and the dividend (which becomes
       the remainder) is in mb. Since the operands are normalized, this will
       take either 13 or 14 shifts. We do the X shift at the top of the loop so
       that the 14th (rounding) digit will be available in q at the end.
       The initial shift has no effect, as it operates using zero values for X
       and q. */
    do {
        q = 0;                  /* initialize the quotient digit */
        while (B >= A) {
            ++q;                /* bump the quotient digit */
            B -= A;             /* subtract divisor from reAinder */
        }

        if (op == WMOP_DIV) {
            if ((X & NORM) != 0) {
                break;          /* quotient has become normalized */
            } else {
                B <<= 3;       /* shift the remainder left one octade */
                X = (X<<3) + (t_uint64)q;  /* shift quotient digit into the
                                        working quotient */
                --exp_b;
            }
        } else {
            X = (X<<3) + (t_uint64)q;  /* shift quotient digit into the
                                         working quotient */
            if ((X & NORM) != 0) {
                break;              /* quotient has become normalized */
            } else if (exp_a >= exp_b) {
                break;
            } else {
                B <<= 3;          /* shift the remainder left one octade */
                --exp_b;          /* decrement the B exponent */
            }
        }
    } while (1);

    if (op == WMOP_DIV) {
        exp_b -= exp_a - 1; /* compute the exponent, accounting for the shifts*/

        /* Round the result (it's already normalized) */
        if (q >= 4) {       /* if high-order bit of last quotient digit is 1 */
           if (X < MANT) {  /* if the rounding would not cause overflow */
               ++X;         /* round up the result */
           }
        }
    } else if (op == WMOP_IDV) {
        if (exp_a == exp_b) {
            exp_b = 0;              /* integer result developed */
        } else {
            if (NCSF)               /* integer overflow result */
               Q |= INT_OVER;
            exp_b -= exp_a;
        }
    } else {
        X = B;                     /* Result in B */
        if (exp_a == exp_b) {      /* integer result developed */
            if (X == 0)            /* if B mantissa is zero, then */
                exp_b = sb = 0;    /* assure result will be all zeroes */
        } else {
            if (NCSF)              /* integer overflow result */
                Q |= INT_OVER;
            X = exp_b = sb = 0;    /* result in B will be all zeroes */
        }
    }

    /* Check for exponent under/overflow */
    if (exp_b > 63) {
        exp_b &= 077;
        if (NCSF) {
            Q |= EXPO_OVER;
        }
    } else if (exp_b < 0) {
        if (exp_b < -63) {
            if (NCSF)
                Q |= EXPO_UNDER;
        }
        exp_b = ((-exp_b) & 077) | 0100;
    }

    /* Put the pieces back together */
    B = (X & MANT) | ((t_uint64)(exp_b & 0177) << EXPO_V) | (sb? MSIGN: 0);
}


/* Double precision addition.
   A & Y (not in real B5500) have operand 1.
   B & X have operand 2 */
void double_add(int opcode) {
    int         exp_a, exp_b;
    int         sa, sb;
    int         ld;
    t_uint64    temp;

    AB_valid();
    X = A;              /* Save registers. X = H, Y=L*/
    Y = B;
    AROF = BROF = 0;
    AB_valid(); /* Grab other operand */
    temp = A;   /* Oprands A,Y and B,X */
    A = X;
    X = B;
    B = temp;

    if (opcode == WMOP_DLS)     /* Double Precision Subtract */
       A ^= MSIGN;
    /* Extract exponent */
    exp_a = (A & EXPO) >> EXPO_V;
    exp_b = (B & EXPO) >> EXPO_V;
    if (A & ESIGN)
       exp_a = -exp_a;
    if (B & ESIGN)
       exp_b = -exp_b;
    /* Larger exponent to A */
    if (exp_b > exp_a) {
        t_uint64 temp;
        temp = A;
        A = B;
        B = temp;
        temp = Y;
        Y = X;
        X = temp;
        sa = exp_a;
        exp_a = exp_b;
        exp_b = sa;
    }
    /* Extract signs, clear upper bits */
    sa = (A & MSIGN) != 0;
    A &= MANT;
    Y &= MANT;
    sb = (B & MSIGN) != 0;
    B &= MANT;
    X &= MANT;
    ld = 0;
    /* While the exponents are not equal, adjust */
    while(exp_a != exp_b) {
        if ((A & NORM) == 0) {
            A <<= 3;
            Y <<= 3;
            A |= (Y >> EXPO_V) & 07;
            Y &= MANT;
            exp_a--;
         } else {
            X |= (B & 07) << EXPO_V;
        ld = (X & 07);
            X >>= 3;
            B >>= 3;
            exp_b++;
        if (B == 0 && X == 0)
           break;
         }
    }
    if (exp_a != exp_b) {
        exp_b = exp_a;
        B = 0;
        X = 0;
    }
    if (sa) {   /* A is negative. */
       Y ^= MANT;
       A ^= FWORD;
       Y++;
       if (Y & EXPO) {
            Y &= MANT;
        A++;
       }
    }
    if (sb) {   /* B is negative */
       X ^= MANT;
       B ^= FWORD;
       X++;
       if (X & EXPO) {
            X &= MANT;
        B++;
       }
    }
    X = Y + X;
    B = A + B;  /* Do final math. */
    if (X & EXPO) {
       B += X >> (EXPO_V);
       X &= MANT;
    }

    if (B & MSIGN) {    /* Result is negative, switch */
       sb = 1;
       X ^= MANT;
       B ^= FWORD;
       X++;
       if (X & EXPO) {
            X &= MANT;
        B++;
       }
    } else {
       sb = 0;
    }

    while (B & EXPO) {  /* Handle overflow */
       X |= (B & 07) << EXPO_V;
       ld = X & 07;
       B >>= 3;
       X >>= 3;
       exp_b++;
    }

    if (ld >= 4 && X != MANT && B != MANT) {
       X++;
       if (X & EXPO) {
           X &= MANT;
           B++;
       }
    }

    while(exp_b > -63 && (B & NORM) == 0) {
        B <<= 3;
        X <<= 3;
        B |= (X >> EXPO_V) & 07;
        X &= MANT;
        exp_b--;
    }

    B &= MANT;
    X &= MANT;
    if (exp_b < 0) {    /* Handle underflow */
       if (exp_b < -64 && NCSF)
        Q |= EXPO_UNDER;
       exp_b = ((-exp_b) & 077)|0100;
    } else {
       if (exp_b > 64 && NCSF)
        Q |= EXPO_OVER;
       exp_b &= 077;
    }
    A = (B & MANT) | ((t_uint64)(exp_b & 0177) << EXPO_V) |
        (sb ? MSIGN: 0);
    B = X;
}

/* Double precision multiply.
   A & Y (not in real B5500) have operand 1.
   B & X have operand 2 */
void double_mult() {
    int         exp_a, exp_b;
    int         f;
    int         ld;
    t_uint64    m7, m6;

    AB_valid();
    X = A;              /* Save registers. X = H, Y=L*/
    Y = B;
    AROF = BROF = 0;
    AB_valid(); /* Grab other operand */
    m7 = A;     /* Oprands A,Y and B,X */
    A = X;
    X = B;
    B = m7;

    /* Extract exponent */
    exp_a = (A & EXPO) >> EXPO_V;
    exp_b = (B & EXPO) >> EXPO_V;
    if (A & ESIGN)
       exp_a = -exp_a;
    if (B & ESIGN)
       exp_b = -exp_b;
    /* Extract signs, clear upper bits */
    f = (A & MSIGN) != 0;
    A &= MANT;
    Y &= MANT;
    f ^= ((B & MSIGN) != 0);
    B &= MANT;
    X &= MANT;

    /* Normalize the operands */
    for(ld = 0; (B & NORM) == 0 && ld < 13 ; ld++) {
        B <<= 3;
        B |= (X >> 36) & 07;
        X <<= 3;
        X &= MANT;
        exp_b--;
    }

    for(ld = 0; (A & NORM) == 0 && ld < 13 ; ld++) {
        A <<= 3;
        A |= (Y >> 36) & 07;
        Y <<= 3;
        Y &= MANT;
        exp_a--;
    }

    if ((X == 0 && B == 0) || (Y == 0 && A == 0)) {
        A = B = 0;
        return;
    }
    exp_b += exp_a + 13;
    /* A = M3, Y = m3 */
    /* B = M4, X = m4 */
    /* B * Y => M6(Y),m6(m6) */
    /* A * X => M7(X) m7(m7) */
    /* Add m6(m7) + m7(m6) save High order digit of m7 */
    /* X = M7(X) + M6(Y) */
    /* A * B => Mx(B),mx(m6) */
    /* Add M7 to mx => M8 + m8 */
    /* Add M6 to m8 => M9(M8) + m9 */
    /*    M6 m6 = M4 * m3 */
    mult_step(B, &Y, &m6);      /* Y = M6, m6 = m6 */
    /*    M7 m7 = (M3 * m4) */
    mult_step(A, &X, &m7);      /* X = M7, m7 = m7 */
    m6 += m7;
    ld = m6 >> (EXPO_V-3);      /* High order digit */
    /* M8 m8 = (M4 * M3) */
    mult_step(A, &B, &m6);      /* B = M8, m6 = m8 */
    /* M8 m8 = (M4 * M3) + M7 + M6 */
    m6 += X + Y;
    /* M9 m9 = M8 + (m8 + M6) */
    /* M10m10= M9 + m9 + (high order digit of m7) */
    A = B + (m6 >> EXPO_V);
    B = m6 & MANT;

    if ((A & EXPO) != 0) {
        ld = B&7;
        B |= (A & 07) << EXPO_V;
        B >>= 3;
        A >>= 3;
        exp_b ++;
    }
    if ((A & NORM) == 0) {
        A <<= 3;
        A |= (B >> 36) & 07;
        B <<= 3;
        B += ld;
        ld = 0;;
        B &= MANT;
        exp_b --;
    }
    if (ld >= 4 && A != MANT && B != MANT) {
       B++;
       if (B & EXPO) {
           B &= MANT;
           A++;
       }
    }

    if (exp_b < 0) {    /* Handle underflow */
       if (exp_b < -64 && NCSF)
            Q |= EXPO_UNDER;
       exp_b = ((-exp_b) & 077)|0100;
    } else {
       if (exp_b > 64 && NCSF)
           Q |= EXPO_OVER;
       exp_b &= 077;
    }
    A = (A & MANT) | ((t_uint64)(exp_b & 0177) << EXPO_V) |
        (f ? MSIGN: 0);
}

/* Double precision divide.
   A & Y (not in real B5500) have operand 1.
   B & X have operand 2 */
void double_divide() {
    int exp_a, exp_b;
    int f;
    int         n;
    int         q;
    t_uint64    Q1, q1;

    AB_valid();
    X = A;              /* Save registers. X = H, Y=L*/
    Y = B;
    AROF = BROF = 0;
    AB_valid(); /* Grab other operand */
    Q1 = A;     /* Oprands A,Y and B,X */
    A = X;
    X = B;
    B = Q1;

    /* Extract exponent */
    exp_a = (A & EXPO) >> EXPO_V;
    if (A & ESIGN)
       exp_a = -exp_a;
    /* Extract signs, clear upper bits */
    f = (A & MSIGN) != 0;
    A &= MANT;
    Y &= MANT;
    /* Normalize A */
    for(n = 0; (A & NORM) == 0 && n < 13 ; n++) {
        A <<= 3;
        A |= (Y >> 36) & 07;
        Y <<= 3;
        Y &= MANT;
        exp_a--;
    }

    /* Extract B */
    exp_b = (B & EXPO) >> EXPO_V;
    if (B & ESIGN)
       exp_b = -exp_b;
    f ^= ((B & MSIGN) != 0);
    B &= MANT;
    X &= MANT;
    for(n = 0; (B & NORM) == 0 && n < 13 ; n++) {
        B <<= 3;
        B |= (X >> 36) & 07;
        X <<= 3;
        X &= MANT;
        exp_b--;
    }

    /* Check for divisor of 0 */
    if ((B == 0) && (X == 0)) {
        A = 0;
        return;
    }

    /* Check for divide by 0 */
    if ((A == 0) && (Y == 0)) {
        if (NCSF)
            Q |= DIV_ZERO;
        A = B;
        B = X;
        return;
    }

    exp_b = exp_b - exp_a + 1;
    /* B,X = M4,m4   A,Y = M3,m3 */

    /* Divide M4,m4 by M3 resulting in Q1, R1 */
    do {
        q = 0;                  /* initialize the quotient digit */
        while (B >= A) {
            ++q;                /* bump the quotient digit */
            B -= A;             /* subtract divisor from reAinder */
        }

        B <<= 3;            /* shift the remainder left one octade */
        X = (X<<3) + (t_uint64)q;  /* shift quotient digit into the
                                      working quotient */
        --exp_b;
    n++;
    } while (n < 13);

    if (exp_b < 0) {    /* Handle underflow */
        if (exp_b < -64 && NCSF)
            Q |= EXPO_UNDER;
        exp_b = ((-exp_b) & 077)|0100;
    } else {
        if (exp_b > 64 && NCSF)
           Q |= EXPO_OVER;
        exp_b &= 077;
    }

    /* Save Q1 in x R1 in B */
    Q1 = (X & MANT) | ((t_uint64)(exp_b & 0177) << EXPO_V) |
                        (f ? MSIGN: 0);
    X = 0;
    /* Now divide R1 by M3 resulting in q1, R2 */
    /* A=M3, B=R1, X=q1, B=R2 */
    for (n = 0; n < 13; n++) {
        q = 0;                  /* initialize the quotient digit */
        while (B >= A) {
            ++q;                /* bump the quotient digit */
            B -= A;             /* subtract divisor from reAinder */
        }

        B <<= 3;            /* shift the remainder left one octade */
        X = (X<<3) + (t_uint64)q;  /* shift quotient digit into the
                         working quotient */
    }

    q1 = X;
    B = Y;
    Y = X;
    X = 0;
    /* Now divide m3 by M3 resulting in q2, R3 */
    /* q2 in X, R3 in B */
    for (n = 0; n < 13; n++) {
        q = 0;                  /* initialize the quotient digit */
        while (B >= A) {
            ++q;                /* bump the quotient digit */
            B -= A;             /* subtract divisor from reAinder */
        }

        B <<= 3;            /* shift the remainder left one octade */
        X = (X<<3) + (t_uint64)q;  /* shift quotient digit into the
                         working quotient */
    }

    if (X == 0) {
        A = Q1;
        B = q1;
    } else {
        /* Load in Q1,q1 into B */
        A = 01157777777777777;
        Y = MANT ^ X;   /* Load q2 into A */
        B = Q1;
        X = q1;
        double_mult();
    }
}

void relativeAddr(int store) {
    uint16    base = R;
    uint16    addr = (uint16)(A & 01777);

    if (SALF) {
       switch ((addr >> 7) & 7) {
       case 0:
       case 1:
       case 2:
       case 3:
       default:         /* To avoid compiler warnings */
              break;

       case 4:
       case 5:
          addr &= 0377;
          if (MSFF) {
               Ma = R+7;
               memory_cycle(4);
               base = FF(A);
          } else
               base = F;
          break;

       case 6:
          addr &= 0177;
          base = (store)? R : ((L) ? C : C-1);
          break;

       case 7:
          addr = -(addr & 0177);
          if (MSFF) {
               Ma = R+7;
               memory_cycle(4);
               base = FF(A);
          } else
               base = F;
          break;
       }
    }
    Ma = (base + addr) & CORE;
}

t_stat
sim_instr(void)
{
    t_stat              reason;
    t_uint64            temp = 0LL;
    uint16              atemp;
    uint8               opcode;
    uint8               field;
    int                 bit_a;
    int                 bit_b;
    int                 f;
    int                 i;
    int                 j;

    reason = 0;
    hltf[0] = 0;
    hltf[1] = 0;
    P1_run = 1;

    while (reason == 0) {       /* loop until halted */
        if (P1_run == 0)
            return SCPE_STOP;
        while (loading) {
            sim_interval = -1;
            reason = sim_process_event();
            if (reason != SCPE_OK) {
                 break; /* process */
            }
        }
        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason != SCPE_OK) {
                 break; /* process */
            }
        }

        if (sim_brk_summ) {
            if(sim_brk_test((C << 3) | L, SWMASK('E'))) {
                reason = SCPE_STOP;
                break;
            }

            if (sim_brk_test((c_reg[0] << 3) | l_reg[0],
                         SWMASK('A'))) {
                reason = SCPE_STOP;
                break;
            }

            if (sim_brk_test((c_reg[1] << 3) | l_reg[1],
                         SWMASK('B'))) {
                reason = SCPE_STOP;
                break;
            }
        }

        /* Toggle between two CPU's. */
        if (cpu_index == 0 && P2_run == 1) {
            cpu_index = 1;
            /* Check if interrupt pending. */
            if (TROF == 0 && NCSF && ((Q != 0) || HLTF))
                /* Force a SFI */
                storeInterrupt(1,0);
        } else {
            cpu_index = 0;

            /* Check if interrupt pending. */
            if (TROF == 0 && NCSF && ((Q != 0) ||
                                 (IAR != 0)))
                /* Force a SFI */
                storeInterrupt(1,0);
        }
        if (TROF == 0)
            next_prog();

crf_loop:
        opcode = T & 077;
        field = (T >> 6) & 077;
        TROF = 0;

        if (hst_lnt) {  /* history enabled? */
            /* Ignore idle loop when recording history */
                /* DCMCP XIII */
            /* if ((C & 077774) != 01140) { */
                /* DCMCP XV */
            /* if ((C & 077774) != 01254) { */
                /* TSMCP XV */
            /* if ((C & 077774) != 01324) { */
            hst_p = (hst_p + 1);        /* next entry */
            if (hst_p >= hst_lnt) {
                    hst_p = 0;
            }
            hst[hst_p].c = (C) | HIST_PC;
            hst[hst_p].op = T;
            hst[hst_p].s = S;
            hst[hst_p].f = F;
            hst[hst_p].r = R;
            hst[hst_p].ma = Ma;
            hst[hst_p].a_reg = A;
            hst[hst_p].b_reg = B;
            hst[hst_p].x_reg = X;
            hst[hst_p].gh = GH;
            hst[hst_p].kv = KV;
            hst[hst_p].l = L;
            hst[hst_p].q = Q;
            hst[hst_p].cpu = cpu_index;
            hst[hst_p].iar = IAR;
            hst[hst_p].flags = ((AROF)? F_AROF : 0) | \
                               ((BROF)? F_BROF : 0) | \
                               ((CWMF)? F_CWMF : 0) | \
                               ((NCSF)? F_NCSF : 0) | \
                               ((SALF)? F_SALF : 0) | \
                               ((MSFF)? F_MSFF : 0) | \
                               ((VARF)? F_VARF : 0);
             /* }  */
        }

        /* Check if Character or Word Mode */
        if (CWMF) {
            /* Character mode source word in A
                        addressed by M,G,H.
                              destination in B
                        addressed by S,K,V.
                R = tally.
                X = loop control.
                F = return control word
            */
            switch(opcode) {
            case CMOP_EXC:              /* EXIT char mode */
                if (BROF) {
                    memory_cycle(013);
                }
                S = F;
                AROF = 0;
                memory_cycle(3);        /* Load B from S */
                if ((B & FLAG) == 0) {
                    if (NCSF)
                        Q |= FLAG_BIT;
                    break;
                }
                f = set_via_RCW(B, (field & 1), 0);
                S = F;
                memory_cycle(3);        /* Load MSW from S to B */
                set_via_MSCW(B);
                prev_addr(S);
                CWMF = 0;
                if (MSFF && SALF) {
                     Ma = F;
                     do {
                        /* B = M[FIELD], Ma = B[FIELD]; */
                        memory_cycle(6);        /* Grab previous MCSW */
                     } while(B & SMSFF);
                     Ma = R | 7;
                     memory_cycle(015); /* Store B in Ma */
                }
                BROF = 0;
                X = 0;
                if ((field & 1) == 0)
                   PROF = 0;
                break;

            case CMOP_BSD:      /* Skip Bit Destiniation */
                if (BROF) {
                    memory_cycle(013);
                }
                while(field > 0) {
                    field--;
                    next_dest(1);
                }
                break;

            case CMOP_SRS:      /* Skip Reverse Source */
                adjust_source();
                while(field > 0) {
                    field--;
                    prev_src(0);
                }
                break;

            case CMOP_SFS:      /* Skip Forward Source */
                adjust_source();
                while(field > 0) {
                    field--;
                    next_src(0);
                }
                break;

            case CMOP_BSS:      /* SKip Bit Source */
                while(field > 0) {
                    field--;
                    next_src(1);
                }
                break;

            case CMOP_SFD:      /* Skip Forward Destination */
                adjust_dest();
                while(field > 0) {
                    field--;
                    next_dest(0);
                }
                break;

            case CMOP_SRD:      /* Skip Reverse Destination */
                adjust_dest();
                while(field > 0) {
                    field--;
                    prev_dest(0);
                }
                break;

            case CMOP_RSA:      /* Recall Source Address */
                Ma = (F - field) & CORE;
                memory_cycle(4);
                AROF = 0;
                if (A & FLAG) {
                    if ((A & PRESENT) == 0) {
                        if (NCSF)
                            Q |= PRES_BIT;
                        break;
                    }
                    GH = 0;
                } else {
                    GH = (A >> 12) & 070;
                }
                Ma = CF(A);
                break;

            case CMOP_RDA:      /* Recall Destination Address */
                if (BROF)
                    memory_cycle(013);
                S = (F - field) & CORE;
                memory_cycle(3);
                BROF = 0;
                if (B & FLAG) {
                    if ((B & PRESENT) == 0) {
                        if (NCSF)
                            Q |= PRES_BIT;
                        break;
                    }
                    KV = 0;
                } else {
                    KV = (B >> 12) & 070;
                }
                S = CF(B);
                break;

            case CMOP_RCA:      /* Recall Control Address */
                AROF = BROF;
                A = B;  /* Save B temporarly */
                atemp = S;      /* Save S */
                S = (F - field) & CORE;
                memory_cycle(3);        /* Load word in B */
                S = atemp;      /* Restore S */
                if (B & FLAG) {
                    if ((B & PRESENT) == 0) {
                        if (NCSF)
                            Q |= PRES_BIT;
                        break;
                    }
                    C = CF(B);
                    L = 0;
                } else {
                    C = CF(B);
                    L = LF(B) + 1;
                    if (L > 3) {
                        L = 0;
                        next_addr(C);
                    }
                }
                B = A;
                BROF = AROF;
                AROF = 0;
                PROF = 0;
                break;

            case CMOP_SED:      /* Set Destination Address */
                if (BROF)
                    memory_cycle(013);
                S = (F - field) & CORE;
                KV = 0;
                BROF = 0;
                break;

            case CMOP_SES:      /* Set Source Address */
                Ma = (F - field) & CORE;
                GH = 0;
                AROF = 0;
                break;

            case CMOP_TSA:      /* Transfer Source Address */
                if (BROF)
                    memory_cycle(013);
                BROF = 0;
                adjust_source();
                field = 3;
                while(field > 0) {
                    fill_src();
                    i = (A >> bit_number[GH | 07]) & 077;
                    B <<= 6;
                    B |= i;
                    next_src(0);
                    field--;
                }
                B &= FLAG|FWORD;
                GH = (uint8)((B >> 12) & 070);
                Ma = CF(B);
                break;

            case CMOP_TDA:      /* Transfer Destination Address */
                if (BROF)
                    memory_cycle(013);
                BROF = 0;
                adjust_dest();
                field = 3;
                temp = 0;
                while(field > 0) {
                    fill_dest();
                    i = (B >> bit_number[KV | 07]) & 077;
                    temp <<= 6;
                    temp |= i;
                    next_dest(0);
                    field--;
                }
                BROF = 0;
                KV = (uint8)((temp >> 12) & 070);
                S = CF(temp);
                break;

            case CMOP_SCA:      /* Store Control Address */
                A = B;
                AROF = BROF;
                B = toF(F) | toL(L) | toC(C);
                F = S;
                S = FF(B);
                S = (S - field) & CORE;
                memory_cycle(013);      /* Store B in S */
                S = F;
                F = FF(B);
                B = A;
                BROF = AROF;
                AROF = 0;
                break;

            case CMOP_SDA:      /* Store Destination Address */
                adjust_dest();
                A = B;
                AROF = BROF;
                B = ((t_uint64)(KV & 070) << (FFIELD_V - 3)) | toC(S);
                S = (F - field) & CORE;
                memory_cycle(013);      /* Store B in S */
                S = CF(B);
                B = A;
                BROF = AROF;
                AROF = 0;
                break;

            case CMOP_SSA:      /* Store Source Address */
                adjust_source();
                A = B;
                AROF = BROF;
                B = ((t_uint64)(GH & 070) << (FFIELD_V - 3)) | toC(Ma);
                Ma = (F - field) & CORE;
                memory_cycle(015);      /* Store B in Ma */
                Ma = CF(B);
                B = A;
                BROF = AROF;
                AROF = 0;
                break;

            case CMOP_TRW:      /* Transfer Words */
                if (BROF) {
                    memory_cycle(013);
                    BROF = 0;
                }
                if (GH != 0) {
                    next_addr(Ma);
                    GH = 0;
                    AROF = 0;
                }
                if (KV != 0) {
                    next_addr(S);
                    KV = 0;
                }
                while(field > 0) {
                    field--;
                    memory_cycle(4);
                    memory_cycle(012);
                    next_addr(Ma);
                    next_addr(S);
                }
                BROF = 0;
                AROF = 0;
                break;

            case CMOP_TEQ:      /* Test for Equal 24 */
            case CMOP_TNE:      /* Test for Not-Equal 25 */
            case CMOP_TEG:      /* Test for Greater Or Equal 26   */
            case CMOP_TGR:      /* Test For Greater 27 */
            case CMOP_TEL:      /* Test For Equal or Less 34 */
            case CMOP_TLS:      /* Test For Less 35 */
            case CMOP_TAN:      /* Test for Alphanumeric 36 */
                adjust_source();
                fill_src();
                i = rank[(A >> bit_number[GH | 07]) & 077];
                j = rank[field];
                if (i == j)
                    f = 1;
                else if (i < j)
                    f = 2;
                else
                    f = 4;
                switch(opcode) {
                case CMOP_TEQ:  /* Test for Equal 24 */
                        TFFF = (f == 1);  break;
                case CMOP_TNE:  /* Test for Not-Equal 25 */
                        TFFF = (f != 1);  break;
                case CMOP_TEG:  /* Test for Greater Or Equal 26   */
                        TFFF = ((f & 5) != 0); break;
                case CMOP_TGR:  /* Test For Greater 27 */
                        TFFF = (f == 4); break;
                case CMOP_TEL:  /* Test For Equal or Less 34 */
                        TFFF = ((f & 3) != 0); break;
                case CMOP_TLS:  /* Test For Less 35 */
                        TFFF = (f == 2); break;
                case CMOP_TAN:  /* Test for Alphanumeric 36 */
                        if (f & 4) {
                            TFFF = !((i == 34) | (i == 44));
                        } else {
                            TFFF = f & 1;
                        }
                        break;
                }
                break;

            case CMOP_BIS:      /* Set Bit */
            case CMOP_BIR:      /* Reet Bit */
                while(field > 0) {
                     field--;
                     fill_dest();
                     temp = bit_mask[bit_number[KV]];
                     if (opcode & 1)
                        B &= ~temp;
                     else
                        B |= temp;
                     next_dest(1);
                }
                break;

            case CMOP_BIT:      /* Test Bit */
                fill_src();
                i = (A >> bit_number[GH]) & 01;
                TFFF = (i == (field & 1));
                break;

            case CMOP_INC:      /* Increase Tally */
                R = (R + (field<<6)) & 07700;
                break;

            case CMOP_STC:      /* Store Tally */
                if (BROF)
                    memory_cycle(11);
                AROF = 0;
                BROF = 0;
                A = toC(F);
                B = R >> 6;
                F = S;
                S = CF(A);
                S = (S - field) & CORE;
                memory_cycle(11);
                S = F;
                F = CF(A);
                break;

            case CMOP_SEC:      /* Set Tally */
                R = T & 07700;
                break;

            case CMOP_CRF:      /* Call repeat Field */
                /* Save B in A */
                AROF = BROF;
                A = B;
                /* Save F in B */
                atemp = F;
                /* Exchange S & F */
                F = S;
                /* Decrement S */
                S = (atemp - field) & CORE;
                /* Read value to B, S <= F, F <= B */
                memory_cycle(3);
                /* field = B & 077 */
                field = B & 077;
                /* Restore B */
                S = F;
                F = atemp;
                B = A;
                BROF = AROF;
                AROF = 0;
                /* fetch_next; */
                next_prog();
                /* If field == 0, opcode |= 4; field = T */
                if (field == 0) {
                    T &= 07700;
                    T |= CMOP_JFW;
                } else {
                    T &= 077;
                    T |= field << 6;
                }
                /* Goto crf_loop */
                goto crf_loop;

            case CMOP_JNC:      /* Jump Out Of Loop Conditional */
                if (TFFF)
                   break;

                /* Fall through */
            case CMOP_JNS:      /* Jump out of loop unconditional */
                /* Read Loop/Return control word */
                atemp = S;
                S = FF(X);
                memory_cycle(2);        /* Load S to A */
                AROF = 0;
                X = (A & MANT);
                S = atemp;
                if (field > 0) {
                    i = (C << 2) | L;   /* Make into syllable pointer */
                    i += field;
                    L = i & 3;          /* Convert back to pointer */
                    C = (i >> 2) & CORE;
                    PROF = 0;
                }
                break;

            case CMOP_JFC:      /* Jump Forward Conditional */
            case CMOP_JRC:      /* Jump Reverse Conditional */
                if (TFFF != 0)
                   break;

                /* Fall through */
            case CMOP_JFW:      /* Jump Forward Unconditional */
            case CMOP_JRV:      /* Jump Reverse Unconditional */
                 i = (C << 2) | L;   /* Make into syllable pointer */
                 if (opcode & 010) {    /* Forward */
                     i -= field;
                 } else {               /* Backward */
                     i += field;
                 }
                 L = i & 3;             /* Convert back to pointer */
                 C = (i >> 2) & CORE;
                 PROF = 0;
                 break;

            case CMOP_ENS:      /* End Loop */
                A = B;
                AROF = BROF;
                B = X;
                field = (uint8)((B & REPFLD) >> REPFLD_V);
                if (field) {
                     X &= ~REPFLD;
                     X |= ((t_uint64)(field - 1) << REPFLD_V) & REPFLD;
                     L = LF(B);
                     C = CF(B);
                     PROF = 0;
                     memory_cycle(020);
                } else {
                     atemp = S;
                     S = FF(X);
                     memory_cycle(3);   /* Load B */
                     X = B & MANT;
                     S = atemp;
                }
                B = A;
                BROF = AROF;
                AROF = 0;
                break;

            case CMOP_BNS:      /* Begin Loop */
                A = B;  /* Save B */
                AROF = BROF;
                B = X | FLAG | DFLAG;
                if (field != 0)
                   field--;
                atemp = S;
                S = FF(B);
                next_addr(S);
                memory_cycle(013);
                X = LCW(S, field);
                S = atemp;
                B = A;
                BROF = AROF;
                AROF = 0;
                break;

            case CMOP_OCV:      /* Output Convert */
                adjust_dest();
                if (BROF) {
                   memory_cycle(013);
                   BROF = 0;
                }
                /* Adjust source to word boundry */
                if (GH) {
                    GH = 0;
                    next_addr(Ma);
                    AROF = 0;
                }

                if (field == 0)
                   break;

                /* Load word into A */
                fill_src();
                next_addr(Ma);
                AROF = 0;
                B = 0;
                temp = 0;
                f = (A & MSIGN) != 0;
                TFFF = 1;
                A &= MANT;
                if (A == 0)
                    f = 0;
                i = 39;
                /* We loop over the bit in A and add one to B
                   each time we have the msb of A set. For each
                   step we BCD double the number in B */
                while(i > 0) {
                    /* Compute carry to next digit */
                    temp = (B + 0x33333333LL) & 0x88888888LL;
                    B <<= 1;    /* Double it */
                    /* Add 6 from every digit that overflowed */
                    temp = (temp >> 1) | (temp >> 2);
                    B += temp;
                    /* Lastly Add in new digit */
                    j = (A & ROUND) != 0;
                    A &= ~ROUND;
                    B += (t_uint64)j;
                    A <<= 1;
                    i--;
                }
                A = B;
                field = field & 07;
                if (field == 0)
                    field = 8;
                /* Now we put each digit into the destination string */
                for(i = 8; i >= 0; i--) {
                    j = (A >> (i * 4)) & 0xF;
                    if (i >= field) {
                        if (j != 0)
                            TFFF = 0;
                    } else {
                        fill_dest();
                        temp = 077LL << bit_number[KV | 07];
                        B &= ~temp;
                        if (i == 0 && f)
                            j |= 040;
                        B |= ((t_uint64)j) << bit_number[KV | 07];
                        BROF = 1;
                        next_dest(0);
                    }
                }
                break;

            case CMOP_ICV:      /* Input Convert */
                adjust_source();
                if (BROF) {
                   memory_cycle(013);
                   BROF = 0;
                }
                /* Align result to word boundry */
                if (KV) {
                   KV = 0;
                   next_addr(S);
                }

                if (field == 0)
                   break;
                B = 0;
                temp = 0;
                f = 0;
                /* Collect the source field into a string of BCD digits */
                while(field > 0) {
                   fill_src();
                   i = (int)(A >> bit_number[GH | 07]);
                   B = (B << 4) | (i & 017);
                   f = (i & 060) == 040;        /* Keep sign */
                   field = (field - 1) & 07;
                   next_src(0);
                }
                /* We loop over the BCD number in B, dividing it by 2
                   each cycle, while shifting the lsb into the top of
                   the A register */
                field = 28;
                A = 0;
                while(field > 0) {
                   A >>= 1;
                   if (B & 1)
                        A |= ((t_uint64)1) << 27;
                   /* BCD divide by 2 */
                   temp = B & 0x0011111110LL;
                   temp = (temp >> 4) | (temp >> 3);
                   B = (B >> 1) - temp;
                   field--;
                }
                if (f && A != 0)
                   A |= MSIGN;
                memory_cycle(012);
                AROF = 0;
                next_addr(S);
                break;

            case CMOP_CEQ:      /* Compare Equal 60 */
            case CMOP_CNE:      /* COmpare for Not Equal 61 */
            case CMOP_CEG:      /* Compare For Greater Or Equal 62 */
            case CMOP_CGR:      /* Compare For Greater 63 */
            case CMOP_CEL:      /* Compare For Equal or Less 70 */
            case CMOP_CLS:      /* Compare for Less 71 */
            case CMOP_FSU:      /* Field Subtract 72 */
            case CMOP_FAD:      /* Field Add 73 */
                adjust_source();
                adjust_dest();
                TFFF = 1;       /* flag to show greater */
                f = 1;          /* Still comparaing */
                while(field > 0) {
                    fill_src();
                    fill_dest();
                    if (f) {
                        i = (A >> bit_number[GH | 07]) & 077;
                        j = (B >> bit_number[KV | 07]) & 077;
                        if (i != j) {
                            switch(opcode) {
                            case CMOP_FSU:      /* Field Subtract */
                            case CMOP_FAD:      /* Field Add */
                                    /* Do numeric compare */
                                    i &= 017;
                                    j &= 017;
                                    if (i != j)
                                        f = 0;  /* Different, so stop check */
                                    if (i < j)
                                        TFFF = 0;
                                    break;
                            case CMOP_CEQ:      /* Compare Equal 60 */
                            case CMOP_CNE:      /* Compare for Not Equal 61 */
                            case CMOP_CEG:      /* Compare For Greater Or Equal 62 */
                            case CMOP_CGR:      /* Compare For Greater 63 */
                            case CMOP_CEL:      /* Compare For Equal or Less 70 */
                            case CMOP_CLS:      /* Compare for Less 71 */
                                    f = 0;      /* No need to continue; */
                                    if (rank[i] < rank[j])
                                        TFFF = 0;
                                    break;
                            }
                        }
                    }
                    next_src(0);
                    next_dest(0);
                    field--;
                }
                /* If F = 1, fields are equal.
                   If F = 0 and TFFF = 0 S < D.
                   If F = 0 and TFFF = 1 S > D.
                */
                switch(opcode) {
                case CMOP_FSU:  /* Field Subtract */
                case CMOP_FAD:  /* Field Add */
                    {
                    int ss, sd, sub;
                    int c;
                    /* Back up one location */
                    prev_src(0);
                    prev_dest(0);
                    fill_src();
                    fill_dest();
                    field = (T >> 6) & 077;
                    i = (A >> bit_number[GH | 07]) & 077;
                    j = (B >> bit_number[KV | 07]) & 077;
                    /* Compute Sign */
                    ss = (i & 060) == 040;      /* S */
                    sd = (j & 060) == 040;      /* D */
                    sub = (ss == sd) ^ (opcode == CMOP_FAD);
                /*
                        ss      sd      sub     f=1     TFFF=0  TFFF=1
                add     0       0       0       0       0       0
                add     1       0       1       0       0       1
                add     0       1       1       0       1       0
                add     1       1       0       1       1       1
                sub     0       0       1       0       0       1
                sub     1       0       0       0       0       0
                sub     0       1       0       1       1       1
                sub     1       1       1       0       1       0
                */
                    f = (f & sd & ss & (!sub)) |
                        (f & sd & (!ss) & (!sub))  |
                        ((!f) & (!TFFF) & sd) |
                        ((!f) & TFFF & (ss ^ (opcode == CMOP_FSU)));
                    c = 0;
                    if (sub) {
                        c = 1;
                        if (TFFF) { /* If S < D */
                            ss = 0;
                            sd = 1;
                        } else {
                            ss = 1;
                            sd = 0;
                        }
                    } else {
                        sd = ss = 0;
                    }
                    /* Do the bulk of adding. */
                    i &= 017;
                    j &= 017;
                    while (field > 0) {
                        i = (ss ? 9-i : i ) + (sd ? 9-j : j) + c;
                        if (i < 10) {
                            c = 0;
                        } else {
                            c = 1;
                            i -= 10;
                        }
                        if (f) {
                            i += 040;
                            f = 0;
                        }
                        temp = 077LL << bit_number[KV | 07];
                        B &= ~temp;
                        B |= ((t_uint64)i) << bit_number[KV | 07];
                        prev_src(0);
                        prev_dest(0);
                        fill_src();
                        fill_dest();
                        i = (A >> bit_number[GH | 07]) & 017;
                        j = (B >> bit_number[KV | 07]) & 017;
                        field--;
                    }
                    /* Set overflow flag */
                    TFFF = sub ^ c;
                    }
                    /* Lastly back to end of field. */
                    field = (T >> 6) & 077;
                    next_src(0);
                    next_dest(0);
                    while (field > 0) {
                        next_src(0);
                        next_dest(0);
                        field--;
                    }
                    break;
                case CMOP_CEQ:  /* Compare Equal 60 */
                    TFFF = f;
                    break;
                case CMOP_CNE:  /* Compare for Not Equal 61 */
                    TFFF = !f;
                    break;
                case CMOP_CEG:  /* Compare For Greater Or Equal 62 */
                    TFFF |= f;
                    break;
                case CMOP_CGR:  /* Compare For Greater 63 */
                    TFFF &= !f;
                    break;
                case CMOP_CEL:  /* Compare For Equal or Less 70 */
                    TFFF = !TFFF;
                    TFFF |= f;
                    break;
                case CMOP_CLS:  /* Compare for Less 71 */
                    TFFF = !TFFF;
                    TFFF &= !f;
                    break;
                }
                break;

            case CMOP_TRP:      /* Transfer Program Characters 74 */
                adjust_dest();
                while(field > 0) {
                   fill_dest();
                   if (!TROF)
                       next_prog();
                   if (field & 1) {
                       i = T & 077;
                       TROF = 0;
                   } else {
                       i = (T >> 6) & 077;
                   }
                   temp = 077LL << bit_number[KV | 07];
                   B &= ~temp;
                   B |= ((t_uint64)i) << bit_number[KV | 07];
                   next_dest(0);
                   field--;
                }
                TROF = 0;
                break;

            case CMOP_TRN:      /* Transfer Numeric 75 */
            case CMOP_TRZ:      /* Transfer Zones 76 */
            case CMOP_TRS:      /* Transfer Source Characters 77 */
                adjust_source();
                adjust_dest();
                while(field > 0) {
                   fill_dest();
                   fill_src();
                   i = (int)(A >> bit_number[GH | 07]);
                   if (opcode == CMOP_TRS) {
                        i &= 077;
                        temp = 077LL << bit_number[KV | 07];
                   } else if (opcode == CMOP_TRN) {
                        if (field == 1) {
                            if ((i & 060) == 040)
                                TFFF = 1;
                            else
                                TFFF = 0;
                        }
                        i &= 017;
                        temp = 077LL << bit_number[KV | 07];
                   } else {
                        i &= 060;
                        temp = 060LL << bit_number[KV | 07];
                   }
                   B &= ~temp;
                   B |= ((t_uint64)i) << bit_number[KV | 07];
                   next_src(0);
                   next_dest(0);
                   field--;
                }
                break;

            case CMOP_TBN:      /* Transfer Blanks for Non-Numerics 12 */
                adjust_dest();
                TFFF = 1;
                while(field > 0) {
                   fill_dest();
                   i = (B >> bit_number[KV | 07]) & 077;
                   if (i > 0 && i <= 9) {
                        TFFF = 0;
                        break;
                   }
                   B &= ~(077LL << bit_number[KV | 07]);
                   B |= 060LL << bit_number[KV | 07];
                   next_dest(0);
                   field--;
                }
                break;

            case 0011:
                goto control;
            }
        } else {
        /* Word mode opcodes */
            switch(opcode & 03) {
            case WMOP_LITC:             /* Load literal */
                A_empty();
                A = toC(T >> 2);
                AROF = 1;
                break;

            case WMOP_OPDC:             /* Load operand */
                A_empty();
                A = toC(T >> 2);
                relativeAddr(0);
                memory_cycle(4);
                SALF |= VARF;
                VARF = 0;
opdc:
                if (A & FLAG) {
                    /* Check if it is a control word. */
                    if ((A & DFLAG) != 0 && (A & PROGF) == 0) {
                        break;
                    }
                    /* Check if descriptor present */
                    if ((A & PRESENT) == 0) {
                        if (NCSF)
                            Q |= PRES_BIT;
                        break;
                    }
                    /* Program Descriptor */
                    if ((A & (DFLAG|PROGF)) == (DFLAG|PROGF)) {
                        enterSubr(0);
                    } else {
                        if (indexWord())
                           break;
                        memory_cycle(4);
                        if (NCSF && (A & FLAG))
                            Q |= FLAG_BIT;
                    }
                }
                break;

            case WMOP_DESC:             /* Load Descriptor */
                A_empty();
                A = toC(T >> 2);
                relativeAddr(0);
                memory_cycle(4);
                SALF |= VARF;
                VARF = 0;
desc:
                if (A & FLAG) {
                    /* Check if it is a control word. */
                    if ((A & DFLAG) != 0 && (A & PROGF) == 0) {
                        A = FLAG | PRESENT | toC(Ma);
                        break;
                    }
                    /* Check if descriptor present */
                    if ((A & PRESENT) == 0) {
                        if (NCSF)
                            Q |= PRES_BIT;
                        break;
                    }
                    /* Data descriptor */
                    if ((A & (DFLAG|PROGF)) == (DFLAG|PROGF)) {
                        enterSubr(1);
                    } else {
                        if (indexWord())
                           break;
                        A |= FLAG | PRESENT;
                    }
                } else {
                    A = FLAG | PRESENT | toC(Ma);
                }
                break;

            case WMOP_OPR:              /* All other operators */
            switch(opcode) {
            case 0001:
                switch(field) {
                case VARIANT(WMOP_SUB): /* Subtract */
                case VARIANT(WMOP_ADD): /* Add */
                        add(T);
                        break;
                case VARIANT(WMOP_MUL): /* Multiply */
                        multiply();
                        break;
                case VARIANT(WMOP_DIV): /* Divide */
                case VARIANT(WMOP_IDV): /* Integer Divide Integer */
                case VARIANT(WMOP_RDV): /* Remainder Divide */
                        divide(T);
                        break;
                }
                break;

            case 0005:
                switch(field) {
                case VARIANT(WMOP_DLS): /* Double Precision Subtract */
                case VARIANT(WMOP_DLA): /* Double Precision Add */
                        double_add(T);
                        break;
                case VARIANT(WMOP_DLM): /* Double Precision Multiply */
                        double_mult();
                        break;
                case VARIANT(WMOP_DLD): /* Double Precision Divide */
                        double_divide();
                        break;
                }
                break;

            case 0011:
                /* Control functions same as Character mode,
                    although most operators do not make sense in
                    Character mode. */
control:
                switch(field) {
                /* Different in Character mode */
                case VARIANT(WMOP_SFT): /* Store for Test */
                        storeInterrupt(0,1);
                        break;

                case VARIANT(WMOP_SFI): /* Store for Interrupt */
                        storeInterrupt(0,0);
                        break;

                case VARIANT(WMOP_ITI): /* Interrogate interrupt */
                        if (NCSF)       /* Nop in normal state */
                            break;

                        if (q_reg[0] & MEM_PARITY) {
                            C = PARITY_ERR;
                            q_reg[0] &= ~MEM_PARITY;
                        } else if (q_reg[0] & INVALID_ADDR) {
                            C = INVADR_ERR;
                            q_reg[0] &= ~INVALID_ADDR;
                        } else if (IAR) {
                            uint16  x;
                            C = INTER_TIME;
                            for(x = 1; (IAR & x) == 0; x <<= 1)
                                C++;
                            /* Release the channel after we got it */
                            if (C >= IO1_FINISH && C <= IO4_FINISH)
                                chan_release(C - IO1_FINISH);
                            IAR &= ~x;
                        } else if ((q_reg[0] & 0170) != 0) {
                            C = 060 + (q_reg[0] >> 3);
                            q_reg[0] &= 07;
                        } else if (q_reg[0] & STK_OVERFL) {
                            C = STK_OVR_LOC;
                            q_reg[0] &= ~STK_OVERFL;
                        } else if (P2_run == 0 && q_reg[1] != 0) {
                            if (q_reg[1] & MEM_PARITY) {
                                C = PARITY_ERR2;
                                q_reg[1] &= ~MEM_PARITY;
                            } else if (q_reg[1] & INVALID_ADDR) {
                                C = INVADR_ERR2;
                                q_reg[1] &= ~INVALID_ADDR;
                            } else if ((q_reg[1] & 0170) != 0) {
                                C = 040 + (q_reg[1] >> 3);
                                q_reg[1] &= 07;
                            } else if (q_reg[1] & STK_OVERFL) {
                                C = STK_OVR_LOC2;
                                q_reg[1] &= ~STK_OVERFL;
                            }
                        } else {
                             /* Could be an idle loop, if P2 running, continue */
                             if (P2_run)
                                 break;
                             if (sim_idle_enab) {
                             /* Check if possible idle loop */
                                 if (check_idle())
                                    sim_idle (TMR_RTC, FALSE);
                             }
                             break;
                        }
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "IAR=%05o Q=%03o\n\r",
                                         IAR,Q);
                        L = 0;
                        S = 0100;
                        CWMF = 0;
                        PROF = 0;
                        break;

                case VARIANT(WMOP_IOR): /* I/O Release */
                        if (NCSF)       /* Nop in normal state */
                            break;

                        /* Fall through */
                case VARIANT(WMOP_PRL): /* Program Release */
                        A_valid();
                        if ((A & FLAG) == 0) {
                            relativeAddr(1);
                        } else if (A & PRESENT) {
                            Ma = CF(A);
                        } else {
                            if (NCSF)
                                Q |= PRES_BIT;
                            break;
                        }
                        memory_cycle(4);        /* Read Ma to A */
                        if (NCSF) {             /* Can't occur for IOR */
                            Q |= (A & CONTIN) ? CONT_BIT : PROG_REL;
                            A = toC(Ma);
                            Ma = R | 9;
                        } else {
                            if (field == VARIANT(WMOP_PRL))
                                A &= ~PRESENT;
                            else
                                A |= PRESENT;
                        }
                        memory_cycle(014);      /* Store A to Ma */
                        AROF = 0;
                        break;

                case VARIANT(WMOP_RTR): /* Read Timer */
                        if (!NCSF) {
                            A_empty();
                            A = RTC;
                            if (IAR & IRQ_0)
                               A |= 0100;
                            AROF = 1;
                        }
                        break;

                case VARIANT(WMOP_COM): /* Communication operator */
                        if (NCSF) {
                            Ma = R|9;
                            save_tos();
                            Q |= COM_OPR;
                        }
                        break;

                case VARIANT(WMOP_ZP1): /* Conditional Halt */
                        if (NCSF)
                           break;
                        if (!HLTF)
                           break;
                        if (!HALT)
                           break;
                        hltf[0] = 1;
                        P1_run = 0;
                        break;

                case VARIANT(WMOP_HP2): /* Halt P2 */
                        /* Control state only */
                        if (NCSF)
                           break;
                        /* If CPU 2 is not running, or disabled nop */
                        if (P2_run == 0 || (cpu_unit[1].flags & UNIT_DIS)) {
                            break;
                        }
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "HALT P2\n\r");
                        /* Flag P2 to stop */
                        hltf[1] = 1;
                        TROF = 1;       /* Reissue until CPU2 stopped */
                        break;

                case VARIANT(WMOP_IP1): /* Initiate P1 */
                        if (NCSF)
                           break;
                        A_valid();      /* Load ICW */
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "INIT P1\n\r");
                        initiate();
                        break;

                case VARIANT(WMOP_IP2): /* Initiate P2 */
                        if (NCSF)
                           break;
                        Ma = 010;
                        save_tos();
                        /* If CPU is operating, or disabled, return busy */
                        if (P2_run != 0 || (cpu_unit[1].flags & UNIT_DIS)) {
                            IAR |= IRQ_11;      /* Set CPU 2 Busy */
                            break;
                        }
                        /* Ok we are going to initiate B.
                           load the initiate word from 010. */
                        hltf[1] = 0;
                        P2_run = 1;
                        cpu_index = 1;  /* To CPU 2 */
                        Ma = 010;
                        memory_cycle(4);
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "INIT P2\n\r");
                        initiate();
                        break;

                case VARIANT(WMOP_IIO): /* Initiate I/O */
                        if (NCSF)
                           break;
                        Ma = 010;
                        save_tos();
                        start_io();             /* Start an I/O channel */
                        break;

                        /* Currently not implimented. */
                case VARIANT(WMOP_IFT): /* Test Initiate */
                        /* Supports the ablity to start operand during any
                           cycle, since this simulator does not function the
                           same as the original hardware, this function can't
                           really be implemented.
                           It is currently only used in diagnostics. */
                        /* Halt execution if this instruction attempted */
                        reason = SCPE_NOFNC;
                        break;
                }
                break;

            case 0015:
                switch(field) {
                case VARIANT(WMOP_LNG): /* Logical Negate */
                        A_valid();
                        A = (A ^ FWORD);
                        break;

                case VARIANT(WMOP_LOR): /* Logical Or */
                        AB_valid();
                        A = (A & FWORD) | B;
                        BROF = 0;
                        break;

                case VARIANT(WMOP_LND): /* Logical And */
                        AB_valid();
                        A = (A & B & FWORD) | (B & FLAG);
                        BROF = 0;
                        break;

                case VARIANT(WMOP_LQV): /* Logical Equivalence */
                        AB_valid();
                        B = ((~(A ^ B) & FWORD)) | (B & FLAG);
                        AROF = 0;
                        break;

                case VARIANT(WMOP_MOP): /* Reset Flag bit */
                        A_valid();
                        A &= ~FLAG;
                        break;

                case VARIANT(WMOP_MDS): /* Set Flag Bit */
                        A_valid();
                        A |= FLAG;
                        break;
                }
                break;

            case 0021:
                switch(field) {
                case VARIANT(WMOP_CID): /* 01 Conditional Integer Store Destructive */
                case VARIANT(WMOP_CIN): /* 02 Conditional Integer Store non-destructive */
                case VARIANT(WMOP_ISD): /* 41 Interger Store Destructive */
                case VARIANT(WMOP_ISN): /* 42 Integer Store Non-Destructive */
                case VARIANT(WMOP_STD): /* 04 B Store Destructive */
                case VARIANT(WMOP_SND): /* 10 B Store Non-destructive */
                        AB_valid();
                        if (A & FLAG) {
                            if ((A & PRESENT) == 0) {
                                if (NCSF)
                                    Q |= PRES_BIT;
                                break;
                            }
                            Ma = CF(A);
                        } else {
                            relativeAddr(1);
                        }
                        SALF |= VARF;
                        VARF = 0;
                        if ((field & 03) != 0) {        /* Integer store */
                            if ((B & EXPO) != 0) {
                                /* Check if force to integer */
                                if ((A & INTEGR) != 0 || (field & 040) != 0) {
                                   if (mkint()) {
                                      /* Fail if not an integer */
                                      if (NCSF)
                                          Q |= INT_OVER;
                                      break;
                                   }
                                }
                            }
                        }
                        AROF = 0;
                        memory_cycle(015);      /* Store B in Ma */
                        if (field & 5)          /* Destructive store */
                           BROF = 0;
                        break;

                case VARIANT(WMOP_LOD): /* Load */
                        A_valid();
                        if (A & FLAG) {
                            if ((A & PRESENT) == 0) {
                                if (NCSF)
                                    Q |= PRES_BIT;
                                break;
                            }
                            Ma = CF(A);
                        } else {
                            relativeAddr(0);
                        }
                        SALF |= VARF;
                        VARF = 0;
                        memory_cycle(4);        /* Read Ma to A */
                        break;
                }
                break;

            case 0025:
                switch(field) {
                case VARIANT(WMOP_GEQ): /* B greater than or equal to A */
                case VARIANT(WMOP_GTR): /* B Greater than A */
                case VARIANT(WMOP_NEQ): /* B Not equal to A */
                case VARIANT(WMOP_LEQ): /* B Less Than or Equal to A */
                case VARIANT(WMOP_LSS): /* B Less Than A */
                case VARIANT(WMOP_EQL): /* B Equal A */
                        AB_valid();
                        f = 0;
                        i = compare();
                        switch(field) {
                        case VARIANT(WMOP_GEQ):
                                if ((i & 5) != 0) f = 1; break;
                        case VARIANT(WMOP_GTR):
                                if (i == 4) f = 1; break;
                        case VARIANT(WMOP_NEQ):
                                if (i != 1) f = 1; break;
                        case VARIANT(WMOP_LEQ):
                                if ((i & 3) != 0) f = 1; break;
                        case VARIANT(WMOP_LSS):
                                if (i == 2) f = 1; break;
                        case VARIANT(WMOP_EQL):
                                if (i == 1) f = 1; break;
                        }
                        B = f;
                        AROF = 0;
                        break;

                case VARIANT(WMOP_XCH): /* Exchange */
                        AB_valid();
                        temp = A;
                        A = B;
                        B = temp;
                        break;

                case VARIANT(WMOP_FTF): /* Transfer F Field to F Field */
                        AB_valid();
                        B &= ~FFIELD;
                        B |= (A & FFIELD);
                        AROF = 0;
                        break;

                case VARIANT(WMOP_FTC): /* Transfer F Field to Core Field */
                        AB_valid();
                        B &= ~CORE;
                        B |= (A & FFIELD) >> FFIELD_V;
                        AROF = 0;
                        break;

                case VARIANT(WMOP_CTC): /* Transfer Core Field to Core Field */
                        AB_valid();
                        B &= ~CORE;
                        B |= (A & CORE);
                        AROF = 0;
                        break;

                case VARIANT(WMOP_CTF): /* Transfer Core Field to F Field */
                        AB_valid();
                        B &= ~FFIELD;
                        B |= FFIELD & (A << FFIELD_V);
                        AROF = 0;
                        break;

                case VARIANT(WMOP_DUP): /* Duplicate */
                        if (AROF && BROF) {
                             B_empty();
                             B = A;
                             BROF = 1;
                        } else if (AROF || BROF) {
                             if (AROF)
                                B = A;
                             else
                                A = B;
                             AROF = BROF = 1;
                        } else {
                             A_valid(); /* Make A Valid */
                             B = A;
                             BROF = 1;
                        }
                        break;
                }
                break;

            case 0031:
                switch(field) {
                case VARIANT(WMOP_BFC): /* Branch Forward Conditional  0231 */
                case VARIANT(WMOP_BBC): /* Branch Backward Conditional 0131 */
                case VARIANT(WMOP_LFC): /* Word Branch Forward Conditional 2231 */
                case VARIANT(WMOP_LBC): /* Word Branch Backward Conditional 2131 */
                        AB_valid();
                        BROF = 0;
                        if (B & 1) {
                            AROF = 0;
                            break;
                        }

                        /* Fall through */
                case VARIANT(WMOP_BFW): /* Branch Forward Unconditional 4231 */
                case VARIANT(WMOP_BBW): /* Banch Backward Unconditional 4131 */
                case VARIANT(WMOP_LFU): /* Word Branch Forward Unconditional  6231*/
                case VARIANT(WMOP_LBU): /* Word Branch Backward Unconditional 6131 */
                        A_valid();
                        if (A & FLAG) {
                            if ((A & PRESENT) == 0) {
                                if (L == 0)     /* Back up to branch word */
                                    prev_addr(C);
                                if (NCSF)
                                    Q |= PRES_BIT;
                                if (field & 020)
                                   BROF = 1;
                                break;
                            }
                            C = CF(A);
                            L = 0;
                        } else {
                            if (L == 0) {       /* Back up to branch op */
                                prev_addr(C);
                                L = 3;
                            } else {
                               L--;
                            }
                            /* Follow logic based on real hardware */
                            if ((field & 020) == 0) {   /* Syllable branch */
                                if (field & 02) {       /* Forward */
                                   if (A & 1) {         /* N = 0 */
                                        L++; C += (L >> 2); L &= 3;
                                   }
                                   A >>= 1;
                                   if (A & 1) {         /* Bump L by 2 */
                                        L+=2; C += (L >> 2); L &= 3;
                                   }
                                   A >>= 1;
                                } else {                /* Backward */
                                   if (A & 1) {         /* N = 0 */
                                        if (L == 0) {
                                           C--; L = 3;
                                        } else {
                                           L--;
                                        }
                                   }
                                   A >>= 1;
                                   if (A & 1) {         /* N = 1 */
                                        if (L < 2) {
                                           C--; L += 2;
                                        } else {
                                           L -= 2;
                                        }
                                   }
                                   A >>= 1;
                                }
                                /* Fix up for backward step */
                                if (L == 3) {   /* N = 3 */
                                   C++; L = 0;
                                } else {
                                   L++;
                                }
                            } else {
                                L = 0;
                            }
                            if (field & 02) {   /* Forward */
                                C += A & 01777;
                            } else {            /* Backward */
                                C -= A & 01777;
                            }
                            C &= CORE;
                        }
                        AROF = 0;
                        PROF = 0;
                        break;

                case VARIANT(WMOP_SSN): /* Set Sign Bit */
                        A_valid();
                        A |= MSIGN;
                        break;

                case VARIANT(WMOP_CHS): /* Change sign bit */
                        A_valid();
                        A ^= MSIGN;
                        break;

                case VARIANT(WMOP_SSP): /* Reset Sign Bit */
                        A_valid();
                        A &= ~MSIGN;
                        break;

                case VARIANT(WMOP_TOP): /* Test Flag Bit */
                        B_valid();      /* Move result to B */
                        if (B & FLAG)
                           A = 0;
                        else
                           A = 1;
                        AROF = 1;
                        break;

                case VARIANT(WMOP_TUS): /* Interrogate Peripheral Status */
                        A_empty();
                        A = iostatus;
                        AROF = 1;
                        break;

                case VARIANT(WMOP_TIO): /* Interrogate I/O Channels */
                        A_empty();
                        A = find_chan();
                        AROF = 1;
                        break;

                case VARIANT(WMOP_FBS): /* Flag Bit Search */
                        A_valid();
                        Ma = CF(A);
                        memory_cycle(4);        /* Read A */
                        while((A & FLAG) == 0) {
                            next_addr(Ma);
                            memory_cycle(4);
                        }
                        A = FLAG | PRESENT | toC(Ma);
                        break;
                }
                break;

            case 0035:
                switch(field) {
                case VARIANT(WMOP_BRT): /* Branch Return */
                        B_valid();
                        if ((B & PRESENT) == 0) {
                           if (NCSF)
                                Q |= PRES_BIT;
                           break;
                        }
                        f = set_via_RCW(B, 0, 1); /* Restore registers */
                        L = 0;
                        S = F;
                        memory_cycle(3);        /* Read B */
                        prev_addr(S);
                        set_via_MSCW(B);
                        BROF = 0;
                        PROF = 0;
                        break;

                case VARIANT(WMOP_RTN): /* Return normal  02 */
                case VARIANT(WMOP_RTS): /* Return Special 12 */
                        A_valid();
                        if (A & FLAG) {
                            if ((A & PRESENT) == 0) {
                                if (NCSF)
                                    Q |= PRES_BIT;
                                break;
                            }
                        }

                        /* Fall through */
                case VARIANT(WMOP_XIT): /* Exit  04 */
                        if (field & 04)
                            AROF = 0;
                        BROF = 0;
                        PROF = 0;
                        if ((field & 010) == 0) /* normal return & XIT */
                            S = F;
                        memory_cycle(3);        /* Restore RCW to B*/
                        if ((B & FLAG) == 0) {
                            if (NCSF)
                                Q |= FLAG_BIT;
                            break;
                        }
                        f = set_via_RCW(B, 0, 0); /* Restore registers */
                        S = F;
                        BROF = 0;
                        memory_cycle(3);        /* Read B */
                        prev_addr(S);
                        set_via_MSCW(B);
                        if (MSFF && SALF) {
                             Ma = F;
                             do {
                                /* B = M[FIELD], Ma = B[FIELD]; */
                                memory_cycle(6);        /* Grab previous MCSW */
                             } while(B & SMSFF);
                             Ma = R | 7;
                             memory_cycle(015); /* Store B in Ma */
                        }
                        BROF = 0;
                        if (field & 2) {        /* RTS and RTN */
                            if (f)
                                goto desc;
                            else
                                goto opdc;
                        }
                        break;
                }
                break;

            case 0041:
                A_valid();
                switch(field) {
                case VARIANT(WMOP_INX): /* Index */
                        AB_valid();
                        A = (A & (~CORE)) | ((A + B) & CORE);
                        BROF = 0;
                        break;

                case VARIANT(WMOP_COC): /* Construct Operand Call */
                case VARIANT(WMOP_CDC): /* Construct descriptor call */
                        AB_valid();
                        temp = A;
                        A = B | FLAG;
                        B = temp;
                        if (field & 010)        /* COC or CDC */
                            goto desc;
                        else
                            goto opdc;
                        /* Not reached */
                        break;

                case VARIANT(WMOP_SSF): /* Set or Store S or F registers */
                        AB_valid();
                        switch( A & 03) {
                        case 0:                 /* F => B */
                                B = replF(B, F);
                                break;
                        case 1:                 /* S => B */
                                B = replC(B, S);
                                break;
                        case 2:
                                F = FF(B);
                                SALF = 1;
                                BROF = 0;
                                break;
                        case 3:
                                S = CF(B);
                                BROF = 0;
                                break;
                        }
                        AROF = 0;
                        break;

                case VARIANT(WMOP_LLL): /* Link List Look-up */
                        AB_valid();
                        A = MANT ^ A;
                        do {
                            Ma = CF(B);
                            memory_cycle(5);
                            temp = (B & MANT) + (A & MANT);
                        } while ((temp & EXPO) == 0);
                        A = FLAG | PRESENT | toC(Ma);
                        break;

                case VARIANT(WMOP_CMN): /* Enter Character Mode In Line */
                        A_valid();      /* Make sure TOS is in A. */
                        AB_empty();     /* Force A&B to stack */
                        B = RCW(0);     /* Build RCW word */
                        BROF = 1;
                        B_empty();      /* Save return control word */
                        CWMF = 1;
                        SALF = 1;
                        MSFF = 0;
                        B = A;          /* A still had copy of TOS */
                        AROF = 0;
                        BROF = 0;
                        R = 0;
                        F = S;          /* Set F and X */
                        X = toF(S);
                        if (B & FLAG) {
                            if ((B & PRESENT) == 0) {
                                if (NCSF)
                                    Q |= PRES_BIT;
                                break;
                            }
                            KV = 0;
                        } else {
                            KV = (uint8)((B >> (FFIELD_V - 3)) & 070);
                        }
                        S = CF(B);
                        break;

                case VARIANT(WMOP_MKS): /* Mark Stack */
                        AB_empty();
                        B = MSCW;
                        BROF = 1;
                        B_empty();
                        F = S;
                        if (!MSFF && SALF) {
                            Ma = R | 7;
                            memory_cycle(015);  /* Store B in Ma */
                        }
                        MSFF = 1;
                        break;
                }
                break;

            case 0051:          /* Conditional bit field */
                if ((field & 074) == 0) {       /* DEL Operator */
                    if (AROF)
                        AROF = 0;
                    else if (BROF)
                        BROF = 0;
                    else
                        prev_addr(S);
                    break;
                }
                AB_valid();
                f = 0;
                bit_b = bit_number[GH];
                if (field & 2)
                    BROF = 0;
                for (i = (field >> 2) & 017; i > 0; i--) {
                    if (B & bit_mask[bit_b-i])
                        f = 1;
                }
                if (f) {
                        T = (field & 1) ? WMOP_BBW : WMOP_BFW;
                        TROF = 1;
                } else {
                        AROF = 0;
                }
                break;

            case WMOP_DIA:              /* Dial A XX */
                if (field != 0)
                    GH = field;
                break;

            case WMOP_DIB:              /* Dial B XX Upper 6 not Zero */
                if (field != 0)
                    KV = field;
                else {          /* Set Variant */
                    VARF |= SALF;
                    SALF = 0;
                }
                break;

            case WMOP_ISO:              /* Variable Field Isolate XX */
                A_valid();
                if ((field & 070) != 0) {
                    bit_a = bit_number[GH | 07];        /* First Character */
                    X = A >> bit_a;                     /* Get first char */
                    X &= 077LL >> (GH & 07);    /* Mask first character */
                    GH &= 070;  /* Clear H */
                    while(field > 017) {        /* Transfer chars. */
                        bit_a -= 6;
                        X = (X << 6) | ((A >> bit_a) & 077);
                        field -= 010;
                        GH += 010;              /* Bump G for each char */
                        GH &= 070;
                    }
                    X >>= (field & 07); /* Align with remaining bits. */
                    A = X & MANT;       /* Max is 39 bits */
                }
                break;

            case WMOP_TRB:              /* Transfer Bits XX */
            case WMOP_FCL:              /* Compare Field Low XX */
            case WMOP_FCE:              /* Compare Field Equal XX */
                AB_valid();
                f = 1;
                bit_a = bit_number[GH];
                bit_b = bit_number[KV];
                for(; field > 0 && bit_a >= 0 && bit_b >= 0;
                        field--, bit_a--, bit_b--) {
                     int ba, bb;
                     ba = (bit_mask[bit_a] & A) != 0;
                     switch(opcode) {
                     case WMOP_TRB:             /* Just copy bit */
                           B &= ~bit_mask[bit_b];
                           if (ba)
                                B |= bit_mask[bit_b];
                           break;
                     case WMOP_FCL:             /* Loop until unequal */
                     case WMOP_FCE:
                          bb = (bit_mask[bit_b] & B) != 0;
                          if (ba != bb) {
                             if (opcode == WMOP_FCL)
                                f = ba;
                             else
                                f = 0;
                             i = field;
                          }
                          break;
                     }
                }
                if (opcode != WMOP_TRB) {
                        A = f;
                } else {
                        AROF = 0;
                }
                break;
             }
          }
        }
    }                           /* end while */
/* Simulation halted */

    return reason;
}

/* Interval timer routines */
t_stat
rtc_srv(UNIT * uptr)
{
    int32 t;

    t = sim_rtcn_calb(rtc_tps, TMR_RTC);
    sim_activate_after(uptr, 1000000/rtc_tps);
    tmxr_poll = t;
    RTC++;
    if (RTC & 0100)  {
        IAR |= IRQ_0;
    }
    RTC &= 077;
    return SCPE_OK;
}
/* Reset routine */

t_stat
cpu_reset(DEVICE * dptr)
{
    /* Reset CPU 2 first */
    cpu_index = 1;
    C = 020;
    S = F = R = T = 0;
    L = 0;
    A = B = X = P = 0;
    AROF = BROF = TROF = PROF = NCSF = SALF = CWMF = MSFF = VARF = 0;
    GH = KV = Q = 0;
    hltf[1] = 0;
    P2_run = 0;
    /* Reset CPU 1 now */
    cpu_index = 0;
    C = 020;
    S = F = R = T = IAR = 0;
    L = 0;
    A = B = X = P = 0;
    AROF = BROF = TROF = PROF = NCSF = SALF = CWMF = MSFF = VARF = 0;
    GH = KV = Q = 0;
    hltf[0] = 0;
    P1_run = 0;

    idle_addr = 0;
    sim_brk_types = sim_brk_dflt = SWMASK('E') | SWMASK('A') | SWMASK('B');
    hst_p = 0;

    sim_rtcn_init_unit (&cpu_unit[0], cpu_unit[0].wait, TMR_RTC);
    sim_activate(&cpu_unit[0], cpu_unit[0].wait) ;

    return SCPE_OK;
}


/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE)
        return SCPE_NXM;
    if (vptr != NULL)
        *vptr = (t_value)(M[addr] & (FLAG|FWORD));

    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE)
        return SCPE_NXM;
    M[addr] = val & (FLAG|FWORD);
    return SCPE_OK;
}

t_stat
cpu_show_size(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf(st, "%dK", MEMSIZE/1024);
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_uint64            mc = 0;
    uint32              i;
    int32               v;

    v = val >> UNIT_V_MSIZE;
    v = (v + 1) * 4096;
    if ((v < 0) || (v > MAXMEMSIZE))
        return SCPE_ARG;
    for (i = v-1; i < MEMSIZE; i++)
        mc |= M[i];
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    cpu_unit[0].flags &= ~UNIT_MSIZE;
    cpu_unit[0].flags |= val;
    cpu_unit[1].flags &= ~UNIT_MSIZE;
    cpu_unit[1].flags |= val;
    MEMSIZE = v;
    for (i = MEMSIZE; i < MAXMEMSIZE; i++)
        M[i] = 0;
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
            hst[i].c = 0;
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
    const char          *cptr = (const char *) desc;
    t_stat              r;
    t_value             sim_eval;
    struct InstHistory *h;
    static const char   flags[] = "ABCNSMV";

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
    fprintf(st, "P    CL                 A                               B   "
                "                       X     S     F     R      M  GH KV Flags"
                "  Q Intruction     IAR\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->c & HIST_PC) {   /* instruction? */
            int i;
            fprintf(st, "%o %05o%o ", h->cpu, h->c & 077777, h->l);
            sim_eval = (t_value)h->a_reg;
            (void)fprint_sym(st, 0, &sim_eval, &cpu_unit[0], SWMASK('B'));
            fputc((h->flags & F_AROF) ? '^': ' ', st);
            fputc(' ', st);
            sim_eval = (t_value)h->b_reg;
            (void)fprint_sym(st, 0, &sim_eval, &cpu_unit[0], SWMASK('B'));
            fputc((h->flags & F_BROF) ? '^': ' ', st);
            fputc(' ', st);
            fprint_val(st, (t_value)h->x_reg, 8, 39, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->s, 8, 15, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->f, 8, 15, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->r, 8, 15, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->ma, 8, 15, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->gh, 8, 6, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->kv, 8, 6, PV_RZRO);
            fputc(' ', st);
            for(i = 2; i < 8; i++) {
                fputc (((1 << i) & h->flags) ? flags[i] : ' ', st);
            }
            fprint_val(st, h->q, 8, 9, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->op, 8, 12, PV_RZRO);
            fputc(' ', st);
            print_opcode(st, h->op,
                ((h->flags & F_CWMF) ? char_ops : word_ops));
            fputc(' ', st);
            fprint_val(st, h->iar, 8, 16, PV_RZRO);
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}


t_stat              cpu_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "B5500 CPU\n\n");
    fprintf(st, "The B5500 could support up to two CPU's the second CPU is disabled by\n");
    fprintf(st, "default. Use:\n");
    fprintf(st, "       sim> SET CPU1 ENABLE                enable second CPU\n");
    fprintf(st, "The primary CPU can't be disabled. Memory is shared between the two\n");
    fprintf(st, "CPU's. Memory can be configured in 4K increments up to 32K total.\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}
