/* i7010_cpu.c: IBM 7010 CPU simulator

   Copyright (c) 2006, Richard Cornwell

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

   cpu          7010 central processor

   The IBM 1410 and 7010 were designed as enhancements to the IBM 1401,
   these were somewhat source compatable, but not binary compatable.
   The 1410 was introduced on September, 12 1960 and the 7010 in 1962.
   The 1410 was withdrawn on March 30, 1970.  The 7010 featured
   4 I/O channels where the 1410 had 2. Also the 7010 could access 100,000
   characters of memory as opposed to the 80,000 for the 1410. The 7010 also
   featured optional decimal floating point instructions. Memory was
   divided into feilds seperated by a special flag called a word mark.
   Instructions end at the first character with the word mark set. They
   consist of a operation code, followed by 1 or 2 5-digit addresses, and
   an optional instruction modifier. If the 10's and 100's digit have zone
   bits set the address is modified by the contents of the five characters
   at locations 25-100. Each register is 5 characters long and word marks
   are ignored. The 1410 and 7010 could also be optionaly equiped with
   priority mode to allow for device complete interupts.

   The 7010 or 1410 cpu has no registers. All operations on done from
   memory.

        i7010_defs.h    add device definitions
        i7010_sys.c     add sim_devices table entry
*/

#include "i7010_defs.h"
#include "sim_card.h"

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (017 << UNIT_V_MSIZE)
#define UNIT_V_CPUMODEL (UNIT_V_UF + 5)
#define UNIT_MODEL      (0x3 << UNIT_V_CPUMODEL)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_CPUMODEL) & 0x3)
#define MODEL(x)        (x << UNIT_V_CPUMODEL)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define OPTION_PRIO     (1 << (UNIT_V_UF + 13))
#define OPTION_FLOAT    (1 << (UNIT_V_UF + 14))
#define OPTION_PROT     (1 << (UNIT_V_UF_31))

#define TMR_RTC         100

#define HIST_XCT        1       /* instruction */
#define HIST_INT        2       /* interrupt cycle */
#define HIST_TRP        3       /* trap cycle */
#define HIST_MIN        64
#define HIST_MAX        65536
#define HIST_NOEA       0x40000000
#define HIST_PC         0x100000
#define HIST_MSK        0x0FFFFF
#define HIST_1401       0x200000        /* 1401 instruction */

struct InstHistory
{
    uint32              ic;
    uint8               inst[15];
    uint32              astart;
    uint32              bstart;
    uint32              aend;
    uint32              bend;
    uint8               dlen;
    uint8               bdata[50];
};

t_stat              cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
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
int                 do_addint(int val);
t_stat              do_addsub(int mode);
t_stat              do_mult();
t_stat              do_divide();

/* Interval timer option */
t_stat              rtc_srv(UNIT * uptr);
t_stat              rtc_reset(DEVICE * dptr);
int32               rtc_tps = 200;


/* General registers */
uint8               M[MAXMEMSIZE] = { 0 };      /* memory */
int32               IAR;                        /* program counter */
int32               AAR;                        /* A Address Register */
int32               BAR;                        /* B Address Register */
int32               CAR;                        /* C Address Register */
int32               DAR;                        /* D Address Register */
uint8               SW;                         /* Switch register */
uint32              XR;                         /* IO Address register */
uint8               cind;                       /* Compare indicators */
uint8               zind;                       /* Zero balence */
uint8               oind;                       /* Overflow indicator */
uint8               dind;                       /* Divide Over indicator */
uint8               tind;                       /* Tape indicator */
uint8               op_mod;                     /* Opcode modifier */
uint8               euind;                      /* Exp underflow indicator */
uint8               eoind;                      /* Exp overflow indicator */
uint8               fault;                      /* Access fault */
uint8               pri_enb = 1;                /* Priority mode flags */
uint8               inquiry = 0;                /* Inquiry IRQ pending */
uint8               urec_irq[NUM_CHAN];         /* Unit record IRQ pending */
uint8               astmode = 1;                /* Astrisk mode */
uint8               chan_io_status[NUM_CHAN];   /* Channel status */
uint8               chan_seek_done[NUM_CHAN];   /* Channel seek finished */
uint8               chan_irq_enb[NUM_CHAN];     /* IRQ type opcode */
uint8               lpr_chan9[NUM_CHAN];        /* Line printer at channel 9 */
uint8               lpr_chan12[NUM_CHAN];       /* Line printer at channel 12 */
extern uint32       caddr[NUM_CHAN];            /* Channel addresses */
int                 low_addr = -1;              /* Low protection address */
int                 high_addr = -1;             /* High protection address */
int                 reloc = 0;                  /* Dislocate address flag */
uint8               prot_fault = 0;             /* Protection fault indicators. */
uint8               prot_enb = 0;               /* Protection enables */
uint8               relo_flags = 0;             /* Relocation flags */
uint8               timer_irq = 0;              /* Interval timer interrupt */
uint8               timer_enable = 0;           /* Interval timer enable */
int                 timer_interval = 0;         /* Interval timer interval */
int                 chwait = 0;                 /* Wait for channel to finish */
int                 io_flags = 0;               /* Io flags for 1401 */
int                 cycle_time = 28;            /* Cycle time in 100ns */
uint8               time_digs[] = {0, 2, 3, 5, 7, 8};

/* History information */
int32               hst_p = 0;                  /* History pointer */
int32               hst_lnt = 0;                /* History length */
struct InstHistory *hst = NULL;                 /* History stack */
extern UNIT         chan_unit[];

/* Simulator debug controls */
DEBTAB              cpu_debug[] = {
    {"CHANNEL", DEBUG_CHAN},
    {"TRAP", DEBUG_TRAP},
    {"CMD", DEBUG_CMD},
    {"DETAIL", DEBUG_DETAIL},
    {"EXP", DEBUG_EXP},
    {"PRI", DEBUG_PRIO},
    {0, 0}
};



/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
    { UDATA(rtc_srv, MODEL(2)|MEMAMOUNT(9)|OPTION_PRIO|OPTION_FLOAT,
                 MAXMEMSIZE), 10000 };

REG                 cpu_reg[] = {
    {DRDATAD(IAR, IAR, 18, "Instruction Address Register"), REG_FIT},
    {DRDATAD(A, AAR, 18, "A Address register"), REG_FIT},
    {DRDATAD(B, BAR, 18, "B Address register"), REG_FIT},
    {DRDATAD(C, CAR, 18, "C Address register"), REG_FIT},
    {DRDATAD(D, DAR, 18, "D Address register"), REG_FIT},
    {DRDATAD(E, caddr[0], 18, "Channel 0 address"), REG_FIT},
    {DRDATAD(F, caddr[1], 18, "Channel 1 address"), REG_FIT},
    {DRDATAD(G, caddr[2], 18, "Channel 2 address"), REG_FIT},
    {DRDATAD(H, caddr[3], 18, "Channel 3 address"), REG_FIT},
    {FLDATAD(ASTRISK, astmode, 1, "Asterix Mode"), REG_FIT},
    {BINRDATAD(SW, SW, 7, "Sense Switch register"), REG_FIT},
    {FLDATAD(SW1, SW, 0, "Sense Switch 0"), REG_FIT},
    {FLDATAD(SW2, SW, 1, "Sense Switch 1"), REG_FIT},
    {FLDATAD(SW3, SW, 2, "Sense Switch 2"), REG_FIT},
    {FLDATAD(SW4, SW, 3, "Sense Switch 3"), REG_FIT},
    {FLDATAD(SW5, SW, 4, "Sense Switch 4"), REG_FIT},
    {FLDATAD(SW6, SW, 5, "Sense Switch 5"), REG_FIT},
    {FLDATAD(SW7, SW, 6, "Sense Switch 6"), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MODEL, MODEL(1), "1401", "1401", NULL, NULL, NULL, "Emulate a 1401"},
    {UNIT_MODEL, MODEL(2), "7010", "7010", NULL, NULL, NULL, "Emulate a 7010"},
    {UNIT_MSIZE, MEMAMOUNT(0),   "10K",  "10K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(1),   "20K",  "20K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(2),   "30K",  "30K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(3),   "40K",  "40K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(4),   "50K",  "50K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(5),   "60K",  "60K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(6),   "70K",  "70K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(7),   "80K",  "80K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(8),   "90K",  "90K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(9),  "100K", "100K", &cpu_set_size},
    {OPTION_PRIO, 0, NULL, "NOPRIORITY", NULL, NULL, NULL,
          "No Priority Mode"},
    {OPTION_PRIO, OPTION_PRIO, "PRIORITY", "PRIORITY", NULL, NULL, NULL,
          "Priority Mode"},
    {OPTION_FLOAT, 0, NULL, "NOFLOAT", NULL, NULL,NULL,
          "No Floating Point"},
    {OPTION_FLOAT, OPTION_FLOAT, "FLOAT", "FLOAT", NULL, NULL, NULL,
          "Floating point"},
    {OPTION_PROT, 0, NULL, "NOPROT", NULL, NULL,NULL,
          "No memory protection"},
    {OPTION_PROT, OPTION_PROT, "PROT", "PROT", NULL, NULL, NULL,
          "Memory Protection"},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 18, 1, 8, 8,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, cpu_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
};


                      /*0, 1, 2, 3, 4, 5, 6, 7, 8, 9, A, B, C, D, E, F */
uint8   bcd_bin[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 3, 4, 5, 6, 7};
uint8   bin_bcd[20] = { 10, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                        10, 1, 2, 3, 4, 5, 6, 7, 8, 9};
uint32  dscale[4][16] = {
    {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 0,30,0,0,0,0},
    {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 0,0,0,0,0,0},
    {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 0,0,0,0,0,0},
    {0, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000,
                0,0,0,0,0,0}
};


#define NORELA          0x2
#define NORELB          0x4

uint8   digit_addone[16] = {
    0,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x10,0x01,0x0b,0x0c,0x0d,0x0e,
    0x0f};

uint8  cmp_order[0100] = {
     /* b   1    2    3    4    5    6    7 */
        0,  55,  56,  57,  58,  59,  60,  61,
     /* 8   9    0    #    @    :    >    tm */
       62,  63,  54,  20,  21,  22,  23,  24,
     /*cent /    S    T    U    V    W    X */
       19,  13,  46,  47,  48,  49,  50,  51,
     /* Y   Z   rm    ,     %    =    '    " */
       52,  53,  45,  14,  15,  16,  17,  18,
     /* -   J    K    L    M    N    O    P */
       12,  36,  37,  38,  39,  40,  41,  42,
     /* Q   R    !    $    *    )    ;   del  */
       43,  44,  35,   7,   8,   9,  10,  11,
     /* &   A    B    C    D    E    F    G  */
        6,  26,  27,  28,  29,  30,  31,  32,
     /* H   I    ?    .   sq   (    <    gm  */
       33,  34,  25,   1,   2,   3,   4,   5
};


#define O_A     001             /* Can take A */
#define O_B     002             /* Can take B */
#define O_AB    (O_A|O_B)       /* Can take both A & B */
#define O_M     004             /* Can take modifier */
#define O_X     010             /* Special Operand */
#define O_C     020             /* Load C register on frist argument */
#define O_D     0100            /* Load D register on second argument */
#define O_DBL   0200            /* When chained A same as B */
#define O_ABCD  (O_A|O_B|O_C|O_D)

uint8   op_args[64] = {
        /* 00    01    02   03    04       05      06       07 */
        /*            CC2          SSF2                        */
           0,    0,   O_M,   0,    O_M,       0,      0,       0,  /* 00 */
        /*                  FP     M                           */
           0,    0,    0, O_A|O_M, O_AB,  0,      0,       0,   /* 10 */
        /*      CS     S          T            UC      BWE     BBE      IO2 */
          0,O_AB|O_DBL,O_AB|O_DBL,O_AB|O_M,O_X|O_M,O_AB|O_M,O_AB|O_M,O_A|O_M|O_DBL,/* 20 */
        /* PRI   MSZ       SWM     D                           */
        O_A|O_M,O_AB,0,O_AB|O_DBL, O_AB,  0,     0,       0,   /* 30 */
        /*      B         SSF1  RDW    RD         NOP                  */
         0,O_A|O_M|O_DBL,O_M,O_X|O_B|O_M,O_X|O_B|O_M, 0,     0,       0,/* 40 */
        /*       IO1        ZS    STS                                */
           0, O_A|O_M|O_DBL,O_AB|O_DBL,O_A|O_M,   0,    0,   0,   0,   /* 50 */
        /*       A        BCE    C   MOV      E    CC1    SAR     */
         0, O_AB|O_DBL,O_AB|O_M,O_AB,O_AB|O_M,O_AB, O_M,O_C|O_M,/* 60 */
        /*  0       ZA        H     CWM                              */
            0, 0, O_AB|O_DBL,O_A,O_AB|O_DBL,      0,     0,    0,   /* 70 */
};


uint8   op_1401[64] = {
        /* 00    01    02   03    04       05      06       07 */
         /* b     1      2      3    4    5    6    7 */
        /*     RCD     PRT      PUN                          */
           0,    O_A,   O_A|O_M,  O_A,    O_A,  O_A,  O_A|O_M,   O_A|O_M,  /* 00 */
        /* 8   9    0    #    @    :    >    tm */
        /*                         M                           */
           0,    0,  0, O_AB|O_DBL, O_AB,  0,      0,       0,   /* 10 */
        /*cent /        S         T   U    V    W    X */
        /*      CS     S                   BWZ  BBE     */
          0,O_AB|O_DBL,O_AB|O_DBL,0,O_X|O_M,O_AB|O_M,O_AB|O_M,0,/* 20 */
        /* Y   Z   rm    ,       %      =    '    " */
        /* MZ  MCS       SWM            MA                 */
        O_AB, O_AB,0,O_AB|O_DBL, O_AB,  O_AB,     0,       0,   /* 30 */
        /* -   J    K   L     M    N    O    P */
        /*          RDW MLCWA MLC  NOP       MRCM   */
         0,    0,O_M|O_A,O_AB, O_AB,O_AB,0,  O_AB,/* 40 */
        /* Q   R    !    $    *    )    ;   del  */
        /* SAR   ZS                                   */
           O_C, 0,    O_AB|O_DBL,0,   0,    0,   0,   0,   /* 50 */
        /* &   A          B        C    D    E    F    G  */
        /*    A          B        C   MLNS                        */
         0, O_AB|O_DBL,O_AB|O_M,O_AB,O_AB,O_AB, O_M|O_A,0,/* 60 */
        /* H   I    ?          .   sq   (    <    gm  */
        /* SBR      ZA         H  CWM                          */
        O_C|O_B,0, O_AB|O_DBL,O_A,O_AB|O_DBL,      0,     0,    0,   /* 70 */
};

uint8 FetchP(uint32 MA) {
      uint32 MAR = MA & AMASK;

      if (reloc && (MA & BBIT) == 0 && MAR > 100) {
          if (low_addr > 0) {
              MAR += low_addr;
              if (MAR >= 100000)
                  MAR -= 100000;
          }
          if (prot_enb && (high_addr > 0) && (MAR > (uint32)high_addr)) {
                fault = STOP_PROT;
                return 0;
          }
      } else if (prot_enb && (MA & BBIT) == 0 && MAR > 100) {
           if (low_addr < 0 && high_addr == 0) {
                fault = STOP_PROT;
                return 0;
           }
      }
      if (MAR >= MEMSIZE) {
        fault = STOP_INVADDR;
        return 0;
      }
      return M[MAR];
}


uint8 ReadP(uint32 MA) {
      uint32 MAR = MA & AMASK;

      if (fault)
        return 0;

      if (reloc && (MA & BBIT) == 0 && MAR > 100) {
          if (low_addr > 0) {
              MAR += low_addr;
              if (MAR >= 100000)
                  MAR -= 100000;
          }
          if (prot_enb && (high_addr > 0) && (MAR > (uint32)high_addr)) {
                fault = STOP_PROT;
                return 0;
          }
      } else if (prot_enb && (MA & BBIT) == 0 && MAR > 100) {
           if (low_addr < 0 && high_addr == 0) {
                fault = STOP_PROT;
                return 0;
           }
           if (((low_addr >= 0) && (MAR < (uint32)low_addr)) ||
                ((high_addr > 0) && (MAR > (uint32)high_addr))) {
                fault = STOP_PROT;
                return 0;
           }
      }
      if (MAR >= MEMSIZE) {
        fault = STOP_INVADDR;
        return 0;
      }
      return M[MAR];
}

void WriteP(uint32 MA, uint8 v) {
      uint32 MAR = MA & AMASK;

      if (fault)
        return;

      if (reloc && (MA & BBIT) == 0 && MAR > 100) {
          if (low_addr > 0) {
              MAR += low_addr;
              if (MAR >= 100000)
                  MAR -= 100000;
          }
          if (prot_enb && (high_addr > 0) && (MAR > (uint32)high_addr)) {
                fault = STOP_PROT;
                return;
          }
      } else if (prot_enb && (MA & BBIT) == 0 && MAR > 100) {
           if (low_addr < 0 && high_addr == 0) {
                fault = STOP_PROT;
                return;
           }
           if (((low_addr >= 0) && (MAR < (uint32)low_addr)) ||
                 ((high_addr > 0) && (MAR > (uint32)high_addr))) {
                fault = STOP_PROT;
                return;
           }
      }
      if (MAR >= MEMSIZE) {
        fault = STOP_INVADDR;
        return;
      }
      M[MAR] = v;
}

void ReplaceMask(uint32 MA, uint8 v, uint8 mask) {
      uint32 MAR = MA & AMASK;

      if (fault)
        return;

      if (reloc && (MA & BBIT) == 0 && MAR > 100) {
          if (low_addr > 0) {
              MAR += low_addr;
              if (MAR >= 100000)
                  MAR -= 100000;
          }
          if (prot_enb && (high_addr > 0) && (MAR > (uint32)high_addr)) {
                fault = STOP_PROT;
                return;
          }
      } else if (prot_enb && (MA & BBIT) == 0 && MAR > 100) {
           if (low_addr < 0 && high_addr == 0) {
                fault = STOP_PROT;
                return;
           }
           if (((low_addr >= 0) && (MAR < (uint32)low_addr)) ||
                 ((high_addr > 0) && (MAR > (uint32)high_addr))) {
                fault = STOP_PROT;
                return;
           }
      }
      if (MAR >= MEMSIZE) {
        fault = STOP_INVADDR;
        return;
      }
      M[MAR] &= ~mask;
      M[MAR] |= v;
}


void SetBit(uint32 MA, uint8 v) {
      uint32 MAR = MA & AMASK;

      if (fault)
        return;

      if (reloc && (MA & BBIT) == 0 && MAR > 100) {
          if (low_addr > 0) {
              MAR += low_addr;
              if (MAR >= 100000)
                  MAR -= 100000;
          }
          if (prot_enb && (high_addr > 0) && (MAR > (uint32)high_addr)) {
                fault = STOP_PROT;
                return;
          }
      } else if (prot_enb && (MA & BBIT) == 0 && MAR > 100) {
           if (low_addr < 0 && high_addr == 0) {
                fault = STOP_PROT;
                return;
           }
           if (((low_addr >= 0) && (MAR < (uint32)low_addr)) ||
                 ((high_addr > 0) && (MAR > (uint32)high_addr))) {
                fault = STOP_PROT;
                return;
           }
      }
      if (MAR >= MEMSIZE) {
        fault = STOP_INVADDR;
        return;
      }
      M[MAR] |= v;
}

void ClrBit(uint32 MA, uint8 v) {
      uint32 MAR = MA & AMASK;

      if (fault)
        return;

      if (reloc && (MA & BBIT) == 0 && MAR > 100) {
          if (low_addr > 0) {
              MAR += low_addr;
              if (MAR >= 100000)
                  MAR -= 100000;
          }
          if (prot_enb && (high_addr > 0) && (MAR > (uint32)high_addr)) {
                fault = STOP_PROT;
                return;
          }
      } else if (prot_enb && (MA & BBIT) == 0 && MAR > 100) {
           if (low_addr < 0 && high_addr == 0) {
                fault = STOP_PROT;
                return;
           }
           if (((low_addr >= 0) && (MAR < (uint32)low_addr)) ||
                ((high_addr > 0) && (MAR > (uint32)high_addr))) {
                fault = STOP_PROT;
                return;
           }
      }
      if (MAR >= MEMSIZE) {
        fault = STOP_INVADDR;
        return;
      }
      M[MAR] &= ~v;
}

#define UpReg(reg) reg++; if ((reg & AMASK) == MEMSIZE) { \
                 reason = STOP_INVADDR; break; }

#define DownReg(reg) if ((reg & AMASK) == 0) { \
                 reason = STOP_INVADDR; break; } else { reg--; }

#define ValidAddr(reg) if ((reg & AMASK)== 0 || !MEM_ADDR_OK(reg)) { \
                    reason = STOP_INVADDR; break; \
                }

#define ZeroAddr(reg) if ((reg & AMASK)== 0) { \
                    reason = STOP_INVADDR; break; \
                }

t_stat
sim_instr(void)
{
    t_stat              reason;
    uint16              t;
    int                 temp;
    int32               STAR = 0;
    uint8               op, op_info;
    int                 state;
    uint8               ix = 0;
    uint8               br;
    uint8               ar = 0;
    int                 sign, qsign;
    uint8               ch;
    int                 cy;
    int                 i;
    int                 jump;           /* Do transfer to AAR after op */
    int                 instr_count = 0;/* Number of instructions to execute */

    if (sim_step != 0) {
        instr_count = sim_step;
        sim_cancel_step();
    }

    reason = 0;
    fault = 0;
    if (cpu_unit.flags & OPTION_PROT)
        sim_activate(&cpu_unit, sim_rtcn_calb(cpu_unit.wait, TMR_RTC));

    while (reason == 0) {       /* loop until halted */

        chan_proc();
        if (chwait != 0) {
            if (chan_active(chwait & 07)) {
                sim_interval = 0;
            } else {
                if ((chwait & 040) == 0) {
                    BAR = caddr[chwait & 07];
                    if (hst_lnt)        /* History enabled? */
                        hst[hst_p].bend = BAR;
                }
                chan_io_status[chwait & 07] &= ~0100;
                chwait = 0;
            }
        }

        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason != SCPE_OK) {
                break;  /* process */
            }
        }

        if (chwait == 0 && sim_brk_summ && sim_brk_test(IAR, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

        if (chwait == 0) {
            uint8 bbit = 0;
            if (hst_lnt) {      /* History enabled? */
                hst_p = (hst_p+1);      /* Next entry */
                if (hst_p >= hst_lnt)
                    hst_p = 0;
                hst[hst_p].ic = IAR | HIST_PC;
                if (CPU_MODEL == 1)
                    hst[hst_p].ic |= HIST_1401;
            }
            op = FetchP(IAR++);
            /* Check if over the top */
            if (fault)
                goto check_prot;
            if (hst_lnt)        /* History enabled? */
                hst[hst_p].inst[0] = op;
            sim_interval -= 2;
            if ((op & WM) == 0) {
                reason = STOP_NOWM;
                goto check_prot;
            }
            op &= 077;
            op_info = (CPU_MODEL != 1)? op_args[op]: op_1401[op];
            state = 1;
            i = 1;
            temp = IAR + 5;     /* Save for intertupt routine */
            while(((br = FetchP(IAR)) & WM) == 0 && op_info != 0 && fault == 0) {
                 IAR++;
                 sim_interval -= 2;
                 if (hst_lnt)   /* History enabled? */
                     hst[hst_p].inst[i++] = br;
                 br &= 077;
                 if (CPU_MODEL != 1) {
                     switch(state) {
                     case 1: /* could be operand or address */
                         ar = br;
                         state = 2;
                         if (ar & 040)
                             bbit = 1;
                         else
                             bbit = 0;
                         break;
                     case 2: /* Has to be address, check if goes to C or AB */
                         state = 3;
                         if (op_info & O_X) {
                             XR = (ar << 12) | (br << 6);
                         } else if (op_info & (O_C|O_A)) {
                             STAR = dscale[3][bcd_bin[ar & 0xf]];
                             STAR += dscale[2][bcd_bin[br & 0xf]];
                             if ((ar & 020) || (br & 060))
                                 reason = STOP_INVADDR;
                         }
                         break;
                     case 3: /* Has to be address, check if goes to C or AB */
                         state = 4;
                         if (op_info & O_X) {
                             XR |= br;
                             state = 6;
                         } else if (op_info & (O_C|O_A)) {  /* hundreds */
                             ix = (br & 0x30) >> 2;
                             STAR += dscale[1][bcd_bin[br & 0xf]];
                         }
                         break;
                     case 4: /* Has to be address, check if goes to C or AB */
                         state = 5;
                         if (op_info & (O_C|O_A)) {  /* tens */
                             ix |= (br & 0x30) >> 4;
                             STAR += dscale[0][bcd_bin[br & 0xf]];
                         }
                         break;
                     case 5: /* Has to be address, check if goes to C or AB */
                         state = 6;
                         if (op_info & (O_C|O_A) && br & 060) {
                             reason = STOP_INVADDR;
                             break;
                         }
                         if (op_info & (O_C|O_A))    /* units */
                             STAR += bcd_bin[br & 0xf];
                         if ((op_info & O_A) && (ix != 0)) {
                             int        j, a, s;
                             /* do indexing */
                             ix = (ix * 5) + 24;
                             s = ((ReadP(ix) & 060) == 040)?1: 0;
                             a = bcd_bin[ReadP(ix--) & 0xf];
                             for(j = 0; j < 4; j++)
                                 a += dscale[j][bcd_bin[ReadP(ix--) & 0xf]];
                             STAR += (s)?(99999 - a):a;
                             STAR += s;
                             STAR %= 100000;
                             sim_interval -= 10;
                         }
                         if (bbit)
                             STAR |= BBIT;
                         bbit = 0;
                         if (op_info & O_C)
                             CAR = STAR;
                         if (op_info & O_A) {
                             AAR = STAR;
                             if (op_info & O_DBL) {
                                if (op_info & O_D)
                                     DAR = AAR;
                                BAR = AAR;
                             }
                         }
                         temp = IAR;    /* Save for intertupt routine */
                         break;
                     case 6:    /* Could be either B address or operand. */
                         state = 7;
                         ar = br;
                         if (ar & 040)
                             bbit = 1;
                         else
                             bbit = 0;
                         break;
                     case 7:    /* Has to be B address */
                         state = 8;
                         /* ten thousand, thousand */
                         if (op_info & (O_B|O_D)) {
                             STAR = dscale[3][bcd_bin[ar & 0xf]];
                             STAR += dscale[2][bcd_bin[br & 0xf]];
                             if ((ar & 020) || (br & 060))
                                 reason = STOP_INVADDR;
                         }
                         if ((op_info & O_M) == 0)
                             op_mod = 0;
                         break;
                     case 8:    /* Has to be B address */
                         state = 9;
                         /* hundreds */
                         if (op_info & (O_B|O_D)) {
                             STAR += dscale[1][bcd_bin[br & 0xf]];
                             ix = (br & 0x30) >> 2;
                         }
                         break;
                     case 9:    /* Has to be B address */
                         state = 10;
                         /* tens */
                         if (op_info & (O_B|O_D)) {
                             STAR += dscale[0][bcd_bin[br & 0xf]];
                             ix |= (br & 0x30) >> 4;
                         }
                         break;
                     case 10:   /* Units digit of B address */
                         state = 11;
                         if (op_info & (O_B|O_D)) {
                             if (br & 060) {
                                 reason = STOP_INVADDR;
                                 break;
                             }
                             STAR += bcd_bin[br & 0xf];
                         }
                         if (op_info & O_B && ix != 0) {
                             int        j, a, s;
                             /* do indexing */
                             ix = (ix * 5) + 24;
                             s = ((ReadP(ix) & 060) == 040)?1: 0;
                             a = bcd_bin[ReadP(ix--) & 0xf];
                             for(j = 0; j < 4; j++)
                                 a += dscale[j][bcd_bin[ReadP(ix--) & 0xf]];
                             STAR += (s)?(99999 - a):a;
                             STAR += s;
                             STAR %= 100000;
                             sim_interval -= 10;
                         }
                         if (bbit)
                             STAR |= BBIT;
                         bbit = 0;
                         if (op_info & O_D)
                             DAR = STAR;
                         if (op_info & O_B)
                             BAR = STAR;
                         break;
                     case 11:   /* Has to be modifier */
                         state = 12;
                         ar = br;
                         break;
                     case 12:   /* Too long */
                         reason = STOP_NOWM;
                         state = 13;
                         break;
                     }
                } else {        /* Handle 1401 emulation mode */
                     switch(state) {
                     case 1:    /* could be operand or address */
                        /* BA */
                        /* 00    0-999 */
                        /* 01    1000-1999 */
                        /* 10    2000-2999 */
                        /* 11    3000-3999 */
                         ar = br;
                         if (op_info & O_X ||
                              ((op == CHR_M || op == CHR_L) && br == CHR_RPARN)) {
                              XR = br << 12;
                              op_info |= O_X;
                         }
                         state = 2;
                         break;
                     case 2:/* Has to be address, check if goes to C or AB */
                        /* BA  */
                        /* 00  - none */
                        /* 01  - 1 87,88,89, 90,91 */
                        /* 10  - 2 92,93,94, 95,96 */
                        /* 11  - 3 97,98,99 */
                         state = 3;
                         if (op_info & O_X)
                             XR |= br << 6;
                         if (op_info & (O_C|O_A)) {
                             STAR = dscale[1][bcd_bin[ar & 0xf]];
                             STAR += dscale[0][bcd_bin[br & 0xf]];
                             STAR += ((ar & 0x30) >> 4) * 1000;
                             ix = (br & 0x30) >> 4;
                         }
                         break;
                     case 3:/* Has to be address, check if goes to C or AB */
                        /* BA */
                        /* 00     0-3999 */
                        /* 01     4000-7999 */
                        /* 10     8000-11999 */
                        /* 11    12000-15999 */
                         state = 4;
                         if (op_info & O_X)
                             XR |= br;
                         if (op_info & (O_C|O_A)) {  /* hundreds */
                             STAR += bcd_bin[br & 0xf];
                             STAR += ((br & 0x30) >> 4) * 4000;
                             if (ix != 0) {
                                 int    a;
                                 /* do indexing */
                                 ix = (ix * 5) + 82;
                                 a = dscale[1][bcd_bin[M[ix] & 0xf]];
                                 a += dscale[0][bcd_bin[M[ix+1] & 0xf]];
                                 a += bcd_bin[M[ix+2] & 0xf];
                                 a += dscale[2][((M[ix] & 060) >> 4)];
                                 a += ((M[ix+2] & 060) >> 4) * 4000;
                                 STAR += a;
                                 STAR %= 16000;
                                 sim_interval -= 3;
                             }
                         }
                         if (op_info & O_C)
                             CAR = STAR;
                         if (op_info & O_A) {
                             AAR = STAR;
                             if (op_info & O_DBL) {
                                if (op_info & O_D)
                                     DAR = AAR;
                                BAR = AAR;
                             }
                         }
                         break;
                     case 4:    /* Could be either B address or operand. */
                         state = 5;
                         ar = br;
                         break;
                     case 5:    /* Has to be B address */
                         state = 6;
                         /* ten thousand, thousand */
                         if (op_info & (O_B|O_D)) {
                             STAR = dscale[1][bcd_bin[ar & 0xf]];
                             STAR += dscale[0][bcd_bin[br & 0xf]];
                             STAR += ((ar & 0x30) >> 4) * 1000;
                             ix = (br & 0x30) >> 4;
                         }
                         if ((op_info & O_M) == 0)
                             op_mod = 0;
                         break;
                     case 6:    /* Has to be B address */
                         state = 7;
                         /* hundreds */
                         if (op_info & (O_B|O_D)) {
                             STAR += bcd_bin[br & 0xf];
                             STAR += ((br & 0x30) >> 4) * 4000;
                             if (ix != 0) {
                                 int    a;
                                 /* do indexing */
                                 ix = (ix * 5) + 82;
                                 a = dscale[1][bcd_bin[M[ix] & 0xf]];
                                 a += dscale[0][bcd_bin[M[ix+1] & 0xf]];
                                 a += bcd_bin[M[ix+2] & 0xf];
                                 a += dscale[2][((M[ix] & 060) >> 4)];
                                 a += ((M[ix+2] & 060) >> 4) * 4000;
                                 STAR += a;
                                 STAR %= 16000;
                                 sim_interval -= 3;
                             }
                         }
                         if (op_info & O_D)
                             DAR = STAR;
                         if (op_info & O_B)
                             BAR = STAR;
                         break;
                     case 7:    /* Has to be modifier */
                         state = 8;
                         ar = br;
                         break;
                     case 8:    /* Too long */
                         if (op != OP_NOP && op != CHR_B)
                             reason = STOP_NOWM;
                         state = 9;
                         break;
                     }
                     /* Some instructions don't have to have word marks */
                     if (op == OP_SWM && state == 7)
                        break;
                     if (op == CHR_B && state == 5 && ar == CHR_ABLANK)
                        break;
                     if (op == CHR_B && state == 9)
                        break;
                }
                if (reason != 0)
                   goto check_prot;
            }

            if (hst_lnt)        /* History enabled? */
                hst[hst_p].inst[i++] = WM;      /* Term hist ins */

            jump = 0;
            if (CPU_MODEL == 1) {

                if (hst_lnt) {  /* History enabled? */
                     hst[hst_p].astart = AAR;
                     hst[hst_p].bstart = BAR;
                     hst[hst_p].inst[state] = WM;
                }

                /* Translate instruction from 1401 to 1410 */
                switch(op) {
                case CHR_B: /* Fix branch to correct kind */
                            switch(state) {
                            case 8: op_mod = ar;        /* B ddd iii c */
                            case 7:                     /* B ddd iii */
                            case 1:                     /* B */
                                    op = OP_BCE;
                                    break;
                            case 4: ar = CHR_ABLANK;    /* B ddd */
                            case 2:                     /* B c ?? */
                            default:                    /* B ddd c */
                            case 5: op = OP_B; op_mod = ar; break;
                            }
                            break;
                case CHR_U:
                case CHR_W:
                case CHR_V: if (state == 8 || state == 2 || state == 5)
                                op_mod = ar;
                            break;
                case CHR_K:
                case CHR_F:
                            temp = (op == CHR_K)?010100:010200;
                            if (state == 2 || state == 5)
                                op_mod = ar;
                            temp |= op_mod;
                            while((t = chan_cmd(temp, (IO_CTL<<8), 0))
                                 == SCPE_BUSY);
                            if (t != SCPE_OK) {
                                t = (temp >> 6) & 07;
                                io_flags &= ~t;
                            }
                            if (state == 4 || state == 5)
                                jump = 1;
                            op = OP_NOP;
                            break;

                            /* Translate Move into 1410 move */
                case CHR_M: if (op_info & O_X) {
                                chan_io_status[1] = 0;
                                op_mod = ar;
                            } else {
                                op = OP_MOV;
                                op_mod = CHR_C;
                            }
                            break;
                case CHR_L: if (op_info & O_X) {
                                chan_io_status[1] = 0;
                                op_mod = ar;
                            } else {
                                op = OP_MOV;
                                op_mod = CHR_X;
                            }
                            break;
                case CHR_D: op = OP_MOV; op_mod = CHR_1; break;
                case CHR_P: op = OP_MOV; op_mod = CHR_DOT; break;
                case CHR_Y: op = OP_MOV; op_mod = CHR_2; break;

                           /* Handle 1401 I/O opcodes */
                case CHR_1: /* Reader */
                case CHR_2: /* Print */
                case CHR_3: /* Print and read */
                case CHR_4: /* Punch */
                case CHR_5: /* Read and Punch */
                case CHR_6: /* Print and Punch */
                case CHR_7: /* Print,Read and Punch */
                            op_mod = op;
                            op = OP_NOP;
                            while(op_mod != 0 || chwait != 0) {
                                while (chan_active(1) && reason == 0) {
                                    sim_interval = 0;
                                    reason = sim_process_event();
                                    chan_proc();
                                }
                                if (chwait != 0) {
                                    BAR = caddr[1];
                                    if (hst_lnt)        /* History enabled? */
                                        hst[hst_p].bend = BAR;
                                    chwait = 0;
                                }

                                /* Stop if something wrong */
                                if (reason != 0)
                                    break;
                                /* Convert to channel instruction */
                                if (op_mod & 02) {      /* Print line */
                                        temp = 010200;
                                        if ((state == 2 || state == 5)
                                                && ar == CHR_LPARN)
                                            temp |= 1;
                                        else
                                            temp |= 012;
                                        t = (IO_WRS << 8);
                                        BAR = 201;
                                } else if (op_mod & 01) { /* Reader */
                                        temp = 010100;
                                        t = (IO_RDS << 8);
                                        BAR = 1;
                                } else if (op_mod & 04) { /* Punch */
                                        temp = 010400;
                                        t = (IO_WRS << 8);
                                        BAR = 101;
                                } else
                                        break;

                                /* Try to start command */
                                switch (chan_cmd(temp, t, BAR)) {
                                case SCPE_OK:
                                        t = (temp >> 6) & 07;
                                        io_flags &= ~t;
                                        op_mod &= ~t;
                                        chwait = 01;
                                        if (chan_stat(1, CHS_EOF))
                                            io_flags |= (t << 3) | t;
                                        break;
                                case SCPE_BUSY:
                                        sim_interval = 0;
                                        reason = sim_process_event();
                                        chan_proc();
                                        break;
                                case SCPE_NODEV:
                                case SCPE_IOERR:
                                        chan_io_status[1] = 01;
                                        io_flags |= (temp >> 6) & 07;
                                        op_mod = 0;     /* Abort */
                                        break;
                                }
                            };
                            /* Handle branching */
                            if (state == 4 || state == 5)
                                jump = 1;
                            break;

                case CHR_8: /* Start read feed */
                case CHR_9: /* Start punch feed */
                            /* Not supportable by sim */
                            op = OP_NOP;
                            break;

                case CHR_EQ:    /* Handle modify address here */
                            op = OP_NOP;
                            DAR = BAR;          /* Save for later */
                            ar = ReadP(AAR);
                            br = ReadP(BAR);
                            sim_interval -= 2;
                            ix = (ar & 060) + (br & 060); /* Add zone */
                            ar = bcd_bin[br & 017] + bcd_bin[ar & 017];
                            cy = ar > 9;
                            WriteP(BAR, (br & WM) | (ix & 060) | bin_bcd[ar]);
                            DownReg(AAR);
                            DownReg(BAR);
                            ar = ReadP(AAR);
                            br = ReadP(BAR);
                            sim_interval -= 2;
                            ar = bcd_bin[br & 017] + bcd_bin[ar & 017] + cy;
                            cy = ar > 9;
                            WriteP(BAR, (br & (WM | 060)) | bin_bcd[ar]);
                            DownReg(AAR);
                            DownReg(BAR);
                            ar = ReadP(AAR);
                            br = ReadP(BAR);
                            sim_interval -= 2;
                            ix = (ar & 060) + (br & 060); /* Add zone */
                            ar = bcd_bin[br & 017] + bcd_bin[ar & 017] + cy;
                            if (ar > 9)
                                ix += 020;
                            WriteP(BAR, (br & WM) | (ix & 060) | bin_bcd[ar]);
                            DownReg(AAR);
                            if (ix & 0100) { /* Carry out of low zone */
                                BAR = DAR;              /* Restore */
                                br = ReadP(BAR);
                                ix = (br & 060) + 020;  /* Add zone */
                                WriteP(BAR, (br & (WM|017)) | (ix & 060));
                                sim_interval--;
                            }
                            DownReg(BAR);
                            break;
                case CHR_Q:     /* Handle SAR here */
                            BAR = AAR;  /* Copy AAR to BAR */
                case CHR_H:     /* Handle SBR here */
                            op = OP_NOP;/* done and at WM, so skip rest */
                            if (state > 2)
                                AAR = CAR;
                            temp = BAR % 1000;  /* Compute base address */
                            i = (BAR - temp) / 1000;
                            ch = temp % 10;
                            temp /= 10;
                            ch = bin_bcd[ch] | ((i & 014) << 2);
                            ReplaceMask(AAR, ch, 077);
                            sim_interval--;
                            DownReg(AAR);
                            ch = temp % 10;
                            temp /= 10;
                            ch = bin_bcd[ch];
                            ReplaceMask(AAR, ch, 077);
                            sim_interval--;
                            DownReg(AAR);
                            ch = temp;
                            ch = bin_bcd[ch] | ((i & 03) << 4);
                            ReplaceMask(AAR, ch, 077);
                            sim_interval--;
                            DownReg(AAR);
                            break;
                default:
                           /* no change needed */
                           break;
                }
            } else {
                if (fault)
                    goto check_prot;

                /* Check instruction length */
                switch(op) {
                case OP_S:
                case OP_A:
                case OP_ZS:
                case OP_ZA:
                case OP_M:
                case OP_D:
                case OP_C:
                case OP_CS:
                case OP_SWM:
                case OP_CWM:
                case OP_MSZ:
                case OP_E:
                    /* Valid forms */
                    /* Op */
                    /* Op AAAAA */
                    /* Op AAAAA BBBBB */
                        if (state != 1 && state != 6 && state != 11)
                            reason = STOP_INVLEN;
                        break;
                case OP_BCE:
                case OP_BBE:
                case OP_BWE:
                case OP_MOV:
                case OP_T:
                    /* Valid forms */
                    /* Op */
                    /* Op mod */
                    /* Op AAAAA */
                    /* Op AAAAA mod */
                    /* Op AAAAA BBBBB */
                    /* Op AAAAA BBBBB mod */
                       /* Check for modifier */
                       if (state == 2 || state == 7 || state == 12) {
                            op_mod = ar;
                            break;
                       }

                      /* Make sure len correct */
                       if (state != 1 && state != 6 && state != 11)
                            reason = STOP_INVLEN;
                       break;
                case OP_IO1:
                case OP_IO2:
                case OP_IO3:
                case OP_IO4:
                    /* Not in protected mode */
                     if (prot_enb || reloc) {
                         reason = STOP_PROG;
                         break;
                     }
                case OP_STS:
                    /* Not in protected mode */
                     if (prot_enb) {
                         reason = STOP_PROG;
                         break;
                     }

                case OP_PRI:
                case OP_B:
                case OP_SAR:
                case OP_FP:
                    /* Valid forms */
                    /* Op */
                    /* Op mod */
                    /* Op AAAAA */
                    /* Op AAAAA mod */
                       /* Check for modifier */
                       if (state == 2 || state == 7) {
                            op_mod = ar;
                            break;
                       }

                      /* Make sure len correct */
                       if (state != 1 && state != 6)
                            reason = STOP_INVLEN;
                       break;
                case OP_H:
                    /* Not in protected mode */
                     if (prot_enb || reloc) {
                         reason = STOP_PROG;
                         break;
                     }

                    /* Valid forms */
                    /* Op */
                    /* Op AAAAA */
                       if (state != 1 && state != 6)
                            reason = STOP_INVLEN;
                       break;
                case OP_UC:
                    /* Not in protected mode */
                     if (prot_enb || reloc) {
                         reason = STOP_PROG;
                         break;
                     }

                    /* Valid forms */
                    /* Op xxx mod */
                       /* Check for modifier */
                       if (state == 7) {
                            op_mod = ar;
                            break;
                       }

                      /* Make sure len correct */
                       if (state != 7)
                            reason = STOP_INVLEN;
                       break;
                case OP_CC1:
                case OP_CC2:
                case OP_SSF1:
                case OP_SSF2:
                    /* Not in protected mode */
                     if (prot_enb || reloc) {
                         reason = STOP_PROG;
                         break;
                     }

                    /* Valid forms */
                    /* Op mod */
                       /* Check for modifier */
                       if (state == 2) {
                            op_mod = ar;
                            break;
                       }

                      /* Make sure len correct */
                       reason = STOP_INVLEN;
                       break;

                case OP_RD:
                case OP_RDW:
                    /* Not in protected mode */
                     if (prot_enb || reloc) {
                         reason = STOP_PROG;
                         break;
                     }

                    /* Valid forms */
                    /* Op xxx mod */
                    /* Op xxx BBBBB mod */
                       /* Check for modifier */
                       if (state == 7 || state == 12 ) {
                            op_mod = ar;
                            break;
                       }

                       reason = STOP_INVLEN;
                       break;
                case OP_NOP:
                       break;
                }

                if (hst_lnt) {  /* History enabled? */
                    hst[hst_p].astart = AAR;
                    hst[hst_p].bstart = BAR;
                    if (op_info & O_M &&
                          (state == 1 || state == 6 || state == 11)) {
                         hst[hst_p].inst[state] = op_mod;
                         hst[hst_p].inst[state+1] = WM;
                    }
                }

                /* Handle fault */
                if (reason != 0) {
                    goto check_prot;
                }

                /* Check to see if we should interupt */
                if (cpu_unit.flags & OPTION_PRIO && (pri_enb || timer_enable)) {
                    int irq = inquiry;
                    int     ok_irq = 0;
                    for(i = 1; i < NUM_CHAN; i++ ) {
                        if ((chan_io_status[i] & 0300) == 0300 &&
                            chan_irq_enb[i])
                            irq = 1;
                        if (chan_test(i, SNS_ATTN1))
                            irq = 1;
                        if (urec_irq[i])
                            irq = 1;
                    }


                    if (irq || (timer_enable && timer_irq == 1)) {
                    /* Check if we can interupt this opcode */
                        switch(op) {
                        case OP_S:
                        case OP_A:
                        case OP_ZS:
                        case OP_ZA:
                        case OP_M:
                        case OP_D:
                        case OP_SWM:
                        case OP_CWM:
                        case OP_MOV:
                        case OP_MSZ:
                        case OP_E:
                        case OP_C:
                        case OP_CS:
                             if (state > 10)
                                ok_irq = 1;
                             break;
                        case OP_T:
                        case OP_BCE:
                        case OP_BBE:
                        case OP_BWE:
                             if (state > 11)
                                ok_irq = 1;
                             break;
                        case OP_IO1:
                        case OP_IO2:
                        case OP_IO3:
                        case OP_IO4:
                             if (op_mod != 0)
                                 break;
                        case OP_B:
                             if (state > 6)
                                ok_irq = 1;
                             break;
                        case OP_SAR:
                        case OP_H:
                        case OP_NOP:
                        case OP_RD:
                        case OP_RDW:
                        case OP_CC1:
                        case OP_CC2:
                        case OP_SSF1:
                        case OP_SSF2:
                        case OP_UC:
                        case OP_PRI:
                        case OP_STS:
                        case OP_FP:
                             break;
                        }

                        if (ok_irq) {
                            sim_debug(DEBUG_PRIO, &cpu_dev, "Irq IAR=%d\n",IAR);
                            prot_enb = reloc = 0;
                            if (pri_enb && irq) {
                                IAR = temp;
                                AAR = 101;
                                op = OP_PRI;
                                op_mod = CHR_X; /* X */
                                if (hst_lnt) {  /* History enabled? */
                                    hst[hst_p].inst[0] = op;
                                    hst[hst_p].inst[1] = op_mod;
                                    hst[hst_p].inst[2] = WM;
                                }
                            } else if (timer_enable && timer_irq == 1) {
                                IAR = temp;
                                AAR = 301;
                                timer_irq = 2;
                                op = OP_PRI;
                                op_mod = CHR_X; /* X */
                                if (hst_lnt) {  /* History enabled? */
                                    hst[hst_p].inst[0] = op;
                                    hst[hst_p].inst[1] = op_mod;
                                    hst[hst_p].inst[2] = WM;
                                }
                            }
                        }
                    }
                }
            }


            /* Execute instructions */
            switch(op) {
            case OP_S:
                /* Check if over the top */
                ValidAddr(AAR);
                ValidAddr(BAR);
                reason = do_addsub(1);
                break;

            case OP_A:
                /* Check if over the top */
                ValidAddr(AAR);
                ValidAddr(BAR);
                reason = do_addsub(0);
                break;

            case OP_M:
                /* Check if over the top */
                ValidAddr(AAR);
                ValidAddr(BAR);
                reason = do_mult();
                break;

            case OP_D:
                /* Check if over the top */
                ValidAddr(AAR);
                ValidAddr(BAR);
                reason = do_divide();
                break;

            case OP_ZS:
                /* Check if over the top */
                ar = ReadP(AAR);
                DownReg(AAR);
                if ((ar & 060) == 040)
                    ar |= 060;
                else {
                    ar &= 017|WM;
                    ar |= 040;
                }
                goto zadd;

            case OP_ZA:
                /* Check if over the top */
                ar = ReadP(AAR);
                DownReg(AAR);
                if ((ar & 060) != 040)
                    ar |= 060;
                else {
                    ar &= 017|WM;
                    ar |= 040;
                }
            zadd:
                zind = 1;
                /* Copy digits until A or B word mark */
                br = ReadP(BAR) & WM;
                STAR = BAR;
                DownReg(BAR);
                sim_interval -= 4;
                while (1) {
                    WriteP(STAR, br | bin_bcd[bcd_bin[ar & 0xf]] | (ar & 060));
                    if (bcd_bin[ar & 0xf] != 0) /* Update zero flag */
                        zind = 0;
                    if (br & WM)
                        break;
                    sim_interval -= 4;
                    if (ar & WM)
                        ar = 10|WM;
                    else {
                        ar = ReadP(AAR) & (WM|017);
                        DownReg(AAR);
                    }
                    br = ReadP(BAR) & WM;
                    STAR = BAR;
                    DownReg(BAR);
                }
                break;

            case OP_SAR:
                if ((CAR & AMASK) < 5 || !MEM_ADDR_OK(CAR)) {
                    reason = STOP_INVADDR;
                    break;
                }
                switch(op_mod) {
                case CHR_A: temp = AAR;
                            if (reloc && low_addr >= 0 && temp & BBIT) {
                                if (temp < low_addr)
                                    temp += 100000 - low_addr;
                                else
                                    temp -= low_addr;
                            }
                         break;                         /* A */
                case CHR_B: temp = BAR;
                            if (reloc && low_addr >= 0 && temp & BBIT) {
                                if (temp < low_addr)
                                    temp += 100000 - low_addr;
                                else
                                    temp -= low_addr;
                            }
                         break;                          /* B */
                case CHR_E: temp = caddr[1]; break;     /* E */
                case CHR_F: temp = caddr[2]; break;     /* F */
                case CHR_G: temp = caddr[3]; break;     /* G */
                case CHR_H: temp = caddr[4]; break;     /* H */
                case CHR_T:                             /* T */
                        {
                         time_t        curtim;
                         struct tm    *tptr;

                             temp = 99999;
                             curtim = sim_get_time(NULL);/* get time */
                             tptr = localtime(&curtim);  /* decompose */
                             if (tptr != NULL && tptr->tm_sec != 59) {
                                  /* Convert minutes to 100th hour */
                                  temp = time_digs[tptr->tm_min % 6];
                                  temp += 10 * (tptr->tm_min / 6);
                                  temp += 100 * tptr->tm_hour;
                             }
                       }
                       break;
                default:  temp = 0;   break;
                }
                temp &= AMASK;
                for(i = 0; i<= 4; i++) {
                   sim_interval --;
                   ch = temp % 10;
                   temp /= 10;
                   if (ch == 0)
                      ch = 10;
                   ReplaceMask(CAR, ch, 017);
                   DownReg(CAR);
                }
                break;

            case OP_SWM:
                SetBit(AAR, WM);
                DownReg(AAR);
                SetBit(BAR, WM);
                DownReg(BAR);
                sim_interval -= 4;
                break;

            case OP_CWM:
                ClrBit(AAR, WM);
                DownReg(AAR);
                ClrBit(BAR, WM);
                DownReg(BAR);
                sim_interval -= 4;
                break;

            case OP_CS:
                /* Clear memory until BAR equal xxx99 */
                do {
                    WriteP(BAR, 0);
                    sim_interval -= 2;
                    if ((BAR & AMASK) == 0) {
                        if (CPU_MODEL == 1)
                           BAR = 15999;
                        else
                           BAR = MAXMEMSIZE-1;
                        break;
                    }
                    BAR--;
                } while (((BAR & AMASK) % 100) != 99);
                /* If two address, do branch */
                if (state > 6)
                   jump = 1;
                break;

            case OP_H:
                if (state > 2)
                   jump = 1;
                reason = STOP_HALT;
                break;

            /* Treat invalid op as a NOP */
            default:
                reason = STOP_UUO;
                /* Fall through */

            case OP_NOP:
                /* Skip until next word mark */
                while((FetchP(IAR) & WM) == 0 && fault == 0) {
                    sim_interval -= 2;
                    UpReg(IAR);
                }
                break;

            case OP_MOV:

                /* Set terminate to false */
                sign = 1;
                while(sign) {
                    sim_interval -= 4;
                    ar = ReadP(AAR);
                    STAR = BAR;
                    br = ReadP(BAR);

                    /* Adjust addresses. */
                    if (op_mod & 010) {
                        UpReg(AAR);
                        UpReg(BAR);
                    } else {
                        DownReg(AAR);
                        DownReg(BAR);
                    }

                    switch(op_mod & 070) {
                    case 020:   /* A, No B or 8 bit */
                        if (ar & WM)    /* 1st WM - A-field */
                           sign = 0;
                        break;
                    case 040:   /* B, no 8 or A bit */
                        if (br & WM)    /* 1st WM - B-field */
                           sign = 0;
                        break;
                    case 010:   /* No A or B, 8 bit */
                    case 060:   /* B and A bit, no 8 bit */
                        if (ar & WM || br & WM)      /* 1st WM - A or B-field */
                           sign = 0;
                        break;
                    case 030:   /* A & 8 bit, No B */
                        if ((ar & 077) == CHR_RM)      /* 1st RM - A-field */
                           sign = 0;
                        break;
                    case 050:   /* B and 8, no A bit */
                        if ((ar & 0277) == (CHR_GM|WM)) /* 1st GM,WM - A-field*/
                           sign = 0;
                        break;
                    case 070:   /* B and A and 8 bit */
                          /* 1st RM or GM,WM - A-field */
                        if ((ar & 077) == CHR_RM || (ar & 0277) == (CHR_GM|WM))
                           sign = 0;
                        break;
                    case 000:   /* No A or B or 8 bit */
                        sign = 0;       /* After one position */
                        break;
                    }
                   /* Copy bits */
                    if (op_mod & 001) {
                        br &= ~0xf;
                        br |= ar & 0xf;
                    }
                    if (op_mod & 002) {
                        br &= ~0x30;
                        br |= ar & 0x30;
                    }
                    if (op_mod & 004) {
                        br &= ~WM;
                        br |= ar & WM;
                    }
                    /* Restore value */
                    WriteP(STAR, br);
                }
                break;

            case OP_MSZ:
                ar = ReadP(AAR);        /* First character, no zone, force WM */
                WriteP(BAR,  (ar & 017) |WM);
                DownReg(AAR);
                DownReg(BAR);
                t = 1;                  /* Suppress zeros. */
                sim_interval -= 4;
                while ((ar & WM) == 0) { /* Copy record */
                    ar = ReadP(AAR);
                    WriteP(BAR, ar & 077);
                    sim_interval -= 4;
                    DownReg(AAR);
                    DownReg(BAR);
                }
                /* Scan backward from end to Word Mark suppressing zeros */
                UpReg(BAR);
                br = ReadP(BAR);                /* Forward one */
                sim_interval -= 2;
                while(1) {
                    ch = br & 077;
                    if (ch > 0 && ch < 10)
                        t = 0;
                    else if (ch == 0 || ch == 10 || ch == CHR_COM)
                        ch = (t)?0:ch;           /* B blank, zero, comma */
                    else if (ch != CHR_MINUS && ch != CHR_DOT)
                        t = 1;                  /*  B - or . */
                    WriteP(BAR, ch);
                    UpReg(BAR);
                    if (br & WM)
                        break;
                    br = ReadP(BAR);            /* Forward one */
                }
                break;

            case OP_C:
                cind = 2;       /* Set equal */
                do {
                   /* scan digits until A or B word mark */
                    ar = ReadP(AAR);
                    br = ReadP(BAR);
                    sim_interval -= 4;
                    sign = cmp_order[br & 077] - cmp_order[ar & 077];
                    if (sign > 0)
                        cind = 4;
                    else if (sign < 0)
                        cind = 1;
                    DownReg(AAR);
                    DownReg(BAR);
                } while ((br & WM) == 0 && (ar & WM) == 0);
                if ((br & WM) == 0 && (ar & WM))
                    cind = 4;
                break;

            case OP_T:
                /* Check opcode */
                if ((op_mod & 070) != 0) {
                    reason = STOP_UUO;
                    break;
                }
                cind = 2;
                qsign = 1;      /* Set unit/body */
                CAR = AAR;
                ar = ReadP(AAR);
                DownReg(AAR);
                while(1) {
                    /* Scan digits until A or B word mark */
                    sim_interval -= 4;
                    ZeroAddr(AAR);
                    br = ReadP(BAR);
                    DownReg(BAR);
                    if (qsign) {
                        sign = cmp_order[br & 077] - cmp_order[ar & 077];
                        if (sign > 0)
                            cind = 4;
                        else if (sign < 0)
                            cind = 1;
                    }
                    /* Hit end of search argument */
                    if (ar & WM) {
                        if (cind & op_mod)              /* Check if match */
                            break;
                        if (br & WM) {
                            AAR = CAR;
                            ar = ReadP(AAR);
                            DownReg(AAR);
                            qsign = 1;  /* Set unit/body */
                            cind = 2;
                        } else
                            qsign = 0;
                    } else if (br & WM) {       /* Found end of table */
                        cind = 4;
                        break;
                    } else {
                        ar = ReadP(AAR);
                        DownReg(AAR);
                    }
                }
                break;

            case OP_E:
                cy = 0x10;      /* latchs */
                                /* 1 Supress zero latch */
                                /* 2 Decimal latch */
                                /* 4 * Fill latch */
                                /* 8 $ Fill latch */
                                /* 0x10 Unit latch */
                                /* 0x20 Body latch */
                                /* 0x40 Ext latch */
                ar = ReadP(AAR);
                DownReg(AAR);
                sim_interval -= 2;
                sign = (ar & 060) == 040;
                ch = ar & 017;
                /* First scan cycle */
                while (1) {
                    br = ReadP(STAR = BAR);
                    DownReg(BAR);
                    sim_interval -= 2;
                    if (cy & 0x40)
                        ch = br & 077;
                    switch (br & 077) {
                    case CHR_MINUS:     /* - */
                    case CHR_C: /* C */
                    case CHR_R: /* R */
                       if (sign || cy & 0x20) /* - or body */
                            WriteP(STAR, br & 077);
                       else
                            WriteP(STAR, 0);
                       break;
                    case CHR_COM:       /* , */
                       if (cy & 0x40)
                           WriteP(STAR, 0);
                       else
                           WriteP(STAR, br & 077);
                       break;
                    case CHR_PLUS:      /* & */
                       WriteP(STAR, 0);
                       break;
                    case CHR_DOL:       /* $ */
                    case CHR_STAR:      /* * */
                        if ((cy & 0x20) == 0) {         /* not body, skip */
                            WriteP(STAR, br & 077);
                            break;
                        }
                        if ((cy & 0xd) == 1) {  /* Set fill flag */
                            cy |= ((br & 077) == CHR_DOL)?0x8:0x4;
                        }
                    case CHR_0: /* 0 */
                        /* Supression off */
                        if ((br & 077) == CHR_0 && (cy & 1) == 0) {
                            ch |= WM;
                            cy |= 1;            /* Set on */
                        }
                    case CHR_ABLANK:    /* blank */
                        WriteP(STAR, ch);
                        if ((br & WM) == 0) {
                            if (ar & WM) {
                                cy &= ~0x70;    /* Set Ext  */
                                cy |= 0x40;
                             } else {
                                ar = ReadP(AAR);        /* Set Body */
                                DownReg(AAR);
                                ch = ar & 077;
                                cy &= ~0x70;
                                cy |= 0x20;
                             }
                        }
                        break;
                    default:
                        WriteP(STAR, br & 077); /* Clear word mark */
                        break;
                    }
                    if (br & WM)
                       break;
                 }
                 /* A */
                 /* If suppression off and first char not zero stop */
                 if ((cy & 0x1) == 0 && (ReadP(BAR) & 077) != CHR_0)
                      break;
                 UpReg(BAR);
                /* Do second scan */
                 while (1) {
                     br = ReadP(STAR = BAR);
                     UpReg(BAR);
                     sim_interval -= 2;
                     ch = br & 077;
                     switch (ch) {
                     case 1: case 2: case 3: case 4: case 5:
                     case 6: case 7: case 8: case 9:
                            cy &= ~1;   /* Turn off suppression latch */
                            break;
                     case CHR_COM:      /* , */
                            if ((cy & 3) == 2) {        /* Decimal suppress */
                                ch = (cy & 0x4)?CHR_STAR:0;     /* * or blank */
                            }
                     case CHR_0:        /* 0 */
                     case CHR_ABLANK:   /* blank */
                            if ((cy & 3) == 1) {        /* Supress, no dec */
                                ch = (cy & 0x4)?CHR_STAR:0;     /* * or blank */
                            }
                            break;
                     case CHR_DOT:      /* . */
                            if (cy & 1)
                                cy |= 2;                /* Set dec */
                     case CHR_MINUS:    /* - */
                            break;
                     default:
                            if ((cy & 0x3) == 0)
                                cy |= 1;                /* Set 0 if not dec */
                            break;
                    }
                    WriteP(STAR, ch);   /* Store char back */
                    if (br & WM)
                        break;
                 }
                 /* Dec not set & $ fill or zero and $ fill not set */
                 if ((cy & 0xA) == 0 || (cy & 0xB) == 2 ||
                                ((cy & 0xB) == 3 && ch == CHR_MINUS))
                     break;
                 DownReg(BAR);
                /* Third scan pass */
                 while(1) {
                     ch = ReadP(STAR = BAR) & 077;
                     DownReg(BAR);
                     sim_interval -= 2;
                     if (ch == 0) {
                        if (cy & 0x4) {
                            WriteP(STAR, CHR_STAR);     /* * */
                        } else if (cy & 0x8) {
                            WriteP(STAR, CHR_DOL);      /* $ */
                            break;      /* Stop after $ */
                        }
                     } else if (ch == CHR_0) {  /* 0 */
                        if (cy & 1) {
                           WriteP(STAR, (cy & 04)?CHR_STAR:0);  /* * */
                        }
                     } else if (ch == CHR_DOT) {        /* . */
                        if (cy & 1) {
                           WriteP(STAR, (cy & 04)?CHR_STAR:0);  /* * */
                           break;
                        }
                        if ((cy & 0xA) == 0xA)  /* Both . and $ set */
                            break;
                     }
                 }
                 break;

            case OP_B:
                switch(op_mod) {
                case CHR_ABLANK:        /* 1401 same */
                        jump = 1;
                        break;
                case CHR_Z:     /* Z Arith overflow */  /* 1401 same */
                        jump = oind;
                        oind = 0;
                        break;
                case CHR_S:     /* S   Equal */ /* 1401 same */
                        jump = (cind == 2);
                        break;
                case CHR_U:     /* U   High */  /* 1401 same */
                        jump = (cind == 4);
                        break;
                case CHR_T:     /* T   Low */   /* 1401 same */
                        jump = (cind == 1);
                        break;
                case CHR_SLSH:  /* /   High or Low */   /* 1401 same */
                        jump = (cind != 2);
                        break;
                case CHR_W:     /* W   Divide overflow */
                        jump = dind;
                        dind = 0;
                        break;
                case CHR_V:     /* V   Zero Balence */
                        jump = zind;
                        break;
                case CHR_X:     /* X floating point */
                        if ((cpu_unit.flags & OPTION_FLOAT) == 0)
                            reason = STOP_UUO;
                        jump = euind;
                        euind = 0;
                        break;
                case CHR_Y:     /* Y floating point */
                        if ((cpu_unit.flags & OPTION_FLOAT) == 0)
                            reason = STOP_UUO;
                        jump = eoind;
                        eoind = 0;
                        break;
                case CHR_K:     /* K   Tape indicator */
                        /* 1401 end of real */
                        if (CPU_MODEL == 1) {
                             jump = chan_stat(1, CHS_EOF|CHS_EOT);
                        } else if (tind) {
                            jump = 1;
                            tind = 0;
                        } else {
                            for(i = 1; i <= NUM_CHAN && jump == 0; i++)
                                 jump = chan_stat(i, STA_PEND);
                            if (jump)
                                sim_debug(DEBUG_CMD, &cpu_dev, "Tape Ind\n");
                        }
                        break;
                case CHR_Q:     /* Q   Inq req ch 1 */
                        jump = inquiry;
                        break;
                case CHR_STAR:  /* *   Inq req ch 2 */
                        break;
                case CHR_1:     /* 1   Overlap in Proc Ch 1 */
                        jump = ((chan_io_status[1] & 0300) == 0200) && 
                                chan_active(1);
                        break;
                case CHR_2:     /* 2   Overlap in Proc Ch 2 */
                        jump = ((chan_io_status[2] & 0300) == 0200) && 
                                chan_active(2);
                        break;
                case CHR_4:     /* 4   Channel 3 */
                        jump = ((chan_io_status[3] & 0300) == 0200) &&
                                chan_active(3);
                        break;
                case CHR_RPARN: /* )   Channel 4 */
                        jump = ((chan_io_status[4] & 0300) == 0200) &&
                                chan_active(4);
                        break;
                case CHR_9:     /* 9 Carriage 9 CH1 */ /* 1401 same */
                        jump = lpr_chan9[1];
                        break;
                case CHR_EXPL:  /* ! Carriage 9 CH2 */
                        /* 1401 punch error */
                        jump = lpr_chan9[2];
                        break;
                case CHR_R:     /* R Carriage Busy CH1 */ /* 1401 same */
                        /* Try to start command */
                        switch (chan_cmd(010200, IO_TRS << 8, 0)) {
                        case SCPE_BUSY:
                                jump = 1;
                                break;
                        case SCPE_OK:
                        case SCPE_NODEV:
                        case SCPE_IOERR:
                                break;
                        }
                        break;
                case CHR_L:     /* L Carriage Busy CH2 */
                        /* 1401 tape error */
                        if (CPU_MODEL == 1) {
                             jump = chan_stat(1, CHS_ERR);
                             break;
                        }
                        /* Try to start command */
                        switch (chan_cmd(020200, IO_TRS << 8, 0)) {
                        case SCPE_BUSY:
                                jump = 1;
                                break;
                        case SCPE_OK:
                        case SCPE_NODEV:
                        case SCPE_IOERR:
                                break;
                        }
                        break;
                case CHR_QUOT:  /* @   Cariage Overflow 12 CH 1 */
                        /* 1401 same */
                        jump = lpr_chan12[1];
                        break;
                case CHR_LPARN: /* sq  Cariage Overflow 12 CH 2 */
                        /* 1401 process check switch off */
                        jump = lpr_chan12[2];
                        break;
                case CHR_A:     /* 1401 sense switch A */
                        if (CPU_MODEL == 1) {
                            jump = (SW & 0x01) | (io_flags & 010);
                            io_flags &= ~010;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_B:     /* 1401 sense switch B */
                        if (CPU_MODEL == 1) {
                            jump = SW & 0x02;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_C:     /* 1401 sense switch C */
                        if (CPU_MODEL == 1) {
                            jump = SW & 0x04;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_D:     /* 1401 sense switch D */
                        if (CPU_MODEL == 1) {
                            jump = SW & 0x08;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_E:     /* 1401 sense switch E */
                        if (CPU_MODEL == 1) {
                            jump = SW & 0x10;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_F:     /* 1401 sense switch F */
                        if (CPU_MODEL == 1) {
                            jump = SW & 0x20;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_G:     /* 1401 sense switch G */
                        if (CPU_MODEL == 1) {
                            jump = SW & 0x40;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_QUEST: /* 1401 reader error */
                        if (CPU_MODEL == 1) {
                            jump = io_flags & 1;
                            io_flags &= ~01;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_RM: /* 1401 print error */
                        if (CPU_MODEL == 1) {
                            jump = io_flags & 2;
                            io_flags &= ~02;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                case CHR_I: /* 1401 punch error */
                        if (CPU_MODEL == 1) {
                            jump = io_flags & 4;
                            io_flags &= ~04;
                        } else {
                            reason = STOP_UUO;
                        }
                        break;
                }
                break;

            case OP_BCE:
                sim_interval -= 2;
                cind = 2;       /* Set equal */
                sign = cmp_order[ReadP(BAR) & ~WM] - cmp_order[op_mod];
                if (sign > 0)
                    cind = 4;
                else if (sign < 0)
                    cind = 1;
                if (cind == 2)
                    jump = 1;
                DownReg(BAR);
                break;

            case OP_BBE:
                sim_interval -= 2;
                if (ReadP(BAR) & op_mod)
                    jump = 1;
                DownReg(BAR);
                break;

            case OP_BWE:
                sim_interval -= 2;
                br = ReadP(BAR);
                if (((op_mod & 01) && (br & WM)) ||
                    ((op_mod & 02) && (br & 060) == (op_mod & 060)))
                    jump = 1;
                DownReg(BAR);
                break;

            case OP_RD:
            case OP_RDW:
                /* Decode operands */
                /* X1 digit 1 == channel 034 % non-overlap */
                /*          2 == channel 074 sq non-overlap */
                /*          3 == channel 072 ? non-overlap */
                /*          4 == channel 052 ! non-overlap */
                /*          1 == channel 014 @ overlap */
                /*          2 == channel 054 * overlap */
                /* X2 digit device */
                /*          1 == Reader 001 */
                /*          2 == Printer 002 */
                /*          4 == Punch 004 */
                /*          U == Tape BCD 024 */
                /*          B == Tape Binary 062 */
                /*          T == Console 023 */
                /*          F == Disk 066 */
                /*          K == Com 042 */
                /* X3 digit device option or unit number */
                /* op_mod   R == Read 051 */
                /*          $ == Read 053 ignore word/group */
                /*          W == Write 026 */
                /*          X == Write 027 ignore word/group */
                /*          Q == Nop 050 input */
                /*          V == Nop 025 output */
                switch ((XR >> 12) & 077) {
                case CHR_RPARN: ch = 011; break; /* %/( 1 - non-overlap */
                case CHR_LPARN: ch = 012; break; /* sq  2 - non-overlap */
                case CHR_QUEST: ch = 013; break; /* ?   3 - non-overlap */
                case CHR_EXPL:  ch = 014; break; /* !   4 - non-overlap */
                case CHR_QUOT:  ch = 001; break; /* @/' 1 - overlap */
                case CHR_STAR:  ch = 002; break; /* *   2 - overlap */
                case CHR_DOL:   ch = 003; break; /* $   3 - overlap */
                case CHR_EQ:    ch = 004; break; /* =   4 - overlap */
                default: ch = 0;
                        reason = STOP_IOCHECK;
                        break;
                }

                temp = ch << 12;
                if ((XR & 07700) == 06200) {
                   if ((XR & 017) != 10)
                      temp |= XR & 017;
                   temp |= 02420;
                } else if ((XR & 07700) == 02400) {
                   if ((XR & 017) != 10)
                      temp |= XR & 017;
                   temp |= 02400;
                } else {
                   temp |= XR & 07777;
                }


                switch(op_mod) {
                case CHR_R:   t = (IO_RDS << 8); break;          /* R */
                case CHR_DOL: t = (IO_RDS << 8) | 0100; break;   /* $ */
                case CHR_W:   t = (IO_WRS << 8); break;          /* W */
                case CHR_X:   t = (IO_WRS << 8) | 0100; break;   /* X */
                case CHR_Q:   t = (IO_TRS << 8); ch &= 07; break;        /* Q */
                case CHR_V:   t = (IO_TRS << 8) | 0100; ch &= 07; break; /* Y */
                case CHR_S:   t = (IO_TRS << 8); break;         /* S sense */
                case CHR_C:   t = (IO_CTL << 8); break;         /* C control */
                default: t = 0;
                        reason = STOP_UUO;
                        break;
                }
                if (reason != 0)
                   break;

                while (chan_active(ch & 07) && reason == 0) {
                    sim_interval = 0;
                    reason = sim_process_event();
                    chan_proc();
                }
                if (reason != 0)
                   break;

                if (op == OP_RDW)
                   t |= 0200;
                if ((ch & 010) == 0)
                   t &= ~0100;  /* Can't be overlaped */

                /* Try to start command */
                switch (chan_cmd(temp, t, BAR & AMASK)) {
                case SCPE_OK:
                        if (ch & 010) {
                           chan_io_status[ch & 07] = 0;
                           chwait = ch & 07;
                           chan_irq_enb[ch & 7] = 0;
                        } else {
                           chan_io_status[ch & 07] = IO_CHS_OVER;
                           chan_irq_enb[ch & 7] = 1;
                        }
                        sim_debug(DEBUG_CMD, &cpu_dev,
                           "%d %c on %o %o %s %c\n", IAR, sim_six_to_ascii[op],
                                ch & 07, temp,
                                (ch & 010)?"":"overlap",
                                sim_six_to_ascii[op_mod]);
                        break;
                case SCPE_BUSY:
                        sim_debug(DEBUG_CMD, &cpu_dev,
                           "%d %c Busy on %o %s %c %o\n", IAR,
                                sim_six_to_ascii[op], ch & 07,
                                (ch & 010)?"": "overlap",
                             sim_six_to_ascii[op_mod], chan_io_status[ch & 07]);
                        chan_io_status[ch & 07] = IO_CHS_BUSY;
                        break;
                case SCPE_NODEV:
                case SCPE_IOERR:
                        chan_io_status[ch & 07] = IO_CHS_NORDY;
                        break;
                }
                if (CPU_MODEL == 1)
                    chan_io_status[ch & 07] &= 0177;
                break;

            case OP_CC1:
                t = (IO_CTL << 8);
                temp = 010200 | op_mod;
                ch = 1;
        chan_io:
                switch (chan_cmd(temp, t, 0)) {
                case SCPE_OK:
                        chan_io_status[ch & 07] = 0000;
                        if (ch & 010)
                            chwait = (ch & 07) | 040;
                        chan_irq_enb[ch & 7] = 0;
                        break;
                case SCPE_BUSY:
                        chan_io_status[ch & 07] = IO_CHS_BUSY;
                        break;
                case SCPE_NODEV:
                case SCPE_IOERR:
                        chan_io_status[ch & 07] = IO_CHS_NORDY;
                        break;
                }
                break;

            case OP_CC2:
                t = (IO_CTL << 8);
                temp = 020200 | op_mod;
                ch = 2;
                goto chan_io;

            case OP_SSF1:
                t = (IO_CTL << 8);
                temp = 010100 | op_mod;
                ch = 1;
                goto chan_io;

            case OP_SSF2:
                t = (IO_CTL << 8);
                temp = 020100 | op_mod;
                ch = 2;
                goto chan_io;

            case OP_UC:
                switch ((XR >> 12) & 077) {
                case CHR_RPARN: ch = 011; break; /* %/) 1 - non-overlap */
                case CHR_LPARN: ch = 012; break; /* sq  2 - non-overlap */
                case CHR_QUEST: ch = 013; break; /* ?   3 - non-overlap */
                case CHR_EXPL:  ch = 014; break; /* !   4 - non-overlap */
                case CHR_QUOT:  ch = 001; break; /* @/' 1 - overlap */
                case CHR_STAR:  ch = 002; break; /* *   2 - overlap */
                case CHR_DOL:   ch = 003; break; /* $   3 - overlap */
                case CHR_EQ:    ch = 004; break; /* =   4 - overlap */
                default: ch = 0;
                        reason = STOP_IOCHECK;
                        break;
                }
                temp = ch << 12;
                if ((XR & 07700) != 02400 && (XR & 07700) != 06200) {
                   reason = STOP_UUO;
                   break;
                }
                if ((XR & 017) != 10)
                    temp |= XR & 017;
                temp |= 02400;
                t = 0;
                switch(op_mod) {
                case CHR_B:  t = (IO_BSR << 8); ch |= 010; break;
                case CHR_A:  t = (IO_SKR << 8); ch |= 010; break;
                case CHR_R:  t = (IO_REW << 8); ch |= 010; break;
                case CHR_GT: t = (IO_RUN << 8); ch |= 010; break;
                case CHR_E:  t = (IO_ERG << 8); ch |= 010; break;
                case CHR_M:  t = (IO_WEF << 8); break;
                default: t = 0; reason = STOP_UUO; break;
                }

                while (chan_active(ch & 07) && reason == 0) {
                    sim_interval = 0;
                    reason = sim_process_event();
                    chan_proc();
                }
                if (reason != 0)
                   break;
                /* For nop, set command done */
                if (t == 0) {
                    chan_io_status[ch & 07] = 0000;
                    break;
                }
                /* Issue command */
                switch (chan_cmd(temp, t, 0)) {
                case SCPE_OK:
                        chan_io_status[ch & 07] = 0000;
                        if (ch & 010) {
                            chwait = (ch & 07) | 040;
                        } else if (op_mod == CHR_M) {
                            chan_io_status[ch & 07] = IO_CHS_OVER;
                        }
                        chan_irq_enb[ch & 7] = 0;
                        sim_debug(DEBUG_CMD, &cpu_dev,
                           "%d UC on %o %o %s %c %o\n", IAR, ch & 07, temp,
                             (ch & 010)?"": "overlap",
                             sim_six_to_ascii[op_mod], chan_io_status[ch & 07]);

                        break;
                case SCPE_BUSY:
                        chan_io_status[ch & 07] = IO_CHS_BUSY;
                        break;
                case SCPE_NODEV:
                case SCPE_IOERR:
                        chan_io_status[ch & 07] = IO_CHS_NORDY;
                        break;
                }
                if (CPU_MODEL == 1)
                    chan_io_status[ch & 07] &= 0177;
                sim_interval -= 100;
                break;

            case OP_IO1:
                /* Wait for channel to finish before continuing */
                ch = 1;
        checkchan:
                chan_proc();
                if (chan_io_status[ch] & op_mod) {
                    jump = 1;
                }
                chan_io_status[ch] &= 077;
                sim_debug(DEBUG_CMD, &cpu_dev, "Check chan %d %o %x\n", ch,
                        chan_io_status[ch], chan_flags[ch]);
                break;

            case OP_IO2:
                ch = 2;
                goto checkchan;

            case OP_IO3:
                ch = 3;
                goto checkchan;

            case OP_IO4:
                ch = 4;
                goto checkchan;

            case OP_FP:
                if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                    reason = STOP_UUO;
                    break;
                }
                /* Check if over the top */
                ValidAddr(AAR);
                /* AAR pointes to exponent of FP */
                /* BAR point to FP register locations 280 - 299 */
                BAR = 299;
                if (hst_lnt)    /* History enabled? */
                    hst[hst_p].bstart = BAR;
                switch(op_mod) {
                case CHR_R:                /* R - Floating Reset Add */
                    /* Copy exponent to accumulator */
                    zind = 1;
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    if ((ar & 060) != 040)
                        ar |= 060;
                    else {
                        ar |= 040;
                        ar &= 057;
                    }
                    WriteP(BAR--, bin_bcd[bcd_bin[ar & 0xf]] | (ar & 060));
                    sim_interval -= 4;
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    WriteP(BAR--, bin_bcd[bcd_bin[ar & 0xf]] | (ar & (WM|060)));
                    /* Prepare to copy rest. */
                    br = ReadP(STAR = BAR--) & WM;
                    sim_interval -= 4;
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    while (1) {
                        WriteP(STAR, ar);
                        if ((ar & 0xf) != 10) /* Update zero flag */
                            zind = 0;
                        if (ar & WM)               /* Done yet? */
                            break;
                        if (BAR == 279)    /* Stop at lower limit */
                            break;
                        sim_interval -= 4;
                        ar = ReadP(AAR);
                        DownReg(AAR);
                        br = ReadP(STAR = BAR--) & WM;
                    }
                    SetBit(STAR, WM);
                    break;

                case CHR_L:                  /* L - Floating store */
                    /* Copy two digit exponent */
                    br = ReadP(BAR--);
                    if ((br & 060) != 040)
                        br |= 060;
                    else {
                        br &= 017|WM;
                        br |= 040;
                    }
                    WriteP(AAR, bin_bcd[bcd_bin[br & 0xf]] | (br & 060));
                    DownReg(AAR);
                    br = ReadP(BAR--);
                    WriteP(AAR, bin_bcd[bcd_bin[br & 0xf]] | (br & (WM|060)));
                    DownReg(AAR);
                    sim_interval -= 4;
                    /* Copy digits until A or B word mark */
                    zind = 1;
                    ar = ReadP(STAR = AAR) & WM;
                    DownReg(AAR);
                    br = ReadP(BAR--);
                    while (1) {
                        WriteP(STAR, br);
                        if ((br & 0xf) != 10)   /* Update zero flag */
                            zind = 0;
                        if (br & WM || ar & WM || BAR == 279)
                            break;
                        sim_interval -= 4;
                        ar = ReadP(STAR = AAR) & WM;
                        DownReg(AAR);
                        br = ReadP(BAR--);
                    }
                    SetBit(STAR, WM);
                    break;

                case CHR_S:                  /* S - Floating sub */
                case CHR_A:                  /* A - Floating add */
                    zind = 1;
                    /* Compute A exponent */
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    qsign = (ar & 060) == 040;
                    cy = bcd_bin[ar & 0xf];
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    cy += 10 * bcd_bin[ar & 0xf];
                    if (qsign)
                        cy = -cy;
                    /* Compute B exponent */
                    br = ReadP(BAR--);
                    sign = (br & 060) == 040;
                    temp = bcd_bin[br & 0xf];
                    br = ReadP(BAR--);
                    temp += 10 * bcd_bin[br & 0xf];
                    if (sign)
                        temp = -temp;
                    sim_interval -= 10;
                    temp -= cy;
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    sign = (ar & 060) == 040;
                    if (temp == 0)      /* Same go add */
                        goto fadd;
                    if (temp > 17) {    /* Normalize */
                        /* Move BAR to just below WM */
                        do {
                            br = ReadP(BAR--);
                        } while((br & WM) == 0);
                        goto fnorm;
                    }
                    if (temp < -17) {   /* Copy A to ACC */
                fcopy:
                        BAR = 299;      /* Copy exponent */
                        if (cy < 0)
                             cy = -cy;
                        WriteP(BAR--, bin_bcd[cy % 10] | ((qsign)? 040: 060));
                        WriteP(BAR--, bin_bcd[cy / 10] | WM);
                        br = ReadP(STAR = BAR--) & WM;
                        /* Flip sign if doing subtract */
                        if (op_mod == CHR_S) {
                           ar &= WM|017;
                           ar |= sign?060:040;
                        }
                        while (1) {
                            WriteP(STAR, ar);
                            if ((ar & 0xf) != 10)       /* Update zero flag */
                                zind = 0;
                            if (br & WM || ar & WM)
                                break;
                            if (BAR == 280)
                                SetBit(BAR, WM);
                            sim_interval -= 4;
                            ar = ReadP(AAR);
                            DownReg(AAR);
                            br = ReadP(STAR = BAR--) & WM;
                        }
                        SetBit(STAR, WM);
                        goto fnorm;
                    }

                    if (temp > 0) {     /* Shift A */
                        while (temp-- > 0 && (ar & WM) == 0) {
                            sim_interval -= 2;
                            ar = ReadP(AAR);
                            DownReg(AAR);
                        }
                        if (ar & WM && temp != 0) {
                            /* Move BAR to just below WM */
                            while((ReadP(BAR--) & WM) == 0);
                            goto fnorm;
                        }
                    } else {            /* Shift B */
                        ix = br = ReadP(BAR--);
                        while (temp++ < 0) {
                            if (br & WM || BAR == 279)
                                break;
                            sim_interval -= 2;
                            br = ReadP(BAR--);
                        }
                        if (br & WM && temp < 0) {
                           /* Copy exponent to ACC first */
                            BAR = 299;
                            if (cy < 0)
                                cy = -cy;
                            WriteP(BAR--, bin_bcd[cy % 10] | ((qsign)? 040: 060));
                            WriteP(BAR--, bin_bcd[cy / 10] | WM);
                            sim_interval -= 4;
                            goto fcopy;
                        }
                        DAR = 297;
                        /* Copy B up */
                        while(1) {
                            WriteP(DAR--, (br & 017) | (ix & 060));
                            ix = 0;
                            if (br & WM || BAR == 279)
                                break;
                            br = ReadP(BAR--);
                        }
                        /* Zero fill new locations */
                        while(DAR != BAR)
                            ReplaceMask(DAR--, 10, 077);
                        /* Copy A exponent */
                        BAR = 299;
                        if (cy < 0)
                            cy = -cy;
                        WriteP(BAR--, bin_bcd[cy % 10] | ((qsign)? 040: 060));
                        WriteP(BAR--, bin_bcd[cy / 10] | WM);
                    }
                fadd:
                    if (op_mod == CHR_S)
                        sign ^= 1;      /* Change sign for subtract oper */
                    zind = 1;
                    DAR = BAR;
                    sim_interval -= 2;
                    if ((ReadP(297) & 060) == 040)
                        sign ^= 1;
                    cy = sign;
                    br = ReadP(STAR = BAR--);

                    ix = 0;
                    /* Add until word mark on A or B */
                    while(1) {
                        ix |= ar & WM;
                        ch = bcd_bin[ar & 0xf];
                        ch = bcd_bin[br& 0xf] + ((sign)? (9 - ch):ch) + cy;
                        cy = ch > 9;    /* Update carry */
                        ch = bin_bcd[ch];
                        if (ch != CHR_0)        /* Clear zero */
                            zind = 0;
                        WriteP(STAR, (br & 0360) | ch);
                        if (br & WM || BAR == 279)
                            break;
                        if (ix)
                            ar = CHR_0;
                        else {
                            ar = ReadP(AAR);
                            DownReg(AAR);
                            sim_interval -= 2;
                        }
                        br = ReadP(STAR = BAR--);
                        sim_interval -= 4;
                    }

                    /* If cy and qsign, tens-compliment result and flip sign */
                    if (sign && cy == 0) {
                        STAR = BAR = DAR;
                        br = ReadP(BAR--);
                        sim_interval -= 2;
                        if ((br & 060) == 040)
                            br |= 060;
                        else {
                            br &= ~020; /* Switch B sign */
                            br |= 040;
                        }
                        zind = 1;
                        cy = 1;
                        /* Compliment until B word mark */
                        while(1) {
                            ch = (9 - bcd_bin[br& 0xf]) + cy;
                            cy = ch > 9;        /* Update carry */
                            ch = bin_bcd[ch];
                            if (ch != CHR_0)    /* Clear zero */
                                zind = 0;
                            WriteP(STAR, (br & 0360) | ch);
                            if (br & WM)
                               break;
                            sim_interval -= 2;
                            br = ReadP(STAR = BAR--);
                        }
                    }

                    /* If carry fix exponent and shift result */
                    if ((sign == 0 && cy) || ix == 0) {
                        BAR = 299;
                        eoind = do_addint(1);
                        /* Now shift mantissa right one */
                        br = ReadP(STAR = BAR--);
                        ar = ReadP(BAR);
                        while ((br & WM) == 0) {
                            WriteP(STAR, (ar & 017) | (br & 060));
                            if (BAR == 279)
                                break;
                            sim_interval -= 4;
                            br = ReadP(STAR = BAR--);
                            ar = ReadP(BAR);
                        }
                        WriteP(STAR, WM|1);     /* New high order 1 + WM */
                        zind = 0;
                    }
        fnorm:
                    temp = 0;
                    DAR = BAR;
                    br = ReadP(++BAR) & 077;
                    zind = 1;
                    while ((br & WM) == 0) {
                        if ((br & 017) != 10) {
                           zind = 0;
                           break;
                        }
                        temp++;
                        br = ReadP(++BAR);
                    }
                    if (br & WM) {      /* Zero result, set exponent to zero */
                        SetBit(BAR-1, 060);     /* Force plus */
                        WriteP(BAR++, WM | 9);
                        WriteP(BAR, 040 | 9);
                        break;
                    }
                    if (temp > 0) {     /* Need to shift it temp places */
                        ar = ReadP(++DAR);
                        while (1) {
                           WriteP(DAR, (ar & WM) | (br & 017));
                           ar = ReadP(++DAR);
                           br = ReadP(++BAR);
                           if (br & WM)
                                break;
                        }
                        while(DAR != BAR) {
                           ReplaceMask(DAR++, 10, 017);
                        }
                        /* Adjust exponent */
                        BAR = 299;
                        if (do_addint(-temp)) {
                undfacc:
                            euind = 1;
                zerofacc:
                            zind = 1;
                            /* Move zero to accumulator */
                            BAR=299;
                            WriteP(BAR--, 040 | 9);
                            WriteP(BAR--, WM | 9);
                            br = ReadP(BAR) | 060;
                            while(1) {
                                WriteP(BAR--, (br & (WM|060)) | 10);
                                if (br & WM)
                                    break;
                                br = ReadP(BAR) & WM;
                            }
                        }
                    }
                    break;

                case CHR_M:                  /* M - Floating mul */
                    temp = oind;
                    oind = 0;
                    reason = do_addsub(0);
                    ch = oind;
                    oind = temp;
                    if (reason != SCPE_OK)
                        break;
                    if (ch) {
                        zind = 0;
                        if ((ReadP(299) & 060) == 040)
                            goto undfacc;
                        eoind = 1;
                        break;
                    }
                    CAR = AAR;
                    DAR = 279;
                    /* Scan for A word mark */
                    qsign = 1;
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    while (1) {
                        if ((ar & 017) != 10)
                           qsign = 0;
                        ClrBit(DAR--, WM);      /* Clear word marks */
                        if (ar & WM || AAR == 0)
                           break;
                        ar = ReadP(AAR);
                        DownReg(AAR);
                        sim_interval -= 4;
                    };

                    ClrBit(DAR--, WM);  /* Extra zero */
                    if (qsign)
                        goto zerofacc;

                    /* Scan for B word mark */
                    zind = 1;
                    br = ReadP(BAR--);
                    while (1) {
                        if ((br & 017) != 10)
                           zind = 0;
                        WriteP(DAR--, br);
                        if (br & WM || BAR == 279)
                           break;
                        br = ReadP(BAR--);
                        sim_interval -= 2;
                    };

                    /* If B zero, scan to A word mark and set B zero */
                    if (zind || qsign)
                        goto zerofacc;

                    temp = BAR; /* Save for later */
                    BAR = 279;
                    AAR = CAR;
                    reason = do_mult();
                    if (reason != SCPE_OK)
                        break;

                    /* Count number of leading zeros */
                    ix = 0;
                    BAR++;      /* Skip first zero */
                    while (BAR != 280) {
                        br = ReadP(++BAR);
                        if ((br & 017) != 10)
                           break;
                        ix++;
                    }
                    if (ix != 0) {
                        DAR = BAR;
                        BAR = 299;
                        if(do_addint(-ix))
                            goto undfacc;
                        BAR = DAR;
                    }
                   /* Find end of result */
                    CAR = 297;
                    ar = ReadP(CAR--);
                    while((ar & WM) == 0)
                        ar = ReadP(CAR--);
                    br = (ReadP(BAR) & 017) | WM;
                  /* Copy result */
                    while(CAR != 297 && BAR != 279) {
                        WriteP(++CAR, br);
                        br = ReadP(++BAR) & 017;
                    }
                    while(CAR != 297)           /* Zero fill rest */
                        WriteP(++CAR, 10);
                    SetBit(297, ReadP(279) & 060);      /* Copy sign */
                    break;

                case CHR_D:                  /* D - Floating div */
                    temp = oind;
                    oind = 0;
                    reason = do_addsub(1);
                    BAR = 299;
                    ch = oind;
                    sign = do_addint(1);        /* Add 1 to exp */
                    oind = temp;
                    if (reason != SCPE_OK)
                        break;

                    CAR = AAR;
                    br = ReadP(BAR--);
                    ar = ReadP(AAR);
                    DownReg(AAR);
                    /* Scan for B word mark */
                    qsign = 1;
                    zind = 1;
                    while (1) {
                        if ((ar & 017) != 10)
                           qsign = 0;
                        if ((br & 017) != 10)
                           zind = 0;
                        if (br & WM || BAR == 279)
                           break;
                        if (ar & WM || AAR == 0)
                           break;
                        br = ReadP(BAR--);
                        ar = ReadP(AAR);
                        DownReg(AAR);
                        sim_interval -= 4;
                    };

                    /* Are fractions same size? */
                    if ((br & WM) && (ar & WM) == 0)
                        goto zerofacc;

                    /* Is A zero? */
                    if (qsign) {
                        if (ch || sign) {
                           eoind = 1;
                        }
                        dind = 1;
                        break;
                    }

                    /* Copy B to work area and fill zeros for A size */
                    DAR = 279;
                    br = ReadP(297);
                    /* Set sign */
                    WriteP(DAR--, (br & 060) | 10);
                    sim_interval -= 2;

                    /* Zero remainder area */
                    for(i = 297 - BAR; i > 1; i--) {
                        WriteP(DAR--, 10);
                        sim_interval -= 2;
                    }

                    /* Save unit position */
                    temp = DAR;

                    /* Copy accumulator to work */
                    BAR = 297;
                    br = ReadP(BAR--);
                    sim_interval -= 2;
                    while(1) {
                        WriteP(DAR--, br & 017);
                        if (br & WM)
                           break;
                        br = ReadP(BAR--);
                        sim_interval -= 2;
                    }

                    /* Two extra zeros */
                    WriteP(DAR--, 10);
                    WriteP(DAR--, 10);

                    /* Set up for divide */
                    BAR = temp;
                    temp = DAR;

                    /* Check for error conditions */
                    if (zind) {
                        if (ch)
                            goto undfacc;
                        goto zerofacc;
                    }

                    if (sign) {
                        eoind = 1;
                        break;
                    }

                    if (ch)
                        goto undfacc;

                    AAR = CAR;
                    /* Do actual divide */
                    reason = do_divide();
                    if (reason != 0)
                        break;

                    /* Scan backward for word mark */
                    qsign = ReadP(BAR+1);
                    sim_interval -= 2;

                    /* Count number of leading zeros */
                    ix = 0;
                    DAR = BAR+2;
                    CAR = temp+1;               /* restore address */
                    while (CAR != 280) {
                        br = ReadP(CAR);
                        sim_interval -= 2;
                        if ((br & 017) != 10)
                           break;
                        CAR++;
                        ix++;
                    }

                    /* Adjust exponent if any leading zeros */
                    if (ix != 0) {
                        BAR = 299;
                        if (do_addint(-ix))
                            goto undfacc;
                    }
                   /* Find end of result */
                    BAR = 297;
                    ar = ReadP(BAR--);
                    while((ar & WM) == 0)
                        ar = ReadP(BAR--);
                    temp = BAR;
                    br = (br & 017) | WM;
                  /* Copy result */
                    while(BAR != 297 && CAR != DAR) {
                        WriteP(++BAR, br);
                        br = ReadP(++CAR) & 017;
                        sim_interval -= 4;
                    }
                    while(BAR != 297)
                        WriteP(++BAR, 10);
                    SetBit(297, qsign & 060);
                    BAR = temp;
                    break;
                }
                break;

            case OP_STS:        /* Store CPU Status */
                /* Check if over the top */
                ValidAddr(AAR);
                BAR = AAR;
                ch = 0;
                switch(op_mod) {
                /* Restore channel status */
                case CHR_1:             /* 1  */
                        ch = 1;
                        break;
                case CHR_2:             /* 2  */
                        ch = 2;
                        break;
                case CHR_3:             /* 3  */
                        ch = 3;
                        break;
                case CHR_4:             /* 4  */
                        ch = 4;
                        break;
                /* Store channel status */
                case CHR_E:             /* E - 1 */
                        ch = 011;
                        break;
                case CHR_F:             /* F - 2 */
                        ch = 012;
                        break;
                case CHR_G:             /* G - 3 */
                        ch = 013;
                        break;
                case CHR_H:             /* H - 4 */
                        ch = 014;
                        break;
                /* Store CPU Status */
                case CHR_S:             /* S  store */
                        br = 0;
                        ch = 0;
                        switch (cind) {
                        case 2: br |= 1; break;
                        case 4: br |= 2; break;
                        case 1: br |= 4; break;
                        }
                        if (zind)
                            br |= 8;
                        if (oind)
                            br |= 16;
                        if (dind)
                            br |= 32;
                        WriteP(BAR, br);
                        DownReg(BAR);
                        break;
                /* Restore CPU Status */
                case CHR_R:             /* R restore */
                        br = ReadP(BAR);
                        DownReg(BAR);
                        ch = 0;
                        oind = (br & 32)?1:0;
                        dind = (br & 16)?1:0;
                        zind = (br & 8)?1:0;
                        cind = (br & 1)?2:0;
                        cind = (br & 2)?4:cind;
                        cind = (br & 4)?1:cind;
                        break;
                /* Protected mode store */
                case CHR_P:             /* P  */
                        if (cpu_unit.flags & OPTION_PROT) {
                            if (prot_enb /*|| reloc != 0*/) { /* Abort */
                                reason = STOP_PROG;
        sim_debug(DEBUG_DETAIL, &cpu_dev, "High set in prot mode\n");
                            } else {
                                temp = bcd_bin[ReadP(BAR) & 017];
                                DownReg(BAR);
                                temp += 10 * bcd_bin[ReadP(BAR) & 017];
                                DownReg(BAR);
                                high_addr = 1000 * temp;
        sim_debug(DEBUG_DETAIL, &cpu_dev, "High set to %d\n", high_addr);
                            }
                        }
                        break;
                case CHR_QUEST:         /* ?  - 3*/
                        if (cpu_unit.flags & OPTION_PROT) {
                            if (prot_enb || reloc != 0) { /* Abort */
                                reason = STOP_PROG;
        sim_debug(DEBUG_DETAIL, &cpu_dev, "Low set in prot mode\n");
                            } else {
                                temp = bcd_bin[ReadP(BAR) & 017];
                                DownReg(BAR);
                                temp += 10 * bcd_bin[ReadP(BAR) & 017];
                                DownReg(BAR);
                                low_addr = 1000 * temp;
            sim_debug(DEBUG_DETAIL, &cpu_dev, "Low set to %d\n", low_addr);
                            }
                        }
                        break;
                default:
                        reason = STOP_UUO;
                        break;
                }
                if (ch) {
                    /* Wait for channel idle before operate */
                    while (chan_active(ch & 07) && reason == 0) {
                        sim_interval = 0;
                        reason = sim_process_event();
                        chan_proc();
                    }
                    /* Do load or store channel */
                    if (ch & 010)
                        WriteP(BAR, chan_io_status[ch & 07] & 0277);
                    else
                        chan_io_status[ch] = ReadP(BAR) & 077;
                    DownReg(BAR);
                }
                break;

            /* Priority mode operations */
            case OP_PRI:
                jump = 0;
                switch(op_mod) {
                case CHR_U:     /* U branch if ch 1 i-o unit priority */
                     jump = urec_irq[1];
                     urec_irq[1] = 0;
                     break;
                case CHR_F:     /* F branch if ch 2 i-o unit priority */
                     jump = urec_irq[2];
                     urec_irq[2] = 0;
                     break;
                case CHR_1:     /* 1 branch if ch 1 overlap priority */
                     if (chan_irq_enb[1]) 
                         jump = (chan_io_status[1] & 0300) == 0300;
                     break;
                case CHR_2:     /* 2 branch if ch 2 overlap priority */
                     if (chan_irq_enb[2]) 
                         jump = (chan_io_status[2] & 0300) == 0300;
                     break;
                case CHR_3:     /* 3 branch if ch 3 overlap priority */
                     if (chan_irq_enb[3]) 
                         jump = (chan_io_status[3] & 0300) == 0300;
                     break;
                case CHR_4:     /* 4 branch if ch 4 overlap priority */
                     if (chan_irq_enb[4]) 
                         jump = (chan_io_status[4] & 0300) == 0300;
                     break;
                case CHR_Q:     /* Q branch if inquiry ch 1 */
                     jump = inquiry;
                     break;
                case CHR_LBRK:  /* * branch if inquiry ch 2 */
                     break;
                case CHR_N:     /* N branch if outquiry ch 1 */
                     break;
                case CHR_TRM:   /* rm branch if outquiry ch 2 */
                     break;
                case CHR_S:     /* S branch if seek priority ch 1 */
                     jump = chan_seek_done[1];
                     chan_seek_done[1] = 0;
                     break;
                case CHR_T:     /* T branch if seek priority ch 2 */
                     jump = chan_seek_done[2];
                     chan_seek_done[2] = 0;
                     break;
                case CHR_Y:     /* Y branch if seek priority ch 3 */
                     jump = chan_seek_done[3];
                     chan_seek_done[3] = 0;
                     break;
                case CHR_RPARN: /* ) branch if seek priority ch 4 */
                     jump = chan_seek_done[4];
                     chan_seek_done[4] = 0;
                     break;
                case CHR_X:     /* X branch and exit */
                     pri_enb = 0;
                sim_debug(DEBUG_PRIO, &cpu_dev, "dis irq\n");
                     jump = 1;
                     break;
                case CHR_E:     /* E branch and enter */
                     pri_enb = 1;
                sim_debug(DEBUG_PRIO, &cpu_dev, "enb irq\n");
                     jump = 1;
                     break;
                case CHR_A:     /* A branch if ch1 attention */
                     jump = chan_stat(1, SNS_ATTN1);
                     break;
                case CHR_B:     /* B branch if ch2 attention */
                     jump = chan_stat(2, SNS_ATTN1);
                     break;
                case CHR_C:     /* C branch if ch3 attention */
                     jump = chan_stat(3, SNS_ATTN1);
                     break;
                case CHR_D:     /* D branch if ch4 attention */
                     jump = chan_stat(4, SNS_ATTN1);
                     break;

                /* Protection mode operations */
                case CHR_QUEST: /* ? Enable protection mode */
                     if (cpu_unit.flags & OPTION_PROT) {
                           sim_debug(DEBUG_DETAIL, &cpu_dev,
                                 "Prot enter %d\n", AAR & AMASK);
                        /* If in protect mode, abort */
                        if (prot_enb) {
                            reason = STOP_PROG;
                        } else {
                        /* Else enter protected mode */
                            prot_enb = 1;
                            prot_fault = 0;
                            jump = 1;
                        }
                     }
                     break;

                case CHR_9:     /* 9 Leave Prot mode */
                     if (cpu_unit.flags & OPTION_PROT) {
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                                "Leave Protect mode %d %d %d\n",
                                         AAR & AMASK, prot_enb, reloc);
                        /* If in protect mode, abort */
                        if ((prot_enb /*|| reloc*/) /*&& (AAR & BBIT) == 0*/) {
                            reason = STOP_PROG;
                        } else {
                            /* Test protect mode */
                            if (reloc && (AAR & BBIT) == 0) {
                                reason = STOP_PROG;
                            } else {
                                jump = 1;
                                prot_enb = 0;
                                reloc = 0;
                                high_addr = -1;
                                low_addr = -1;
                            }
                        }
                     }
                     break;

                case CHR_P:     /* P Check Protection faults */
                     if (cpu_unit.flags & OPTION_PROT) {
                        /* If in protect mode, abort */
                            sim_debug(DEBUG_DETAIL, &cpu_dev,
                                        "Check protect fault %d %d\n",
                                         AAR, prot_fault&1);
                        if (prot_enb) {
                            reason = STOP_PROG;
                        } else {
                            jump = prot_fault & 1;
                            prot_fault &= 2; /* Clear fault */
                        }
                     }
                     break;

                case CHR_H:     /* H Test for Prog faults */
                     if (cpu_unit.flags & OPTION_PROT) {
                            sim_debug(DEBUG_DETAIL, &cpu_dev,
                                         "Check prog fault %d %d\n",
                                         AAR, prot_fault&2);
                        /* If in protect mode, abort */
                        if (prot_enb) {
                            reason = STOP_PROG;
                        } else {
                            jump = prot_fault & 2;
                            prot_fault &= 1; /* Clear fault */
                        }
                     }
                     break;

                case CHR_SLSH:  /* Enable relocation - mode */
                     if (cpu_unit.flags & OPTION_PROT) {
                        /* If in protect mode, abort */
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                                         "Enable relocation %d\n",
                                         AAR & AMASK);
                        if (prot_enb) {
                           reason = STOP_PROG;
                        } else {
                            reloc = 1;
                            prot_fault = 0;
                            BAR = IAR;
                            IAR = AAR;
                            if ((IAR & BBIT) == 0 && low_addr >= 0) {
                                if (IAR < low_addr)
                                    IAR += 100000 - low_addr;
                                else
                                    IAR -= low_addr;
                            }
                            /* Fix BAR for correct return address */
                            if ((BAR & BBIT) == 0 && low_addr >= 0) {
                                if (BAR < low_addr)
                                    BAR += 100000 - low_addr;
                                else
                                    BAR -= low_addr;
                            }
                            AAR = BAR;
                        }
                     }
                     break;

                case CHR_DOL:   /* Enable relocation + prot mode */
                     if (cpu_unit.flags & OPTION_PROT) {
                        /* If in protect mode, abort */
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                                 "Enable relocation + prot %d\n",
                                 AAR & AMASK);
                        if (prot_enb) {
                            reason = STOP_PROG;
                        } else {
                            prot_enb = 1;
                            reloc = 1;
                            prot_fault = 0;
                            BAR = IAR;
                            IAR = AAR;
                            if ((IAR & BBIT) == 0 && low_addr >= 0) {
                                if (IAR < low_addr)
                                    IAR += 100000 - low_addr;
                                else
                                    IAR -= low_addr;
                            }
                            /* Fix BAR for correct return address */
                            if ((BAR & BBIT) == 0 && low_addr >= 0) {
                                if (BAR < low_addr)
                                    BAR += 100000 - low_addr;
                                else
                                    BAR -= low_addr;
                            }
                            AAR = BAR;
                        }
                     }
                     break;

                case CHR_I:     /* I ???? */
                     if (cpu_unit.flags & OPTION_PROT) {
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                                 "Prot opcode %02o %d\n", op_mod, AAR);
                     }
                     break;

                case CHR_GM:    /* | timer release? */
                     if (cpu_unit.flags & OPTION_PROT) {
                        jump = timer_irq;
                        timer_irq &= 1;
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                                 "Timer release %d\n", jump);
                     }
                     break;
                case CHR_QUOT:  /* ' Turn on 20ms timer */
                     if (cpu_unit.flags & OPTION_PROT) {
                        timer_enable = 1;
                        timer_interval = 10;
                        timer_irq = 0;
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "Timer start\n");
                     }
                     jump = 1;
                     break;
                case CHR_DOT:   /* . Turn off 20ms timer */
                     jump = 1;
                     if (cpu_unit.flags & OPTION_PROT) {
                        timer_enable = 0;
                        timer_irq = 0;
                        sim_debug(DEBUG_DETAIL, &cpu_dev, "Timer stop\n");
                     }
                     break;
                }
                break;
            }

            /* Do a jump to new location. */
            if (jump) {
                BAR = IAR;      /* Save current for posterity */
                IAR = AAR & AMASK;
            }
            if (hst_lnt) {      /* History enabled? */
                int     len, start;
                hst[hst_p].aend = AAR;
                hst[hst_p].bend = BAR;
                len = hst[hst_p].bend - hst[hst_p].bstart;
                if (len < 0) {
                   len = -len;
                   start = hst[hst_p].bend + 1;
                   if (len > 50) {
                        start = hst[hst_p].bstart - 50;
                        len = 50;
                   }
                } else {
                   if (len > 50)
                        len = 50;
                   start = hst[hst_p].bstart;
                }
                if (jump) {
                   len = 0;
                   start = hst[hst_p].bstart;
                }
                for(i = 0; i < len; i++)
                    hst[hst_p].bdata[i] = ReadP(start+i);
                hst[hst_p].dlen = len;
            }
        }

        /* Handle protection faults */
check_prot:
        if (fault) {
             reason = fault;
             fault = 0;
        }

        if (reason != 0 && cpu_unit.flags & OPTION_PROT && (prot_enb || reloc != 0)) {
             switch(reason) {
             case STOP_NOWM:
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                         "IAR = %d No WM AAR=%d BAR=%d\n", IAR, AAR, BAR);
                        prot_fault |= 2;
                        reason = 0;
                        break;
            case STOP_INVADDR:
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                         "IAR = %d Inv Addr AAR=%d BAR=%d\n", IAR, AAR, BAR);
                        prot_fault |= 2;
                        reason = 0;
                        break;
            case STOP_UUO:
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                         "IAR = %d Inv Op AAR=%d BAR=%d\n", IAR, AAR, BAR);
                        prot_fault |= 2;
                        reason = 0;
                        break;
            case STOP_INVLEN:
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "IAR = %d Invlen Op AAR=%d BAR=%d\n", IAR, AAR, BAR);
                        prot_fault |= 2;
                        reason = 0;
                        break;
            case STOP_IOCHECK:
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "IAR = %d I/O Check AAR=%d BAR=%d\n", IAR, AAR, BAR);
                        prot_fault |= 2;
                        reason = 0;
                        break;
            case STOP_PROG:
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "IAR = %d Prog check AAR=%d BAR=%d low=%d high=%d\n",
                                 IAR, AAR, BAR, low_addr, high_addr);
                        prot_fault |= 2;
                        reason = 0;
                        break;
            case STOP_PROT:
                        sim_debug(DEBUG_DETAIL, &cpu_dev,
                         "IAR = %d Prot check AAR=%d BAR=%d low=%d high=%d\n",
                                 IAR, AAR, BAR, low_addr, high_addr);
                        prot_fault |= 1;
                        reason = 0;
                        break;
            default:    /* Anything else halt sim */
                        break;
            }
            /* If faults, B 8, otherwise stop sim */
            if (prot_fault && reason == 0) {
                prot_enb = 0;
                high_addr = -1;
                low_addr = -1;
                reloc = 0;
                BAR = IAR;      /* Save current for posterity */
                IAR = AAR = 8;
            }
        }
        if (instr_count != 0 && --instr_count == 0)
            return SCPE_STEP;
    }                           /* end while */

/* Simulation halted */
    return reason;
}

#define UpAddr(reg) reg++; if ((reg & AMASK) == MEMSIZE) { \
                 return STOP_INVADDR; }
#define DownAddr(reg) if ((reg & AMASK) == 0) { \
                 return STOP_INVADDR; } else { reg--; }


/* Add constant, two digits only, used by FP code */
int do_addint(int val) {
    uint8               br;
    int                 sign;
    uint8               ch;
    int                 cy;

    br = ReadP(BAR);
    sign = (br & 060) == 040;
    if (val < 0) {
        sign = !sign;
        val = -val;
    }
    cy = sign;
    ch = val % 10;
    ch = bcd_bin[br& 0xf] + (sign?(9-ch):ch) + cy;
    cy = ch > 9;        /* Update carry */
    ch = bin_bcd[ch];
    WriteP(BAR--, (br & 060) | ch);
    br = ReadP(BAR);
    ch = val / 10;
    ch = bcd_bin[br& 0xf] + (sign?(9-ch):ch) + cy;
    cy = ch > 9;        /* Update carry */
    ch = bin_bcd[ch];
    WriteP(BAR--, WM | (br & 060) | ch);
    sim_interval -= 2;
    if (sign && cy == 0) {
        BAR += 2;       /* Back up */
        br = ReadP(BAR);
        sim_interval -= 2;
        if ((br & 060) == 040)
            br |= 060;
        else {
            br &= ~020; /* Switch B sign */
            br |= 040;
        }
        cy = 1;
        /* Compliment until B word mark */
        ch = (9 - bcd_bin[br& 0xf]) + cy;
        cy = ch > 9;    /* Update carry */
        ch = bin_bcd[ch];
        WriteP(BAR--, (br & 0360) | ch);
        sim_interval -= 2;
        br = ReadP(BAR);
        ch = (9 - bcd_bin[br& 0xf]) + cy;
        cy = ch > 9;    /* Update carry */
        ch = bin_bcd[ch];
        WriteP(BAR--, (br & 0360) | ch);
    }
    if (sign == 0 && cy)
        return 1;
    return 0;
}

t_stat do_addsub(int mode) {
    uint8               br;
    uint8               ar;
    int                 sign;
    uint8               ch;
    int                 cy;
    uint32              STAR;

    DAR = BAR;
    ar = ReadP(AAR);
    br = ReadP(STAR = BAR);
    sim_interval -= 2;
    DownAddr(AAR);
    DownAddr(BAR);
    if (mode)   /* Subtraction */
        sign = (ar & 060) != 040;
    else        /* Addition */
        sign = (ar & 060) == 040;
    zind = 1;
    if ((br & 060) == 040)
        sign ^= 1;
    cy = sign;

    if (CPU_MODEL == 1 && sign)
        br |= ((br & 060) != 040)?060:0;
    /* Add until word mark on A or B */
    while(1) {
        ch = bcd_bin[ar & 0xf];
        ch = bcd_bin[br & 0xf] + ((sign)? (9 - ch):ch) + cy;
        cy = ch > 9;    /* Update carry */
        ch = bin_bcd[ch];
        if (ch != CHR_0)        /* Clear zero */
            zind = 0;
        WriteP(STAR, (br & 0360) | ch);
        if (br & WM) {
             if (CPU_MODEL == 1 && !sign && cy)
                 WriteP(STAR,
                        WM | ch |(060&(br + 020)));
             break;
        }
        if (ar & WM)
            ar = WM|CHR_0;
        else {
            sim_interval--;
            ar = ReadP(AAR);
            DownAddr(AAR);
        }
        sim_interval--;
        br = ReadP(STAR = BAR);
        DownAddr(BAR);
        if (CPU_MODEL == 1) {
             if ((br & WM) == 0 || sign)
                 br &= WM | 0xf;
        }
    }

    /* If cy and qsign, tens-compliment result and flip sign */
    if (sign && cy == 0) {
        STAR = BAR = DAR;
        br = ReadP(BAR);
        DownAddr(BAR);
        sim_interval--;
        if ((br & 060) == 040)
            br |= 060;
        else {
            br &= ~020;         /* Switch B sign */
            br |= 040;
        }
        zind = 1;
        cy = 1;
        /* Compliment until B word mark */
        while(1) {
            ch = (9 - bcd_bin[br& 0xf]) + cy;
            cy = ch > 9;        /* Update carry */
            ch = bin_bcd[ch];
            if (ch != CHR_0)    /* Clear zero */
                zind = 0;
            WriteP(STAR, (br & 0360) | ch);
            if (br & WM)
               break;
            br = ReadP(STAR = BAR);
            DownAddr(BAR);
            sim_interval--;
            if (CPU_MODEL == 1)
                br &= WM|0xf;
        }
    }

    /* If carry set overflow */
    if (sign == 0 && cy)
       oind = 1;
    return SCPE_OK;
}

t_stat
do_mult()
{
    uint8               br;
    uint8               ar;
    int                 sign;
    uint8               ch;
    int                 cy;

    CAR = AAR;
    DAR = BAR;
    ar = ReadP(AAR);
    DownAddr(AAR);
    zind = 1;
    sign = ((ar & 060) == 040);
    /* Scan A for word mark setting B digits to zero */
    while (1) {
        WriteP(BAR, 10);
        sim_interval -= 4;
        DownAddr(BAR);
        if (ar & WM)
            break;
        ar = ReadP(AAR);
        DownAddr(AAR);
    };

    /* Skip last digit of product */
    WriteP(BAR, 10);
    DownAddr(BAR);
    sim_interval -= 2;
    /* Check signs of B and A. */
    br = ReadP(BAR);
    /* Compute sign */
    sign ^= ((br & 060) == 040);
    sign = (sign)?040:060;
    /* Do multiple loop until B word mark */
    while(1) {
         /* Interloop, multiply one digit */
         ch = bcd_bin[br & 0xf];
         while (ch != 0) {
             WriteP(BAR, bin_bcd[ch - 1] | (br & WM));
             BAR = DAR;
             br = ReadP(BAR);
             cy = 0;
             AAR = CAR;
             ar = ReadP(AAR);
             DownAddr(AAR);
             while(1) {
                 ch = bcd_bin[br & 0xf];
                 ch = bcd_bin[ar & 0xf] + ch + cy;
                 if (ch != 0)   /* Clear zero */
                    zind = 0;
                 cy = ch > 9;   /* Update carry */
                 WriteP(BAR, bin_bcd[ch] | (br & WM));
                 DownAddr(BAR);
                 br = ReadP(BAR);
                 if (ar & WM)
                    break;
                 ar = ReadP(AAR);
                 DownAddr(AAR);
                 sim_interval -= 4;
             }
             /* Add carry to next digit */
             ch = bcd_bin[br & 0xf] + cy;
             if (ch != 0)       /* Clear zero */
                 zind = 0;
             sim_interval -= 2;
             WriteP(BAR, bin_bcd[ch] | (br & WM));
             DownAddr(BAR);
             br = ReadP(BAR);
             ch = bcd_bin[br & 0xf];
        }
        WriteP(BAR, CHR_0 | (br & WM));
        DownAddr(BAR);
        SetBit(DAR, sign);
        DownAddr(DAR);
        sign = 0;       /* Only on first digit */
        if (br & WM)
            break;
        br = ReadP(BAR);
    }
    return SCPE_OK;
}

t_stat
do_divide()
{
    uint16              t;
    int                 temp;
    uint8               br;
    uint8               ar;
    int                 sign, qsign;
    uint8               ch;
    int                 cy;

    qsign = 9;  /* Set compliment and carry in */
    cy = 1;
    temp = 0;   /* MDL latch */
    sign = 0;
    CAR = AAR;
    DAR = BAR;
    while (1) {
        AAR = CAR;
        BAR = DAR;
        ar = ReadP(AAR);
        DownAddr(AAR);
        br = ReadP(BAR);
        if (qsign == 0 && br & 040) {
            sign = ((ar & 060) == 040); /* Compute sign */
            sign ^= ((br & 060) == 040);
            sign = (sign)?040:060;
            temp = 1;           /* Set last cycle */
        }
        while (1) {
            sim_interval -= 4;
            t = bcd_bin[ar& 0xf];
            ch = ((qsign)?(9-t):t) + bcd_bin[br & 0xf] + cy;
            cy = ch > 9;        /* Update carry */
            ReplaceMask(BAR, bin_bcd[ch], 017);
            DownAddr(BAR);
            br = ReadP(BAR);
            sim_interval -= 2;
            if (ar & WM) {
                ch = qsign + bcd_bin[br & 0xf] + cy;
                cy = ch > 9;    /* Update carry */
                ReplaceMask(BAR, bin_bcd[ch], 017);
                DownAddr(BAR);
                br = ReadP(BAR);
                sim_interval -= 2;
                break;
            } else {
                ar = ReadP(AAR);
                DownAddr(AAR);
            }
        }
        if (qsign == 9) {
            if (cy) {
                ch = bcd_bin[br & 0xf] + cy;
                ReplaceMask(BAR, bin_bcd[ch], 017);
                DownAddr(BAR);
                if (ch > 9) {
                    if (CPU_MODEL == 1)
                        oind = 1;
                    else
                        dind = 1;
                    break;
                }
            } else {
                qsign = 0;
            }
        } else {
            if (temp) {
                ch = 9 + bcd_bin[br & 0xf] + cy;
                WriteP(BAR, bin_bcd[ch] | sign | (br & WM));
                DownAddr(BAR);
                break;
            }
            qsign = 9;
            cy = 1;
            UpAddr(DAR);                /* Back up one digit */
        }
    }
    return SCPE_OK;
}


/* Interval timer routines */
t_stat
rtc_srv(UNIT * uptr)
{
    (void)sim_rtcn_calb (rtc_tps, TMR_RTC);
    sim_activate_after(uptr, 1000000/rtc_tps);

    if (timer_enable) {
        if (--timer_interval == 0) {
            timer_irq |= 1;
            timer_interval = 10;
        }
    }
    return SCPE_OK;
}

/* Reset routine */
t_stat
cpu_reset(DEVICE * dptr)
{
    IAR = 1;
    AAR = 0;
    BAR = 0;
    sim_brk_types = sim_brk_dflt = SWMASK('E');
    pri_enb = 0;
    timer_enable = 0;
    cind = 2;
    zind = oind = dind = euind = eoind = 0;
    if (cpu_unit.flags & OPTION_PROT)
        sim_rtcn_init_unit (&cpu_unit, 10000, TMR_RTC);
    return SCPE_OK;
}

/* Memory examine */
t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MEMSIZE)
        return SCPE_NXM;
    if (vptr != NULL)
        *vptr = M[addr] & (077 | WM);

    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MEMSIZE)
        return SCPE_NXM;
    M[addr] = val & (077 | WM);
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    uint8            mc = 0;
    int32            i;
    int32            v;

    v = val >> UNIT_V_MSIZE;
    v++;
    v *= 10000;
    if ((v < 0) || (v > MAXMEMSIZE))
        return SCPE_ARG;
    for (i = v-1; i < MAXMEMSIZE; i++)
        mc |= M[i];
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    cpu_unit.capac = v;
    cpu_unit.flags &= ~UNIT_MSIZE;
    cpu_unit.flags |= val;
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
    int32               k, i, di, lnt, pc;
    char               *cptr = (char *) desc;
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
    fprintf(st, "IC     A     B    Aend  Bend   \n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->ic & HIST_PC) {  /* instruction? */
            pc = h->ic & HIST_MSK;
            fprintf(st, "%05d ", pc);
            fprintf(st, "%05d ", h->astart & AMASK);
            fprintf(st, "%05d ", h->bstart & AMASK);
            fprintf(st, "%05d%c", h->aend & AMASK, (h->aend & BBIT)?'+':' ');
            fprintf(st, "%05d%c|", h->bend & AMASK, (h->bend & BBIT)?'+':' ');
            for(i = 0; i < h->dlen; i++)
                fputc(mem_to_ascii[h->bdata[i]&077], st);
            fputc('|', st);
            fputc(' ', st);
            for(i = 0; i< 15; i++)
                sim_eval[i] = h->inst[i];
            (void)fprint_sym(st, pc, sim_eval, &cpu_unit, SWMASK((h->ic & HIST_1401)?'N':'M'));
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}


const char *
cpu_description (DEVICE *dptr)
{
       return "IBM 7010 CPU";
}

t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The CPU can be set to a IBM 1401 or IBM 1410/7010\n");
    fprintf (st, "The type of CPU can be set by one of the following commands\n\n");
    fprintf (st, "   sim> set CPU 1401        sets IBM 1401 emulation\n");
    fprintf (st, "   sim> set CPU 7010        sets IBM 1410/7010 emulation\n\n");
    fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\n\n");
    fprintf (st, "      -c      examine/deposit characters, 6 per word\n");
    fprintf (st, "      -l      examine/deposit half words\n");
    fprintf (st, "      -m      examine/deposit IBM 7010 instructions\n\n");
    fprintf (st, "The memory of the CPU can be set in 10K incrememts from 10K to 100K with the\n\n");
    fprintf (st, "   sim> SET CPU xK\n\n");
    fprintf (st, "For the IBM 7010 the following options can be enabled\n\n");
    fprintf (st, "   sim> SET CPU PRIORITY      enables Priority Interupts\n");
    fprintf (st, "   sim> SET CPU NOPRIORITY    disables Priority Interupts\n\n");
    fprintf (st, "   sim> SET CPU FLOAT     enables Floating Point\n");
    fprintf (st, "   sim> SET CPU NOFLOAT   disables Floating Point\n\n");
    fprintf (st, "   sim> SET CPU PROT    enables memory protection feature\n");
    fprintf (st, "   sim> SET CPU NOPROT  disables memory protection feature\n\n");
    fprintf (st, "The CPU can maintain a history of the most recently executed instructions.\n");
    fprintf (st, "This is controlled by the SET CPU HISTORY and SHOW CPU HISTORY commands:\n\n");
    fprintf (st, "   sim> SET CPU HISTORY                 clear history buffer\n");
    fprintf (st, "   sim> SET CPU HISTORY=0               disable history\n");
    fprintf (st, "   sim> SET CPU HISTORY=n{:file}        enable history, length = n\n");
    fprintf (st, "   sim> SHOW CPU HISTORY                print CPU history\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);

    return SCPE_OK;
}

