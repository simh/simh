/* alpha_cpu.c: Alpha CPU simulator

   Copyright (c) 2003-2006, Robert M Supnik

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

   Alpha architecturally-defined CPU state:

   PC<63:0>                     program counter
   R[0:31]<63:0>                integer registers
   F[0:31]<63:0>                floating registers
   FPCR<63:0>                   floating point control register
                                (only left 32b are implemented)
   PCC<63:0>                    hardware cycle counter
   trap_summ<6:0>               arithmetic trap summary
   trap_mask<63:0>              arithmetic trap register mask
   lock_flag<varies>            load_locked flag
   vax_flag<0>                  VAX compatibility interrupt flag
   FEN<0>                       floating point enable flag

   The Alpha CPU privileged state is "soft" and varies significantly from
   operating system to operating system.  Alpha provides an intermediate layer
   of software (called PALcode) that implements the privileged state as well
   as a library of complex instruction functions.  PALcode implementations
   are chip specific and system specific, as well as OS specific.

   Alpha memory management is also "soft" and supported a variety of mapping
   schemes.  VMS and Unix use a three level page table and directly expose
   the underlying 64b hardware PTE.  NT uses a condensed 32b PTE.

   All Alpha instructions are 32b wide.  There are five basic formats: PALcall,
   branch, memory reference, integer operate, and floating operate.

    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           |                                                   |
   |   opcode  |                   PAL function                    | PAL
   |           |                                                   |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           |         |                                         |
   |   opcode  |    Ra   |           branch displacement           | branch
   |           |         |                                         |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           |         |         |                               |
   |   opcode  |    Ra   |    Rb   |      address displacement     | memory
   |           |         |         |                               | reference
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           |         |         |     | |             |         |
   |   opcode  |    Ra   |    Rb   |0 0 0|0|  function   |    Rc   | integer
   |           |         |         |     | |             |         | operate
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
                         |               | |
                         |    literal    |1|
                         |               | |
                         +-+-+-+-+-+-+-+-+-+

    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |           |         |         |     |   |           |         |
   |   opcode  |    Ra   |    Rb   | trap|rnd|  function |    Rc   | floating
   |           |         |         |     |   |           |         | operate
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   Memory reference format is also used for some two-operand operates;
   the address displacement is the function code.

   This routine is the instruction decode routine for the Alpha.  It
   is called from the simulator control program to execute instructions
   in simulated memory, starting at the simulated PC.  It runs until an
   enabled exception is encountered.

   General notes:

   1. Traps and interrupts.  Variable trap_summ summarizes the outstanding
      trap requests (if any).  Variable intr_summ summarizes the outstanding
      interrupt requests (if any).

   2. Interrupt requests are maintained in the int_req array, one word per
      interrupt level, one bit per device.

   3. Adding I/O devices.  These modules must be modified:

        alpha_defs.h    add device address and interrupt definitions
        alpha_sys.c     add sim_devices table entry
*/

#include "alpha_defs.h"

#define UNIT_V_CONH     (UNIT_V_UF + 0)                 /* halt to console */
#define UNIT_V_MSIZE    (UNIT_V_UF + 1)
#define UNIT_CONH       (1 << UNIT_V_CONH)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

#define HIST_PC         0x2
#define HIST_MIN        64
#define HIST_MAX        (1 << 18)

typedef struct {
    t_uint64            pc;
    uint32              ir;
    uint32              filler;
    t_uint64            ra;
    t_uint64            rb;
    } InstHistory;

#define H_A             0x01
#define H_B             0x02
#define H_B_LIT         0x04
#define H_EA            0x08
#define H_EA_B          0x10
#define H_EA_L16        0x20
#define H_MRF           (H_A|H_B|H_EA)
#define H_BRA           (H_A|H_EA|H_EA_B)
#define H_IOP           (H_A|H_B|H_B_LIT)
#define H_FOP           (H_A|H_B)
#define H_PAL           (H_A|H_EA|H_EA_L16)
#define H_JMP           (H_A|H_B|H_EA|H_EA_L16)

t_uint64 *M = 0;                                        /* memory */
t_uint64 R[32];                                         /* integer reg */
t_uint64 FR[32];                                        /* floating reg */
t_uint64 PC;                                            /* PC, <1:0> MBZ */
uint32 pc_align = 0;                                    /* PC<1:0> */
t_uint64 trap_mask = 0;                                 /* trap reg mask */
uint32 trap_summ = 0;                                   /* trap summary */
uint32 fpcr = 0;                                        /* fp ctrl reg */
uint32 pcc_l = 0;                                       /* rpcc high */
uint32 pcc_h = 0;                                       /* rpcc low */
uint32 pcc_enb = 0;
uint32 arch_mask = AMASK_BWX | AMASK_PRC;               /* arch mask */
uint32 impl_ver = IMPLV_EV5;                            /* impl version */
uint32 lock_flag = 0;                                   /* load lock flag */
uint32 vax_flag = 0;                                    /* vax intr flag */
uint32 intr_summ = 0;                                   /* interrupt summary */
uint32 pal_mode = 1;                                    /* PAL mode */
uint32 pal_type = PAL_UNDF;                             /* PAL type */
uint32 dmapen = 0;                                      /* data mapping enable */
uint32 fpen = 0;                                        /* flt point enabled */
uint32 ir = 0;                                          /* instruction register */
t_uint64 p1 = 0;                                        /* exception parameter */
uint32 int_req[IPL_HLVL] = { 0 };                       /* interrupt requests */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
t_uint64 pcq[PCQ_SIZE] = { 0 };                         /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
uint32 cpu_astop = 0;
uint32 hst_p = 0;                                       /* history pointer */
uint32 hst_lnt = 0;                                     /* history length */
InstHistory *hst = NULL;                                /* instruction history */
jmp_buf save_env;

const t_uint64 byte_mask[8] = {
    0x00000000000000FF, 0x000000000000FF00,
    0x0000000000FF0000, 0x00000000FF000000,
    0x000000FF00000000, 0x0000FF0000000000,
    0x00FF000000000000, 0xFF00000000000000
    };

const t_uint64 word_mask[4] = {
    0x000000000000FFFF, 0x00000000FFFF0000,
    0x0000FFFF00000000, 0xFFFF000000000000
    };

t_uint64 uemul64 (t_uint64 a, t_uint64 b, t_uint64 *hi);
t_uint64 byte_zap (t_uint64 op, uint32 mask);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);
t_stat cpu_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_show_virt (FILE *of, UNIT *uptr, int32 val, void *desc);
t_stat cpu_fprint_one_inst (FILE *st, uint32 ir, t_uint64 pc, t_uint64 ra, t_uint64 rb);

extern t_uint64 op_ldf (t_uint64 op);
extern t_uint64 op_ldg (t_uint64 op);
extern t_uint64 op_lds (t_uint64 op);
extern t_uint64 op_stf (t_uint64 op);
extern t_uint64 op_stg (t_uint64 op);
extern t_uint64 op_sts (t_uint64 op);
extern t_uint64 vax_sqrt (uint32 ir, t_bool dp);
extern t_uint64 ieee_sqrt (uint32 ir, t_bool dp);
extern void vax_fop (uint32 ir);
extern void ieee_fop (uint32 ir);
extern t_stat pal_19 (uint32 ir);
extern t_stat pal_1b (uint32 ir);
extern t_stat pal_1d (uint32 ir);
extern t_stat pal_1e (uint32 ir);
extern t_stat pal_1f (uint32 ir);
extern t_uint64 trans_c (t_uint64 va);
extern t_stat cpu_show_tlb (FILE *of, UNIT *uptr, int32 val, void *desc);
extern t_stat pal_eval_intr (uint32 flag);
extern t_stat pal_proc_excp (uint32 type);
extern t_stat pal_proc_trap (uint32 type);
extern t_stat pal_proc_intr (uint32 type);
extern t_stat pal_proc_inst (uint32 fnc);
extern uint32 tlb_set_cm (int32 cm);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, INITMEMSIZE) };

REG cpu_reg[] = {
    { HRDATA (PC, PC, 64), PV_LEFT },
    { HRDATA (PCALG, pc_align, 3) },
    { HRDATA (R0, R[0], 64) },
    { HRDATA (R1, R[1], 64) },
    { HRDATA (R2, R[2], 64) },
    { HRDATA (R3, R[3], 64) },
    { HRDATA (R4, R[4], 64) },
    { HRDATA (R5, R[5], 64) },
    { HRDATA (R6, R[6], 64) },
    { HRDATA (R7, R[7], 64) },
    { HRDATA (R8, R[8], 64) },
    { HRDATA (R9, R[9], 64) },
    { HRDATA (R10, R[10], 64) },
    { HRDATA (R11, R[11], 64) },
    { HRDATA (R12, R[12], 64) },
    { HRDATA (R13, R[13], 64) },
    { HRDATA (R14, R[14], 64) },
    { HRDATA (R15, R[15], 64) },
    { HRDATA (R16, R[16], 64) },
    { HRDATA (R17, R[17], 64) },
    { HRDATA (R18, R[18], 64) },
    { HRDATA (R19, R[19], 64) },
    { HRDATA (R20, R[20], 64) },
    { HRDATA (R21, R[21], 64) },
    { HRDATA (R22, R[22], 64) },
    { HRDATA (R23, R[23], 64) },
    { HRDATA (R24, R[24], 64) },
    { HRDATA (R25, R[25], 64) },
    { HRDATA (R26, R[26], 64) },
    { HRDATA (R27, R[27], 64) },
    { HRDATA (R28, R[28], 64) },
    { HRDATA (R29, R[29], 64) },
    { HRDATA (R30, R[30], 64) },
    { HRDATA (R31, R[31], 64), REG_RO },
    { HRDATA (F0, FR[0], 64) },
    { HRDATA (F1, FR[1], 64) },
    { HRDATA (F2, FR[2], 64) },
    { HRDATA (F3, FR[3], 64) },
    { HRDATA (F4, FR[4], 64) },
    { HRDATA (F5, FR[5], 64) },
    { HRDATA (F6, FR[6], 64) },
    { HRDATA (F7, FR[7], 64) },
    { HRDATA (F8, FR[8], 64) },
    { HRDATA (F9, FR[9], 64) },
    { HRDATA (F10, FR[10], 64) },
    { HRDATA (F11, FR[11], 64) },
    { HRDATA (F12, FR[12], 64) },
    { HRDATA (F13, FR[13], 64) },
    { HRDATA (F14, FR[14], 64) },
    { HRDATA (F15, FR[15], 64) },
    { HRDATA (F16, FR[16], 64) },
    { HRDATA (F17, FR[17], 64) },
    { HRDATA (F18, FR[18], 64) },
    { HRDATA (F19, FR[19], 64) },
    { HRDATA (F20, FR[20], 64) },
    { HRDATA (F21, FR[21], 64) },
    { HRDATA (F22, FR[22], 64) },
    { HRDATA (F23, FR[23], 64) },
    { HRDATA (F24, FR[24], 64) },
    { HRDATA (F25, FR[25], 64) },
    { HRDATA (F26, FR[26], 64) },
    { HRDATA (F27, FR[27], 64) },
    { HRDATA (F28, FR[28], 64) },
    { HRDATA (F29, FR[29], 64) },
    { HRDATA (F30, FR[30], 64) },
    { HRDATA (F31, FR[31], 64), REG_RO },
    { HRDATA (FPCR, fpcr, 32) },
    { FLDATA (FEN, fpen, 0) },
    { HRDATA (TRAPS, trap_summ, 8) },
    { HRDATA (TRAPM, trap_mask, 64) },
    { HRDATA (PCCH, pcc_h, 32) },
    { HRDATA (PCCL, pcc_l, 32) },
    { FLDATA (LOCK, lock_flag, 0) },
    { FLDATA (VAXF, vax_flag, 0) },
    { FLDATA (PALMODE, pal_mode, 0) },
    { HRDATA (PALTYPE, pal_type, 2), REG_HRO },
    { HRDATA (DMAPEN, dmapen, 0) },
    { HRDATA (AMASK, arch_mask, 13), REG_RO },
    { HRDATA (IMPLV, impl_ver, 2), REG_RO },
    { BRDATA (PCQ, pcq, 16, 32, PCQ_SIZE), REG_RO+REG_CIRC },
    { HRDATA (PCQP, pcq_p, 6), REG_HRO },
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_MSIZE, (1u << 25), NULL, "32M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 26), NULL, "64M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 27), NULL, "128M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 28), NULL, "256M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 29), NULL, "512M", &cpu_set_size },
    { UNIT_CONH, 0, "HALT to SIMH", "SIMHALT", NULL },
    { UNIT_CONH, UNIT_CONH, "HALT to console", "CONHALT", NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "VIRTUAL", NULL,
      NULL, &cpu_show_virt },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "ITLB", NULL,
      NULL, &cpu_show_tlb },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 1, "DTLB", NULL,
      NULL, &cpu_show_tlb },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 48, 8, 16, 64,
    &cpu_ex, &cpu_dep, &cpu_reset,
    &cpu_boot, NULL, NULL,
    NULL, DEV_DYNM|DEV_DEBUG, 0,
    NULL, &cpu_set_size, NULL
    };

t_stat sim_instr (void)
{
t_stat reason;
int abortval;
t_bool tracing;

PC = PC | pc_align;                                     /* put PC together */
abortval = setjmp (save_env);                           /* set abort hdlr */
if (abortval != 0) {                                    /* exception? */
    if (abortval < 0) {                                 /* SCP stop? */
        pcc_l = pcc_l & M32;
        pcq_r->qptr = pcq_p;                            /* update pc q ptr */
        pc_align = ((uint32) PC) & 3;                   /* separate PC<1:0> */
        PC = PC & 0xFFFFFFFFFFFFFFFC;
        return -abortval;
        }
    reason = pal_proc_excp (abortval);                  /* pal processing */
    }
else reason = 0;
tlb_set_cm (-1);                                        /* resync cm */
tracing = ((hst_lnt != 0) || DEBUG_PRS (cpu_dev));

intr_summ = pal_eval_intr (1);                          /* eval interrupts */

/* Main instruction loop */

while (reason == 0) {

    int32 i;
    uint32 op, ra, rb, rc, fnc, sc, s32, t32, sgn;
    t_int64 s1, s2, sr;
    t_uint64 ea, dsp, rbv, res, s64, t64;

    if (cpu_astop) {                                    /* debug stop? */
        cpu_astop = 0;                                  /* clear */
        reason = SCPE_STOP;                             /* stop simulation */
        break;
        }

    if (sim_interval <= 0) {                            /* chk clock queue */
        if (reason = sim_process_event ()) break;
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        }

    if (intr_summ && !pal_mode) {                       /* interrupt pending? */
        reason = pal_proc_intr (intr_summ);             /* pal processing */
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        continue;
        }

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {      /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

    sim_interval = sim_interval - 1;                    /* count instr */
    pcc_l = pcc_l + pcc_enb;
    ir = ReadI (PC);                                    /* get instruction */
    op = I_GETOP (ir);                                  /* get opcode */
    ra = I_GETRA (ir);                                  /* get ra */
    rb = I_GETRB (ir);                                  /* get rb */

    if (tracing) {                                      /* trace or history? */
        if (hst_lnt) {                                  /* history enabled? */
            hst_p = (hst_p + 1);                        /* next entry */
            if (hst_p >= hst_lnt) hst_p = 0;
            hst[hst_p].pc = PC | pc_align | HIST_PC;    /* save PC */
            hst[hst_p].ir = ir;                         /* save ir */
            hst[hst_p].ra = R[ra];                      /* save Ra */
            hst[hst_p].rb = R[rb];                      /* save Rb */
            }
        if (DEBUG_PRS (cpu_dev))                        /* trace enabled? */
            cpu_fprint_one_inst (sim_deb, ir, PC | pc_align, R[ra], R[rb]);
        }

    PC = (PC + 4) & M64;                                /* advance PC */
    switch (op) {

/* Memory reference instructions */

    case OP_LDA:                                        /* LDA */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            R[ra] = ea;
            }
        break;

    case OP_LDAH:                                       /* LDAH */
        if (ra != 31) {
            dsp = I_GETMDSP (ir) << 16;
            ea = (R[rb] + SEXT_L_Q (dsp)) & M64;
            R[ra] = ea;
            }
        break;

    case OP_LDBU:                                       /* LDBU */
        if (!(arch_mask & AMASK_BWX)) ABORT (EXC_RSVI); /* EV56 or later */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            R[ra] = ReadB (ea);
            }
        break;

    case OP_LDQ_U:                                      /* LDQ_U */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            R[ra] = ReadQ (ea & ~7);                    /* ignore ea<2:0> */
            }
        break;

    case OP_LDWU:                                       /* LDWU */
        if (!(arch_mask & AMASK_BWX)) ABORT (EXC_RSVI); /* EV56 or later */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            R[ra] = ReadW (ea);
            }
        break;

    case OP_STW:                                        /* STW */
        if (!(arch_mask & AMASK_BWX)) ABORT (EXC_RSVI); /* EV56 or later */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteW (ea, R[ra]);
        break;

    case OP_STB:                                        /* STB */
        if (!(arch_mask & AMASK_BWX)) ABORT (EXC_RSVI); /* EV56 or later */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteB (ea, R[ra]);
        break;

    case OP_STQ_U:                                      /* STQ_U */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteQ (ea & ~7, R[ra]);                        /* ignore ea<2:0> */
        break;

    case OP_LDF:                                        /* LDF */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            FR[ra] = op_ldf (ReadL (ea));               /* swizzle bits */
            }
        break;

    case OP_LDG:                                        /* LDG */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            FR[ra] = op_ldg (ReadQ (ea));               /* swizzle bits */
            }
        break;

    case OP_LDS:                                        /* LDS */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            FR[ra] = op_lds (ReadL (ea));               /* swizzle bits */
            }
        break;

    case OP_LDT:                                        /* LDT */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            FR[ra] = ReadQ (ea);                        /* no swizzling needed */
            }
        break;

    case OP_STF:                                        /* STF */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteL (ea, op_stf (FR[ra]));                   /* swizzle bits */
        break;

    case OP_STG:                                        /* STG */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteQ (ea, op_stg (FR[ra]));                   /* swizzle bits */
        break;

    case OP_STS:                                        /* STS */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteL (ea, op_sts (FR[ra]));                   /* swizzle bits */
        break;

    case OP_STT:                                        /* STT */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteQ (ea, FR[ra]);                            /* no swizzling needed */
        break;

    case OP_LDL:                                        /* LDL */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            res = ReadL (ea);
            R[ra] = SEXT_L_Q (res);
            }
        break;

    case OP_LDQ:                                        /* LDQ */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            R[ra] = ReadQ (ea);
            }
        break;

    case OP_LDL_L:                                      /* LDL_L */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            res = ReadL (ea);
            R[ra] = SEXT_L_Q (res);
            lock_flag = 1;                              /* set lock flag */
            }
        break;

    case OP_LDQ_L:                                      /* LDQ_L */
        if (ra != 31) {
            dsp = I_GETMDSP (ir);
            ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
            R[ra] = ReadQ (ea);
            lock_flag = 1;                              /* set lock flag */
            }
        break;

    case OP_STL:                                        /* STL */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteL (ea, R[ra]);
        break;

    case OP_STQ:                                        /* STQ */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        WriteQ (ea, R[ra]);
        break;

    case OP_STL_C:                                      /* STL_C */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        if (lock_flag) WriteL (ea, R[ra]);              /* unlocking? ok */
        else R[ra] = 0;                                 /* write fails */
        lock_flag = 0;                                  /* clear lock */
        break;

    case OP_STQ_C:                                      /* STQ_C */
        dsp = I_GETMDSP (ir);
        ea = (R[rb] + SEXT_MDSP (dsp)) & M64;
        if (lock_flag) WriteQ (ea, R[ra]);              /* unlocking? ok */
        else R[ra] = 0;                                 /* write fails */
        lock_flag = 0;                                  /* clear lock */
        break;

/* Control instructions */

    case OP_JMP:                                        /* JMP */
        PCQ_ENTRY;
        rbv = R[rb];                                    /* in case Ra = Rb */
        if (ra != 31) R[ra] = PC;                       /* save PC */
        PC = rbv;                                       /* jump */
        break;

    case OP_BR:                                         /* BR, BSR */
    case OP_BSR:
        PCQ_ENTRY;
        if (ra != 31) R[ra] = PC;                       /* save PC */
        dsp = I_GETBDSP (ir);
        PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;       /* branch */
        break;

    case OP_FBEQ:                                       /* FBEQ */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if ((FR[ra] & ~FPR_SIGN) == 0) {                /* +0 or - 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_FBLT:                                       /* FBLT */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if (FR[ra] > FPR_SIGN) {                        /* -0 to -n? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_FBLE:                                       /* FBLE */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if ((FR[ra] & FPR_SIGN) || (FR[ra] == 0)) {     /* - or 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_FBNE:                                       /* FBNE */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if ((FR[ra] & ~FPR_SIGN) != 0) {                /* not +0 or -0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_FBGE:                                       /* FBGE */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if (FR[ra] <= FPR_SIGN) {                       /* +0 to +n? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_FBGT:                                       /* FBGT */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        if (!(FR[ra] & FPR_SIGN) && (FR[ra] != 0)) {    /* not - and not 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BLBC:                                       /* BLBC */
        if ((R[ra] & 1) == 0) {                         /* R<0> == 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BEQ:                                        /* BEQ */
        if (R[ra] == 0) {                               /* R == 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BLT:                                        /* BLT */
        if (R[ra] & Q_SIGN) {                           /* R<63> == 1? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BLE:                                        /* BLE */
        if ((R[ra] == 0) || (R[ra] & Q_SIGN)) {         /* R == 0 || R<63> == 1? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BLBS:                                       /* BLBS */
        if ((R[ra] & 1) != 0) {                         /* R<0> == 1? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BNE:                                        /* BNE */
        if (R[ra] != 0) {                               /* R != 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BGE:                                        /* BGE */
        if (!(R[ra] & Q_SIGN)) {                        /* R<63> == 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

    case OP_BGT:                                        /* BGT */
        if ((R[ra] != 0) && !(R[ra] & Q_SIGN)) {        /* R != 0 && R<63> == 0? */
            PCQ_ENTRY;
            dsp = I_GETBDSP (ir);
            PC = (PC + (SEXT_BDSP (dsp) << 2)) & M64;
            }
        break;

/* Integer arithmetic operates (10) */

    case OP_IALU:                                       /* integer arith opr */
        rc = I_GETRC (ir);                              /* get rc */
        if (ir & I_ILIT) rbv = I_GETLIT8 (ir);          /* literal? rbv = lit */
        else rbv = R[rb];                               /* no, rbv = R[rb] */
        fnc = I_GETIFNC (ir);                           /* get function */                              
        switch (fnc) {                                  /* case on function */

        case 0x00:                                      /* ADDL */
            res = SEXT_L_Q (R[ra] + rbv);
            break;

        case 0x02:                                      /* S4ADDL */
            res = SEXT_L_Q ((R[ra] << 2) + rbv);
            break;

        case 0x09:                                      /* SUBL */
            res = SEXT_L_Q (R[ra] - rbv);
            break;

        case 0x0B:                                      /* S4SUBL */
            res = SEXT_L_Q ((R[ra] << 2) - rbv);
            break;

        case 0x0F:                                      /* CMPBGE */
            for (i = 0, res = 0; i < 8; i++) {
                if ((R[ra] & byte_mask[i]) >= (rbv & byte_mask[i]))
                    res = res | ((t_uint64) 1u << i);
                }
            break;

        case 0x12:                                      /* S8ADDL */
            res = SEXT_L_Q ((R[ra] << 3) + rbv);
            break;

        case 0x1B:                                      /* S8SUBL */
            res = SEXT_L_Q ((R[ra] << 3) - rbv);
            break;

        case 0x1D:                                      /* CMPULT */
            res = (R[ra] < rbv);
            break;

        case 0x20:                                      /* ADDQ */
            res = R[ra] + rbv;
            break;

        case 0x22:                                      /* S4ADDQ */
            res = (R[ra] << 2) + rbv;
            break;

        case 0x29:                                      /* SUBQ */
            res = R[ra] - rbv;
            break;

        case 0x2B:                                      /* S4SUBQ */
            res = (R[ra] << 2) - rbv;
            break;

        case 0x2D:                                      /* CMPEQ */
            res = (R[ra] == rbv);
            break;

        case 0x32:                                      /* S8ADDQ */
            res = (R[ra] << 3) + rbv;
            break;

        case 0x3B:                                      /* S8SUBQ */
            res = (R[ra] << 3) - rbv;
            break;

        case 0x3D:                                      /* CMPULE */
            res = (R[ra] <= rbv);
            break;

        case 0x40:                                      /* ADDL/V */
            res = SEXT_L_Q (R[ra] + rbv);
            if (((~R[ra] ^ rbv) & (R[ra] ^ res)) & L_SIGN)
                arith_trap (TRAP_IOV, ir);
            break;

        case 0x49:                                      /* SUBL/V */
            res = SEXT_L_Q (R[ra] - rbv);
            if (((R[ra] ^ rbv) & (~rbv ^ res)) & L_SIGN)
                arith_trap (TRAP_IOV, ir);
            break;

        case 0x4D:                                      /* CMPLT */
            sgn = Q_GETSIGN (R[ra]);                    /* get Ra sign */
            if (sgn ^ Q_GETSIGN (rbv)) res = sgn;       /* signs diff? */
            else res = sgn ^ (R[ra] < rbv);
            break;

        case 0x60:                                      /* ADDQ/V */
            res = R[ra] + rbv;
            if (((~R[ra] ^ rbv) & (R[ra] ^ res)) & Q_SIGN)
                arith_trap (TRAP_IOV, ir);
            break;

        case 0x69:                                      /* SUBQ/V */
            res = R[ra] - rbv;
            if (((R[ra] ^ rbv) & (~rbv ^ res)) & Q_SIGN)
                arith_trap (TRAP_IOV, ir);
            break;

        case 0x6D:                                      /* CMPLE */
            if (R[ra] == rbv) res = 1;
            else {
                sgn = Q_GETSIGN (R[ra]);                /* get Ra sign */
                if (sgn ^ Q_GETSIGN (rbv)) res = sgn;   /* signs diff? */
                else res = sgn ^ (R[ra] < rbv);
                }

            break;
        default:
            res = R[rc];
            break;
            }

        if (rc != 31) R[rc] = res & M64;
        break;

/* Integer logical operates (11) */

    case OP_ILOG:                                       /* integer logic opr */
        rc = I_GETRC (ir);                              /* get rc */
        if (ir & I_ILIT) rbv = I_GETLIT8 (ir);          /* literal? rbv = lit */
        else rbv = R[rb];                               /* no, rbv = R[rb] */
        fnc = I_GETIFNC (ir);                           /* get function */                              
        switch (fnc) {                                  /* case on function */

        case 0x00:                                      /* AND */
            res = R[ra] & rbv;
            break;

        case 0x08:                                      /* BIC */
            res = R[ra] & ~rbv;
            break;

        case 0x14:                                      /* CMOVLBS */
            if ((R[ra] & 1) != 0) res = rbv;
            else res = R[rc];
            break;

        case 0x16:                                      /* CMOVLBC */
            if ((R[ra] & 1) == 0) res = rbv;
            else res = R[rc];
            break;

        case 0x20:                                      /* BIS */
            res = R[ra] | rbv;
            break;

        case 0x24:                                      /* CMOVEQ */
            if (R[ra] == 0) res = rbv;
            else res = R[rc];
            break;

        case 0x26:                                      /* CMOVNE */
            if (R[ra] != 0) res = rbv;
            else res = R[rc];
            break;

        case 0x28:                                      /* ORNOT */
            res = R[ra] | ~rbv;
            break;

        case 0x40:                                      /* XOR */
            res = R[ra] ^ rbv;
            break;

        case 0x44:                                      /* CMOVLT */
            if (R[ra] & Q_SIGN) res = rbv;
            else res = R[rc];
            break;

        case 0x46:                                      /* CMOVGE */
            if (!(R[ra] & Q_SIGN)) res = rbv;
            else res = R[rc];
            break;

        case 0x48:                                      /* EQV */
            res = R[ra] ^ ~rbv;
            break;

        case 0x61:                                      /* AMASK */
            res = rbv & ~arch_mask;
            break;

        case 0x64:                                      /* CMOVLE */
            if ((R[ra] & Q_SIGN) || (R[ra] == 0)) res = rbv;
            else res = R[rc];
            break;

        case 0x66:                                      /* CMOVGT */
            if (!(R[ra] & Q_SIGN) && (R[ra] != 0)) res = rbv;
            else res = R[rc];
            break;

        case 0x6C:                                      /* IMPLVER */
            res = impl_ver;
            break;

        default:
            res = R[rc];
            break;
            }

        if (rc != 31) R[rc] = res & M64;
        break;

/* Integer logical shifts (12) */

    case OP_ISHFT:                                      /* integer shifts */
        rc = I_GETRC (ir);                              /* get rc */
        if (ir & I_ILIT) rbv = I_GETLIT8 (ir);          /* literal? rbv = lit */
        else rbv = R[rb];                               /* no, rbv = R[rb] */
        fnc = I_GETIFNC (ir);                           /* get function */                              
        switch (fnc) {                                  /* case on function */

        case 0x02:                                      /* MSKBL */
            sc = ((uint32) rbv) & 7;
            res = byte_zap (R[ra], 0x1 << sc);
            break;

        case 0x06:                                      /* EXTBL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = (R[ra] >> sc) & M8;
            break;

        case 0x0B:                                      /* INSBL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = (R[ra] & M8) << sc;
            break;

        case 0x12:                                      /* MSKWL */
            sc = ((uint32) rbv) & 7;
            res = byte_zap (R[ra], 0x3 << sc);
            break;

        case 0x16:                                      /* EXTWL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = (R[ra] >> sc) & M16;
            break;

        case 0x1B:                                      /* INSWL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = (R[ra] & M16) << sc;
            break;

        case 0x22:                                      /* MSKLL */
            sc = ((uint32) rbv) & 7;
            res = byte_zap (R[ra], 0xF << sc);
            break;

        case 0x26:                                      /* EXTLL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = (R[ra] >> sc) & M32;
            break;

        case 0x2B:                                      /* INSLL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = (R[ra] & M32) << sc;
            break;

        case 0x30:                                      /* ZAP */
            res = byte_zap (R[ra], (uint32) rbv);
            break;

        case 0x31:                                      /* ZAPNOT */
            res = byte_zap (R[ra], ~((uint32) rbv));
            break;

        case 0x32:                                      /* MSKQL */
            sc = ((uint32) rbv) & 7;
            res = byte_zap (R[ra], 0xFF << sc);
            break;

        case 0x34:                                      /* SRL */
            sc = ((uint32) rbv) & 0x3F;
            res = R[ra] >> sc;
            break;

        case 0x36:                                      /* EXTQL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = R[ra] >> sc;
            break;

        case 0x39:                                      /* SLL */
            sc = ((uint32) rbv) & 0x3F;
            res = R[ra] << sc;
            break;

        case 0x3B:                                      /* INSQL */
            sc = (((uint32) rbv) << 3) & 0x3F;
            res = R[ra] << sc;
            break;

        case 0x3C:                                      /* SRA */
            sc = ((uint32) rbv) & 0x3F;
            res = (R[ra] >> sc);
            if (sc && (R[ra] & Q_SIGN)) res = res |
                (((t_uint64) M64) << (64 - sc));
            break;

        case 0x52:                                      /* MSKWH */
            sc = 8 - (((uint32) rbv) & 7);
            res = byte_zap (R[ra], 0x3 >> sc);
            break;

        case 0x57:                                      /* EXTWH */
            sc = (64 - (((uint32) rbv) << 3)) & 0x3F;
            res = (R[ra] << sc) & M16;
            break;

        case 0x5A:                                      /* INSWH */
            sc = (64 - (((uint32) rbv) << 3)) & 0x3F;
            res = (R[ra] & M16) >> sc;
            break;

        case 0x62:                                      /* MSKLH */
            sc = 8 - (((uint32) rbv) & 7);
            res = byte_zap (R[ra], 0xF >> sc);
            break;

        case 0x67:                                      /* EXTLH */
            sc = (64 - (((uint32) rbv) << 3)) & 0x3F;
            res = (R[ra] << sc) & M32;
            break;

        case 0x6A:                                      /* INSLH */
            sc = (64 - (((uint32) rbv) << 3)) & 0x3F;
            res = (R[ra] & M32) >> sc;
            break;

        case 0x72:                                      /* MSKQH */
            sc = 8 - (((uint32) rbv) & 7);
            res = byte_zap (R[ra], 0xFF >> sc);
            break;

        case 0x77:                                      /* EXTQH */
            sc = (64 - (((uint32) rbv) << 3)) & 0x3F;
            res = R[ra] << sc;
            break;

        case 0x7A:                                      /* INSQH */
            sc = (64 - (((uint32) rbv) << 3)) & 0x3F;
            res = R[ra] >> sc;
            break;

        default:
            res = R[rc];
            break;
            }

        if (rc != 31) R[rc] = res & M64;
        break;

/* Integer multiply (13) */

    case OP_IMUL:                                       /* integer multiply */
        rc = I_GETRC (ir);                              /* get rc */
        if (ir & I_ILIT) rbv = I_GETLIT8 (ir);          /* literal? rbv = lit */
        else rbv = R[rb];                               /* no, rbv = R[rb] */
        fnc = I_GETIFNC (ir);                           /* get function */                              
        switch (fnc) {                                  /* case on function */

        case 0x00:                                      /* MULL */
            s1 = SEXT_L_Q (R[ra]);
            s2 = SEXT_L_Q (rbv);
            sr = s1 * s2;
            res = SEXT_L_Q (sr);
            break;

        case 0x20:                                      /* MULQ */
            res = uemul64 (R[ra], rbv, NULL);           /* low 64b invariant */
            break;                                      /* with sign/unsigned */

        case 0x30:                                      /* UMULH */
            uemul64 (R[ra], rbv, &res);
            break;

        case 0x40:                                      /* MULL/V */
            s1 = SEXT_L_Q (R[ra]);
            s2 = SEXT_L_Q (rbv);
            sr = s1 * s2;
            res = SEXT_L_Q (sr);
            if (((sr ^ res) & M64) != 0)                /* overflow? */
                arith_trap (TRAP_IOV, ir);
            break;

        case 0x60:                                      /* MULQ/V */
            res = uemul64 (R[ra], rbv, &t64);
            if (Q_GETSIGN(R[ra]))
                t64 = (t64 - rbv) & M64;
            if (Q_GETSIGN(rbv))
                t64 = (t64 - R[ra]) & M64;
            if (Q_GETSIGN (res)? (t64 != M64): (t64 != 0))
                arith_trap (TRAP_IOV, ir);
            break;

        default:
            res = R[rc];
            break;
            }

        if (rc != 31) R[rc] = res & M64;
        break;

/* FIX optional floating point set (14) */

    case OP_IFLT:                                       /* int to flt */
        if (!(arch_mask & AMASK_FIX)) ABORT (EXC_RSVI); /* EV56 or later */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        rc = I_GETRC (ir);                              /* get rc */
        fnc = I_GETFFNC (ir);                           /* get function */
        switch (fnc) {                                  /* case on function */

        case 0x04:                                      /* ITOFS */
            if (ir & (I_FRND|I_FTRP)) ABORT (EXC_RSVI);
            t32 = ((uint32) R[ra]) & M32;
            res = op_lds (t32);
            break;

        case 0x0A:                                      /* SQRTF */
            if (ir & I_F_VAXRSV) ABORT (EXC_RSVI);
            res = vax_sqrt (ir, DT_F);
            break;

        case 0x0B:                                      /* SQRTS */
            res = ieee_sqrt (ir, DT_S);
            break;

        case 0x14:                                      /* ITOFF */
            if (ir & (I_FRND|I_FTRP)) ABORT (EXC_RSVI);
            t32 = ((uint32) R[ra]) & M32;
            res = op_ldf (SWAP_VAXF (t32));
            break;

        case 0x24:                                      /* ITOFT */
            if (ir & (I_FRND|I_FTRP)) ABORT (EXC_RSVI);
            res = R[ra];
            break;

        case 0x2A:                                      /* SQRTG */
            if (ir & I_F_VAXRSV) ABORT (EXC_RSVI);
            res = vax_sqrt (ir, DT_G);
            break;

        case 0x2B:                                      /* SQRTT */
            res = ieee_sqrt (ir, DT_T);
            break;

        default:
            ABORT (EXC_RSVI);
            }

        if (rc != 31) FR[rc] = res & M64;
        break;

/* VAX and IEEE floating point operates - done externally */

    case OP_VAX:                                        /* VAX fp opr */
        if (ir & I_F_VAXRSV) ABORT (EXC_RSVI);          /* reserved */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        vax_fop (ir);
        break;

    case OP_IEEE:                                       /* IEEE fp opr */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        ieee_fop (ir);
        break;

/* Data type independent floating point (17) */

    case OP_FP:                                         /* other fp */
        if (fpen == 0) ABORT (EXC_FPDIS);               /* flt point disabled? */
        rc = I_GETRC (ir);                              /* get rc */
        fnc = I_GETFFNC (ir);                           /* get function */
        switch (fnc) {                                  /* case on function */

        case 0x10:                                      /* CVTLQ */
            res = ((FR[rb] >> 32) & 0xC0000000) | ((FR[rb] >> 29) & 0x3FFFFFFF);
            res = SEXT_L_Q (res);
            break;

        case 0x20:                                      /* CPYS */
            res = (FR[ra] & FPR_SIGN) | (FR[rb] & ~FPR_SIGN);
            break;

        case 0x21:                                      /* CPYSN */
            res = ((FR[ra] & FPR_SIGN) ^ FPR_SIGN) | (FR[rb] & ~FPR_SIGN);
            break;

        case 0x22:                                      /* CPYSE */
            res = (FR[ra] & (FPR_SIGN|FPR_EXP)) | (FR[rb] & ~(FPR_SIGN|FPR_EXP));
            break;

        case 0x24:                                      /* MT_FPCR */
            fpcr = ((uint32) (FR[ra] >> 32)) & ~FPCR_RAZ;
            res = FR[rc];
            break;

        case 0x25:                                      /* MF_FPCR */
            res = ((t_uint64) fpcr) << 32;
            break;

        case 0x2A:                                      /* FCMOVEQ */
            if ((FR[ra] & ~FPR_SIGN) == 0) res = FR[rb];
            else res = FR[rc];
            break;

        case 0x2B:                                      /* FCMOVNE */
            if ((FR[ra] & ~FPR_SIGN) != 0) res = FR[rb];
            else res = FR[rc];
            break;

        case 0x2C:                                      /* FCMOVLT */
            if (FR[ra] > FPR_SIGN) res = FR[rb];
            else res = FR[rc];
            break;

        case 0x2D:                                      /* FCMOVGE */
            if (FR[ra] <= FPR_SIGN) res = FR[rb];
            else res = FR[rc];
            break;

        case 0x2E:                                      /* FCMOVLE */
            if (FPR_GETSIGN (FR[ra]) || (FR[ra] == 0)) res = FR[rb];
            else res = FR[rc];
            break;

        case 0x2F:                                      /* FCMOVGT */
            if (!FPR_GETSIGN (FR[ra]) && (FR[ra] != 0)) res = FR[rb];
            else res = FR[rc];
            break;

        case 0x30:                                      /* CVTQL */
            res = ((FR[rb] & 0xC0000000) << 32) | ((FR[rb] & 0x3FFFFFFF) << 29);
            if (FPR_GETSIGN (FR[rb])?
                (FR[rb] < 0xFFFFFFFF80000000):
                (FR[rb] > 0x000000007FFFFFFF)) {
                fpcr = fpcr | FPCR_IOV | FPCR_INE | FPCR_SUM;
                if (ir & I_FTRP_V) arith_trap (TRAP_IOV, ir);
                }
            break;

        default:
            res = FR[rc];
            break;
            }

        if (rc != 31) FR[rc] = res & M64;
        break;

/* Barriers and misc (18)

   Alpha has a weak memory ordering model and an imprecise exception model;
   together, they require a wide variety of barrier instructions to guarantee
   memory coherency in multiprocessor systems, as well as backward compatible
   exception instruction semantics.

   The simulator is uniprocessor only, and has ordered memory accesses and
   precise exceptions.  Therefore, the barriers are all NOP's. */

    case OP_MISC:                                       /* misc */
        fnc = I_GETMDSP (ir);                           /* get function */
        switch (fnc) {                                  /* case on function */

        case 0xC000:                                    /* RPCC */
            pcc_l = pcc_l & M32;
            if (ra != 31) R[ra] = (((t_uint64) pcc_h) << 32) | ((t_uint64) pcc_l);
            break;

        case 0xE000:                                    /* RC */
            if (ra != 31) R[ra] = vax_flag;
            vax_flag = 0;
            break;

        case 0xF000:                                    /* RS */
            if (ra != 31) R[ra] = vax_flag;
            vax_flag = 1;
            break;

        default:
            break;
            }

        break;

/* Optional instruction sets (1C) */

    case OP_FLTI:                                       /* float to int */
        rc = I_GETRC (ir);                              /* get rc */
        if (ir & I_ILIT) rbv = I_GETLIT8 (ir);          /* literal? rbv = lit */
        else rbv = R[rb];                               /* no, rbv = R[rb] */
        fnc = I_GETIFNC (ir);                           /* get function */      
        switch (fnc) {                                  /* case on function */

        case 0x00:                                      /* SEXTB */
            if (!(arch_mask & AMASK_BWX)) ABORT (EXC_RSVI);
            res = SEXT_B_Q (rbv);
            break;

        case 0x01:                                      /* SEXTW */
            if (!(arch_mask & AMASK_BWX)) ABORT (EXC_RSVI);
            res = SEXT_W_Q (rbv);
            break;

        case 0x30:                                      /* CTPOP */
            if (!(arch_mask & AMASK_CIX)) ABORT (EXC_RSVI);
            for (res = 0; rbv != 0; res++) {
                rbv = rbv & ~(rbv & NEG_Q (rbv));
				}
            break;

        case 0x31:                                      /* PERR */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 64; i = i + 8) {
                s32 = (uint32) (R[ra] >> i) & M8;
                t32 = (uint32) (rbv >> i) & M8;
                res = res + ((t_uint64) (s32 >= t32)? (s32 - t32): (t32 - s32));
                }
            break;

        case 0x32:                                      /* CTLZ */
            if (!(arch_mask & AMASK_CIX)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 64; i++) {
                if ((rbv >> (63 - i)) & 1) break;
                res = res + 1;
                }
            break;

        case 0x33:                                      /* CTTZ */
            if (!(arch_mask & AMASK_CIX)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 64; i++) {
                if ((rbv >> i) & 1) break;
                res = res + 1;
                }
            break;

        case 0x34:                                      /* UNPKBL */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            res = ((rbv & 0xFF00) << 24) | (rbv & 0xFF);
            break;

        case 0x35:                                      /* UNPKBW */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            res = ((rbv & 0xFF000000) << 24) | ((rbv & 0xFF0000) << 16) |
                ((rbv & 0xFF00) << 8) | (rbv & 0xFF);
            break;

        case 0x36:                                      /* PKWB */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            res = ((rbv >> 24) & 0xFF000000) | ((rbv >> 16) & 0xFF0000) |
                ((rbv >> 8) & 0xFF00) | (rbv & 0xFF);
            break;

        case 0x37:                                      /* PKLB */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            res = ((rbv >> 24) & 0xFF00) | (rbv & 0xFF);
            break;

        case 0x38:                                      /* MINSB8 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i++) {
                s1 = SEXT_B_Q (R[ra] >> (i << 3));
                s2 = SEXT_B_Q (rbv >> (i << 3));
                res = res | (((s1 <= s2)? R[ra]: rbv) & byte_mask[i]);
                }
            break;

        case 0x39:                                      /* MINSW4 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i = i++) {
                s1 = SEXT_W_Q (R[ra] >> (i << 4));
                s2 = SEXT_W_Q (rbv >> (i << 4));
                res = res | (((s1 <= s2)? R[ra]: rbv) & word_mask[i]);
                }
            break;

        case 0x3A:                                      /* MINUB8 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i++) {
                s64 = R[ra] & byte_mask[i];
                t64 = rbv & byte_mask[i];
                res = res | ((s64 <= t64)? s64: t64);
                }
            break;

        case 0x3B:                                      /* MINUW4 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i = i++) {
                s64 = R[ra] & word_mask[i];
                t64 = rbv & word_mask[i];
                res = res | ((s64 <= t64)? s64: t64);
                }
            break;

        case 0x3C:                                      /* MAXUB8 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i++) {
                s64 = R[ra] & byte_mask[i];
                t64 = rbv & byte_mask[i];
                res = res | ((s64 >= t64)? s64: t64);
                }
            break;

        case 0x3D:                                      /* MAXUW4 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i = i++) {
                s64 = R[ra] & word_mask[i];
                t64 = rbv & word_mask[i];
                res = res | ((s64 >= t64)? s64: t64);
                }
            break;

        case 0x3E:                                      /* MAXSB8 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i++) {
                s1 = SEXT_B_Q (R[ra] >> (i << 3));
                s2 = SEXT_B_Q (rbv >> (i << 3));
                res = res | (((s1 >= s2)? R[ra]: rbv) & byte_mask[i]);
                }
            break;

        case 0x3F:                                      /* MAXSW4 */
            if (!(arch_mask & AMASK_MVI)) ABORT (EXC_RSVI);
            for (i = 0, res = 0; i < 8; i = i++) {
                s1 = SEXT_W_Q (R[ra] >> (i << 4));
                s2 = SEXT_W_Q (rbv >> (i << 4));
                res = res | (((s1 >= s2)? R[ra]: rbv) & word_mask[i]);
                }
            break;

        case 0x70:                                      /* FTOIS */
            if (!(arch_mask & AMASK_FIX)) ABORT (EXC_RSVI);
            if (fpen == 0) ABORT (EXC_FPDIS);           /* flt point disabled? */
            res = op_sts (FR[ra]);
            break;

        case 0x78:                                      /* FTOIT */
            if (!(arch_mask & AMASK_FIX)) ABORT (EXC_RSVI);
            if (fpen == 0) ABORT (EXC_FPDIS);           /* flt point disabled? */
            res = FR[ra];
            break;

        default:
            ABORT (EXC_RSVI);
            }

        if (rc != 31) R[rc] = res & M64;
        break;

/* PAL hardware functions */

    case OP_PAL19:
        reason = pal_19 (ir);
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        break;

    case OP_PAL1B:
        reason = pal_1b (ir);
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        break;

    case OP_PAL1D:
        reason = pal_1d (ir);
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        break;

    case OP_PAL1E:
        reason = pal_1e (ir);
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        break;

    case OP_PAL1F:
        reason = pal_1f (ir);
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        break;

    case OP_PAL:                                        /* PAL code */
        fnc = I_GETPAL (ir);                            /* get function code */
        if ((fnc & 0x40) || (fnc >= 0xC0))              /* out of range? */
            ABORT (EXC_RSVI);
        reason = pal_proc_inst (fnc);                   /* processed externally */
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        break;

    default:
        ABORT (EXC_RSVI);
        }                                               /* end case */
    if (trap_summ) {                                    /* any traps? */
        reason = pal_proc_trap (trap_summ);             /* process trap */
        trap_summ = 0;                                  /* clear trap reg */
        trap_mask = 0;
        intr_summ = pal_eval_intr (1);                  /* eval interrupts */
        }
    }                                                   /* end while */
pcc_l = pcc_l & M32;
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
pc_align = ((uint32) PC) & 3;                           /* separate PC<1:0> */
PC = PC & 0xFFFFFFFFFFFFFFFC;
return reason;
}

/* Utility routines */

/* Byte zap function */

t_uint64 byte_zap (t_uint64 op, uint32 m)
{
int32 i;

m = m & 0xFF;                                           /* 8 bit mask */
for (i = 0; m != 0; m = m >> 1, i++) {
    if (m & 1) op = op & ~byte_mask[i];
    }
return op;
}

/* 64b * 64b unsigned multiply */

t_uint64 uemul64 (t_uint64 a, t_uint64 b, t_uint64 *hi)
{
t_uint64 ahi, alo, bhi, blo, rhi, rmid1, rmid2, rlo;

ahi = (a >> 32) & M32;
alo = a & M32;
bhi = (b >> 32) & M32;
blo = b & M32;
rhi = ahi * bhi;
rmid1 = ahi * blo;
rmid2 = alo * bhi;
rlo = alo * blo;
rhi = rhi + ((rmid1 >> 32) & M32) + ((rmid2 >> 32) & M32);
rmid1 = (rmid1 << 32) & M64;
rmid2 = (rmid2 << 32) & M64;
rlo = (rlo + rmid1) & M64;
if (rlo < rmid1) rhi = rhi + 1;
rlo = (rlo + rmid2) & M64;
if (rlo < rmid2) rhi = rhi + 1;
if (hi) *hi = rhi & M64;
return rlo;
}

/* 64b / 64b unsigned fraction divide */

t_uint64 ufdiv64 (t_uint64 dvd, t_uint64 dvr, uint32 prec, uint32 *sticky)
{
t_uint64 quo;
uint32 i;

quo = 0;                                                /* clear quotient */
for (i = 0; (i < prec) && dvd; i++) {                   /* divide loop */
    quo = quo << 1;                                     /* shift quo */
    if (dvd >= dvr) {                                   /* div step ok? */
        dvd = dvd - dvr;                                /* subtract */
        quo = quo + 1;                                  /* quo bit = 1 */
        }
    dvd = dvd << 1;                                     /* shift divd */
    }
quo = quo << (UF_V_NM - i + 1);                         /* shift quo */
if (sticky) *sticky = (dvd? 1: 0);                      /* set sticky bit */
return quo;                                             /* return quotient */
}

/* Set arithmetic trap */

void arith_trap (uint32 mask, uint32 ir)
{
uint32 rc = I_GETRC (ir);

trap_summ = trap_summ | mask;
if (ir & I_FTRP_S) trap_summ = trap_summ | TRAP_SWC;
if ((mask & TRAP_IOV) == 0) rc = rc + 32;
trap_mask = trap_mask | ((t_uint64) 1u << rc);
return;
}

/* Reset */

t_stat cpu_reset (DEVICE *dptr)
{
R[31] = 0;
FR[31] = 0;
pal_mode = 1;
dmapen = 0;
fpen = 1;
vax_flag = 0;
lock_flag = 0;
trap_summ = 0;
trap_mask = 0;
if (M == NULL) M = (t_uint64 *) calloc (((uint32) MEMSIZE) >> 3, sizeof (t_uint64));
if (M == NULL) return SCPE_MEM;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r) pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Bootstrap */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_ARG;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (vptr == NULL) return SCPE_ARG;
if (sw & SWMASK ('V') && dmapen) {
    addr = trans_c (addr);
    if (addr == M64) return STOP_MME;
    }
if (ADDR_IS_MEM (addr)) {
    *vptr = ReadPQ (addr);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (sw & SWMASK ('V') && dmapen) {
    addr = trans_c (addr);
    if (addr == M64) return STOP_MME;
    }
if (ADDR_IS_MEM (addr)) {
    WritePQ (addr, val);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Memory allocation */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
t_uint64 mc = 0;
uint32 i, clim;
t_uint64 *nM = NULL;

for (i = val; i < MEMSIZE; i = i + 8) mc = mc | M[i >> 3];
if ((mc != 0) && !get_yn ("Really truncate memory [N]?", FALSE))
    return SCPE_OK;
nM = (t_uint64 *) calloc (val >> 3, sizeof (t_uint64));
if (nM == NULL) return SCPE_MEM;
clim = (uint32) ((((uint32) val) < MEMSIZE)? val: MEMSIZE);
for (i = 0; i < clim; i = i + 8) nM[i >> 3] = M[i >>3];
free (M);
M = nM;
MEMSIZE = val;
return SCPE_OK;
}

/* Show virtual address */

t_stat cpu_show_virt (FILE *of, UNIT *uptr, int32 val, void *desc)
{
t_stat r;
char *cptr = (char *) desc;
t_uint64 va, pa;

if (cptr) {
    DEVICE *dptr = find_dev_from_unit (uptr);
    if (dptr == NULL) return SCPE_IERR;
    va = get_uint (cptr, 16, M64, &r);
    if (r == SCPE_OK) {
        pa = trans_c (va);
        if (pa == M64) {
            fprintf (of, "Translation error\n");
            return SCPE_OK;
            }
        fputs ("Virtual ", of);
        fprint_val (of, va, 16, 64, PV_LEFT);
        fputs (" = physical ", of);
        fprint_val (of, pa, 16, 64, PV_LEFT);
        fputc ('\n', of);
        return SCPE_OK;
        }
    }
fprintf (of, "Invalid argument\n");
return SCPE_OK;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++) hst[i].pc = 0;
    hst_p = 0;
    return SCPE_OK;
    }
lnt = (uint32) get_uint (cptr, 10, HIST_MAX, &r);
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

/* Print instruction trace */

t_stat cpu_fprint_one_inst (FILE *st, uint32 ir, t_uint64 pc, t_uint64 ra, t_uint64 rb)
{
uint32 op;
t_value sim_val;

static const int h_fmt[64] = {
    0,     0,     0,     0,     0,     0,     0,     0,
    H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF,
    H_IOP, H_IOP, H_IOP, H_IOP, H_FOP, H_FOP, H_FOP, H_FOP,
    0,     H_PAL, H_JMP, H_PAL, H_FOP, H_PAL, H_PAL, H_PAL,
    H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF,
    H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF, H_MRF,
    H_BRA, H_BRA, H_BRA, H_BRA, H_BRA, H_BRA, H_BRA, H_BRA,
    H_BRA, H_BRA, H_BRA, H_BRA, H_BRA, H_BRA, H_BRA, H_BRA
    };

pc = pc & ~HIST_PC;
fprint_val (st, pc, 16, 64, PV_RZRO);
fputc (' ', st);
op = I_GETOP (ir);                                      /* get opcode */
if (h_fmt[op] & H_A) fprint_val (st, ra, 16, 64, PV_RZRO);
else fputs ("                ", st);
fputc (' ', st);
if (h_fmt[op] & H_B) {                                  /* Rb? */
    t_uint64 rbv;
    if ((h_fmt[op] & H_B_LIT) && (ir & I_ILIT))
        rbv = I_GETLIT8 (ir);                           /* literal? rbv = lit */
    else rbv = rb;                                      /* no, rbv = R[rb] */
    fprint_val (st, rbv, 16, 64, PV_RZRO);
    }
else fputs ("                ", st);
fputc (' ', st);
if (h_fmt[op] & H_EA) {                                 /* ea? */
    t_uint64 ea;
    if (h_fmt[op] & H_EA_L16) ea = ir & M16;
    else if (h_fmt[op] & H_EA_B)
        ea = (pc + (SEXT_BDSP (I_GETBDSP (ir)) << 2)) & M64;
    else ea = (rb + SEXT_MDSP (I_GETMDSP (ir))) & M64;
    fprint_val (st, ea, 16, 64, PV_RZRO);
    }
else fputs ("                ", st);
fputc (' ', st);
if (pc & 4) sim_val = ((t_uint64) ir) << 32;
else sim_val = ir;
if ((fprint_sym (st, pc & ~03, &sim_val, &cpu_unit, SWMASK ('M'))) > 0)
    fprintf (st, "(undefined) %08X", ir);
fputc ('\n', st);                                       /* end line */
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc)
{
uint32 k, di, lnt;
char *cptr = (char *) desc;
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
fprintf (st, "PC               Ra               Rb               IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        cpu_fprint_one_inst (st, h->ir, h->pc, h->ra, h->rb);
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_OK;
}
