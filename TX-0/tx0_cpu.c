/*************************************************************************
 *                                                                       *
 * $Id: tx0_cpu.c 2066 2009-02-27 15:57:22Z hharte $                     *
 *                                                                       *
 * Copyright (c) 2009-2017 Howard M. Harte.                              *
 * Based on pdp1_cpu.c, Copyright (c) 1993-2007, Robert M. Supnik        *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * of Howard M. Harte.                                                   *
 *                                                                       *
 * Module Description:                                                   *
 *     TX-0 Central Processor                                            *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 * References:                                                           *
 *     See: www.bitsavers.org/pdf/mit/tx-0/ for documentation.           *
 *     See: www.bitsavers.org/bits/MIT/tx-0/ for software.               *
 *************************************************************************/

/*

The original Lincoln Labs TX-0 had only two bits of opcode and no index
register. The machine was moved to room 26-248 at MIT in July 1958 and
after about a year and a half the opcode field was extended to four bits
and an index register was added. 

(ref. Computer Museum Report Vol 8, Spring 1984)

--------------------------------------------------------------
Original TX-0 Registers and Instruction Set
from "A Functional Description of the TX-0 Computer" Oct, 1958
--------------------------------------------------------------

    The register state for the TX-0 is:

        MBR[0:17]       Memory Buffer Register (18 bits)
        AC[0:17]        Accumulator (18 bits)
        MAR[0:15]       Memory Address Register (16 bits)
        PC[0:15]        Program Counter (16 bits)
        IR[0:1]         Instruction Register (2 bits)
        LR[0:17]        Live Register (18 bits)
        TBR[0:17]       Toggle Switch Buffer Register (18 toggle switches)
        TAC[0:17]       Toggle Switch Accumulator (18 toggle switches)

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |  op |                       address                 |              
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   This routine is the instruction decode routine for the TX-0.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Instruction Set: The TX-0 had (at least) two different instruction sets.
      The original instruction set from 1956 had two bits for the opcode (IR)
      while the later 1961 instruction set used five bits for this purpose.
      This simulator is designed to simulate both of these modes.  The micro
      orders were also changed along with the new instruction set.  The main
      part of the instruction fetch/decode logic is the same for both
      instruction sets.  Memory address range is adjusted depending on the mode.
      IR[2:4] are forced to 0 in the 1956 mode.  The Micro Orders are vastly
      different, so the main loop decodes and executes the 1961 micro-orders,
      while the sim_opr_orig() function decodes and executes the 1956 micro-orders.

   2. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        I/O error in I/O simulator

   3. Arithmetic.  The TX-0 is a 1's complement system.  In 1's
      complement arithmetic, a negative number is represented by the
      complement (XOR 0777777) of its absolute value.  Addition of 1's
      complement numbers requires propagating the carry out of the high
      order bit back to the low order bit.

   4. Adding I/O devices.  Three modules must be modified:

        tx0_cpu.c      add dispatch code
        tx0_sys.c      add sim_devices table entry

   5. Bugs, limitations, and known issues:
        o There is a bug in the 1961 instruction set simulation, which causes the
          mouse maze program's searching algorithm to fail.
        o The CRY micro-order is not implemented.
        o The instruction timing (ie, sim_interval) is not accurate, so implementing a
          timing-critical I/O device like the Magtape would require this to be added first.
        o PCQ and History do not work.
        o Symbolic input does not work.
        o Probably lots of other bugs, as Tic-Tac-Toe and Mouse Maze are the only tapes
          that I've tested to mostly work.  It's difficult to tell what tapes on
          bitsavers.org are designed for the 1956 instruction set vs. the later one.


OP    Description
--    -----------

00    sto x   Replace the contents of register x with the contents of the AC
              Let the AC remain the same.
01    add x   Add the word in register x to the contents of the AC and leave
              the sum in the AC
10    trn x   If the sign digit of the accumulator (AC bit 0) is negative (i.e., a one)
              take the next instruction from register x and continue from there.
              If the sign is positive (i.e., a zero ignore this instruction and proceed
              to the next instruction
11    opr x   Execute one of the operate class commands indicated by the number x

Around 1961, the TX-0 was modified to include additional instructions, and the addressable
memory range was lowered to 8K words.  The IR was increased from two bits to five bits.
In addtion, many of the operate-class micro-orders changed.

      OPERATE CLASS MICRO-ORDERS FOR 1961 INSTRUCTION SET
      ---------------------------------------------------
                              Cycle.tp
  cla  --1 --- --- --- --- ---  0.8 Clear AC
  amb  --- 1-- --- --- --- ---  0.7 Transfer AC contents to MBR
  cyr  --- --- --- 110 --- ---  1.6 Cycle AC contents right one binary position (AC 17 -> AC 0)
  shr  --- --- --- 100 --- ---  1.6 Shift AC contents right one binary position (AC 0 unchanged)
  mbl  --- --- --- 01x --- ---  1.4 Transfer MBR contents to LR
  xmb  --- --- --- 0x1 --- ---  1.2 Transfer XR contents to MBR
  com  --- --- --- --- 1-- ---  1.2 Compliment AC
  pad  --- --- --- --- -1- ---  1.5 Parital add MBR to AC (for each MBR one, complement the correp AC bit)
  cry  --- --- --- --- --1 ---  1.7 A carry digit is a one if in the next least sigmificant digit,
                                    either ac=0 and mbr=1 or ac=1 and carry digit=1. The carry digits
                                    so determined are partial added to the AC by cry. pad and cry used
                                    together give a full one's complement addition of C(MBR) to C(AC)
  anb  --- --- --- --- --- 111  1.2-2 And LR and MBR
  orb  --- --- --- --- --- 101  1.3 Or LR into MBR
  lmb  --- --- --- --- --- 01x  1.4 Tranfer LR contents to MBR
  mbx  --- --- --- --- --- 0x1  1.8 Transfer MBR contents to XR
*/
#include "tx0_defs.h"

#define OPR_CLA         0100000 /* 0.8 */
#define OPR_AMB         0040000 /* 0.7 */

#define OPR_SHF_MASK    0000700 /* 1.6 */
#define OPR_CYR         0000600
#define OPR_SHR         0000400

#define OPR_MBL_MASK    0000600 /* 1.4 */
#define OPR_MBL         0000200
#define OPR_XMB_MASK    0000500 /* 1.2 */
#define OPR_XMB         0000100

#define OPR_COM         0000040 /* 1.2 */
#define OPR_PAD         0000020 /* 1.5 */
#define OPR_CRY         0000010 /* 1.7 */

#define OPR_LOG_MASK    0000007 /* Logical operation mask */
#define OPR_ANB         0000007 /* 1.2-2 */
#define OPR_ORB         0000005 /* 1.3 */

#define OPR_LMB_MASK    0000006 /* 1.4 */
#define OPR_LMB         0000002
#define OPR_MBX_MASK    0000005 /* 1.8 */
#define OPR_MBX         0000001

/*
      IN OUT GROUP
      ------------
  nop  --- -00 000 --- --- ---  NOP
  tac  --- -00 001 --- --- ---  1.1
  tbr  --- -00 010 --- --- ---  1.2
  pen  --- -00 011 --- --- ---  1.1
  sel  --- -00 100 --- --- ---  
  spare--- -00 101 --- --- ---  
  rpf  --- -00 110 --- --- ---  1.2
  spf  --- -00 111 --- --- ---  1.6
  exN  --- -01 nnn --- --- ---  IOS
  cpy  --- -10 000 --- --- ---  IOS
  r1l  --- -10 001 --- --- ---  IOS
  dis  --- -10 010 --- --- ---  IOS
  r3l  --- -10 011 --- --- ---  IOS
  prt  --- -10 100 --- --- ---  IOS
  spare--- -10 101 --- --- ---  
  p6h  --- -10 110 --- --- ---  IOS
  p7h  --- -10 111 --- --- ---  IOS
  hlt  --- -11 000 --- --- ---  1.8
  cll  --- -11 001 --- --- ---  0.6
  clr  --- -11 010 --- --- ---  0.6
*/
#define IOS_MASK    0037000
#define IOS_EX_MASK 0030000
#define IOS_NOP     0000000
#define IOS_TAC     0001000
#define IOS_TBR     0002000
#define IOS_PEN     0003000
#define IOS_SEL     0004000
#define IOS_RPF     0006000
#define IOS_SPF     0007000
#define IOS_CPY     0020000
#define IOS_R1L     0021000
#define IOS_DIS     0022000
#define IOS_R3L     0023000
#define IOS_PRT     0024000
#define IOS_P6H     0026000
#define IOS_P7H     0027000
#define IOS_HLT     0030000
#define IOS_CLL     0031000
#define IOS_CLR     0032000

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC
#define UNIT_V_MSIZE    (UNIT_V_UF + 4)                 /* dummy mask */
#define UNIT_V_EXT      (UNIT_V_UF + 2)
#define UNIT_EXT_INST   (1 << UNIT_V_EXT)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

#define HIST_PC         0x40000000
#define HIST_V_SHF      18
#define HIST_MIN        64
#define HIST_MAX        65536

#define TRACE_PRINT(level, args)    if(cpu_dev.dctrl & level) {   \
                                         printf args;             \
                                    }
typedef struct {
    uint32              pc;
    uint32              ir;
    uint32              ovac;
    uint32              pfio;
    uint32              ea;
    uint32              opnd;
} InstHistory;

int32 M[MAXMEMSIZE] = { 0 };                            /* memory */
int32 AC = 0;                                           /* AC */
int32 IR = 0;                                           /* IR */
int32 PC = 0;                                           /* PC */
int32 MAR = 0;                                          /* MAR */
int32 XR = 0;                                           /* XR (index register) */
int32 MBR = 0;                                          /* MBR */
int32 LR = 0;                                           /* LR (Live Register) */
int32 OV = 0;                                           /* overflow */
int32 TBR = 0;                                          /* sense switches */
int32 PF = 0;                                           /* program flags */
int32 TAC = 0;                                          /* Toggle Switch Accumulator */
int32 iosta = 0;                                        /* status reg */
int32 ios = 0;                                          /* I/O Stop */
int32 ch = 0;                                           /* Chime Alarm */
int32 LPEN = 0;                                         /* Light Pen / Light Gun flops */
int32 mode_tst = 1;                                     /* Test Mode Flip-flop */
int32 mode_rdin = 1;                                    /* Read-In Mode Flip-flop */

uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* inst history */

int32 fpc_MA;                                           /* shadow ma for FPC access */
int32 fpc_OP;                                           /* shadow op for FPC access */

int32 addr_mask = YMASK;

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
int32 cpu_get_mode (void);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_ext (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_noext (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat Read (void);
t_stat Write (void);

extern int32 petr (int32 inst, int32 dev, int32 dat);
extern int32 ptp (int32 inst, int32 dev, int32 dat);
extern int32 tti (int32 inst, int32 dev, int32 dat);
extern int32 tto (int32 inst, int32 dev, int32 dat);
extern int32 lpt (int32 inst, int32 dev, int32 dat);
extern int32 dt  (int32 inst, int32 dev, int32 dat);
extern int32 drm (int32 inst, int32 dev, int32 dat);
#ifdef USE_DISPLAY
extern int32 dpy (int32 ac);
#endif

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX | UNIT_BINK | UNIT_EXT_INST | UNIT_MODE_READIN, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATAD (PC, PC, ASIZE, "program counter") },
    { ORDATAD (AC, AC, 18, "accumulator") },
    { ORDATAD (IR, IR, 5, "instruction register (5 bits in Extented Mode,                                  2 bits in standard mode)") },
    { ORDATAD (MAR, MAR, 16, "memory address register") },
    { ORDATAD (XR, XR, 14, "index register (Extended Mode only)") },
    { ORDATAD (MBR, MBR, 18, "memory buffer register") },
    { ORDATAD (LR, LR, 18, "live register") },
    { ORDATAD (TAC, TAC, 18, "toggle switch accumulator") },
    { ORDATAD (TBR, TBR, 18, "toggle switch buffer register") },
    { ORDATA (PF, PF, 18) },
    { BRDATA (PCQ, pcq, 8, ASIZE, PCQ_SIZE), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { FLDATAD (IOS, ios, 0, "in out stop") },       /* In Out Stop */
    { FLDATAD (CH, ch, 0, "chime alarm") },         /* Chime Alarm */
    { ORDATAD (LP, LPEN, 2, "light pen") },       /* Light Pen */
    { FLDATA (R, mode_rdin, 0), REG_HRO },      /* Mode "R" (Read In) Flip-Flop */
    { FLDATA (T, mode_tst, 0), REG_HRO },       /* Mode "T" (Test) Flip-Flop */
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_EXT_INST, 0, "standard CPU", "TX0STD", &cpu_set_noext },
    { UNIT_EXT_INST, UNIT_EXT_INST, "Extended Instruction Set", "TX0EXT", &cpu_set_ext },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
    { UNIT_MODE, 0, "NORMAL", "NORMAL", &cpu_set_mode },
    { UNIT_MODE, UNIT_MODE_TEST, "TEST", "TEST", &cpu_set_mode },
    { UNIT_MODE, UNIT_MODE_READIN, "READIN", "READIN", &cpu_set_mode }, 
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

/* Debug flags */
#define ERROR_MSG       (1 << 0)
#define TRACE_MSG       (1 << 1)
#define STO_MSG         (1 << 2)
#define ADD_MSG         (1 << 3)
#define TRN_MSG         (1 << 4)
#define ORD_MSG         (1 << 5)
#define IOS_MSG         (1 << 6)
#define READIN_MSG      (1 << 7)
#define VERBOSE_MSG     (1 << 8)
#define COUNTERS_MSG    (1 << 9)

/* Debug Flags */
static DEBTAB cpu_dt[] = {
    { "ERROR",  ERROR_MSG },
    { "TRACE",  TRACE_MSG },
    { "STO",    STO_MSG },
    { "ADD",    ADD_MSG },
    { "TRN",    TRN_MSG },
    { "ORD",    ORD_MSG },
    { "IOS",    IOS_MSG },
    { "READIN", READIN_MSG },
    { "VERBOSE",VERBOSE_MSG },
    { "COUNTERS",COUNTERS_MSG },
    { NULL,     0 }
};

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, ASIZE, 1, 8, 18,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, ERROR_MSG,
    cpu_dt, NULL
    };

int32 compute_index (int32 y, int32 XR)
{
    int32 sum;

    y  &= YMASK;    /* force 13-bit (0 sign) */
    XR &= 037777;   /* force 14-bit */

    sum = y + XR;

    if (sum > 037777) { /* Carry from bit 4 into bit 17. */
        sum += 1;
    }

    sum &= YMASK;       /* truncate to 13-bit */

    return (sum);
}

/* CPU Instruction usage counters */
typedef struct {
/* Store group */
    int32 sto, stx, sxa, ado, slr, slx, stz;
/* Add group */
    int32 add, adx, ldx, aux, llr, llx, lda, lax;
/* TRN Group */
    int32 trn, tze, tsx, tix, tra, trx, tlv;
/* OPR Group */
    int32 cla, amb, cyr, shr, mbl, xmb, com, pad, cry, anb, orb, lmb, mbx;
} INST_CTRS;

INST_CTRS inst_ctr;


void tx0_dump_regs(const char *desc)
{
    TRACE_PRINT(TRACE_MSG, ("%s: AC=%06o, MAR=%05o, MBR=%06o, LR=%06o, XR=%05o\n", desc, AC, MAR, MBR, LR, XR));

    /* Check regs sanity */
    if (AC > DMASK) {
        sim_printf("Error: AC > DMASK\n");
    }
    if (MBR > DMASK) {
        sim_printf("Error: MBR > DMASK\n");
    }
    if (LR > DMASK) {
        sim_printf("Error: LR > DMASK\n");
    }
    if (!MEM_ADDR_OK(MAR)) {
        sim_printf("Error: MAR > %06o\n", MEMSIZE);
    }

}

t_stat sim_opr_orig(int32 op);

t_stat sim_instr (void)
{
    int32 IR, op, inst_class, y;
    int32 tempLR;   /* LR temporary storage in case both LMB and MBL are set (swap LR<->MBR) */
    t_stat reason;

    /* Clear Instruction counters */
    inst_ctr.sto = inst_ctr.stx = inst_ctr.sxa = inst_ctr.ado = inst_ctr.slr = inst_ctr.slx = inst_ctr.stz = 0;
    inst_ctr.add = inst_ctr.adx = inst_ctr.ldx = inst_ctr.aux = inst_ctr.llr = inst_ctr.llx = inst_ctr.lda = inst_ctr.lax = 0;
    inst_ctr.trn = inst_ctr.tze = inst_ctr.tsx = inst_ctr.tix = inst_ctr.tra = inst_ctr.trx = inst_ctr.tlv = 0;
    inst_ctr.cla = inst_ctr.amb = inst_ctr.cyr = inst_ctr.shr = inst_ctr.mbl = inst_ctr.xmb = inst_ctr.com = inst_ctr.pad = inst_ctr.cry = inst_ctr.anb = inst_ctr.orb = inst_ctr.lmb = inst_ctr.mbx = 0;

    #define INCR_ADDR(x)    ((x+1) & (MEMSIZE-1))

    /* Main instruction fetch/decode loop: check events */

    reason = 0;
    while (reason == 0) {                                   /* loop until halted */

        if (sim_interval <= 0) {                            /* check clock queue */
            reason = sim_process_event ();
            if (reason != SCPE_OK)
                break;
        }

        if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
            reason = STOP_IBKPT;                            /* stop simulation */
            break;
        }

        if (ios) {
            TRACE_PRINT(ERROR_MSG, ("I/O Stop - Waiting...\n"));
            continue;
        }

        /* Handle Instruction Execution in TEST and READIN modes */
        if (mode_tst) { /* Test Mode / Readin mode */
            if (mode_rdin) { /* Readin Mode */
                reason = SCPE_OK;   /* Default is to continue reading, and transfer control when done. */
                AC = petr(3,0,0);   /* Read three chars from tape into AC */
                MAR = AC & AMASK;   /* Set memory address */
                IR = AC >> 16;

                if (!MEM_ADDR_OK(MAR)) {
                    TRACE_PRINT(ERROR_MSG, ("READIN: Tape address out of range.\n"));
                    reason = SCPE_FMT;
                }

                switch (IR) {
                    case 00:    /* Storage (sto x) */
                    case 03:    /* Storage (opr x) */
                        MBR = petr(3,0,0);  /* Read three characters from tape. */
                        TRACE_PRINT(READIN_MSG, ("READIN: sto @%06o = %06o\n", MAR, MBR));
                        Write();
                        break;
                    case 02:    /* Transfer Control (trn x) Start Execution */
                        PC = MAR;
                        reason = SCPE_OK;   /* let SIMH start execution. */
                        TRACE_PRINT(READIN_MSG, ("READIN: trn %06o (Start Execution)\n", PC));
                        reason = cpu_set_mode(&cpu_unit, 0, NULL, NULL);
                        break;
                    case 01:    /* Transfer (add x) - Halt */
                        PC = MAR;
                        reason = SCPE_STOP; /* let SIMH halt. */
                        TRACE_PRINT(READIN_MSG, ("READIN: add %06o (Halt)\n", PC));
                        reason = cpu_set_mode(&cpu_unit, 0, NULL, NULL);
                        break;
                    default:
                        reason = SCPE_IERR;
                        break;
                }
            } else if (mode_tst) {  /* Test mode not implemented yet. */
                TRACE_PRINT(ERROR_MSG, ("TEST Mode not implemented.\n"));
                reason = SCPE_STOP;

            } else {
                TRACE_PRINT(ERROR_MSG, ("Invalid CPU mode.\n"));
                reason = SCPE_IERR;
            }
            continue;   /* Proceed with next instruction */
        }

        /* Fetch, decode instruction in NORMAL mode */
        MAR = PC;
        if (Read ()) break;                                 /* fetch inst */
     
        IR = (MBR >> 13);                                   /* save in IR */
        inst_class = IR >> 3;
        op = MBR & AMASK;
        y = MBR & YMASK;
        sim_interval = sim_interval - 1;

        if ((cpu_unit.flags & UNIT_EXT_INST) == 0) {  /* Original instruction set */
            IR &= 030;
            MAR = MBR & AMASK; /* 16-bit address field */
        } else {
            MAR = MBR & YMASK; /* 13-bit address field */
        }

        if (hst_lnt) {                                      /* history enabled? */
            hst_p = (hst_p + 1);                            /* next entry */
            if (hst_p >= hst_lnt) hst_p = 0;
            hst[hst_p].pc = MAR | HIST_PC;                   /* save state */
            hst[hst_p].ir = IR;
            hst[hst_p].ovac = (OV << HIST_V_SHF) | AC;
        }

        PC = INCR_ADDR (PC);                                /* increment PC */

#ifdef USE_FPC
        fpc_OP = op;                                        /* shadow opcode for FPC */
#endif

        tx0_dump_regs("START");
        
        switch (inst_class) {                               /* decode IR<0:1> */

        /* Logical, load, store instructions */
        case 00:                                            /* sto x */
            switch (IR & 07) {
                case 0:     /* sto */
                    MBR = AC;
                    Write();
                    inst_ctr.sto++;
                    break;
                case 1:     /* stx */
                    MBR = AC;
                    MAR = compute_index(y, XR);
                    Write();
                    inst_ctr.stx++;
                    break;
                case 2:     /* sxa */
                    {
                        int32 temp = M[MAR];
                        temp &= 0760000;
                        temp |= (XR & YMASK);
                        MBR = temp;
                        Write();
                    }
                    inst_ctr.sxa++;
                    break;
                case 3:     /* ado */
                    {
                        int32 temp = M[MAR];
                        temp += 1;              /* add 1 */
                        if (temp > DMASK) {     /* Overflow, */
                            temp += 1;          /* propagate carry from bit 0 to bit 17. */
                        }
                        temp &= DMASK;
                        MBR = temp;
                        AC = temp;
                        Write();
                    }
                    inst_ctr.ado++;
                    break;
                case 4:     /* slr */
                    MBR = LR;
                    Write();
                    inst_ctr.slr++;
                    break;
                case 5:     /* slx */
                    MAR = compute_index(y, XR);
                    MBR = LR;
                    Write();
                    inst_ctr.slx++;
                    break;
                case 6:     /* stz */
                    MBR = 0;
                    Write();
                    inst_ctr.stz++;
                    break;
                case 7:     /* no-op */
                    break;
            }
            break;

        case 01:                                            /* add x */
            switch (IR & 07) {
                case 0:     /* add */
                    Read();
                    AC = AC + MBR;
                    if (AC > DMASK) {
                        AC += 1;
                    }
                    AC &= DMASK;
                    inst_ctr.add++;
                    break;
                case 1:     /* adx */
                    MAR = compute_index(y, XR);
                    Read();
                    AC = AC + MBR;
                    if (AC > DMASK) {
                        AC += 1;
                    }
                    AC &= DMASK;
                    inst_ctr.adx++;
                    break;
                case 2:     /* ldx */
                    Read();
                    XR  = MBR & YMASK;         /* load XR[5:17] from C(y[5:17]) */
                    XR |= ((MBR & SIGN) >> 4); /* Load XR[4] from C(y[0]) */
                    inst_ctr.ldx++;
                    break;
                case 3:     /* aux  (Augment Index) */
                    {
                        uint32 newY = (y & 0017777) | ((y & SIGN) >> 4);
                        TRACE_PRINT(ADD_MSG, ("[%06o] AUX: y=%05o, XR=%05o = ", PC-1, newY, XR));
                        XR = XR + newY;
                        TRACE_PRINT(ADD_MSG, ("%05o\n", XR));
                        inst_ctr.aux++;
                        break;
                    }
                case 4:     /* llr (Load Live Register) */
                    Read();
                    LR = MBR;
                    inst_ctr.llr++;
                    break;
                case 5:     /* llx (Load Live Register, Indexed) */
                    MAR = compute_index(y, XR);
                    Read();
                    LR = MBR;
                    inst_ctr.llx++;
                    break;
                case 6:     /* lda (Load Accumulator) */
                    Read();
                    AC = MBR;
                    inst_ctr.lda++;
                    break;
                case 7:     /* lax (Load Accumulator, Indexed) */
                    MAR = compute_index(y, XR);
                    Read();
                    AC = MBR;
                    inst_ctr.lax++;
                    break;
            }
            break;

        case 02:                                            /* trn x */
            switch (IR & 07) {
                case 0:     /* trn (Transfer on Negative AC) */
                    if (AC & SIGN) {
                        TRACE_PRINT(TRN_MSG, ("[%06o] TRN: Transfer taken: PC=%06o\n", PC-1, y));
                        PC = MAR;
                    }
                    inst_ctr.trn++;
                    break;
                case 1:     /* tze (Transfer on +/- Zero) */
                    if ((AC == 0777777) || (AC == 0000000)) {
                        TRACE_PRINT(TRN_MSG, ("[%06o] TZE: Transfer taken: PC=%06o\n", PC-1, y));
                        PC = y;
                    }
                    inst_ctr.tze++;
                    break;
                case 2:     /* tsx (Transfer and set Index) */
                    XR = PC & 0017777;  /* XR[4] = 0; */
                    TRACE_PRINT(TRN_MSG, ("[%06o] TSX: PC=%06o, XR=%05o\n", PC-1, y, XR));
                    PC = y;
                    inst_ctr.tsx++;
                    break;
                case 3:     /* tix (Transfer and Index) */
                    TRACE_PRINT(TRN_MSG, ("[%06o] TIX: XR=%05o\n", PC-1, XR));
                    if ((XR == 037777) || (XR == 000000)) { /* +/- 0, take next instruction */
                        TRACE_PRINT(TRN_MSG, ("+/- 0, transfer not taken.\n"));
                    } else { /* Not +/- 0 */
                        if (XR & 0020000) { /* XR[4] == 1 */
                            TRACE_PRINT(TRN_MSG, ("XR is negative, transfer taken,"));
                            XR ++;
                        } else { /* XR[4] = 0 */
                            TRACE_PRINT(TRN_MSG, ("XR is positive, transfer taken,"));
                            XR --;
                        }
                        PC = y;
                        XR &= 037777;
                        TRACE_PRINT(TRN_MSG, (" PC=%06o, XR=%05o\n", PC, XR));
                        
                    }
                    inst_ctr.tix++;
                    break;
                case 4:     /* tra (Unconditional Transfer) */
                    TRACE_PRINT(TRN_MSG, ("[%06o] TRA: Transfer taken: PC=%06o\n", PC-1, y));
                    PC = y;
                    inst_ctr.tra++;
                    break;
                case 5:     /* trx */
                    {
                        int32 newPC;
                        newPC = compute_index(y, XR);
                        TRACE_PRINT(TRN_MSG, ("[%06o] TRA: Transfer taken: PC=%06o\n", PC-1, newPC));
                        PC = newPC;
                    }
                    inst_ctr.trx++;
                    break;
                case 6:     /* tlv (Transfer on External Level) */
                    TRACE_PRINT(ERROR_MSG, ("[%06o] TODO: Implement TLV\n", PC-1));
                    inst_ctr.tlv++;
                    break;
                case 7:     /* no-op */
                    break;
            }
            break;

        case 03:                                            /* opr x */
            if ((cpu_unit.flags & UNIT_EXT_INST) == 0) {  /* Original instruction set */
                reason = sim_opr_orig(op);
                break;
            }

            /* I can't find this mentioned in the TX-0 Documentation, but for the
             * lro and xro instructions, this must be needed.
             */
            MBR = 0;

/* Cycle 0 */
            if (op & OPR_AMB) { /* 0.7 */
                inst_ctr.amb++;
                MBR = AC;
                TRACE_PRINT(ORD_MSG, ("[%06o]: AMB: MBR=%06o\n", PC-1, MBR));
            }

            if (op & OPR_CLA) { /* 0.8 */
                inst_ctr.cla++;
                AC = 0;
                TRACE_PRINT(ORD_MSG, ("[%06o]: CLA: AC=%06o\n", PC-1, AC));
            }

/* IOS - In / Out Stop */
            /* Check TTI for character.  If so, put in LR and set LR bit 0. */
            if (iosta & IOS_TTI) {
                int32 rbuf;
                rbuf = tti(0,0,0);
                TRACE_PRINT(IOS_MSG, ("TTI: character received=%03o\n", rbuf &077));
                LR &= 0266666; /* Clear bits 0,2,5,8,...,17 */

                LR |= SIGN; /* Set bit 0, character available. */
                LR |= ((rbuf & 001) >> 0) << 15;/* bit 2  */
                LR |= ((rbuf & 002) >> 1) << 12;/* bit 5  */
                LR |= ((rbuf & 004) >> 2) << 9; /* bit 8  */
                LR |= ((rbuf & 010) >> 3) << 6; /* bit 11 */
                LR |= ((rbuf & 020) >> 4) << 3; /* bit 14 */
                LR |= ((rbuf & 040) >> 5) << 0; /* bit 17 */
            }

            switch(op & IOS_MASK) {
                case IOS_NOP:
                    break;
                case IOS_TAC:
                    TRACE_PRINT(IOS_MSG, ("[%06o] TAC %06o\n", PC-1, TAC));
                    AC |= TAC;
                    break;
                case IOS_TBR:
                    TRACE_PRINT(IOS_MSG, ("[%06o] TBR %06o\n", PC-1, TBR));
                    MBR |= TBR;
                    break;
                case IOS_PEN:
                    TRACE_PRINT(IOS_MSG, ("[%06o] Light Pen %01o\n", PC-1, LPEN));
                    AC &= AMASK;
                    AC |= (LPEN & 1) << 17;
                    AC |= (LPEN & 2) << 16;
                    AC &= DMASK;
                    break;
                case IOS_SEL:
                    { /* These are used for Magtape control.
                         Magtape is compatible with IBM 709.  Maybe the SIMH 7090 magtape can be leveraged. */
                        int32 CLRA = (op & 0100000);
                        int32 BINDEC = (op & 020);
                        int32 device = op & 03;
                        int32 tape_ord = (op >> 2) & 03;
                        const char *tape_cmd[] = {"Backspace Tape", "Read/Select Tape", "Rewind Tape", "Write/Select Tape" };

                        TRACE_PRINT(ERROR_MSG, ("[%06o] TODO: SEL (magtape)\n", PC-1));
                        sim_printf("Device %d: CLRA=%d, BINDEC=%d: %s\n", device, CLRA, BINDEC, tape_cmd[tape_ord]);
                    }
                    break;
                case IOS_RPF: /* These are used for Magtape control. */
                    TRACE_PRINT(IOS_MSG, ("[%06o] RPF %06o\n", PC-1, PF));
                    MBR |= PF;
                    break;
                case IOS_SPF: /* These are used for Magtape control. */
                    TRACE_PRINT(IOS_MSG, ("[%06o] SPF %06o\n", PC-1, MBR));
                    PF = MBR;
                    break;
                case IOS_CPY: /* These are used for Magtape control. */
                    TRACE_PRINT(ERROR_MSG, ("[%06o] TODO: CPY\n", PC-1));
                    break;
                case IOS_R1L:
                    AC &= 0333333; /* Clear bits 0,3,6,9,12,15 */
                    AC |= petr(1, 0, 0); /* Read one line from PETR */
                    break;
                case IOS_DIS:
#ifdef USE_DISPLAY
                    LPEN = dpy (AC);  /* Display point on the CRT */
#endif /* USE_DISPLAY */
                    break;
                case IOS_R3L:
                    AC = petr(3, 0, 0); /* Read three lines from PETR */
                    break;
                case IOS_PRT:
                    {
                        uint32 tmpAC = 0;
                        tmpAC |= ((AC & 0000001) >> 0) << 0; /* bit 17 */
                        tmpAC |= ((AC & 0000010) >> 3) << 1; /* bit 14 */
                        tmpAC |= ((AC & 0000100) >> 6) << 2; /* bit 11 */
                        tmpAC |= ((AC & 0001000) >> 9) << 3; /* bit 8 */
                        tmpAC |= ((AC & 0010000) >> 12) << 4; /* bit 5 */
                        tmpAC |= ((AC & 0100000) >> 15) << 5; /* bit 2 */
                        tto (0, 0, tmpAC & 077);    /* Print one character on TTO */
                    }
                    break;
                case IOS_P6H:
                case IOS_P7H:
                    {
                        uint32 tmpAC = 0;
                        tmpAC |= ((AC & 0000001) >> 0) << 0; /* bit 17 */
                        tmpAC |= ((AC & 0000010) >> 3) << 1; /* bit 14 */
                        tmpAC |= ((AC & 0000100) >> 6) << 2; /* bit 11 */
                        tmpAC |= ((AC & 0001000) >> 9) << 3; /* bit 8 */
                        tmpAC |= ((AC & 0010000) >> 12) << 4; /* bit 5 */
                        tmpAC |= ((AC & 0100000) >> 15) << 5; /* bit 2 */
                        tmpAC &= 0077;
                        if ((op & IOS_MASK) == IOS_P7H) {
                            tmpAC |= 0100;  /* Punch 7th hole. */
                            TRACE_PRINT(ERROR_MSG, ("[%06o] Punch 7 holes\n", PC-1));
                        } else {
                            TRACE_PRINT(ERROR_MSG, ("[%06o] Punch 6 holes\n", PC-1));
                        }
                        ptp (0, 0, tmpAC);    /* Punch character on PTP */
                    }
                    break;
                case IOS_HLT:
                    TRACE_PRINT(IOS_MSG, ("[%06o] HALT Instruction\n", PC-1));
                    reason = STOP_HALT;
                    break;
                case IOS_CLL:
                    AC &= 0000777;
                    break;
                case IOS_CLR:
                    AC &= 0777000;
                    break;
                default: /* Could be ex0-ex7, handle them here. */
                    if ((op & IOS_EX_MASK) == 0010000) {
                        TRACE_PRINT(ERROR_MSG, ("[%06o] TODO: EX%o\n", PC-1, (op >> 9) & 07));
                    }
                    break;
            }

/* Cycle 1 */
            if (op & OPR_COM) { /* 1.2 */
                AC = ~AC;
                AC &= DMASK;
                TRACE_PRINT(ORD_MSG, ("[%06o]: COM: AC=%06o\n", PC-1, AC));
                inst_ctr.com++;
            }

            if ((op & OPR_XMB_MASK) == OPR_XMB) { /* 1.2 XR[5:17] -> MBR[5:17], XR[4] -> MBR[0:4] */
                int32 bit14 = (XR >> 13) & 1;
                MBR  = XR & YMASK;      /* XR[5:17] -> MBR[5:17] */
                MBR |= (bit14 << 17);   /* XR[4] -> MBR[0] */
                MBR |= (bit14 << 16);   /* XR[4] -> MBR[1] */
                MBR |= (bit14 << 15);   /* XR[4] -> MBR[2] */
                MBR |= (bit14 << 14);   /* XR[4] -> MBR[3] */
                MBR |= (bit14 << 13);   /* XR[4] -> MBR[4] */

                TRACE_PRINT(ORD_MSG, ("[%06o]: XMB: XR=%05o, MBR=%06o\n", PC-1, XR, MBR));
                inst_ctr.xmb++;
            }

            if ((op & OPR_LOG_MASK) == OPR_ANB) { /* 1.2-2 */
                MBR &= LR;
                TRACE_PRINT(ORD_MSG, ("[%06o]: ANB: MBR=%06o\n", PC-1, MBR));
                inst_ctr.anb++;
            }

            if ((op & OPR_LOG_MASK) == OPR_ORB) { /* 1.3 */
                MBR |= LR;
                TRACE_PRINT(ORD_MSG, ("[%06o]: ORB: MBR=%06o\n", PC-1, MBR));
                inst_ctr.orb++;
            }

            tempLR = LR; /* LR temporary storage in case both LMB and MBL are set (swap LR<->MBR) */
            if ((op & OPR_MBL_MASK) == OPR_MBL) { /* 1.4 */
                LR = MBR;
                TRACE_PRINT(ORD_MSG, ("[%06o]: MBL: LR=%06o, prev LR=%06o\n", PC-1, LR, tempLR));
                inst_ctr.mbl++;
            }

            if ((op & OPR_LMB_MASK) == OPR_LMB) { /* 1.4 */
                MBR = tempLR;
                TRACE_PRINT(ORD_MSG, ("[%06o]: LMB: LR=%06o, MBR=%06o\n", PC-1, LR, MBR));
                inst_ctr.lmb++;
            }

            if (op & OPR_PAD) { /* 1.5 Partial Add (XOR): AC = MBR ^ AC */
                if (op & OPR_CRY) { /* 1.7 */
                    TRACE_PRINT(ORD_MSG, ("[%06o] PAD+CRY: AC=%06o, MBR=%06o = ", PC-1, AC, MBR));
                    AC = AC + MBR;
                    if (AC > DMASK) {
                        AC += 1;
                    }
                    AC &= DMASK;
                    TRACE_PRINT(ORD_MSG, ("%06o\n", AC));
                } else {
                    TRACE_PRINT(ORD_MSG, ("[%06o] PAD: AC=%06o, MBR=%06o\n", PC-1, AC, MBR)); 
                    AC = AC ^ MBR;
                    AC &= DMASK;
                    TRACE_PRINT(ORD_MSG, ("[%06o] PAD: Check: AC=%06o\n", PC-1, AC)); 
                }
                inst_ctr.pad++;
            }

            if ((op & OPR_SHF_MASK) == OPR_CYR) { /* 1.6 */
                int32 bit17;
                bit17 = (AC & 1) << 17;
                AC  >>= 1;
                AC |= bit17;
                TRACE_PRINT(ORD_MSG, ("[%06o]: CYR: AC=%06o\n", PC-1, AC));
                inst_ctr.cyr++;
            }

            if ((op & OPR_SHF_MASK) == OPR_SHR) { /* 1.6 Shift AC Right, preserve bit 0. */
                int32 bit0;
                bit0 = AC & 0400000;
                AC = AC >> 1;
                AC |= bit0;
                TRACE_PRINT(ORD_MSG, ("[%06o]: SHR: AC=%06o\n", PC-1, AC));
                inst_ctr.shr++;
            }

            if (op & OPR_CRY) { /* 1.7 */
                if (op & OPR_PAD) {
                } else {
                    TRACE_PRINT(ERROR_MSG, ("[%06o] CRY: TODO: AC=%06o\n", PC-1, AC)); 
                    inst_ctr.cry++;
                }
            }

            if ((op & OPR_MBX_MASK) == OPR_MBX) { /* 1.8    MBR[5:17] -> XR[5:17], MBR[0] -> XR[4] */
                int32 tempXR;
                tempXR  = MBR & YMASK;
                tempXR |= (((MBR >> 17) & 1) << 13);

                XR = tempXR;
                TRACE_PRINT(ORD_MSG, ("[%06o]: MBX: MBR=%06o, XR=%06o\n", PC-1, MBR, XR));
                inst_ctr.mbx++;
            }
        }

        tx0_dump_regs("END");

#ifdef USE_FPC
        fpc_MA = MAR;                                        /* shadow MAR for FPC */
#endif

    } /* end while */
    pcq_r->qptr = pcq_p;                                    /* update pc q ptr */

    TRACE_PRINT(COUNTERS_MSG, ("Instruction Counters\nSTO=%d, STX=%d, SXA=%d, ADO=%d, SLR=%d, SLX=%d, STZ=%d\n",
        inst_ctr.sto, inst_ctr.stx, inst_ctr.sxa, inst_ctr.ado, inst_ctr.slr, inst_ctr.slx, inst_ctr.stz));
    TRACE_PRINT(COUNTERS_MSG, ("ADD=%d, ADX=%d, LDX=%d, AUX=%d, LLR=%d, LLX=%d, LDA=%d, LAX=%d\n",
        inst_ctr.add, inst_ctr.adx, inst_ctr.ldx, inst_ctr.aux, inst_ctr.llr, inst_ctr.llx, inst_ctr.lda, inst_ctr.lax));
    TRACE_PRINT(COUNTERS_MSG, ("TRN=%d, TZE=%d, TSX=%d, TIX=%d, TRA=%d, TRX=%d, TLV=%d\n",
        inst_ctr.trn, inst_ctr.tze, inst_ctr.tsx, inst_ctr.tix, inst_ctr.tra, inst_ctr.trx, inst_ctr.tlv));
    TRACE_PRINT(COUNTERS_MSG, ("CLA=%d, AMB=%d, CYR=%d, SHR=%d, MBL=%d, XMB=%d, COM=%d, PAD=%d, CRY=%d, ANB=%d, ORB=%d, LMB=%d, MBX=%d\n",
        inst_ctr.cla, inst_ctr.amb, inst_ctr.cyr, inst_ctr.shr, inst_ctr.mbl, inst_ctr.xmb, inst_ctr.com, inst_ctr.pad, inst_ctr.cry, inst_ctr.anb, inst_ctr.orb, inst_ctr.lmb, inst_ctr.mbx));

    return reason;
}

/* Read and write memory */
t_stat Read (void)
{
    MAR &= (MEMSIZE - 1);
    MBR = M[MAR];
    MBR &= DMASK;
    return SCPE_OK;
}

t_stat Write (void)
{
    MAR &= (MEMSIZE - 1);
    MBR &= DMASK;
    M[MAR] = MBR;
    return SCPE_OK;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
    ios = 0;
    PF = 0;
    MAR = 0;
    MBR = 0;
    pcq_r = find_reg ("PCQ", NULL, dptr);

    if (pcq_r) {
        pcq_r->qptr = 0;
    } else {
        return SCPE_IERR;
    }

    sim_brk_types = sim_brk_dflt = SWMASK ('E');
    
    return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE) return SCPE_NXM;
    if (vptr != NULL) *vptr = M[addr] & DMASK;

    return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
    if (addr >= MEMSIZE) return SCPE_NXM;

    M[addr] = val & DMASK;

    return SCPE_OK;
}

/* Change memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32 mc = 0;
    uint32 i;

    if ((val <= 0) || (val > (int32)MAXMEMSIZE) || ((val & 07777) != 0))
        return SCPE_ARG;
    for (i = val; i < MEMSIZE; i++) mc = mc | M[i];
    if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    MEMSIZE = val;
    for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0;
    return SCPE_OK;
}

/* Change CPU Mode (Normal, Test, Readin) */

t_stat cpu_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    if (val == UNIT_MODE_TEST) {
        mode_tst = 1;
        mode_rdin = 0;
    } else if (val == UNIT_MODE_READIN) {
        mode_tst = 1;
        mode_rdin = 1;
    } else {    /* Normal Mode */
        mode_tst = 0;
        mode_rdin = 0;
    }

    return SCPE_OK;
}


/* Set TX-0 with Extended Instruction Set */

t_stat cpu_set_ext (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    sim_printf("Set CPU Extended Mode\n");
    return SCPE_OK;
}

t_stat cpu_set_noext (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    sim_printf("Set CPU Non-Extended Mode\n");
    return SCPE_OK;
}

int32 cpu_get_mode (void)
{
    return (cpu_unit.flags & UNIT_EXT_INST);
}



/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++) hst[i].pc = 0;
    hst_p = 0;
    return SCPE_OK;
    }
lnt = (int32) get_uint (cptr, 10, HIST_MAX, &r);
if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN))) return SCPE_ARG;
hst_p = 0;
if (hst_lnt) {
    free (hst);
    hst_lnt = 0;
    hst = NULL;
    }
if (lnt) {
    hst = (InstHistory *) calloc (lnt, sizeof (InstHistory));
    if (hst == NULL) return SCPE_MEM;
    hst_lnt = lnt;
    }
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 ov, pf, op, k, di, lnt;
const char *cptr = (const char *) desc;
t_stat r;
InstHistory *h;

if (hst_lnt == 0) return SCPE_NOFNC;                    /* enabled? */
if (cptr) {
    lnt = (int32) get_uint (cptr, 10, hst_lnt, &r);
    if ((r != SCPE_OK) || (lnt == 0)) return SCPE_ARG;
    }
else lnt = hst_lnt;
di = hst_p - lnt;                                       /* work forward */
if (di < 0) di = di + hst_lnt;
fprintf (st, "PC      OV AC     IO      PF EA      IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        ov = (h->ovac >> HIST_V_SHF) & 1;               /* overflow */
        pf = 0;
        op = ((h->ir >> 13) & 037);                     /* get opcode */
        fprintf (st, "%06o  %o  %06o %06o %03o ",
            h->pc & AMASK, ov, h->ovac & DMASK, h->pfio & DMASK, pf);
        if ((op < 032) && (op != 007))                  /* mem ref instr */
            fprintf (st, "%06o  ", h->ea);
        else fprintf (st, "        ");
        sim_eval[0] = h->ir;
        if ((fprint_sym (st, h->pc & AMASK, sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "(undefined) %06o", h->ir);
        else if (op < 030)                              /* mem ref instr */
            fprintf (st, " [%06o]", h->opnd);
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}

#ifdef USE_DISPLAY
#include "display/display.h"      /* prototypes */

/* set "test switches"; from display code */
void
cpu_set_switches(unsigned long v1, unsigned long v2)
{
    /* just what we want; smaller CPUs might want to shift down? */
    TAC = v1 ^ v2;
}

void
cpu_get_switches(unsigned long *p1, unsigned long *p2)
{
    *p1 = TAC;
    *p2 = 0;
}
#endif

t_stat sim_load(FILE *fileref, CONST char *cptr, CONST char *fnam, int flag) {
    uint32 word;
    t_addr j, lo, hi, sz, sz_words;
    CONST char *result;

    if (flag) { /* Dump to file. */
        result = get_range(NULL, cptr, &lo, &hi, 8, 0xFFFF, 0);
        if (result == NULL) return SCPE_ARG;

        for (j = lo; j <= hi; j++) {
            if (sim_fwrite(&j, 4, 1, fileref) == 0) return SCPE_IOERR;
            if (sim_fwrite(&M[j], 4, 1, fileref) == 0) return SCPE_IOERR;
        }
    } else {
        lo = strtotv(cptr, &result, 8) & 0xFFFF;
        sz = sim_fsize(fileref);
        sz_words = MIN (sz, sizeof (M)) / 4;
        for (j = lo; j < sz_words; j++) {
            sim_fread(&word, 4, 1, fileref);
            M[j] = word;
        }
    }

    sim_printf("%d words %s [%06o - %06o].\n", j - lo, flag ? "dumped" : "loaded", lo, j-1);

    return SCPE_OK;
}

/*
Original Operate-class instruction micro orders for the 1956 TX-0 Instruction Set

      Operate Fields
      --------------
      --1 --- --- --- --- ---   CLL     0.8
      --- 1-- --- --- --- ---   CLR     0.8
      --- -10 --- --- --- ---   IOS     0.8
      --- -11 --- --- --- ---   HLT     1.8
      --- --- 111 --- --- ---   P7H     0.8
      --- --- 110 --- --- ---   P6H     0.8
      --- --- 100 --- --- ---   PNT     0.8
      --- --- 001 --- --- ---   R1C     0.8
      --- --- 011 --- --- ---   R3C     0.8
      --- --- 010 --- --- ---   DIS     0.8
      --- --- --- 10- --- ---   SHR     1.4
      --- --- --- 11- --- ---   CYR     1.4
      --- --- --- 01- --- ---   MLR     1.3
      --- --- --- --1 --- 0--   PEN     1.1
      --- --- --- --0 --- 1--   TAC     1.1
      --- --- --- --- 1-- ---   COM     1.2
      --- --- --- --- -1- ---   PAD     1.4
      --- --- --- --- --1 ---   CRY     1.7
      --- --- --- --- --- -01   AMB     1.2 AC -> MBR
      --- --- --- --- --- -11   TBR     1.2 TBR -> MBR
      --- --- --- --- --- -10   LMB     1.3 LR -> MBR
*/
#define OOPR_CLL            0100000
#define OOPR_CLR            0040000
#define OOPR_IOS            0020000
#define OOPR_HLT            0030000
#define OOPR_IOS_MASK   0007000
#define OOPR_P7H            0007000
#define OOPR_P6H            0006000
#define OOPR_PNT            0004000
#define OOPR_R3C            0003000
#define OOPR_DIS            0002000
#define OOPR_R1C            0001000

#define OOPR_SHF_MASK   0000600
#define OOPR_SHR            0000400
#define OOPR_CYR            0000600
#define OOPR_MLR            0000200

#define OOPR_PEN_MASK   0000104
#define OOPR_PEN        0000100

#define OOPR_TAC_MASK   0000104
#define OOPR_TAC        0000004

#define OOPR_COM        0000040
#define OOPR_PAD        0000020
#define OOPR_CRY        0000010

#define OOPR_AMB_MASK   0000007
#define OOPR_AMB        0000001
#define OOPR_TBR        0000003
#define OOPR_LMB        0000002

t_stat sim_opr_orig(int32 op)
{
    t_stat reason = SCPE_OK;

    if (op & OOPR_CLL) {    /* cll  0.8 Clear the left nine digital positions of the AC */
        AC &= 0000777;
        TRACE_PRINT(ORD_MSG, ("[%06o]: CLL\n", PC-1));
    }
    if (op & OOPR_CLR) {    /* clr  0.8 Clear the right nine digital positions of the AC */
        AC &= 0777000;
        TRACE_PRINT(ORD_MSG, ("[%06o]: CLR\n", PC-1));
    }

/* IOS - In / Out Stop */
    /* Check TTI for character.  If so, put in LR and set LR bit 0. */
    if (iosta & IOS_TTI) {
        int32 rbuf;
        rbuf = tti(0,0,0);
        TRACE_PRINT(IOS_MSG, ("TTI: character received='%c'\n", rbuf &077));
        sim_printf("TTI: character received='%c'\n", rbuf &077);
        LR &= 0266666; /* Clear bits 0,2,5,8,...,17 */

        LR |= SIGN; /* Set bit 0, character available. */
        LR |= ((rbuf & 001) >> 0) << 15;/* bit 2  */
        LR |= ((rbuf & 002) >> 1) << 12;/* bit 5  */
        LR |= ((rbuf & 004) >> 2) << 9; /* bit 8  */
        LR |= ((rbuf & 010) >> 3) << 6; /* bit 11 */
        LR |= ((rbuf & 020) >> 4) << 3; /* bit 14 */
        LR |= ((rbuf & 040) >> 5) << 0; /* bit 17 */
    }



    if ((op & OOPR_HLT) == OOPR_IOS) {   /* I/O  0.8 IOS */
        TRACE_PRINT(IOS_MSG, ("[%06o] I/O Operation\n", PC-1));

        switch (op & OOPR_IOS_MASK) {
        case OOPR_P7H:
        case OOPR_P6H:
            {
                uint32 tmpAC = 0;
                tmpAC |= ((AC & 0000001) >> 0) << 0; /* bit 17 */
                tmpAC |= ((AC & 0000010) >> 3) << 1; /* bit 14 */
                tmpAC |= ((AC & 0000100) >> 6) << 2; /* bit 11 */
                tmpAC |= ((AC & 0001000) >> 9) << 3; /* bit 8 */
                tmpAC |= ((AC & 0010000) >> 12) << 4; /* bit 5 */
                tmpAC |= ((AC & 0100000) >> 15) << 5; /* bit 2 */
                tmpAC &= 0077;
                if ((op & OOPR_IOS_MASK) == OOPR_P7H) {
                    tmpAC |= 0100;  /* Punch 7th hole. */
                    TRACE_PRINT(ERROR_MSG, ("[%06o] Punch 7 holes\n", PC-1));
                } else {
                    TRACE_PRINT(ERROR_MSG, ("[%06o] Punch 6 holes\n", PC-1));
                }
                ptp (0, 0, tmpAC);    /* Punch one character on TTO */
            }
            break;
        case OOPR_PNT:
            {
                uint32 tmpAC = 0;
                tmpAC |= ((AC & 0000001) >> 0) << 0; /* bit 17 */
                tmpAC |= ((AC & 0000010) >> 3) << 1; /* bit 14 */
                tmpAC |= ((AC & 0000100) >> 6) << 2; /* bit 11 */
                tmpAC |= ((AC & 0001000) >> 9) << 3; /* bit 8 */
                tmpAC |= ((AC & 0010000) >> 12) << 4; /* bit 5 */
                tmpAC |= ((AC & 0100000) >> 15) << 5; /* bit 2 */
                tto (0, 0, tmpAC & 077);    /* Print one character on TTO */
            }
            break;
        case OOPR_R3C:
            AC = petr(3, 0, 0);
            break;
        case OOPR_R1C:
            AC &= 0333333; /* Clear bits 0,3,6,9,12,15 */
            AC |= petr(1, 0, 0);
            break;
        case OOPR_DIS:
#ifdef USE_DISPLAY
            LPEN = dpy (AC);  /* Display point on the CRT */
#endif /* USE_DISPLAY */
            break;
        }
    }

/* 1.1 TAC and PEN */
    if ((op & OOPR_PEN_MASK) == OOPR_PEN) {  /* pen  1.1 Read the light pen flip flops 1 and 2 into AC0 and AC1 */
        TRACE_PRINT(IOS_MSG, ("[%06o] Light Pen %01o\n", PC-1, LPEN));
        AC &= AMASK;
        AC |= (LPEN & 1) << 17;
        AC |= (LPEN & 2) << 16;
        AC &= DMASK;
    }

    if ((op & OOPR_TAC_MASK) == OOPR_TAC) {  /* tac  1.1 Insert a one in each digital position of the AC whereever there is a one in the corresponding digital position of the TAC */
        TRACE_PRINT(IOS_MSG, ("[%06o] TAC %06o\n", PC-1, TAC));
        AC |= TAC;
    }

    /* 1.2: COM, AMB, TBR */
    if (op & OOPR_COM) {    /* com  1.2 Complement every digit in the accumulator */
        AC = ~AC;
        inst_ctr.com++;
    }

    switch (op & OOPR_AMB_MASK) {
    case OOPR_AMB:
        inst_ctr.amb++;
        MBR = AC;
        break;
    case OOPR_TBR:
        TRACE_PRINT(IOS_MSG, ("[%06o] TBR %06o\n", PC-1, TBR));
        MBR |= TBR;
        break;
    case OOPR_LMB:
        MBR = LR;
        inst_ctr.lmb++;
        break;
    }

    /* 1.3, 1.4: can these happen together? */
    switch (op & OOPR_SHF_MASK) {
    case OOPR_MLR:
        LR = MBR;
        inst_ctr.mbl++;
        break;
    case OOPR_SHR: /* Shift AC Right, preserve bit 0. */
        {
            int32 bit0;
            bit0 = AC & 0400000;
            AC = AC >> 1;
            AC |= bit0;
            inst_ctr.shr++;
            break;
        }
    case OOPR_CYR:  /* cyr  1.4 Cycle the AC right one digital position (AC17 -> AC0) */
        {
            int32 bit17;
            bit17 = (AC & 1) << 17;
            AC  >>= 1;
            AC |= bit17;
            inst_ctr.cyr++;
        }
        break;
    }

    if (op & OOPR_PAD) {    /* 1.5 Partial Add (XOR): AC = MBR ^ AC */
        if (op & OOPR_CRY) {    /* 1.7 */
            TRACE_PRINT(ORD_MSG, ("[%06o] PAD+CRY: AC=%06o, MBR=%06o = ", PC-1, AC, MBR));
            AC = AC + MBR;
            if (AC & 01000000) {
                AC += 1;
            }
            AC &= DMASK;
            TRACE_PRINT(ORD_MSG, ("%06o\n", AC));
            inst_ctr.cry++;
        } else {
            TRACE_PRINT(ORD_MSG, ("[%06o] PAD: AC=%06o, MBR=%06o\n", PC-1, AC, MBR)); 
            AC = AC ^ MBR;
            AC &= DMASK;
            TRACE_PRINT(ORD_MSG, ("[%06o] PAD: Check: AC=%06o\n", PC-1, AC)); 
        }
        inst_ctr.pad++;
    }

    if (op & OOPR_CRY) {    /* 1.7 */
        if (op & OOPR_PAD) {
        } else {
            TRACE_PRINT(ERROR_MSG, ("[%06o] CRY: TODO: AC=%06o\n", PC-1, AC)); 
            inst_ctr.cry++;
        }
    }

    if ((op & OOPR_HLT) == OOPR_HLT) {   /* hlt  1.8 Halt the computer */
        TRACE_PRINT(IOS_MSG, ("[%06o] HALT Instruction\n", PC-1));
        reason = STOP_HALT;
    }

    return reason;
}
