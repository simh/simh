/* gri_cpu.c: GRI-909 CPU simulator

   Copyright (c) 2001-2015, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   cpu          GRI-909/GRI-99 CPU

   14-Jan-08    RMS     Added GRI-99 support
   28-Apr-07    RMS     Removed clock initialization
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   18-Jul-04    RMS     Fixed missing ao_update calls in AX, AY write
   17-Jul-04    RMS     Revised MSR, EAO based on additional documentation
   14-Mar-03    RMS     Fixed bug in SC queue tracking

   The system state for the GRI-909/GRI-99 is:

   AX<15:0>             arithmetic input
   AY<15:0>             arithmetic input
   BSW<15:0>            byte swapper
   BPK<15:0>            byte packer
   GR[0:5]<15:0>        extended general registers
   MSR<15:0>            machine status register
   TRP<15:0>            trap register (subroutine return)
   SC<14:0>             sequence counter
   XR<15:0>             index register (GRI-99 only)

   The GRI-909 has, nominally, just one instruction format: move.

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |      source     |     op    |   destination   |    move
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

    <6:9>               operation

    xx1x                complement
    01xx                add 1
    10xx                rotate left 1
    11xx                rotate right 1

   In fact, certain of the source and destination operators have side
   effects, yielding four additional instruction formats: function out,
   skip on function, memory reference, and conditional jump.

   The function out format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0  1  0|  pulse    |   destination   |    function out
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The skip on function format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |      source     |  skip  |rv| 0  0  0  0  1  0|    skip function
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The memory reference format is (src and/or dst = 006):

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |      source     |  op | mode|   destination   |    memory ref
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |             address or immediate              |
   +-----------------------------------------------+

    <6:9>               operation

    xx0x                direct, ea = M[SC+1]
    xx1x                immediate, ea = SC+1
    xxx1                indirect, M[ea] = M[ea]+1, then ea = M[ea]
    01xx                add 1
    10xx                rotate left 1
    11xx                rotate right 1

   The conditional jump format is (src != 006):

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |      source     | cond|rv|df| 0  0  0  0  1  1|    cond jump
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                  jump address                 |
   +-----------------------------------------------+

    <6:9>               operation

    xxx0                direct, ea = M[SC+1]
    xxx1                indirect, ea = M[SC+1], M[ea] = M[ea]+1,
                        then ea = M[ea]
    xx1x                reverse conditional sense
    x1xx                jump if src == 0
    1xxx                jump if src < 0

   This routine is the instruction decode routine for the GRI-909.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        unknown source or destination and STOP_OPR flag set
        I/O error in I/O simulator

   2. Interrupts.  The interrupt structure is kept in two parallel variables:

        dev_done        device done flags
        ISR             interrupt status register (enables)

      In addition, there is a master interrupt enable, and a one cycle
      interrupt defer, both kept in dev_done.

   3. Non-existent memory.  On the GRI-909, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        gri_defs.h      add interrupt request definition
        gri_cpu.c       add dev_tab table entry
        gri_sys.c       add sim_devices table entry
*/

#include "gri_defs.h"

#define SCQ_SIZE        64                              /* must be 2**n */
#define SCQ_MASK        (SCQ_SIZE - 1)
#define SCQ_ENTRY       scq[scq_p = (scq_p - 1) & SCQ_MASK] = SC
#define UNIT_V_AO       (UNIT_V_UF + 0)                 /* AO */
#define UNIT_AO         (1u << UNIT_V_AO)
#define UNIT_V_EAO      (UNIT_V_UF + 1)                 /* EAO  */
#define UNIT_EAO        (1u << UNIT_V_EAO)
#define UNIT_V_GPR      (UNIT_V_UF + 2)                 /* GPR */
#define UNIT_GPR        (1u << UNIT_V_GPR)
#define UNIT_V_BSWPK    (UNIT_V_UF + 3)                 /* BSW-BPK */
#define UNIT_BSWPK      (1u << UNIT_V_BSWPK)
#define UNIT_V_GRI99    (UNIT_V_UF + 4)                 /* GRI-99 */
#define UNIT_GRI99      (1u << UNIT_V_GRI99)
#define UNIT_V_MSIZE    (UNIT_V_UF + 5)                 /* dummy mask */
#define UNIT_MSIZE      (1u << UNIT_V_MSIZE)

#define IDX_ADD(x)      ((((cpu_unit.flags & UNIT_GRI99) && ((x) & INDEX))? ((x) + XR): (x)) & AMASK)

uint16 M[MAXMEMSIZE] = { 0 };                           /* memory */
uint32 SC;                                              /* sequence cntr */
uint32 AX, AY, AO;                                      /* arithmetic unit */
uint32 IR;                                              /* instr reg */
uint32 MA;                                              /* memory addr */
uint32 TRP;                                             /* subr return */
uint32 MSR;                                             /* machine status */
uint32 ISR;                                             /* interrupt status */
uint32 BSW, BPK;                                        /* byte swap, pack */
uint32 GR[6];                                           /* extended general regs */
uint32 SWR;                                             /* switch reg */
uint32 DR;                                              /* display register */
uint32 XR;                                              /* index register */
uint32 thwh = 0;                                        /* thumbwheel */
uint32 dev_done = 0;                                    /* device flags */
uint32 bkp = 0;                                         /* bkpt pending */
uint32 stop_opr = 1;                                    /* stop ill operator */
int16 scq[SCQ_SIZE] = { 0 };                            /* PC queue */
int32 scq_p = 0;                                        /* PC queue ptr */
REG *scq_r = NULL;                                      /* PC queue reg ptr */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat bus_op (uint32 src, uint32 op, uint32 dst);

/* Dispatch tables for source, dest, function out, skip on function */

uint32 no_rd (uint32 src);
t_stat no_wr (uint32 dst, uint32 val);
t_stat no_fo (uint32 op);
uint32 no_sf (uint32 op);
uint32 zero_rd (uint32 src);
t_stat zero_wr (uint32 dst, uint32 val);
t_stat zero_fo (uint32 op);
uint32 zero_sf (uint32 op);
uint32 ir_rd (uint32 op);
t_stat ir_fo (uint32 op);
uint32 trp_rd (uint32 src);
t_stat trp_wr (uint32 dst, uint32 val);
uint32 atrp_rd (uint32 src);
t_stat atrp_wr (uint32 dst, uint32 val);
uint32 isr_rd (uint32 src);
t_stat isr_wr (uint32 dst, uint32 val);
t_stat isr_fo (uint32 op);
uint32 isr_sf (uint32 op);
uint32 ma_rd (uint32 src);
uint32 mem_rd (uint32 src);
t_stat mem_wr (uint32 dst, uint32 val);
uint32 sc_rd (uint32 src);
t_stat sc_wr (uint32 dst, uint32 val);
uint32 swr_rd (uint32 src);
uint32 ax_rd (uint32 src);
t_stat ax_wr (uint32 dst, uint32 val);
uint32 ay_rd (uint32 src);
t_stat ay_wr (uint32 dst, uint32 val);
uint32 ao_rd (uint32 src);
t_stat ao_fo (uint32 op);
uint32 ao_sf (uint32 op);
uint32 ao_update (void);
t_stat eao_fo (uint32 op);
uint32 msr_rd (uint32 src);
t_stat msr_wr (uint32 dst, uint32 val);
uint32 bsw_rd (uint32 src);
t_stat bsw_wr (uint32 dst, uint32 val);
uint32 bpk_rd (uint32 src);
t_stat bpk_wr (uint32 dst, uint32 val);
uint32 gr_rd (uint32 src);
t_stat gr_wr (uint32 dst, uint32 val);
uint32 xr_rd (uint32 src);
t_stat xr_wr (uint32 dst, uint32 val);

extern t_stat rtc_fo (uint32 op);
extern uint32 rtc_sf (uint32 op);
extern uint32 hsrp_rd (uint32 src);
extern t_stat hsrp_wr (uint32 dst, uint32 val);
extern t_stat hsrp_fo (uint32 op);
extern uint32 hsrp_sf (uint32 op);
extern uint32 tty_rd (uint32 src);
extern t_stat tty_wr (uint32 dst, uint32 val);
extern t_stat tty_fo (uint32 op);
extern uint32 tty_sf (uint32 op);

struct gdev dev_tab[64] = {
    { &zero_rd, &zero_wr, &zero_fo, &zero_sf },         /* 00: zero */
    { &ir_rd, &zero_wr, &ir_fo, &zero_sf },             /* ir */
    { &no_rd, &no_wr, &no_fo, &no_sf },                 /* fo/sf */
    { &trp_rd, &trp_wr, &zero_fo, &zero_sf },           /* trp */
    { &isr_rd, &isr_wr, &isr_fo, &isr_sf },             /* isr */
    { &ma_rd, &no_wr, &no_fo, &no_sf },                 /* ma */
    { &mem_rd, &mem_wr, &zero_fo, &zero_sf },           /* memory */
    { &sc_rd, &sc_wr, &zero_fo, &zero_sf },             /* sc */
    { &swr_rd, &no_wr, &no_fo, &no_sf },                /* swr */
    { &ax_rd, &ax_wr, &zero_fo, &zero_sf },             /* ax */
    { &ay_rd, &ay_wr, &zero_fo, &zero_sf },             /* ay */
    { &ao_rd, &zero_wr, &ao_fo, &ao_sf },               /* ao */
    { &zero_rd, &zero_wr, &eao_fo, &zero_sf },          /* eao */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &msr_rd, &msr_wr, &zero_fo, &zero_sf },           /* msr */
    { &no_rd, &no_wr, &no_fo, &no_sf },                 /* 20 */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &xr_rd, &xr_wr, &no_fo, &no_sf },                 /* xr */
    { &atrp_rd, &atrp_wr, &no_fo, &no_sf },
    { &bsw_rd, &bsw_wr, &no_fo, &no_sf },               /* bsw */
    { &bpk_rd, &bpk_wr, &no_fo, &no_sf },               /* bpk */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &gr_rd, &gr_wr, &zero_fo, &zero_sf },             /* 30: gr1 */
    { &gr_rd, &gr_wr, &zero_fo, &zero_sf },             /* gr2 */
    { &gr_rd, &gr_wr, &zero_fo, &zero_sf },             /* gr3 */
    { &gr_rd, &gr_wr, &zero_fo, &zero_sf },             /* gr4 */
    { &gr_rd, &gr_wr, &zero_fo, &zero_sf },             /* gr5 */
    { &gr_rd, &gr_wr, &zero_fo, &zero_sf },             /* gr6 */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },                 /* 40 */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },                 /* 50 */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },                 /* 60 */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },                 /* 70 */
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &no_rd, &no_wr, &no_fo, &no_sf },
    { &zero_rd, &zero_wr, &rtc_fo, &rtc_sf },           /* rtc */
    { &hsrp_rd, &hsrp_wr, &hsrp_fo, &hsrp_sf },         /* hsrp */
    { &tty_rd, &tty_wr, &tty_fo, &tty_sf }              /* tty */
    };

static const int32 vec_map[16] = {
    VEC_TTO, VEC_TTI, VEC_HSP, VEC_HSR,
    -1, -1, -1, -1,
    -1, -1, -1, VEC_RTC,
    -1, -1, -1, -1
    };

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_AO+UNIT_EAO+UNIT_GPR, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATA (SC, SC, 15) },
    { ORDATA (AX, AX, 16) },
    { ORDATA (AY, AY, 16) },
    { ORDATA (AO, AO, 16), REG_RO },
    { ORDATA (TRP, TRP, 16) },
    { ORDATA (MSR, MSR, 16) },
    { ORDATA (ISR, ISR, 16) },
    { ORDATA (BSW, BSW, 16) },
    { ORDATA (BPK, BPK, 16) },
    { ORDATA (GR1, GR[0], 16) },
    { ORDATA (GR2, GR[1], 16) },
    { ORDATA (GR3, GR[2], 16) },
    { ORDATA (GR4, GR[3], 16) },
    { ORDATA (GR5, GR[4], 16) },
    { ORDATA (GR6, GR[5], 16) },
    { ORDATA (XR, XR, 16) },
    { FLDATA (BOV, MSR, MSR_V_BOV) },
    { FLDATA (L, MSR, MSR_V_L) },
    { GRDATA (FOA, MSR, 8, 2, MSR_V_FOA) },
    { FLDATA (SOV, MSR, MSR_V_SOV) },
    { FLDATA (AOV, MSR, MSR_V_AOV) },
    { ORDATA (IR, IR, 16), REG_RO },
    { ORDATA (MA, MA, 16), REG_RO },
    { ORDATA (SWR, SWR, 16) },
    { ORDATA (DR, DR, 16) },
    { ORDATA (THW, thwh, 6) },
    { ORDATA (IREQ, dev_done, INT_V_NODEF) },
    { FLDATA (ION, dev_done, INT_V_ON) },
    { FLDATA (INODEF, dev_done, INT_V_NODEF) },
    { FLDATA (BKP, bkp, 0) },
    { BRDATA (SCQ, scq, 8, 15, SCQ_SIZE), REG_RO + REG_CIRC },
    { ORDATA (SCQP, scq_p, 6), REG_HRO },
    { FLDATA (STOP_OPR, stop_opr, 0) },
    { ORDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_GRI99, UNIT_GRI99, "GRI99", "GRI99", NULL },
    { UNIT_GRI99, 0,          "GRI909", "GRI909", NULL },
    { UNIT_AO, UNIT_AO, "AO", "AO", NULL },
    { UNIT_AO, 0,       "no AO", "NOAO", NULL },
    { UNIT_EAO, UNIT_EAO, "EAO", "EAO", NULL },
    { UNIT_EAO, 0,        "no EAO", "NOEAO", NULL },
    { UNIT_GPR, UNIT_GPR, "GPR", "GPR", NULL },
    { UNIT_GPR, 0,        "no GPR", "NOGPR", NULL },
    { UNIT_BSWPK, UNIT_BSWPK, "BSW-BPK", "BSW-BPK", NULL },
    { UNIT_BSWPK, 0,          "no BSW-BPK", "NOBSW-BPK", NULL },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 15, 1, 8, 16,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
    };

t_stat sim_instr (void)
{
uint32 src, dst, op, t, jmp;
t_stat reason;

/* Restore register state */

SC = SC & AMASK;                                        /* load local PC */
reason = 0;
ao_update ();                                           /* update AO */

/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until halted */

    if (sim_interval <= 0) {                            /* check clock queue */
        if ((reason = sim_process_event ()))
            break;
        }

    if (bkp) {                                          /* breakpoint? */
        bkp = 0;                                        /* clear request */
        dev_done = dev_done & ~INT_ON;                  /* int off */
        M[VEC_BKP] = SC;                                /* save SC */
        SC = VEC_BKP + 1;                               /* new SC */
        }

    else if ((dev_done & (INT_PENDING | ISR)) > (INT_PENDING)) { /* intr? */
        int32 i, vec;
        t = dev_done & ISR;                             /* find hi pri */
        for (i = 15; i >= 0; i--) {
            if ((t >> i) & 1)
                break;
            }
        if ((i < 0) || ((vec = vec_map[i]) < 0)) {      /* undefined? */
            reason = STOP_ILLINT;                       /* stop */
            break;
            }
        dev_done = dev_done & ~INT_ON;                  /* int off */
        M[vec] = SC;                                    /* save SC */
        SC = vec + 1;                                   /* new SC */
        continue;
        }

    if (sim_brk_summ && sim_brk_test (SC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

    MA = SC;                                            /* set mem addr */
    IR = M[MA];                                         /* fetch instr */
    dev_done = dev_done | INT_NODEF;                    /* clr ion defer */
    sim_interval = sim_interval - 1;

/* Decode instruction types */

    src = I_GETSRC (IR);                                /* src unit */
    dst = I_GETDST (IR);                                /* dst unit */
    op = I_GETOP (IR);                                  /* bus op */

    if (src == U_FSK) {                                 /* func out? */
        reason = dev_tab[dst].FO (op);                  /* send function */
        SC = (SC + 1) & AMASK;                          /* incr SC */
        }

    else if (dst == U_FSK) {                            /* skip func? */
        t = dev_tab[src].SF (op & ~1);                  /* issue SF */
        reason = t >> SF_V_REASON;
        if ((t ^ op) & 1)                               /* skip? */
            SC = SC + 2;
        SC = (SC + 1) & AMASK;                          /* incr SC */
        }

    else if ((src != U_MEM) && (dst == U_TRP)) {        /* cond jump */
        t = dev_tab[src].Src (src);                     /* get source */
        switch (op >> 1) {                              /* case on jump */

            case 00:                                    /* never */
                jmp = 0;
                break;

            case 01:                                    /* always */
                jmp = 1;
                break;

            case 02:                                    /* src == 0 */
                jmp = (t == 0);
                break;

            case 03:                                    /* src != 0 */
                jmp = (t != 0);
                break;

            case 04:                                    /* src < 0 */
                jmp = (t >= SIGN);
                break;

            case 05:                                    /* src >= 0 */
                jmp = (t < SIGN);
                break;

            case 06:                                    /* src <= 0 */
                jmp = (t == 0) || (t & SIGN);
                break;

            case 07:                                    /* src > 0 */
                jmp = (t != 0) && !(t & SIGN);
                break;
            }

        if (jmp) {                                      /* jump taken? */
            SCQ_ENTRY;                                  /* save SC */
            SC = (SC + 1) & AMASK;                      /* incr SC once */
            MA = M[SC];                                 /* get jump addr */
            MA = IDX_ADD (MA);                          /* index? */
            if (op & TRP_DEF) {                         /* defer? */
                t = (M[MA] + 1) & DMASK;                /* autoinc */
                if (MEM_ADDR_OK (MA)) M[MA] = t;
                MA = IDX_ADD (t);                       /* index? */
                }
            TRP = SC;                                   /* save SC */
            SC = MA;                                    /* load new SC */
            }
        else SC = (SC + 2) & AMASK;                     /* incr SC twice */
        }

    else if ((src != U_MEM) && (dst != U_MEM)) {        /* reg-reg? */
        reason = bus_op (src, op, dst);                 /* xmt and modify */
        SC = (SC + 1) & AMASK;                          /* incr SC */
        }

/* Memory reference.  The second SC increment occurs after the first
   execution cycle.  For direct, defer, and immediate defer, this is
   after the first memory read and before the bus transfer; but for
   immediate, it is after the bus transfer.
*/

    else {                                              /* memory reference */
        SC = (SC + 1) & AMASK;                          /* incr SC */
        switch (op & MEM_MOD) {                         /* case on addr mode */

            case MEM_DIR:                               /* direct */
                MA = M[SC];                             /* get address */
                MA = IDX_ADD (MA);                      /* index? */
                SC = (SC + 1) & AMASK;                  /* incr SC again */
                reason = bus_op (src, op & BUS_FNC, dst); /* xmt and modify */
                break;

            case MEM_DEF:                               /* defer */
                MA = M[SC];                             /* get ind addr */
                MA = IDX_ADD (MA);                      /* index? */
                SC = (SC + 1) & AMASK;                  /* incr SC again */
                t = (M[MA] + 1) & DMASK;                /* autoinc */
                if (MEM_ADDR_OK (MA))
                    M[MA] = t;
                MA = IDX_ADD (t);                       /* index? */
                reason = bus_op (src, op & BUS_FNC, dst); /* xmt and modify */
                break;

            case MEM_IMM:                               /* immediate */
                MA = SC;                                /* eff addr */
                reason = bus_op (src, op & BUS_FNC, dst); /* xmt and modify */
                SC = (SC + 1) & AMASK;                  /* incr SC again */
                break;

            case MEM_IDF:                               /* immediate defer */
                MA = SC;                                /* get ind addr */
                t = (M[MA] + 1) & DMASK;                /* autoinc */
                if (MEM_ADDR_OK (MA))
                    M[MA] = t;
                MA = IDX_ADD (t);                       /* index? */
                SC = (SC + 1) & AMASK;                  /* incr SC again */
                reason = bus_op (src, op & BUS_FNC, dst); /* xmt and modify */
                break;
            }                                           /* end switch */
        }                                               /* end mem ref */
    }                                                   /* end while */

/* Simulation halted */

ao_update ();                                           /* update AO */
scq_r->qptr = scq_p;                                    /* update sc q ptr */
return reason;
}

/* Bus operations */

t_stat bus_op (uint32 src, uint32 op, uint32 dst)
{
uint32 t, old_t;

t = dev_tab[src].Src (src);                             /* get src */
if (op & BUS_COM)                                       /* complement? */
    t = t ^ DMASK;
switch (op & BUS_FNC) {                                 /* case op */

    case BUS_P1:                                        /* plus 1 */
        t = t + 1;                                      /* do add */
        if (t & CBIT)                                   /* set cry out */
            MSR = MSR | MSR_BOV;
        else MSR = MSR & ~MSR_BOV;
        break;

    case BUS_L1:                                        /* left 1 */
        t = (t << 1) | ((MSR & MSR_L)? 1: 0);           /* rotate */
        if (t & CBIT)                                   /* set link out */
            MSR = MSR | MSR_L;
        else MSR = MSR & ~MSR_L;
        break;

    case BUS_R1:                                        /* right 1 */
        old_t = t;
        t = (t >> 1) | ((MSR & MSR_L)? SIGN: 0);        /* rotate */
        if (old_t & 1)                                  /* set link out */
            MSR = MSR | MSR_L;
        else MSR = MSR & ~MSR_L;
        break;
        }                                               /* end case op */

if (dst == thwh)                                        /* display dst? */
    DR = t & DMASK;
return dev_tab[dst].Dst (dst, t & DMASK);               /* store dst */
}

/* Non-existent device */

uint32 no_rd (uint32 src)
{
return 0;
}

t_stat no_wr (uint32 dst, uint32 dat)
{
return stop_opr;
}

t_stat no_fo (uint32 fnc)
{
return stop_opr;
}

uint32 no_sf (uint32 fnc)
{
return (stop_opr << SF_V_REASON);
}

/* Zero device */

uint32 zero_rd (uint32 src)
{
return 0;
}

t_stat zero_wr (uint32 dst, uint32 val)
{
return SCPE_OK;
}

t_stat zero_fo (uint32 op)
{
switch (op & 3) {                                       /* FOM link */

    case 1:                                             /* CLL */
        MSR = MSR & ~MSR_L;
        break;

    case 2:                                             /* STL */
        MSR = MSR | MSR_L;
        break;

    case 3:                                             /* CML */
        MSR = MSR ^ MSR_L;
        break;
        }

if (op & 4)                                             /* HALT */
    return STOP_HALT;
return SCPE_OK;
}

uint32 zero_sf (uint32 op)
{
if ((op & 010) ||                                       /* power always ok */
    ((op & 4) && (MSR & MSR_L)) ||                      /* link set? */
    ((op & 2) && (MSR & MSR_BOV)))                      /* BOV set? */
    return 1;
return 0;
}

/* Instruction register (01) */

uint32 ir_rd (uint32 src)
{
return IR;
}

t_stat ir_fo (uint32 op)
{
if (op & 2)
    bkp = 1;
return SCPE_OK;
}

/* Trap register (03) */

uint32 trp_rd (uint32 src)
{
return TRP;
}

t_stat trp_wr (uint32 dst, uint32 val)
{
TRP = val;
return SCPE_OK;
}

/* Interrupt status register (04) */

uint32 isr_rd (uint32 src)
{
return ISR;
}

t_stat isr_wr (uint32 dst, uint32 dat)
{
ISR = dat;
return SCPE_OK;
}

t_stat isr_fo (uint32 op)
{
if (op & ISR_ON)
    dev_done = (dev_done | INT_ON) & ~INT_NODEF;
if (op & ISR_OFF)
    dev_done = dev_done & ~INT_ON;
return SCPE_OK;
}

uint32 isr_sf (uint32 op)
{
return 0;
}

/* Memory address (05) */

uint32 ma_rd (uint32 src)
{
return MA;
}

/* Memory (06) */

uint32 mem_rd (uint32 src)
{
return M[MA];
}

t_stat mem_wr (uint32 dst, uint32 dat)
{

if (MEM_ADDR_OK (MA))
    M[MA] = dat;
return SCPE_OK;
}

/* Sequence counter (07) */

uint32 sc_rd (uint32 src)
{
return SC;
}

t_stat sc_wr (uint32 dst, uint32 dat)
{
SCQ_ENTRY;
SC = IDX_ADD (dat);
return SCPE_OK;
}

/* Switch register (10) */

uint32 swr_rd (uint32 src)
{
return SWR;
}

/* Machine status register (17) */

uint32 msr_rd (uint32 src)
{
return MSR & MSR_RW;
}

t_stat msr_wr (uint32 src, uint32 dat)
{
MSR = dat & MSR_RW;                                     /* new MSR */
ao_update ();                                           /* update SOV,AOV */
return SCPE_OK;
}

/* Arithmetic operator (11:13) */

uint32 ao_update (void)
{
uint32 af = MSR_GET_FOA (MSR);

switch (af) {

    case AO_ADD:
        AO = (AX + AY) & DMASK;                         /* add */
        break;

    case AO_AND:
        AO = AX & AY;                                   /* and */
        break;

    case AO_XOR:                                        /* xor */
        AO = AX ^ AY;
        break;

    case AO_IOR:
        AO = AX | AY;                                   /* or */
        break;
        }

if ((AX + AY) & CBIT)                                   /* always calc AOV */
    MSR = MSR | MSR_AOV;
else MSR = MSR & ~MSR_AOV;
if (SIGN & ((AX ^ (AX + AY)) & (~AX ^ AY)))             /* always calc SOV */
    MSR = MSR | MSR_SOV;
else MSR = MSR & ~MSR_SOV;
return AO;
}

uint32 ax_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_AO)
    return AX;
else return 0;
}

t_stat ax_wr (uint32 dst, uint32 dat)
{
if (cpu_unit.flags & UNIT_AO) {
    AX = dat;
    ao_update ();
    return SCPE_OK;
    }
return stop_opr;
}

uint32 ay_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_AO)
    return AY;
else return 0;
}

t_stat ay_wr (uint32 dst, uint32 dat)
{
if (cpu_unit.flags & UNIT_AO) {
    AY = dat;
    ao_update ();
    return SCPE_OK;
    }
return stop_opr;
}

uint32 ao_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_AO)
    return ao_update ();
else return 0;
}

t_stat ao_fo (uint32 op)
{
if (cpu_unit.flags & UNIT_AO) {
    uint32 t = OP_GET_FOA (op);                         /* get func */
    MSR = MSR_PUT_FOA (MSR, t);                         /* store in MSR */
    ao_update ();                                       /* update AOV */
    return SCPE_OK;
    }
return stop_opr;
}

uint32 ao_sf (uint32 op)
{
if (!(cpu_unit.flags & UNIT_AO))                        /* not installed? */
    return (stop_opr << SF_V_REASON);
if (((op & 2) && (MSR & MSR_AOV)) ||                    /* arith carry? */
    ((op & 4) && (MSR & MSR_SOV)))                      /* arith overflow? */
    return 1;
return 0;
}

/* Extended arithmetic operator (14) */

t_stat eao_fo (uint32 op)
{
uint32 t;

if (!(cpu_unit.flags & UNIT_EAO))                       /* EAO installed? */
    return stop_opr;
switch (op) {

    case EAO_MUL:                                       /* mul? */
        t = AX * AY;                                    /* AX * AY */
        AX = (t >> 16) & DMASK;                         /* to AX'GR1 */
        GR[0] = t & DMASK;
        break;

    case EAO_DIV:                                       /* div? */
        if (AY && (AX < AY)) {
            t = (AX << 16) | GR[0];                     /* AX'GR1 / AY */
            GR[0] = t / AY;                             /* quo to GR1 */
            AX = t % AY;                                /* rem to AX */
            MSR = MSR & ~MSR_L;                         /* clear link */
            }
        else MSR = MSR | MSR_L;                         /* set link */
        break;

    case EAO_ARS:                                       /* arith right? */
        t = 0;                                          /* shift limiter */
        if (AX & SIGN)                                  /* L = sign */
            MSR = MSR | MSR_L;
        else MSR = MSR & ~MSR_L;
        do {                                            /* shift one bit */
            AY = ((AY >> 1) | (AX << 15)) & DMASK;
            AX = (AX & SIGN) | (AX >> 1);
            GR[0] = (GR[0] + 1) & DMASK;
            }
        while (GR[0] && (++t < 32));                    /* until cnt or limit */
        break;      

    case EAO_NORM:                                      /* norm? */
        if ((AX | AY) != 0) {                           /* can normalize? */
            while ((AX & SIGN) != ((AX << 1) & SIGN)) { /* until AX15 != AX14 */
                AX = ((AX << 1) | (AY >> 15)) & DMASK;
                AY = (AY << 1) & DMASK;
                GR[0] = (GR[0] + 1) & DMASK;
                }
            }
        break;
    }

//    MSR = MSR_PUT_FOA (MSR, AO_ADD);                    /* AO fnc is add */
ao_update ();
return SCPE_OK;
}

/* Index register (GRI-99) (22) */

uint32 xr_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_GRI99)
    return XR;
else return 0;
}

t_stat xr_wr (uint32 dst, uint32 val)
{
if (cpu_unit.flags & UNIT_GRI99) {
    XR = val;
    return SCPE_OK;
    }
return stop_opr;
}

/* Alternate trap (GRI-99) (23) */

uint32 atrp_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_GRI99)
    return TRP;
else return 0;
}

t_stat atrp_wr (uint32 dst, uint32 val)
{
if (cpu_unit.flags & UNIT_GRI99) {
    TRP = val;
    return SCPE_OK;
    }
return stop_opr;
}

/* Byte swapper (24) */

uint32 bsw_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_BSWPK)
    return BSW;
else return 0;
}

t_stat bsw_wr (uint32 dst, uint32 val)
{
if (cpu_unit.flags & UNIT_BSWPK) {
    BSW = ((val >> 8) & 0377) | ((val & 0377) << 8);
    return SCPE_OK;
    }
return stop_opr;
}

/* Byte packer (25) */

uint32 bpk_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_BSWPK)
    return BPK;
else return 0;
}

t_stat bpk_wr (uint32 dst, uint32 val)
{
if (cpu_unit.flags & UNIT_BSWPK) {
    BPK = ((BPK & 0377) << 8) | (val & 0377);
    return SCPE_OK;
    }
return stop_opr;
}

/* General registers (30:35) */

uint32 gr_rd (uint32 src)
{
if (cpu_unit.flags & UNIT_GPR)
    return GR[src - U_GR];
else return 0;
}

t_stat gr_wr (uint32 dst, uint32 dat)
{
if (cpu_unit.flags & UNIT_GPR) {
    GR[dst - U_GR] = dat;
    return SCPE_OK;
    }
return stop_opr;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int32 i;

AX = AY = AO = 0;
XR = 0;
TRP = 0;
ISR = 0;
MSR = 0;
MA = IR = 0;
BSW = BPK = 0;
for (i = 0; i < 6; i++)
    GR[i] = 0;
dev_done = dev_done & ~INT_PENDING;
scq_r = find_reg ("SCQ", NULL, dptr);
if (scq_r)
    scq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = M[addr] & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
M[addr] = val & DMASK;
return SCPE_OK;
}

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++)
    mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++)
    M[i] = 0;
return SCPE_OK;
}
