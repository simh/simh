/* i7090_cpu.c: IBM 7090 CPU simulator

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

   cpu          7094 central processor
   rtc          real time clock

   The IBM 7090 was first introduced as the IBM 704. This led to the 709,
   7090, 7094, 7040 and 7044. All where 36 bit signed magnitude machines.
   They were single address machines and had 3 or more index registers.
   These were the first machines to include indexing. Also first machines
   to impliment indirect addressing, built in floating point, first Fortran
   compiler, first TimeShareing Sytem CTSS.

     IBM 704:   Announced May 7, 1954 withdrawn April 7, 1960.
                First floating point.
                First index addressing.
                Memory to 32k words.

     IBM 709:   Announced January 2, 1957 withdrawn April 7, 1960.
                First indirect addressing.
                First I/O channel.
                Added indicator register.

     IBM 7090:  Announced Decemeber 30, 1958 withdrawn July 14, 1969.
                Transister version of 709.

     IBM 7094 & IBM 7094/II: Announced January 15, 1962 withdrawn July 14, 1969.
                Added double precision floating point.
                Added up to 7 index registers.

     IBM 7040   Announced April 1961.
                Transisterize 704, with channel.
                Character operate instructions.
                Currently not implmented in simulator.

     IBM 7044   Announced 1961
                Enhanced verion of 7040.
                Currently not implmented in simulator.

   The system state for the IBM 7090 is:

   AC<S,P,Q,1:35>       AC register
   MQ<S,1:35>           MQ register
   XR<0:15>[8]          XR index registers register
   IC<0:15>             program counter
   SSW<0:5>             sense switches
   SLT<0:3>             sense lights
   ID<S:36>             indicators lights
   ACOVF                AC overflow
   MQOVF                MQ overflow
   DVC                  divide check
   IOC                  I/O check
   TM                   transfer trap mode
   CTM                  copy trap mode (for 709 compatibility)
   FTM                  floating trap mode (off is 704 compatibility)
   STM                  select trap mode
   NMODE                storage nullifcation mode
   MTM                  multi-tag mode (7090 compatibility)

   CTSS required a set of special features: memory extension (to 65K),
   protection, and relocation.  Additional state:

   INST_BASE            instruction memory select (A vs B core)
   DATA_BASE            data memory select (A vs B core)
   BASE<0:6>            start address block
   LIMIT<0:6>           limit address block

   The 7094 had five instruction formats: memory reference,
   memory reference with count, convert, decrement, and immediate.

      00000000011 11 1111 112 222222222333333
     S12345678901 23 4567 890 123456789012345
    +------------+--+----+---+---------------+
    |   opcode   |ND|0000|tag|     address   | memory reference
    +------------+--+----+---+---------------+

      00000000011 111111 112 222222222333333
     S12345678901 234567 890 123456789012345
    +------------+------+---+---------------+
    |   opcode   | count|tag|     address   | memory reference
    +------------+------+---+---------------+ with count

      000000000 11111111 11 2 222222222333333
     S123456789 01234567 89 0 123456789012345
    +----------+--------+--+-+---------------+
    |  opcode  | count  |00|X|    address    | convert
    +----------+--------+--+-+---------------+

      00 000000011111111 112 222222222333333
     S12 345678901234567 890 123456789012345
    +---+---------------+---+---------------+
    |opc|   decrement   |tag|     address   | decrement
    +---+---------------+---+---------------+

      00000000011 111111 112222222222333333
     S12345678901 234567 890123456789012345
    +------------+------+------------------+
    |   opcode   |000000|   immediate      | immediate
    +------------+------+------------------+

   This routine is the instruction decode routine for the 7094.
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

   2. Data channel traps.  The 7094 is a channel-based system.
      Channels can generate traps for errors and status conditions.
      Channel trap state:

        iotraps[0..7]   flags for channels A..H
        ioflags         channel trap enables
        itrap           channel trap inhibit due to trap (cleared by RCT)
        ihold           channel trap inhibit due to XEC, ENAB, RCT, RDS,
                        or WDS (cleared after one instruction)

   3. Arithmetic.  The 7094 uses signed magnitude arithmetic for
      integer and floating point calculations, and 2's complement
      arithmetic for indexing calculations.

   4. Adding I/O devices.  These modules must be modified:

        i7094_defs.h    add device definitions
        i7094_chan.c    add channel subsystem
        i7094_sys.c     add sim_devices table entry
*/

#include "i7090_defs.h"
#include "sim_timer.h"
#include <math.h>
#ifdef CPANEL
#include "cpanel.h"
#endif

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (7 << UNIT_V_MSIZE)
#define UNIT_V_CPUMODEL (UNIT_V_UF + 4)
#define UNIT_MODEL      (0x3 << UNIT_V_CPUMODEL)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_CPUMODEL) & 0x3)
#define MODEL(x)        (x << UNIT_V_CPUMODEL)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define UNIT_DUALCORE   (1 << (UNIT_V_CPUMODEL + 2))
#define UNIT_FASTIO     (1 << (UNIT_V_CPUMODEL + 3))
#define OPTION_EFP      (1 << (UNIT_V_CPUMODEL + 4))
#define OPTION_TIMER    (1 << (UNIT_V_CPUMODEL + 5))
#define OPTION_FPSM     (1 << (UNIT_V_UF_31))

#define CPU_704         0
#define CPU_709         1
#define CPU_7090        2
#define CPU_7094        3

#define TMR_RTC         0

#define HIST_XCT        1       /* instruction */
#define HIST_INT        2       /* interrupt cycle */
#define HIST_TRP        3       /* trap cycle */
#define HIST_MIN        64
#define HIST_MAX        1000000
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
    uint16              xr1;
    uint16              xr2;
    uint16              xr4;
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
uint32              cpu_cmd(UNIT * uptr, uint16 cmd, uint16 dev);
t_stat              cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                        const char *cptr);
const char          *cpu_description (DEVICE *dptr);

/* Interval timer option */
t_stat              rtc_srv(UNIT * uptr);
t_stat              rtc_reset(DEVICE * dptr);
int32               rtc_tps = 60;

t_uint64            M[MAXMEMSIZE] = { 0 };      /* memory */
t_uint64            AC, MQ;                     /* registers */
uint16              XR[8];                      /* Index registers */
uint16              IC;                         /* program counter */
uint16              IR;                         /* Instruction register */
uint16              MA;                         /* Memory Address resister */
t_uint64            ID;                         /* Indicator registers */
t_uint64            SR;                         /* Internal temp register */
t_uint64            KEYS;                       /* Console Keys */
uint8               SL;                         /* Sense lights */
uint16              SW = 0;                     /* Sense switch */
uint8               MTM;                        /* Multi Index mode */
uint8               TM;                         /* Trap mode */
uint8               STM;                        /* Special trap mode */
uint8               CTM;                        /* Copy trap mode */
uint8               FTM;                        /* Floating trap mode */
uint8               nmode;                      /* Storage null mode */
uint8               smode;                      /* Signifigance mode */
uint8               itrap;                      /* Can take io traps */
uint8               dcheck;                     /* Divide check */
uint8               acoflag;                    /* AC Overflow */
uint8               mqoflag;                    /* MQ Overflow */
uint8               ihold = 0;                  /* Hold interrupts */
uint8               interval_irq = 0;           /* Interval timer IRQ */
uint16              iotraps;                    /* IO trap flags */
t_uint64            ioflags = 0;                /* Trap enable flags */
uint8               iocheck;
uint8               prot_pend;                  /* Protection mode pending */
uint8               relo_mode;                  /* Relocation mode */
uint8               relo_pend;                  /* Relocation mode pending */
uint8               hltinst;                    /* Executed halt instruction */
uint8               iowait;                     /* Waiting on io */
uint16              relocaddr = 0;              /* Relocation. */
uint16              baseaddr = 0;               /* Base Address. */
uint16              limitaddr = 077777;         /* High limit */
uint16              memmask = 077777;           /* Mask for memory access */
uint8               bcore;                      /* Access to B core memory */
                                                /* 00 - Execute A Core, Eff A */
                                                /* 01 - Execute A core, Eff B */
                                                /* 10 - Execute B core, Eff A */
                                                /* 11 - Execute B core, Eff B */
                                                /* 1xx - Protection mode */
                                                /* 1xxx - Relocation mode */
uint8               dualcore;                   /* Set to true if dual core in
                                                         use */

uint16              dev_pulse[NUM_CHAN];        /* SPRA device pulses */
int                 cycle_time = 12;            /* Cycle time in 100ns */
uint8               exe_KEYS = 0;               /* Execute one instruction
                                                   from KEYS, used by CPANEL */

/* History information */
int32               hst_p = 0;                  /* History pointer */
int32               hst_lnt = 0;                /* History length */
struct InstHistory *hst = NULL;                 /* History stack */
extern uint32       drum_addr;

#define DP_FLOAT        1
#define CHANNEL         2
#define MULTIIX         4
#define TIMER           7
#define INDICATORS      16
#define PROTECT         32
#define DUALCORE        64

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT                cpu_unit =
#ifdef I7090
    { UDATA(rtc_srv, UNIT_BINK | MODEL(CPU_7090) | MEMAMOUNT(4),
            MAXMEMSIZE/2 ), 120 };
#else
    { UDATA(rtc_srv, UNIT_BINK | MODEL(CPU_704) | MEMAMOUNT(4),
            MAXMEMSIZE/2 ), 120 };
#endif

REG                 cpu_reg[] = {
    {ORDATAD(IC, IC, 15, "Instruction Counter"), REG_FIT},
    {ORDATAD(IR, IR, 10, "Instruction Register"), REG_FIT},
    {ORDATAD(AC, AC, 38, "Accumulator"), REG_FIT},
    {ORDATAD(MQ, MQ, 36, "Multiplier Quotent"), REG_FIT},
    {BRDATAD(XR, XR, 8, 15, 8, "Index registers"), REG_FIT},
    {ORDATAD(ID, ID, 36, "Indicator Register")},
    {ORDATAD(MA, MA, 15, "Memory Address Register"), REG_FIT},
#ifdef EXTRA_SL
    {ORDATAD(SL, SL, 8, "Sense Lights"), REG_FIT},
#else
    {ORDATAD(SL, SL, 4, "Sense Lights"), REG_FIT},
#endif
#ifdef EXTRA_SW
    {ORDATAD(SW, SW, 12, "Sense Switches"), REG_FIT},
#else
    {ORDATAD(SW, SW, 6, "Sense Switches"), REG_FIT},
#endif
    {FLDATA(SW1, SW, 0), REG_FIT},
    {FLDATA(SW2, SW, 1), REG_FIT},
    {FLDATA(SW3, SW, 2), REG_FIT},
    {FLDATA(SW4, SW, 3), REG_FIT},
    {FLDATA(SW5, SW, 4), REG_FIT},
    {FLDATA(SW6, SW, 5), REG_FIT},
#ifdef EXTRA_SW
    {FLDATA(SW7, SW, 6), REG_FIT},
    {FLDATA(SW8, SW, 7), REG_FIT},
    {FLDATA(SW9, SW, 8), REG_FIT},
    {FLDATA(SW10, SW, 9), REG_FIT},
    {FLDATA(SW11, SW, 10), REG_FIT},
    {FLDATA(SW12, SW, 11), REG_FIT},
#endif
    {ORDATAD(KEYS, KEYS, 36, "Console Key Register"), REG_FIT},
    {ORDATAD(MTM, MTM, 1, "Multi Index registers"), REG_FIT},
    {ORDATAD(TM, TM, 1, "Trap mode"), REG_FIT},
    {ORDATAD(STM, STM, 1, "Select trap mode"), REG_FIT},
    {ORDATAD(CTM, CTM, 1, "Copy Trap Mode"), REG_FIT},
    {ORDATAD(FTM, FTM, 1, "Floating trap mode"), REG_FIT},
    {ORDATAD(NMODE, nmode, 1, "Storage null mode"), REG_FIT},
    {ORDATAD(ACOVF, acoflag, 1, "AC Overflow Flag"), REG_FIT},
    {ORDATAD(MQOVF, mqoflag, 1, "MQ Overflow Flag"), REG_FIT},
    {ORDATAD(IOC, iocheck, 1, "I/O Check flag"), REG_FIT},
    {ORDATAD(DVC, dcheck, 1, "Divide Check flag"), REG_FIT},
    {ORDATAD(RELOC, relocaddr, 14, "Relocation offset"), REG_FIT},
    {ORDATAD(BASE, baseaddr, 14, "Relocation base"), REG_FIT},
    {ORDATAD(LIMIT, limitaddr, 14, "Relocation limit"), REG_FIT},
    {ORDATAD(ENB, ioflags, 36, "I/O Trap Flags"), REG_FIT},
    {FLDATA(INST_BASE, bcore, 0), REG_FIT},
    {FLDATA(DATA_BASE, bcore, 1), REG_FIT},
    {NULL}
};

MTAB                cpu_mod[] = {
    {UNIT_MODEL, MODEL(CPU_704), "704", "704", NULL, NULL, NULL},
#ifdef I7090
    {UNIT_MODEL, MODEL(CPU_709), "709", "709", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(CPU_7090), "7090", "7090", NULL, NULL, NULL},
    {UNIT_MODEL, MODEL(CPU_7094), "7094", "7094", NULL, NULL, NULL},
#endif
    {UNIT_MSIZE, MEMAMOUNT(0), "4K", "4K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(1), "8K", "8K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(2), "16K", "16K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(4), "32K", "32K", &cpu_set_size},
#ifdef I7090
    {UNIT_FASTIO, 0, NULL, "TRUEIO", NULL, NULL, NULL,
       "True I/O mode"},
    {UNIT_FASTIO, UNIT_FASTIO, "FASTIO", "FASTIO", NULL, NULL, NULL,
       "Fast I/O mode"},
    {OPTION_EFP, 0, NULL, "NOEFP", NULL, NULL, NULL},
    {OPTION_EFP, OPTION_EFP, "EFP", "EFP", NULL, NULL, NULL, "Extended FP"},
    {OPTION_FPSM, 0, NULL, "NOFPSM", NULL, NULL, NULL},
    {OPTION_FPSM, OPTION_FPSM, "FPSM", "FPSM", NULL, NULL, NULL, "Signfigance mode"},
    {OPTION_TIMER, 0, NULL, "NOCLOCK", NULL, NULL, NULL},
    {OPTION_TIMER, OPTION_TIMER, "CLOCK", "CLOCK", NULL, NULL, NULL},
    {UNIT_DUALCORE, 0, NULL, "STANDARD", NULL, NULL, NULL},
    {UNIT_DUALCORE, UNIT_DUALCORE, "CTSS", "CTSS", NULL, NULL, NULL, "CTSS support"},
#endif
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
     &cpu_set_hist, &cpu_show_hist},
    {0}
};

DEVICE              cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 16, 1, 8, 36,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
};


#define T_B     0x0001          /* Do load with indirection */
#define T_D     0x0002          /* Do ea, but no indirection */
#define T_F     0x0004          /* Ea = Y, no indirection */
#define T_T     0x0008          /* Do ea with indirection, no load */
#define S_B     0x0010          /* Do ea, but store at end */
#define S_F     0x0020          /* Ea = Y, no indirection, store */
#define S_X     0x0040          /* Store index registers with SR */
#define T_N     0x0100          /* Nop op addressing, but instruct */
#define I_9     0x0200          /* 709 and up */
#define I_94    0x0400          /* 7094 only */
#define I_D     0x0800          /* Need dual core */
#define X_T     0x1000          /* Trap if in io trap mode or protection */
#define X_P     0x2000          /* Trap if in protection enabled */
#define X_C     0x4000          /* Trap if in copy trap mode or protection */
#define N       0x0000          /* No instruction */

/*
  Opcode flags, and execution.

  T_B   T_D     T_T     T_F     S_B     S_X     S_F
  *     *       *               *                       SR <- MA <- Y - xr
  *             *               *                       SR <- MA <- [MA] if ind
  *                     *                       *       SR <- [MA]
                                *               *       [MA] <- SR
                                        *               xr <- SR
*/

/* Positive opcodes */
uint16  pos_opcode_flags[01000] = {
        /* 0    1       2       3       4       5       6       7 */
        /*HTR*/
/* 0000 */ T_T, N,      N,      N,      N,      N,      N,      N,
/* 0010 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*TRA   TTR     TRCA            TRCC            TRCE    TRCG */
/* 0020 */ T_T, T_T,    X_T|T_T,N,      X_T|T_T,N,      X_T|T_T,X_T|T_T,
        /* TEFA TEFC    TEFE    TEFG */
/* 0030 */ X_T|T_T, X_T|T_T,X_T|T_T,X_T|T_T, N, N,      N,      N,
        /* TLQ   IIA     TIO     OAI      PAI           TIF  */
/* 0040 */ T_T,  I_9,    I_9|T_T,I_9,     I_9,   N,     I_9|T_T, N,
        /*        IIR                   RFT     SIR     RNT     RIR */
/* 0050 */ N,     I_9,  N,      N,      I_9,    I_9,    I_9,    I_9,
        /* TCOA TCOB    TCOC    TCOD    TCOE    TCOF    TCOG    TCOH */
/* 0060 */ X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,
        /*                              TSX */
/* 0070 */ N,   N,      N,      N,      S_X,    N,      N,      N,
        /* TZE     TIA */
/* 0100 */ T_T, I_D|X_P|T_T,N,  N,      N,      N,      N,      N,
        /*                              CVR     CVR+100*/
/* 0110 */ N,   N,      N,      N,      I_9,    I_9,    I_9,    I_9,
        /* TPL */
/* 0120 */ T_T, N,      N,      N,      N,      N,      N,      N,
        /*      XCA     */
/* 0130 */ N, I_9|T_N,  N,      N,      N,      N,      N,      N,
        /* TOV */
/* 0140 */ T_T, N,      N,      N,      N,      N,      N,      N,
/* 0150 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*     TQO      TQP */
/* 0160 */ N,   T_T,  T_T,      N,      N,      N,      N,      N,
/* 0170 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* MPY                          VLM       VLM + 100 */
/* 0200 */ T_B, N,      N,      N,      I_9|T_B, I_9|T_B,N,     N,
/* 0210 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* DVH    DVP                   VDH     VDP     VDH+200 VDP+200  */
/* 0220 */ T_B,  T_B,   N,      N,      I_9|T_B,I_9|T_B,I_9|T_B,I_9|T_B,
/* 0230 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* FDH  FDP*/
/* 0240 */ T_B, T_B,    N,      N,      N,      N,      N,      N,
/* 0250 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* FMP    DFMP */
/* 0260 */ T_B,  I_94|T_B,N,    N,      N,      N,      N,      N,
/* 0270 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* FAD  DFAD    FSB     DFSB    FAM     DFAM    FSM     DFSM */
/* 0300 */ T_B, I_94|T_B,  T_B,  I_94|T_B, I_9|T_B,I_94|T_B,I_9|T_B,I_94|T_B,
/* 0310 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* ANS          ERA */
/* 0320 */ T_B|S_B,N,  I_9|T_B, N,      N,      N,      N,      N,
/* 0330 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* CAS */
/* 0340 */ T_B, N,      N,      N,      N,      N,      N,      N,
/* 0350 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*      ACL */
/* 0360 */ N,   T_B,    N,      N,      N,      N,      N,      N,
/* 0370 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* ADD  ADM     SUB */
/* 0400 */ T_B, T_B,  T_B,      N,      N,      N,      N,      N,
/* 0410 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* HPR */
/* 0420 */ T_N, N,      N,      N,      N,      N,      N,      N,
/* 0430 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* IIS  LDI     OSI     DLD     OFT     RIS     ONT     */
/* 0440 */I_9|T_B,I_9|T_B,I_9|T_B,I_94|T_B,I_9|T_B,I_9|T_B,I_9|T_B,     N,
/* 0450 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* LDA */
/* 0460 */X_C|T_B,N,    N,      N,      N,      N,      N,      N,
/* 0470 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* CLA          CLS */
/* 0500 */ T_B, N,      T_B,    N,      N,      N,      N,      N,
/* 0510 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* ZET          XEC */
/* 0520 */ T_B, N,  I_9|T_B,    N,      N,      N,      N,      N,
        /*                              LXA     LAC */
/* 0530 */ N,   N,      N,      N,      S_X|T_F,T_F|S_X,N,      N,
        /* RSCA RSCC    RSCE    RSCG    STCA    STCC    STCE    STCG */
/* 0540 */ X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,
/* 0550 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* LDQ  ECA     LRI             ENB */
/* 0560 */ T_B, T_B|S_B,I_D|X_P|T_B,N,  I_9|T_B,N,      N,      N,
/* 0570 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* STZ  STO     SLW             STI*/
/* 0600 */ S_B, S_B,    S_B,    N,      I_9|S_B,N,      N,      N,
/* 0610 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*      STA     STD                     STT */
/* 0620 */ N,   T_B|S_B, T_B|S_B,N,     N,      T_B|S_B,N,      N,
        /*  STP                         SXA             SCA */
/* 0630 */ T_B|S_B,N,   N,      N,      I_9|S_F, N,     I_9|S_F,N,
        /*  SCHA SCHC   SCHE    SCHG    SCDA    SCDC    SCDE    SCDG */
/* 0640 */ T_T,  T_T,   T_T,    T_T,    X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,
/* 0650 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 0660 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* ELD  EAD      EDP     EMP */
/* 0670 */ T_B, T_B,    T_B,    T_B,    N,      N,      N,      N,
        /*  CPY */
/* 0700 */ X_C|T_B,N,   N,      N,      N,      N,      N,      N,
/* 0710 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 0720 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*                              PAX                     PAC */
/* 0730 */ N,   N,      N,      N,      S_X,    N,      N,    S_X|I_9,
/* 0740 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*                              PXA             PCA */
/* 0750 */ N,   N,      N,      N,      T_N,    N,      T_N,    N,
        /* PSE  NOP     RDS     LLS     BSR     LRS     WRS     ALS */
/* 0760 */ T_D, T_N, X_T|T_D,   T_D, X_T|T_D,   T_D,    X_T|T_D, T_D,
        /*  WEF ARS     REW             AXT     DRS     SDN      */
/* 0770 */ X_T|T_D,T_D, X_T|T_D, N,     S_X,    X_T|T_D,X_T|T_D, N
};

/* Negative opcodes */
uint16  neg_opcode_flags[01000] = {
/* 4000 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4010 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*      ESNT    TRCB            TRCD            TRCF    TRCH */
/* 4020 */ N,   I_9|T_T,X_T|T_T,N,      X_T|T_T,N,      X_T|T_T,X_T|T_T,
        /* TEFB TEFD    TEFF    TEFH */
/* 4030 */ X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,N,   N,      N,      N,
        /*              RIA                             PIA */
/* 4040 */ N,   N,      I_9,     N,     N,      N,      I_9,     N,
        /*      IIL                     LFT     SIL     LNT     RIL */
/* 4050 */ N,   I_9,    N,      N,      I_9,    I_9,    I_9,    I_9,
        /* TCNA TCNB    TCNC    TNCD    TNCE    TNCF    TNCG    TNCH */
/* 4060 */X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,
/* 4070 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* TNZ  TIB */
/* 4100 */ T_T,I_D|X_P|T_T, N,  N,      N,      N,      N,      N,
        /*                              CAQ    CAQ +100 */
/* 4110 */ N,   N,      N,      N,      I_9,    I_9,    I_9,    I_9,
        /* TMI */
/* 4120 */ T_T, N,      N,      N,      N,      N,      N,      N,
        /* XCL */
/* 4130 */I_9|T_N, N,   N,      N,      N,      N,      N,      N,
        /* TNO */
/* 4140 */ T_T, N,      N,      N,      N,      N,      N,      N,
        /*                              CRQ     CRQ+100 */
/* 4150 */ N,   N,      N,      N,      I_9,    I_9,    I_9,    I_9,
/* 4160 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4170 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* MPR */
/* 4200 */ T_B, N,      N,      N,      N,      N,      N,      N,
/* 4210 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4220 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4230 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* DFDH DFDP */
/* 4240 */ I_94|T_B, I_94|T_B,N,N,      N,      N,      N,      N,
/* 4250 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* UFM  DUFM */
/* 4260 */ T_B, I_94|T_B, N,    N,      N,      N,      N,      N,
/* 4270 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* UFA  DUFA    UFS     DUFS    UAM     DUAM    USM     DUSM */
/* 4300 */ T_B, I_94|T_B, T_B, I_94|T_B, I_9|T_B, I_94|T_B, I_9|T_B, I_94|T_B,
/* 4310 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* ANA */
/* 4320 */ T_B, N,      N,      N,      N,      N,      N,      N,
/* 4330 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* LAS */
/* 4340 */ I_9|T_B,N,   N,      N,      N,      N,      N,      N,
/* 4350 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4360 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4370 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* SBM */
/* 4400 */ T_B, N,      N,      N,      N,      N,      N,      N,
/* 4410 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4420 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4430 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4440 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4450 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4460 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4470 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* CAL  ORA */
/* 4500 */ T_B, T_B,    N,      N,      N,      N,      N,      N,
/* 4510 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* NZT */
/* 4520 */ I_9|T_B, N,  N,      N,      N,      N,      N,      N,
        /*                              LXD     LDC */
/* 4530 */ N,   N,      N,      N,      T_F|S_X,I_9|T_F|S_X, N, N,
        /* RSCB RSCD    RSCF    RSCH    STCB    STCD    STCF    STCH */
/* 4540 */ X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,
/* 4550 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*      ECQ                     LPI */
/* 4560 */ N,   S_B|T_B,N,      N,I_D|X_P|T_B,  N,      N,      N,
/* 4570 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* STQ  SRI          ORS        DST    SPI*/
/* 4600 */ S_B, I_D|S_B,    T_B|S_B,I_94|S_B,I_D|S_B,N,N,       N,
/* 4610 */ N,   N,      N,      N,      N,      N,      N,      N,
        /* SLQ                                  STL */
/* 4620 */ S_B|T_B,N,   N,      N,      N,      T_B|I_9|S_B,N,  N,
        /*                              SXD             SCD */
/* 4630 */ N,   N,      N,      N,      S_F,    N,      S_F,    N,
        /* SCHB SCHD    SCHF    SCHH    SCDB    SCDD    SCDF    SCDH */
/* 4640 */ T_T,  T_T,   T_T,    T_T,X_T|T_T,X_T|T_T,X_T|T_T,X_T|T_T,
/* 4650 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4660 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*      ESB     EUA     EST */
/* 4670 */ N,   T_B,    T_B,    T_B|S_B,N,      N,      N,      N,
        /* CAD */
/* 4700 */ X_C|T_B,N,   N,      N,      N,      N,      N,      N,
/* 4710 */ N,   N,      N,      N,      N,      N,      N,      N,
/* 4720 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*                              PDX                     PDC */
/* 4730 */ N,   N,      N,      N,      S_X,    N,      N,      S_X,
/* 4740 */ N,   N,      N,      N,      N,      N,      N,      N,
        /*                              PXD             PCD */
/* 4750 */ N,   N,      N,      N,      S_X,    N,      S_X,    N,
        /* MSE   SPOPS          LGL     BSF     LGR */
/* 4760 */ T_D,  T_D,   N,      T_D, X_T|T_D,   T_D,    N,      N,
        /*              RUN     RQL     AXC     TRS */
/* 4770 */ N,   N,      X_T|T_D,T_D,    S_X,    X_T|T_D,N,      N
};

#define CORE_B   0100000        /* Core B base address. */

#define do_trapmode  \
     if (TM) { \
          sim_interval = sim_interval - 1;      /* count down */ \
          M[0] &= ~AMASK; \
          M[0] |= (IC - 1) & memmask; \
          ihold = 1; \
     }

#define do_transfer(new_pc) IC = (TM)? 1 :(new_pc)

#define update_xr(t, v) \
         if ((t)) { \
             if (MTM) { \
                 if ((t)&04) XR[4] = (uint16)(v); \
                 if ((t)&02) XR[2] = (uint16)(v); \
                 if ((t)&01) XR[1] = (uint16)(v); \
             } else { \
                 XR[(t)] = (uint16)(v); \
             } \
         }

#define get_xr(t) \
    (((t)) ? ((MTM) ? (XR[(t)&04] | XR[(t)&02] | XR[(t)&01]) : XR[(t)]) : 0)

#define ReadMem(ind, reg) \
         /* In address nulify mode, half address */ \
         MA &= memmask; \
         if (bcore & 10)        /* Relocation enabled? adjust address */ \
           MA = AMASK & (MA + relocaddr); \
         if (bcore & 4) {       /* Protection enabled? check address */ \
           if (((MA & 077400) < baseaddr) || ((MA & 077400) >limitaddr)) { \
               /* Trap to A core */ \
               /* bcore to D3,4, IC=ADDR MA is going to DECR */ \
               M[032] = (((t_uint64)bcore & 3) << 31) | (((t_uint64)MA) << 18) | IC;\
                /* store 32, IC = 33 */ \
                IC = 033; \
               bcore = 0; \
               prot_pend = 0; \
               tbase = 0; \
               goto next_exe; \
           } \
         } \
         if ((ind) == 0 && bcore & 1)           /* Data in B core */ \
           MA |= CORE_B; \
         if ((ind) == 1 && bcore & 2)           /* Code in B core */ \
           MA |= CORE_B; \
         sim_interval = sim_interval - 1;               /* count down */ \
         reg = ReadP(MA);


#define WriteMem() \
         /* In address nulify mode, half address */ \
         MA &= memmask; \
         if (bcore & 10)        /* Relocation enabled? adjust address */ \
           MA = AMASK & (MA + relocaddr); \
         if (bcore & 4) {       /* Protection enabled? check address */ \
           if (((MA & 077400) < baseaddr) || ((MA & 077400) >limitaddr)) { \
               /* Trap to A core */ \
               /* bcore to D3,4, IC=ADDR MA is going to DECR */ \
               M[032] = (((t_uint64)bcore & 3) << 31) | (((t_uint64)MA) << 18) | IC;\
                /* store 32, IC = 33 */ \
                IC = 033; \
               bcore = 0; \
               prot_pend = 0; \
               tbase = 0; \
               goto next_exe; \
           } \
         } \
         if (bcore & 1)                 /* Data in B core */ \
           MA |= CORE_B; \
         sim_interval = sim_interval - 1;               /* count down */ \
         WriteP(MA, SR);

t_stat
sim_instr(void)
{
    t_stat              reason;
    t_uint64            temp = 0LL;
#ifdef I7090
    t_uint64            ibr;
#endif
    uint16              opcode;
    uint8               tag;
    uint16              decr;
    uint16              xr;
    uint16              opinfo;
    int                 fptemp = 0, fptemp2;
    uint8               f;
    uint16              tbase;
    int                 xeccnt = 15;
    int                 shiftcnt;
    int                 stopnext = 0;
    int                 instr_count = 0; /* Number of instructions to execute */

    if (sim_step != 0) {
        instr_count = sim_step;
        sim_cancel_step();
    }

    /* Set cycle time for delays */
    switch(CPU_MODEL) {
    case CPU_704: cycle_time = 50; break;    /* Needed to allow SAP to work */
    case CPU_709: cycle_time = 120; break;   /* 83,333 cycles per second */
    default:
    case CPU_7090:  cycle_time = 22; break;  /* 454,545 cycles per second */
    case CPU_7094:  cycle_time = 18; break;  /* 555,555 cycles per second */
    }

    reason = 0;
    hltinst = 0;

    /* Enable timer if option set */
    if (cpu_unit.flags & OPTION_TIMER) {
        sim_activate(&cpu_unit, 10000);
    }
    interval_irq = 0;

/* Main instruction fetch/decode loop */
    tbase = 0;
    if (bcore & 010)
        tbase = relocaddr;
    if (bcore & 2)
        tbase |= CORE_B;

    iowait = 0;
    ihold = 0;
    while (reason == 0) {       /* loop until halted */

        if (exe_KEYS) {
           SR = KEYS;
           hltinst = 1;
           exe_KEYS = 0;
           goto next_xec;
        }

   hltloop:
/* If doing fast I/O don't sit in idle loop */
        if (iowait && (cpu_unit.flags & UNIT_FASTIO))
            sim_interval = 0;
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
        }

#if defined(CPANEL)
        if (cpanel_interval > 0) {
            if (cpanel_interval > 1) {
                cpanel_interval--;
            } else {
                reason = ControlPanel_Refresh_CPU_Running();
                /* do control panel refresh and user clicks event processing */
                if (reason != SCPE_OK)
                    break;
            }
        }
#endif

        if (iowait == 0 && sim_brk_summ &&
                 sim_brk_test(((bcore & 2)? CORE_B:0)|IC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

/* Check if we need to take any traps */
#ifdef I7090    /* I704 did not have interrupts */
        if (CPU_MODEL != CPU_704 && itrap && ihold == 0 && iowait == 0 && ioflags != 0 && instr_count == 0) {
            t_uint64            mask = 00000001000001LL;

            MA = 012;
            f = 0;

            for (shiftcnt = 1; shiftcnt < NUM_CHAN; shiftcnt++) {
                /* CRC *//* Trap *//* EOF */
                /* Wait until channel stops to trigger interupts */
                if (ioflags & mask) {
                    f = 0;
                    if (mask & AMASK & ioflags) {
                        if (chan_stat(shiftcnt, CHS_EOF))
                            f |= 4;     /* We have a EOF */
                        if (iotraps & (1 << shiftcnt)) {
                            f |= 1;     /* We have a IOCT/IORT/IOST */
                            iotraps &= ~(1 << shiftcnt);
                        }
                    }
                    if (mask & DMASK & ioflags && chan_stat(shiftcnt, CHS_ERR))
                        f |= 2;        /* We have device error */
                    /* check if we need to perform a trap */
                    if (f) {
                        /* HTR/HPR behave like wait if protected */
                        if (hltinst)
                            temp = (((t_uint64) bcore & 3) << 31) |
                                  (((t_uint64) f) << 18) | (fptemp & memmask);
                        else
                            temp = (((t_uint64) bcore & 3) << 31) |
                                  (((t_uint64) f) << 18) | (IC & memmask);
                        hltinst = 0;
                        sim_interval = sim_interval - 1;        /* count down */
                        WriteP(MA, temp);
                        if (nmode) {
                            memmask <<= 1;
                            memmask |= 1;
                            nmode = 0;
                        }
                        MA++;
                        tbase = 0;
                        prot_pend = itrap = bcore = iowait = 0;
                        ihold = 1;
                        sim_interval = sim_interval - 1;        /* count down */
                        SR = ReadP(MA);
                        sim_debug(DEBUG_TRAP, &cpu_dev,
                          "Doing trap chan %c %o >%012llo loc %o %012llo IC=%06o\n",
                                  shiftcnt + 'A' - 1, f, temp, MA, SR, IC);
                        if (hst_lnt) {  /* history enabled? */
                            hst_p = (hst_p + 1);        /* next entry */
                            if (hst_p >= hst_lnt)
                                hst_p = 0;
                            hst[hst_p].ic = MA | HIST_PC | (bcore << 18);
                            hst[hst_p].ea = 0;
                            hst[hst_p].op = SR;
                            hst[hst_p].ac = AC;
                            hst[hst_p].mq = MQ;
                            hst[hst_p].xr1 = XR[1];
                            hst[hst_p].xr2 = XR[2];
                            hst[hst_p].xr4 = XR[4];
                            hst[hst_p].sr = 0;
                        }
                        goto next_xec;
                    }
                }
                MA += 2;
                mask <<= 1;
            }

            /* Interval timer has lower priority then I/O traps */
            if (interval_irq && (ioflags & 0400000)) {
                /* HTR/HPR behave like wait if protected */
                if (hltinst)
                    temp = (((t_uint64) bcore & 3) << 31) |
                          (fptemp & memmask) | (relo_mode << 21);
                else
                    temp = (((t_uint64) bcore & 3) << 31) |
                          (IC & memmask) | (relo_mode << 21);
                hltinst = 0;
                sim_interval = sim_interval - 1;        /* count down */
                MA = 6;
                WriteP(MA, temp);
                if (nmode) {
                    memmask <<= 1;
                    memmask |= 1;
                    nmode = 0;
                }
                MA++;
                prot_pend = 0;
                interval_irq = prot_pend = itrap = bcore = iowait = 0;
                ihold = 1;
                sim_interval = sim_interval - 1;        /* count down */
                SR = ReadP(MA);
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                          "Doing timer trap >%012llo loc %o %012llo\n", temp,
                          MA, SR);
                if (hst_lnt) {  /* history enabled? */
                    hst_p = (hst_p + 1);        /* next entry */
                    if (hst_p >= hst_lnt)
                        hst_p = 0;
                    hst[hst_p].ic = MA | HIST_PC | (bcore << 18);
                    hst[hst_p].ea = 0;
                    hst[hst_p].op = SR;
                    hst[hst_p].ac = AC;
                    hst[hst_p].mq = MQ;
                    hst[hst_p].xr1 = XR[1];
                    hst[hst_p].xr2 = XR[2];
                    hst[hst_p].xr4 = XR[4];
                    hst[hst_p].sr = 0;
                }
                goto next_xec;
            }
        }

        if (hltinst) {
             t_uint64            mask = 00000001000001LL;
             /* Hold out until all channels have idled out */
             sim_interval = sim_interval - 1;        /* count down */
             chan_proc();
             f = chan_active(0);
             for (shiftcnt = 1; f == 0 && shiftcnt < NUM_CHAN; shiftcnt++)  {
                f = chan_active(shiftcnt);
                /* CRC *//* Trap *//* EOF */
                /* Wait until channel stops to trigger interrupts */
                if (itrap) {
                    /* Check for EOF or IOCT/IORT/IOST */
                    if (mask & AMASK & ioflags &&
                        (chan_test(shiftcnt, CHS_EOF) || iotraps & (1 << shiftcnt)))
                         f = 1;
                    if (mask & DMASK & ioflags && chan_test(shiftcnt, CHS_ERR))
                         f = 1; /* We have device error */
                 }
             }

             /* If all channels idle and not in protected mode, real halt */
             if (f == 0 && (bcore & 4) == 0) {
                reason = STOP_HALT;
                break;
             }
             goto hltloop;
        }
#else                     /* Handle halt on 704 */
        if (hltinst) {
             sim_interval = sim_interval - 1;        /* count down */
             chan_proc();
             if (chan_active(0))
                goto hltloop;
             reason = STOP_HALT;
             break;
        }
#endif

/* Split out current instruction */
      next_exe:
        if (iowait) {
            /* If we are awaiting I/O complete, don't fetch. */
            sim_interval--;
            SR = temp;
            iowait = 0;
        } else {
            xeccnt = 15;
            MA = IC;
            ReadMem(1, SR);
            temp = SR;
            if (hst_lnt) {      /* history enabled? */
                hst_p = (hst_p + 1);    /* next entry */
                if (hst_p >= hst_lnt)
                    hst_p = 0;
                hst[hst_p].ic = MA | HIST_PC | (bcore << 18);
                hst[hst_p].ea = 0;
                hst[hst_p].op = SR;
                hst[hst_p].ac = AC;
                hst[hst_p].mq = MQ;
                hst[hst_p].xr1 = XR[1];
                hst[hst_p].xr2 = XR[2];
                hst[hst_p].xr4 = XR[4];
                hst[hst_p].sr = 0;
            }
            IC = memmask & (IC + 1);
        }
        if (ihold != 0)
            ihold--;
        else if (relo_pend || prot_pend) {
            bcore = (bcore & 3) | (relo_pend << 3) | (prot_pend << 2);
            relo_pend = 0;
            prot_pend = 0;
        }


      next_xec:
        opcode = (uint16)(SR >> 24);
        IR = opcode;
        if (hst_lnt) {  /* history enabled? */
            hst[hst_p].op = SR;
        }
        MA = (uint16)(SR & AMASK);
        tag = (uint8)(SR >> 15) & 07;
        decr = (uint16)((SR >> 18) & AMASK);
                                        /* Set flags to D to start with */
        xr = get_xr(tag);
        iowait = 0;                     /* Kill iowait */
        sim_interval--;                 /* one cycle for execute */

        switch (opcode & 07000) {
        case (OP_TXI << 9):     /* Transfer and inc  XR[T] += D, IC <- Y */
            do_trapmode;

            decr &= memmask;
            xr += decr;
            xr &= memmask;
            if (hst_lnt) {      /* history enabled? */
                hst[hst_p].ea = decr;
                hst[hst_p].sr = xr;
            }

            /* Save register */
            update_xr(tag, xr);
            do_transfer(MA);
            break;

        case (OP_TXH << 9):     /* Transfer on High Index XR[T] > D, IC <- Y */
            do_trapmode;
            if (hst_lnt) {      /* history enabled? */
                hst[hst_p].ea = decr;
                hst[hst_p].sr = xr;
            }
            xr &= memmask;
            decr &= memmask;
            if (tag && xr > decr)
                do_transfer(MA);
            break;
        case (OP_TNX << 9):     /* Transfer No index XR[T]  */
            do_trapmode;
            if (hst_lnt) {      /* history enabled? */
                hst[hst_p].ea = decr;
                hst[hst_p].sr = xr;
            }
            xr &= memmask;
            decr &= memmask;
            if (tag && xr > decr) {
                xr = AMASK & (xr - decr);
                update_xr(tag, xr);
            } else {
                do_transfer(MA);
            }
            break;
        case (OP_TXL << 9):     /* Transfer on Low Index XR[T] <= D, IC <- Y */
            do_trapmode;
            if (hst_lnt) {      /* history enabled? */
                hst[hst_p].ea = decr;
                hst[hst_p].sr = xr;
            }
            xr &= memmask;
            decr &= memmask;
            if (tag == 0 || xr <= decr)
                do_transfer(MA);
            break;
        case (OP_TIX << 9):     /* Transfer and Index XR */
            do_trapmode;
            if (hst_lnt) {      /* history enabled? */
                hst[hst_p].ea = decr;
                hst[hst_p].sr = xr;
            }
            xr &= memmask;
            decr &= memmask;
            if (tag && xr > decr) {
                xr = AMASK & (xr - decr);
                update_xr(tag, xr);
                do_transfer(MA);
            }
            break;
        case (OP_STR << 9):
            M[tbase] &= ~AMASK;
            M[tbase] |= IC & memmask;
            if (hst_lnt)        /* history enabled? */
                hst[hst_p].ea = tbase;
            IC = 2;
            break;
        case (4 << 9):          /* Neg opcodes */
            opinfo = neg_opcode_flags[opcode & 0777];
            goto proc_op;
        case 0:         /* Pos opcodes */
            opinfo = pos_opcode_flags[opcode];
          proc_op:
            /* If proc does not support this opcode, just skip it */
            if (opinfo & I_9 && CPU_MODEL == CPU_704)
                break;
            if (opinfo & I_94 && CPU_MODEL != CPU_7094)
                break;
            if (opinfo & (X_P|X_T) && (bcore & 4)) {
                /* Trap to 0 and drop out of B core */
prottrap:
                MA = 032;
                if (nmode) {
                    memmask <<= 1;
                    memmask |= 1;
                }
                temp = ((t_uint64) (bcore & 3) << 31) | IC;
                tbase = 0;
                prot_pend = nmode = bcore = STM = CTM = 0;
                WriteP(MA, temp);
                IC = MA + 1;
                break;
            }
            /* Check for traping conditions */
                /* Check for protection mode */
            if (opinfo & X_T && STM) {
                /* Trap to 40001 */
                MA = MEMSIZE >> 1;
                if (nmode) {
                    memmask <<= 1;
                    memmask |= 1;
                }
                temp = ((t_uint64) (bcore & 3) << 31) | IC;
                tbase = 0;
                prot_pend = nmode = bcore = STM = CTM = 0;
                WriteP(MA, temp);
                IC = MA + 1;
                break;
            }
                /* Check for protection mode */
            if (opinfo & X_C && CTM) {
                /* Trap to 40002 */
                MA = MEMSIZE >> 1;
                if (nmode) {
                    memmask <<= 1;
                    memmask |= 1;
                }
                temp = ((t_uint64) (bcore & 3) << 31) | IC;
                tbase = 0;
                prot_pend = nmode = bcore = STM = CTM = 0;
                WriteP(MA, temp);
                IC = MA + 2;
                break;
            }
            /* Merge in index register if needed */
            if (opinfo & (T_B | T_D | T_T | S_B))
                MA = memmask & (MA - xr);
            decr &= 077;        /* Set flags to D to start with */
            /* If indirect flag and indirect instruction do memory access */
            if ((CPU_MODEL != CPU_704) && ((decr & 060) == 060) &&
                (opinfo & (T_B | T_T | S_B))) {
                ReadMem(1, SR);
                tag = (uint8)((SR >> 15) & 07);
                xr = get_xr(tag);
                MA = (uint16)(memmask & (SR - xr));
            }
            MA &= memmask;
            if (opinfo & (T_B | T_F | S_F)) {
                ReadMem(opcode == OP_XEC, SR);
            }
            if (hst_lnt) {      /* history enabled? */
                hst[hst_p].ea = MA;
                hst[hst_p].sr = SR;
            }
            switch (opcode) {
            case 0760:          /* PSE */
/* Positive 0760 opcodes */
                switch (MA) {
#ifdef I7090    /* Not on 704 */
                case OP_RDCA:   /* Reset data channel */
                case OP_RDCB:
                case OP_RDCC:
                case OP_RDCD:
                case OP_RDCE:
                case OP_RDCF:
                case OP_RDCG:
                case OP_RDCH:
                    if (CPU_MODEL == CPU_704)
                        break;
                    if ((bcore & 4) || STM)
                        goto seltrap;
                    f = (MA >> 9) & 017;
                    chan_rst(f, 1);
                    break;
                case OP_RICA:   /* Reset channel */
                case OP_RICB:
                case OP_RICC:
                case OP_RICD:
                case OP_RICE:
                case OP_RICF:
                case OP_RICG:
                case OP_RICH:
                    if (CPU_MODEL == CPU_704)
                        break;
                    if ((bcore & 4) || STM)
                        goto seltrap;
                    chan_rst((MA >> 9) & 017, 0);
                    break;
                    /* Not great coding, but keeps multiple copys out of code */
#endif
                  seltrap:
                    /* Trap to 40000 or 0 depending on mode */
                    if (bcore & 4)
                        MA = 032;
                    else
                        MA = MEMSIZE >> 1;
                    if (nmode) {
                        memmask <<= 1;
                        memmask |= 1;
                    }
                    temp = ((t_uint64) (bcore & 3) << 31) | IC;
                    tbase = 0;
                    prot_pend = nmode = bcore = STM = CTM = 0;
                    WriteP(MA, temp);
                    IC = MA + 1;
                    break;

                case OP_BTTA:   /* Skip BOT */
                case OP_BTTB:   /* Skip BOT */
                case OP_BTTC:   /* Skip BOT */
                case OP_BTTD:   /* Skip BOT */
                case OP_BTTE:   /* Skip BOT */
                case OP_BTTF:   /* Skip BOT */
                case OP_BTTG:   /* Skip BOT */
                case OP_BTTH:   /* Skip BOT */
                    if (CPU_MODEL == CPU_704)
                        break;
                    if ((bcore & 4) || STM)
                        goto seltrap;
                    if (chan_stat((MA >> 9) & 017, CHS_BOT) == 0)
                        IC++;
                    break;
                case OP_SLF:
                    SL = 0;
                    break;
                case OP_SLN1:
                case OP_SLN2:
                case OP_SLN3:
                case OP_SLN4:
#ifdef EXTRA_SL
                case OP_SLN5:
                case OP_SLN6:
                case OP_SLN7:
                case OP_SLN8:
#endif
                    SL |= 1 << (MA - OP_SLN1);
                    break;
                case OP_SWT1:
                case OP_SWT2:
                case OP_SWT3:
                case OP_SWT4:
                case OP_SWT5:
                case OP_SWT6:
                    if (SW & (1 << (MA - OP_SWT1)))
                        IC++;
                    break;
                case OP_LBT:
                    if (AC & 1)
                        IC++;
                    break;
                case OP_CLM:
                    AC &= AMSIGN;
                    break;
                case OP_CHS:
                    AC ^= AMSIGN;
                    break;
                case OP_SSP:
                    AC &= AMMASK;
                    break;
                case OP_COM:
                    AC ^= AMMASK;
                    break;
                case OP_ENK:
                    MQ = KEYS;
                    break;
                case OP_IOT:    /* Skip & clear ioready */
                    if (iocheck == 0)
                        IC++;
                    iocheck = 0;
                    break;
                case OP_ETM:
                    if (bcore & 4)
                        goto prottrap;
                    TM = 1;
                    break;
                case OP_RND:
                    if (MQ & ONEBIT) {
                        SR = 1;
                        goto iadd;
                    }
                    break;
                case OP_FRN:
                    /* Extract AC char */
                    temp = 0;
                    if (MQ & FPNBIT) {
                        /* Save everything but characterist, +1 to fraction */
                        SR = (AC & (FPMMASK | AMSIGN | AQSIGN | APSIGN)) + 1;
                        /* If overflow, normalize */
                        if (SR & FPOBIT) {
                            SR >>= 1;   /* Move right one bit */
                            if ((AC & (AQSIGN | APSIGN | FPCMASK)) ==
                                FPCMASK) {
                                temp = FPOVERR | FPACERR;
                            }
                            AC += FPOBIT;       /* Fix characteristic */
                            /* Fix the sign */
                            AC &= AMMASK;
                            AC |= (SR & AQSIGN) << 1;   /* Fix sign */
                        }
                        /* Restore fixed ac */
                        AC &= ~FPMMASK;
                        AC |= SR & FPMMASK;
                        if (temp != 0)
                            goto dofptrap;
                    }
                    break;
                case OP_DCT:
                    if (dcheck == 0)
                        IC++;
                    dcheck = 0;
                    break;
#ifdef I7090    /* Not on 704 */
                case OP_RCT:
                    if (CPU_MODEL != CPU_704) {
                        if (bcore & 4)
                            goto prottrap;
                        sim_debug(DEBUG_TRAP, &cpu_dev, "RCT %012llo\n", ioflags);
                        if ((bcore & 4) || STM)
                            goto seltrap;
                        itrap = 1;
                        if (CPU_MODEL == CPU_709)
                            ihold = 1;
                        else
                            ihold = 2;
                    }
                    break;
                case OP_LMTM:
                    if (CPU_MODEL != CPU_704)
                        MTM = 0;
                    break;
#endif
                default:
                    if ((bcore & 4) || STM)
                        goto seltrap;
                    f = MA >> 9;
                    if (f < 11) {
                        MA &= 0777;
                        if (MA >= 0341 && MA <= 0372) {
                            MA -= 0341;
                            if (MA < PUNCH_M) {
                                dev_pulse[f] |= 1 << MA;
                            } else {
                                MA -= 13;
                                if (MA == 2) {
                                    if (dev_pulse[f] & PRINT_I)
                                        IC++;
                                    dev_pulse[f] &= ~PRINT_I;
                                } else {
                                    dev_pulse[f] |= 1 << MA;
                                }
                            }
                        }
                    }
                    break;
                }
                break;
            case 04760: /* MSE */
/* Negative 04760 opcodes */
                switch (MA) {
                case OP_ETTA:   /* Transfer on EOT */
                case OP_ETTB:   /* Transfer on EOT */
                case OP_ETTC:   /* Transfer on EOT */
                case OP_ETTD:   /* Transfer on EOT */
                case OP_ETTE:   /* Transfer on EOT */
                case OP_ETTF:   /* Transfer on EOT */
                case OP_ETTG:   /* Transfer on EOT */
                case OP_ETTH:   /* Transfer on EOT */
                    if ((bcore & 4) || STM)
                        goto seltrap;
                    if (chan_stat((MA >> 9) & 017, CHS_EOT) == 0)
                        IC++;
                    break;
                case OP_PBT:
                    if (AC & APSIGN)
                        IC++;
                    break;
                case OP_EFTM:
                    if (CPU_MODEL != CPU_704)
                        FTM = 1;
                    break;
                case OP_SSM:
                    AC |= AMSIGN;
                    break;
#ifdef I7090    /* Not on 704 */
                case OP_LFTM:
                    if (bcore & 4)
                        goto prottrap;
                    if (CPU_MODEL != CPU_704)
                        acoflag = mqoflag = FTM = 0;
                    break;
                case OP_ESTM:
                    if (bcore & 4)
                        goto prottrap;
                    if (CPU_MODEL != CPU_704)
                        STM = 1;
                    break;
                case OP_ECTM:
                    if (bcore & 4)
                        goto prottrap;
                    if (CPU_MODEL != CPU_704)
                        CTM = 1;
                    break;
                case OP_EMTM:
                    if (CPU_MODEL != CPU_704)
                        MTM = 1;
                    break;
#endif
                case OP_LTM:
                    if (bcore & 4)
                        goto prottrap;
                    TM = 0;
                    break;
                case OP_LSNM:
                    if (nmode) {
                        memmask <<= 1;
                        memmask |= 1;
                    }
                    nmode = 0;
                    break;
                case OP_ETT:
                    if ((bcore & 4) || STM)
                        goto seltrap;
                    if (chan_stat(0, CHS_EOT) == 0)
                        IC++;
                    break;
                case OP_RTT:
                    if ((bcore & 4) || STM)
                        goto seltrap;
                    /* Check ERR on channel 0 */
                    if (chan_stat(0, CHS_ERR) == 0)
                        IC++;
                    break;
                case OP_SLT1:
                case OP_SLT2:
                case OP_SLT3:
                case OP_SLT4:
#ifdef EXTRA_SL
                case OP_SLT5:
                case OP_SLT6:
                case OP_SLT7:
                case OP_SLT8:
#endif
                    f = 1 << (MA - OP_SLN1);
                    if (SL & f)
                        IC++;
                    SL &= ~f;
                    break;
#ifdef EXTRA_SW
                case OP_SWT7:
                case OP_SWT8:
                case OP_SWT9:
                case OP_SWT10:
                case OP_SWT11:
                case OP_SWT12:
                    if (SW & (1 << (6 + MA - OP_SWT7)))
                        IC++;
                    break;
#endif
                default:
                    break;
                }
                break;
/* Transfer opcodes */
            case OP_HTR:
                /* Stop at HTR instruction if trapped */
                IC--;
                /* Fall through */
            case OP_HPR:
              halt:
                hltinst = 1;
                ihold = 0;      /* Kill any hold on traps now */
                if (opcode == OP_HTR) {
                     fptemp = IC-1;
                     IC = MA;
                } else
                     fptemp = IC;
                break;
            case OP_XEC:
                opcode = (uint16)(SR >> 24);
                if (opcode != OP_XEC) {
                    xeccnt = 15;
                    goto next_xec;
                }
                if (xeccnt-- != 0) {
                    iowait = 1;
                    goto next_xec;
                }
                reason = STOP_XECLIM;
                break;
            case OP_NOP:
                break;
            case OP_TTR:
                IC = MA;
                break;
            case OP_TLQ:
                do_trapmode;
                /* Is AC - and MQ + */
                if ((MQ & MSIGN) == 0 && (AC & AMSIGN) != 0)
                    break;
                /* Same sign, compare magintudes */
                if (((MQ & MSIGN) == 0 && (AC & AMSIGN) == 0)) {
                    SR = (MQ & PMASK) - (AC & AQMASK);
                    if ((SR & AMSIGN) == 0)
                        break;
                } else if (((MQ & MSIGN) != 0 && (AC & AMSIGN) != 0)) {
                    SR = (AC & AQMASK) - (MQ & PMASK);
                    if ((SR & AMSIGN) == 0)
                        break;
                }
                /* Nope, MQ bigger, so take branch */
                do_transfer(MA);
                break;
            case OP_TRA:
                do_trapmode;
                do_transfer(MA);
                break;
            case OP_TSX:
                do_trapmode;
                /* Exchange SR and IC */
                SR = AMASK & (-(IC - 1));
                do_transfer(MA);
                break;
            case OP_TZE:
                f = (AC & AMMASK) == 0;
              branch:
                do_trapmode;
                if (f) {
                    do_transfer(MA);
                }
                break;
            case OP_TOV:
                f = acoflag;
                acoflag = 0;
                goto branch;
            case OP_TQP:
                f = ((MQ & MSIGN) == 0);
                goto branch;
            case OP_TQO:
                if ((CPU_MODEL == CPU_704) || FTM == 0) {
                    f = mqoflag;
                    mqoflag = 0;
                    goto branch;
                }
                break;
            case OP_TPL:
                f = ((AC & AMSIGN) == 0);
                goto branch;
            case OP_TNZ:
                f = ((AC & AMMASK) != 0);
                goto branch;
            case OP_TMI:
                f = ((AC & AMSIGN) != 0);
                goto branch;
            case OP_TNO:
                f = !acoflag;
                acoflag = 0;
                goto branch;
            case OP_NZT:
                if ((SR & PMASK) != 0)
                    IC++;
                break;
            case OP_ZET:
                if ((SR & PMASK) == 0)
                    IC++;
                break;
            case OP_ESNT:
                IC = MA;
                if (!nmode)
                    memmask >>= 1;
                nmode = 1;
                break;
#ifdef I7090    /* Not on 704 */
/* Indicator opcodes */
            case OP_IIA:
                ID ^= AC & AQMASK;
                break;
            case OP_IIS:
                ID ^= SR;
                break;
            case OP_IIR:
                ID ^= SR & RMASK;
                break;
            case OP_IIL:
                ID ^= (SR & RMASK) << 18;
                break;
            case OP_OAI:
                ID |= AC & AQMASK;
                break;
            case OP_OSI:
                ID |= SR;
                break;
            case OP_SIR:
                ID |= (SR & RMASK);
                break;
            case OP_SIL:
                ID |= (SR & RMASK) << 18;
                break;
            case OP_RIA:
                ID &= ~AC;
                break;
            case OP_RIS:
                ID &= ~SR;
                break;
            case OP_RIR:
                ID &= ~(SR & RMASK);
                break;
            case OP_RIL:
                ID &= ~((SR & RMASK) << 18);
                break;
            case OP_PIA:
                AC = ID & AQMASK;
                break;
            case OP_PAI:
                ID = AC & AQMASK;
                break;
            case OP_LDI:
                ID = SR;
                break;
            case OP_STI:
                SR = ID;
                break;
            case OP_ONT:
                if ((ID & SR) == SR)
                    IC++;
                break;
            case OP_OFT:
                if ((ID & SR) == 0)
                    IC++;
                break;
            case OP_RFT:
                if (0 == (SR & ID & RMASK))
                    IC++;
                break;
            case OP_LFT:
                if (0 == (((SR & RMASK) << 18) & ID))
                    IC++;
                break;
            case OP_RNT:
                if ((SR & RMASK) == (SR & ID & RMASK))
                    IC++;
                break;
            case OP_LNT:
                if ((SR & RMASK) == (SR & (ID >> 18) & RMASK))
                    IC++;
                break;
            case OP_TIO:
                do_trapmode;
                if ((ID & AC) == (AC & AQMASK))
                    do_transfer(MA);
                break;
            case OP_TIF:
                do_trapmode;
                if ((ID & AC) == 0)
                    do_transfer(MA);
                break;
#endif
/* General index and load store opcodes */
            case OP_XCA:
                SR = (AC & (PMASK));
                if (AC & AMSIGN)
                    SR |= MSIGN;
                AC = MQ;
                if (AC & APSIGN)
                    AC ^= AMSIGN | APSIGN;
                MQ = SR;
                break;
            case OP_XCL:
                SR = AC & AQMASK;
                AC = MQ & AQMASK;
                MQ = SR;
                break;
            case OP_AXC:
                SR = (t_uint64)(-(t_int64)SR);
                break;
            case OP_AXT:
                break;
            case OP_LXA:
                SR &= memmask;
                break;
            case OP_LAC:
                SR = (t_uint64)(-(t_int64)SR);
                SR &= memmask;
                break;
            case OP_LDQ:
                MQ = SR;
                break;
            case OP_LXD:
                SR >>= 18;
                SR &= memmask;
                break;
            case OP_LDC:
                SR >>= 18;
                SR = (t_uint64)(-(t_int64)SR);
                SR &= memmask;
                break;
            case OP_CLA:
                AC = ((SR & MSIGN) << 2) | (SR & PMASK);
                break;
            case OP_CLS:
                AC = (((SR & MSIGN) ^ MSIGN) << 2) | (SR & PMASK);
                break;
            case OP_CAL:
                AC = SR;
                break;
            case OP_STQ:
                SR = MQ;
                break;
            case OP_ECA:
                temp = AC;
                AC = SR;
                SR = temp;
                break;
            case OP_ECQ:
                temp = MQ;
                MQ = SR;
                SR = temp;
                break;
            case OP_SLQ:
                SR = (SR & RMASK) | (MQ & LMASK);
                break;
            case OP_STL:
                SR &= ~AMASK;
                SR |= IC & memmask;
                break;
            case OP_STZ:
                SR = 0;
                break;
            case OP_STO:
                SR = AC & PMASK;
                if (AC & AMSIGN)
                    SR |= MSIGN;
                break;
            case OP_SLW:
                SR = AC & AQMASK;
                break;
            case OP_STA:
                SR &= ~AMASK;
                SR |= AC & AMASK;
                break;
            case OP_STD:
                SR &= ~DMASK;
                SR |= AC & DMASK;
                break;
            case OP_STT:
                SR &= ~TMASK;
                SR |= AC & TMASK;
                break;
            case OP_STP:
                SR &= ~PREMASK;
                SR |= AC & PREMASK;
                break;
            case OP_SXA:
                SR &= ~AMASK;
                SR |= memmask & xr;
                update_xr(tag, xr);
                break;
            case OP_SCA:
                SR &= ~AMASK;
                SR |= memmask & (-xr);
                update_xr(tag, xr);
                break;
            case OP_SCD:
                SR &= ~DMASK;
                temp = (-xr) & memmask;
                temp &= AMASK;
                temp <<= 18;
                SR |= temp;
                update_xr(tag, xr);
                break;
            case OP_SXD:
                SR &= ~DMASK;
                temp = xr & memmask;
                temp &= AMASK;
                temp <<= 18;
                SR |= temp;
                update_xr(tag, xr);
                break;
            case OP_PDX:
                SR = memmask & (AC >> 18);
                break;
            case OP_PDC:
                SR = (t_uint64)(memmask & (-(t_int64)(AC >> 18)));
                break;
            case OP_PXD:
                SR = AC = xr & memmask;
                AC <<= 18;
                break;
            case OP_PCD:
                AC = (-xr) & memmask;
                AC <<= 18;
                SR = xr & memmask;
                break;
            case OP_PAX:
                SR = memmask & AC;
                break;
            case OP_PAC:
                SR = (t_uint64)(memmask & (-(t_int64)AC));
                break;
            case OP_PXA:
                AC = memmask & xr;
                SR = xr & memmask;
                break;
            case OP_PCA:
                AC = AMASK & (-xr);
                SR = xr & AMASK;
                break;
/* Integer math */
            case OP_CAS:
                if (AC & AMSIGN) {
                    if (SR & MSIGN) {
                        if ((AC & AMMASK) == (SR & PMASK))
                            IC++;
                        else if (((SR & PMASK) - (AC & AMMASK)) & AMSIGN)
                            IC += 2;
                    } else
                        IC += 2;
                } else {
                    if ((SR & MSIGN) == 0) {
                        if ((AC & AMMASK) == (SR & PMASK))
                            IC++;
                        else if (((AC & AMMASK) - (SR & PMASK)) & AMSIGN)
                            IC += 2;
                    }
                }
                break;
            case OP_LAS:
                SR = (AC & AMMASK) - SR;
                if (SR == 0)
                    IC++;
                if ((SR & AMSIGN))
                    IC += 2;
                break;
            case OP_ACL:
              ladd:
                SR += (AC & AQMASK);
                if (SR & AQSIGN)
                    SR++;
                AC = (AC & (AMSIGN | AQSIGN)) | (SR & AQMASK);
                break;
            case OP_SBM:
                SR |= MSIGN;
                goto iadd;
            case OP_ADM:
                SR &= PMASK;
                goto iadd;
            case OP_SUB:
                SR ^= MSIGN;
                /* Fall through */

            case OP_ADD:
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
                break;
            case OP_MPY:
            case OP_MPR:
                decr = 043;
                /* Fall through */

            case OP_VLM + 1:
            case OP_VLM:
                shiftcnt = decr;
                if (shiftcnt == 0)
                    break;
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
                if (opcode == OP_MPR && MQ & ONEBIT)
                    AC++;
                if (f & 2)
                    f ^= 1;
                if (f & 1) {
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }
                break;
            case OP_DVH:
            case OP_DVP:
                decr = 043;
                /* Fall through */

            case OP_VDH + 2:
            case OP_VDH:
            case OP_VDP + 2:
            case OP_VDP:
                shiftcnt = decr;
                if (shiftcnt == 0)
                    break;
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
                    if (CPU_MODEL < CPU_7090) {
                        MQ &= PMASK;
                        if (f == 2 || f == 1)
                            MQ |= MSIGN;
                    }
                    if (opcode == OP_DVH || opcode == OP_VDH
                        || opcode == (OP_VDH + 2)) {
                        goto halt;
                    }
                    break;
                }
                /* Clear signs */
                MQ &= PMASK;
                AC &= AMMASK;
                sim_interval = sim_interval - shiftcnt;
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
/* Floating point */
            case OP_USM:
            case OP_FSM:
                SR |= MSIGN;
                goto fpadd;
            case OP_FSB:
            case OP_UFS:
                SR ^= MSIGN;    /* Reverse sign */
                goto fpadd;
            case OP_FAM:
            case OP_UAM:
                SR &= PMASK;    /* Clear SR sign */
            case OP_FAD:
            case OP_UFA:
              fpadd:
                temp = 0;       /* Steal temp for errors */
                MQ = 0;
                f = 0;
                /* Extract AC char */
                shiftcnt = (int)(AC >> 27) & 01777;     /* Include P&Q */
                /* Diff SR char */
                shiftcnt -= (int)((SR >> 27) & 0377);
                if (shiftcnt > 0) {     /* AC Bigger */
                    /* Exchange AC & SR */
                    AC ^= SR;
                    SR ^= AC;
                    AC ^= SR;
                    /* Fix up signs */
                    if (SR & AMSIGN)
                        SR |= MSIGN;
                    AC &= AMMASK;
                    if (AC & APSIGN)
                        AC ^= (AMSIGN | APSIGN);
                } else          /* SR Bigger then AC, AC Smaller */
                    shiftcnt = -shiftcnt;       /* Change sign */
                fptemp = (int)((SR >> 27) & 0377);      /* Get exponent */
                /* Save AC & SR signs */
                if (AC & AMSIGN)
                    f |= 1;
                if (SR & MSIGN)
                    f |= 2;
                /* Clear sign */
                SR &= PMASK;
                AC &= FPMMASK;  /* Clear char and sign */
                shiftcnt &= 0377;
                if (shiftcnt >= 0 && shiftcnt < 077) {
                    sim_interval--;
                    while (shiftcnt > 0) {
                        MQ >>= 1;
                        if (AC & 1)
                            MQ |= FPNBIT;
                        AC >>= 1;
                        shiftcnt--;
                    }
                } else
                    AC = 0;
                sim_interval--;

                /* Do add */
                if (f == 2 || f == 1) {
                    AC -= (SR & FPMMASK);
                    /* If AC < 0 then SR was larger */
                    if (AC & AMSIGN) {
                        AC = ~AC;
                        if ((MQ & FPMMASK) != 0) {
                            MQ ^= FPMMASK;
                            MQ++;
                        } else
                            AC++;
                    } else
                        f ^= 2; /* Change sign of AC */
                } else
                    AC += SR & FPMMASK;

                /* Check for overflow */
                if (AC & FPOBIT) {
                    if (AC & 1)
                        MQ |= FPOBIT;
                    AC >>= 1;
                    MQ >>= 1;
                    /* OV check */
                    if (fptemp == 0377)
                        temp |= FPACERR | FPOVERR;
                    fptemp++;
                }
                /* Are we normalizing */
                if (smode == 0 &&
                    (opcode == OP_FAD || opcode == OP_FSB ||
                     opcode == OP_FAM || opcode == OP_FSM)) {
                    sim_interval--;
                    while ((AC & FPNBIT) == 0 &&
                           ((AC & FPMMASK) != 0 || (MQ & FPMMASK) != 0)) {
                        /* 704 does not check MQ when normalizing */
                        if (CPU_MODEL == CPU_704 && (AC & FPMMASK) == 0)
                            break;
                        MQ <<= 1;
                        AC <<= 1;
                        if (MQ & FPOBIT) {
                            AC |= 1;
                            MQ &= ~FPOBIT;
                        }
                        if (fptemp == 0 && (temp & FPOVERR) == 0)
                            temp |= FPACERR;
                        fptemp--;       /* UF Check */
                    }
                    if (AC == 0 && MQ == 0) {
                        fptemp = 0;
                        f |= f << 1;
                    }
                }

                /* Handle signifigance mode */
                if (smode && MQ & FPNBIT &&
                    (opcode == OP_FAD || opcode == OP_FSB ||
                     opcode == OP_FAM || opcode == OP_FSM)) {
                    sim_interval--;     /* Extra cycle */
                    /* If overflow, normalize */
                    AC++;
                    if (AC & FPOBIT) {
                        AC >>= 1;       /* Move right one bit */
                        /* OV check */
                        if (fptemp == 0377)
                            temp |= FPACERR | FPOVERR;
                        fptemp++;
                    }
                }

                /* Put pieces back together */
                AC &= FPMMASK;
                MQ &= FPMMASK;
                AC |= ((t_uint64) (fptemp & 01777)) << 27;
                if (AC != 0) {
                    if (fptemp < 27 && (temp & FPOVERR) == 0)
                        temp |= FPMQERR;
                    fptemp -= 27;
                    MQ |= ((t_uint64) (fptemp & 0377)) << 27;
                }
                if (f & 2) {
                    AC |= AMSIGN;
                    MQ |= MSIGN;
                }
                if (temp == 0)
                    break;
              dofptrap:
                if (CPU_MODEL != CPU_704 && FTM) {
                    sim_interval = sim_interval - 1;    /* count down */
                    M[0] &= ~(AMASK | DMASK);
                    M[0] |= temp | (IC & memmask);
                    IC = 010;
                } else {
                    if (temp & FPMQERR)
                        mqoflag = 1;
                    if (temp & FPACERR)
                        acoflag = 1;
                }
                break;
            case OP_UFM:
            case OP_FMP:
                AC = 0;
                temp = 0;
                /* Quick out for times 0 */
                if (SR == 0) {
                    MQ &= MSIGN;
                    if (MQ & MSIGN)
                        AC |= AMSIGN;
                    break;
                }
                /* Result sign */
                if ((MQ & MSIGN) != (SR & MSIGN))
                    f = 1;
                else
                    f = 0;
                /* 7090 checks MQ for zero before multipling */
                if (CPU_MODEL == CPU_7090 && (MQ & PMASK) == 0) {
                    if (f)
                        AC |= AMSIGN;
                    break;
                }

                /* Handle signifigance mode */
                if (smode) {
                    /* Find larger operand and move to MQ */
                    if ((MQ & FPMMASK) < (SR & FPMMASK)) {
                        MQ ^= SR;
                        SR ^= MQ;
                        MQ ^= SR;
                    }
                    /* Now normalize number in MQ */
                    fptemp = (int)(MQ >> 27) & 0377;
                    MQ &= FPMMASK;
                    while ((MQ & FPNBIT) == 0 && MQ != 0) {
                        fptemp--;
                        MQ <<= 1;
                    }
                    /* If zero time zero, fix exponent */
                    if (MQ == 0 && (SR & FPMMASK) == 0) {
                        fptemp -= 27;
                        MQ = FPNBIT;
                    }
                } else
                    /* Extract characteristic */
                    fptemp = (int)(MQ >> 27) & 0377;
                fptemp += (int)(SR >> 27) & 0377;
                fptemp -= 128;
                MQ &= FPMMASK;
                SR &= FPMMASK;
                /* Do multiply */
                shiftcnt = 27;
                while (shiftcnt-- > 0) {
                    if (MQ & 1)
                        AC += SR;
                    MQ >>= 1;
                    if (AC & 1)
                        MQ |= FPNBIT;
                    AC >>= 1;
                    AC &= FPMMASK;
                }

                /* Normalize the result */
                if (opcode == OP_FMP) {
                    if ((AC & FPNBIT) == 0) {
                        MQ <<= 1;
                        AC <<= 1;
                        if (MQ & FPOBIT)
                            AC |= 1;
                        MQ &= FPMMASK;
                        fptemp--;
                        if (smode && (AC & FPNBIT) == 0 &&
                            (AC & (FPNBIT >> 1)) == 0) {
                            MQ <<= 1;
                            AC <<= 1;
                            if (MQ & FPOBIT)
                                AC |= 1;
                            MQ &= FPMMASK;
                            fptemp--;
                        }
                    }
                    if (smode && MQ & FPNBIT) {
                        sim_interval--; /* Extra cycle */
                        /* If overflow, normalize */
                        AC++;
                        if (AC & FPOBIT) {
                            AC >>= 1;   /* Move right one bit */
                            fptemp++;
                        }
                    }
                    if (AC == 0)
                        fptemp = 0;
                }

                if (AC != 0 || opcode == OP_UFM || smode) {
                    if (fptemp < 0)
                        temp |= FPACERR;
                    else if (fptemp > 0377)
                        temp |= FPOVERR | FPACERR;
                    AC |= ((t_uint64) (fptemp & 01777)) << 27;
                    fptemp -= 27;
                    if (fptemp < 0)
                        temp |= FPMQERR;
                    else if (fptemp > 0377)
                        temp |= FPOVERR | FPMQERR;
                    MQ |= ((t_uint64) (fptemp & 0377)) << 27;   /* UF Check */
                }
                if (f & 1) {
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }
                if (temp != 0)
                    goto dofptrap;
                break;
            case OP_FDH:
            case OP_FDP:
                /* Sign of SR => MQ */
                if ((SR & MSIGN) != ((AC >> 2) & MSIGN))
                    f = 1;
                else
                    f = 0;
                if (AC & AMSIGN)
                    f |= 2;
                if (CPU_MODEL != CPU_704)
                    MQ = 0;
                shiftcnt = 27;

                /* Handle signifigance mode */
                if (smode) {
                    /* Divide check if 0 divisor */
                    if ((SR & FPMMASK) == 0) {
                        dcheck = 1;
                        if (f & 1)
                            MQ |= MSIGN;
                        if (opcode == OP_FDH)
                            goto halt;
                    }
                    fptemp2 = (int)(AC >> 27) & 0377;
                    AC &= FPMMASK;
                    /* If dividend 0, adjust exponent */
                    if (AC == 0) {
                        while ((SR & FPNBIT) == 0) {
                            SR <<= 1;
                            fptemp2--;
                        }
                        if (fptemp2 < 0)
                            temp |= FPSPERR | FPMQERR;
                        AC = ((t_uint64) (fptemp2 & 01777)) << 27;
                        if (FTM && CPU_MODEL != CPU_704 && fptemp2 < 27)
                            temp |= FPSPERR | FPACERR;
                        fptemp2 -= 27;
                        MQ = ((t_uint64) (fptemp2 & 0377)) << 27;
                        /* Fix signs */
                        if (f & 1)
                            MQ |= MSIGN;
                        if (f & 2)      /* Sign does not change */
                            AC |= AMSIGN;
                        break;
                    }
                    fptemp = (int)(SR >> 27) & 0377;
                    SR &= FPMMASK;
                    /* Normalize dividend if larger fraction */
                    if (AC > (SR & FPMMASK)) {
                        while ((AC & FPOBIT) == 0) {
                            fptemp2--;
                            AC <<= 1;
                        }
                        /* Normalize SR, denomalize AC */
                        /* We checked before for SR==0 */
                        while ((SR & FPOBIT) == 0) {
                            SR <<= 1;
                            fptemp2--;
                            AC >>= 1;
                            fptemp++;
                        }
                    } else if (AC < (SR & FPMMASK)) {
                        /* Normalize SR, denomalize AC */
                        /* We checked before for SR==0 */
                        while ((SR & FPOBIT) == 0) {
                            SR <<= 1;
                            fptemp--;
                            AC >>= 1;
                            fptemp2++;
                        }
                    }
                    if ((SR & (FPOBIT >> 1)) == 0)
                        shiftcnt--;
                    goto fpdivide;
                }

                /* Begin common FDP/FDH code */
                temp = (AC & FPMMASK) - ((SR & FPMMASK) << 1);
                if ((temp & AMSIGN) == 0 || (SR & FPMMASK) == 0) {
                    dcheck = 1;
                    if (f & 1)
                        MQ |= MSIGN;
                    if (opcode == OP_FDH)
                        goto halt;
                    break;
                }
                temp = 0;
                /* Check for divide by 0 */
                if ((AC & FPMMASK) == 0) {
                    AC = 0;
                    if (CPU_MODEL != CPU_704)
                        f &= 1;
                } else {
                    /* Split appart fraction and charateristics */
                    fptemp2 = (int)(AC >> 27) & 0377;
                    fptemp = (int)(SR >> 27) & 0377;
                    AC &= FPMMASK;
                    SR &= FPMMASK;
                  fpdivide:
                    /* Precheck SR less then AC */
                    if (((AC - SR) & AMSIGN) == 0) {
                        if (AC & 1)
                            MQ |= FPNBIT;
                        AC >>= 1;
                        fptemp2++;
                    }
                    /* Do actual divide */
                    do {
                        AC <<= 1;
                        MQ <<= 1;
                        if (MQ & FPOBIT) {
                            MQ &= ~FPOBIT;
                            AC |= 1;
                        }
                        if (SR <= AC) {
                            AC -= SR;
                            MQ |= 1;
                        }
                    } while (--shiftcnt != 0);

                    /* Compute new characteristic */
                    AC &= FPMMASK;
                    fptemp = (fptemp2 - fptemp) + 128;  /* UF check */
                    if (fptemp > 0377)
                        temp |= FPSPERR | FPOVERR | FPMQERR;
                    else if (fptemp < 0)
                        temp |= FPSPERR | FPMQERR;
                    MQ |= ((t_uint64) (fptemp & 0377)) << 27;
                    if (FTM && CPU_MODEL != CPU_704 && fptemp2 < 27)
                        temp |= FPSPERR | FPACERR;
                    fptemp2 -= 27;
                    AC |= ((t_uint64) (fptemp2 & 01777)) << 27; /* UF check */
                }
                /* Fix signs on results */
                if (f & 1)
                    MQ |= MSIGN;
                if (f & 2)      /* Sign does not change */
                    AC |= AMSIGN;
                if (temp != 0)
                    goto dofptrap;
                if (smode) {
                    sim_interval = sim_interval - 1;    /* count down */
                    M[0] &= ~(AMASK | DMASK);
                    M[0] |= (IC & memmask);
                    IC = 011;
                }
                break;
#ifdef I7090    /* Not on 704 */
/* Double precision floating point */
            case OP_DFSM:
            case OP_DUSM:
                SR |= MSIGN;
                goto dfpadd;
            case OP_DFSB:
            case OP_DUFS:
                SR ^= MSIGN;    /* Reverse sign */
                goto dfpadd;
            case OP_DFAM:
            case OP_DUAM:
                SR &= PMASK;    /* Clear SR sign */
            case OP_DFAD:
            case OP_DUFA:
              dfpadd:
                temp = 0;       /* Steal temp for errors */
                if (MA & 1 && FTM) {
                    temp = FPDPERR;
                    goto dofptrap;
                }

                shiftcnt = (int)(AC >> 27) & 01777;     /* Include P&Q */
                shiftcnt -= (int)(SR >> 27) & 0377;

                f = 0;
                /* Save AC & ID signs */
                if (AC & AMSIGN)
                    f |= 1;
                if (SR & MSIGN)
                    f |= 2;
                MA |= 1;        /* Point to second word */
                if (shiftcnt > 0) {     /* AC Bigger */
                    fptemp = (int)(AC >> 27) & 0377;
                    if (shiftcnt <= 0100) {
                        ID = AC;
                        if (f & 1)      /* Copy sign */
                            ID |= MSIGN;
                    }
                    f = (f >> 1) | ((1 & f) << 1);      /* Exchange signs */
                    if (shiftcnt > 077) {
                        if ((AC & FPNBIT) == 0)
                            ID = AC;
                        goto dpfnorm;
                    } else {
                        t_uint64        t;
                        AC &= ~FPMMASK;
                        AC |= SR & FPMMASK;
                        SR &= ~FPMMASK;
                        SR |= MQ & FPMMASK;
                        MQ &= ~FPMMASK;
                        ReadMem(0, t);
                        MQ |= t & FPMMASK;
                    }
                    /* AC=c, MQ=d SR=b, ID=a */
                } else {        /* ID Bigger then AC, AC Smaller */
                    t_uint64    t;
                    shiftcnt = -shiftcnt;       /* Change sign */
                    fptemp = (int)(SR >> 27) & 0377;
                    if (shiftcnt > 077) {
                        if (SR & FPNBIT) {
                            /* Early exit */
                            AC = SR;
                            fptemp = (int)(AC >> 27) & 0377;
                            ID = (SR & (~FPMMASK)) | (MQ & FPMMASK);
                            ReadMem(0, MQ);
                            goto dpdone;
                        }
                        MQ &= ~FPMMASK;
                        AC &= ~FPMMASK;
                    }
                    ID = SR;
                    SR &= ~FPMMASK;
                    ReadMem(0, t);
                    SR |= t & FPMMASK;
                }
                /* Clear sign */
                AC &= FPMMASK;  /* Clear char and sign */
                MQ &= FPMMASK;
                shiftcnt &= 0377;
                if (shiftcnt >= 0 && shiftcnt < 0177) {
                    sim_interval--;
                    while (shiftcnt > 0) {
                        MQ >>= 1;
                        if (AC & 1)
                            MQ |= FPNBIT;
                        AC >>= 1;
                        shiftcnt--;
                    }
                } else {
                    AC = 0;
                    MQ = 0;
                }
                sim_interval--;

                /* Do add */
                if (f == 2 || f == 1) {
                    /* Ones compliment AC/MQ */
                    MQ ^= FPMMASK;
                    AC ^= FPMMASK;
                    /* Form 2's compliment */
                    MQ++;
                    if (MQ & FPOBIT) {
                        AC++;
                        MQ ^= FPOBIT;
                    }
                    /* Subract ID/SR */
                    MQ += (SR & FPMMASK);
                    if (MQ & FPOBIT) {
                        AC++;
                        MQ ^= FPOBIT;
                    }
                    AC += (ID & FPMMASK);
                    /* If AC,MQ < 0 then ID,SR was larger */
                    if (AC & FPOBIT)
                        AC ^= FPOBIT;
                    else {
                        f ^= 2; /* Change sign of AC */
                        MQ ^= FPMMASK;
                        AC ^= FPMMASK;
                        MQ++;
                        if (MQ & FPOBIT) {
                            AC++;
                            MQ ^= FPOBIT;
                        }
                    }
                } else {
                    MQ += SR & FPMMASK;
                    /* Propegate carry */
                    if (MQ & FPOBIT) {
                        AC++;
                        MQ ^= FPOBIT;
                    }
                    AC += ID & FPMMASK;
                }

                /* Check for overflow */
                if (AC & FPOBIT) {
                    if (AC & 1)
                        MQ |= FPOBIT;
                    AC >>= 1;
                    MQ >>= 1;
                    /* OV check */
                    if (fptemp == 0377)
                        temp |= FPACERR | FPOVERR;
                    fptemp++;
                }

              dpfnorm:
                /* Are we normalizing */
                if (opcode == OP_DFAD || opcode == OP_DFSB ||
                    opcode == OP_DFAM || opcode == OP_DFSM) {
                    sim_interval--;
                    /* Preshift before we normalize */
                    if ((AC & FPMMASK) == 0 && (MQ & FPMMASK) != 0) {
                        AC |= MQ & FPMMASK;
                        MQ &= ~FPMMASK;
                        if (fptemp < 27)
                            temp |= FPACERR;
                        fptemp -= 27;
                    }
                    while ((AC & FPNBIT) == 0 && (AC & FPMMASK) != 0) {
                        MQ <<= 1;
                        AC <<= 1;
                        if (MQ & FPOBIT) {
                            AC |= 1;
                            MQ &= ~FPOBIT;
                        }
                        if (fptemp == 0 && (temp & FPOVERR) == 0)
                            temp |= FPACERR;
                        fptemp--;       /* UF Check */
                    }
                    if (AC == 0 && MQ == 0) {
                        fptemp = 0;
                        f |= f << 1;
                    }
                }
              dpdone:
                /* Put pieces back together */
                AC &= FPMMASK;
                MQ &= FPMMASK;
                AC |= ((t_uint64) (fptemp & 01777)) << 27;
                if (AC != 0) {
                    if (fptemp < 27 && (temp & FPOVERR) == 0)
                        temp |= FPMQERR;
                    fptemp -= 27;
                    MQ |= ((t_uint64) (fptemp & 0377)) << 27;
                }
                if (f & 2)
                    AC |= AMSIGN;
                if (f & 2)
                    MQ |= MSIGN;
                if (temp != 0)
                    goto dofptrap;
                break;
            case OP_DFMP:
            case OP_DUFM:
                temp = 0;
                if (MA & 1) {
                    temp |= FPDPERR;
                    if (FTM)
                        goto dofptrap;
                }
                /* Quick out for zero result. */
                fptemp = (int)(SR >> 27) & 0377;
                if ((SR & PMASK) == 0) {
                    AC = MQ = 0;
                    break;
                }

                /* Compute exponent */
                fptemp += (int)(AC >> 27) & 0377;
                fptemp -= 128;

                /* Figure out sign */
                if (((AC & AMSIGN) != 0) != (0 != (SR & MSIGN)))
                    f = 1;
                else
                    f = 0;

                /* Prepare for first multiply */
                MQ &= FPMMASK;  /* B */
                ID = AC & FPMMASK;      /* A */
                if (AC == 0 && MQ == 0) {
                    ID = SR & (MSIGN | FPCMASK);
                    AC = (f) ? AMSIGN : 0;
                    MQ = (f) ? MSIGN : 0;
                    if (temp != 0)
                        goto dofptrap;
                    break;
                }

                AC = 0;
                /* First multiply B * C */
                if ((SR & FPMMASK) != 0 && MQ != 0) {
                    SR &= FPMMASK;
                    shiftcnt = 27;
                    while (shiftcnt-- > 0) {
                        if (MQ & 1)
                            AC += SR;
                        MQ >>= 1;
                        if (AC & 1)
                            MQ |= FPNBIT;
                        AC >>= 1;
                        AC &= FPMMASK;
                    }
                }

                /* Adjust registers for second multiply */
                /* ID:A=xxx-0, MQ:b =0 SR:c = xx:xx d=xx:xxx */
                ID ^= SR;       /* A, C */
                SR ^= ID;
                ID ^= SR;       /* C, A */
                MA |= 1;
                ReadMem(0, MQ); /* D */
                if (MQ == 0 || (SR & FPMMASK) == 0) {
                    /* Early out two. */
                    if ((SR & FPMMASK) == 0 && opcode == OP_DFMP) {
                        AC = (f) ? AMSIGN : 0;
                        MQ = (f) ? MSIGN : 0;
                        if (temp != 0)
                            goto dofptrap;
                        break;
                    }
                    MQ = SR;    /* A */
                    SR = ID;    /* C */
                    /* Early out three. */
                    if ((SR & FPMMASK) == 0 && opcode == OP_DFMP) {
                        AC = (f) ? AMSIGN : 0;
                        MQ = (f) ? MSIGN : 0;
                        ID &= FPMMASK;
                        if (temp != 0)
                            goto dofptrap;
                        break;
                    }
                    ID &= ~FPMMASK;
                    ID |= FPMMASK & AC; /* BC */
                } else {
                    ibr = AC & FPMMASK; /* BC */
                    MQ &= FPMMASK;
                    AC = 0;
                    /* Second multiply A * D */
                    shiftcnt = 27;
                    while (shiftcnt-- > 0) {
                        if (MQ & 1)
                            AC += SR;
                        MQ >>= 1;
                        if (AC & 1)
                            MQ |= FPNBIT;
                        AC >>= 1;
                        AC &= FPMMASK;
                    }
                    MQ = SR;    /* A */
                    SR = ID;    /* C */
                    ID = FPMMASK & ibr; /* BC */
                    AC += ibr;  /* AD + BC */
                }
                SR &= FPMMASK;

                /* AC:A=220-77, MQ:b =0 SR:c = 220-77 d=0 mq = 7601 */
                /* third multiply high * high */
                if (MQ == 0 || SR == 0) {
                    /* MQ == A, AC = AD + BC, ID = BC, SR = C */
                    MQ = AC;
                    AC = 0;
                    if (opcode == OP_DFMP && SR == 0)
                        ID &= FPMMASK;
                } else {
                    MQ &= FPMMASK;      /* Just to be sure */
                    ID &= FPMMASK;      /* Clear char */
                    shiftcnt = 27;
                    while (shiftcnt-- > 0) {
                        if (MQ & 1)
                            AC += SR;
                        MQ >>= 1;
                        if (AC & 1)
                            MQ |= FPNBIT;
                        AC >>= 1;
                        AC &= FPMMASK;
                    }
                }

                /* Normalize if DFMP */
                if (opcode == OP_DFMP) {
                    /* Check for true zero result */
                    if (MQ == 0 && AC == 0) {
                        fptemp = 0;
                    } else if ((AC & FPNBIT) == 0 && (AC & FPMMASK) != 0) {
                        MQ <<= 1;
                        AC <<= 1;
                        if (MQ & FPOBIT)
                            AC |= 1;
                        MQ &= FPMMASK;
                        fptemp--;       /* UF check */
                    }
                }

                /* Fix exponents and check for over/underflow */
                if (fptemp != 0) {
                    if (fptemp < 0)
                        temp |= FPACERR | FPMQERR;
                    else if (fptemp < 27)
                        temp |= FPMQERR;
                    else if (fptemp > 0377)
                        temp |= FPOVERR | FPACERR;
                    AC |= ((t_uint64) (fptemp & 01777)) << 27;
                    fptemp -= 27;
                    if (fptemp > 0377)
                        temp |= FPOVERR | FPMQERR;
                    MQ |= ((t_uint64) (fptemp & 0377)) << 27;   /* UF Check */
                }

                /* Restore signs */
                if (f) {
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }

                /* Handle trapping */
                if (temp != 0)
                    goto dofptrap;
                break;

            case OP_DFDH:
            case OP_DFDP:
                if (MA & 1) {
                    temp = FPDPERR;
                    if (FTM)
                        goto dofptrap;
                }
                /* Sign of SR => MQ */
                temp = (AC & FPMMASK) - ((SR & FPMMASK) << 1);
                if ((temp & AMSIGN) == 0 || (SR & FPMMASK) == 0) {
                    dcheck = 1;
                    if (opcode == OP_DFDH)
                        goto halt;
                    break;
                }

                /* Result sign */
                if (((AC & AMSIGN) != 0) != (0 != (SR & MSIGN)))
                    f = 1;
                else
                    f = 0;
                if (AC & AMSIGN)        /* Sign A+B */
                    f |= 2;
                if (SR & MSIGN) /* Sign C+D */
                    f |= 4;

                /* Check for divide by 0 */
                if ((MQ & FPMMASK) == 0 && (AC & FPMMASK) == 0) {
                    /* Divide check by 0 */
                    ID = MQ = (f & 1) ? MSIGN : 0;
                    AC = (f & 1) ? AMSIGN : 0;
                    break;
                }
                /* Split appart fraction and charateristics */
                fptemp2 = (int)(AC >> 27) & 01777;
                fptemp = (int)(SR >> 27) & 0377;
                fptemp = fptemp2 - fptemp;
                fptemp += 0200;

                ID = SR & FPMMASK;      /* ID = C */
                AC &= FPMMASK;  /* A */
                MQ &= FPMMASK;  /* B */
                SR &= FPMMASK;  /* C */
                MA |= 1;
                ReadMem(0, ibr);
                ibr &= FPMMASK; /* D */

                /* Precheck SR less then AC */
                if (((AC - SR) & AMSIGN) == 0) {
                    if (AC & 1)
                        MQ |= FPOBIT;
                    MQ >>= 1;
                    AC >>= 1;
                    f |= 16;    /* Q>1 trigger */
                }

                /* Divide AB / C => AC=R, MQ=Q1 */
                shiftcnt = 27;
                do {
                    AC <<= 1;
                    MQ <<= 1;
                    if (MQ & FPOBIT) {
                        MQ &= ~FPOBIT;
                        AC |= 1;
                    }
                    if (SR <= AC) {
                        AC -= SR;
                        MQ |= 1;
                    }
                } while (--shiftcnt != 0);
                /* ID=xxx.0, SR=C, ibr=D, AC=R1, MQ=Q1 */
                /* Set up for multiply */
                SR = MQ;        /* SR <- Q1' */
                MQ = ibr;       /* MQ <- D */
                ibr = AC;       /* ibr <- R1 */
                AC = 0;
                /* ID=xxx.C, SR=Q1, ibr=R1, AC=0, MQ=D */
                /* Multiply Q1*D => AC,MQ */
                shiftcnt = 27;
                while (shiftcnt-- > 0) {
                    if (MQ & 1)
                        AC += SR;
                    MQ >>= 1;
                    if (AC & 1)
                        MQ |= FPNBIT;
                    AC >>= 1;
                    AC &= FPMMASK;
                }
                /* ID=C.C, SR=Q1, ibr=R1, AC=Q1Dh, MQ=Q1Dl */
                /* AC <- R1 - Q1D */
                if (ibr < AC) {
                    AC = AC - ibr;
                    f |= 8;     /* Sign R1 - Q1D */
                } else
                    AC = ibr - AC;
                MQ = 0;         /* MQ <- 0 */
                ID ^= SR;       /* SR<>ID Q1<>C */
                SR ^= ID;
                ID ^= SR;

                /* Divide R1 - Q1D / C => AC=R, MQ=Q */
                if (f & 16)
                    fptemp++;
                /* Adjust ID register to correct exponent */
                ID |= ((t_uint64) (fptemp & 0377)) << 27;
                if (f & 1)
                    ID |= MSIGN;
                /* Check before final divide */
                temp = AC - (SR << 1);
                if ((temp & AMSIGN) == 0 || SR == 0) {
                    if ((f & 0xa) == 2 || (f & 0xa) == 8) {
                        MQ |= MSIGN;
                        AC |= AMSIGN;
                    }
                    dcheck = 1;
                    if (opcode == OP_DFDH)
                        goto halt;
                    break;
                }

                /* Check Quotient > 1 */
                if (((AC - SR) & AMSIGN) == 0) {
                    if (AC & 1)
                        MQ |= FPNBIT;
                    MQ >>= 1;
                    AC >>= 1;
                    f |= 32;    /* Q2>1 trigger */
                }

                /* Actual divide (R1-Q1D/C) => AC=R2, MQ=Q2 */
                shiftcnt = 27;
                do {
                    AC <<= 1;
                    MQ <<= 1;
                    if (MQ & FPOBIT) {
                        MQ &= ~FPOBIT;
                        AC |= 1;
                    }
                    if (SR <= AC) {
                        AC -= SR;
                        MQ |= 1;
                    }
                } while (--shiftcnt != 0);
                /* ID=Q1, SR=C, ibr=R1, AC=R1-Q1D/C, MQ=Q2 */
                AC = 0;
                if (f & 32) {
                    MQ <<= 1;
                    if (MQ & FPOBIT) {
                        AC |= 1;
                        MQ ^= FPOBIT;
                    }
                }

                /* MQ = Q2, ID=Q1, SR=C, ibr=R1, AC=xxx */
                /* Sign Q2 = sign C+D, sign Q1 = sign R1-Q1D */
                temp = (MA & 1) ? FPDPERR : 0;  /* Restore errors */

                SR = ID & FPMMASK;      /* SR <- Q1 */
                if ((f & 8)) {
                    AC = SR - AC;
                    MQ ^= FPMMASK;
                    MQ++;
                    if (MQ & FPOBIT) {
                        MQ &= FPMMASK;
                    } else {
                        AC--;
                    }
                } else {
                    AC += SR;
                }
                /* Check for overflow */
                if (AC & FPOBIT) {
                    if (AC & 1)
                        MQ |= FPOBIT;
                    AC >>= 1;
                    MQ >>= 1;
                    /* OV check */
                    if (fptemp == 0377)
                        temp |= FPACERR | FPOVERR;
                    fptemp++;
                }

                /* Normalize results */
                while ((AC & FPNBIT) == 0 &&
                       ((AC & FPMMASK) != 0 || (MQ & FPMMASK) != 0)) {
                    MQ <<= 1;
                    AC <<= 1;
                    if (MQ & FPOBIT) {
                        AC |= 1;
                        MQ ^= FPOBIT;
                    }
                    if (fptemp == 0 && (temp & FPOVERR) == 0)
                        temp |= FPACERR;
                    fptemp--;   /* UF Check */
                }
                MQ &= FPMMASK;
                if (AC == 0 && MQ == 0)
                    fptemp = 0;
                /* Compute new characteristic */
                if (fptemp > 0377)
                    temp |= FPOVERR | FPACERR;
                else if (fptemp < 0)
                    temp |= FPACERR | FPMQERR;
                else if (fptemp < 27)
                    temp |= FPMQERR;
                AC |= ((t_uint64) (fptemp & 01777)) << 27;
                fptemp -= 27;
                if (fptemp > 0377)
                    temp |= FPOVERR | FPMQERR;
                MQ |= ((t_uint64) (fptemp & 0377)) << 27;
                /* Fix signs on results */
                if (f & 1) {    /* Sign does not change */
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }
                if (temp != 0)
                    goto dofptrap;
                break;
            case OP_DLD:
                AC = ((SR & MSIGN) << 2) | (SR & PMASK);
                f = MA & 1;
                MA |=  1;
                ReadMem(0, MQ);
                if (f) {
                    temp = FPDPERR;
                    if (FTM) {
                        goto dofptrap;
                    }
                }
                break;
            case OP_DST:
                SR = (AC & (APSIGN - 1));
                if (AC & AMSIGN)
                    SR |= MSIGN;
                WriteMem();
                MA = (MA + 1);
                SR = MQ;
                break;
#endif

/* Logic operations */
            case OP_ORA:
                AC |= SR & AQMASK;
                break;
            case OP_ORS:
                SR |= AC;
                SR &= AQMASK;
                break;
            case OP_ANA:
                AC &= SR;
                AC &= AQMASK;
                break;
            case OP_ANS:
                SR &= AC;
                SR &= AQMASK;
                break;
            case OP_ERA:
                AC ^= SR;
                AC &= AQMASK;   /* Clear S & Q  */
                break;

#ifdef I7090    /* Not on 704 */
/* Conversion */
            case OP_CVR + 3:
            case OP_CVR + 2:
            case OP_CVR + 1:
            case OP_CVR:
                shiftcnt = (int)(SR >> 18L) & 0377;
                if (AC & AMSIGN) {
                    f = 1;
                    AC &= AMMASK;
                } else
                    f = 0;
                while (shiftcnt != 0) {
                    MA += (uint16)(AC & 077);
                    ReadMem(0, SR);
                    MA = (uint16)(AMASK & SR);
                    AC >>= 6;
                    AC |= SR & (077LL << 30);
                    shiftcnt--;
                }
                /* Save XR if tag set */
                if (tag & 1)
                    XR[1] = (uint16)(MA & AMASK);
                /* restore sign */
                if (f)
                    AC |= AMSIGN;
                break;

            case OP_CAQ + 3:
            case OP_CAQ + 2:
            case OP_CAQ + 1:
            case OP_CAQ:
                shiftcnt = (int)(SR >> 18L) & 0377;
                while (shiftcnt != 0) {
                    MA += (uint16)(MQ >> 30) & 077;
                    ReadMem(0, SR);
                    MA = (uint16)(AMASK & SR);
                    MQ <<= 6;
                    MQ |= (MQ >> 36) & 077;
                    MQ &= WMASK;
                    AC += SR;
                    AC &= AMMASK;
                    shiftcnt--;
                }
                if (tag & 1)
                    XR[1] = (uint16)(MA & AMASK);
                break;

            case OP_CRQ + 3:
            case OP_CRQ + 2:
            case OP_CRQ + 1:
            case OP_CRQ:
                shiftcnt = (int)(SR >> 18L) & 0377;
                while (shiftcnt != 0) {
                    MA += (uint16)(MQ >> 30) & 077;
                    ReadMem(0, SR);
                    MA = (uint16)(AMASK & SR);
                    MQ <<= 6;
                    MQ &= (WMASK ^ 077);
                    MQ |= (SR >> 30) & 077;
                    shiftcnt--;
                }
                if (tag & 1)
                    XR[1] = (uint16)(MA & AMASK);
                break;
#endif

/* Shift */
            case OP_LLS:
                shiftcnt = MA & 0377;
                sim_interval = sim_interval - (shiftcnt >> 6);
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
            case OP_LRS:
                shiftcnt = MA & 0377;
                sim_interval = sim_interval - (shiftcnt >> 6);
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
            case OP_ALS:
                shiftcnt = MA & 0377;
                sim_interval = sim_interval - (shiftcnt >> 6);
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
            case OP_ARS:
                shiftcnt = MA & 0377;
                sim_interval = sim_interval - (shiftcnt >> 6);
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

            case OP_LGL:
                shiftcnt = MA & 0377;
                sim_interval = sim_interval - (shiftcnt >> 6);
                /* Save sign */
                if (AC & AMSIGN)
                    f = 1;
                else
                    f = 0;
                /* Clear it for now */
                AC &= AMMASK;
                while (shiftcnt-- > 0) {
                    AC <<= 1;
                    if (MQ & MSIGN)
                        AC |= 1;
                    MQ <<= 1;
                    if (AC & APSIGN)
                        acoflag = 1;
                }
                /* Restore sign when done */
                AC &= AMMASK;
                MQ &= WMASK;
                if (f)
                    AC |= AMSIGN;
                break;
            case OP_LGR:
                shiftcnt = MA & 0377;
                sim_interval = sim_interval - (shiftcnt >> 6);
                /* Save sign */
                if (AC & AMSIGN)
                    f = 1;
                else
                    f = 0;
                /* Clear it for now */
                AC &= AMMASK;
                while (shiftcnt-- > 0) {
                    MQ >>= 1;
                    if (AC & 1)
                        MQ |= MSIGN;
                    AC >>= 1;
                }
                /* Restore sign when done */
                AC &= AMMASK;
                if (f)
                    AC |= AMSIGN;
                break;
            case OP_RQL:
                shiftcnt = MA & 0377;
                sim_interval = sim_interval - (shiftcnt >> 6);
                while (shiftcnt-- > 0) {
                    MQ <<= 1;
                    if (MQ & AQSIGN)
                        MQ |= 1;
                    MQ &= WMASK;
                }
                break;

/* 704 Input output Instructions */
            case OP_LDA:
                if (chan_select(0)) {
                    extern DEVICE drm_dev;
                    drum_addr = (uint32)(SR);
                    sim_debug(DEBUG_DETAIL, &drm_dev,
                                 "set address %06o\n", drum_addr);
                    chan_clear(0, DEV_FULL);    /* In case we read something
                                                   before we got here */
                } else
                    iocheck = 1;
                break;
            case OP_CPY:
            case OP_CAD:
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
                        if (opcode == OP_CAD)
                            goto ladd;
                        break;
                    case DEV_FULL:
                        SR = MQ;
                        WriteP(MA, MQ);
                        bcnt[0] = 6;
                        chan_clear(0, DEV_FULL);
                        if (opcode == OP_CAD)
                            goto ladd;
                        break;
                    }
                } else {
                    /* If channel not active, turn on io-check */
                    if (chan_test(0, STA_ACTIVE) == 0) {
                        iocheck = 1;
                        break;
                    }

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

#ifdef I7090    /* Not on 704 */
/* Input/Output Instuctions */
            case OP_ENB:
                ioflags = SR;
                if (SR)
                   itrap = 1;
                else
                   itrap = 0;
                sim_debug(DEBUG_TRAP, &cpu_dev, "ENB %012llo\n", ioflags);
                ihold = 1;
                /*
                 * IBSYS can't have an trap right after ENB or it will hang
                 * on a TTR * in IBNUC.
                 */
                if (CPU_MODEL >= CPU_7090)
                    break;
                temp = 00000001000001LL;

                for (shiftcnt = 1; shiftcnt < NUM_CHAN; shiftcnt++) {
                    if ((temp & ioflags & DMASK) == 0)
                        chan_clear(shiftcnt, CHS_ERR);
                    else if (chan_test(shiftcnt,CHS_ERR))
                        ihold = 0;
                    if ((temp & ioflags & AMASK) == 0)
                        chan_clear(shiftcnt, CHS_EOF);
                    else if (chan_test(shiftcnt,CHS_EOF))
                        ihold = 0;
                    temp <<= 1;
                }
                break;
#endif

            case OP_RDS:                /* Read select */
                opcode = IO_RDS;
                goto docmd;
            case OP_WRS:                /* Write select */
                opcode = IO_WRS;
                goto docmd;
            case OP_WEF:                /* Write EOF */
                opcode = IO_WEF;
                goto docmd;
            case OP_BSR:                /* Backspace */
                opcode = IO_BSR;
                goto docmd;
            case OP_BSF:                /* Backspace File */
                opcode = IO_BSF;
                goto docmd;
            case OP_REW:                /* Rewind */
                opcode = IO_REW;
                goto docmd;
            case OP_RUN:                /* Rewind unload */
                opcode = IO_RUN;
                goto docmd;
            case OP_SDN:                /* Set density */
                opcode = (MA & 020) ? IO_SDH: IO_SDL;
                goto docmd;
            case OP_DRS:                /* Drop ready status */
                opcode = IO_DRS;
        docmd:
                switch (chan_cmd(MA, opcode)) {
                case SCPE_BUSY:
                    iowait = 1; /* Channel is active, hold */
                    break;
                case SCPE_OK:
                    if (((MA >> 9) & 017) == 0) {
                        if (opcode==IO_RDS)
                            MQ = 0;
                        chan_clear(0, CHS_EOF|CHS_EOT|DEV_REOR);
                    }
                    ihold = 1;  /* Hold interupts for one cycle */
                    iotraps &= ~(1 << ((MA >> 9) & 017));
                    break;
                case SCPE_IOERR:
                    iocheck = 1;
                    break;
                case SCPE_NODEV:
                    reason = STOP_IOCHECK;
                    break;
                }
                break;

            case OP_TRS:                /* Test ready status */
                switch (chan_cmd(MA, IO_TRS)) {
                case SCPE_BUSY:
                    iowait = 1; /* Channel is active, hold */
                    break;
                case SCPE_OK:   /* Ready, skip one */
                    IC++;
                    ihold = 2;  /* Hold interupts for two */
                case SCPE_IOERR:        /* Not ready, just return */
                    break;
                case SCPE_NODEV:
                    reason = STOP_IOCHECK;
                    break;
                }
                break;

#ifdef I7090    /* Not on 704 */
            case OP_TRCA:               /* Transfer on Redundancy check */
                ihold = 2;
                if ((1LL << 18) & ioflags)
                    break;
                f = chan_stat(1, CHS_ERR);
                goto branch;
            case OP_TRCB:               /* Transfer on Redundancy check */
                if ((1LL << 19) & ioflags)
                    break;
                f = chan_stat(2, CHS_ERR);
                goto branch;
            case OP_TRCC:
                if ((1LL << 20) & ioflags)
                    break;
                f = chan_stat(3, CHS_ERR);
                goto branch;
            case OP_TRCD:
                if ((1LL << 21) & ioflags)
                    break;
                f = chan_stat(4, CHS_ERR);
                goto branch;
            case OP_TRCE:
                if ((1LL << 22) & ioflags)
                    break;
                f = chan_stat(5, CHS_ERR);
                goto branch;
            case OP_TRCF:
                if ((1LL << 23) & ioflags)
                    break;
                f = chan_stat(6, CHS_ERR);
                goto branch;
            case OP_TRCG:
                if ((1LL << 24) & ioflags)
                    break;
                f = chan_stat(7, CHS_ERR);
                goto branch;
            case OP_TRCH:
                if ((1LL << 25) & ioflags)
                    break;
                f = chan_stat(8, CHS_ERR);
                goto branch;
#endif

            case OP_TEFA:               /* Transfer on channel EOF */
                ihold = 2;
                if ((1LL << 0) & ioflags)
                    break;
                f = chan_stat(1, CHS_EOF);
                goto branch;
#ifdef I7090    /* Not on 704 */
            case OP_TEFB:               /* Transfer on EOF */
                if ((1LL << 1) & ioflags)
                    break;
                f = chan_stat(2, CHS_EOF);
                goto branch;
            case OP_TEFC:
                if ((1LL << 2) & ioflags)
                    break;
                f = chan_stat(3, CHS_EOF);
                goto branch;
            case OP_TEFD:
                if ((1LL << 3) & ioflags)
                    break;
                f = chan_stat(4, CHS_EOF);
                goto branch;
            case OP_TEFE:
                if ((1LL << 4) & ioflags)
                    break;
                f = chan_stat(5, CHS_EOF);
                goto branch;
            case OP_TEFF:
                if ((1LL << 5) & ioflags)
                    break;
                f = chan_stat(6, CHS_EOF);
                goto branch;
            case OP_TEFG:
                if ((1LL << 6) & ioflags)
                    break;
                f = chan_stat(7, CHS_EOF);
                goto branch;
            case OP_TEFH:
                if ((1LL << 7) & ioflags)
                    break;
                f = chan_stat(8, CHS_EOF);
                goto branch;
            case OP_TCOA:       /* Transfer if channel in operation */
            case OP_TCOB:
            case OP_TCOC:
            case OP_TCOD:
            case OP_TCOE:
            case OP_TCOF:
            case OP_TCOG:
            case OP_TCOH:
                f = chan_active((opcode & 017) + 1);
                /* Check if TCOx * */
                if ((cpu_unit.flags & UNIT_FASTIO) && f && MA == (IC - 1))
                    iowait = 1;
                goto branch;
            case OP_TCNA:       /* Transfer on channel not in operation */
            case OP_TCNB:
            case OP_TCNC:
            case OP_TCND:
            case OP_TCNE:
            case OP_TCNF:
            case OP_TCNG:
            case OP_TCNH:
                f = !chan_active((opcode & 017) + 1);
                goto branch;

            case OP_RSCA:               /* Reset and load channel */
                f = 1;
                goto chanrst;
            case OP_RSCB:             /* Reset and load channel */
                f = 2;
                goto chanrst;
            case OP_RSCC:
                f = 3;
                goto chanrst;
            case OP_RSCD:
                f = 4;
                goto chanrst;
            case OP_RSCE:
                f = 5;
                goto chanrst;
            case OP_RSCF:
                f = 6;
                goto chanrst;
            case OP_RSCG:
                f = 7;
                goto chanrst;
            case OP_RSCH:
                f = 8;
              chanrst:
                /* 7607 channel, start imedately */
                /* 7909 channel, wait until channel not active */
                if (bcore & 1)
                    MA |= CORE_B;
                switch (chan_start(f, MA)) {
                case SCPE_IOERR:
                    iocheck = 1;
                    break;
                case SCPE_BUSY:
                    iowait = 1;
                    break;
                case SCPE_OK:
                    ihold = 1;
                    break;
                }
                break;
            case OP_STCA:               /* Load channel, 7909 Start NEA */
                f = 1;
                goto chanst;
            case OP_STCB:
                f = 2;
                goto chanst;
            case OP_STCC:
                f = 3;
                goto chanst;
            case OP_STCD:
                f = 4;
                goto chanst;
            case OP_STCE:
                f = 5;
                goto chanst;
            case OP_STCF:
                f = 6;
                goto chanst;
            case OP_STCG:
                f = 7;
                goto chanst;
            case OP_STCH:
                f = 8;
              chanst:
                /* 7907 channel, set new address */
                /* 7909 channel, wait until channel idle, and start */
                if (bcore & 1)
                    MA |= CORE_B;
                switch (chan_load(f, MA)) {
                case SCPE_IOERR:
                    iocheck = 1;
                    break;
                case SCPE_BUSY:
                    iowait = 1;
                case SCPE_OK:
                    break;
                }
                break;
            case OP_SCHA:               /* Store data channel */
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(1, MA);
                break;
            case OP_SCHB:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(2, MA);
                break;
            case OP_SCHC:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(3, MA);
                break;
            case OP_SCHD:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(4, MA);
                break;
            case OP_SCHE:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(5, MA);
                break;
            case OP_SCHF:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(6, MA);
                break;
            case OP_SCHG:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(7, MA);
                break;
            case OP_SCHH:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store(8, MA);
                break;
            case OP_SCDA:               /* Store channel diags 7909 */
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(1, MA);
                break;
            case OP_SCDB:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(2, MA);
                break;
            case OP_SCDC:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(3, MA);
                break;
            case OP_SCDD:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(4, MA);
                break;
            case OP_SCDE:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(5, MA);
                break;
            case OP_SCDF:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(6, MA);
                break;
            case OP_SCDG:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(7, MA);
                break;
            case OP_SCDH:
                if (bcore & 1)
                    MA |= CORE_B;
                chan_store_diag(8, MA);
                break;

/* Optional RPQ instructions */
/* Extended precision floating point */
            case OP_ESB:
                SR ^= MSIGN;    /* Reverse sign */
            case OP_EAD:
            case OP_EUA:
                if ((cpu_unit.flags & OPTION_EFP) == 0)
                    break;
                temp = 0;       /* Steal temp for errors */
                f = 0;
                /* Extract AC char */
                fptemp = (int)(AC >> 18) & AMASK;       /* Include P&Q? */
                /* Diff SR char */
                fptemp -= (int)(SR >> 18) & AMASK;
                if (AC & AMSIGN)
                    f |= 2;
                if (SR & MSIGN)
                    f |= 1;
                /* Get mem frac */
                MA = (MA + 1);
                ReadMem(0, ibr);
                if (fptemp >= 0) {      /* AC Bigger */
                    /* Exchange MQ and ibr */
                    SR = MQ;
                    MQ = ibr;
                } else {        /* ibr Bigger then MQ, MQ Smaller */
                    fptemp = -fptemp;   /* Change sign */
                    /* Set exponent of result */
                    AC &= ~DMASK;
                    AC |= SR & DMASK;
                    SR = ibr;
                    f = ((f >> 1) & 1) | ((f & 1) << 1);
                }
                AC &= DMASK;
                /* Clear sign */
                MQ &= PMASK;

                /* Adjust smaller number */
                if (fptemp >= 0 && fptemp < 044) {
                    sim_interval--;
                    shiftcnt = fptemp;
                    while (shiftcnt > 0) {
                        MQ >>= 1;
                        shiftcnt--;
                    }
                } else
                    MQ = 0;
                sim_interval--;

                /* Check signes of SR & AC */
                if (f == 2 || f == 1) {
                    MQ ^= PMASK;
                    MQ += SR & PMASK;
                    /* If MQ < 0 then SR was larger */
                    if (MQ & MSIGN) {
                        MQ++;
                        MQ &= PMASK;
                    } else {
                        MQ ^= PMASK;
                        if (MQ != 0)
                            f ^= 2;     /* Change sign of Result */
                    }
                } else
                    MQ += SR & PMASK;

                /* Check for overflow */
                temp = 0;
                if (MQ & MSIGN) {
                    MQ >>= 1;
                    AC += 00000001000000LL;
                    /* OV check */
                    if (AC & APSIGN)
                        temp |= FPSPERR | FPACERR | FPOVERR;
                }

                /* Are we normalizing */
                if (opcode == OP_EAD || opcode == OP_ESB) {
                    sim_interval--;
                    while ((MQ & ONEBIT) == 0 && ((MQ & PMASK) != 0)) {
                        MQ <<= 1;
                        AC -= 00000001000000LL;
                    }
                    if (MQ == 0) {
                        AC = 0;
                    }
                }

                /* Check underflow */
                if (AC & AMSIGN) {
                    temp |= FPSPERR | FPMQERR;
                    if (AC & APSIGN)
                        temp |= FPSPERR | FPOVERR | FPACERR;
                } else if (AC & (AQSIGN | PREMASK))
                    temp |= FPOVERR | FPACERR;
                AC &= AMMASK;
                /* Set signs */
                if (f & 2) {
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }
                if (temp != 0) {
                  doefptrap:
                    if (FTM && CPU_MODEL != CPU_704) {
                        sim_interval = sim_interval - 1;        /* count down */
                        temp &= ~(FPMQERR | FPACERR);
                        M[0] &= ~(AMASK | DMASK);
                        M[0] |= temp | (IC & memmask);
                        IC = 010;
                    } else {
                        if (temp & FPMQERR)
                            mqoflag = 1;
                        if (temp & FPACERR)
                            acoflag = 1;
                    }
                }
                break;
            case OP_EMP:
                if ((cpu_unit.flags & OPTION_EFP) == 0)
                    break;
                temp = 0;

                /* Result sign */
                if (SR & MSIGN)
                    f = 1;
                else
                    f = 0;
                if (AC & AMSIGN)
                    f ^= 1;

                MQ &= PMASK;
                /* Quick out for times 0 */
                if (MQ == 0) {
                    AC &= RMASK;
                    if (f) {
                        MQ |= MSIGN;
                        AC |= AMSIGN;
                    }
                    break;
                }

                /* Extract AC char */
                fptemp = (int)(AC >> 18) & AMASK;
                /* Diff SR char */
                fptemp += (int)(SR >> 18) & AMASK;
                fptemp -= 040000;

                /* Get mem frac */
                MA = MA + 1;
                ReadMem(0, SR);
                SR &= PMASK;

                /* Quick out for times 0 */
                if (SR == 0) {
                    MQ = 0;
                    AC &= RMASK;
                    if (f) {
                        MQ |= MSIGN;
                        AC |= AMSIGN;
                    }
                    break;
                }

                AC = 0;
                /* Do multiply */
                shiftcnt = 043;
                while (shiftcnt-- > 0) {
                    if (MQ & 1)
                        AC += SR;
                    MQ >>= 1;
                    if (AC & 1)
                        MQ |= ONEBIT;
                    AC >>= 1;
                }

                /* Normalize result */
                if ((AC & ONEBIT) == 0) {
                    AC <<= 1;
                    if (MQ & ONEBIT)
                        AC |= 1;
                    fptemp--;
                }

                /* Move results to MQ. */
                MQ = AC;

                if (MQ == 0) {
                    AC = 0;
                } else {
                    /* Put exponent in place. */
                    AC = ((t_uint64) (fptemp)) << 18;
                    /* Check underflow */
                    if (AC & AMSIGN) {
                        temp |= FPSPERR | FPMQERR;
                        if (AC & APSIGN)
                            temp |= FPSPERR | FPOVERR | FPACERR;
                    } else if (AC & (AQSIGN | PREMASK))
                        temp |= FPOVERR | FPACERR;
                    /* Clear sign */
                    AC &= AMMASK;
                }
                if (f) {
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }
                if (temp != 0)
                    goto doefptrap;
                break;
            case OP_EDP:
                if ((cpu_unit.flags & OPTION_EFP) == 0)
                    break;

                /* Result sign */
                if (SR & MSIGN)
                    f = 1;
                else
                    f = 0;
                if (AC & AMSIGN)
                    f ^= 1;

                /* Extract AC char */
                fptemp = (int)(AC >> 18) & AMASK;       /* Include P&Q */
                /* Extract SR char */
                fptemp -= (int)(SR >> 18) & AMASK;
                fptemp += 040000;       /* UF check */

                /* Get mem frac */
                MA = MA + 1;
                ReadMem(0, SR);

                temp = 0;
                /* Check for divide by 0 */
                MQ &= PMASK;
                if (MQ == 0) {
                    AC = MQ = 0;
                } else {
                    SR &= PMASK;
                    if (((MQ - (SR << 1)) & AMSIGN) == 0 || SR == 0) {
                        dcheck = 1;
                        AC &= DMASK;
                        AC |= MQ & RMASK;
                        if (f) {
                            MQ |= MSIGN;
                            AC |= AMSIGN;
                        }
                        break;
                    }

                    /* Move MQ to AC */
                    AC = MQ & PMASK;

                    /* Clear MQ before starting */
                    MQ = 0;
                    shiftcnt = 043;
                    /* Precheck SR less then AC */
                    if (((AC - SR) & AMSIGN) == 0) {
                        if (AC & 1)
                            MQ |= ONEBIT;
                        AC >>= 1;
                        fptemp++;
                        f |= 2;
                    }

                    /* Do divide operation */
                    sim_interval = sim_interval - shiftcnt;
                    do {
                        AC <<= 1;
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
                    /* Fix things if we didn't preshifted */
                    if ((f & 2) == 0 && AC != 0)
                        MQ &= ~1;
                    AC = 0;
                    if (f & 2) {
                        if ((MQ & ONEBIT) == 0)
                            MQ <<= 1;
                    } else {
                        AC = RMASK;
                    }

                    /* Put exponent in place. */
                    AC |= ((t_uint64) (fptemp)) << 18;
                    /* Check underflow */
                    if (AC & AMSIGN) {
                        temp |= FPSPERR | FPMQERR;
                        if (AC & APSIGN)
                            temp |= FPSPERR | FPOVERR | FPACERR;
                    } else if (AC & (AQSIGN | PREMASK))
                        temp |= FPOVERR | FPACERR;
                    /* Clear sign */
                    AC &= AMMASK;
                }
                /* Fix signs on results */
                if (f & 1) {
                    MQ |= MSIGN;
                    AC |= AMSIGN;
                }
                if (temp != 0)
                    goto doefptrap;
                break;
            case OP_EST:
                if ((cpu_unit.flags & OPTION_EFP) == 0)
                    break;
                SR &= RMASK;
                if (AC & AMSIGN)
                    SR |= MSIGN;
                SR |= LMASK & PMASK & AC;
                /* Clear P+Q and 18-35 */
                AC &= AMSIGN | (PMASK & LMASK);
                WriteMem();
                MA = memmask & (MA + 1);
                SR = MQ;
                break;
            case OP_ELD:
                if ((cpu_unit.flags & OPTION_EFP) == 0)
                    break;
                AC = ((SR & MSIGN) << 2) | (SR & PMASK);
                MA = memmask & (MA + 1);
                ReadMem(0, MQ);
                break;

/* Special CTSS modes */
            case OP_TIA:
                /* Regular xfer in A core, B core trap */
                bcore &= ~2;
                sim_debug(DEBUG_PROT, &cpu_dev, "TIA %07o %07o\n", IC, MA);
                IC = MA;
                tbase = (relo_mode)?relocaddr:0;
                break;
            case OP_TIB:
                /* In A core xfer to B core, B core trap */
                bcore |= 2;
                sim_debug(DEBUG_PROT, &cpu_dev, "TIB %07o %07o\n", IC, MA);
                IC = MA;
                tbase = ((relo_mode)?relocaddr:0);
                break;
            case OP_LRI:
                /* In B core trap, else load relocation */
                relocaddr = (uint16)(SR & 077400);
                relo_pend = (SR & MSIGN) ? 0: 1;
                ihold = 1;
                sim_debug(DEBUG_PROT, &cpu_dev, "LRI %07o %012llo\n", IC, SR);
                break;
            case OP_LPI:
                /* In B core trap, else load protection */
                baseaddr = (uint16)(SR & 077400);
                limitaddr = (uint16)((SR >> 18) & 077400);
                ihold = 1;
                prot_pend = (SR & MSIGN)?0:1;
                sim_debug(DEBUG_PROT, &cpu_dev, "LPI %07o %012llo\n", IC, SR);
                break;
            case OP_SRI:
                /* In B core trap, else store relocation */
                SR = relocaddr | ((relo_mode)? (MSIGN >> 1) : 0);
                sim_debug(DEBUG_PROT, &cpu_dev, "SRI %07o %012llo\n", IC, SR);
                break;
            case OP_SPI:
                /* In B core trap, else store protection */
                SR = ((t_uint64)limitaddr) << 18 |
                     ((t_uint64)baseaddr);
                sim_debug(DEBUG_PROT, &cpu_dev, "SPI %07o %012llo\n", IC, SR);
                break;

            case OP_SPOP:
                switch (MA) {
                    /* Direct data disconnect */
                case 0:
                    /* Should do disco on channel 0 */
                    break;
                    /* Handle signifigence mode */
                case OP_ESM:
                    if (cpu_unit.flags & OPTION_FPSM)
                        smode = 1;
                    break;
                case OP_TSM:
                    if (cpu_unit.flags & OPTION_FPSM && smode)
                        IC++;
                    smode = 0;
                    break;
                    /* Special CTSS memory mods */
                case OP_SEA:
                    if ((cpu_unit.flags & UNIT_DUALCORE) == 0)
                        break;
                    /* CTSS Special, set effective to A, Acore only */
                    if (bcore & 4)
                        goto prottrap;
                    bcore &= ~1;
                    ihold = 1;
                    break;
                case OP_SEB:
                    if ((cpu_unit.flags & UNIT_DUALCORE) == 0)
                        break;
                    /* CTSS Special, set effective to B, Acore only */
                    if (bcore & 4)
                        goto prottrap;
                    bcore |= 1;
                    ihold = 1;
                    break;
                case OP_IFT:
                    if ((cpu_unit.flags & UNIT_DUALCORE) == 0)
                        break;
                    /* CTSS Special, skip if instruction A, Acore only */
                    if (bcore & 4)
                        goto prottrap;
                    if ((bcore & 1) == 0)
                        IC++;
                    break;
                case OP_EFT:
                    if ((cpu_unit.flags & UNIT_DUALCORE) == 0)
                        break;
                    /* CTSS Special, skip if effective A, Acore only */
                    if (bcore & 4)
                        goto prottrap;
                    if ((bcore & 2) == 0)
                        IC++;
                    break;
                }
                break;
#endif

            default:
                sim_printf("Invalid opcode %o IC=%o %012llo\n", opcode, IC, temp);
                reason = STOP_UUO;
                break;
            }
            if (opinfo & (S_B | S_F)) {
                WriteMem();
            }
            /* Store result into an index register */
            if ((opinfo & S_X)) {
                SR &= AMASK;
                update_xr(tag, SR);
            }
            break;
        }

        chan_proc();            /* process any pending channel events */
        if (instr_count != 0 && --instr_count == 0)
            return SCPE_STEP;
    }                           /* end while */

/* Simulation halted */

    return reason;
}

/* Nothing special to do, just return true if cmd is write and we got here */
uint32 cpu_cmd(UNIT * uptr, uint16 cmd, uint16 dev)
{
    if (cmd == OP_WRS)
        return 1;
    return -1;
}


/* Reset routine */

t_stat
cpu_reset(DEVICE * dptr)
{
    int                 i;

    AC = 0;
    MQ = 0;
    SR = 0;
    dualcore = 0;
    if (cpu_unit.flags & UNIT_DUALCORE)
        dualcore = 1;
    for (i = 0; i < 7; i++)
        XR[i] = 0;
    MTM = 1;
    TM = STM = CTM = nmode = smode = 0;
    FTM = 1;
    itrap = 1;
    iotraps = baseaddr = bcore = 0;
    ioflags = 0;
    interval_irq = dcheck = acoflag = mqoflag = iocheck = 0;
    sim_brk_types = sim_brk_dflt = SWMASK('E');
    limitaddr = 077777;
    memmask = MEMMASK;
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
        (void)sim_rtcn_calb (rtc_tps, TMR_RTC);
        sim_activate_after(uptr, 1000000/rtc_tps);
        M[5] += 1;
        if (M[5] & MSIGN)
            interval_irq = 1;
    }
    return SCPE_OK;
}

/* Memory examine */

t_stat
cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE)
        return SCPE_NXM;
    if (vptr != NULL)
        *vptr = M[addr] & 0777777777777LL;

    return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw)
{
    if (addr >= MAXMEMSIZE)
        return SCPE_NXM;
    M[addr] = val & 0777777777777LL;
    return SCPE_OK;
}

t_stat
cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc)
{
    t_uint64            mc = 0;
    uint32              i;
    int32               v;

    v = val >> UNIT_V_MSIZE;
    v *= 8192;
    if (v == 0)
        v = 4096;
    if ((v < 0) || (v > MAXMEMSIZE) || ((v & 07777) != 0))
        return SCPE_ARG;
    for (i = v-1; i < MEMSIZE; i++)
        mc |= M[i];
    if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    MEMSIZE = v;
    memmask = v - 1;
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
    fprintf(st,
"IC      AC            MQ            EA      SR             XR1    XR2   XR4\n\n");
    for (k = 0; k < lnt; k++) { /* print specified */
        h = &hst[(++di) % hst_lnt];     /* entry pointer */
        if (h->ic & HIST_PC) {  /* instruction? */
            fprintf(st, "%06o%c", h->ic & 077777, ((h->ic>>19)&1)?'b':' ');
            switch ((h->ac & (AMSIGN | AQSIGN | APSIGN)) >> 35L) {
            case (AMSIGN | AQSIGN | APSIGN) >> 35L:
                fprintf(st, "-QP");
                break;
            case (AMSIGN | AQSIGN) >> 35L:
                fprintf(st, " -Q");
                break;
            case (AMSIGN | APSIGN) >> 35L:
                fprintf(st, " -P");
                break;
            case (AMSIGN) >> 35L:
                fprintf(st, "  -");
                break;
            case (AQSIGN | APSIGN) >> 35L:
                fprintf(st, " QP");
                break;
            case (AQSIGN) >> 35L:
                fprintf(st, "  Q");
                break;
            case (APSIGN) >> 35L:
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
            fprint_val(st, h->ea, 8, 16, PV_RZRO);
            fputc(((h->ic>>18)&1)?'b':' ', st);
            if (h->sr & MSIGN)
                fputc('-', st);
            else
                fputc(' ', st);
            fprint_val(st, h->sr & PMASK, 8, 35, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->xr1, 8, 15, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->xr2, 8, 15, PV_RZRO);
            fputc(' ', st);
            fprint_val(st, h->xr4, 8, 15, PV_RZRO);
            fputc(' ', st);
            sim_eval = h->op;
            if (
                (fprint_sym
                 (st, h->ic & AMASK, &sim_eval, &cpu_unit,
                  SWMASK('M'))) > 0) fprintf(st, "(undefined) %012llo", h->op);
            fputc('\n', st);    /* end line */
        }                       /* end else instruction */
    }                           /* end for */
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr)
{
#ifdef I7090
       return "IBM 709x CPU";
#else
       return "IBM 704 CPU";
#endif
}

t_stat
cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
#ifdef I7090
fprintf (st, "The CPU can be set to a IBM 704, IBM 709, IBM 7090 or IBM 7094\n");
fprintf (st, "The type of CPU can be set by one of the following commands\n\n");
fprintf (st, "   sim> set CPU 704         sets IBM 704 emulation\n");
fprintf (st, "   sim> set CPU 709         sets IBM 709 emulation\n");
fprintf (st, "   sim> set CPU 7090        sets IBM 7090 emulation\n");
fprintf (st, "   sim> set CPU 7094        sets IBM 7094 emulation\n\n");
#else
fprintf (st, "The CPU behaves as a IBM 704\n");
#endif
fprintf (st, "These switches are recognized when examining or depositing in CPU memory:\n\n");
fprintf (st, "      -c      examine/deposit characters, 6 per word\n");
fprintf (st, "      -l      examine/deposit half words\n");
fprintf (st, "      -m      examine/deposit IBM 709 instructions\n\n");
fprintf (st, "The memory of the CPU can be set in 4K incrememts from 4K to 32K with the\n\n");
fprintf (st, "   sim> SET CPU xK\n\n");
#ifdef I7090
fprintf (st, "For systems like IBSYS FASTIO can be enabled. This causes the CPU to finish\n");
fprintf (st, "all outstanding I/O requests when it detects an IDLE loop. This is detected\n");
fprintf (st, "by a TCOx to itself. TRUEIO waits until the given timeout. ");
fprintf (st, "For faster\noperation FASTIO can speed up execution, by eliminating");
fprintf (st, "waits on devices.\nThe default is TRUEIO.\n\n");
fprintf (st, "For the IBM 709x the following options can be enabled\n\n");
fprintf (st, "   sim> SET CPU EFP      enables extended Floating Point\n");
fprintf (st, "   sim> SET CPU NOEFP    disables extended Floating Point\n\n");
fprintf (st, "   sim> SET CPU FPSM     enables significance mode Floating Point\n");
fprintf (st, "   sim> SET CPU NOFPSM   disables significance mode Floating Point\n\n");
fprintf (st, "   sim> SET CPU CLOCK    enables clock in memory location 5\n");
fprintf (st, "   sim> SET CPU NOCLOCK  disables the clock in memory location 5\n\n");
fprintf (st, "   sim> SET CPU STANDARD sets generic IBM 709x CPU\n");
fprintf (st, "   sim> SET CPU CTSS     enables RPQ options, DUAL Core and extended memory for\n");
fprintf (st, "                         CTSS support\n\n");
#endif
fprintf (st, "The CPU can maintain a history of the most recently executed instructions.\n"
);
fprintf (st, "This is controlled by the SET CPU HISTORY and SHOW CPU HISTORY commands:\n\n"
);
fprintf (st, "   sim> SET CPU HISTORY                 clear history buffer\n");
fprintf (st, "   sim> SET CPU HISTORY=0               disable history\n");
fprintf (st, "   sim> SET CPU HISTORY=n{:file}        enable history, length = n\n");
fprintf (st, "   sim> SHOW CPU HISTORY                print CPU history\n");
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);

return SCPE_OK;
}

