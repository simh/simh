/* i701_cpu.c: IBM 701 CPU simulator

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

   cpu          701 central processor

   The IBM 701 also know as "Defense Calculator" was introduced by IBM
   on April 7, 1953. This computer was start of IBM 700 and 7000 line.
   Memory was 2048 36 bit words. Each instruction could be signed plus
   or minus, plus would access memory as 18 bit words, minus as 36 bit
   words. There was a expansion option to add another 2048 words of
   memory, but I can't find documentation on how it worked. Memory cycle
   time was 12 microseconds. The 701 was withdrawn from the market
   October 1, 1954 replaced by 704 and 702. A total of 19 machines were
   installed.

   The system state for the IBM 701 is:

   AC<S,P,Q,1:35>       AC register
   MQ<S,1:35>           MQ register
   IC<0:15>             program counter
   SSW<0:5>             sense switches
   SLT<0:3>             sense lights
   ACOVF                AC overflow
   DVC                  divide check
   IOC                  I/O check

   The 701 had one instruction format: memory reference,

       00000 000011111111
     S 12345 678901234567
    +-+-----+------------+
    | |opcod|  address   | memory reference
    +-+-----+------------+

   This routine is the instruction decode routine for the 701.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until a stop condition occurs.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        illegal instruction
        illegal I/O operation for device
        illegal I/O operation for channel
        breakpoint encountered
        nested XEC's exceeding limit
        divide check
        I/O error in I/O simulator

   3. Arithmetic.  The 701 uses signed magnitude arithmetic for
      integer and floating point calculations, and 2's complement
      arithmetic for indexing calculations.

   4. Adding I/O devices.  These modules must be modified:

        i7090_defs.h    add device definitions
        i7090_chan.c    add channel subsystem
        i701_sys.c      add sim_devices table entry
*/

#include "i7090_defs.h"

#define HIST_XCT        1       /* instruction */
#define HIST_INT        2       /* interrupt cycle */
#define HIST_TRP        3       /* trap cycle */
#define HIST_MIN        64
#define HIST_MAX        65536
#define HIST_NOEA       0x40000000
#define HIST_PC         0x10000

struct InstHistory
{
    t_int64             ac;
    t_int64             mq;
    t_int64             op;
    t_int64             sr;
    uint32              ic;
    uint16              ea;
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


t_uint64            M[MAXMEMSIZE] = { 0 };      /* memory */
t_uint64            AC, MQ;                     /* registers */
uint16              IC;                         /* program counter */
uint8               SL;                         /* Sense lights */
uint8               SW = 0;                     /* Sense switch */
uint8               dcheck;                     /* Divide check */
uint8               acoflag;                    /* AC Overflow */
uint8               ihold = 0;                  /* Hold interrupts */
uint16              iotraps;                    /* IO trap flags */
t_uint64            ioflags;                    /* Trap enable flags */
uint8               iocheck;
uint8               iowait;                     /* Waiting on io */
uint8               dualcore;                   /* Set to true if dual core in
                                                         use */
uint16              dev_pulse[NUM_CHAN];        /* SPRA device pulses */
int                 cycle_time = 120;           /* Cycle time of 12us */

/* History information */
int32               hst_p = 0;                  /* History pointer */
int32               hst_lnt = 0;                /* History length */
struct InstHistory *hst = NULL;                 /* History stack */
extern uint32       drum_addr;
uint32              hsdrm_addr;
extern UNIT         chan_unit[];

#undef  AMASK           /* Change definition of AMASK here */
#define AMASK           00000000007777L

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
    { UDATA(NULL, UNIT_BINK, MAXMEMSIZE / 2) };

REG                 cpu_reg[] = {
    {ORDATAD(IC, IC, 15, "Instruction counter"), REG_FIT},
    {ORDATAD(AC, AC, 38, "Accumulator"), REG_FIT},
    {ORDATAD(MQ, MQ, 36, "Multiplier quotent"), REG_FIT},
    {ORDATAD(SL, SL, 4, "Lights"), REG_FIT},
    {ORDATAD(SW, SW, 6, "Switch register"), REG_FIT},
    {FLDATAD(SW1, SW, 0, "Switch 0"), REG_FIT},
    {FLDATAD(SW2, SW, 1, "Switch 1"), REG_FIT},
    {FLDATAD(SW3, SW, 2, "Switch 2"), REG_FIT},
    {FLDATAD(SW4, SW, 3, "Switch 3"), REG_FIT},
    {FLDATAD(SW5, SW, 4, "Switch 4"), REG_FIT},
    {FLDATAD(SW6, SW, 5, "Switch 5"), REG_FIT},
    {ORDATAD(ACOVF, acoflag, 1, "Overflow flag"), REG_FIT},
    {ORDATAD(IOC, iocheck, 1, "I/O Check flag"), REG_FIT},
    {ORDATAD(DVC, dcheck, 1, "Divide Check"), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 15, 1, 8, 36,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, 0, 0, NULL,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
};

/* Simulate instructions */
t_stat
sim_instr(void)
{
    t_stat          reason;
    t_uint64        temp = 0;
    t_uint64        ibr;
    t_uint64        SR;
    uint16          opcode;
    uint16          MA;
    uint8           f;
    int             shiftcnt;
    int             stopnext = 0;
    int             instr_count = 0;   /* Number of instructions to execute */

    if (sim_step != 0) {
        instr_count = sim_step;
        sim_cancel_step();
    }

    reason = 0;

    iowait = 0;
    while (reason == 0) {       /* loop until halted */

/* If doing fast I/O don't sit in idle loop */
        if (iowait == 0 && stopnext)
            return SCPE_STEP;

        if (sim_interval <= 0) {        /* event queue? */
            reason = sim_process_event();
            if (reason != SCPE_OK) {
                if (reason == SCPE_STEP && iowait)
                    stopnext = 1;
                else
                    break;      /* process */
            }
            sim_interval--;
        }

        if (iowait == 0 && sim_brk_summ && sim_brk_test(IC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

/* Split out current instruction */
        if (iowait) {
            /* If we are awaiting I/O complete, don't fetch. */
            sim_interval -= 6;
            iowait = 0;
        } else {
            MA = IC >> 1;
            sim_interval -= 24;         /* count down */
            SR = ReadP(MA);
            temp = SR;
            if ((IC & 1) == 0)
                temp >>= 18;
            if (hst_lnt) {      /* history enabled? */
                hst_p = (hst_p + 1);    /* next entry */
                if (hst_p >= hst_lnt)
                    hst_p = 0;
                hst[hst_p].ic = IC | HIST_PC;
                hst[hst_p].ea = 0;
                hst[hst_p].op = temp & RMASK;
                hst[hst_p].ac = AC;
                hst[hst_p].mq = MQ;
                hst[hst_p].sr = 0;
            }
            IC = (IC + 1) & AMASK;
        }
        ihold = 0;
        opcode = ((uint16)(temp >> 12L)) & 077;
        MA = (uint16)(temp & AMASK);
        ibr = SR = ReadP(MA>>1);
        if ((opcode & 040) == 0) {
           if (MA & 1)
                SR <<= 18;
           SR &= LMASK;
        }
        if (hst_lnt) {  /* history enabled? */
            hst[hst_p].sr = SR;
            hst[hst_p].ea = MA;
        }

        switch (opcode & 037) {
            case 19:    /* RND */
                if (MQ & ONEBIT)
                    AC++;
                break;
            case 30:            /* SENSE */
                switch (MA) {
                case 64:        /* SLN */
                    SL = 0;
                    break;
                case 65:        /* */
                    SL |= 1;
                    break;
                case 66:
                    SL |= 2;
                    break;
                case 67:
                    SL |= 4;
                    break;
                case 68:
                    SL |= 8;
                    break;
                case 69:
                    if ((SW & 1) == 0)
                        IC++;
                    break;
                case 70:
                    if ((SW & 2) == 0)
                        IC++;
                    break;
                case 71:
                    if ((SW & 4) == 0)
                        IC++;
                    break;
                case 72:
                    if ((SW & 8) == 0)
                        IC++;
                    break;
                case 73:
                    if ((SW & 16) == 0)
                        IC++;
                    break;
                case 74:
                    if ((SW & 32) == 0)
                        IC++;
                    break;
                case 1024:
                case 1025:
                    MA -= 1024;
                    dev_pulse[0] |= 1 << MA;
                    break;
                case 522:
                    if (dev_pulse[0] & PRINT_I)
                       IC++;
                    dev_pulse[0] &= ~PRINT_I;
                    break;
                case 512:
                case 513:
                case 514:
                case 515:
                case 516:
                case 517:
                case 518:
                case 519:
                case 520:
                case 521:
                    MA = (MA - 512) + 5;
                    dev_pulse[0] |= 1 << MA;
                    break;
                }
                break;
            case 0:     /* STOP */
                /* Stop at HTR instruction if trapped */
                IC--;
              halt:
                reason = STOP_HALT;
                /* Clear off any pending events before we halt */
                do {

                    f = chan_active(0);
                    chan_proc();
                    for (shiftcnt = 1; shiftcnt < NUM_CHAN; shiftcnt++) {
                        f |= chan_active(shiftcnt);
                    }
                    sim_interval = 0;
                    (void)sim_process_event();
                } while (f);
                if (reason != 0)
                     IC = MA;
                break;
            case 8:     /* NO OP */
                break;
            case 1:     /* TR */
                IC = MA;
                break;
            case 4:     /* TR 0 */
                f = (AC & AMMASK) == 0;
              branch:
                if (f) {
                    IC = MA;
                }
                break;
            case 2:     /* TR OV */
                f = acoflag;
                acoflag = 0;
                goto branch;
            case 3:     /* TR + */
                f = ((AC & AMSIGN) == 0);
                goto branch;
            case 10:    /* R ADD */
                AC = ((SR & MSIGN) << 2) | (SR & PMASK);
                sim_interval -= 6;
                break;
            case 6:     /* R SUB */
                AC = (((SR & MSIGN) ^ MSIGN) << 2) | (SR & PMASK);
                sim_interval -= 6;
                break;
            case 15:    /* LOAD MQ */
                MQ = SR;
                sim_interval -= 6;
                break;
            case 14:    /* STORE MQ */
                SR = MQ;
                goto store;
            case 12:            /* STORE */
                SR = AC & PMASK;
                if (AC & AMSIGN)
                    SR |= MSIGN;
store:
                if ((opcode & 040) == 0) {
                    SR &= LMASK;
                    if (MA & 1) {
                        ibr &= LMASK;
                        SR >>= 18;
                    } else {
                        ibr &= RMASK;
                    }
                    SR |= ibr;
                }
                WriteP(MA>>1, SR);
                if (hst_lnt) {  /* history enabled? */
                    hst[hst_p].sr = SR;
                }
                sim_interval -= 6;
                break;
/* Logic operations */
            case 13:    /* EXTR or STORE A */
                if ((opcode & 040) == 0) {
                    SR &= ~(AMASK << 18);
                    SR |= AC & (AMASK << 18);
                } else {
                    t_uint64   t = AC & PMASK;
                    if (AC & AMSIGN)
                        t |= MSIGN;
                    SR &= t;
                }
                goto store;
            case 7:     /* SUB AB */
                SR |= MSIGN;
                goto iadd;
            case 11:    /* ADD AB */
                SR &= PMASK;
                goto iadd;
            case 5:     /* SUB */
                SR ^= MSIGN;
                /* Fall through */
            case 9:     /* ADD */
              iadd:
                f = 0;
                /* Make AC Positive */
                if (AC & AMSIGN) {
                    f = 2;
                    AC &= AMMASK;
                }
                if (AC & APSIGN)
                    f |= 8;
                /* Check signes of SR & AC */
                if (((SR & MSIGN) && ((f & 2) == 0)) ||
                    (((SR & MSIGN) == 0) && ((f & 2) != 0))) {
                    AC ^= AMMASK;       /* One's compliment */
                    f |= 1;
                }
                AC = AC + (SR & PMASK);
                /* Check carry from Q */
                if (f & 1) {    /* Check if signs were not same */
                    if (AC & AMSIGN) {
                        f ^= 2;
                        AC++;
                        if (((AC & APSIGN) != 0) != ((f & 8) != 0))
                            acoflag = 1;
                    } else {
                        AC ^= AMMASK;   /* One's compliment */
                    }
                } else {
                    if (((AC & APSIGN) != 0) != ((f & 8) != 0))
                        acoflag = 1;
                }
                /* Restore sign to AC */
                AC &= AMMASK;
                if (f & 2)
                    AC |= AMSIGN;
                sim_interval -= 6;
                break;
            case 16:            /* MPY */
            case 17:            /* MPY R */
                shiftcnt = 043;
                sim_interval -= 34 * 6;
                f = 0;
                /* Save sign */
                if (MQ & MSIGN)
                    f |= 1;
                if (SR & MSIGN)
                    f |= 2;
                SR &= PMASK;
                MQ &= PMASK;
                AC = 0;         /* Clear AC */
                if (SR == 0) {
                    MQ = 0;
                } else {
                    while (shiftcnt-- > 0) {
                        if (MQ & 1)
                            AC += SR;
                        MQ >>= 1;
                        if (AC & 1)
                            MQ |= ONEBIT;
                        AC >>= 1;
                    }
                }
                if ((opcode & 037) == 17 && MQ & ONEBIT)
                    AC++;
                if (f & 2)
                    f ^= 1;
                if (f & 1) {
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }
                break;
            case 18:            /* DIV */
                shiftcnt = 043;
                sim_interval -= 34 * 6;
                /* Save sign */
                if (SR & MSIGN) {
                    SR &= PMASK;
                    f = 1;
                } else
                    f = 0;

                if (AC & AMSIGN)
                    f |= 2;

                /* Check if SR less then AC */
                if (((SR - (AC & AMMASK)) & AMSIGN) ||
                    (SR == (AC & AMMASK))) {
                    dcheck = 1;
                    MQ &= PMASK;
                    if (f == 1 || f == 2)
                        MQ |= MSIGN;
                    goto halt;
                }
                /* Clear signs */
                MQ &= PMASK;
                AC &= AMMASK;
                /* Do divide operation */
                do {
                    AC <<= 1;
                    AC &= AMMASK;
                    MQ <<= 1;
                    if (MQ & MSIGN) {
                        MQ ^= MSIGN;
                        AC |= 1;
                    }
                    if (SR <= AC) {
                        AC -= SR;
                        MQ |= 1;
                    }
                } while (--shiftcnt != 0);
                switch (f) {
                case 0:
                    break;
                case 3:
                    AC |= AMSIGN;
                    break;
                case 2:
                    AC |= AMSIGN;
                    /* FALL THRU */
                case 1:
                    MQ |= MSIGN;
                    break;
                }
                break;

/* Shift */
            case 20:            /* L LEFT */
                shiftcnt = MA & 0377;
                sim_interval -= (shiftcnt >> 6) * 6;
                /* Save sign */
                if (MQ & MSIGN)
                    f = 1;
                else
                    f = 0;
                /* Clear it for now */
                AC &= AQMASK;
                while (shiftcnt-- > 0) {
                    MQ <<= 1;
                    AC <<= 1;
                    if (MQ & MSIGN)
                        AC |= 1;
                    if (AC & APSIGN)
                        acoflag = 1;
                }
                /* Restore sign when done */
                AC &= AMMASK;
                MQ &= PMASK;
                if (f) {
                    AC |= AMSIGN;
                    MQ |= MSIGN;
                }
                break;
            case 21:            /* L RIGHT */
                shiftcnt = MA & 0377;
                sim_interval -= (shiftcnt >> 6) * 6;
                /* Save sign */
                if (AC & AMSIGN)
                    f = 1;
                else
                    f = 0;
                /* Clear it for now */
                AC &= AMMASK;
                MQ &= PMASK;
                while (shiftcnt-- > 0) {
                    if (AC & 1)
                        MQ |= MSIGN;;
                    MQ >>= 1;
                    AC >>= 1;
                }
                /* Restore sign when done */
                AC &= AMMASK;
                if (f) {
                    AC |= AMSIGN;
                    MQ |= MSIGN;
                }
                break;
            case 22:            /* A LEFT */
                shiftcnt = MA & 0377;
                sim_interval -= (shiftcnt >> 6) * 6;
                /* Save sign */
                if (AC & AMSIGN)
                    f = 1;
                else
                    f = 0;
                /* Clear it for now */
                AC &= AQMASK;
                while (shiftcnt-- > 0) {
                    AC <<= 1;
                    if (AC & APSIGN)
                        acoflag = 1;
                }
                /* Restore sign and overflow when done */
                AC &= AMMASK;
                if (f)
                    AC |= AMSIGN;
                break;
            case 23:            /* A RIGHT */
                shiftcnt = MA & 0377;
                sim_interval -= (shiftcnt >> 6) * 6;
                /* Save sign */
                if (AC & AMSIGN)
                    f = 1;
                else
                    f = 0;
                /* Clear it for now */
                AC &= AMMASK;
                AC >>= shiftcnt;
                /* Restore sign when done */
                if (f)
                    AC |= AMSIGN;
                break;

/* 704 Input output Instructions */
            case 29:            /* SET DR */
                if (chan_test(0, DEV_SEL)) {
                    drum_addr = (uint32)MA;
                    chan_clear(0, DEV_FULL);    /* Incase something got
                                                  read while waiting */
                } else
                    iocheck = 1;
                break;
            case 31:            /* COPY */
                /* If no channel, set Iocheck and treat as nop */
                if (chan_unit[0].flags & UNIT_DIS) {
                    iocheck = 1;
                    break;
                }

                /* If device disconnecting, just wait */
                if (chan_test(0, DEV_DISCO)) {
                    iowait = 1;
                    break;
                }

                /* Instruct is NOP first time */
                /* Incomplete last word leaves result in MQ */
                if (chan_select(0)) {
                    extern uint8 bcnt[NUM_CHAN];
                    chan_set(0, STA_ACTIVE);
                    switch (chan_flags[0] & (DEV_WRITE | DEV_FULL)) {
                    case DEV_WRITE | DEV_FULL:
                    case 0:
                        /* On EOR skip 1, on EOF skip two */
                        if (chan_test(0, CHS_EOF|CHS_EOT|DEV_REOR))
                            chan_set(0, DEV_DISCO);
                        iowait = 1;
                        break;
                    case DEV_WRITE:
                        MQ = assembly[0] = SR;
                        bcnt[0] = 6;
                        chan_set(0, DEV_FULL);
                        break;
                    case DEV_FULL:
                        SR = MQ;
                        WriteP(MA, MQ);
                        bcnt[0] = 6;
                        chan_clear(0, DEV_FULL);
                        break;
                    }
                } else {
                    if (chan_stat(0, CHS_EOF|CHS_EOT)) {
                        IC++;
                    /* On EOR skip two */
                    } else if (chan_stat(0, DEV_REOR)) {
                        IC += 2;
                    /* Advance 1 on Error and set iocheck */
                    } else if (chan_stat(0, CHS_ERR)) {
                        iocheck = 1;
                        IC++;
                    }
                    chan_clear(0, STA_ACTIVE|DEV_REOR|CHS_ERR);
                    break;
                }
                break;

/* Input/Output Instuctions */
            case 24:            /* Read select */
                    opcode = IO_RDS;
                    MQ = 0;
                    goto iostart;
            case 26:            /* Write select */
                    opcode = IO_WRS;
                    goto iostart;
            case 27:            /* Write EOF */
                    opcode = IO_WEF;
                    goto iostart;
            case 25:            /* Read Backwards */
                    opcode = IO_RDB;
                    MQ = 0;
                    goto iostart;
            case 28:            /* Rewind */
                    opcode = IO_REW;
        iostart:
                switch (chan_cmd(MA, opcode)) {
                case SCPE_BUSY:
                    iowait = 1; /* Channel is active, hold */
                case SCPE_OK:
                    ihold = 1;  /* Hold interupts for one */
                    break;
                case SCPE_IOERR:
                    iocheck = 1;
                    break;
                case SCPE_NODEV:
                    reason = STOP_IOCHECK;
                    break;
                }
                break;

            default:
                reason = STOP_UUO;
                break;
            }

         chan_proc();   /* process any pending channel events */
         if (instr_count != 0 && --instr_count == 0)
             return SCPE_STEP;
    }                           /* end while */

/* Simulation halted */

    return reason;
}


/* Reset routine */

t_stat
cpu_reset(DEVICE * dptr)
{
    extern void sys_init(void);

    sys_init();
    AC = 0;
    MQ = 0;
    dualcore = 0;
    iotraps = 0;
    ioflags = 0;
    dcheck = acoflag = iocheck = 0;
    sim_brk_types = sim_brk_dflt = SWMASK('E');
    return SCPE_OK;
}

/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= (MEMSIZE * 2))
        return SCPE_NXM;
    if (vptr == NULL)
        return SCPE_OK;

    *vptr = M[(addr & 07777) >> 1];
    if ((addr & 0400000) == 0) {
        if ( addr & 1)
           *vptr <<= 18;
        else
           *vptr &= LMASK;
    }
    *vptr &= 0777777777777L;

    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    t_addr      a = (addr >> 1) & 03777;

    if (addr >= (MEMSIZE * 2))
        return SCPE_NXM;
    if ((addr & 0400000) == 0) {
        if (addr & 1) {
           M[a] &= LMASK;
           M[a] |= (val >> 18) & RMASK;
        } else {
           M[a] &= RMASK;
           M[a] |= val & LMASK;
        }
    } else
        M[a] = val & 0777777777777L;
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
    fprintf(st, "IC      AC            MQ            EA      SR\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->ic & HIST_PC) {  /* instruction? */
            fprintf(st, "%06lo ", h->ic & AMASK);
            switch (h->ac & (AMSIGN | AQSIGN | APSIGN)) {
            case AMSIGN | AQSIGN | APSIGN:
                fprintf(st, "-QP");
                break;
            case AMSIGN | AQSIGN:
                fprintf(st, " -Q");
                break;
            case AMSIGN | APSIGN:
                fprintf(st, " -P");
                break;
            case AMSIGN:
                fprintf(st, "  -");
                break;
            case AQSIGN | APSIGN:
                fprintf(st, " QP");
                break;
            case AQSIGN:
                fprintf(st, "  Q");
                break;
            case APSIGN:
                fprintf(st, "  P");
                break;
            case 0:
                fprintf(st, "   ");
                break;
            }
            fprint_val(st, h->ac & PMASK, 8, 35, PV_RZRO);
            fputc(' ', st);
            if (h->mq & MSIGN)
                fputc('-', st);
            else
                fputc(' ', st);
            fprint_val(st, h->mq & PMASK, 8, 35, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->ea, 8, 12, PV_RZRO);
            fputc(' ', st);
            if (h->sr & MSIGN)
                fputc('-', st);
            else
                fputc(' ', st);
            fprint_val(st, h->sr & PMASK, 8, 35, PV_RZRO);
            fputc(' ', st);
            sim_eval = h->op;
            if (
                (fprint_sym
                 (st, h->ic & AMASK, &sim_eval, &cpu_unit,
                  SWMASK('M'))) > 0) fprintf(st, "(undefined) %012llo",
                                             h->op);
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr)
{
       return "IBM 701 CPU";
}

t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf (st, "The CPU behaves as a IBM 701\n");
    fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\n\n");
    fprintf (st, "      -c      examine/deposit characters, 6 per word\n");
    fprintf (st, "      -l      examine/deposit half words\n");
    fprintf (st, "      -m      examine/deposit IBM 701 instructions\n\n");
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

