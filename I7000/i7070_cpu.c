/* i7070_cpu.c: IBM 7070 CPU simulator

   Copyright (c) 2005-2016, Richard Cornwell

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

   cpu          IBM 7070 central processor

   The IBM 7070 was introduced in June 1960, as a replacement to the IBM 650.
   It had core memory up to 10,000 10 digit words.
   The 7072 was introduced November 1962 and the 7074 on November 1961.
   The 7074 is a faster version of the 7070 with the addition of memory up
   to 40,000 10 digit words. The first 100 memory locations can be used as
   index registers. Most memory reference instructions allow for a field
   of digits to be selected to operate on and not modify the rest.

   The 7070 is a decimal machine with each word consisting of 10 digits
   plus a sign. The sign can be plus, minus or alpha. Alpha data is stored
   5 characters to a word (2 digits per character).

   The system state for the IBM 7070 is:

   AC1<0:10>            AC1 register
   AC2<0:10>            AC2 register
   AC3<0:10>            AC3 register
   IC<0:5>              program counter

   The 7070 had one basic instuction format.

   <sign> 01 23 45 6789
     <sign> and 01 are opcode. Alpha is not allowed.
     23 specify an index register from memory location 01 to 99.
        or if extended addressing is enabled 10-99. 01-09 specify
        high order digit of address.
     45 encode either a field, or operands depending on instruction.
     6789 are address in memory. If index is specified they are
        added to fields <sign> [1]2345 of memory addressed by field 23.

   Accumulators may be accessed 9991/2/3 or 99991/2/3.

   Signs are stored as 9 for plus.
                       6 for minus.
                       3 for alpha.

   Options supported are Timer, Extended addressing and Floating point.
*/

#include "i7070_defs.h"
#include "sim_timer.h"

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (7 << UNIT_V_MSIZE)
#define UNIT_V_CPUMODEL (UNIT_V_UF + 4)
#define UNIT_MODEL      (0x01 << UNIT_V_CPUMODEL)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_CPUMODEL) & 0x01)
#define MODEL(x)        (x << UNIT_V_CPUMODEL)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define OPTION_FLOAT    (1 << (UNIT_V_CPUMODEL + 1))
#define OPTION_TIMER    (1 << (UNIT_V_CPUMODEL + 2))
#define OPTION_EXTEND   (1 << (UNIT_V_CPUMODEL + 3))

#define TMR_RTC         1

#define HIST_NOEA       0x10000000
#define HIST_NOAFT      0x20000000
#define HIST_NOBEF      0x40000000
#define HIST_PC         0x10000
#define HIST_MIN        64
#define HIST_MAX        65536

struct InstHistory
{
    t_int64             op;
    uint32              ic;
    uint32              ea;
    t_int64             before;
    t_int64             after;
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

void                mem_init(void);

/* Interval timer option */
t_stat              rtc_srv(UNIT * uptr);
t_stat              rtc_reset(DEVICE * dptr);

t_uint64            M[MAXMEMSIZE] = { PSIGN };  /* memory */
t_uint64            AC[4];                      /* registers */
t_uint64            inds;                       /* Error indicators */
t_uint64            diaglatch;                  /* Diagnostic latches */
uint16              timer;                      /* Timer register */
uint32              IC;                         /* program counter */
uint16              timer_clock;                /* Timer clock */
uint8               SW = 0;                     /* Sense switch */
uint8               emode;                      /* Extended address mode */
uint16              pri_latchs[10];             /* Priority latchs */
uint32              pri_mask = 0xFFFFFF;        /* Priority masks */
uint8               pri_enb = 1;                /* Enable priority procs */
uint8               lpr_chan9[NUM_CHAN];        /* Line printer on channel 9 */
int                 cycle_time = 20;            /* Cycle time of 12us */

/* History information */
int32               hst_p = 0;                  /* History pointer */
int32               hst_lnt = 0;                /* History length */
struct InstHistory *hst = NULL;                 /* History stack */


/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
    { UDATA(&rtc_srv, OPTION_FLOAT|MEMAMOUNT(1)|MODEL(0x0), 10000), 10  };

REG                 cpu_reg[] = {
    {DRDATA(IC, IC, 20), REG_FIT},
    {HRDATA(AC1, AC[1], 44), REG_VMIO|REG_FIT},
    {HRDATA(AC2, AC[2], 44), REG_VMIO|REG_FIT},
    {HRDATA(AC3, AC[3], 44), REG_VMIO|REG_FIT},
    {HRDATA(IND, inds, 44), REG_VMIO|REG_FIT},
    {ORDATA(SW, SW, 4), REG_FIT},
    {FLDATA(SW1, SW, 0), REG_FIT},
    {FLDATA(SW2, SW, 1), REG_FIT},
    {FLDATA(SW3, SW, 2), REG_FIT},
    {FLDATA(SW4, SW, 3), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MODEL, MODEL(0x0), "7070", "7070", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(0x1), "7074", "7074", NULL, NULL, NULL},
    {UNIT_MSIZE, MEMAMOUNT(0),  "5K",  "5K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(1), "10K", "10K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(2), "15K", "15K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(3), "20K", "20K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(4), "25K", "25K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(5), "30K", "30K", &cpu_set_size},
    {OPTION_FLOAT, 0, NULL, "NOFLOAT", NULL, NULL, NULL},
    {OPTION_FLOAT, OPTION_FLOAT, "FLOAT", "FLOAT", NULL, NULL, NULL},
    {OPTION_EXTEND, 0, NULL, "NOEXTEND", NULL, NULL, NULL},
    {OPTION_EXTEND, OPTION_EXTEND, "EXTEND", "EXTEND", NULL, NULL, NULL},
    {OPTION_TIMER, 0, NULL, "NOCLOCK", NULL, NULL, NULL},
    {OPTION_TIMER, OPTION_TIMER, "CLOCK", "CLOCK", NULL, NULL, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 18, 1, 16, 44,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
};

uint32  dscale[4][16] = {
    {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 0,0,0,0,0,0},
    {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 0,0,0,0,0,0},
    {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 0,0,0,0,0,0},
    {0, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000,
                                                                   0,0,0,0,0,0}
};

t_uint64 fdmask[11] = {
    0x0000000000LL,
    0xF000000000LL, 0xFF00000000LL, 0xFFF0000000LL, 0xFFFF000000LL,
    0xFFFFF00000LL, 0xFFFFFF0000LL, 0xFFFFFFF000LL, 0xFFFFFFFF00LL,
    0xFFFFFFFFF0LL, 0xFFFFFFFFFFLL
};

t_uint64 rdmask[11] = {
    0xFFFFFFFFFFLL, 0x0FFFFFFFFFLL, 0x00FFFFFFFFLL, 0x000FFFFFFFLL,
    0x0000FFFFFFLL, 0x00000FFFFFLL, 0x000000FFFFLL, 0x0000000FFFLL,
    0x00000000FFLL, 0x000000000FLL, 0x0LL
};

t_uint64 ldmask[11] = {
    0x0LL, 0xFLL, 0xFFLL, 0xFFFLL, 0xFFFFLL, 0xFFFFFLL, 0xFFFFFFLL, 0xFFFFFFFLL,
    0xFFFFFFFFLL, 0xFFFFFFFFFLL, 0xFFFFFFFFFFLL
};

t_uint64 dmask[11] = {
    0x0LL, 0xFLL, 0xF0LL, 0xF00LL, 0xF000LL, 0xF0000LL,
    0xF00000LL, 0xF000000LL, 0xF0000000LL, 0xF00000000LL, 0xF000000000LL
};

#define gdigit(w, d) (((w) >> ((d) * 4)) & 0xF)
#define sdigit(d, v) ((((t_uint64)v) & 0xFLL) << ((d) * 4))
#define mdigit(d)    (0xFLL << ((d) * 4))

t_uint64 ReadP(uint32 addr) {
    sim_interval -= (CPU_MODEL == 0x0)? 2: 1;
    if (emode) {
        if (addr > MAXMEMSIZE) {
            switch(addr) {
            case 99991: return AC[1];
            case 99992: return AC[2];
            case 99993: return AC[3];
            default:    return 0LL;
            }
        }
    } else {
        if (addr >= 9990) {
            switch(addr) {
            case 9991: return AC[1];
            case 9992: return AC[2];
            case 9993: return AC[3];
            default:    return 0LL;
            }
        }
    }
    if (addr < MEMSIZE && addr < MAXMEMSIZE)
        return M[addr];
    return 0LL;
}

void WriteP(uint32 addr, t_uint64 value) {
    sim_interval -= (CPU_MODEL == 0x0)? 2: 1;
    if (emode) {
        if (addr > MAXMEMSIZE) {
            switch(addr) {
            case 99991: AC[1] = value; return;
            case 99992: AC[2] = value; return;
            case 99993: AC[3] = value; return;
            }
        }
    } else {
        if (addr >= 9990) {
            switch(addr) {
            case 9991: AC[1] = value; return;
            case 9992: AC[2] = value; return;
            case 9993: AC[3] = value; return;
            default: return;
            }
        }
    }
    if (addr < MEMSIZE && addr < MAXMEMSIZE)
        M[addr] = value;
}

t_stat
sim_instr(void)
{
    t_stat              reason;
    t_uint64            temp;
    t_uint64            MBR;
    uint16              opcode = 0;
    uint32              MA = 0;
    uint32              utmp;   /* Unsigned temp */
    int                 tmp;    /* Signed temp */
    uint8               f = 0;
    uint8               stopnext;
    uint8               IX = 0;
    uint8               f1 = 0;
    uint8               f2 = 0;
    uint8               op2 = 0;
    int                 iowait = 0;     /* Wait for IO to start */
    int                 chwait = 0;     /* Wait for channel to be inactive */
    uint8               sign;
    int                 instr_count = 0; /* Number of instructions to execute */

    if (sim_step != 0) {
        instr_count = sim_step;
        sim_cancel_step();
    }

    reason = 0;

    iowait = 0;
    stopnext = 0;
    while (reason == 0) {       /* loop until halted */

/* If doing fast I/O don't sit in idle loop */
        if (iowait == 0 && chwait == 0 && stopnext)
            return SCPE_STEP;

        if (chwait != 0 && chan_active(chwait))
            sim_interval = 0;
        else
            chwait = 0;

        if (iowait)
            sim_interval = 0;

        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason != SCPE_OK) {
                if (reason == SCPE_STEP && iowait)
                    stopnext = 1;
                else
                    break;      /* process */
            }
        }

        /* Only check for break points during actual fetch */
        if (iowait == 0 && chwait == 0
                        && sim_brk_summ && sim_brk_test(IC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

        /* Don't do interupt if waiting on IO or channel */
        if (pri_enb && iowait == 0 && chwait == 0) {
            /* Check if we have to process one */
            if ((tmp = scan_irq()) != 0) {
                /* Save instruction counter */
                if (CPU_MODEL == 0x1)  /* On 7074 location 97 modified */
                        MBR = M[97];
                else
                        MBR = 0;        /* On 7070/2 location cleared */
                upd_idx(&MBR, IC);
                MBR &= DMASK;
                MBR |= PSIGN;
                M[97] = MBR;
                /* Save indicators */
                M[100] = inds;
                inds = PSIGN;
                pri_enb = 0;
                IC = tmp;
                sim_debug(DEBUG_TRAP, &cpu_dev, "IRQ= %d %d\n\r", IC, tmp);
            }
        }

   /* Main instruction fetch/decode loop */
       if (chwait == 0) {
            /* Split out current instruction */
            sim_interval -= 24;         /* count down */
            /* If waiting for IO don't bump IC or create history */
            if (iowait)
                /* Don't do a fetch if waiting on I/O to be ready */
                iowait = 0;
            else {
                MBR = ReadP(IC);
                if (hst_lnt) {  /* history enabled? */
                    hst_p = (hst_p + 1);        /* next entry */
                    if (hst_p >= hst_lnt)
                        hst_p = 0;
                    hst[hst_p].ic = (IC) | HIST_PC;
                    hst[hst_p].op = MBR;
                    hst[hst_p].after = 0;
                }
                IC++;
                MA = MBR & 0xf;                 MBR >>= 4;
                MA += dscale[0][MBR & 0xf];     MBR >>= 4;
                MA += dscale[1][MBR & 0xf];     MBR >>= 4;
                MA += dscale[2][MBR & 0xf];     MBR >>= 4;
                f2 = MBR & 0xf;                 MBR >>= 4;
                f1 = MBR & 0xf;                 MBR >>= 4;
                IX = MBR & 0xf;                 MBR >>= 4;
                IX += dscale[0][MBR & 0xf];     MBR >>= 4;
                opcode = MBR & 0xff;
                op2 = (opcode >> 4) & 0xf;
                if ((MBR & (SMASK >> 32)) == (MSIGN >> 32))
                    opcode |= 0x100;
            /* Check if extended addressing mode */
                if (emode && IX < 10) {
                    MA += dscale[3][IX];
                    IX = 0;
                }
            /* Handle indexing */
                if (IX > 0) {
                    sim_interval -= (CPU_MODEL == 0x0)? 10: 1;
                    MBR = M[IX];
                    utmp = dec_bin_idx(MBR);
                    if ((MBR & SMASK) == MSIGN) {       /* Change sign */
                        if (MA < utmp) {
                           if (emode)
                               MA = 100000 - MA - utmp;
                           else
                               MA = 10000 - MA - utmp;
                        } else
                           MA -= utmp;
                    } else if ((MBR & SMASK) == PSIGN) {
                        MA += utmp;
                        if (emode) {
                            if (MA > 100000)
                                MA -= 100000;
                        } else {
                            if (MA > 10000)
                                MA -= 10000;
                        }
                    } else {
                        reason = STOP_INDEX;
                        break;
                    }
                 }
                 IX = f2 + dscale[0][f1];
            /* Fetch data */
                 MBR = ReadP(MA);
                 if (hst_lnt) {  /* history enabled? */
                     hst[hst_p].ea = MA;
                     hst[hst_p].before = MBR;
                 }
             }

             switch(opcode) {
                /* Zero add absolute */
                case OP_ZAA:
                     MBR &= DMASK;
                     MBR |= PSIGN;
                     goto set_ac;
                /* Zero sub absolute */
                case OP_ZSA:
                     MBR &= DMASK;
                     MBR |= MSIGN;
                     goto set_ac;
                /* Load AC negitive from memory */
                case OP_ZS1: case OP_ZS2: case OP_ZS3:
                     if ((MBR & SMASK) != ASIGN)
                        MBR ^= SMASK;
                /* Zero add, load AC from memory */
                case OP_ZA1: case OP_ZA2: case OP_ZA3:
        set_ac:
                     MBR = (MBR & SMASK)|((rdmask[f1] & MBR) >> ((9 - f2) * 4));

                     AC[op2] = MBR;
                     sim_interval -= (CPU_MODEL == 0x0)? (f2 - f1)/3: 1;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[op2];
                     }
                     break;
                /* AC - memory */
                case OP_S1: case OP_S2: case OP_S3:
                     sign = (MBR & SMASK) != MSIGN;
                     if ((MBR & SMASK) == ASIGN)
                        sign |= 8;
                     goto add;
                /* AC - |memory| */
                case OP_SA:
                /* AC + |memory| */
                     sign = 1;
                     goto add;
                case OP_AA:
                     sign = 0;
                     goto add;
                /* AC + memory */
                case OP_A1: case OP_A2: case OP_A3:
                        /* Get field. */
                     sign = (MBR & SMASK) == MSIGN;
                     if ((MBR & SMASK) == ASIGN)
                        sign |= 8;
                add:
                     if ((AC[op2] & SMASK) == ASIGN)
                        sign |= 8;
                     MBR = (rdmask[f1] & MBR) >> ((9 - f2) * 4);
                     sim_interval -= (CPU_MODEL == 0x0)? 4*(f2 - f1)/3: 1;
                     if ((AC[op2] & SMASK) == MSIGN)
                        sign ^= 3;
                     AC[op2] &= DMASK;
                     if (sign & 1) {
                          int cy;
                          cy = dec_add(&AC[op2], NINES - MBR);
                          cy |= dec_add(&AC[op2], 1LL);
                          if (cy == 0) {
                              AC[op2] = NINES - AC[op2];
                              dec_add(&AC[op2], 1LL);
                              sim_interval -= (CPU_MODEL == 0x0)?
                                                        12*(f2 - f1)/3: 1;
                              sign ^= 3;
                          }
                     } else {
                          if(dec_add(&AC[op2], MBR))
                            inds |= 1LL << (4 * (3 - op2)); /* Set overflow */
                     }
                     AC[op2] &= DMASK;
                     if (sign & 8)
                        AC[op2] |= ASIGN;
                     else if (sign & 2)
                        AC[op2] |= MSIGN;
                     else
                        AC[op2] |= PSIGN;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[op2];
                     }
                     break;
                /* |memory| + AC */
                case OP_AAS1: case OP_AAS2: case OP_AAS3:
                     sign = ((MBR & SMASK) == MSIGN)?2:0;
                     goto addstore;
                /* mem = AC - |memory| */
                case OP_SS1: case OP_SS2: case OP_SS3:
                     sign = ((MBR & SMASK) != MSIGN)?1:2;
                     goto addstore;
                /* memory + AC */
                case OP_AS1: case OP_AS2: case OP_AS3:
                        /* Get field. */
                     sign = ((MBR & SMASK) == MSIGN)?3:0;
                addstore:
                     if ((MBR & SMASK) == ASIGN)
                        sign |= 4;
                     switch (AC[op2] & SMASK) {
                     case ASIGN: sign |= 8; break;
                     case MSIGN: sign ^= 1; break;
                     }
                     temp = (rdmask[f1] & MBR) >> ((9 - f2) * 4);
                     sim_interval -= (CPU_MODEL == 0x0)? 4*(f2 - f1)/3: 1;
                     if (sign & 1) {
                          int cy;
                          cy = dec_add(&temp, NINES - (AC[op2] & DMASK));
                          cy |= dec_add(&temp, 1LL);
                          if (cy == 0) {
                              temp = NINES - temp;
                              dec_add(&temp, 1LL);
                              sim_interval -= (CPU_MODEL == 0x0)?
                                                        12*(f2 - f1)/3: 1;
                              sign ^= 3;
                          }
                     } else {
                          if(dec_add(&temp, DMASK&AC[op2]))
                            inds |= 1LL << (4 * (3 - op2)); /* Set overflow */
                     }

                     /* Put results back */
                     utmp = (MBR & SMASK) >> 40;/* Original sign for compare */
                     MBR &= DMASK;
                     MBR &= ~(rdmask[f1] & fdmask[f2+1]); /* Clear digits. */
                     /* Check overflow */
                     if (temp & ~ldmask[f2-f1+1]) {
                        if (inds & 0x0F00000000LL) {
                           inds &= 0xFF0FFFFFFFFLL;     /* Set field */
                           inds |= 0x00900000000LL;
                        } else {
                           reason = STOP_FIELD;
                        }
                     }
                     temp &= ldmask[f2-f1+1];
                     /* Compute final sign */
                     if ((opcode & 0x10f) == (OP_AAS1 & 0x10f)) {
                          sign = (uint8)(utmp & 0xf);
                     } else if (sign & 0xc) {
                          sign = ASIGN >> 40;
                     } else if (sign & 2) {
                          sign = MSIGN >> 40;
                     } else {
                          sign = PSIGN >> 40;
                     }
                     /* Check for sign change, and other data in word */
                     if (MBR != 0 && ((sign != utmp && f1 != 0 && f2 != 9) ||
                         (sign == (ASIGN >> 40) && utmp != (ASIGN >> 40)))) {
                        if (inds & 0xF000000000LL) {
                           inds &= 0xF0FFFFFFFFFLL;     /* Set field */
                           inds |= 0x09000000000LL;
                        } else {
                           reason = STOP_SIGN;
                        }
                     }
                     /* Restore results and sign */
                     MBR |= DMASK & temp << ((9 - f2) * 4);
                     MBR |= ((t_uint64)sign) << 40;
                     WriteP(MA, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;

                /* AC :: memory */
                case OP_C1: case OP_C2: case OP_C3:
                     temp = (rdmask[f1] & MBR) >> ((9 - f2) * 4);
                     temp |= MBR & SMASK;       /* Copy sign */
                     inds &= 0xFFFFF000FFFLL;
                     switch(dec_cmp(temp, AC[op2])) {
                     case -1: inds |= 0x0000001000LL; break;
                     case 1: inds |= 0x0000100000LL; break;
                     case 0: inds |= 0x0000010000LL; break;
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[op2];
                     }
                     break;

                /* Clear Memory, store */
                case OP_ZST1: case OP_ZST2: case OP_ZST3:
                     MBR = SMASK & AC[op2];     /* Same sign as AC */
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF;
                     }
                /* Store digit */
                case OP_STD1: case OP_STD2: case OP_STD3:
                /* Store AC */
                case OP_ST1: case OP_ST2: case OP_ST3:
                     /* Check for sign change, and other data in word */
                     if ((opcode & 0x10f) == (OP_ST1 & 0x10f)) {
                         if ((AC[op2] & SMASK) != (MBR & SMASK) &&
                             (MBR & DMASK) != 0) {
                            if (inds & 0xF000000000LL) {
                               inds &= 0xF0FFFFFFFFFLL; /* Set field */
                               inds |= 0x09000000000LL;
                            } else {
                               reason = STOP_SIGN;
                               break;
                            }
                         }
                         /* Restore set */
                         MBR &= DMASK;
                         MBR |= SMASK & AC[op2];
                     }
                     MBR &= ~(rdmask[f1] & fdmask[f2+1]); /* Clear digits. */
                     temp = AC[op2] & DMASK;
                     /* Check overflow */
                     if (temp & ~ldmask[f2-f1+1]) {
                        if (inds & 0x0F00000000LL) {
                           inds &= 0xFF0FFFFFFFFLL;     /* Set field */
                           inds |= 0x00900000000LL;
                        } else {
                           reason = STOP_FIELD;
                           break;
                        }
                     }
                     temp &= ldmask[f2-f1+1];
                     /* Restore results */
                     MBR |= DMASK & (temp << ((9 - f2) * 4));
                     WriteP(MA, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;

                /* Branch AC zero */
                case OP_BZ1: case OP_BZ2: case OP_BZ3:
                     if ((AC[op2] & DMASK) == 0)
                        IC = MA;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Branch AC overflow */
                case OP_BV1: case OP_BV2: case OP_BV3:
                     if ((inds >> (4 * (3 - op2))) & 0x1) {
                        IC = MA;
                        inds &= ~(0xFLL << (4 * (3 - op2)));/* clear overflow */
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Branch AC minus */
                case OP_BM1: case OP_BM2: case OP_BM3:
                     if ((AC[op2] & SMASK) == MSIGN)
                        IC = MA;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Multiply */
                case OP_M:
                     /* Multiplicand in AC[3] */
                     AC[1] = AC[2] = 0;
                     sign = (uint8)(((MBR & SMASK) >> 40) & 0xf);
                     MBR = (rdmask[f1] & MBR) >> ((9 - f2) * 4);
                     sign = (((AC[3] & SMASK) >> 40) != sign) ? 6 : 9;
                     /* Multiply MBR * AC[3] result to AC[1],AC[2] <low> */
                     for(tmp = 36; tmp >= 0; tmp-=4) {
                         int digit = (AC[3] >> tmp) & 0xf;
                         AC[1] <<= 4;
                         AC[1] &= DMASK;
                         AC[1] |= (AC[2] >> 36) & 0xf;
                         AC[2] <<= 4;
                         AC[2] &= DMASK;
                         if (digit != 0) {
                            sim_interval -= (CPU_MODEL == 0x0)?
                                                        12*digit: digit;
                            /* Form product */
                            mul_step(&AC[2], MBR, digit);
                            digit = (AC[2] >> 40) & 0xff;
                            if (digit != 0)
                                dec_add_noov(&AC[1], digit);
                            AC[2] &= DMASK;
                         } else
                            sim_interval -= (CPU_MODEL == 0x0)? 2: 0;
                     }
                     /* Set sign */
                     AC[1] |= ((t_uint64)sign) << 40;
                     AC[2] |= ((t_uint64)sign) << 40;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;

                /* Divide */
                case OP_D:
                     /* dividend AC[1],AC[2] */
                     /* divisor in MBR */
                     sign = (uint8)(((MBR & SMASK) >> 40) & 0xf);
                     AC[3] = (rdmask[f1] & MBR) >> ((9 - f2) * 4);
                     if (AC[3] == 0) {
                        AC[3] |= ((t_uint64)sign) << 40;
                        reason = STOP_DIV;
                        break;
                     }
                     utmp = (AC[1] & SMASK) >> 40;
                    /* Compute sign */
                     if (utmp != 3 && utmp != sign)
                        utmp ^= 0xf;
                    /* If any are alpha, result is alpha */
                     if (sign == 3 || utmp == 3 || utmp == 0xc)
                        utmp = 3;
                     /* Divide AC1,AC2 by AC3 */
                     AC[1] &= DMASK;
                     AC[2] &= DMASK;
                     dec_comp(&AC[3]);
                     for(tmp = 10; tmp != 0; --tmp)
                         div_step(AC[3]);
                     dec_comp(&AC[3]);
                    /* Fix signs */
                     AC[1] |= ((t_uint64)utmp) << 40;
                     AC[2] |= ((t_uint64)sign) << 40;
                     AC[3] &= DMASK;
                     AC[3] |= ((t_uint64)sign) << 40;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;

                /* Shift control */
                case OP_SC:
                     op2 = (MA / 1000) % 10;
                     if (op2 == 0 || op2 > 3)
                        break;
                     utmp = MA % 100;
                     if (utmp > 10)
                        break;
                     temp = AC[op2] & DMASK;
                     switch ((MA / 100) % 10) {
                     case 0:
                        temp >>= utmp * 4;
                        break;
                     case 1:
                        if (utmp != 0) {
                           temp >>= (utmp - 1) * 4;
                           f1 = temp & 0xF;
                           temp >>= 4;
                           if (f1 > 5)
                              dec_add(&temp, 1LL);
                        }
                        break;
                     case 2:
                        temp <<= utmp * 4;
                        break;
                     case 3:
                        utmp = 0;
                        if (temp != 0) {
                            while((temp & dmask[10]) == 0) {
                                utmp++;
                                temp <<= 4;
                            }
                        }
                        if (IX) {
                            MBR = ReadP(IX);
                            MBR &= ~IMASK;
                            MBR &= DMASK;
                            MBR |= PSIGN;
                            if (utmp > 10)      /* BCD adjust */
                                utmp += 6;
                            MBR |= ((t_uint64)utmp) << 16;
                            WriteP(IX, MBR);
                        }
                        break;
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = AC[op2];
                     }
                     AC[op2] &= SMASK;
                     AC[op2] |= DMASK & temp;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[op2];
                     }
                     break;
                /* Coupled shift control */
                case OP_CSC:
                     utmp = MA % 100;   /* Number of shifts */
                     if (utmp > 20)
                        break;
                     op2 = (MA / 100) % 10;     /* Operand */
                     f2 += f1 * 10;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = AC[1];
                     }
                     switch (op2) {
                     default:
                     case 0:    /* Shift right coupled */
                         sign = (AC[1] >> 40) & 0xf;
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         for(;utmp != 0; utmp--) {
                             f1 = AC[1] & 0xf;
                             AC[1] >>= 4;
                             AC[2] |= ((t_uint64)f1) << 40;
                             AC[2] >>= 4;
                        }
                        break;
                     case 1:    /* Shift right coupled and round */
                         sign = (AC[1] >> 40) & 0xf;
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         for(;utmp != 0; utmp--) {
                             f1 = AC[1] & 0xf;
                             AC[1] >>= 4;
                             AC[2] |= ((t_uint64)f1) << 40;
                             f1 = AC[2] & 0xf;
                             AC[2] >>= 4;
                        }
                        if (f1 > 5)
                           if (dec_add(&AC[2], 1LL))
                                if (dec_add(&AC[1], 1LL))
                                    inds |= 1LL << 8; /* Set overflow */
                        break;
                     case 2:    /* Shift left coupled */
                         sign = (AC[2] >> 40) & 0xf;
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         for(;utmp != 0; utmp--) {
                             AC[1] <<= 4;
                             AC[1] &= DMASK;
                             AC[1] |= (AC[2] >> 36) & 0xf;
                             AC[2] <<= 4;
                             AC[2] &= DMASK;
                        }
                        break;
                     case 3:    /* Shift left and count coupled */
                         sign = (AC[2] >> 40) & 0xf;
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         utmp = 0;
                         if (AC[1] != 0 || AC[2] != 0) {
                             while((AC[1] & dmask[10]) == 0) {
                                 AC[1] <<= 4;
                                 AC[1] &= DMASK;
                                 AC[1] |= (AC[2] >> 36) & 0xf;
                                 AC[2] <<= 4;
                                 AC[2] &= DMASK;
                                 utmp++;
                             }
                        }
                        if (IX) {
                            MBR = ReadP(IX);
                            MBR &= ~IMASK;
                            MBR &= DMASK;
                            if (utmp > 10)      /* BCD adjust */
                                utmp += 6;
                            MBR |= ((t_uint64)utmp) << 16;
                            WriteP(IX, MBR);
                        }
                        break;
                     case 4:    /* Shift right from point ac1 */
                         sign = (AC[1] >> 40) & 0xf;
                         tmp = (MA / 1000) % 10;        /* Split point */
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         for(;utmp != 0; utmp--) {
                             f1 = AC[1] & 0xf;
                             AC[1] = (AC[1] & fdmask[tmp]) |
                                        ((AC[1] & rdmask[tmp]) >> 4);
                             AC[2] |= ((t_uint64)f1) << 40;
                             AC[2] >>= 4;
                        }
                        break;
                     case 5:    /* Shift left from point ac1 */
                         sign = (AC[2] >> 40) & 0xf;
                         tmp = (MA / 1000) % 10;        /* Split point */
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         for(;utmp != 0; utmp--) {
                             AC[1] = (AC[1] & rdmask[tmp]) |
                                        ((AC[1] & fdmask[tmp]) << 4);
                        }
                        break;
                     case 6:    /* Shift right from point ac2 */
                         sign = (AC[2] >> 40) & 0xf;
                         tmp = (MA / 1000) % 10;        /* Split point */
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         for(;utmp != 0; utmp--) {
                             AC[2] = (AC[2] & fdmask[tmp]) |
                                        ((AC[2] & rdmask[tmp]) >> 4);
                        }
                        break;
                     case 7:    /* Shift left from point ac2 */
                         sign = (AC[2] >> 40) & 0xf;
                         tmp = (MA / 1000) % 10;        /* Split point */
                         AC[1] &= DMASK;
                         AC[2] &= DMASK;
                         for(;utmp != 0; utmp--) {
                             AC[1] <<= 4;
                             AC[1] &= DMASK;
                             AC[1] |= ((AC[2] & fdmask[tmp]) >> 36) & 0xf;
                             AC[2] = (AC[2] & rdmask[tmp]) |
                                        ((AC[2] & fdmask[tmp]) << 4);
                        }
                        break;
                     }
                     AC[1] |= ((t_uint64)sign) << 40;
                     AC[2] |= ((t_uint64)sign) << 40;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;
                /* AC1 : |Memory | */
                case OP_CA:
                     MBR = (rdmask[f1] & MBR) >> ((9 - f2) * 4);
                     inds &= 0xFFFFF000FFFLL;
                     switch(dec_cmp(MBR & DMASK, AC[1]&DMASK)) {
                     case -1: inds |= 0x0000001000LL; break;
                     case 1: inds |= 0x0000100000LL; break;
                     case 0: inds |= 0x0000010000LL; break;
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;
                /* Compare digit */
                case OP_CD:
                     inds &= 0xFFFFF000FFFLL;
                     MBR >>= ((9 - f2) * 4);
                     MBR &= 0xF;
                     if (MBR > f1)
                        inds |= 0x0000100000LL;
                     else if (MBR < f1)
                        inds |= 0x0000001000LL;
                     else
                        inds |= 0x0000010000LL;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOAFT;
                     }
                     break;
                /* Branch load index */
                case OP_BLX:
                     upd_idx(&M[IX], IC);
                /* Branch */
                case OP_B:
                     IC = MA;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Branch low */
                case OP_BL:
                     if (inds & 0x0000001000LL)
                        IC = MA;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Branch high */
                case OP_BH:
                     if (inds & 0x0000100000LL)
                        IC = MA;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Branch equal */
                case OP_BE:
                     if (inds & 0x0000010000LL)
                        IC = MA;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Extended memory on 7074 */
                case OP_EXMEM:
                     if (CPU_MODEL == 0x1 && cpu_unit.flags & OPTION_EXTEND) {
                        switch(f1) {
                        case 0:
                                if (emode)
                                   IC = MA;
                                break;
                        case 1:
                                emode = 0;
                                break;
                        case 2:
                                emode = 1;
                                break;
                        }
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                case OP_HB:
                     IC = MA;
                     /* fall through */
                case OP_HP:
                     reason = STOP_HALT;
                     /* fall through */
                case OP_NOP:
                     /* fall through */
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                     }
                     break;
                /* Floating point option */
                /* Floating point: sEEMMMMMMM  EE+50*/
                case OP_FD: /* AC2 <- 0, AC1,AC2 <- AC1/M, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     AC[2] = 0;
                case OP_FDD: /* AC1,2 <- AC1/M, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((AC[1] & SMASK) == ASIGN || (MBR & SMASK) == ASIGN) {
                        reason = STOP_SIGN;
                        break;
                     }

                     /* Extract signs */
                     sign = (MBR & SMASK) == MSIGN;
                     if ((AC[1] & SMASK) == MSIGN)
                         sign ^= 3;

                     /* Extract exponents */
                     f1 = (((AC[1] >> 36) & 0xf) * 10) +
                                                ((AC[1] >> 32) & 0xf);
                     tmp = (((MBR >> 36) & 0xf) * 10) + ((MBR >> 32) & 0xf);
                     tmp = 51 + (f1 - tmp);
                     /* Clear exponents */
                     MBR &= FMASK;
                     if (MBR == 0) {
                        reason = STOP_DIV;
                        break;
                     }
                     AC[1] &= FMASK;
                     AC[2] &= FMASK;
                     AC[1] = NINES - AC[1];  /* One's compliment AC[1]&AC[2] */
                     AC[2] = FNINES - AC[2];
                     dec_add(&AC[2], 1LL);
                     if (AC[2] & EMASK) {       /* Propagate carry */
                        dec_add(&AC[1], AC[2] >> 32);
                        AC[2] &= FMASK;
                     }
                     /* Check if overflow on first try */
                     temp = AC[1];
                     if (dec_add(&temp, MBR)) {
                        /* Yes, shift right before starting */
                        AC[1] <<= 4;
                        AC[1] &= DMASK;
                        AC[2] <<= 4;
                        AC[1] |= (AC[2] >> 32) & 0xf;
                        AC[2] &= FMASK;
                        tmp--;
                     } else
                        f1++;
                     utmp = 8;
                     do {
                        int     cnt = 0;
                        /* Repeated subtract until overflow */
                        while(1) {
                            temp = AC[1];
                            if (dec_add(&temp, MBR))
                                break;
                            cnt++;
                            AC[1] = temp;       /* Restore AC if not less */
                            if (cnt > 9) {      /* Catch divide check */
                                reason = STOP_DIV;
                                goto done;
                            }
                        }
                        /* Shift right coupled */
                        AC[1] <<= 4;
                        AC[1] &= DMASK;
                        AC[2] <<= 4;
                        AC[1] |= (AC[2] >> 32) & 0xf;
                        AC[2] &= FMASK;
                        AC[2] |= cnt;   /* Put in count */
                     } while (--utmp != 0);

                     /* Restore remainder to correct value */
                     dec_comp(&AC[1]);

                     /* Now exchange AC1 & AC2 to get results in right place */
                     temp = AC[1];
                     AC[1] = AC[2];
                     AC[2] = temp;

                     /* Check overflow */
                     if (tmp > 99) {
                        inds |= 0x0001000000LL;         /* Set overflow */
                        tmp = 0;
                     }
                    /* Save exponents */
                     bin_dec(&AC[1], tmp, 8, 2);        /* Restore exponent */
                     if (f1 < 8)
                        AC[2] = 0;
                     else {
                        f1 -= 8;
                        AC[2] >>= 4;
                        if ((AC[2] & EMASK) != 0) {
                           if (f1-- != 0)
                               AC[2] >>= 4;
                           else
                               AC[2] = f1 = 0;
                        }
                        bin_dec(&AC[2], f1, 8, 2);      /* Restore exponent */
                     }

                    /* Fix signs */
                     if (sign & 1) {
                         AC[1] |= MSIGN;
                     } else {
                         AC[1] |= PSIGN;
                     }

                     if (sign & 3) {
                         AC[2] |= MSIGN;
                     } else {
                         AC[2] |= PSIGN;
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;

                /* Floating point multiply */
                case OP_FM: /* AC1,2 <- AC1 * M, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((AC[1] & SMASK) == ASIGN || (MBR & SMASK) == ASIGN) {
                        reason = STOP_SIGN;
                        break;
                     }

                     /* Extract signs */
                     sign = (MBR & SMASK) == MSIGN;
                     sign ^= (AC[1] & SMASK) == MSIGN;

                     /* Extract exponents */
                     utmp = (((AC[1] >> 36) & 0xf) * 10) +
                                                ((AC[1] >> 32) & 0xf);
                     f1 = (((MBR >> 36) & 0xf) * 10) + ((MBR >> 32) & 0xf);
                     utmp += f1;
                     utmp -= 50;
                     /* Clear exponents */
                     MBR &= FMASK;
                     temp = AC[1];
                     AC[1] = 0;
                     AC[2] = 0;
                     /* Multiplicand in AC[3] */
                     /* Multiply MBR * AC[1] result to AC[1],AC[2] <low> */
                     for(tmp = 28; tmp >= 0; tmp-=4) {
                         int digit = (temp >> tmp) & 0xf;
                         AC[1] <<= 4;
                         AC[1] &= DMASK;
                         AC[1] |= (AC[2] >> 28) & 0xf;
                         AC[2] <<= 4;
                         AC[2] &= FMASK;
                         if (digit != 0) {
                            sim_interval -= (CPU_MODEL == 0x0)? 12*digit:digit;
                            /* Form product */
                            mul_step(&AC[2], MBR, digit);
                            digit = (AC[2] >> 32) & 0xff;
                            if (digit != 0)
                                dec_add(&AC[1], digit);
                            AC[2] &= FMASK;
                         } else
                            sim_interval -= (CPU_MODEL == 0x0)? 2: 0;
                     }
                     /* Check if needs to be normalized */
                     if ((AC[1] & NMASK) == 0) {
                         AC[1] <<= 4;
                         AC[1] |= (AC[2] >> 28) & 0xf;
                         AC[2] <<= 4;
                         AC[2] &= FMASK;
                         utmp--;
                     }
                     /* Check overflow */
                     if (utmp > 99) {
                        inds |= 0x0001000000LL;         /* Set overflow */
                        utmp = 0;
                     }
                    /* Save exponents */
                     bin_dec(&AC[1], utmp, 8, 2);       /* Restore exponent */
                     if (utmp < 8)
                        AC[2] = 0;
                     else
                        bin_dec(&AC[2], utmp-8, 8, 2);  /* Restore exponent */
                     /* Set signs */
                     if (sign) {
                        AC[1] |= MSIGN;
                        AC[2] |= MSIGN;
                     } else {
                        AC[1] |= PSIGN;
                        AC[2] |= PSIGN;
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;

                /* Floating point round */
                case OP_FR: /* AC1 <- +.5, AC2 <- 0 */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = AC[1];
                     }
                     if (((AC[2] >> 28) & 0xf) > 5) {
                        temp = AC[1] & SMASK;
                        AC[1] &= DMASK;
                        if (dec_add(&AC[1], 1LL)) {
                            inds |= 0x0001000000LL;
                            AC[1] = 0;
                        }
                        AC[1] |= temp;          /* Restore sign */
                     }
                     AC[2] = PSIGN;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;

                /* Floating subtract absolute */
                case OP_FSA: /* AC2 <- 0, AC1,2 <- AC1,2-|M|, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((AC[1] & SMASK) == ASIGN || (MBR & SMASK) == ASIGN) {
                        reason = STOP_SIGN;
                        break;
                     }
                     sign = 1;
                     AC[2] = 0x65000000000LL;
                     goto float_add;
                /* Floating subtract */
                case OP_FS: /* AC2 <- 0, AC1,2 <- AC1,2 - M, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((AC[1] & SMASK) == ASIGN || (MBR & SMASK) == ASIGN) {
                        reason = STOP_SIGN;
                        break;
                     }
                     sign = (MBR & SMASK) != MSIGN;
                     AC[2] = 0x65000000000LL;
                     goto float_add;
                /* Floating add absolute */
                case OP_FAA: /* AC2 <- 0, AC1,2 <- AC1,2+|M|, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((AC[1] & SMASK) == ASIGN || (MBR & SMASK) == ASIGN) {
                        reason = STOP_SIGN;
                        break;
                     }
                     sign = 0;
                     goto float_add;
                /* Floating add */
                case OP_FA:  /* AC2 <- 0, AC1,2 <- AC1,2+M, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     AC[2] = 0x65000000000LL;
                /* Floating add double */
                case OP_FAD: /* AC1,2 <- AC1,2 + M, norm */
                /* Floating add double, suppress norm */
                case OP_FADS: /* AC1,2 <- AC1,2 + M */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((AC[1] & SMASK) == ASIGN || (MBR & SMASK) == ASIGN) {
                        reason = STOP_SIGN;
                        goto done;
                     }
                     sign = (MBR & SMASK) == MSIGN;
        float_add:
                     /* Extract sign */
                     if ((AC[1] & SMASK) == MSIGN)
                        sign ^= 3;

                     /* Compare exponents */
                     utmp = (((AC[1] >> 36) & 0xf) * 10) +
                                                ((AC[1] >> 32) & 0xf);
                     f1 = (((MBR >> 36) & 0xf) * 10) + ((MBR >> 32) & 0xf);
                     tmp = ((signed int)utmp) - ((signed int)f1);
                     /* Clear exponents and sign */
                     MBR   &= FMASK;
                     AC[1] &= FMASK;
                     AC[2] &= FMASK;
                     temp = 0;
                     if (tmp > 0) {     /* AC Bigger */
                        /* Shift MBR */
                         if (tmp > 16)
                            goto float_norm;
                         while(tmp > 0) {
                            temp |= ((t_uint64)(MBR & 0xf)) << 32;
                            MBR >>= 4;
                            temp >>= 4;
                            tmp--;
                        }
                     } else if (tmp < 0) {      /* AC Smaller */
                         utmp = f1;
                        /* Shift AC */
                         if (tmp > -16) {
                             while(tmp < 0) {
                                 AC[2] |= ((t_uint64)(AC[1] & 0xf)) << 32;
                                 AC[1] >>= 4;
                                 AC[2] >>= 4;
                                 tmp++;
                            }
                        } else {
                            AC[1] = MBR;
                            AC[2] = 0;
                            goto float_norm;
                        }
                     }
                     /* Do actual addition now */
                     if (sign & 1) {
                          /* Do compliment add */
                          dec_add(&AC[2], FNINES - temp);
                          dec_add(&AC[2], 1LL);
                          dec_add(&AC[1], FNINES - MBR);
                          /* Carry between halfs */
                          if (AC[2] & EMASK) { /* Carry out */
                             dec_add(&AC[1], (AC[2] >> 32) & 0xff);
                             AC[2] &= FMASK;
                          }

                          if ((AC[1] & EMASK) == 0) {
                              AC[2] = FNINES - (AC[2] & FMASK);
                              AC[1] = FNINES - (AC[1] & FMASK);
                              dec_add(&AC[2], 1LL);
                              if (AC[2] & EMASK) { /* Carry out */
                                 dec_add(&AC[1], (AC[2] >> 32) & 0xff);
                                 AC[2] &= FMASK;
                              }
                              sim_interval -= (CPU_MODEL == 0x0)?
                                                        12*(f2 - f1)/3: 1;
                              sign ^= 3;
                          }
                          AC[1] &= FMASK;
                     } else {
                        /* Add low then high */
                          dec_add(&AC[2], temp);
                          dec_add(&AC[1], MBR);
                          if (AC[2] & EMASK) /* Carry out */
                             dec_add(&AC[1], (AC[2] >> 32) & 0xf);
                     }
                     if (AC[1] & EMASK) {
                          AC[2] |= ((t_uint64)(AC[1] & 0xf)) << 32;
                          AC[1] >>= 4;
                          AC[2] >>= 4;
                          utmp++;
                     }
        float_norm:
                     /* Normalize result */
                     tmp = utmp;
                     if (opcode != OP_FADS && AC[1] != 0 && AC[2] != 0) {
                         while((AC[1] & NMASK) == 0) {
                             AC[1] <<= 4;
                             AC[1] |= (AC[2] >> 28) & 0xf;
                             AC[2] <<= 4;
                             AC[2] &= FMASK;
                             tmp--;
                         }
                     }
                     /* Set exponent if error */
                     if (AC[1] == 0 && AC[2] == 0)
                        tmp = 50;
                     if (tmp < 0) {
                        inds |= 0x0010000000LL;         /* Set underflow */
                        tmp = 0;
                     }
                     if (tmp > 99) {
                        inds |= 0x0001000000LL;         /* Set overflow */
                        tmp = 0;
                     }
                    /* Restore exponents */
                     bin_dec(&AC[1], tmp, 8, 2);        /* Restore exponent */
                     if (tmp < 8)
                        AC[2] = 0;
                     else
                        bin_dec(&AC[2], tmp-8, 8, 2);   /* Restore exponent */
                     /* Set signs */
                     if (sign & 2) {
                        AC[1] |= MSIGN;
                        AC[2] |= MSIGN;
                     } else {
                        AC[1] |= PSIGN;
                        AC[2] |= PSIGN;
                     }
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;

                /* Floating zero and add */
                case OP_FZA: /* AC2 <- 0, AC1 <- M, norm */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     /* Check sign */
                     if ((MBR & SMASK) == ASIGN) {
                         reason = STOP_SIGN;
                         break;
                     }
                     AC[2] = 0;
                     /* Extract exponent */
                     tmp = (((MBR >> 36) & 0xf) * 10) + ((MBR >> 32) & 0xf);
                     AC[1] = MBR & FMASK;
                     if (AC[1] != 0) {
                         while((AC[1] & NMASK) == 0) {
                             tmp--;
                             AC[1] <<= 4;
                         }
                     } else
                         tmp = 50;
                     if (tmp < 0) {
                        inds |= 0x0010000000LL;         /* Set underflow */
                        tmp = 0;
                     }
                     bin_dec(&AC[1], tmp, 8, 2);        /* Restore exponent */
                     AC[1] |= MBR & SMASK;
                     AC[2] |= MBR & SMASK;
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = AC[1];
                     }
                     break;

                case OP_FBU: /* Branch floating overflow */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((inds & 0x000F000000LL) != 0)
                        IC = MA;
                     inds &= 0xFFFF0FFFFFFLL;           /* Clear flag */
                     break;

                case OP_FBV: /* Branch floating underflow */
                     if ((cpu_unit.flags & OPTION_FLOAT) == 0) {
                         reason = STOP_UUO;
                         break;
                     }
                     if ((inds & 0x00F0000000LL) != 0)
                        IC = MA;
                     inds &= 0xFFF0FFFFFFFLL;           /* Clear flag */
                     break;

                /* Load with interchange */
                case OP_XLIN:
                     MBR = (MBR & (SMASK|OMASK)) | ((MBR >> 16) & AMASK) |
                                ((MBR << 16) & IMASK);
                /* Load index */
                case OP_XL:
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOAFT;
                     }
                     break;
                /* Unload index */
                case OP_XU:
                     MBR = ReadP(IX);
                     WriteP(MA, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;
                /* Index zero subtract */
                case OP_XZS:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                     }
                     upd_idx(&MBR, MA);
                     MBR &= DMASK;
                     MBR |= MSIGN;
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;
                /* Index zero add */
                case OP_XZA:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                     }
                     upd_idx(&MBR, MA);
                     MBR &= DMASK;
                     MBR |= PSIGN;
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;
                /* Index subtract */
                case OP_XS:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                     }
                     temp = dec_bin_idx(MBR);
                     sign = (uint8)(((MBR & SMASK)>> 40) & 0xf);
                     MBR &= DMASK;
                     switch(sign) {
                     case 0x6:  /* + -  tc b add */
                          temp += MA;
                          break;
                     default:
                     case 0x3:  /* + a  add res a */
                     case 0x9:  /* + +  add */
                          temp = ~temp + MA + 1;
                          if (temp & 0x8000) {
                              temp = ~temp + 1;
                              if (sign == 0x9) sign = 0x6;
                          }
                          break;
                     }
                     MBR |= ((t_uint64)sign) << 40;
                     upd_idx(&MBR, (uint32)temp);
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;
                /* Index add */
                case OP_XA:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                     }
                     temp = 0;
                     upd_idx(&temp, MA);
                     sign = (uint8)(((MBR & SMASK)>> 40) & 0xf);
                     MBR &= DMASK;
                     switch(sign) {
                     default:
                     case 0x3:  /* + a  add res a */
                     case 0x9:  /* + +  add */
                          dec_add(&temp, MBR & ((emode)?IMASK2:IMASK));
                          break;
                     case 0x6:  /* + -  tc b add */
                          if (temp == 0)
                             break;
                          dec_comp(&temp);
                          dec_add(&temp, MBR & ((emode)?IMASK2:IMASK));
                          if (temp & ((emode)?XMASK2:XMASK)) {
                              dec_comp(&temp);
                              sign = 0x9;
                          }
                          break;
                     }
                     MBR |= ((t_uint64)sign) << 40;
                     MBR &= (emode)?~IMASK2:~IMASK;
                     MBR |= temp;
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;
                /* Index set non-indexing */
                case OP_XSN:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                     }
                     bin_dec(&MBR, MA, 0, 4);
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     break;
                /* Branch if index word index = 0 */
                case OP_BXN:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                         hst[hst_p].ic |= HIST_NOAFT;
                     }
                     if ((MBR & IMASK) != 0)
                        IC = MA;
                     break;
                /* Branch decrement index */
                case OP_BDX:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                     }
                     temp = MBR & IMASK;
                     dec_add(&temp, (emode)?0x999990000LL:0x99990000LL);
                     MBR &= (emode)?~IMASK2:~IMASK;
                     MBR |= temp & (emode)?IMASK2:IMASK;
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     goto checkix;
                /* Branch increment index */
                case OP_BIX:
                     temp = MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                     }
                     dec_add(&temp, 0x10000LL);
                     MBR &= (emode)?~IMASK2:~IMASK;
                     MBR |= temp & ((emode)?IMASK2:IMASK);
                     WriteP(IX, MBR);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].after = MBR;
                     }
                     goto checkix;
                /* Branch compared index */
                case OP_BCX:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                         hst[hst_p].ic |= HIST_NOAFT;
                     }
                checkix:
                     temp = (MBR & IMASK) >> 16;
                     MBR &= AMASK;
                     dec_comp(&temp);
                     if(dec_add(&temp, MBR))
                         IC = MA;
                     break;
                /* Branch if index word index minus */
                case OP_BXM:
                     MBR = ReadP(IX);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].before = MBR;
                         hst[hst_p].ic |= HIST_NOAFT;
                     }
                     if ((MBR & SMASK) == MSIGN)
                        IC = MA;
                     break;
                /* Field over flow control */
                case OP_BFLD:
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF;
                     }
                     switch (f2) {
                     case 0:
                           if ((inds & 0x0F00000000LL) == 0x0900000000LL) {
                                IC = MA;
                                inds ^= 0x0F00000000LL;
                           }
                           break;
                     case 1:
                           if ((inds & 0x0F00000000LL) != 0x0500000000LL) {
                                inds &= 0xFF0FFFFFFFFLL;
                                inds |= 0x00500000000LL;
                           }
                           if (hst_lnt) {  /* history enabled? */
                               hst[hst_p].ic |= HIST_NOEA;
                           }
                           break;
                     case 2:
                           if ((inds & 0x0F00000000LL) == 0x0900000000LL)
                                reason = STOP_SIGN;
                           else
                                inds &= 0xFF0FFFFFFFFLL;
                           if (hst_lnt) {  /* history enabled? */
                               hst[hst_p].ic |= HIST_NOEA;
                           }
                           break;
                     }
                     break;
                /* Sign change control */
                case OP_CS:
                     switch (f2) {
                     case 0:
                           inds &= 0xFFFFF000FFFLL;
                           utmp = ((MBR >> 40) & 0xf);
                           if (utmp > f1)
                                inds |= 0x00000100000LL;
                           else if (utmp < f1)
                                inds |= 0x00000001000LL;
                           else
                                inds |= 0x00000010000LL;
                           if (hst_lnt) {  /* history enabled? */
                               hst[hst_p].ic |= HIST_NOAFT;
                           }
                           break;
                      case 1:
                           MBR &= DMASK;
                           MBR |= ((t_uint64)f1) << 40;
                           WriteP(MA, MBR);
                           if (hst_lnt) {  /* history enabled? */
                               hst[hst_p].after = MBR;
                           }
                           break;
                      case 2:
                           if ((inds & 0xF000000000LL) == 0) {
                                inds |= 0x5000000000LL;
                           }
                           if (hst_lnt) {  /* history enabled? */
                               hst[hst_p].ic |= HIST_NOEA|HIST_NOBEF|HIST_NOAFT;
                           }
                           break;
                      case 3:
                           if ((inds & 0xF000000000LL) == 0x9000000000LL)
                                reason = STOP_SIGN;
                           else
                                inds &= 0xF0FFFFFFFFFLL;
                           if (hst_lnt) {  /* history enabled? */
                               hst[hst_p].ic |= HIST_NOEA|HIST_NOBEF|HIST_NOAFT;
                           }
                           break;
                      case 4:
                           if ((inds & 0xF000000000LL) == 0x9000000000LL) {
                                IC = MA;
                                inds ^= 0xF000000000LL;
                           }
                           if (hst_lnt) {  /* history enabled? */
                               hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                           }
                           break;
                      }
                      break;

                /* Record scatter */
                case OP_RS:
                /* Generate source address */
                     temp = M[IX];
                     utmp = dec_bin_idx(temp);
                     do {
                       uint32 dst, limit;
                       MBR = ReadP(MA++);       /* Grab next RDW */
                       get_rdw(MBR, &dst, &limit);
                       while(dst <= limit) {
                             WriteP(dst++, ReadP(utmp++));
                             if (utmp > MEMSIZE)
                                utmp = 0;
                       }
                     } while ((MBR & SMASK) != MSIGN);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOAFT;
                     }
                     break;
                /* Record gather */
                case OP_RG:
                     /* Generate destination address */
                     temp = M[IX];
                     utmp = dec_bin_idx(temp);
                     do {
                          uint32 src, limit;
                          MBR = ReadP(MA++);    /* Grab next RDW */
                          get_rdw(MBR, &src, &limit);
                          while(src <= limit) {
                               WriteP(utmp++, ReadP(src++));
                               if (utmp > MEMSIZE)
                               utmp = 0;
                          }
                     } while ((MBR & SMASK) != MSIGN);
                     if (hst_lnt) {  /* history enabled? */
                         hst[hst_p].ic |= HIST_NOAFT;
                     }
                     break;
                /* Edit alpha to numeric */
                case OP_ENB:
                case OP_ENS:
                case OP_ENA:
                     /* Generate source address */
                     temp = M[IX];
                     utmp = dec_bin_idx(temp);
                     do {
                          uint32 dst, limit;
                          MBR = ReadP(MA++);    /* Grab next RDW */
                          get_rdw(MBR, &dst, &limit);
                          while(dst <= limit) {
                                t_uint64   buffer;
                                f1 = (opcode == OP_ENB)? 0: 1;
                                temp = ReadP(utmp++);
                                if (utmp > MEMSIZE)
                                   utmp = 0;
                                buffer = 0x9090909090LL|ASIGN;
                                for(tmp = 9; tmp > 4; tmp--) {
                                    if (f1 == 0) {
                                        if ((temp & dmask[tmp+1]) == 0)
                                            buffer &= ~(0xFFLL << ((tmp-4)*8));
                                        else
                                            f1 = 1;
                                    }
                                    buffer |= (temp & dmask[tmp+1]) << ((tmp-4)*8);
                                }
                                WriteP(dst++, buffer);
                                if (opcode == OP_ENS) {
                                    switch(temp & SMASK) {
                                    case ASIGN:
                                        buffer = 0x9090909090LL|ASIGN;
                                        break;
                                    case PSIGN:
                                        buffer = 0x9090909060LL|ASIGN;
                                        break;
                                    case MSIGN:
                                        buffer = 0x9090909070LL|ASIGN;
                                        break;
                                    }
                                } else
                                    buffer = 0x9090909090LL|ASIGN;
                                for(; tmp >= 0; tmp--) {
                                    if (f1 == 0) {
                                        if ((temp & dmask[tmp+1]) == 0)
                                            buffer &= ~(0xFFLL << (tmp * 8));
                                        else
                                            f1 = 1;
                                    }
                                    buffer |= (temp & dmask[tmp+1]) << (tmp*8);
                                }
                                WriteP(dst++, buffer);
                          }
                        } while ((MBR & SMASK) != MSIGN);
                        if (hst_lnt) {  /* history enabled? */
                             hst[hst_p].ic |= HIST_NOAFT;
                        }
                        break;
                case OP_EAN:
                        /* Generate destination address */
                        temp = M[IX];
                        utmp = dec_bin_idx(temp);
                        do {
                          uint32 src, limit;
                          MBR = ReadP(MA++);    /* Grab next RDW */
                          get_rdw(MBR, &src, &limit);
                          while(src <= limit) {
                                t_uint64   buffer = 0;
                                temp = ReadP(src++);
                                for(tmp = 8, f1 = 16; tmp >= 0; tmp-= 2) {
                                    buffer |= (temp & dmask[tmp+1]) << f1;
                                    f1 += 4;
                                }
                                temp = ReadP(src++);
                                for(tmp = 8, f1 = 16; tmp >= 0; tmp-=2) {
                                    buffer |= (temp & dmask[tmp+1]) >> f1;
                                    f1 -= 4;
                                }
                                if ((temp & 0xF0LL) == 0x70LL)
                                    buffer |= MSIGN;
                                else
                                    buffer |= PSIGN;
                                WriteP(utmp++, buffer);
                                if (utmp > MEMSIZE)
                                   utmp = 0;
                          }
                        } while ((MBR & SMASK) != MSIGN);
                        if (hst_lnt) {  /* history enabled? */
                             hst[hst_p].ic |= HIST_NOAFT;
                        }
                        break;
                case OP_LL:
                case OP_LE:
                case OP_LEH:
                        /* Generate increment */
                        if (hst_lnt) {  /* history enabled? */
                             hst[hst_p].ic |= HIST_NOAFT;
                        }
                        temp = M[98];
                        utmp = dec_bin_idx(temp);
                        do {
                          uint32 src, limit;
                          MBR = ReadP(MA++);    /* Grab next RDW */
                          get_rdw(MBR, &src, &limit);
                          while(src <= limit) {
                                temp = ReadP(src);
                                temp = (rdmask[f1] & temp) >> ((9 - f2) * 4);
                                switch(dec_cmp(temp, AC[3])) {
                                case -1:
                                        if (opcode == OP_LL) {
                                            f = 1;
                                            AC[3] = temp;
                                            bin_dec(&M[98], src, 4,(emode)?5:4);
                                            M[98] &= DMASK;
                                            M[98] |= PSIGN;
                                        }
                                        break;
                                case 1:
                                        if (opcode != OP_LEH)
                                            break;
                                case 0:
                                        if (opcode != OP_LL) {
                                            f = 1;
                                            bin_dec(&M[98], src, 4,(emode)?5:4);
                                            M[98] &= DMASK;
                                            M[98] |= PSIGN;
                                            goto found;
                                        }
                                        break;
                                }
                                src += utmp;
                          }
                        } while ((MBR & SMASK) != MSIGN);
                found:
                        if (f)
                            IC++;
                        break;
                /* Electronic switch */
                case OP_BSW21:
                case OP_BSW22:
                case OP_BSW23:
                        /* Read flag */
                        opcode -= OP_BSW21;
                        opcode += 101;
                        MBR = ReadP(opcode);
                        if (hst_lnt) {  /* history enabled? */
                            hst[hst_p].before = MBR;
                        }
                        /* Compute mask */
                        f2 = 4 * (9 - f2);
                        temp = 0xFLL << f2;
                        switch(f1) {
                        case 0: /* Switch test */
                        case 3: /* test and turn on */
                        case 4: /* test and turn off */
                                if (MBR & temp)
                                    IC = MA;
                        }
                        if (f1 != 0)
                            MBR &= ~(temp);
                        if (f1 & 1) /* Set flag */
                            MBR |= 0x1LL << f2;
                        if (hst_lnt) {  /* history enabled? */
                            hst[hst_p].after = MBR;
                        }
                        WriteP(opcode, MBR);
                        break;

                /* Priority control */
                case OP_PC:
                      utmp = 0;
                      temp = 0xF;
                      /* Convert digits into bits */
                      for(f1 = 0; f1 < 10; f1++) {
                         if ((MBR & temp) != 0)
                            utmp |= 1;
                         temp <<= 4;
                         utmp <<= 1;
                      }
                      utmp >>= 1;
                      if (f2 == 1) {
                         utmp <<= 10;
                         pri_mask &= 0x3FF;
                         pri_mask |= utmp;
                      } else if (f2 == 0) {
                         pri_mask &= 0xFFC00;
                         pri_mask |= utmp;
                      }
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT;
                      }
                      break;

                /* Test priority latches */
                case OP_PRTST:
                      if (f1 == 0 && f2 == 0) {
                         for(tmp = 0; tmp < 10; tmp++) {
                            if (pri_latchs[tmp] != 0) {
                                IC = MA;
                                break;
                            }
                         }
                      } else {
                         if ((pri_latchs[f1] >> f2) & 1)
                             IC = MA;
                      }
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOBEF|HIST_NOAFT;
                      }
                      break;

                /* Set priority latches */
                case OP_PRION:
                      switch(f1) {
                      case 0:
                      case 8:
                      case 9:
                      case 1:
                      case 2:
                      case 3:
                      case 4:
                             pri_latchs[f1] |= 1 << f2;
                             break;
                      }
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOEA;
                      }
                      break;

                /* Clear priority latches */
                case OP_PRIOF:
                      pri_latchs[f1] &= ~(1 << f2);
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF|HIST_NOEA;
                      }
                      break;

                case OP_PR:
                      if (pri_enb == 1) /* Not in priority mode, nop */
                           break;
                      if ((tmp = scan_irq()) != 0) {
                          if (MA != 97)  {
                             /* Save instruction counter */
                             MBR = ReadP(97);
                             upd_idx(&MBR, MA);
                             MBR &= DMASK;
                             MBR |= PSIGN;
                             WriteP(97, MBR);
                             pri_enb = 0;
                             if (hst_lnt) {  /* history enabled? */
                                 hst[hst_p].after = MBR;
                             }
                          } else if (hst_lnt) {  /* history enabled? */
                              hst[hst_p].ic |= HIST_NOAFT;
                          }
                          inds = PSIGN;
                          IC = tmp;
                      } else {
                          if (MA == 97)
                             IC = dec_bin_idx(MBR);
                          else
                             IC = MA;
                          inds = ReadP(100);
                          pri_enb = 1;
                          if (hst_lnt) {  /* history enabled? */
                              hst[hst_p].ic |= HIST_NOAFT;
                          }
                      }
                      break;
                /* Check switch */
                case OP_BSWITCH:
                      if (f1 == 0 || f1 > 4) {
                           reason =  STOP_UUO;
                           break;
                      }
                      switch(f2) {
                      case 0:
                           if ((SW >> (f1 - 1)) & 1)
                              IC = MA;
                           break;
                      case 1:
                      case 2:
                           if (chan_active(((f2 - 1) * 4) + f1))
                              IC = MA;
                      }
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF;
                      }
                      break;

                /* Inquiry station */
                case OP_INQ:
                      if (hst_lnt) { /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF;
                      }
                      IX = 0;
                      if (f1 != 1) {    /* Only support group one */
                         reason = STOP_IOCHECK;
                         goto done;
                      }
                      switch (f2) {
                      case 0:   utmp = (IO_RDS << 8)|CHN_ALPHA; break;  /* QR */
                      case 1:   utmp = (IO_WRS << 8)|CHN_ALPHA; break;  /* QW */
                      default:
                                reason = STOP_UUO;
                                goto done;
                      }

                      /* Start off command */
                      switch (chan_cmd(4, utmp, MA)) {
                      case SCPE_BUSY:
                           iowait = 1;
                           break;
                      case SCPE_IOERR:
                           reason = STOP_IOCHECK;
                           break;
                      case SCPE_OK:
                           while(chan_active(0)) {
                               sim_interval = 0;
                               reason = sim_process_event();
                               if (reason != SCPE_OK)
                                   break;
                               chan_proc();
                           }
                           break;
                      }
                      break;
                /* Unit record I/O */
                case OP_UREC:
                      if (hst_lnt) { /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF;
                      }
                      switch (f2) {
                      case 0:   utmp = (IO_TRS << 8); break;    /* US */
                      case 1:   utmp = (IO_RDS << 8)|CHN_ALPHA; break;/* UR */
                      case 4:                                   /* TYP */
                      case 2:                                   /* UW */
                      case 3:                                   /* UWIV */
                                utmp = (IO_WRS << 8)|CHN_ALPHA;
                                break;
                      case 9:
                               if (cpu_unit.flags & OPTION_TIMER) {
                                   if (f1 == 0)
                                        timer = 0;
                                   else if (f1 == 1)
                                        WriteP(MA, PSIGN|timer);
                                   goto done;
                                }
                                /* fall through */
                      default:
                                reason = STOP_UUO;
                                goto done;
                      }

                      /* Start off command */
                      switch (chan_cmd(f1, utmp, MA)) {
                      case SCPE_BUSY:
                           iowait = 1;
                           break;
                      case SCPE_IOERR:
                           reason = STOP_IOCHECK;
                           break;
                      case SCPE_OK:
                          while(chan_active(0)) {
                              sim_interval = 0;
                              reason = sim_process_event();
                              if (reason != SCPE_OK)
                                  break;
                              chan_proc();
                          }
                          switch (f2) {
                          case 0:                       /* US */
                                 chan_stat(0, CHS_ERR);
                                 break;
                          case 1:                       /* UR */
                                if (chan_stat(0, CHS_ERR))
                                    break;
                                IC++;
                                if (chan_stat(0, CHS_EOF))
                                    break;
                                IC++;
                                break;
                          case 4:                       /* TYP */
                                if (chan_stat(0, CHS_ERR))
                                    break;
                                IC++;
                                break;
                          case 2:                       /* UW */
                                if (chan_stat(0, CHS_ERR))
                                    break;
                                IC++;
                                if (lpr_chan9[0])
                                    break;
                                IC++;
                                break;
                          case 3:                       /* UWIV */
                                chan_stat(0, CHS_ERR);
                                break;
                          }
                      }
                done:
                      break;
                /* Tape channel channels */
                case OP_TAP1:
                case OP_TAP2:
                case OP_TAP3:
                case OP_TAP4:
                case OP_TAPP1:
                case OP_TAPP2:
                case OP_TAPP3:
                case OP_TAPP4:
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF;
                      }
                     /* If pending IRQ, wait for it to be processed */
                      if ((pri_latchs[opcode & 0xf] >> f1) & 1) {
                            iowait = 1;
                            break;
                      }
                     /* If the channel is busy no need to continue */
                      if (chan_active(opcode & 0xf)) {
                            iowait = 1;
                            break;
                      }
                     /* Set up channel */
                      utmp = ((opcode & 0xf) << 8) + f1;
                      if ((opcode & 0x100) == 0)  /* Set priority signal */
                            utmp |= 0x1000;

                      tmp = 0;
                      switch (f2) {
                      case 0:
                          switch(MA % 10) {
                          case 0: tmp = (IO_TRS << 8);
                                  utmp &= 0xfff;
                                  break;                         /* TSEL */
                          case 1: tmp = (IO_WEF << 8); break; /* TM */
                          case 2: tmp = (IO_REW << 8);
                                  utmp &= 0xfff;
                                  break;                        /* TRW */
                          case 3: tmp = (IO_RUN << 8);
                                  utmp &= 0xfff;
                                  break;                        /* TRU */
                          case 4: tmp = (IO_BSR << 8);
                                  utmp &= 0xfff;
                                  break;                         /* TRB */
                          case 5: tmp = (IO_WRS << 8)|CHN_SEGMENT|CHN_ALPHA;
                                  break;                         /* TSM */
                          case 6: tmp = (IO_ERG << 8);
                                  utmp &= 0xfff;
                                  break;                         /* TSK */
                          case 7: chan_stat(opcode & 0xf, CHS_EOF);
                                  goto done;                     /* TEF */
                          case 8: tmp = (IO_SDL << 8);
                                  utmp &= 0xfff;
                                  break;                        /* TSDL */
                          case 9: tmp = (IO_SDH << 8);
                                  utmp &= 0xfff;
                                  break;                        /* TSDH */
                          }
                          break;
                      case 1:   tmp = (IO_RDS << 8);   break;   /* TR */
                      case 2:   tmp = (IO_RDS << 8)|CHN_RECORD;
                                                      break;    /* TRR */
                      case 3:   tmp = (IO_WRS << 8);   break;   /* TW */
                      case 4:   tmp = (IO_WRS << 8)|CHN_RECORD;
                                                      break;    /* TWR */
                      case 5:   tmp = (IO_WRS << 8)|CHN_COMPRESS;
                                                      break;    /* TWZ */
                      case 6:   tmp = (IO_WRS << 8)|CHN_COMPRESS|CHN_RECORD;
                                                      break;    /* TWC */
                      case 7:   tmp = (IO_RDS << 8)|CHN_SEGMENT|CHN_ALPHA;
                                                      break;    /* TSF */
                      case 8:   tmp = (IO_RDS << 8)|CHN_SEGMENT|CHN_RECORD|CHN_ALPHA;
                                                      break;    /* TSB */
                      case 9:   tmp = (IO_RDS << 8)|CHN_ALPHA;
                                                      break;    /* TRA */
                      }
                      MBR = ((utmp & 0x1000) ? PSIGN:MSIGN) | 0x8000000000LL |
                                (((t_uint64)f2)<<32);
                      upd_idx(&MBR, IC);
                      bin_dec(&MBR, MA, 0, 4);
                      f = (utmp >> 8) & 0xf;
                      WriteP(150 + (f * 10) + (utmp & 0xF), MBR);
                      /* Start off command */
                      switch(chan_cmd(utmp, tmp, MA)) {
                      case SCPE_BUSY:
                           iowait = 1;
                           break;
                      case SCPE_IOERR:
                           reason = STOP_IOCHECK;
                           break;
                      case SCPE_OK:
                          /* Not priority transfer, wait for channel done */
                          if ((utmp & 0x1000) == 0)
                              chwait = f;
                      }
                      break;

                /* Binary tape read channel 1 */
                case OP_TRN:
                case OP_TRNP:
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF;
                      }
                     /* If pending IRQ, wait for it to be processed */
                      if ((pri_latchs[1] >> f1) & 1) {
                            iowait = 1;
                            break;
                      }
                     /* If the channel is busy no need to continue */
                      if (chan_active(1)) {
                            iowait = 1;
                            break;
                      }
                     /* Set up channel */
                      utmp = (1 << 8) + f1 + 020; /* Binary mode */
                      if ((opcode & 0x100) == 0)  /* Set priority signal */
                            utmp |= 0x1000;

                      switch (f2) {
                      case 1:   tmp = (IO_RDS << 8)|CHN_ALPHA; break;
                      default:  /* Anything else is error */
                                reason = STOP_UUO;
                                goto done;
                      }
                      MBR = ((utmp & 0x1000) ? PSIGN:MSIGN) | 0x8000000000LL |
                                (((t_uint64)f2)<<32);
                      upd_idx(&MBR, IC);
                      bin_dec(&MBR, MA, 0, 4);
                      f = (utmp >> 8) & 0xf;
                      WriteP(150 + (f * 10) + (utmp & 0xF), MBR);
                      /* Start off command */
                      switch(chan_cmd(utmp, tmp, MA)) {
                      case SCPE_BUSY:
                           iowait = 1;
                           break;
                      case SCPE_IOERR:
                           reason = STOP_IOCHECK;
                           break;
                      case SCPE_OK:
                          /* Not priority transfer, wait for channel done */
                          if ((utmp & 0x1000) == 0)
                              chwait = f;
                      }
                      break;

                case OP_CHNP1:
                case OP_CHNP2:
                case OP_CHNP3:
                case OP_CHNP4:
                case OP_CHN1:
                case OP_CHN2:
                case OP_CHN3:
                case OP_CHN4:
                     /* If the channel is busy no need to continue */
                      if (chan_active(opcode & 0xf)) {
                            iowait = 1;
                            break;
                      }
                      utmp = ((opcode & 0xf) << 8) + ((f1 & 3) - 1) + 0x200;
                      if ((opcode & 0x100) == 0)  /* Set priority signal */
                            utmp |= 0x1000;
                      /* Build initial status word */
                      MBR = ((t_uint64)opcode & 0xFF) << 32;
                      MBR |= (opcode & 0x100)?MSIGN:PSIGN;
                      upd_idx(&MBR, IC);
                      bin_dec(&MBR, MA, 0, 4);
                      tmp = 0xff;
                      switch (f2) {
                      case 1:   tmp = CHN_COMPRESS;           break; /* DCP */
                      case 2:   tmp = 0;                      break; /* DCUA */
                      case 3:   tmp = CHN_RECORD;             break; /* DCUR */
                      case 4:   tmp = CHN_RECORD|CHN_COMPRESS;break;/*DCPR*/
                      case 6:   tmp = CHN_NUM_MODE;        break; /* DCU */
                      }
                      if (tmp == 0xff)
                         break;
                      tmp |= IO_RDS << 8;       /* Activate channel */
                      /* Start off command */
                      switch(chan_cmd(utmp, tmp, MA)) {
                      case SCPE_BUSY:
                           iowait = 1;
                           break;
                      case SCPE_IOERR:
                           reason = STOP_IOCHECK;
                           break;
                      case SCPE_OK:
                           WriteP(350 + ((utmp >> 8) & 0xf) - 4, MBR);
                           break;
                      }
                      if (hst_lnt) {  /* history enabled? */
                          hst[hst_p].ic |= HIST_NOAFT|HIST_NOBEF;
                      }
                      break;

           /* Diagnostics instructions, not fully implimented */
           /* All of these errors can not occur on the simulation */
             case OP_DIAGT:
                      if (IX == 99) {
                        IX = 63;
                      } else if (IX == 98) {
                        IX = 63;
                        diaglatch |= 1LL << 63;
                        IC = MA;
                        break;
                      } else if (IX > 60) {
                        break;
                      }
                      if (diaglatch & (1LL << IX))
                        IC = MA;
                      diaglatch &= ~(1LL << IX);
                      break;
             case OP_DIAGR:
                      if (IX > 60)
                        break;
                      if (IX == 0)
                        diaglatch &= 1LL << 63;
                      else
                        diaglatch &= ~(1LL << IX);
                      break;
             case OP_DIAGC:
             case OP_DIAGS:
                      break;
             }


        }
        chan_proc();            /* process any pending channel events */
        if (instr_count != 0 && --instr_count == 0)
            return SCPE_STEP;
    }                           /* end while */

/* Simulation halted */

    return reason;
}

/* Decimal arithmetic routines */
/* Add a to b result in a */
int dec_add(t_uint64 *a, t_uint64 b) {
  t_uint64      t1,t2,t3;
  t1 = *a ^ b;
  t2 = *a + b;
  t3 = t2 + 0x6666666666LL;
  t2 = ((t2 < *a) || (t3 < t2));
  t2 = ((t1 ^ t3) >> 3) | (t2 << 37);
  t2 = 0x2222222222LL & ~t2;
  t1 = t3 - (3 * t2);
  if ((t1 & (~DMASK)) != 0) {
        t1 &= DMASK;
        *a = t1;
        return 1;
  }
  *a = t1;
  return 0;
}

/* Decimal arithmetic routines */
/* Add a to b result in a */
/* Don't detect overflow, and use 2 more guard digits */
void dec_add_noov(t_uint64 *a, t_uint64 b) {
  t_uint64      t1,t2,t3;
  t1 = *a ^ b;
  t2 = *a + b;
  t3 = t2 + 0x666666666666LL;
  t2 = ((t2 < *a) || (t3 < t2));
  t2 = ((t1 ^ t3) >> 3) | (t2 << 45);
  t2 = 0x222222222222LL & ~t2;
  t1 = t3 - (3 * t2);
  *a = t1;
}


/* tens compliment a */
void dec_comp(t_uint64 *a) {
  *a = 0x9999999999LL - *a;
  dec_add(a, 1LL);
}

/* Compare to words, includeing sign */
int dec_cmp(t_uint64 a, t_uint64 b) {
  t_uint64      t1,t2,t3;

  a = 0x99999999999LL - a;
  t1 = a ^ b;
  t2 = a + b;
  t3 = t2 + 0x66666666666LL;
  t2 = ((t2 < a) || (t3 < t2));
  t2 = ((t1 ^ t3) >> 3) | (t2 << 41);
  t2 = 0x22222222222LL & ~t2;
  t1 = t3 - (3 * t2);
  if (t1 == 0x99999999999LL) {
        return 0;
  }
  if ((t1 & ~(SMASK|DMASK)) != 0) {
        return 1;
  }
  return -1;
}

/* Do a multiply step */
void mul_step(t_uint64 *a, t_uint64 b, int c) {
  t_uint64      prod;
  int           i;

  for(i = 0; i < 40; i+=4) {
      /* Multiply each digit */
      prod = ((b >> i) & 0xf) * c;
      /* Convert to decimal */
      prod = ((prod / 10) << 4) + (prod % 10);
      if (prod != 0) {
          prod <<= i;   /* Move into place */
          dec_add_noov(a, prod);
      }
  }
}

void div_step(t_uint64 b) {
  t_uint64      t1,t2,t3;

  AC[1] &= DMASK;
  AC[1] <<= 4;
  AC[1] |= (AC[2] >> 36) & 0xf;
  AC[2] <<= 4;
  AC[2] &= DMASK;
/* Repeated subtract until overflow */
  while((AC[2] & 0xF) != 0x9) {
      t1 = AC[1] ^ b;
      t2 = AC[1] + b;
      t3 = t2 + 0x66666666666LL;
      t2 = ((t2 < AC[1]) || (t3 < t2));
      t2 = ((t1 ^ t3) >> 3) | (t2 << 41);
      t2 = 0x22222222222LL & ~t2;
      t1 = t3 - (3 * t2);
      if ((t1 & ~DMASK) == 0)
          return;
      AC[1] = t1 & DMASK;
      AC[2] += 1LL;
   }
}

/* Convert a binary number to BCD */
void bin_dec(t_uint64 *a, uint32 b, int s, int l) {
  s *= 4;
  l *= 4;
  l += s;
  while (s < l) {
      *a &= ~(0xFLL << s);
      *a |= ((t_uint64)(b % 10)) << s;
      b /= 10;
      s += 4;
   }
}

/* Convert index to binary */
uint32 dec_bin_idx(t_uint64 a) {
    uint32      v = (a >> 16) & 0xf;
    v += dscale[0][(a >> 20) & 0xf];
    v += dscale[1][(a >> 24) & 0xf];
    v += dscale[2][(a >> 28) & 0xf];
    if (emode)
        a += dscale[3][(a >> 32) & 0xf];
    return v;
}

uint32 dec_bin_lim(t_uint64 a, uint32 b) {
    uint32      v = a & 0xf;
    v += dscale[0][(a >> 4) & 0xf];
    v += dscale[1][(a >> 8) & 0xf];
    v += dscale[2][(a >> 12) & 0xf];
    if (emode) {
         if (v < b)
             v += dscale[3][((a >> 32) & 0xf)+1];
    }
    return v;
}

/* Extract information from a RDW */
int get_rdw(t_uint64 a, uint32 *base, uint32 *limit) {
    *base = dec_bin_idx(a);
    *limit = dec_bin_lim(a, *base);
    return (a >> 40);
}

void upd_idx(t_uint64 *a, uint32 b) {
    bin_dec(a, b, 4, (emode)?5:4);
}

/* Scan for interupt */
int scan_irq() {
    int irq = 0;
    int tmp;

    for(tmp = 0; tmp < 20 && irq == 0; tmp++) {
        if ((pri_mask & (1 << tmp)) == 0) {
           switch(tmp) {
           case 9:      /* Unit A */
                if (pri_latchs[0] & 0x002) {
                    pri_latchs[0] &= ~0x002;
                    irq = 104;
                }
                break;
           case 8:      /* Unit B */
                if (pri_latchs[0] & 0x004) {
                    pri_latchs[0] &= ~0x004;
                    irq = 105;
                }
                break;
           case 7:      /* Tape Chan 1 */
                if (pri_latchs[1]) {
                    int i;
                    for(i = 0; i < 10; i++) {
                        if (pri_latchs[1] & (1 << i)) {
                            pri_latchs[1] &= ~(1 << i);
                            irq = 150 + ((M[110+i] >> 36) & 0xf);
                            /* Save final record address */
                            upd_idx(&M[99], 110+i);
                            M[99] &= DMASK;
                            M[99] |= PSIGN;
                            break;
                        }
                    }
                }
                break;
           case 6:      /* Tape Chan 2 */
                if (pri_latchs[2]) {
                    int i;
                    for(i = 0; i < 10; i++) {
                        if (pri_latchs[2] & (1 << i)) {
                            pri_latchs[2] &= ~(1 << i);
                            irq = 150 + ((M[120+i] >> 36) & 0xf);
                            /* Save final record address */
                            upd_idx(&M[99], 120+i);
                            M[99] &= DMASK;
                            M[99] |= PSIGN;
                            break;
                        }
                    }
                }
                break;
           case 5:      /* 7300 disk seek */
                break;  /* Not implimented */
           case 4:      /* 7300 disk read and write */
                break;  /* Not implimented */
           case 3:      /* Inquiry group 1 */
                if (pri_latchs[0] & 0x080) {
                    pri_latchs[0] &= ~0x080;
                    irq = 106;
                }
                break;
           case 2:      /* Inquiry group 2 */
                if (pri_latchs[0] & 0x100) {
                    pri_latchs[0] &= ~0x100;
                    irq = 107;
                }
                break;
           case 1:      /* Tape Chan 3 */
                if (pri_latchs[3]) {
                    int i;
                    for(i = 0; i < 10; i++) {
                        if (pri_latchs[3] & (1 << i)) {
                            pri_latchs[3] &= ~(1 << i);
                            irq = 150 + ((M[130+i] >> 36) & 0xf);
                            /* Save final record address */
                            upd_idx(&M[99], 130+i);
                            M[99] &= DMASK;
                            M[99] |= PSIGN;
                            break;
                        }
                    }
                }
                break;
           case 0:      /* Tape Chan 4 */
                if (pri_latchs[4]) {
                    int i;
                    for(i = 0; i < 10; i++) {
                        if (pri_latchs[4] & (1 << i)) {
                            pri_latchs[4] &= ~(1 << i);
                            irq = 150 + ((M[140+i] >> 36) & 0xf);
                            /* Save final record address */
                            upd_idx(&M[99], 140+i);
                            M[99] &= DMASK;
                            M[99] |= PSIGN;
                            break;
                        }
                    }
                }
                break;
           case 10:     /* Channel 1 */
                if (pri_latchs[8] & 0x002) {
                    pri_latchs[8] &= ~0x002;
                    irq = 311;
                    upd_idx(&M[99], 301);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           case 11:     /* Channel 1 Attn */
                if (pri_latchs[9] & 0x002) {
                    pri_latchs[9] &= ~0x002;
                    irq = 321;
                    upd_idx(&M[99], 301);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           case 12:     /* Channel 2 */
                if (pri_latchs[8] & 0x004) {
                    pri_latchs[8] &= ~0x004;
                    irq = 312;
                    upd_idx(&M[99], 302);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           case 13:     /* Channel 2 Attn */
                if (pri_latchs[9] & 0x004) {
                    pri_latchs[9] &= ~0x004;
                    irq = 322;
                    upd_idx(&M[99], 302);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           case 14:     /* Channel 3 */
                if (pri_latchs[8] & 0x008) {
                    pri_latchs[8] &= ~0x008;
                    irq = 313;
                    upd_idx(&M[99], 303);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           case 15:     /* Channel 3 Attn */
                if (pri_latchs[9] & 0x008) {
                    pri_latchs[9] &= ~0x008;
                    irq = 323;
                    upd_idx(&M[99], 303);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           case 16:     /* Channel 4 */
                if (pri_latchs[8] & 0x010) {
                    pri_latchs[8] &= ~0x010;
                    irq = 314;
                    upd_idx(&M[99], 304);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           case 17:     /* Channel 4 Attn */
                if (pri_latchs[9] & 0x010) {
                    pri_latchs[9] &= ~0x010;
                    irq = 324;
                    upd_idx(&M[99], 304);
                    M[99] &= DMASK;
                    M[99] |= PSIGN;
                }
                break;
           }
        }
    }
    return irq;
}


/* Initialize memory to all plus zero */
void
mem_init() {
    int                 i;
    /* Force memory to have all valid signs */
    for(i = 0; i < MAXMEMSIZE; i++)
        M[i] = PSIGN;
}


/* Reset routine */
t_stat
cpu_reset(DEVICE * dptr)
{
    static int  initialized = 0;

    if (initialized == 0) {
        initialized = 1;
        mem_init();
    }

    AC[1] = PSIGN;
    AC[2] = PSIGN;
    AC[3] = PSIGN;
    inds = PSIGN;
    pri_enb = 1;
    sim_brk_types = sim_brk_dflt = SWMASK('E');
    if (cpu_unit.flags & OPTION_TIMER) {
        sim_rtcn_init_unit (&cpu_unit, cpu_unit.wait, TMR_RTC);
        sim_activate(&cpu_unit, cpu_unit.wait);
    }
    return SCPE_OK;
}

/* Interval timer routines */
t_stat
rtc_srv(UNIT * uptr)
{
    if (cpu_unit.flags & OPTION_TIMER) {
        timer_clock++;
        if (timer_clock == 300) {
            t_uint64    t = (t_uint64) timer;
            dec_add(&t, 1);
            timer = t & 0xfff;
            timer_clock = 0;
        }
        sim_activate(&cpu_unit, sim_rtcn_calb(uptr->wait, TMR_RTC));
    }
    return SCPE_OK;
}

t_stat
rtc_reset(DEVICE * dptr)
{
    if (cpu_unit.flags & OPTION_TIMER) {
        sim_activate(&cpu_unit, cpu_unit.wait);
    }
    return SCPE_OK;
}

/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr > MEMSIZE)
        return SCPE_NXM;
    if (vptr != NULL)
        *vptr = M[addr];

    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr > MEMSIZE)
        return SCPE_NXM;
    M[addr] = val;
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_uint64            mc = 0;
    uint32              i;
    int32               v;

    v = val >> UNIT_V_MSIZE;
    v = (v + 1) * 5000;
    if ((v <= 0) || (v > MAXMEMSIZE))
        return SCPE_ARG;
    for (i = v-1; i < MEMSIZE; i++) {
        if (M[i] != PSIGN) {
           mc = 1;
           break;
        }
    }
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    cpu_unit.flags &= ~UNIT_MSIZE;
    cpu_unit.flags |= val;
    MEMSIZE = v;
    for (i = MEMSIZE; i < MAXMEMSIZE; i++)
        M[i] = PSIGN;
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
    t_stat              r;
    t_value             sim_eval;
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
    fprintf(st, "IC    EA    BEFORE      AFTER       INST\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->ic & HIST_PC) {  /* instruction? */
            fprintf(st, "%05d ", h->ic & 0xffff);
            if (h->ic & HIST_NOEA)
                fputs("       ", st);
            else
                fprintf(st, " %05d ", h->ea);
            if (h->ic & HIST_NOBEF)
                fputs("           ", st);
            else {
                switch (h->before & SMASK) {
                case PSIGN: fputc('+', st); break;
                case MSIGN: fputc('-', st); break;
                case ASIGN: fputc('@', st); break;
                default: fputc('#', st); break;
                }
                fprint_val(st, h->before & DMASK, 16, 40, PV_RZRO);
            }
            fputc(' ', st);
            if (h->ic & HIST_NOAFT)
                fputs("           ", st);
            else {
                switch (h->after & SMASK) {
                case PSIGN: fputc('+', st); break;
                case MSIGN: fputc('-', st); break;
                case ASIGN: fputc('@', st); break;
                default: fputc('#', st); break;
                }
                fprint_val(st, h->after & DMASK, 16, 40, PV_RZRO);
            }
            fputc(' ', st);
            sim_eval = h->op;
            if (
                (fprint_sym(st, h->ic & AMASK, &sim_eval, &cpu_unit,
                  SWMASK('M'))) > 0) fputs("(undefined)", st);
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}

t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr) {
    fprintf (st, "The CPU can be set to a IBM 7070 or IBM 7074\n");
    fprintf (st, "The type of CPU can be set by one of the following commands\n\n");
    fprintf (st, "   sim> set CPU 7070        sets IBM 7070 emulation\n");
    fprintf (st, "   sim> set CPU 7074        sets IBM 7074 emulation\n\n");
    fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\n\n");
    fprintf (st, "      -c      examine/deposit characters, 5 per word\n");
    fprintf (st, "      -m      examine/deposit IBM 7070 instructions\n\n");
    fprintf (st, "The memory of the CPU can be set in 5K incrememts from 5K to 30K with the\n\n");
    fprintf (st, "   sim> SET CPU xK\n\n");
    fprintf (st, "For the IBM 7070 the following options can be enabled\n\n");
    fprintf (st, "   sim> SET CPU FLOAT     enables Floating Point\n");
    fprintf (st, "   sim> SET CPU NOFLOAT   disables Floating Point\n\n");
    fprintf (st, "   sim> SET CPU EXTEND      enables extended memory\n");
    fprintf (st, "   sim> SET CPU NOEXTEND    disables extended memory\n\n");
    fprintf (st, "   sim> SET CPU CLOCK      enables timer clock\n");
    fprintf (st, "   sim> SET CPU NOCLOCK    disables timer clock\n\n");
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

const char *
cpu_description (DEVICE *dptr) {
    return "IBM 7070 CPU";
}

