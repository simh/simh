/* i1620_cpu.c: IBM 1620 CPU simulator

   Copyright (c) 2002-2018, Robert M. Supnik

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

   This CPU module incorporates code and comments from the 1620 simulator by
   Geoff Kuenning, with his permission.

   05-Jun-18    RMS     Fixed bug in select index A (COVERITY)
   23-Jun-17    RMS     BS should not enable indexing unless configured
   15-Jun-17    RMS     Added more information to IO in progress message
   26-May-17    RMS     Added deferred IO mode for slow devices
   20-May-17    RMS     Changed to commit PC on certain stops
                        Added SET CPU RELEASE command
                        Undefined indicators don't throw an error (Dave Wise)
   19-May-17    RMS     Added Model I mode to allow record marks in adds (Dave Wise)
   18-May-17    RMS     Allowed undocumented indicator 8 (Dave Wise)
   13-Mar-17    RMS     Added error test on device addr (COVERITY)
   07-May-15    RMS     Added missing TFL instruction (Tom McBride)
   28-Mar-15    RMS     Revised to use sim_printf
   26-Mar-15    RMS     Separated compare from add/sub flows (Tom McBride)
                        Removed ADD_SIGNC return from add/sub flows
   10-Dec-13    RMS     Fixed several bugs in add and compare (Bob Armstrong)
                        Fixed handling of P field in K instruction (Bob Armstrong)
   28-May-06    RMS     Fixed bug in cpu history (Peter Schorn)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Nov-04    RMS     Added instruction history
   26-Mar-04    RMS     Fixed warnings with -std=c99
   02-Nov-03    RMS     Fixed bug in branch digit (Dave Babcock)
   21-Aug-03    RMS     Fixed bug in immediate index add (Michael Short)
   25-Apr-03    RMS     Changed t_addr to uint32 throughout
   18-Oct-02    RMS     Fixed bugs in invalid result testing (Hans Pufal)

   The simulated register state for the IBM 1620 is:

   1620         sim     comment

   IR1          [PC]    program counter
   IR2                  instruction register 2 (subroutine return address)
   OR1          [QAR]   Q address
   OR2          [PAR]   P address
   PR1                  manual save address
   ind[0:99]            indicators

   Additional internal registers OR3, PR2, and PR3 are not simulated.

   The IBM 1620 is a fixed instruction length, variable data length, decimal
   data system.  Memory consists of 20000 - 60000 BCD digits, each containing
   four bits of data and a flag.  There are no general registers; all
   instructions are memory to memory.

   The 1620 uses a fixed, 12 digit instruction format:

        oo ppppp qqqqq

   where

        oo      =       opcode
        ppppp   =       P (usually destination) address
        qqqqq   =       Q (usually source) address

   Immediate instructions use the qqqqq field as the second operand.

   The 1620 Model 1 uses table lookups for add and multiply; for that reason,
   it was nicknamed CADET (Can't Add, Doesn't Even Try).  The Model 2 does
   adds in hardware and uses the add table memory for index registers.

   The 1620 has no concept of overlapped IO. When an IO instruction is
   issued, instruction execution is suspended until the IO is complete.
   For "fast" devices, like the disk, IO is done in an instantaneous burst.
   "Slow" devices have the option of going character-by-character, with
   delays in between. This allows for operator intervention, such as
   ^E or changing paper tapes.

   The simulated IO state for character-by-character IO is:

   cpuio_mode           set if IO in progress
   cpuio_opc            saved IO opcode
   cpuio_dev            saved IO device number
   cpuio_cnt            character counter; increments
   PAR                  P address; increments

   This routine is the instruction decode routine for the IBM 1620.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        illegal addresses or instruction formats
        I/O error in I/O simulator

   2. Interrupts.  The 1620 has no interrupt structure.

   3. Non-existent memory.  On the 1620, all memory references
      are modulo the memory size.

   4. Adding I/O devices.  These modules must be modified:

        i1620_cpu.c     add iodisp table entry
        i1620_sys.c     add sim_devices table entry
*/

#include "i1620_defs.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = saved_PC

#define HIST_PC         0x40000000
#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    uint16              vld;
    uint16              pc;
    uint8               inst[INST_LEN];
    } InstHistory;

uint8 M[MAXMEMSIZE] = { 0 };                            /* main memory */
uint32 saved_PC = 0;                                    /* saved PC */
uint32 actual_PC = 0;                                   /* actual PC at halt */
uint32 IR2 = 1;                                         /* inst reg 2 */
uint32 PAR = 0;                                         /* P address */
uint32 QAR = 0;                                         /* Q address */
uint32 PR1 = 1;                                         /* proc reg 1 */
uint32 iae = 1;                                         /* ind addr enb */
uint32 idxe = 0;                                        /* index enable */
uint32 idxb = 0;                                        /* index band */
uint32 io_stop = 1;                                     /* I/O stop */
uint32 ar_stop = 1;                                     /* arith stop */
uint32 cpuio_inp = 0;                                   /* IO in progress */
uint32 cpuio_opc = 0;
uint32 cpuio_dev = 0;
uint32 cpuio_cnt = 0;
int32 ind_max = 16;                                     /* iadr nest limit */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */
uint8 ind[NUM_IND] = { 0 };                             /* indicators */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_opt1 (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_opt2 (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_save (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_table (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_release (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_cps (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_cps (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

int32 get_2d (uint32 ad);
t_stat get_addr (uint32 alast, int32 lnt, t_bool indexok, uint32 *addr);
t_stat cvt_addr (uint32 alast, int32 lnt, t_bool signok, int32 *val);
t_stat get_idx (uint32 aidx);
t_stat xmt_field (uint32 d, uint32 s, uint32 skp);
t_stat xmt_record (uint32 d, uint32 s, t_bool cpy);
t_stat xmt_index (uint32 d, uint32 s);
t_stat xmt_divd (uint32 d, uint32 s);
t_stat xmt_tns (uint32 d, uint32 s);
t_stat xmt_tnf (uint32 d, uint32 s);
t_stat add_field (uint32 d, uint32 s, t_bool sub, uint32 skp, int32 *sta);
t_stat cmp_field (uint32 d, uint32 s);
uint32 add_one_digit (uint32 dst, uint32 src, uint32 *cry);
t_stat mul_field (uint32 mpc, uint32 mpy);
t_stat mul_one_digit (uint32 mpyd, uint32 mpcp, uint32 prop, uint32 last);
t_stat div_field (uint32 dvd, uint32 dvr, int32 *ez);
t_stat div_one_digit (uint32 dvd, uint32 dvr, uint32 max, uint32 *quod, uint32 *quop);
t_stat oct_to_dec (uint32 tbl, uint32 s);
t_stat dec_to_oct (uint32 d, uint32 tbl, int32 *ez);
t_stat or_field (uint32 d, uint32 s);
t_stat and_field (uint32 d, uint32 s);
t_stat xor_field (uint32 d, uint32 s);
t_stat com_field (uint32 d, uint32 s);
void upd_ind (void);

extern t_stat tty (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat ptp (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat ptr (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat cdp (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat cdr (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat dp (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat lpt (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat btp (uint32 op, uint32 pa, uint32 f0, uint32 f1);
extern t_stat btr (uint32 op, uint32 pa, uint32 f0, uint32 f1);

extern t_stat fp_add (uint32 d, uint32 s, t_bool sub);
extern t_stat fp_mul (uint32 d, uint32 s);
extern t_stat fp_div (uint32 d, uint32 s);
extern t_stat fp_fsl (uint32 d, uint32 s);
extern t_stat fp_fsr (uint32 d, uint32 s);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX+UNIT_BCD+MI_STD, MAXMEMSIZE) };

REG cpu_reg[] = {
    { DRDATA (PC, saved_PC, 16), PV_LEFT },
    { DRDATA (APC, actual_PC, 16), PV_LEFT + REG_HRO },
    { DRDATAD (IR2, IR2, 16, "instruction storage address register (PC)"), PV_LEFT },
    { DRDATAD (PR1, PR1, 16, "processor register 1"), PV_LEFT },
    { DRDATAD (PAR, PAR, 16, "P address register (OR2)"), PV_LEFT + REG_RO },
    { DRDATAD (QAR, QAR, 16, "Q address register (OR1)"), PV_LEFT + REG_RO },
    { FLDATAD (SW1, ind[IN_SW1], 0, "sense switch 1" ) },
    { FLDATAD (SW2, ind[IN_SW2], 0, "sense switch 2" ) },
    { FLDATAD (SW3, ind[IN_SW3], 0, "sense switch 3" ) },
    { FLDATAD (SW4, ind[IN_SW4], 0, "sense switch 4" ) },
    { FLDATAD (HP, ind[IN_HP], 0, "high/positive indicator") },
    { FLDATAD (EZ, ind[IN_EZ], 0, "equal/zero indicator") },
    { FLDATA (OVF, ind[IN_OVF], 0) },
    { FLDATA (EXPCHK, ind[IN_EXPCHK], 0) },
    { FLDATA (RDCHK, ind[IN_RDCHK], 0) },
    { FLDATA (WRCHK, ind[IN_WRCHK], 0) },
    { FLDATAD (ARSTOP, ar_stop, 0, "arith stop") },
    { FLDATAD (IOSTOP, io_stop, 0, "I/O stop") },
    { FLDATAD (IOINP, cpuio_inp, 0, "IO in progress"), REG_RO },
    { DRDATAD (IOOPC, cpuio_opc, 6, "IO opcode"), REG_RO },
    { DRDATAD (IODEV, cpuio_dev, 7, "IO device"), REG_RO },
    { DRDATA (IOCNT, cpuio_cnt, 16), REG_RO },
    { BRDATA (IND, ind, 10, 1, NUM_IND) },
    { FLDATAD (IAE, iae, 0, "indirect address enable (Model 2 only)") },
    { FLDATAD (IDXE, idxe, 0, "indexing enable (Model 2 only)") },
    { FLDATAD (IDXB, idxb, 0, "indexing band select (Model 2 only)") },
    { DRDATA (INDMAX, ind_max, 16), REG_NZ + PV_LEFT },
    { BRDATA (PCQ, pcq, 10, 14, PCQ_SIZE), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { ORDATAD (WRU, sim_int_char, 8, "interrupt character") },
    { NULL }
    };

MTAB cpu_mod[] = {
    { IF_IA, IF_IA, "IA", "IA", &cpu_set_opt1, NULL, NULL, "enable indirect addressing" },
    { IF_IA, 0, "no IA", "NOIA", &cpu_set_opt1, NULL, NULL, "disable indirect addressing" },
    { IF_EDT, IF_EDT, "EDT", "EDT", &cpu_set_opt1, NULL, NULL, "enable extra editing instructions" },
    { IF_EDT, 0, "no EDT", "NOEDT", &cpu_set_opt1, NULL, NULL, "disable extra editing instructions" },
    { IF_DIV, IF_DIV, "DIV", "DIV", &cpu_set_opt1, NULL, NULL, "enable divide instructions" },
    { IF_DIV, 0, "no DIV", "NODIV", &cpu_set_opt1, NULL, NULL, "disable divide instructions" },
    { IF_IDX, IF_IDX, "IDX", "IDX", &cpu_set_opt2, NULL, NULL, "enable indexing" },
    { IF_IDX, 0, "no IDX", "NOIDX", &cpu_set_opt2, NULL, NULL, "disable indexing" },
    { IF_BIN, IF_BIN, "BIN", "BIN", &cpu_set_opt2, NULL, NULL, "enable binary instructions" },
    { IF_BIN, 0, "no BIN", "NOBIN", &cpu_set_opt2, NULL, NULL, "disable binary instructions" },
    { IF_FP, IF_FP, "FP", "FP", NULL, NULL, NULL, "disable record marks in add/sub/compare" },
    { IF_FP, 0, "no FP", "NOFP", NULL, NULL, NULL, "disable record marks in add/sub/compare" },
    { IF_RMOK, IF_RMOK, "RM allowed", "RMOK", &cpu_set_opt1, NULL, NULL, "enable record marks in add/sub/compare" },
    { IF_RMOK, 0, "RM disallowed", "NORMOK", &cpu_set_opt1, NULL, NULL, "disable record marks in add/sub/compare" },
    { IF_MII, 0, "Model 1", "MOD1", &cpu_set_model, NULL, NULL, "set Model 1" },
    { IF_MII, IF_MII, "Model 2", "MOD2", &cpu_set_model, NULL, NULL, "set Model 2" },
    { UNIT_MSIZE, 20000, NULL, "20K", &cpu_set_size, NULL, NULL, "set memory size = 20K" },
    { UNIT_MSIZE, 40000, NULL, "40K", &cpu_set_size, NULL, NULL, "set memory size = 40K" },
    { UNIT_MSIZE, 60000, NULL, "60K", &cpu_set_size, NULL, NULL, "set memory size = 60K" },
    { UNIT_MSIZE, 0, NULL, "SAVE", &cpu_set_save },
    { UNIT_MSIZE, 0, NULL, "TABLE", &cpu_set_table },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist, NULL, "Displays instruction history" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "RELEASE",
      &cpu_set_release, NULL, NULL, "Release/Complete pending I/O" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_VALR, 0, "CPS", "CPS",
      &cpu_set_cps, &cpu_show_cps, NULL, "set characters per second" },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 18, 1, 16, 5,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
    };

/* Instruction table */

const int32 op_table[100] = {
    0,                                                  /* 0 */
    IF_FP + IF_VPA + IF_VQA,                            /* FADD */
    IF_FP + IF_VPA + IF_VQA,                            /* FSUB */
    IF_FP + IF_VPA + IF_VQA,                            /* FMUL */
    0,
    IF_FP + IF_VPA + IF_VQA,                            /* FSL */
    IF_FP + IF_MII + IF_VPA + IF_VQA,                   /* TFL */
    IF_FP + IF_MII + IF_VPA + IF_VQA,                   /* BTFL */
    IF_FP + IF_VPA + IF_VQA,                            /* FSR */
    IF_FP + IF_VPA + IF_VQA,                            /* FDV */
    IF_MII + IF_VPA + IF_IMM,                           /* 10: BTAM */
    IF_VPA + IF_IMM,                                    /* AM */
    IF_VPA + IF_IMM,                                    /* SM */
    IF_VPA + IF_IMM,                                    /* MM */
    IF_VPA + IF_IMM,                                    /* CM */
    IF_VPA + IF_IMM,                                    /* TDM */
    IF_VPA + IF_IMM,                                    /* TFM */
    IF_VPA + IF_IMM,                                    /* BTM */
    IF_DIV + IF_VPA + IF_IMM,                           /* LDM */
    IF_DIV + IF_VPA + IF_IMM,                           /* DM */
    IF_MII + IF_VPA + IF_VQA,                           /* 20: BTA */
    IF_VPA + IF_VQA,                                    /* A */
    IF_VPA + IF_VQA,                                    /* S */
    IF_VPA + IF_VQA,                                    /* M */
    IF_VPA + IF_VQA,                                    /* C */
    IF_VPA + IF_VQA,                                    /* TD */
    IF_VPA + IF_VQA,                                    /* TF */
    IF_VPA + IF_VQA,                                    /* BT */
    IF_DIV + IF_VPA + IF_VQA,                           /* LD */
    IF_DIV + IF_VPA + IF_VQA,                           /* D */
    IF_MII + IF_VPA + IF_VQA,                           /* 30: TRNM */
    IF_VPA + IF_VQA,                                    /* TR */
    IF_VPA,                                             /* SF */
    IF_VPA,                                             /* CF */
    0,                                                  /* K */
    IF_VPA,                                             /* DN */
    IF_VPA,                                             /* RN */
    IF_VPA,                                             /* RA */
    IF_VPA,                                             /* WN */
    IF_VPA,                                             /* WA */
    0,                                                  /* 40 */
    0,                                                  /* NOP */
    0,                                                  /* BB */
    IF_VPA + IF_VQA,                                    /* BD */
    IF_VPA + IF_VQA,                                    /* BNF */
    IF_VPA + IF_VQA,                                    /* BNR */
    IF_VPA,                                             /* BI */
    IF_VPA,                                             /* BNI */
    0,                                                  /* H */
    IF_VPA,                                             /* B */
    0,                                                  /* 50 */
    0,
    0,
    0,
    0,
    IF_VPA + IF_VQA,                                    /* BNG - disk sys */
    0,
    0,
    0,
    0,
    IF_MII + IF_VPA,                                    /* 60: BS */
    IF_IDX + IF_VPA + IF_NQX,                           /* BX */
    IF_IDX + IF_VPA + IF_IMM,                           /* BXM */
    IF_IDX + IF_VPA + IF_NQX,                           /* BCX */
    IF_IDX + IF_VPA + IF_IMM,                           /* BCXM */
    IF_IDX + IF_VPA + IF_NQX,                           /* BLX */
    IF_IDX + IF_VPA + IF_IMM,                           /* BLXM */
    IF_IDX + IF_VPA + IF_NQX,                           /* BSX */
    0,
    0,
    IF_IDX + IF_VPA + IF_VQA,                           /* 70: MA */
    IF_EDT + IF_VPA + IF_VQA,                           /* MF */
    IF_EDT + IF_VPA + IF_VQA,                           /* TNS */
    IF_EDT + IF_VPA + IF_VQA,                           /* TNF */
    0,
    0,
    0,
    0,
    0,
    0,
    0,                                                  /* 80 */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    IF_BIN + IF_VPA + IF_4QA,                           /* 90: BBT */
    IF_BIN + IF_VPA + IF_4QA,                           /* BMK */
    IF_BIN + IF_VPA + IF_VQA,                           /* ORF */
    IF_BIN + IF_VPA + IF_VQA,                           /* ANDF */
    IF_BIN + IF_VPA + IF_VQA,                           /* CPLF */
    IF_BIN + IF_VPA + IF_VQA,                           /* EORF */
    IF_BIN + IF_VPA + IF_VQA,                           /* OTD */
    IF_BIN + IF_VPA + IF_VQA,                           /* DTO */
    0,
    0
    };

/* IO dispatch table */

t_stat (*iodisp[NUM_IO])(uint32 op, uint32 pa, uint32 f0, uint32 f1) = {
    NULL, &tty, &ptp, &ptr, &cdp,                       /* 00 - 09 */
    &cdr, NULL, &dp,  NULL, &lpt,
    NULL, NULL, NULL, NULL, NULL,                       /* 10 - 19 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,                       /* 20 - 29 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, &btp, &btr, NULL,                       /* 30 - 39 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,                       /* 40 - 49 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,                       /* 50 - 59 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,                       /* 60 - 69 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,                       /* 70 - 79 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,                       /* 80 - 89 */
    NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL,                       /* 90 - 99 */
    NULL, NULL, NULL, NULL, NULL
    };

/* K instruction validate P field table */

const uint8 k_valid_p[NUM_IO] = {
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

/* Indicator table: -1 = undefined, +1 = resets when tested */
/* Indicator 8 is MAR CHECK, for maintenance use only */
/* Undefined indicators always read as 0 */

const int32 ind_table[NUM_IND] = {
    -1,  0,  0,  0,  0, -1,  1,  1,  0,  1,             /* 00 - 09 */
    -1,  0,  0,  0,  1,  1,  1,  1, -1,  0,             /* 10 - 19 */
    -1, -1, -1, -1, -1,  0, -1, -1, -1, -1,             /* 20 - 29 */
     0,  0,  0,  1,  1,  0,  1,  1,  1,  0,             /* 30 - 39 */
    -1, -1,  1, -1, -1, -1, -1, -1, -1, -1,             /* 40 - 49 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,             /* 50 - 59 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,             /* 60 - 69 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,             /* 70 - 79 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,             /* 80 - 89 */
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1              /* 90 - 99 */
    };

/* Add table for 1620 Model 1 */

const uint8 std_add_table[ADD_TABLE_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x10,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x10, 0x11,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12,
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13,
    0x05, 0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14,
    0x06, 0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x07, 0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x08, 0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x09, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
    };

/* Add table for 1620 Model 2 ("hardware add") */

const uint8 sum_table[20] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19
    };

/* Multiply table */

const uint8 std_mul_table[MUL_TABLE_LEN] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 2, 0, 3, 0, 4, 0,
    0, 0, 2, 0, 4, 0, 6, 0, 8, 0,
    0, 0, 3, 0, 6, 0, 9, 0, 2, 1,
    0, 0, 4, 0, 8, 0, 2, 1, 6, 1,
    0, 0, 5, 0, 0, 1, 5, 1, 0, 2,
    0, 0, 6, 0, 2, 1, 8, 1, 4, 2,
    0, 0, 7, 0, 4, 1, 1, 2, 8, 2,
    0, 0, 8, 0, 6, 1, 4, 2, 2, 3,
    0, 0, 9, 0, 8, 1, 7, 2, 6, 3,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    5, 0, 6, 0, 7, 0, 8, 0, 9, 0,
    0, 1, 2, 1, 4, 1, 6, 1, 8, 1,
    5, 1, 8, 1, 1, 2, 4, 2, 7, 2,
    0, 2, 4, 2, 8, 2, 2, 3, 6, 3,
    5, 2, 0, 3, 5, 3, 0, 4, 5, 4,
    0, 3, 6, 3, 2, 4, 8, 4, 4, 5,
    5, 3, 2, 4, 9, 4, 6, 5, 3, 6,
    0, 4, 8, 4, 6, 5, 4, 6, 2, 7,
    5, 4, 4, 5, 3, 6, 2, 7, 1, 8
    };

/* Table of stop codes that commit PC before returning to SCP */

static t_stat commit_pc[] = {
    STOP_HALT, SCPE_STOP, STOP_NOCD, SCPE_EOF, SCPE_IOERR, 0
    };

#define BRANCH(x)       PCQ_ENTRY; PC = (x)
#define GET_IDXADDR(x)  ((idxb? IDX_B: IDX_A) + ((x) * ADDR_LEN) + (ADDR_LEN - 1))

t_stat sim_instr (void)
{
uint32 PC, pla, qla, f0, f1;
int32 i, t, idx, flags, sta, dev, op;
t_stat reason;

/* Restore saved state */

PC = saved_PC;
if ((cpu_unit.flags & IF_IA) == 0)
    iae = 0;
if ((cpu_unit.flags & IF_IDX) == 0)
    idxe = idxb = 0;
upd_ind ();                                             /* update indicators */
reason = SCPE_OK;

/* Main instruction fetch/decode loop */

while (reason == SCPE_OK) {                             /* loop until halted */

    saved_PC = PC;                                      /* commit prev instr */
    if (sim_interval <= 0) {                            /* check clock queue */
        if ((reason = sim_process_event ()))
            break;
        }
    if (cpuio_inp != 0) {                               /* IO in progress? */
        sim_interval = sim_interval - 1;                /* tick & continue */
        continue;
        }

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

    sim_interval = sim_interval - 1;

/* Instruction fetch and address decode */

    if (PC & 1) {                                       /* PC odd? */
        reason = STOP_INVIAD;                           /* stop */
        break;
        }

    op = get_2d (PC);                                   /* get opcode */
    if (op < 0) {                                       /* invalid? */
        reason = STOP_INVINS;
        break;
        }
    flags = op_table[op];                               /* get op, flags */
    if ((flags & ALLOPT) &&                             /* need option? */
        !(flags & ALLOPT & cpu_unit.flags)) {           /* any set? */
        reason = STOP_INVINS;                           /* no, error */
        break;
        }

    pla = ADDR_A (PC, I_PL);                            /* P last addr */
    qla = ADDR_A (PC, I_QL);                            /* Q last addr */
    if (flags & IF_VPA) {                               /* need P? */
        reason = get_addr (pla, 5, TRUE, &PAR);         /* get P addr */
        if (reason != SCPE_OK)                          /* stop if error */
            break;
        }
    if (flags & (IF_VQA | IF_4QA | IF_NQX)) {           /* need Q? */
        reason = get_addr (qla,                         /* get Q addr */
            ((flags & IF_4QA)? 4: 5),                   /* 4 or 5 digits */
            ((flags & IF_NQX)? FALSE: TRUE),            /* not or indexed */
            &QAR);
        if (reason != SCPE_OK) {                        /* stop if invalid */
            reason = reason + (STOP_INVQDG - STOP_INVPDG);
            break;
            }
        }
    else if (flags & IF_IMM)                            /* immediate? */
        QAR = qla;

    if (hst_lnt) {                                      /* history enabled? */
        hst_p = (hst_p + 1);                            /* next entry */
        if (hst_p >= hst_lnt)
            hst_p = 0;
        hst[hst_p].vld = 1;
        hst[hst_p].pc = PC;     
        for (i = 0; i < INST_LEN; i++)
            hst[hst_p].inst[i] = M[(PC + i) % MEMSIZE];
        }

    PC = ADDR_A (PC, INST_LEN);                         /* advance PC */
    switch (op) {                                       /* case on op */

/* Transmit digit - P,Q are valid */

    case OP_TD:
    case OP_TDM:
        M[PAR] = M[QAR] & (FLAG | DIGIT);               /* move dig, flag */
        break;

/* Transmit field - P,Q are valid */

    case OP_TF:
    case OP_TFM:
        reason = xmt_field (PAR, QAR, 1);               /* xmit field */
        break;

/* Transmit floating - P,Q are valid */

    case OP_TFL:
        reason = xmt_field (PAR, QAR, 3);               /* xmit field */
        break;

/* Transmit record - P,Q are valid */

    case OP_TR:
        reason = xmt_record (PAR, QAR, TRUE);           /* xmit record */
        break;

/* Transmit record no record mark - P,Q are valid */

    case OP_TRNM:
        reason = xmt_record (PAR, QAR, FALSE);          /* xmit record but */
        break;                                          /* not rec mark */

/* Set flag - P is valid */

    case OP_SF:
        M[PAR] = M[PAR] | FLAG;                         /* set flag on P */
        break;

/* Clear flag - P is valid */

    case OP_CF:
        M[PAR] = M[PAR] & ~FLAG;                        /* clear flag on P */
        break;

/* Branch - P is valid */

    case OP_B:
        BRANCH (PAR);                                   /* branch to P */
        break;

/* Branch and transmit - P,Q are valid */

    case OP_BT:
    case OP_BTM:
        reason = xmt_field (ADDR_S (PAR, 1), QAR, 1);   /* xmit field to P-1 */
        IR2 = PC;                                       /* save PC */
        BRANCH (PAR);                                   /* branch to P */
        break;

/* Branch and transmit floating - P,Q are valid */

    case OP_BTFL:
        reason = xmt_field (ADDR_S (PAR, 1), QAR, 3);   /* skip 3 flags */
        IR2 = PC;                                       /* save PC */
        BRANCH (PAR);                                   /* branch to P */
        break;

/* Branch and transmit address - P,Q are valid */

    case OP_BTA:
    case OP_BTAM:
        reason = xmt_field (ADDR_S (PAR, 1), QAR, 4);   /* skip 4 flags */
        IR2 = PC;                                       /* save PC */
        BRANCH (PAR);                                   /* branch to P */
        break;

/* Branch back */

    case OP_BB:
        if (PR1 != 1) {                                 /* PR1 valid? */
            BRANCH (PR1);                               /* return to PR1 */
            PR1 = 1;                                    /* invalidate */
            }
        else if (IR2 != 1) {                            /* IR2 valid? */
            BRANCH (IR2);                               /* return to IR2 */
            IR2 = 1;                                    /* invalidate */
            }
        else reason = STOP_INVRTN;                      /* MAR check */
        break;

/* Branch on digit (not zero) - P,Q are valid */

    case OP_BD:
        if ((M[QAR] & DIGIT) != 0) {                    /* digit != 0? */
            BRANCH (PAR);                               /* branch */
            }
        break;

/* Branch no flag - P,Q are valid */

    case OP_BNF:
        if ((M[QAR] & FLAG) == 0) {                     /* flag == 0? */
            BRANCH (PAR);                               /* branch */
            }
        break;

/* Branch no record mark (8-2 not set) - P,Q are valid */

    case OP_BNR:
        if ((M[QAR] & REC_MARK) != REC_MARK) {          /* not rec mark? */
            BRANCH (PAR);                               /* branch */
            }
        break;

/* Branch no group mark - P,Q are valid */

    case OP_BNG:
        if ((M[QAR] & DIGIT) != GRP_MARK) {             /* not grp mark? */
            BRANCH (PAR);                               /* branch */
            }
        break;

/* Branch (no) indicator - P is valid */

    case OP_BI:
    case OP_BNI:
        upd_ind ();                                     /* update indicators */
        t = get_2d (ADDR_A (saved_PC, I_BR));           /* get ind number */
        if (t < 0) {                                    /* not valid? */
            reason = STOP_INVIND;                       /* stop */
            break;
            }
        if ((ind[t] != 0) ^ (op == OP_BNI)) {           /* ind value correct? */
            BRANCH (PAR);                               /* branch */
            }
        if (ind_table[t] > 0)                           /* reset if needed */
            ind[t] = 0;
        break;

/* Add/subtract/compare - P,Q are valid */

    case OP_A:
    case OP_AM:
        reason = add_field (PAR, QAR, FALSE, 0, &sta);  /* add */
        if (sta == ADD_CARRY)                           /* cout => ovflo */
            ind[IN_OVF] = 1;
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        break;

    case OP_S:
    case OP_SM:
        reason = add_field (PAR, QAR, TRUE, 0, &sta);   /* sub, store */
        if (sta == ADD_CARRY)                           /* cout => ovflo */
            ind[IN_OVF] = 1;
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        break;

/* IBM's diagnostics try a compare that generates a carry out; it does not
   generate overflow. Therefore, do not set overflow on a carry out status. */

    case OP_C:
    case OP_CM:
        reason = cmp_field (PAR, QAR);                  /* compare */
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        break;

/* Multiply - P,Q are valid */

    case OP_M:
    case OP_MM:
        reason = mul_field (PAR, QAR);                  /* multiply */
        break;

/* IO instructions - P is valid, except for K */

    case OP_RA:
    case OP_WA:
        if ((PAR & 1) == 0) {                           /* P even? */
            reason = STOP_INVEAD;                       /* stop */
            break;
            }
    case OP_DN:
    case OP_RN:
    case OP_WN:
        dev = get_2d (ADDR_A (saved_PC, I_IO));         /* get IO dev */
        f0 = M[ADDR_A (saved_PC, I_CTL)] & DIGIT;       /* get function */
        f1 = M[ADDR_A (saved_PC, I_CTL + 1)] & DIGIT;
        if ((dev < 0) || (iodisp[dev] == NULL))         /* undefined dev? */
            reason = STOP_INVIO;                        /* stop */
        else reason = iodisp[dev] (op, PAR, f0, f1);    /* call device */
        break;

    case OP_K:
        dev = get_2d (ADDR_A (saved_PC, I_IO));         /* get IO dev */
        if (dev < 0)                                    /* invalid digits? */
            return STOP_INVDIG;
        if (k_valid_p[dev]) {                           /* validate P? */
            reason = get_addr (pla, 5, TRUE, &PAR);     /* get P addr */
            if (reason != SCPE_OK)                      /* stop if error */
                 break;
            }
        else PAR = 0;
        f0 = M[ADDR_A (saved_PC, I_CTL)] & DIGIT;       /* get function */
        f1 = M[ADDR_A (saved_PC, I_CTL + 1)] & DIGIT;
        if (iodisp[dev] == NULL)                        /* undefined dev? */
            reason = STOP_INVIO;                        /* stop */
        else reason = iodisp[dev] (op, PAR, f0, f1);    /* call device */
        break;

/* Divide special feature instructions */

    case OP_LD:
    case OP_LDM:
        for (i = 0; i < PROD_AREA_LEN; i++)             /* clear prod area */
            M[PROD_AREA + i] = 0;
        t = M[QAR] & FLAG;                              /* save Q sign */
        reason = xmt_divd (PAR, QAR);                   /* xmit dividend */
        M[PROD_AREA + PROD_AREA_LEN - 1] |= t;          /* set sign */
        break;

/* Divide - P,Q are valid */

    case OP_D:
    case OP_DM:
        reason = div_field (PAR, QAR, &t);              /* divide */
        ind[IN_EZ] = t;                                 /* set indicator */
        if ((reason == STOP_OVERFL) && !ar_stop)        /* ovflo stop? */
            reason = SCPE_OK;                           /* no */
        break;

/* Edit special feature instructions */

/* Move flag - P,Q are valid */

    case OP_MF:
        M[PAR] = (M[PAR] & ~FLAG) | (M[QAR] & FLAG);    /* copy Q flag */
        M[QAR] = M[QAR] & ~FLAG;                        /* clr Q flag */
        break;

/* Transmit numeric strip - P,Q are valid, P is source */

    case OP_TNS:
        if ((PAR & 1) == 0) {                           /* P must be odd */
            reason = STOP_INVEAD;
            break;
            }
        reason = xmt_tns (QAR, PAR);                    /* xmit and strip */
        break;

/* Transmit numeric fill - P,Q are valid */

    case OP_TNF:
        if ((PAR & 1) == 0) {                           /* P must be odd */
            reason = STOP_INVEAD;
            break;
            }
        reason = xmt_tnf (PAR, QAR);                    /* xmit and strip */
        break;

/* Index special feature instructions */

/* Move address - P,Q are valid */

    case OP_MA:
        for (i = 0; i < ADDR_LEN; i++) {                /* move 5 digits */
            M[PAR] = (M[PAR] & FLAG) | (M[QAR] & DIGIT);
            MM (PAR); MM (QAR);
            }
        break;

/* Branch load index - P,Q are valid, Q not indexed */

    case OP_BLX:
    case OP_BLXM:
        idx = get_idx (ADDR_A (saved_PC, I_QL - 1));    /* get index */
        if (idx < 0) {                                  /* disabled? */
            reason = STOP_INVIDX;                       /* stop */
            break;
            }
        xmt_index (GET_IDXADDR (idx), QAR);             /* copy Q to idx */
        BRANCH (PAR);                                   /* branch to P */
        break;

/* Branch store index - P,Q are valid, Q not indexed */

    case OP_BSX:
        idx = get_idx (ADDR_A (saved_PC, I_QL - 1));    /* get index */
        if (idx < 0) {                                  /* disabled? */
            reason = STOP_INVIDX;                       /* stop */
            break;
            }
        xmt_index (QAR, GET_IDXADDR (idx));             /* copy idx to Q */
        BRANCH (PAR);                                   /* branch to P */
        break;

/* Branch and modify index - P,Q are valid, Q not indexed */

    case OP_BX:
        idx = get_idx (ADDR_A (saved_PC, I_QL - 1));    /* get index */
        if (idx < 0) {                                  /* disabled? */
            reason = STOP_INVIDX;                       /* stop */
            break;
            }
        reason = add_field (GET_IDXADDR (idx), QAR, FALSE, 0, &sta);
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        BRANCH (PAR);                                   /* branch to P */
        break;

    case OP_BXM:
        idx = get_idx (ADDR_A (saved_PC, I_QL - 1));    /* get index */
        if (idx < 0) {                                  /* disabled? */
            reason = STOP_INVIDX;                       /* stop */
            break;
            }
        reason = add_field (GET_IDXADDR (idx), QAR, FALSE, 3, &sta);
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        BRANCH (PAR);                                   /* branch to P */
        break;

/* Branch conditionally and modify index - P,Q are valid, Q not indexed */

    case OP_BCX:
        idx = get_idx (ADDR_A (saved_PC, I_QL - 1));    /* get index */
        if (idx < 0) {                                  /* disabled? */
            reason = STOP_INVIDX;                       /* stop */
            break;
            }
        reason = add_field (GET_IDXADDR (idx), QAR, FALSE, 0, &sta);
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        if ((ind[IN_EZ] == 0) && (sta == ADD_NOCRY)) {  /* ~z, ~c, ~sign chg? */
            BRANCH (PAR);                               /* branch */
            }
        break;

    case OP_BCXM:
        idx = get_idx (ADDR_A (saved_PC, I_QL - 1));    /* get index */
        if (idx < 0) {                                  /* disabled? */
            reason = STOP_INVIDX;                       /* stop */
            break;
            }
        reason = add_field (GET_IDXADDR (idx), QAR, FALSE, 3, &sta);
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        if ((ind[IN_EZ] == 0) && (sta == ADD_NOCRY)) {  /* ~z, ~c, ~sign chg? */
            BRANCH (PAR);                               /* branch */
            }
        break;

/* Branch and select - P is valid - Model 2 only */

    case OP_BS:
        t = M[ADDR_A (saved_PC, I_SEL)] & DIGIT;        /* get select */
        switch (t) {                                    /* case on select */
        case 0:
            idxe = idxb = 0;                            /* indexing off */
            break;
        case 1:
            if ((cpu_unit.flags & IF_IDX) != 0)         /* indexing present? */
                idxe = 1, idxb = 0;                     /* index band A */
            break;
        case 2:
            if ((cpu_unit.flags & IF_IDX) != 0)         /* indexing present? */
                idxe = idxb = 1;                        /* index band B */
            break;
        case 8:
            iae = 0;                                    /* indirect off */
            break;
        case 9:
            iae = 1;                                    /* indirect on */
            break;
        default:
            reason = STOP_INVSEL;                       /* undefined */
            break;
            }
        BRANCH (PAR);
        break;

/* Binary special feature instructions */

/* Branch on bit - P,Q are valid, Q is 4d address */

    case OP_BBT:
        t = M[ADDR_A (saved_PC, I_Q)];                  /* get Q0 digit */
        if (t & M[QAR] & DIGIT) {                       /* match to mem? */
            BRANCH (PAR);                               /* branch */
            }
        break;

/* Branch on mask - P,Q are valid, Q is 4d address */

    case OP_BMK:
        t = M[ADDR_A (saved_PC, I_Q)];                  /* get Q0 digit */
        if (((t ^ M[QAR]) &                             /* match to mem? */
            ((t & FLAG)? (FLAG + DIGIT): DIGIT)) == 0) {
            BRANCH (PAR);                               /* branch */
            }
        break;

/* Or - P,Q are valid */

    case OP_ORF:
        reason = or_field (PAR, QAR);                   /* OR fields */
        break;

/* AND - P,Q are valid */

    case OP_ANDF:
        reason = and_field (PAR, QAR);                  /* AND fields */
        break;

/* Exclusive or - P,Q are valid */

    case OP_EORF:
        reason = xor_field (PAR, QAR);                  /* XOR fields */
        break;

/* Complement - P,Q are valid */

    case OP_CPLF:
        reason = com_field (PAR, QAR);                  /* COM field */
        break;

/* Octal to decimal - P,Q are valid */

    case OP_OTD:
        reason = oct_to_dec (PAR, QAR);                 /* convert */
        break;

/* Decimal to octal - P,Q are valid */

    case OP_DTO:
        reason = dec_to_oct (PAR, QAR, &t);             /* convert */
        ind[IN_EZ] = t;                                 /* set indicator */
        if (ar_stop && ind[IN_OVF])
            reason = STOP_OVERFL;
        break;

/* Floating point special feature instructions */

    case OP_FADD:
        reason = fp_add (PAR, QAR, FALSE);              /* add */
        if (ar_stop && ind[IN_EXPCHK])
            reason = STOP_EXPCHK;
        break;

    case OP_FSUB:
        reason = fp_add (PAR, QAR, TRUE);               /* subtract */
        if (ar_stop && ind[IN_EXPCHK])
            reason = STOP_EXPCHK;
        break;

    case OP_FMUL:
        reason = fp_mul (PAR, QAR);                     /* multiply */
        if (ar_stop && ind[IN_EXPCHK])
            reason = STOP_EXPCHK;
        break;

    case OP_FDIV:
        reason = fp_div (PAR, QAR);                     /* divide */
        if (ar_stop && ind[IN_OVF])
            reason = STOP_FPDVZ;
        if (ar_stop && ind[IN_EXPCHK])
            reason = STOP_EXPCHK;
        break;

    case OP_FSL:
        reason = fp_fsl (PAR, QAR);                     /* shift left */
        break;

    case OP_FSR:
        reason = fp_fsr (PAR, QAR);                     /* shift right */
        break;

/* Halt */

    case OP_H:
        reason = STOP_HALT;                             /* stop */
        break;

/* NOP */

    case OP_NOP:
        break;

/* Invalid instruction code */

    default:
        reason = STOP_INVINS;                           /* stop */
        break;
        }                                               /* end switch */
    }                                                   /* end while */

/* Simulation halted */

for (i = 0; commit_pc[i] != 0; i++) {                   /* check stop code */
    if (reason == commit_pc[i])                         /* on list? */
        saved_PC = PC;                                  /* commit PC */
    }
actual_PC = PC;                                         /* save cur PC for RLS */
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
upd_ind ();
if (cpuio_inp != 0) {                                   /* flag IO in progress */
    const char *opstr = opc_lookup (cpuio_opc, cpuio_dev * 100, NULL);

    if (opstr != NULL)
        sim_printf ("\r\nIO in progress (%s %05d)", opstr, PAR);
    else sim_printf ("\r\nIO in progress (%02d %05d,%05d)", cpuio_opc, PAR, cpuio_dev * 100);
    }
return reason;
}

/* Utility routines */

/* Get 2 digit field

   Inputs:
        ad      =       address of high digit
   Outputs:
        val     =       field converted to binary
                        -1 if bad digit
*/

int32 get_2d (uint32 ad)
{
int32 d, d1;

d = M[ad] & DIGIT;                                      /* get 1st digit */
d1 = M[ADDR_A (ad, 1)] & DIGIT;                         /* get 2nd digit */
if (BAD_DIGIT (d) || BAD_DIGIT (d1))                    /* bad? error */
    return -1; 
return ((d * 10) + d1);                                 /* cvt to binary */
}

/* Get address routine

   Inputs:
        alast   =       address of low digit
        lnt     =       length
        indexok =       TRUE if indexing allowed
        &addr   =       pointer to address output
   Output:
        return  =       error status (in terms of P address)
        addr    =       address converted to binary

   Notes:
   - If indexing produces a negative result, the effective address is
     the 10's complement of the result
   - An address that exceeds memory produces a MAR check stop
*/

t_stat get_addr (uint32 alast, int32 lnt, t_bool indexok, uint32 *reta)
{
uint8 indir;
int32 cnt, idx, idxa, idxv, addr;

if (iae)                                                /* init indirect */
    indir = FLAG;
else indir = 0;

cnt = 0;                                                /* count depth */
do {
    indir = indir & M[alast];                           /* get indirect */
    if (cvt_addr (alast, lnt, FALSE, &addr))            /* cvt addr to bin */
        return STOP_INVPDG;                             /* bad? */
    idx = get_idx (ADDR_S (alast, 1));                  /* get index reg num */
    if (indexok && (idx > 0)) {                         /* indexable? */
        idxa = GET_IDXADDR (idx);                       /* get idx reg addr */
        if (cvt_addr (idxa, ADDR_LEN, TRUE, &idxv))     /* cvt idx reg */
            return STOP_INVPDG;
        addr = addr + idxv;                             /* add in index */
        if (addr < 0)                                   /* -? 10's comp */
            addr = addr + 100000;
        }
    if (addr >= (int32) MEMSIZE)                        /* invalid addr? */
        return STOP_INVPAD;
    alast = addr;                                       /* new address */
    lnt = ADDR_LEN;                                     /* std len */
    } while (indir && (cnt++ < ind_max));
if (cnt > ind_max)                                      /* indir too deep? */
    return STOP_INVPIA;
*reta = addr;                                           /* return address */
return SCPE_OK;
}

/* Convert address to binary

   Inputs:
        alast   =       address of low digit
        lnt     =       length
        signok  =       TRUE if signed
        val     =       address of output
   Outputs:
        status  =       0 if ok, != 0 if error
*/

t_stat cvt_addr (uint32 alast, int32 lnt, t_bool signok, int32 *val)
{
int32 sign = 0, addr = 0, t;

if (signok && (M[alast] & FLAG))                        /* signed? */
    sign = 1;
alast = alast - lnt;                                    /* find start */
do {
    PP (alast);                                         /* incr mem addr */
    t = M[alast] & DIGIT;                               /* get digit */
    if (BAD_DIGIT (t))                                  /* bad? error */
        return STOP_INVDIG;
    addr = (addr * 10) + t;                             /* cvt to bin */
    } while (--lnt > 0);
if (sign)                                               /* minus? */
    *val = -addr;
else *val = addr;
return SCPE_OK;
}

/* Get index register number

   Inputs:
        aidx    =       address of low digit
   Outputs:
        index   =       >0 if indexed
                        =0 if not indexed
                        <0 if indexing disabled
*/

t_stat get_idx (uint32 aidx)
{
int32 i, idx;

if (idxe == 0)                                          /* indexing off? */
    return -1;
for (i = idx = 0; i < 3; i++) {                         /* 3 flags worth */
    if (M[aidx] & FLAG)                                 /* test flag */
        idx = idx | (1 << i);
    MM (aidx);                                          /* next digit */
    }
return idx;
}

/* Update indicators routine */

void upd_ind (void)
{
ind[IN_HPEZ] = ind[IN_HP] | ind[IN_EZ];                 /* HPEZ = HP | EZ */
ind[IN_DERR] = ind[IN_DACH] | ind[IN_DWLR] | ind[IN_DCYO];
ind[IN_ANYCHK] = ind[IN_RDCHK] | ind[IN_WRCHK] |        /* ANYCHK = all chks */
    ind[IN_MBREVEN] | ind[IN_MBRODD] |
    ind[IN_PRCHK] | ind[IN_DACH];
ind[IN_IXN] = ind[IN_IXA] = ind[IN_IXB] = 0;            /* clr index indics */
if (!idxe)                                              /* off? */
    ind[IN_IXN] = 1;
else if (!idxb)                                         /* on, band A? */
    ind[IN_IXA] = 1;
else ind[IN_IXB] = 1;                                   /* no, band B */
return;
}

/* Transmit routines */

/* Transmit field from 's' to 'd' - ignore first 'skp' flags */

t_stat xmt_field (uint32 d, uint32 s, uint32 skp)
{
uint32 cnt = 0;
uint8 t;

do {
    t = M[d] = M[s] & (FLAG | DIGIT);                   /* copy src to dst */
    MM (d);                                             /* decr mem addrs */
    MM (s);
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while (((t & FLAG) == 0) || (cnt <= skp));        /* until flag */
return SCPE_OK;
}

/* Transmit record from 's' to 'd' - copy record mark if 'cpy' = TRUE */

t_stat xmt_record (uint32 d, uint32 s, t_bool cpy)
{
uint32 cnt = 0;

while ((M[s] & REC_MARK) != REC_MARK) {                 /* until rec mark */
    M[d] = M[s] & (FLAG | DIGIT);                       /* copy src to dst */
    PP (d);                                             /* incr mem addrs */
    PP (s);
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    }
if (cpy)                                                /* copy rec mark */
    M[d] = M[s] & (FLAG | DIGIT);
return SCPE_OK;
}

/* Transmit index from 's' to 'd' - fixed five character field */

t_stat xmt_index (uint32 d, uint32 s)
{
int32 i;

M[d] = M[s] & (FLAG | DIGIT);                           /* preserve sign */
MM (d); MM (s);                                         /* decr mem addrs */
for (i = 0; i < ADDR_LEN - 2; i++) {                    /* copy 3 digits */
    M[d] = M[s] & DIGIT;                                /* without flags */
    MM (d);                                             /* decr mem addrs */
    MM (s);
    }
M[d] = (M[s] & DIGIT) | FLAG;                           /* set flag on last */
return SCPE_OK;
}

/* Transmit dividend from 'd' to 's' - clear flag on first digit */

t_stat xmt_divd (uint32 d, uint32 s)
{
uint32 cnt = 0;

M[d] = M[s] & DIGIT;                                    /* first w/o flag */
do {
    MM (d);                                             /* decr mem addrs */
    MM (s);
    M[d] = M[s] & (FLAG | DIGIT);                       /* copy src to dst */
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while ((M[d] & FLAG) == 0);                       /* until src flag */
return SCPE_OK;
}

/* Transmit numeric strip from 's' to 'd' - s is odd */

t_stat xmt_tns (uint32 d, uint32 s)
{
uint32 cnt = 0;
uint8 t, z;

t = M[s] & DIGIT;                                       /* get units */
z = M[s - 1] & DIGIT;                                   /* get zone */
if ((z == 1) || (z == 5) || ((z == 2) && (t == 0)))     /* 1x, 5x, 20? */
    M[d] = t | FLAG;                                    /* set flag */
else M[d] = t;                                          /* else clear flag */
do {
    MM (d);                                             /* decr mem addrs */
    s = ADDR_S (s, 2);
    t = M[d] & FLAG;                                    /* save dst flag */
    M[d] = M[s] & (FLAG | DIGIT);                       /* copy src to dst */
    if (cnt >= MEMSIZE)                                 /* (stop runaway) */
        return STOP_FWRAP;
    cnt = cnt + 2;
    } while (t == 0);                                   /* until dst flag */
M[d] = M[d] | FLAG;                                     /* set flag at end */
return SCPE_OK;
}

/* Transmit numeric fill from 's' to 'd' - d is odd */

t_stat xmt_tnf (uint32 d, uint32 s)
{
uint32 cnt = 0;
uint8 t;

t = M[s];                                               /* get 1st digit */
M[d] = t & DIGIT;                                       /* store */
M[d - 1] = (t & FLAG)? 5: 7;                            /* set sign from flag */
do {
    MM (s);                                             /* decr mem addr */
    d = ADDR_S (d, 2);
    t = M[s];                                           /* get src digit */
    M[d] = t & DIGIT;                                   /* move to dst, no flag */
    M[d - 1] = 7;                                       /* set zone */
    if (cnt >= MEMSIZE)                                 /* (stop runaway) */
        return STOP_FWRAP;
    cnt = cnt + 2;
    } while ((t & FLAG) == 0);                          /* until src flag */
return SCPE_OK;
}

/* Add routine

   Inputs:
        d       =       destination field low (P)
        s       =       source field low (Q)
        sub     =       TRUE if subtracting
        sto     =       TRUE if storing
        skp     =       number of source field flags, beyond sign, to ignore
   Output:
        return  =       status
        sta     =       ADD_NOCRY: no carry out, no sign change
                        ADD_SIGNC: sign change
                        ADD_CARRY: carry out

   Reference Manual: "When the sum is zero, the sign of the P field
   is retained."

   Model 1 hack: If the Q field contains a record mark, it is treated
   as 0 (Dave Wise; from schematics).
*/

t_stat add_field (uint32 d, uint32 s, t_bool sub, uint32 skp, int32 *sta)
{
uint32 cry, src, dst, res, comp, dp, dsv;
uint32 src_f = 0, cnt = 0, dst_f = 0;

*sta = ADD_NOCRY;                                       /* assume no cry */
dsv = d;                                                /* save dst */
comp = ((M[d] ^ M[s]) & FLAG) ^ (sub? FLAG: 0);         /* set compl flag */
cry = 0;                                                /* clr carry */
ind[IN_HP] = ((M[d] & FLAG) == 0);                      /* set sign from res */
ind[IN_EZ] = 1;                                         /* assume zero */

dst = M[d] & DIGIT;                                     /* 1st digits */
src = M[s] & DIGIT;
if ((src == REC_MARK) &&                                /* Q record mark? */
    ((cpu_unit.flags & IF_RMOK) != 0))                  /* Model I & enabled? */
    src = 0;                                            /* treat as 0 */
if (BAD_DIGIT (dst) || BAD_DIGIT (src))                 /* bad digit? */
     return STOP_INVDIG;
if (comp)                                               /* complement? */
    src = 10 - src;
res = add_one_digit (dst, src, &cry);                   /* add */
M[d] = (M[d] & FLAG) | res;                             /* store */
MM (d); MM (s);                                         /* decr mem addrs */
do {
    dst = M[d] & DIGIT;                                 /* get dst digit */
    dst_f = M[d] & FLAG;                                /* get dst flag */
    if (src_f)                                          /* src done? src = 0 */
        src = 0;
    else {
        src = M[s] & DIGIT;                             /* get src digit */
        if (cnt >= skp)                                 /* get src flag */
            src_f = M[s] & FLAG;
        MM (s);                                         /* decr src addr */
        if ((src == REC_MARK) &&                        /* Q record mark? */
            ((cpu_unit.flags & IF_RMOK) != 0))          /* Model I & enabled? */
            src = 0;                                    /* treat as 0 */
        }
    if (BAD_DIGIT (dst) || BAD_DIGIT (src))             /* bad digit? */
        return STOP_INVDIG;
    if (comp)                                           /* complement? */
        src = 9 - src;
    res = add_one_digit (dst, src, &cry);               /* add */
    M[d] = dst_f | res;                                 /* store */
    MM (d);                                             /* decr dst addr */
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while (dst_f == 0);                               /* until dst done */
if (!src_f)                                             /* !src done? ovf */
    ind[IN_OVF] = 1;

/* Because recomplement is done (model 1) with table lookup, the first digit
   must be explicitly 10s complemented, and not 9s complemented with a carry
   in of 1. (Bob Armstrong) */

if (comp && !cry && !ind[IN_EZ]) {                      /* recomp needed? */
    ind[IN_HP] = ind[IN_HP] ^ 1;                        /* flip indicator */
    for (cry = 0, dp = dsv; dp != d; ) {                /* rescan */
        dst = M[dp] & DIGIT;                            /* get dst digit */
        dst = (dp == dsv)? (10 - dst): (9 - dst);       /* 10 or 9s comp */
        res = add_one_digit (0, dst, &cry);             /* "add" */
        M[dp] = (M[dp] & FLAG) | res;                   /* store */
        MM (dp);                                        /* decr dst addr */
        }
    M[dsv] = M[dsv] ^ FLAG;                             /* compl sign */
    *sta = ADD_SIGNC;                                   /* sign changed */
    return SCPE_OK;     
    }                                                   /* end if recomp */
if (ind[IN_EZ])                                         /* res = 0? clr HP */
    ind[IN_HP] = 0;
if (!comp && cry)                                       /* set status */
    *sta = ADD_CARRY;
return SCPE_OK;
}

/* Compare routine

   Inputs:
        d       =       destination field low (P)
        s       =       source field low (Q)
   Output:
        return  =       status

   In the unlike signs case, the compare is abandoned as soon as a non-zero
   digit is seen; zeroes go through the normal flows.

   See add for Model I hack in handling Q field record marks.
*/

t_stat cmp_field (uint32 d, uint32 s)
{
uint32 cry, src, dst, unlike, dsv;
uint32 src_f = 0, cnt = 0, dst_f = 0;

dsv = d;                                                /* save dst */
cry = 0;                                                /* clr carry */
unlike = (M[d] ^ M[s]) & FLAG;                          /* set unlike signs flag */
ind[IN_HP] = ((M[d] & FLAG) == 0);                      /* set sign from res */
ind[IN_EZ] = 1;                                         /* assume zero */

do {
    dst = M[d] & DIGIT;                                 /* get dst digit */
    if (d != dsv)                                       /* if not first digit, */
        dst_f = M[d] & FLAG;                            /* get dst flag */
    if (src_f)                                          /* src done? src = 0 */
        src = 0;
    else {
        src = M[s] & DIGIT;                             /* get src digit */
        if (d != dsv)                                   /* if not first digit, */
            src_f = M[s] & FLAG;                        /* get src flag */
        MM (s);                                         /* decr src addr */
        }
    if (unlike && ((dst | src) != 0)) {                 /* unlike signs, digit? */
        ind[IN_EZ] = 0;                                 /* not equal */
        return SCPE_OK;
        }
    if ((src == REC_MARK) &&                            /* Q record mark? */
        ((cpu_unit.flags & IF_RMOK) != 0))              /* Model I & enabled? */
        src = 0;                                        /* treat as 0 */
    if (BAD_DIGIT (dst) || BAD_DIGIT (src))             /* bad digit? */
        return STOP_INVDIG;
    src = (d != dsv)? 9 - src: 10 - src;                /* complement */
    add_one_digit (dst, src, &cry);                     /* throw away result */
    MM (d);                                             /* decr dst addr */
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while (dst_f == 0);                               /* until dst done */
if (!src_f)                                             /* !src done? ovf */
    ind[IN_OVF] = 1;

/* At this point, we have three possible cases:
   - Fields are equal, signs irrelevant: ind[IN_EZ] is still set
   - Fields are unequal, signs are the same, carry out:
        |p| > |q|, ind[IN_HP] is correct
   - Fields are unequal, signs are the same, no carry out:
        |p| < |q, ind[IN_HP] must be inverted
*/

if (!cry && !ind[IN_EZ]) {                              /* recomp needed? */
    ind[IN_HP] = ind[IN_HP] ^ 1;                        /* flip indicator */
    return SCPE_OK;     
    }                                                   /* end if recomp */
if (ind[IN_EZ])                                         /* res = 0? clr HP */
    ind[IN_HP] = 0;
return SCPE_OK;
}

/* Add one digit via table (Model 1) or "hardware" (Model 2) */

uint32 add_one_digit (uint32 dst, uint32 src, uint32 *cry)
{
uint32 res;

if (*cry)                                               /* cry in? incr src */
    src = src + 1;
if (src >= 10) {                                        /* src > 10? */
    src = src - 10;                                     /* src -= 10 */
    *cry = 1;                                           /* carry out */
    }
else *cry = 0;                                          /* else no carry */
if (cpu_unit.flags & IF_MII)                            /* Model 2? */
    res = sum_table[dst + src];                         /* "hardware" */
else res = M[ADD_TABLE + (dst * 10) + src];             /* table lookup */
if (res & FLAG)                                         /* carry out? */
    *cry = 1;
if (res & DIGIT)                                        /* nz? clr ind */
    ind[IN_EZ] = 0;
return res & DIGIT;
}

/* Multiply routine 

   Inputs:
        mpc     =       multiplicand address
        mpy     =       multiplier address
   Outputs:
        return  =       status

   Reference manual: "A zero product may have a negative or positive sign,
   depending on the signs of the fields at the P and Q addresses."
*/

t_stat mul_field (uint32 mpc, uint32 mpy)
{
int32 i;
uint32 pro;                                             /* prod pointer */
uint32 mpyd, mpyf;                                      /* mpy digit, flag */
uint32 cnt = 0;                                         /* counter */
uint8 sign;                                             /* final sign */
t_stat r;

PR1 = 1;                                                /* step on PR1 */
for (i = 0; i < PROD_AREA_LEN; i++)                     /* clr prod area */
    M[PROD_AREA + i] = 0;
sign = (M[mpc] & FLAG) ^ (M[mpy] & FLAG);               /* get final sign */
ind[IN_HP] = (sign == 0);                               /* set indicators */
ind[IN_EZ] = 1;
pro = PROD_AREA + PROD_AREA_LEN - 1;                    /* product ptr */

/* Loop on multiplier (mpy) and product (pro) digits */

do {
    mpyd = M[mpy] & DIGIT;                              /* multiplier digit */
    mpyf = (M[mpy] & FLAG) && (cnt != 0);               /* last digit flag */
    if (BAD_DIGIT (mpyd))                               /* bad? */
        return STOP_INVDIG;
    r = mul_one_digit (mpyd, mpc, pro, mpyf);           /* prod += mpc*mpy_dig */
    if (r != SCPE_OK)                                   /* error? */
        return r;
    MM (mpy);                                           /* decr mpyr, prod addrs */
    MM (pro);
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while ((mpyf == 0) || (cnt <= 1));                /* until mpyr flag */

if (ind[IN_EZ])                                         /* res = 0? clr HP */
    ind[IN_HP] = 0;
M[PROD_AREA + PROD_AREA_LEN - 1] |= sign;               /* set final sign */
return SCPE_OK;
}

/* Multiply step

   Inputs:
        mpyd    =       multiplier digit (tested valid)
        mpcp    =       multiplicand low address
        prop    =       product low address
        last    =       last iteration flag (set flag on high product)
   Outputs:
        prod    +=      multiplicand * multiplier_digit
        return  =       status

   The multiply table address is constructed as follows:
   - double the multiplier digit
   - use the 10's digit of the doubled result, + 1, as the 100's digit
     of the table address
   - use the multiplicand digit as the 10's digit of the table address
   - use the unit digit of the doubled result as the unit digit of the
     table address
   EZ indicator is cleared if a non-zero digit is ever generated
*/

t_stat mul_one_digit (uint32 mpyd, uint32 mpcp, uint32 prop, uint32 last)
{
uint32 mpta, mptb;                                      /* mult table */
uint32 mptd;                                            /* mult table digit */
uint32 mpcd, mpcf;                                      /* mpc digit, flag */
uint32 prwp;                                            /* prod working ptr */
uint32 prod;                                            /* product digit */
uint32 cry;                                             /* carry */
uint32 mpcc, cryc;                                      /* counters */

mptb = MUL_TABLE + ((mpyd <= 4)? (mpyd * 2):            /* set mpy table 100's, */
    (((mpyd - 5) * 2) + 100));                          /* 1's digits */

/* Inner loop on multiplicand (mpcp) and product (prop) digits */

mpcc = 0;                                               /* multiplicand ctr */
do {
    prwp = prop;                                        /* product working ptr */
    mpcd = M[mpcp] & DIGIT;                             /* multiplicand digit */
    mpcf = M[mpcp] & FLAG;                              /* multiplicand flag */
    if (BAD_DIGIT (mpcd))                               /* bad? */
        return STOP_INVDIG;
    mpta = mptb + (mpcd * 10);                          /* mpy table 10's digit */
    cry = 0;                                            /* init carry */
    mptd = M[mpta] & DIGIT;                             /* mpy table digit */
    if (BAD_DIGIT (mptd))                               /* bad? */
        return STOP_INVDIG;
    prod = M[prwp] & DIGIT;                             /* product digit */
    if (BAD_DIGIT (prod))                               /* bad? */
        return STOP_INVDIG;
    M[prwp] = add_one_digit (prod, mptd, &cry);         /* add mpy tbl to prod */
    MM (prwp);                                          /* decr working ptr */
    mptd = M[mpta + 1] & DIGIT;                         /* mpy table digit */
    if (BAD_DIGIT (mptd))                               /* bad? */
        return STOP_INVDIG;
    prod = M[prwp] & DIGIT;                             /* product digit */
    if (BAD_DIGIT (prod))                               /* bad? */
        return STOP_INVDIG;
    M[prwp] = add_one_digit (prod, mptd, &cry);         /* add mpy tbl to prod */
    cryc = 0;                                           /* (stop runaway) */
    while (cry) {                                       /* propagate carry */
        MM (prwp);                                      /* decr working ptr */
        prod = M[prwp] & DIGIT;                         /* product digit */
        if (BAD_DIGIT (prod))                           /* bad? */
            return STOP_INVDIG;
        M[prwp] = add_one_digit (prod, 0, &cry);        /* add cry */
        if (cryc++ > MEMSIZE)
            return STOP_FWRAP;
        }
    MM (mpcp);                                          /* decr mpc, prod ptrs */
    MM (prop);
    if (mpcc++ > MEMSIZE)
        return STOP_FWRAP;
    } while ((mpcf == 0) || (mpcc <= 1));               /* until mpcf flag */
if (last)                                               /* flag high product */
    M[prop] = M[prop] | FLAG;
return SCPE_OK;
}

/* Divide routine - comments from Geoff Kuenning's 1620 simulator

   The destination of the divide is given by:

        100 - <# digits in quotient>

   Which is more easily calculated as:

        100 - <# digits in divisor> - <# digits in dividend>

   The quotient goes into 99 minus the divisor length.  The
   remainder goes into 99.  The load dividend instruction (above)
   should have specified a P address of 99 minus the size of the
   divisor.

   Note that this all implies that "dest" points to the *leftmost*
   digit of the dividend.

   After the division, the assumed decimal point will be as many
   positions to the left as there are digits in the divisor.  In
   other words, a 4-digit divisor will produce 4 (assumed) decimal
   places.

   There are other ways to do these things.  In particular, the
   load-dividend instruction doesn't have to specify the above
   formula; if it's done differently, then you don't have to get
   decimal places.  This is not well-explained in the books I have.

   How to divide on a 1620:

   The dividend is the field at 99:

            90 = _1234567890

   The divisor is somewhere else in memory:

            _03

   The divide operation specifies the left-most digit of the
   dividend as the place to begin trial subtractions:

            DM  90,3

   The loop works as follows:

        1.  Call the left-most digit of the dividend "current_dividend".
            Call the location current_dividend - <divisor_length>
            "quotient_digit".
        2.  Clear the flag at current_dividend, and set one at
            quotient_digit.

                88 = _001234567890, q_d = 88, c_d = 90
            [Not actually done; divisor length controls subtract.]
        3.  Subtract the divisor from the field at current-dividend,
            using normal 1620 rules, except that signs are ignored.
            Continue these subtractions until either 10 subtractions
            have been done, or you get a negative result:

                88 = _00_2234567890, q_d = 88, c_d = 90
        4.  If 10 subtractions have been done, set the overflow
            indicator and abort.  Otherwise, add the divisor back to
            correct for the oversubtraction:

                88 = _001234567890, q_d = 88, c_d = 90
        5.  Store the (net) number of subtractions in quotient_digit:

                88 = _001234567890, q_d = 88, c_d = 90
        6.  If this is not the first pass, clear the flag at
            quotient_digit.  Increment quotient_digit and
            current_dividend, and set a flag at the new
            quotient_digit:

                88 = _0_01234567890, q_d = 89, c_d = 91
            [If first pass, set a flag at quotient digit.]
        7.  If current_dividend is not 100, repeat steps 3 through 7.
        8.  Set flags at 99 and quotient_digit - 1 according to the
            rules of algebra:  the quotient's sign is the exclusive-or
            of the signs of the divisor and dividend, and the
            remainder has the sign of the dividend:

                 10 /  3 =  3 remainder  1
                 10 / -3 = -3 remainder  1
                -10 /  3 = -3 remainder -1
                -10 / -3 =  3 remainder -1
  
            This preserves the relationship dd = q * dv + r.

   Our example continues as follows for steps 3 through 7:
  
            3.  88 = _0_00_334567890, q_d = 89, c_d = 91
            4.  88 = _0_00034567890
            5.  88 = _0_40034567890
            6.  88 = _04_0034567890, q_d = 90, c_d = 92
            3.  88 = _04_00_34567890
            4.  88 = _04_0004567890
            5.  88 = _04_1004567890
            6.  88 = _041_004567890, q_d = 91, c_d = 93
            3.  88 = _041_00_2567890
            4.  88 = _041_001567890
            5.  88 = _041_101567890
            6.  88 = _0411_01567890, q_d = 92, c_d = 94
            3.  88 = _0411_00_367890
            4.  88 = _0411_00067890
            5.  88 = _0411_50067890
            6.  88 = _04115_0067890, q_d = 93, c_d = 95
            3.  88 = _04115_00_37890
            4.  88 = _04115_0007890
            5.  88 = _04115_2007890
            6.  88 = _041152_007890, q_d = 94, c_d = 96
            3.  88 = _041152_00_2890
            4.  88 = _041152_001890
            5.  88 = _041152_201890
            6.  88 = _0411522_01890, q_d = 95, c_d = 97
            3.  88 = _0411522_00_390
            4.  88 = _0411522_00090
            5.  88 = _0411522_60090
            6.  88 = _04115226_0090, q_d = 96, c_d = 98
            3.  88 = _04115226_00_30
            4.  88 = _04115226_0000
            5.  88 = _04115226_3000
            6.  88 = _041152263_000, q_d = 97, c_d = 99
            3.  88 = _041152263_00_3
            4.  88 = _041152263_000
            5.  88 = _041152263_000
            6.  88 = _0411522630_00, q_d = 98, c_d = 100

   In the actual code below, we elide several of these steps in
   various ways for convenience and efficiency.

   Note that the EZ indicator is NOT valid for divide, because it
   is cleared by any non-zero result in an intermediate add.  The
   code maintains its own EZ indicator for the quotient.
*/

t_stat div_field (uint32 dvd, uint32 dvr, int32 *ez)
{
uint32 quop, quod, quos;                                /* quo ptr, dig, sign */
uint32 dvds;                                            /* dvd sign */
t_bool first = TRUE;                                    /* first pass */
t_stat r;

dvds = (M[PROD_AREA + PROD_AREA_LEN - 1]) & FLAG;       /* dividend sign */
quos = dvds ^ (M[dvr] & FLAG);                          /* quotient sign */
ind[IN_HP] = (quos == 0);                               /* set indicators */
*ez = 1;

/* Loop on current dividend, high order digit at dvd */

do {
    r = div_one_digit (dvd, dvr, 10, &quod, &quop);     /* dev quo digit */
    if (r != SCPE_OK)                                   /* error? */
        return r;

/* Store quotient digit and advance current dividend pointer */

    if (first) {                                        /* first pass? */
        if (quod >= 10) {                               /* overflow? */
            ind[IN_OVF] = 1;                            /* set indicator */
            return STOP_OVERFL;                         /* stop */
            }
        M[quop] = FLAG | quod;                          /* set flag on quo */
        first = FALSE;
        }
    else M[quop] = quod;                                /* store quo digit */
    if (quod)                                           /* if nz, clr ind */
        *ez = 0;
    PP (dvd);                                           /* incr dvd ptr */
    } while (dvd != (PROD_AREA + PROD_AREA_LEN));       /* until end prod */

/* Division done.  Set signs of quo, rem, set flag on high order remainder */

if (*ez)                                                /* res = 0? clr HP */
    ind[IN_HP] = 0;
M[PROD_AREA + PROD_AREA_LEN - 1] |= dvds;               /* remainder sign */
M[quop] = M[quop] | quos;                               /* quotient sign */
PP (quop);                                              /* high remainder */
M[quop] = M[quop] | FLAG;                               /* set flag */
return SCPE_OK;
}

/* Divide step

   Inputs:
        dvd     =       current dividend address (high digit)
        dvr     =       divisor address (low digit)
        max     =       max number of iterations before overflow
        &quod   =       address to store quotient digit
        &quop   =       address to store quotient pointer (can be NULL)
   Outputs:
        return  =       status

   Divide step calculates a quotient digit by repeatedly subtracting the
   divisor from the current dividend.  The divisor's length controls the
   subtraction; dividend flags are ignored.
*/

t_stat div_one_digit (uint32 dvd, uint32 dvr, uint32 max,
                 uint32 *quod, uint32 *quop)
{
uint32 dvrp, dvrd, dvrf;                                /* dvr ptr, dig, flag */
uint32 dvdp, dvdd;                                      /* dvd ptr, dig */
uint32 qd, cry;                                         /* quo dig, carry */
uint32 cnt;

for (qd = 0; qd < max; qd++) {                          /* devel quo dig */
    dvrp = dvr;                                         /* divisor ptr */
    dvdp = dvd;                                         /* dividend ptr */
    cnt = 0;
    cry = 1;                                            /* carry in = 1 */
    do {                                                /* sub dvr fm dvd */
        dvdd = M[dvdp] & DIGIT;                         /* dividend digit */
        if (BAD_DIGIT (dvdd))                           /* bad? */
            return STOP_INVDIG;
        dvrd = M[dvrp] & DIGIT;                         /* divisor digit */
        dvrf = M[dvrp] & FLAG;                          /* divisor flag */
        if (BAD_DIGIT (dvrd))                           /* bad? */
            return STOP_INVDIG;
        M[dvdp] = add_one_digit (dvdd, 9 - dvrd, &cry); /* sub */
        MM (dvdp);                                      /* decr ptrs */
        MM (dvrp);
        if (cnt++ >= MEMSIZE)                           /* (stop runaway) */
            return STOP_FWRAP;
        } while ((dvrf == 0) || (cnt <= 1));            /* until dvr flag */
    if (!cry) {                                         /* !cry = borrow */
        dvdd = M[dvdp] & DIGIT;                         /* borrow digit */
        if (BAD_DIGIT (dvdd))                           /* bad? */
            return STOP_INVDIG;
        M[dvdp] = add_one_digit (dvdd, 9, &cry);        /* sub */
        }
    if (!cry)                                           /* !cry = negative */
        break;
    }

/* Add back the divisor to correct for the negative result */

dvrp = dvr;                                             /* divisor ptr */
dvdp = dvd;                                             /* dividend ptr */
cnt = 0;
cry = 0;                                                /* carry in = 0 */
do {
    dvdd = M[dvdp] & DIGIT;                             /* dividend digit */
    dvrd = M[dvrp] & DIGIT;                             /* divisor digit */
    dvrf = M[dvrp] & FLAG;                              /* divisor flag */
    M[dvdp] = add_one_digit (dvdd, dvrd, &cry);         /* add */
    MM (dvdp);                                          /* decr ptrs */
    MM (dvrp);
    cnt++;
    } while ((dvrf == 0) || (cnt <= 1));                /* until dvr flag */
if (cry) {                                              /* carry out? */
    dvdd = M[dvdp] & DIGIT;                             /* borrow digit */
    M[dvdp] = add_one_digit (dvdd, 0, &cry);            /* add */
    }
if (quop != NULL)                                       /* set quo addr */
    *quop = dvdp;
*quod = qd;                                             /* set quo digit */
return SCPE_OK;
}

/* Logical operation routines (and, or, xor, complement)

   Inputs:
        d       =       destination address
        s       =       source address
   Output:
        return  =       status

   Destination flags are preserved; EZ reflects the result.
   COM does not obey normal field length restrictions.
*/

t_stat or_field (uint32 d, uint32 s)
{
uint32 cnt = 0;
int32 t;

ind[IN_EZ] = 1;                                         /* assume result zero */
do {
    t = M[s];                                           /* get src */
    M[d] = (M[d] & FLAG) | ((M[d] | t) & 07);           /* OR src to dst */
    if (M[d] & DIGIT)                                   /* nz dig? clr ind */
        ind[IN_EZ] = 0;
    MM (d);                                             /* decr pointers */
    MM (s);
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while (((t & FLAG) == 0) || (cnt <= 1));          /* until src flag */
return SCPE_OK;
}

t_stat and_field (uint32 d, uint32 s)
{
uint32 cnt = 0;
int32 t;

ind[IN_EZ] = 1;                                         /* assume result zero */
do {
    t = M[s];                                           /* get src */
    M[d] = (M[d] & FLAG) | ((M[d] & t) & 07);           /* AND src to dst */
    if (M[d] & DIGIT)                                   /* nz dig? clr ind */
        ind[IN_EZ] = 0;
    MM (d);                                             /* decr pointers */
    MM (s);
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while (((t & FLAG) == 0) || (cnt <= 1));          /* until src flag */
return SCPE_OK;
}

t_stat xor_field (uint32 d, uint32 s)
{
uint32 cnt = 0;
int32 t;

ind[IN_EZ] = 1;                                         /* assume result zero */
do {
    t = M[s];                                           /* get src */
    M[d] = (M[d] & FLAG) | ((M[d] ^ t) & 07);           /* XOR src to dst */
    if (M[d] & DIGIT)                                   /* nz dig? clr ind */
        ind[IN_EZ] = 0;
    MM (d);                                             /* decr pointers */
    MM (s);
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while (((t & FLAG) == 0) || (cnt <= 1));          /* until src flag */
return SCPE_OK;
}

t_stat com_field (uint32 d, uint32 s)
{
uint32 cnt = 0;
int32 t;

ind[IN_EZ] = 1;                                         /* assume result zero */
do {
    t = M[s];                                           /* get src */
    M[d] = (t & FLAG) | ((t ^ 07) & 07);                /* comp src to dst */
    if (M[d] & DIGIT)                                   /* nz dig? clr ind */
        ind[IN_EZ] = 0;
    MM (d);                                             /* decr pointers */
    MM (s);
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while ((t & FLAG) == 0);                          /* until src flag */
return SCPE_OK;
}

/* Octal to decimal

   Inputs:
        tbl     =       conversion table address (low digit)
        s       =       source address
   Outputs:
        product area =  converted source
        result  =       status

   OTD is a cousin of multiply.  The octal digits in the source are
   multiplied by successive values in the conversion table, and the
   results are accumulated in the product area.  Although the manual
   does not say, this code assumes that EZ and HP are affected.
 */

t_stat oct_to_dec (uint32 tbl, uint32 s)
{
uint32 cnt = 0, tblc;
uint32 i, sd, sf, tf, sign;
t_stat r;

for (i = 0; i < PROD_AREA_LEN; i++)                     /* clr prod area */
    M[PROD_AREA + i] = 0;
sign = M[s] & FLAG;                                     /* save sign */
ind[IN_EZ] = 1;                                         /* set indicators */
ind[IN_HP] = (sign == 0);
do {
    sd = M[s] & DIGIT;                                  /* src digit */
    sf = M[s] & FLAG;                                   /* src flag */
    r = mul_one_digit (sd, tbl, PROD_AREA + PROD_AREA_LEN - 1, sf);
    if (r != SCPE_OK)                                   /* err? */
        return r;
    MM (s);                                             /* decr src addr */
    MM (tbl);                                           /* skip 1st tbl dig */
    tblc = 0;                                           /* count */
    do {
        tf = M[tbl] & FLAG;                             /* get next */
        MM (tbl);                                       /* decr ptr */
        if (tblc++ > MEMSIZE)
            return STOP_FWRAP;
        } while (tf == 0);                              /* until flag */
    if (cnt++ >= MEMSIZE)                               /* (stop runaway) */
        return STOP_FWRAP;
    } while (sf == 0);
if (ind[IN_EZ])                                         /* res = 0? clr HP */
    ind[IN_HP] = 0;
M[PROD_AREA + PROD_AREA_LEN - 1] |= sign;               /* set sign */
return SCPE_OK;
}

/* Decimal to octal 

   Inputs:
        d       =       destination address
        tbl     =       conversion table address (low digit of highest power)
        &ez     =       address of soft EZ indicator
        product area =  field to convert
   Outputs:
        return  =       status

   DTO is a cousin to divide.  The number in the product area is repeatedly
   divided by successive values in the conversion table, and the quotient
   digits are stored in the destination.  Although the manual does not say,
   this code assumes that EZ and HP are affected.
 */

t_stat dec_to_oct (uint32 d, uint32 tbl, int32 *ez)
{
uint32 sign, octd, t;
t_bool first = TRUE;
uint32 ctr = 0;
t_stat r;

sign = M[PROD_AREA + PROD_AREA_LEN - 1] & FLAG;         /* input sign */
*ez = 1;                                                /* set indicators */
ind[IN_HP] = (sign == 0);
for ( ;; ) {
    r = div_one_digit (PROD_AREA + PROD_AREA_LEN - 1,   /* divide */
        tbl, 8, &octd, NULL);
    if (r != SCPE_OK)                                   /* error? */
        return r;
    if (first) {                                        /* first pass? */
        if (octd >= 8) {                                /* overflow? */
            ind[IN_OVF] = 1;                            /* set indicator */
            return SCPE_OK;                             /* stop */
            }
        M[d] = FLAG | octd;                             /* set flag on quo */
        first = FALSE;
        }
    else M[d] = octd;                                   /* store quo digit */
    if (octd)                                           /* if nz, clr ind */
        *ez = 0;
    PP (tbl);                                           /* incr tbl addr */
    if ((M[tbl] & REC_MARK) == REC_MARK)                /* record mark? */
        break;
    PP (tbl);                                           /* skip flag */
    if ((M[tbl] & REC_MARK) == REC_MARK)                /* record mark? */
        break;
    do {                                                /* look for F, rec mk */
        PP (tbl);
        t = M[tbl];
        } while (((t & FLAG) == 0) && ((t & REC_MARK) != REC_MARK));
    MM (tbl);                                           /* step back one */
    PP (d);                                             /* incr quo addr */
    if (ctr++ > MEMSIZE)                                /* (stop runaway) */
        return STOP_FWRAP;
    }
if (*ez)                                                /* res = 0? clr HP */
    ind[IN_HP] = 0;
M[d] = M[d] | sign;                                     /* set result sign */   
return SCPE_OK;
}

/* Set and clear IO in progress */

t_stat cpuio_set_inp (uint32 op, uint32 dev, UNIT *uptr)
{
cpuio_inp = 1;
cpuio_opc = op;
cpuio_dev = dev;
cpuio_cnt = 0;
if (uptr != NULL)
    DEFIO_ACTIVATE_ABS (uptr);
return SCPE_OK;
}

t_stat cpuio_clr_inp (UNIT *uptr)
{
cpuio_inp = 0;
cpuio_opc = 0;
cpuio_dev = 0;
cpuio_cnt = 0;
if (uptr != NULL)
    sim_cancel (uptr);
return SCPE_OK;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int32 i;
static t_bool one_time = TRUE;

PR1 = IR2 = 1;                                          /* invalidate PR1,IR2 */
ind[0] = 0;
for (i = IN_SW4 + 1; i < NUM_IND; i++)                  /* init indicators */
    ind[i] = 0;
if (cpuio_inp != 0)                                     /* IO in progress? */
    cpu_set_release (NULL, 0, NULL, NULL);              /* clear IO */
if (cpu_unit.flags & IF_IA)                             /* indirect enabled? */
    iae = 1;
else iae = 0;
idxe = idxb = 0;                                        /* indexing off */
pcq_r = find_reg ("PCQ", NULL, dptr);                   /* init old PC queue */
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');            /* init breakpoints */
upd_ind ();                                             /* update indicators */
if (one_time) {                                         /* set default tables */
    cpu_set_table (&cpu_unit, 1, NULL, NULL);
    actual_PC = saved_PC = 0;                           /* sync PCs */
    }
one_time = FALSE;
return SCPE_OK;
}

/* Release routine */

t_stat cpu_set_release (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i;
DEVICE *dptr;

if (cpuio_inp != 0) {                                   /* IO in progress? */
    cpuio_inp = 0;
    cpuio_opc = 0;
    cpuio_dev = 0;
    cpuio_cnt = 0;
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        if (((dptr->flags & DEV_DEFIO) != 0) && (dptr->reset != NULL))
            dptr->reset (dptr);
        }
    sim_printf ("IO operation canceled\n");
    }
else if (actual_PC == ADDR_A (saved_PC, INST_LEN)) {    /* one instr ahead? */
    saved_PC = actual_PC;
    sim_printf ("New PC = %05d\n", saved_PC);
    }
else sim_printf ("PC unchanged\n");
return SCPE_OK;
}

/* Character rate */

t_stat cpu_set_cps (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i, j, cps;
DEVICE *dptr;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
cps = get_uint (cptr, 10, 1000000, &r);
if (r != SCPE_OK)
    return SCPE_ARG;

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    if ((dptr->flags & DEV_DEFIO) != 0) {
        for (j = 0; j < dptr->numunits; j++)
            dptr->units[j].DEFIO_CPS = cps;
        }
    }
return SCPE_OK;
}

/* Show CPS */

t_stat cpu_show_cps (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 i;
DEVICE *dptr;

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    if ((dptr->flags & DEV_DEFIO) != 0)
        fprintf (st, "%s CPS: %d\n", dptr->name, dptr->units[0].DEFIO_CPS);
    }
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = M[addr] & (FLAG | DIGIT);
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
M[addr] = val & (FLAG | DIGIT);
return SCPE_OK;
}

/* Memory size change */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val % 1000) != 0))
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

/* Model change */

t_stat cpu_set_model (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (val)
    cpu_unit.flags = (cpu_unit.flags & (UNIT_SCP | UNIT_BCD | MII_OPT)) |
        IF_DIV | IF_IA | IF_EDT;
else cpu_unit.flags = cpu_unit.flags & (UNIT_SCP | UNIT_BCD | MI_OPT);
return SCPE_OK;
}

/* Set/clear Model 1 option */

t_stat cpu_set_opt1 (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cpu_unit.flags & IF_MII) {
    if ((val & IF_RMOK) != 0)
        sim_printf ("Feature is not available on 1620 Model 2\n");
    else sim_printf ("Feature is standard on 1620 Model 2\n");
    return SCPE_NOFNC;
    }
return SCPE_OK;
}

/* Set/clear Model 2 option */

t_stat cpu_set_opt2 (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (!(cpu_unit.flags & IF_MII)) {
    sim_printf ("Feature is not available on 1620 Model 1\n");
    return SCPE_NOFNC;
    }
return SCPE_OK;
}

/* Front panel save */

t_stat cpu_set_save (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (saved_PC & 1)
    return SCPE_NOFNC;
PR1 = saved_PC;
return SCPE_OK;
}

/* Set standard add/multiply tables */

t_stat cpu_set_table (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i;

for (i = 0; i < MUL_TABLE_LEN; i++)                     /* set mul table */
    M[MUL_TABLE + i] = std_mul_table[i];
if (((cpu_unit.flags & IF_MII) == 0) || val) {          /* set add table */
    for (i = 0; i < ADD_TABLE_LEN; i++)
        M[ADD_TABLE + i] = std_add_table[i];
    }
return SCPE_OK;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].vld = 0;
    hst_p = 0;
    return SCPE_OK;
    }
lnt = (int32) get_uint (cptr, 10, HIST_MAX, &r);
if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
    return SCPE_ARG;
hst_p = 0;
if (hst_lnt) {
    free (hst);
    hst_lnt = 0;
    hst = NULL;
    }
if (lnt) {
    hst = (InstHistory *) calloc (lnt, sizeof (InstHistory));
    if (hst == NULL)
        return SCPE_MEM;
    hst_lnt = lnt;
    }
return SCPE_OK;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 i, k, di, lnt;
CONST char *cptr = (CONST char *) desc;
t_value sim_eval[INST_LEN];
t_stat r;
InstHistory *h;
extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw);

if (hst_lnt == 0)                                       /* enabled? */
    return SCPE_NOFNC;
if (cptr) {
    lnt = (int32) get_uint (cptr, 10, hst_lnt, &r);
    if ((r != SCPE_OK) || (lnt == 0))
        return SCPE_ARG;
    }
else lnt = hst_lnt;
di = hst_p - lnt;                                       /* work forward */
if (di < 0)
    di = di + hst_lnt;
fprintf (st, "PC     IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->vld) {                                       /* instruction? */
        fprintf (st, "%05d  ", h->pc);
        for (i = 0; i < INST_LEN; i++)
            sim_eval[i] = h->inst[i];
        if ((fprint_sym (st, h->pc, sim_eval, &cpu_unit, SWMASK ('M'))) > 0) {
            fprintf (st, "(undefined)");
            for (i = 0; i < INST_LEN; i++)
                fprintf (st, "%02X", h->inst[i]);
            }
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}
