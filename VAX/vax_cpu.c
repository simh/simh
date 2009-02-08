/* vax_cpu.c: VAX CPU

   Copyright (c) 1998-2008, Robert M Supnik

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

   cpu          VAX central processor

   21-May-08    RMS     Removed inline support
   28-May-08    RMS     Inlined instruction prefetch, physical memory routines
   13-Aug-07    RMS     Fixed bug in read access g-format indexed specifiers
   28-Apr-07    RMS     Removed clock initialization
   29-Oct-06    RMS     Added idle support
   22-May-06    RMS     Fixed format error in CPU history (found by Peter Schorn)
   10-May-06    RMS     Added -kesu switches for virtual addressing modes
                        Fixed bugs in examine virtual
                        Rewrote history function for greater usability
                        Fixed bug in reported VA on faulting cross-page write
   02-May-06    RMS     Fixed fault cleanup to clear PSL<tp>
                        Fixed ADAWI r-mode to preserve dst<31:16>
                        Fixed ACBD/G to test correct operand
                        Fixed access checking on modify-class specifiers
                        Fixed branch displacements in history buffer
                        (all reported by Tim Stark)
   17-Nov-05    RMS     Fixed CVTfi with integer overflow to trap if PSW<iv> set
   13-Nov-05    RMS     Fixed breakpoint test with 64b addresses
   25-Oct-05    RMS     Removed cpu_extmem
   22-Sep-05    RMS     Fixed declarations (from Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   13-Jan-05    RMS     Fixed initial state of cpu_extmem
   06-Nov-04    RMS     Added =n to SHOW HISTORY
   30-Sep-04    RMS     Added octaword specifier decodes and instructions
                        Moved model-specific routines to system module
   02-Sep-04    RMS     Fixed bug in EMODD/G, second word of quad dst not probed
   28-Jun-04    RMS     Fixed bug in DIVBx, DIVWx (reported by Peter Trimmel)
   18-Apr-04    RMS     Added octaword macros
   25-Jan-04    RMS     Removed local debug logging support
                RMS,MP  Added extended physical memory support
   31-Dec-03    RMS     Fixed bug in set_cpu_hist
   21-Dec-03    RMS     Added autoconfiguration controls
   29-Oct-03    RMS     Fixed WriteB declaration (found by Mark Pizzolato)
   23-Sep-03    RMS     Revised instruction history for dynamic sizing
   17-May-03    RMS     Fixed operand order in EMODx
   23-Apr-03    RMS     Revised for 32b/64b t_addr
   05-Jan-02    RMS     Added memory size restore support
   25-Dec-02    RMS     Added instruction history (from Mark Pizzolato)
   29-Sep-02    RMS     Revised to build dib_tab dynamically
   14-Jul-02    RMS     Added halt to console, infinite loop detection
                        (from Mark Pizzolato)
   02-May-02    RMS     Fixed bug in indexed autoincrement register logging
   30-Apr-02    RMS     Added TODR powerup routine
   18-Apr-02    RMS     Cleanup ambiguous signed left shifts
   15-Apr-02    RMS     Fixed bug in CASEL condition codes

   The register state for the VAX is:

        R[0:15]         general registers
        PSL<31:0>       processor status longword
         TP<30>         trace pending
         FPD<27>        first part done
         IS<26>         interrupt stack
         CM<25:24>      current mode
         PM<23:22>      previous mode
         IPL<20:16>     interrupt priority level
         PSW<15:0>      non-privileged processor status word
          DV<7>         decimal overflow trap enable
          FU<6>         floating underflow fault enable
          IV<5>         integer overflow trap enable
          T<4>          trace trap enable
          CC<3:0>       condition codes
        SCBB            system control block base
        PCBB            process control block base
        SBR             system page table base
        SLR             system page table length
        P0BR            process region 0 page table base
        P0LR            process region 0 page table length
        P1BR            process region 1 page table base
        P1LR            process region 1 page table length
        SIRR/SISR       software interrupt request/summary register
        ASTLVL          AST level register

   The VAX has a variable length instruction format with up to six operands:

        opcode byte
        operand 1 specifier
         :
        operand n specifier

   Each operand specifier is a byte consisting of an addressing mode, a
   register, and possibly 1-8 bytes of extension:

        number  name        extension   mnemonic        operation

        0-3     short literal   -       #n      op <- specifier
        4       index           -       [Rn]    index by Rn
        5       register        -       Rn      op <- Rn
        6       register def    -       (Rn)    op <- M[Rn]
        7       autodecrement   -       -(Rn)   Rn <- Rn - length
                                                op <- M[Rn]
        8       autoincrement   -       (Rn)+   op <- M[Rn]
                                                Rn <- Rn + length
        9       auto deferred   -       @(Rn)+  op <- M[M[Rn]]
                                                Rn <- Rn + 4
        A       byte displ      byte d  d(Rn)   op <- M[Rn + sxt.d]
        B       byte displ def  byte d  @d(Rn)  op <- M[M[Rn + sxt.d]]
        C       word displ      word d  d(Rn)   op <- M[Rn + sxt.d]
        D       word displ def  word d  @d(Rn)  op <- M[M[Rn + sxt.d]]
        E       long displ      long d  d(Rn)   op <- M[Rn + d]
        F       long displ def  long d  @d(Rn)  op <- M[M[Rn + d]]

   When the general register is the PC, certain modes are forbidden, and
   others have special interpretations:

        4F      index           fault
        5F      register        fault
        6F      register def    fault
        7F      autodecrement   fault
        8F      immediate       1-8B    #imm    op <- imm
        9       absolute        4B      @#imm   op <- M[imm]
        A       byte relative   byte d  d(Rn)   op <- M[PC + sxt.d]
        B       byte rel def    byte d  @d(Rn)  op <- M[M[PC + sxt.d]]
        C       word relative   word d  d(Rn)   op <- M[PC + sxt.d]
        D       word rel def    word d  @d(Rn)  op <- M[M[PC + sxt.d]]
        E       long relative   long d  d(Rn)   op <- M[PC + d]
        F       long rel def    long d  @d(Rn)  op <- M[M[PC + d]]

   This routine is the instruction decode routine for the VAX.  It
   is called from the simulator control program to execute instructions
   in simulated memory, starting at the simulated PC.  It runs until an
   enabled exception is encountered.

   General notes:

   1. Traps and interrupts.  Variable trpirq microencodes the outstanding
        trap request (if any) and the level of the highest outstanding
        interrupt (if any).

   2. Interrupt requests are maintained in the int_req array, one word per
      interrupt level, one bit per device.

   3. Adding I/O devices.  These modules must be modified:

        vax_defs.h      add device address and interrupt definitions
        vax_sys.c       add sim_devices table entry
*/

/* Definitions */

#include "vax_defs.h"

#define OP_MEM          -1
#define UNIT_V_CONH     (UNIT_V_UF + 0)                 /* halt to console */
#define UNIT_V_MSIZE    (UNIT_V_UF + 1)                 /* dummy */
#define UNIT_CONH       (1u << UNIT_V_CONH)
#define UNIT_MSIZE      (1u << UNIT_V_MSIZE)
#define GET_CUR         acc = ACC_MASK (PSL_GETCUR (PSL))

#define OPND_SIZE       16
#define INST_SIZE       52
#define op0             opnd[0]
#define op1             opnd[1]
#define op2             opnd[2]
#define op3             opnd[3]
#define op4             opnd[4]
#define op5             opnd[5]
#define op6             opnd[6]
#define op7             opnd[7]
#define op8             opnd[8]
#define CHECK_FOR_PC    if (rn == nPC) \
                            RSVD_ADDR_FAULT
#define CHECK_FOR_SP    if (rn >= nSP) \
                            RSVD_ADDR_FAULT
#define CHECK_FOR_AP    if (rn >= nAP) \
                            RSVD_ADDR_FAULT
#define WRITE_B(r)      if (spec > (GRN | nPC)) \
                            Write (va, r, L_BYTE, WA); \
                        else R[rn] = (R[rn] & ~BMASK) | ((r) & BMASK)
#define WRITE_W(r)      if (spec > (GRN | nPC)) \
                            Write (va, r, L_WORD, WA); \
                        else R[rn] = (R[rn] & ~WMASK) | ((r) & WMASK)
#define WRITE_L(r)      if (spec > (GRN | nPC)) \
                            Write (va, r, L_LONG, WA); \
                        else R[rn] = (r)
#define WRITE_Q(rl,rh)  if (spec > (GRN | nPC)) { \
                        if ((Test (va + 7, WA, &mstat) >= 0) || \
                            (Test (va, WA, &mstat) < 0)) \
                            Write (va, rl, L_LONG, WA); \
                            Write (va + 4, rh, L_LONG, WA); \
                            } \
                        else { \
                            if (rn >= nSP) \
                                RSVD_ADDR_FAULT; \
                            R[rn] = rl; \
                            R[rn + 1] = rh; \
                            }

#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    int32               iPC;
    int32               PSL;
    int32               opc;
    uint8               inst[INST_SIZE];
    int32               opnd[OPND_SIZE];
    } InstHistory;

uint32 *M = NULL;                                       /* memory */
int32 R[16];                                            /* registers */
int32 STK[5];                                           /* stack pointers */
int32 PSL;                                              /* PSL */
int32 SCBB = 0;                                         /* SCB base */
int32 PCBB = 0;                                         /* PCB base */
int32 P0BR = 0;                                         /* P0 mem mgt */
int32 P0LR = 0;
int32 P1BR = 0;                                         /* P1 mem mgt */
int32 P1LR = 0;
int32 SBR = 0;                                          /* S0 mem mgt */
int32 SLR = 0;
int32 SISR;                                             /* swre int req */
int32 ASTLVL;                                           /* AST level */
int32 mapen;                                            /* map enable */
int32 pme;                                              /* perf mon enable */
int32 trpirq;                                           /* trap/intr req */
int32 in_ie = 0;                                        /* in exc, int */
int32 recq[6];                                          /* recovery queue */
int32 recqptr;                                          /* recq pointer */
int32 hlt_pin = 0;                                      /* HLT pin intr */
int32 mem_err = 0;
int32 crd_err = 0;
int32 p1 = 0, p2 = 0;                                   /* fault parameters */
int32 fault_PC;                                         /* fault PC */
int32 pcq_p = 0;                                        /* PC queue ptr */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
int32 badabo = 0;
int32 cpu_astop = 0;
int32 mchk_va, mchk_ref;                                /* mem ref param */
int32 ibufl, ibufh;                                     /* prefetch buf */
int32 ibcnt, ppc;                                       /* prefetch ctl */
uint32 cpu_idle_ipl_mask = 0x8;                         /* idle if on IPL 3 */
uint32 cpu_idle_type = 1;                               /* default to VMS */
int32 cpu_idle_wait = 1000;                             /* for these cycles */
jmp_buf save_env;
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 pcq[PCQ_SIZE] = { 0 };                            /* PC queue */
InstHistory *hst = NULL;                                /* instruction history */

const uint32 byte_mask[33] = { 0x00000000,
 0x00000001, 0x00000003, 0x00000007, 0x0000000F,
 0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
 0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
 0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
 0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
 0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
 0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
 };
const uint32 byte_sign[33] = { 0x00000000,
 0x00000001, 0x00000002, 0x00000004, 0x00000008,
 0x00000010, 0x00000020, 0x00000040, 0x00000080,
 0x00000100, 0x00000200, 0x00000400, 0x00000800,
 0x00001000, 0x00002000, 0x00004000, 0x00008000,
 0x00010000, 0x00020000, 0x00040000, 0x00080000,
 0x00100000, 0x00200000, 0x00400000, 0x00800000,
 0x01000000, 0x02000000, 0x04000000, 0x08000000,
 0x10000000, 0x20000000, 0x40000000, 0x80000000
 };
const uint32 align[4] = {
 0xFFFFFFFF, 0x00FFFFFF, 0x0000FFFF, 0x000000FF
 };

/* External and forward references */

extern int32 sim_interval;
extern int32 sim_int_char;
extern int32 sim_switches;
extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */
extern t_bool sim_idle_enab;

extern t_stat build_dib_tab (void);
extern UNIT rom_unit, nvr_unit;
extern int32 op_ashq (int32 *opnd, int32 *rh, int32 *flg);
extern int32 op_emul (int32 mpy, int32 mpc, int32 *rh);
extern int32 op_ediv (int32 *opnd, int32 *rh, int32 *flg);
extern int32 op_bb_n (int32 *opnd, int32 acc);
extern int32 op_bb_x (int32 *opnd, int32 newb, int32 acc);
extern int32 op_extv (int32 *opnd, int32 vfldrp1, int32 acc);
extern int32 op_ffs (uint32 fld, int32 size);
extern void op_insv (int32 *opnd, int32 vfldrp1, int32 acc);
extern int32 op_call (int32 *opnd, t_bool gs, int32 acc);
extern int32 op_ret (int32 acc);
extern int32 op_insque (int32 *opnd, int32 acc);
extern int32 op_remque (int32 *opnd, int32 acc);
extern int32 op_insqhi (int32 *opnd, int32 acc);
extern int32 op_insqti (int32 *opnd, int32 acc);
extern int32 op_remqhi (int32 *opnd, int32 acc);
extern int32 op_remqti (int32 *opnd, int32 acc);
extern void op_pushr (int32 *opnd, int32 acc);
extern void op_popr (int32 *opnd, int32 acc);
extern int32 op_movc (int32 *opnd, int32 opc, int32 acc);
extern int32 op_cmpc (int32 *opnd, int32 opc, int32 acc);
extern int32 op_locskp (int32 *opnd, int32 opc, int32 acc);
extern int32 op_scnspn (int32 *opnd, int32 opc, int32 acc);
extern int32 op_chm (int32 *opnd, int32 cc, int32 opc);
extern int32 op_rei (int32 acc);
extern void op_ldpctx (int32 acc);
extern void op_svpctx (int32 acc);
extern int32 op_probe (int32 *opnd, int32 opc);
extern int32 op_mtpr (int32 *opnd);
extern int32 op_mfpr (int32 *opnd);
extern int32 op_movfd (int32 val);
extern int32 op_movg (int32 val);
extern int32 op_mnegfd (int32 val);
extern int32 op_mnegg (int32 val);
extern int32 op_cmpfd (int32 h1, int32 l1, int32 h2, int32 l2);
extern int32 op_cmpg (int32 h1, int32 l1, int32 h2, int32 l2);
extern int32 op_cvtifdg (int32 val, int32 *rh, int32 opc);
extern int32 op_cvtfdgi (int32 *opnd, int32 *flg, int32 opc);
extern int32 op_cvtdf (int32 *opnd);
extern int32 op_cvtgf (int32 *opnd);
extern int32 op_cvtfg (int32 *opnd, int32 *rh);
extern int32 op_cvtgh (int32 *opnd, int32 *hflt);
extern int32 op_addf (int32 *opnd, t_bool sub);
extern int32 op_addd (int32 *opnd, int32 *rh, t_bool sub);
extern int32 op_addg (int32 *opnd, int32 *rh, t_bool sub);
extern int32 op_mulf (int32 *opnd);
extern int32 op_muld (int32 *opnd, int32 *rh);
extern int32 op_mulg (int32 *opnd, int32 *rh);
extern int32 op_divf (int32 *opnd);
extern int32 op_divd (int32 *opnd, int32 *rh);
extern int32 op_divg (int32 *opnd, int32 *rh);
extern int32 op_emodf (int32 *opnd, int32 *intgr, int32 *flg);
extern int32 op_emodd (int32 *opnd, int32 *rh, int32 *intgr, int32 *flg);
extern int32 op_emodg (int32 *opnd, int32 *rh, int32 *intgr, int32 *flg);
extern void op_polyf (int32 *opnd, int32 acc);
extern void op_polyd (int32 *opnd, int32 acc);
extern void op_polyg (int32 *opnd, int32 acc);
extern int32 op_cmode (int32 cc);
extern int32 op_cis (int32 *opnd, int32 cc, int32 opc, int32 acc);
extern int32 op_octa (int32 *opnd, int32 cc, int32 opc, int32 acc, int32 spec, int32 va);
extern int32 intexc (int32 vec, int32 cc, int32 ipl, int ei);
extern int32 Test (uint32 va, int32 acc, int32 *status);
extern int32 BadCmPSL (int32 newpsl);
extern int32 eval_int (void);
extern int32 get_vector (int32 lvl);
extern void set_map_reg (void);
extern void rom_wr_B (int32 pa, int32 val);
extern int32 machine_check (int32 p1, int32 opc, int32 cc, int32 delta);
extern const uint16 drom[NUM_INST][MAX_SPEC + 1];
extern t_stat cpu_boot (int32 unitno, DEVICE *dptr);
extern int32 con_halt (int32 code, int32 cc);

t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_show_virt (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_set_idle (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_idle (FILE *st, UNIT *uptr, int32 val, void *desc);
int32 cpu_get_vsw (int32 sw);
SIM_INLINE int32 get_istr (int32 lnt, int32 acc);
int32 ReadOcta (int32 va, int32 *opnd, int32 j, int32 acc);
t_bool cpu_show_opnd (FILE *st, InstHistory *h, int32 line);
int32 cpu_psl_ipl_idle (int32 newpsl);
t_stat cpu_idle_svc (UNIT *uptr);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = {
    UDATA (&cpu_idle_svc, UNIT_FIX|UNIT_BINK, INITMEMSIZE)
    };

REG cpu_reg[] = {
    { HRDATA (PC, R[nPC], 32) },
    { HRDATA (R0, R[0], 32) },
    { HRDATA (R1, R[1], 32) },
    { HRDATA (R2, R[2], 32) },
    { HRDATA (R3, R[3], 32) },
    { HRDATA (R4, R[4], 32) },
    { HRDATA (R5, R[5], 32) },
    { HRDATA (R6, R[6], 32) },
    { HRDATA (R7, R[7], 32) },
    { HRDATA (R8, R[8], 32) },
    { HRDATA (R9, R[9], 32) },
    { HRDATA (R10, R[10], 32) },
    { HRDATA (R11, R[11], 32) },
    { HRDATA (R12, R[12], 32) },
    { HRDATA (R13, R[13], 32) },
    { HRDATA (R14, R[14], 32) },
    { HRDATA (AP, R[nAP], 32) },
    { HRDATA (FP, R[nFP], 32) },
    { HRDATA (SP, R[nSP], 32) },
    { HRDATA (PSL, PSL, 32) },
    { HRDATA (CC, PSL, 4) },
    { HRDATA (KSP, KSP, 32) },
    { HRDATA (ESP, ESP, 32) },
    { HRDATA (SSP, SSP, 32) },
    { HRDATA (USP, USP, 32) },
    { HRDATA (IS, IS, 32) },
    { HRDATA (SCBB, SCBB, 32) },
    { HRDATA (PCBB, PCBB, 32) },
    { HRDATA (P0BR, P0BR, 32) },
    { HRDATA (P0LR, P0LR, 22) },
    { HRDATA (P1BR, P1BR, 32) },
    { HRDATA (P1LR, P1LR, 22) },
    { HRDATA (SBR, SBR, 32) },
    { HRDATA (SLR, SLR, 22) },
    { HRDATA (SISR, SISR, 16) },
    { HRDATA (ASTLVL, ASTLVL, 4) },
    { FLDATA (MAPEN, mapen, 0) },
    { FLDATA (PME, pme, 0) },
    { HRDATA (TRPIRQ, trpirq, 8) },
    { FLDATA (CRDERR, crd_err, 0) },
    { FLDATA (MEMERR, mem_err, 0) },
    { FLDATA (HLTPIN, hlt_pin, 0) },
    { HRDATA (IDLE_IPL, cpu_idle_ipl_mask, 16), REG_HIDDEN },
    { DRDATA (IDLE_TYPE, cpu_idle_type, 4), REG_HRO },
    { DRDATA (IDLE_WAIT, cpu_idle_wait, 16), REG_HIDDEN },
    { BRDATA (PCQ, pcq, 16, 32, PCQ_SIZE), REG_RO+REG_CIRC },
    { HRDATA (PCQP, pcq_p, 6), REG_HRO },
    { HRDATA (BADABO, badabo, 32), REG_HRO },
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_CONH, 0, "HALT to SIMH", "SIMHALT", NULL },
    { UNIT_CONH, UNIT_CONH, "HALT to console", "CONHALT", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &cpu_set_idle, &cpu_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, (1u << 23), NULL, "8M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 24), NULL, "16M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 25), NULL, "32M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 25) + (1u << 24), NULL, "48M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 26), NULL, "64M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 27), NULL, "128M", &cpu_set_size },
#if !defined (VAX_780)
    { UNIT_MSIZE, (1u << 28), NULL, "256M", &cpu_set_size },
    { UNIT_MSIZE, (1u << 29), NULL, "512M", &cpu_set_size },
#endif
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "VIRTUAL", NULL,
      NULL, &cpu_show_virt },
    { 0 }
    };

DEBTAB cpu_deb[] = {
    { "INTEXC", LOG_CPU_I },
    { "REI", LOG_CPU_R },
    { "CONTEXT", LOG_CPU_P },
    { NULL, 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 32, 1, 16, 8,
    &cpu_ex, &cpu_dep, &cpu_reset,
    &cpu_boot, NULL, NULL,
    NULL, DEV_DYNM | DEV_DEBUG, 0,
    cpu_deb, &cpu_set_size, NULL
    };

t_stat sim_instr (void)
{
volatile int32 opc, cc;                                 /* used by setjmp */
int32 acc;                                              /* set by setjmp */
int abortval;
t_stat r;

if ((r = build_dib_tab ()) != SCPE_OK)                  /* build, chk dib_tab */
    return r;
if ((PSL & PSL_MBZ) ||                                  /* validate PSL<mbz> */
    ((PSL & PSL_CM) && BadCmPSL (PSL)) ||               /* validate PSL<cm> */
    ((PSL_GETCUR (PSL) != KERN) &&                      /* esu => is, ipl = 0 */
        (PSL & (PSL_IS|PSL_IPL))) ||
    ((PSL & PSL_IS) && ((PSL & PSL_IPL) == 0)))         /* is => ipl > 0 */
    return SCPE_STOP;
cc = PSL & CC_MASK;                                     /* split PSL */
PSL = PSL & ~CC_MASK;
in_ie = 0;                                              /* not in exc */
set_map_reg ();                                         /* set map reg */
GET_CUR;                                                /* set access mask */
SET_IRQL;                                               /* eval interrupts */
FLUSH_ISTR;                                             /* clear prefetch */

abortval = setjmp (save_env);                           /* set abort hdlr */
if (abortval > 0) {                                     /* sim stop? */
    PSL = PSL | cc;                                     /* put PSL together */
    pcq_r->qptr = pcq_p;                                /* update pc q ptr */
    return abortval;                                    /* return to SCP */
    }
else if (abortval < 0) {                                /* mm or rsrv or int */
    int32 i, delta;
    if ((PSL & PSL_FPD) == 0) {                         /* FPD? no recovery */
        for (i = 0; i < recqptr; i++) {                 /* unwind inst */
            int32 rrn, rlnt;
            rrn = RQ_GETRN (recq[i]);                   /* recover reg # */
            rlnt = DR_LNT (RQ_GETLNT (recq[i]));        /* recovery lnt */
            if (recq[i] & RQ_DIR)
                R[rrn] = R[rrn] - rlnt;
            else R[rrn] = R[rrn] + rlnt;
            }
        }
    PSL = PSL & ~PSL_TP;                                /* clear <tp> */
    recqptr = 0;                                        /* clear queue */
    delta = PC - fault_PC;                              /* save delta PC */
    SETPC (fault_PC);                                   /* restore PC */
    switch (-abortval) {                                /* case on abort code */

    case SCB_RESIN:                                     /* rsrv inst fault */
    case SCB_RESAD:                                     /* rsrv addr fault */
    case SCB_RESOP:                                     /* rsrv opnd fault */
        if (in_ie)                                      /* in exc? panic */
            ABORT (STOP_INIE);
        cc = intexc (-abortval, cc, 0, IE_EXC);         /* take exception */
        GET_CUR;                                        /* PSL<cur> changed */
        break;

    case SCB_CMODE:                                     /* comp mode fault */
    case SCB_ARITH:                                     /* arithmetic fault */
        if (in_ie)                                      /* in exc? panic */
            ABORT (STOP_INIE);
        cc = intexc (-abortval, cc, 0, IE_EXC);         /* take exception */
        GET_CUR;
        in_ie = 1;
        Write (SP - 4, p1, L_LONG, WA);                 /* write arith param */
        SP = SP - 4;
        in_ie = 0;
        break;

    case SCB_ACV:                                       /* ACV fault */
    case SCB_TNV:                                       /* TNV fault */
        if (in_ie) {                                    /* in exception? */
            if (PSL & PSL_IS)                           /* on is? panic */
                ABORT (STOP_INIE);
            cc = intexc (SCB_KSNV, cc, 0, IE_SVE);      /* ksnv */
            GET_CUR;
            }
        else {
            cc = intexc (-abortval, cc, 0, IE_EXC);     /* take exception */
            GET_CUR;
            in_ie = 1;
            Write (SP - 8, p1, L_LONG, WA);             /* write mm params */
            Write (SP - 4, p2, L_LONG, WA);
            SP = SP - 8;
            in_ie = 0;
            }
        break;

    case SCB_MCHK:                                      /* machine check */
        if (in_ie)                                      /* in exc? panic */
            ABORT (STOP_INIE);
        cc = machine_check (p1, opc, cc, delta);        /* system specific */
        in_ie = 0;
        GET_CUR;                                        /* PSL<cur> changed */
        break;

    case 1:                                             /* interrupt */
        break;                                          /* just proceed */
    default:                                            /* other */
        badabo = abortval;                              /* save code */
        ABORT (STOP_UNKABO);                            /* panic */
        }                                               /* end case */
    }                                                   /* end else */

/* Main instruction loop */

for ( ;; ) {

    int32 spec, disp, rn, index, numspec;
    int32 vfldrp1, brdisp, flg, mstat;
    int32 i, j, r, rh, temp;
    uint32 va, iad;
    int32 opnd[OPND_SIZE];                              /* operand queue */

    if (cpu_astop) {
        cpu_astop = 0;
        ABORT (SCPE_STOP);
        }
    fault_PC = PC;
    recqptr = 0;                                        /* clr recovery q */
    if (sim_interval <= 0) {                            /* chk clock queue */
        temp = sim_process_event ();
        if (temp)
            ABORT (temp);
        SET_IRQL;                                       /* update interrupts */
        }

/* Test for non-instruction dispatches, in SRM order

        - trap or interrupt (trpirq != 0)
        - PSL<tp> set

   If any of these conditions are met, re-dispatch; otherwise,
   set PSL<tp> from PSL<t>.
*/

    if (trpirq) {                                       /* trap or interrupt? */
        if (temp = GET_TRAP (trpirq)) {                 /* trap? */
            cc = intexc (SCB_ARITH, cc, 0, IE_EXC);     /* take, clear trap */
            GET_CUR;                                    /* set cur mode */
            in_ie = 1;
            Write (SP - 4, temp, L_LONG, WA);           /* write parameter */
            SP = SP - 4;
            in_ie = 0;
            }
        else if (temp = GET_IRQL (trpirq)) {            /* interrupt? */
            int32 vec;
            if (temp == IPL_HLTPIN) {                   /* console halt? */
                hlt_pin = 0;                            /* clear intr */
                trpirq = 0;                             /* clear everything */
                cc = con_halt (CON_HLTPIN, cc);         /* invoke firmware */
                continue;                               /* continue */
                }
            else if (temp >= IPL_HMIN)                  /* hardware req? */
                vec = get_vector (temp);                /* get vector */
            else if (temp > IPL_SMAX)
                ABORT (STOP_UIPL);
            else {
                vec = SCB_IPLSOFT + (temp << 2);
                SISR = SISR & ~(1u << temp);
                }
            if (vec)                                    /* take intr */
                cc = intexc (vec, cc, temp, IE_INT);
            GET_CUR;                                    /* set cur mode */
            }
        else trpirq = 0;                                /* clear everything */
        SET_IRQL;                                       /* eval interrupts */
        continue;
        }

    if (PSL & (PSL_CM|PSL_TP|PSW_T)) {                  /* PSL event? */
        if (PSL & PSL_TP) {                             /* trace trap? */
            PSL = PSL & ~PSL_TP;                        /* clear <tp> */
            cc = intexc (SCB_TP, cc, 0, IE_EXC);        /* take trap */
            GET_CUR;                                    /* set cur mode */
            continue;
            }
        if (PSL & PSW_T)                                /* if T, set TP */
            PSL = PSL | PSL_TP;
        if (PSL & PSL_CM) {                             /* compat mode? */
            cc = op_cmode (cc);                         /* exec instr */
            continue;                                   /* skip fetch */
            }
        }                                               /* end PSL event */

    if (sim_brk_summ &&
        sim_brk_test ((uint32) PC, SWMASK ('E'))) {     /* breakpoint? */
        ABORT (STOP_IBKPT);                             /* stop simulation */
        }

    sim_interval = sim_interval - 1;                    /* count instr */
    GET_ISTR (opc, L_BYTE);                             /* get opcode */
    if (opc == 0xFD) {                                  /* 2 byte op? */
        GET_ISTR (opc, L_BYTE);                         /* get second byte */
        opc = opc | 0x100;                              /* flag */
        }
    numspec = drom[opc][0];                             /* get # specs */
    if (PSL & PSL_FPD) {
        if ((numspec & DR_F) == 0)
            RSVD_INST_FAULT;
        }
    else {
        numspec = numspec & DR_NSPMASK;                 /* get # specifiers */

/* Specifier flows.  Operands are parsed and placed into queue opnd.

        r.bwl   opnd[j]         =       value of operand
        r.q     opnd[j:j+1]     =       value of operand
        r.o     opnd[j:j+3]     =       value of operand
        a.bwlqo opnd[j]         =       address of operand
        m.bwl   opnd[j]         =       value of operand
        m.q     opnd[j:j+1]     =       value of operand 
        m.o     opnd[j:j+3]     =       value of operand
        w.bwlqo opnd[j]         =       register/memory flag
                opnd[j+1]       =       memory address

   For the last memory specifier, the specifier is in spec, the register
   number is in rn, and the effective address is in va.  Modify specifiers
   (always last) can test spec > reg+PC, as short literal are illegal for
   modifiers specifiers, and final index specifiers are always illegal.
*/

        for (i = 1, j = 0; i <= numspec; i++) {         /* loop thru specs */
            disp = drom[opc][i];                        /* get dispatch */
            if (disp >= BB) {
                GET_ISTR (brdisp, DR_LNT (disp & 1));
                break;
                }
            GET_ISTR (spec, L_BYTE);                    /* get spec byte */
            rn = spec & RGMASK;                         /* get reg # */
            disp = (spec & ~RGMASK) | disp;             /* merge w dispatch */
            switch (disp) {                             /* dispatch spec */

/* Short literal - only read access permitted */

            case SH0|RB: case SH0|RW: case SH0|RL:
            case SH1|RB: case SH1|RW: case SH1|RL:
            case SH2|RB: case SH2|RW: case SH2|RL:
            case SH3|RB: case SH3|RW: case SH3|RL:
                opnd[j++] = spec;
                break;

            case SH0|RQ: case SH1|RQ: case SH2|RQ: case SH3|RQ:
                opnd[j++] = spec;
                opnd[j++] = 0;
                break;

            case SH0|RO: case SH1|RO: case SH2|RO: case SH3|RO:
                opnd[j++] = spec;
                opnd[j++] = 0;
                opnd[j++] = 0;
                opnd[j++] = 0;
                break;

            case SH0|RF: case SH1|RF: case SH2|RF: case SH3|RF:
                opnd[j++] = (spec << 4) | 0x4000;
                break;

            case SH0|RD: case SH1|RD: case SH2|RD: case SH3|RD:
                opnd[j++] = (spec << 4) | 0x4000;
                opnd[j++] = 0;
                break;

            case SH0|RG: case SH1|RG: case SH2|RG: case SH3|RG:
                opnd[j++] = (spec << 1) | 0x4000;
                opnd[j++] = 0;
                break;

            case SH0|RH: case SH1|RH: case SH2|RH: case SH3|RH:
                opnd[j++] = ((spec & 0x7) << 29) | (0x4000 | ((spec >> 3) & 0x7));
                opnd[j++] = 0;
                opnd[j++] = 0;
                opnd[j++] = 0;
                break;

/* Register */

            case GRN|RB: case GRN|MB:
                CHECK_FOR_PC;
                opnd[j++] = R[rn] & BMASK;
                break;

            case GRN|RW: case GRN|MW:
                CHECK_FOR_PC;
                opnd[j++] = R[rn] & WMASK;
                break;

            case GRN|VB:
                vfldrp1 = R[(rn + 1) & RGMASK];
            case GRN|WB: case GRN|WW: case GRN|WL: case GRN|WQ: case GRN|WO:
                opnd[j++] = rn;
            case GRN|RL: case GRN|RF: case GRN|ML:
                CHECK_FOR_PC;
                opnd[j++] = R[rn];
                break;

            case GRN|RQ: case GRN|RD: case GRN|RG: case GRN|MQ:
                CHECK_FOR_SP;
                opnd[j++] = R[rn];
                opnd[j++] = R[rn + 1];
                break;

            case GRN|RO: case GRN|RH: case GRN|MO:
                CHECK_FOR_AP;
                opnd[j++] = R[rn];
                opnd[j++] = R[rn + 1];
                opnd[j++] = R[rn + 2];
                opnd[j++] = R[rn + 3];
                break;

/*  Register deferred, autodecrement */

            case RGD|VB:
            case RGD|WB: case RGD|WW: case RGD|WL: case RGD|WQ: case RGD|WO:
                opnd[j++] = OP_MEM;
            case RGD|AB: case RGD|AW: case RGD|AL: case RGD|AQ: case RGD|AO:
                CHECK_FOR_PC;
                va = opnd[j++] = R[rn];
                break;

            case ADC|VB:
            case ADC|WB: case ADC|WW: case ADC|WL: case ADC|WQ: case ADC|WO:
                opnd[j++] = OP_MEM;
            case ADC|AB: case ADC|AW: case ADC|AL: case ADC|AQ: case ADC|AO:
                CHECK_FOR_PC;
                va = opnd[j++] = R[rn] = R[rn] - DR_LNT (disp);
                recq[recqptr++] = RQ_REC (disp, rn);
                break;

            case ADC|RB: case ADC|RW: case ADC|RL: case ADC|RF:
                R[rn] = R[rn] - (DR_LNT (disp));
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|RB: case RGD|RW: case RGD|RL: case RGD|RF:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], DR_LNT (disp), RA);
                break;

            case ADC|RQ: case ADC|RD: case ADC|RG:
                R[rn] = R[rn] - 8;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|RQ: case RGD|RD: case RGD|RG:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], L_LONG, RA);
                opnd[j++] = Read (R[rn] + 4, L_LONG, RA);
                break;

            case ADC|RO: case ADC|RH:
                R[rn] = R[rn] - 16;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|RO: case RGD|RH:
                CHECK_FOR_PC;
                j = ReadOcta (va = R[rn], opnd, j, RA);
                break;

            case ADC|MB: case ADC|MW: case ADC|ML:
                R[rn] = R[rn] - (DR_LNT (disp));
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|MB: case RGD|MW: case RGD|ML:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], DR_LNT (disp), WA);
                break;

            case ADC|MQ:
                R[rn] = R[rn] - 8;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|MQ:
                CHECK_FOR_PC;
                opnd[j++] = Read (va = R[rn], L_LONG, WA);
                opnd[j++] = Read (R[rn] + 4, L_LONG, WA);
                break;

            case ADC|MO:
                R[rn] = R[rn] - 16;
                recq[recqptr++] = RQ_REC (disp, rn);
            case RGD|MO:
                CHECK_FOR_PC;
                j = ReadOcta (va = R[rn], opnd, j, WA);
                break;

/* Autoincrement */

            case AIN|VB:
            case AIN|WB: case AIN|WW: case AIN|WL: case AIN|WQ: case AIN|WO:
/*              CHECK_FOR_PC; */
                opnd[j++] = OP_MEM;
            case AIN|AB: case AIN|AW: case AIN|AL: case AIN|AQ: case AIN|AO:
                va = opnd[j++] = R[rn];
                if (rn == nPC) {
                    if (DR_LNT (disp) >= L_QUAD) {
                        GET_ISTR (temp, L_LONG);
                        GET_ISTR (temp, L_LONG);
                        if (DR_LNT (disp) == L_OCTA) {
                            GET_ISTR (temp, L_LONG);
                            GET_ISTR (temp, L_LONG);
                            }
                        }
                    else GET_ISTR (temp, DR_LNT (disp));
                    }
                else {
                    R[rn] = R[rn] + DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|RB: case AIN|RW: case AIN|RL: case AIN|RF:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], DR_LNT (disp));
                    }
                else {
                    opnd[j++] = Read (R[rn], DR_LNT (disp), RA);
                    R[rn] = R[rn] + DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|RQ: case AIN|RD: case AIN|RG:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    opnd[j++] = Read (va, L_LONG, RA);
                    opnd[j++] = Read (va + 4, L_LONG, RA);  
                    R[rn] = R[rn] + 8;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|RO: case AIN|RH:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    j = ReadOcta (va, opnd, j, RA);
                    R[rn] = R[rn] + 16;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|MB: case AIN|MW: case AIN|ML:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], DR_LNT (disp));
                    }
                else {
                    opnd[j++] = Read (R[rn], DR_LNT (disp), WA);
                    R[rn] = R[rn] + DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|MQ:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    opnd[j++] = Read (va, L_LONG, WA);
                    opnd[j++] = Read (va + 4, L_LONG, WA);  
                    R[rn] = R[rn] + 8;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

            case AIN|MO:
                va = R[rn];
                if (rn == nPC) {
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    GET_ISTR (opnd[j++], L_LONG);
                    }
                else {
                    j = ReadOcta (va, opnd, j, WA);
                    R[rn] = R[rn] + 16;
                    recq[recqptr++] = RQ_REC (disp, rn);
                    }
                break;

/* Autoincrement deferred */

            case AID|VB:
            case AID|WB: case AID|WW: case AID|WL: case AID|WQ: case AID|WO:
                opnd[j++] = OP_MEM;
            case AID|AB: case AID|AW: case AID|AL: case AID|AQ: case AID|AO:
                if (rn == nPC) {
                    GET_ISTR (va = opnd[j++], L_LONG);
                    }
                else {
                    va = opnd[j++] = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                break;

            case AID|RB: case AID|RW: case AID|RL: case AID|RF:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case AID|RQ: case AID|RD: case AID|RG:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case AID|RO: case AID|RH:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                j = ReadOcta (va, opnd, j, RA);
                break;

            case AID|MB: case AID|MW: case AID|ML:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case AID|MQ:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case AID|MO:
                if (rn == nPC) {
                    GET_ISTR (va, L_LONG);
                    }
                else {
                    va = Read (R[rn], L_LONG, RA);
                    R[rn] = R[rn] + 4;
                    recq[recqptr++] = RQ_REC (AID|RL, rn);
                    }
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Byte displacement */

            case BDP|VB:
            case BDP|WB: case BDP|WW: case BDP|WL: case BDP|WQ: case BDP|WO:
                opnd[j++] = OP_MEM;
            case BDP|AB: case BDP|AW: case BDP|AL: case BDP|AQ: case BDP|AO:
                GET_ISTR (temp, L_BYTE);
                va = opnd[j++] = R[rn] + SXTB (temp);
                break;

            case BDP|RB: case BDP|RW: case BDP|RL: case BDP|RF:
                GET_ISTR (temp, L_BYTE);
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case BDP|RQ: case BDP|RD: case BDP|RG:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case BDP|RO: case BDP|RH:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case BDP|MB: case BDP|MW: case BDP|ML:
                GET_ISTR (temp, L_BYTE);
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case BDP|MQ:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case BDP|MO:
                GET_ISTR (temp, L_BYTE);        
                va = R[rn] + SXTB (temp);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Byte displacement deferred */

            case BDD|VB:
            case BDD|WB: case BDD|WW: case BDD|WL: case BDD|WQ: case BDD|WO:
                opnd[j++] = OP_MEM;
            case BDD|AB: case BDD|AW: case BDD|AL: case BDD|AQ: case BDD|AO:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = opnd[j++] = Read (iad, L_LONG, RA);
                break;

            case BDD|RB: case BDD|RW: case BDD|RL: case BDD|RF:
                GET_ISTR (temp, L_BYTE);        
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);    
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case BDD|RQ: case BDD|RD: case BDD|RG:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;  

            case BDD|RO: case BDD|RH:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, RA);
                break;  

            case BDD|MB: case BDD|MW: case BDD|ML:
                GET_ISTR (temp, L_BYTE);        
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);    
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case BDD|MQ:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;  

            case BDD|MO:
                GET_ISTR (temp, L_BYTE);
                iad = R[rn] + SXTB (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, WA);
                break;  

/* Word displacement */

            case WDP|VB:
            case WDP|WB: case WDP|WW: case WDP|WL: case WDP|WQ: case WDP|WO:
                opnd[j++] = OP_MEM;
            case WDP|AB: case WDP|AW: case WDP|AL: case WDP|AQ: case WDP|AO:
                GET_ISTR (temp, L_WORD);
                va = opnd[j++] = R[rn] + SXTW (temp);
                break;

            case WDP|RB: case WDP|RW: case WDP|RL: case WDP|RF:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case WDP|RQ: case WDP|RD: case WDP|RG:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case WDP|RO: case WDP|RH:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case WDP|MB: case WDP|MW: case WDP|ML:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case WDP|MQ:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case WDP|MO:
                GET_ISTR (temp, L_WORD);
                va = R[rn] + SXTW (temp);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Word displacement deferred */

            case WDD|VB:
            case WDD|WB: case WDD|WW: case WDD|WL: case WDD|WQ: case WDD|WO:
                opnd[j++] = OP_MEM;
            case WDD|AB: case WDD|AW: case WDD|AL: case WDD|AQ: case WDD|AO:
                GET_ISTR (temp, L_WORD);
                iad = R[rn] + SXTW (temp);
                va = opnd[j++] = Read (iad, L_LONG, RA);
                break;

            case WDD|RB: case WDD|RW: case WDD|RL: case WDD|RF:
                GET_ISTR (temp, L_WORD);
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case WDD|RQ: case WDD|RD: case WDD|RG:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case WDD|RO: case WDD|RH:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case WDD|MB: case WDD|MW: case WDD|ML:
                GET_ISTR (temp, L_WORD);
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case WDD|MQ:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case WDD|MO:
                GET_ISTR (temp, L_WORD);        
                iad = R[rn] + SXTW (temp);
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Longword displacement */

            case LDP|VB:
            case LDP|WB: case LDP|WW: case LDP|WL: case LDP|WQ: case LDP|WO:
                opnd[j++] = OP_MEM;
            case LDP|AB: case LDP|AW: case LDP|AL: case LDP|AQ: case LDP|AO:
                GET_ISTR (temp, L_LONG);
                va = opnd[j++] = R[rn] + temp;
                break;

            case LDP|RB: case LDP|RW: case LDP|RL: case LDP|RF:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case LDP|RQ: case LDP|RD: case LDP|RG:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case LDP|RO: case LDP|RH:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                j = ReadOcta (va, opnd, j, RA);
                break;

            case LDP|MB: case LDP|MW: case LDP|ML:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case LDP|MQ:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case LDP|MO:
                GET_ISTR (temp, L_LONG);
                va = R[rn] + temp;
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Longword displacement deferred */

            case LDD|VB:
            case LDD|WB: case LDD|WW: case LDD|WL: case LDD|WQ: case LDD|WO:
                opnd[j++] = OP_MEM;
            case LDD|AB: case LDD|AW: case LDD|AL: case LDD|AQ: case LDD|AO:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = opnd[j++] = Read (iad, L_LONG, RA);
                break;

            case LDD|RB: case LDD|RW: case LDD|RL: case LDD|RF:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), RA);
                break;

            case LDD|RQ: case LDD|RD: case LDD|RG:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, RA);
                opnd[j++] = Read (va + 4, L_LONG, RA);
                break;

            case LDD|RO: case LDD|RH:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, RA);
                break;

            case LDD|MB: case LDD|MW: case LDD|ML:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, DR_LNT (disp), WA);
                break;

            case LDD|MQ:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                opnd[j++] = Read (va, L_LONG, WA);
                opnd[j++] = Read (va + 4, L_LONG, WA);
                break;

            case LDD|MO:
                GET_ISTR (temp, L_LONG);
                iad = R[rn] + temp;
                va = Read (iad, L_LONG, RA);
                j = ReadOcta (va, opnd, j, WA);
                break;

/* Index */

            case IDX|VB:
            case IDX|WB: case IDX|WW: case IDX|WL: case IDX|WQ: case IDX|WO:
            case IDX|AB: case IDX|AW: case IDX|AL: case IDX|AQ: case IDX|AO:
            case IDX|MB: case IDX|MW: case IDX|ML: case IDX|MQ: case IDX|MO:
            case IDX|RB: case IDX|RW: case IDX|RL: case IDX|RQ: case IDX|RO:
            case IDX|RF: case IDX|RD: case IDX|RG: case IDX|RH:
                CHECK_FOR_PC;
                index = R[rn] << (disp & DR_LNMASK);
                GET_ISTR (spec, L_BYTE);
                rn = spec & RGMASK;
                switch (spec & ~RGMASK) {
                case ADC:
                    R[rn] = R[rn] - DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (ADC | (disp & DR_LNMASK), rn);
                case RGD:
                    CHECK_FOR_PC;       
                    index = index + R[rn];
                    break;

                case AIN:
                    CHECK_FOR_PC;
                    index = index + R[rn];
                    R[rn] = R[rn] + DR_LNT (disp);
                    recq[recqptr++] = RQ_REC (AIN | (disp & DR_LNMASK), rn);
                    break;

                case AID:
                    if (rn == nPC) {
                        GET_ISTR (temp, L_LONG);
                        }
                    else {
                        temp = Read (R[rn], L_LONG, RA);
                        R[rn] = R[rn] + 4;
                        recq[recqptr++] = RQ_REC (AID|RL, rn);
                        }
                    index = temp + index;
                    break;

                case BDP:
                    GET_ISTR (temp, L_BYTE);
                    index = index + R[rn] + SXTB (temp);
                    break;

                case BDD:
                    GET_ISTR (temp, L_BYTE);
                    index = index + Read (R[rn] + SXTB (temp), L_LONG, RA);
                    break;

                case WDP:
                    GET_ISTR (temp, L_WORD);
                    index = index + R[rn] + SXTW (temp);
                    break;

                case WDD:
                    GET_ISTR (temp, L_WORD);
                    index = index + Read (R[rn] + SXTW (temp), L_LONG, RA);
                    break;

                case LDP:
                    GET_ISTR (temp, L_LONG);
                    index = index + R[rn] + temp;
                    break;

                case LDD:
                    GET_ISTR (temp, L_LONG);
                    index = index + Read (R[rn] + temp, L_LONG, RA);
                    break;

                default:
                    RSVD_ADDR_FAULT;                    /* end case idxspec */
					}

                switch (disp & (DR_ACMASK|DR_SPFLAG|DR_LNMASK)) { /* case acc+lnt */
                case VB:
                case WB: case WW: case WL: case WQ: case WO:
                    opnd[j++] = OP_MEM;
                case AB: case AW: case AL: case AQ: case AO:
                    va = opnd[j++] = index;
                    break;

                case RB: case RW: case RL: case RF:
                    opnd[j++] = Read (va = index, DR_LNT (disp), RA);
                    break;

                case RQ: case RD: case RG:
                    opnd[j++] = Read (va = index, L_LONG, RA);
                    opnd[j++] = Read (index + 4, L_LONG, RA);
                    break;

                case RO: case RH:
                    j = ReadOcta (va = index, opnd, j, RA);
                    break;

                case MB: case MW: case ML:
                    opnd[j++] = Read (va = index, DR_LNT (disp), WA);
                    break;

                case MQ:
                    opnd[j++] = Read (va = index, L_LONG, WA);
                    opnd[j++] = Read (index + 4, L_LONG, WA);
                    break;

                case MO:
                    j = ReadOcta (va = index, opnd, j, WA);
                    break;

                default:                                /* all others */
                    RSVD_ADDR_FAULT;                    /* fault */
                    break;
                    }                                   /* end case access/lnt */
                break;                                  /* end index */

            default:                                    /* all others */
                RSVD_ADDR_FAULT;                        /* fault */
                break;
                }                                       /* end case spec */
            }                                           /* end for */
        }                                               /* end if not FPD */

/* Optionally record instruction history */

    if (hst_lnt) {
        int32 lim;
        t_value wd;

        hst[hst_p].iPC = fault_PC;
        hst[hst_p].PSL = PSL | cc;
        hst[hst_p].opc = opc;
        for (i = 0; i < j; i++)
            hst[hst_p].opnd[i] = opnd[i];
        lim = PC - fault_PC;
        if ((uint32) lim > INST_SIZE)
            lim = INST_SIZE;
        for (i = 0; i < lim; i++) {
            if ((cpu_ex (&wd, fault_PC + i, &cpu_unit, SWMASK ('V'))) == SCPE_OK)
                hst[hst_p].inst[i] = (uint8) wd;
            else {
                hst[hst_p].inst[0] = hst[hst_p].inst[1] = 0xFF;
                break;
                }
            }
        hst_p = hst_p + 1;
        if (hst_p >= hst_lnt)
            hst_p = 0;
        }

/* Dispatch to instructions */

    switch (opc) {              

/* Single operand instructions with dest, write only - CLRx dst.wx

        spec    =       reg/memory flag
        rn      =       register number
        va      =       virtual address
*/

    case CLRB:
        WRITE_B (0);                                    /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

    case CLRW:
        WRITE_W (0);                                    /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

    case CLRL:
        WRITE_L (0);                                    /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

    case CLRQ:
        WRITE_Q (0, 0);                                 /* store result */
        CC_ZZ1P;                                        /* set cc's */
        break;

/* Single operand instructions with source, read only - TSTx src.rx

        opnd[0] =       source
*/

    case TSTB:
        CC_IIZZ_B (op0);                                /* set cc's */
        break;

    case TSTW:
        CC_IIZZ_W (op0);                                /* set cc's */
        break;

    case TSTL:
        CC_IIZZ_L (op0);                                /* set cc's */
        break;

/* Single operand instructions with source, read/write - op src.mx

        opnd[0] =       operand
        spec    =       reg/mem flag
        rn      =       register number
        va      =       operand address
*/

    case INCB:
        r = (op0 + 1) & BMASK;                          /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_ADD_B (r, 1, op0);                           /* set cc's */
        break;

    case INCW:
        r = (op0 + 1) & WMASK;                          /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_ADD_W (r, 1, op0);                           /* set cc's */
        break;

    case INCL:
        r = (op0 + 1) & LMASK;                          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_ADD_L (r, 1, op0);                           /* set cc's */
        break;

    case DECB:
        r = (op0 - 1) & BMASK;                          /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_SUB_B (r, 1, op0);                           /* set cc's */
        break;

    case DECW:
        r = (op0 - 1) & WMASK;                          /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_SUB_W (r, 1, op0);                           /* set cc's */
        break;

    case DECL:
        r = (op0 - 1) & LMASK;                          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, 1, op0);                           /* set cc's */
        break;

/* Push instructions - PUSHL src.rl or PUSHAx src.ax
        
        opnd[0] =       source
*/

    case PUSHL: case PUSHAB: case PUSHAW: case PUSHAL: case PUSHAQ:
        Write (SP - 4, op0, L_LONG, WA);                /* push operand */
        SP = SP - 4;                                    /* decr stack ptr */
        CC_IIZP_L (op0);                                /* set cc's */
        break;

/* Moves, converts, and ADAWI - op src.rx, dst.wx
        
        opnd[0] =       source
        spec    =       reg/mem flag
        rn      =       register number
        va      =       operand address
*/

    case MOVB:
        WRITE_B (op0);                                  /* result */
        CC_IIZP_B (op0);                                /* set cc's */
        break;

    case MOVW: case MOVZBW:
        WRITE_W (op0);                                  /* result */
        CC_IIZP_W (op0);                                /* set cc's */
        break;

    case MOVL: case MOVZBL: case MOVZWL:
    case MOVAB: case MOVAW: case MOVAL: case MOVAQ:
        WRITE_L (op0);                                  /* result */
        CC_IIZP_L (op0);                                /* set cc's */
        break;

    case MCOMB:
        r = op0 ^ BMASK;                                /* compl opnd */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case MCOMW:
        r = op0 ^ WMASK;                                /* compl opnd */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case MCOML:
        r = op0 ^ LMASK;                                /* compl opnd */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case MNEGB:
        r = (-op0) & BMASK;                             /* negate opnd */
        WRITE_B (r);                                    /* store result */
        CC_SUB_B (r, op0, 0);                           /* set cc's */
        break;

    case MNEGW:
        r = (-op0) & WMASK;                             /* negate opnd */
        WRITE_W (r);                                    /* store result */
        CC_SUB_W (r, op0, 0);                           /* set cc's */
        break;

    case MNEGL:
        r = (-op0) & LMASK;                             /* negate opnd */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, op0, 0);                           /* set cc's */
        break;

    case CVTBW:
        r = SXTBW (op0);                                /* ext sign */
        WRITE_W (r);                                    /* store result */
        CC_IIZZ_W (r);                                  /* set cc's */
        break;

    case CVTBL:
        r = SXTB (op0);                                 /* ext sign */
        WRITE_L (r);                                    /* store result */
        CC_IIZZ_L (r);                                  /* set cc's */
        break;

    case CVTWL:
        r = SXTW (op0);                                 /* ext sign */
        WRITE_L (r);                                    /* store result */
        CC_IIZZ_L (r);                                  /* set cc's */
        break;

    case CVTLB:
        r = op0 & BMASK;                                /* set result */
        WRITE_B (r);                                    /* store result */
        CC_IIZZ_B (r);                                  /* initial cc's */
        if ((op0 > 127) || (op0 < -128)) {
            V_INTOV;
            }
        break;

    case CVTLW:
        r = op0 & WMASK;                                /* set result */
        WRITE_W (r);                                    /* store result */
        CC_IIZZ_W (r);                                  /* initial cc's */
        if ((op0 > 32767) || (op0 < -32768)) {
            V_INTOV;
            }
        break;

    case CVTWB:
        r = op0 & BMASK;                                /* set result */
        WRITE_B (r);                                    /* store result */
        CC_IIZZ_B (r);                                  /* initial cc's */
        temp = SXTW (op0);                              /* cvt op to long */
        if ((temp > 127) || (temp < -128)) {
            V_INTOV;
            }
        break;

    case ADAWI:
        if (op1 >= 0) temp = R[op1] & WMASK;            /* reg? ADDW2 */
        else {
            if (op2 & 1)                                /* mem? chk align */
                RSVD_OPND_FAULT;
            temp = Read (op2, L_WORD, WA);              /* ok, ADDW2 */
            }
        r = (op0 + temp) & WMASK;
        WRITE_W (r);
        CC_ADD_W (r, op0, temp);                        /* set cc's */
        break;

/* Integer operates, 2 operand, read only - op src1.rx, src2.rx

        opnd[0] =       source1
        opnd[1] =       source2
*/

    case CMPB:
        CC_CMP_B (op0, op1);                            /* set cc's */
        break;

    case CMPW:
        CC_CMP_W (op0, op1);                            /* set cc's */
        break;

    case CMPL:
        CC_CMP_L (op0, op1);                            /* set cc's */
        break;

    case BITB:
        r = op1 & op0;                                  /* calc result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case BITW:
        r = op1 & op0;                                  /* calc result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case BITL:
        r = op1 & op0;                                  /* calc result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

/* Integer operates, 2 operand read/write, and 3 operand, also MOVQ
        op2 src.rx, dst.mx      op3 src.rx, src.rx, dst.wx

        opnd[0] =       source1
        opnd[1] =       source2
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case ADDB2: case ADDB3:
        r = (op1 + op0) & BMASK;                        /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_ADD_B (r, op0, op1);                         /* set cc's */
        break;

    case ADDW2: case ADDW3:
        r = (op1 + op0) & WMASK;                        /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_ADD_W (r, op0, op1);                         /* set cc's */
        break;

    case ADWC:
        r = (op1 + op0 + (cc & CC_C)) & LMASK;          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_ADD_L (r, op0, op1);                         /* set cc's */
        if ((r == op1) && op0)                          /* special case */
            cc = cc | CC_C;
        break;

    case ADDL2: case ADDL3:
        r = (op1 + op0) & LMASK;                        /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_ADD_L (r, op0, op1);                         /* set cc's */
        break;

    case SUBB2: case SUBB3:
        r = (op1 - op0) & BMASK;                        /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_SUB_B (r, op0, op1);                         /* set cc's */
        break;

    case SUBW2: case SUBW3:
        r = (op1 - op0) & WMASK;                        /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_SUB_W (r, op0, op1);                         /* set cc's */
        break;

    case SBWC:
        r = (op1 - op0 - (cc & CC_C)) & LMASK;          /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, op0, op1);                         /* set cc's */
        if ((op0 == op1) && r)                          /* special case */
            cc = cc | CC_C;
        break;

    case SUBL2: case SUBL3:
        r = (op1 - op0) & LMASK;                        /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_SUB_L (r, op0, op1);                         /* set cc's */
        break;

    case MULB2: case MULB3:
        temp = SXTB (op0) * SXTB (op1);                 /* multiply */
        r = temp & BMASK;                               /* mask to result */
        WRITE_B (r);                                    /* store result */
        CC_IIZZ_B (r);                                  /* set cc's */
        if ((temp > 127) || (temp < -128)) {
            V_INTOV;
            }
        break;

    case MULW2: case MULW3:
        temp = SXTW (op0) * SXTW (op1);                 /* multiply */
        r = temp & WMASK;                               /* mask to result */
        WRITE_W (r);                                    /* store result */
        CC_IIZZ_W (r);                                  /* set cc's */
        if ((temp > 32767) || (temp < -32768)) {
            V_INTOV;
            }
        break;

    case MULL2: case MULL3:
        r = op_emul (op0, op1, &rh);                    /* get 64b result */
        WRITE_L (r);                                    /* store result */
        CC_IIZZ_L (r);                                  /* set cc's */
        if (rh != ((r & LSIGN)? -1: 0)) {               /* chk overflow */
            V_INTOV;
            }
        break;

    case DIVB2: case DIVB3:
        if (op0 == 0) {                                 /* div by zero? */
            r = op1;
            temp = CC_V;
            SET_TRAP (TRAP_DIVZRO);
            }
        else if ((op0 == BMASK) && (op1 == BSIGN)) {    /* overflow? */
            r = op1;
            temp = CC_V;
            INTOV;
            }
        else {
            r = SXTB (op1) / SXTB (op0);                /* ok, divide */
            temp = 0;
            }
        r = r & BMASK;                                  /* mask to result */
        WRITE_B (r);                                    /* write result */
        CC_IIZZ_B (r);                                  /* set cc's */
        cc = cc | temp;                                 /* error? set V */
        break;

    case DIVW2: case DIVW3:
        if (op0 == 0) {                                 /* div by zero? */
            r = op1;
            temp = CC_V;
            SET_TRAP (TRAP_DIVZRO);
            }
        else if ((op0 == WMASK) && (op1 == WSIGN)) {    /* overflow? */
            r = op1;
            temp = CC_V;
            INTOV;
            }
        else {
            r = SXTW (op1) / SXTW (op0);                /* ok, divide */
            temp = 0;
            }
        r = r & WMASK;                                  /* mask to result */
        WRITE_W (r);                                    /* write result */
        CC_IIZZ_W (r);                                  /* set cc's */
        cc = cc | temp;                                 /* error? set V */
        break;

    case DIVL2: case DIVL3:
        if (op0 == 0) {                                 /* div by zero? */
            r = op1;
            temp = CC_V;
            SET_TRAP (TRAP_DIVZRO);
            }
        else if ((op0 == LMASK) && (op1 == LSIGN)) {    /* overflow? */
            r = op1;
            temp = CC_V;
            INTOV;
            }
        else {
            r = op1 / op0;                              /* ok, divide */
            temp = 0;
            }
        r = r & LMASK;                                  /* mask to result */
        WRITE_L (r);                                    /* write result */
        CC_IIZZ_L (r);                                  /* set cc's */
        cc = cc | temp;                                 /* error? set V */
        break;

    case BISB2: case BISB3:
        r = op1 | op0;                                  /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case BISW2: case BISW3:
        r = op1 | op0;                                  /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case BISL2: case BISL3:
        r = op1 | op0;                                  /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case BICB2: case BICB3:
        r = op1 & ~op0;                                 /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case BICW2: case BICW3:
        r = op1 & ~op0;                                 /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case BICL2: case BICL3:
        r = op1 & ~op0;                                 /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case XORB2: case XORB3:
        r = op1 ^ op0;                                  /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        break;

    case XORW2: case XORW3:
        r = op1 ^ op0;                                  /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        break;

    case XORL2: case XORL3:
        r = op1 ^ op0;                                  /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

/* MOVQ - movq src.rq, dst.wq

        opnd[0:1] =     source
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
        
*/

    case MOVQ:
        WRITE_Q (op0, op1);                             /* store result */
        CC_IIZP_Q (op0, op1);
        break;

/* Shifts - op shf.rb,src.rl,dst.wl

        opnd[0] =       shift count
        opnd[1] =       source
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case ROTL:
        j = op0 % 32;                                   /* reduce sc, mod 32 */
        if (j)
            r = ((((uint32) op1) << j) | (((uint32) op1) >> (32 - j))) & LMASK;
        else r = op1;
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case ASHL:
        if (op0 & BSIGN) {                              /* right shift? */
            temp = 0x100 - op0;                         /* get |shift| */
            if (temp > 31)                              /* sc > 31? */
                r = (op1 & LSIGN)? LMASK: 0;
            else r = op1 >> temp;                       /* shift */
            WRITE_L (r);                                /* store result */
            CC_IIZZ_L (r);                              /* set cc's */
            break;
            }
        else {
            if (op0 > 31)                               /* sc > 31? */
                r = temp = 0;
            else {
                r = (((uint32) op1) << op0) & LMASK;    /* shift */
                temp = r >> op0;                        /* shift back */
                }
            WRITE_L (r);                                /* store result */
            CC_IIZZ_L (r);                              /* set cc's */
            if (op1 != temp) {                          /* bits lost? */
                V_INTOV;
                }
            }
        break;

    case ASHQ:
        r = op_ashq (opnd, &rh, &flg);                  /* do qw shift */
        WRITE_Q (r, rh);                                /* store results */
        CC_IIZZ_Q (r, rh);                              /* set cc's */
        if (flg) {                                      /* if ovflo, set */
            V_INTOV;
            }
        break;

/* EMUL - emul mplr.rl,mpcn.rl,add.rl,dst.wq

        op0     =       multiplier
        op1     =       multiplicand
        op2     =       adder
        op3:op4 =       destination (.wq)
*/

    case EMUL:
        r = op_emul (op0, op1, &rh);                    /* calc 64b result */
        r = r + op2;                                    /* add 32b value */
        rh = rh + (((uint32) r) < ((uint32) op2)) -     /* into 64b result */
            ((op2 & LSIGN)? 1: 0);
        WRITE_Q (r, rh);                                /* write result */
        CC_IIZZ_Q (r, rh);                              /* set cc's */
        break;

/* EDIV - ediv dvr.rl,dvd.rq,quo.wl,rem.wl

        op0     =       divisor (.rl)
        op1:op2 =       dividend (.rq)
        op3:op4 =       quotient address (.wl)
        op5:op6 =       remainder address (.wl)
*/

    case EDIV:
        if (op5 < 0)                                    /* wtest remainder */
            Read (op6, L_LONG, WA);
        if (op0 == 0) {                                 /* divide by zero? */
            flg = CC_V;                                 /* set V */
            r = opnd[1];                                /* quo = low divd */
            rh = 0;                                     /* rem = 0 */
            SET_TRAP (TRAP_DIVZRO);                     /* set trap */
            }
        else {
            r = op_ediv (opnd, &rh, &flg);              /* extended divide */
            if (flg) {                                  /* if ovf+IV, set trap */
                INTOV;
                }
            }
        if (op3 >= 0)                                   /* store quotient */
            R[op3] = r;
        else Write (op4, r, L_LONG, WA);
        if (op5 >= 0)                                   /* store remainder */
            R[op5] = rh;
        else Write (op6, rh, L_LONG, WA);
        CC_IIZZ_L (r);                                  /* set cc's */
        cc = cc | flg;                                  /* set V if required */
        break;

/* Control instructions */

/* Simple branches and subroutine calls */

    case BRB:
        BRANCHB (brdisp);                               /* branch  */
        if ((PC == fault_PC) && (PSL_GETIPL (PSL) == 0x1F))
            ABORT (STOP_LOOP);
        break;

    case BRW:
        BRANCHW (brdisp);                               /* branch */
        if ((PC == fault_PC) && (PSL_GETIPL (PSL) == 0x1F))
            ABORT (STOP_LOOP);
        break;

    case BSBB:
        Write (SP - 4, PC, L_LONG, WA);                 /* push PC on stk */
        SP = SP - 4;                                    /* decr stk ptr */
        BRANCHB (brdisp);                               /* branch  */
        break;

    case BSBW:
        Write (SP - 4, PC, L_LONG, WA);                 /* push PC on stk */
        SP = SP - 4;                                    /* decr stk ptr */
        BRANCHW (brdisp);                               /* branch */
        break;

    case BGEQ:
        if (!(cc & CC_N))                               /* br if N = 0 */
            BRANCHB (brdisp);
        break;

    case BLSS:
        if (cc & CC_N)                                  /* br if N = 1 */
            BRANCHB (brdisp);
        break;

    case BNEQ:
        if (!(cc & CC_Z))                               /* br if Z = 0 */
            BRANCHB (brdisp);
        break;

    case BEQL:
        if (cc & CC_Z)                                  /* br if Z = 1 */
            BRANCHB (brdisp);
        break;

    case BVC:
        if (!(cc & CC_V))                               /* br if V = 0 */
            BRANCHB (brdisp);
        break;

    case BVS:
        if (cc & CC_V)                                  /* br if V = 1 */
            BRANCHB (brdisp);
        break;

    case BGEQU:
        if (!(cc & CC_C))                               /* br if C = 0 */
            BRANCHB (brdisp);
        break;

    case BLSSU:
        if (cc & CC_C)                                  /* br if C = 1 */
            BRANCHB (brdisp);
        break;

    case BGTR:
        if (!(cc & (CC_N | CC_Z)))                      /* br if N | Z = 0 */
            BRANCHB (brdisp);
        break;

    case BLEQ:
        if (cc & (CC_N | CC_Z))                         /* br if N | Z = 1 */
            BRANCHB (brdisp);
        break;

    case BGTRU:
        if (!(cc & (CC_C | CC_Z)))                      /* br if C | Z = 0 */
            BRANCHB (brdisp);
        break;

    case BLEQU:
        if (cc & (CC_C | CC_Z))                         /* br if C | Z = 1 */
            BRANCHB (brdisp);
        break;

/* Simple jumps and subroutine calls - op addr.ab

        opnd[0] =       address
*/

    case JSB:
        Write (SP - 4, PC, L_LONG, WA);                 /* push PC on stk */
        SP = SP - 4;                                    /* decr stk ptr */

    case JMP:
        JUMP (op0);                                     /* jump */
        break;

    case RSB:
        temp = Read (SP, L_LONG, RA);                   /* get top of stk */
        SP = SP + 4;                                    /* incr stk ptr */
        JUMP (temp);
        break;

/* SOB instructions - op idx.ml,disp.bb

        opnd[0] =       index
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case SOBGEQ:
        r = op0 - 1;                                    /* decr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_SUB_L (r, 1, op0);                            /* test for ovflo */    
        if (r >= 0)                                     /* if >= 0, branch */
            BRANCHB (brdisp);
        break;

    case SOBGTR:
        r = op0 - 1;                                    /* decr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_SUB_L (r, 1, op0);                            /* test for ovflo */    
        if (r > 0)                                      /* if >= 0, branch */
            BRANCHB (brdisp);
        break;

/* AOB instructions - op limit.rl,idx.ml,disp.bb

        opnd[0] =       limit
        opnd[1] =       index
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case AOBLSS:
        r = op1 + 1;                                    /* incr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_ADD_L (r, 1, op1);                            /* test for ovflo */
        if (r < op0)                                    /* if < lim, branch */
            BRANCHB (brdisp);
        break;

    case AOBLEQ:
        r = op1 + 1;                                    /* incr index */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_ADD_L (r, 1, op1);                            /* test for ovflo */
        if (r <= op0)                                   /* if < lim, branch */
            BRANCHB (brdisp);
        break;

/* ACB instructions - op limit.rx,add.rx,index.mx,disp.bw

        opnd[0] =       limit
        opnd[1] =       adder
        opnd[2] =       index
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case ACBB:
        r = (op2 + op1) & BMASK;                        /* calc result */
        WRITE_B (r);                                    /* store result */
        CC_IIZP_B (r);                                  /* set cc's */
        V_ADD_B (r, op1, op2);                          /* test for ovflo */
        if ((op1 & BSIGN)? (SXTB (r) >= SXTB (op0)): (SXTB (r) <= SXTB (op0)))
            BRANCHW (brdisp);
        break;

    case ACBW:
        r = (op2 + op1) & WMASK;                        /* calc result */
        WRITE_W (r);                                    /* store result */
        CC_IIZP_W (r);                                  /* set cc's */
        V_ADD_W (r, op1, op2);                          /* test for ovflo */
        if ((op1 & WSIGN)? (SXTW (r) >= SXTW (op0)): (SXTW (r) <= SXTW (op0)))
            BRANCHW (brdisp);
        break;

    case ACBL:
        r = (op2 + op1) & LMASK;                        /* calc result */
        WRITE_L (r);                                    /* store result */
        CC_IIZP_L (r);                                  /* set cc's */
        V_ADD_L (r, op1, op2);                          /* test for ovflo */
        if ((op1 & LSIGN)? (r >= op0): (r <= op0))
            BRANCHW (brdisp);
        break;

/* CASE instructions - casex sel.rx,base.rx,lim.rx

        opnd[0] =       selector
        opnd[1] =       base
        opnd[2] =       limit
*/

    case CASEB:
        r = (op0 - op1) & BMASK;                        /* sel - base */
        CC_CMP_B (r, op2);                              /* r:limit, set cc's */
        if (r > op2)                                    /* r > limit (unsgnd)? */
            JUMP (PC + ((op2 + 1) * 2));
        else {
            temp = Read (PC + (r * 2), L_WORD, RA);
            BRANCHW (temp);
            }
        break;

    case CASEW:
        r = (op0 - op1) & WMASK;                        /* sel - base */
        CC_CMP_W (r, op2);                              /* r:limit, set cc's */
        if (r > op2)                                    /* r > limit (unsgnd)? */
            JUMP (PC + ((op2 + 1) * 2));
        else {
            temp = Read (PC + (r * 2), L_WORD, RA);
            BRANCHW (temp);
            }
        break;

    case CASEL:
        r = (op0 - op1) & LMASK;                        /* sel - base */
        CC_CMP_L (r, op2);                              /* r:limit, set cc's */
        if (((uint32) r) > ((uint32) op2))              /* r > limit (unsgnd)? */
            JUMP (PC + ((op2 + 1) * 2));
        else {
            temp = Read (PC + (r * 2), L_WORD, RA);
            BRANCHW (temp);
            }
        break;

/* Branch on bit instructions - bbxy pos.rl,op.wb,disp.bb

        opnd[0] =       position
        opnd[1] =       register number/memory flag
        opnd[2] =       memory address, if memory
*/

    case BBS:
        if (op_bb_n (opnd, acc))                        /* br if bit set */
            BRANCHB (brdisp);
        break;

    case BBC:
        if (!op_bb_n (opnd, acc))                       /* br if bit clr */
            BRANCHB (brdisp);
        break;

    case BBSS: case BBSSI:
        if (op_bb_x (opnd, 1, acc))                     /* br if set, set */
            BRANCHB (brdisp);
        break;

    case BBCC: case BBCCI:
        if (!op_bb_x (opnd, 0, acc))                    /* br if clr, clr*/
            BRANCHB (brdisp);
        break;

    case BBSC:
        if (op_bb_x (opnd, 0, acc))                     /* br if clr, set */
            BRANCHB (brdisp);
        break;

    case BBCS:
        if (!op_bb_x (opnd, 1, acc))                    /* br if set, clr */
            BRANCHB (brdisp);
        break;

    case BLBS:
        if (op0 & 1)                                    /* br if bit set */
            BRANCHB (brdisp);
        break;

    case BLBC:
        if ((op0 & 1) == 0)                             /* br if bit clear */
            BRANCHB (brdisp);
        break;

/* Extract field instructions - ext?v pos.rl,size.rb,base.wb,dst.wl

        opnd[0] =       position
        opnd[1] =       size
        opnd[2] =       register number/memory flag
        opnd[3] =       register content/memory address
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case EXTV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        if (r & byte_sign[op1])
            r = r | ~byte_mask[op1];
        WRITE_L (r);                                    /* store field */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

    case EXTZV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        WRITE_L (r);                                    /* store field */
        CC_IIZP_L (r);                                  /* set cc's */
        break;

/* Compare field instructions - cmp?v pos.rl,size.rb,base.wb,src2.rl

        opnd[0] =       position
        opnd[1] =       size
        opnd[2] =       register number/memory flag
        opnd[3] =       register content/memory address
        opnd[4] =       source2
*/

    case CMPV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        if (r & byte_sign[op1])
            r = r | ~byte_mask[op1];
        CC_CMP_L (r, op4);                              /* set cc's */
        break;

    case CMPZV:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        CC_CMP_L (r, op4);                              /* set cc's */
        break;

/* Find first field instructions - ff? pos.rl,size.rb,base.wb,dst.wl

        opnd[0] =       position
        opnd[1] =       size
        opnd[2] =       register number/memory flag
        opnd[3] =       register content/memory address
        spec    =       register/memory flag
        rn      =       register number
        va      =       memory address
*/

    case FFS:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        temp = op_ffs (r, op1);                         /* find first 1 */
        WRITE_L (op0 + temp);                           /* store result */
        cc = r? 0: CC_Z;                                /* set cc's */
        break;

    case FFC:
        r = op_extv (opnd, vfldrp1, acc);               /* get field */
        r = r ^ byte_mask[op1];                         /* invert bits */
        temp = op_ffs (r, op1);                         /* find first 1 */
        WRITE_L (op0 + temp);                           /* store result */
        cc = r? 0: CC_Z;                                /* set cc's */
        break;

/* Insert field instruction - insv src.rl,pos.rb,size.rl,base.wb

        opnd[0] =       source
        opnd[1] =       position
        opnd[2] =       size
        opnd[3] =       register number/memory flag
        opnd[4] =       register content/memory address
*/

    case INSV:
        op_insv (opnd, vfldrp1, acc);                   /* insert field */
        break;

/* Call and return - call? arg.rx,proc.ab

        opnd[0] =       argument
        opnd[1] =       procedure address
*/

    case CALLS:
        cc = op_call (opnd, TRUE, acc);
        break;

    case CALLG:
        cc = op_call (opnd, FALSE, acc);
        break;

    case RET:
        cc = op_ret (acc);
        break;

/* Miscellaneous instructions */

    case HALT:
        if (PSL & PSL_CUR)                              /* not kern? rsvd inst */
            RSVD_INST_FAULT;
        else if (cpu_unit.flags & UNIT_CONH)            /* halt to console? */
            cc = con_halt (CON_HLTINS, cc);             /* enter firmware */
        else {
            ABORT (STOP_HALT);                          /* halt to simulator */
            }

    case NOP:
        break;

    case BPT:
        SETPC (fault_PC);
        cc = intexc (SCB_BPT, cc, 0, IE_EXC);
        GET_CUR;
        break;

    case XFC:
        SETPC (fault_PC);
        cc = intexc (SCB_XFC, cc, 0, IE_EXC);
        GET_CUR;
        break;

    case BISPSW:
        if (opnd[0] & PSW_MBZ)
            RSVD_OPND_FAULT;
        PSL = PSL | (opnd[0] & ~CC_MASK);
        cc = cc | (opnd[0] & CC_MASK);
        break;

    case BICPSW:
        if (opnd[0] & PSW_MBZ)
            RSVD_OPND_FAULT;
        PSL = PSL & ~opnd[0];
        cc = cc & ~opnd[0];
        break;

    case MOVPSL:
        r = PSL | cc;
        WRITE_L (r);
        break;

    case PUSHR:
        op_pushr (opnd, acc);
        break;

    case POPR:
        op_popr (opnd, acc);
        break;

    case INDEX:
        if ((op0 < op1) || (op0 > op2))
            SET_TRAP (TRAP_SUBSCR);
        r = (op0 + op4) * op3;
        WRITE_L (r);
        CC_IIZZ_L (r);
        break;

/* Queue and interlocked queue */

    case INSQUE:
        cc = op_insque (opnd, acc);
        break;

    case REMQUE:
        cc = op_remque (opnd, acc);
        break;

    case INSQHI:
        cc = op_insqhi (opnd, acc);
        break;

    case INSQTI:
        cc = op_insqti (opnd, acc);
        break;

    case REMQHI:
        cc = op_remqhi (opnd, acc);
        break;

    case REMQTI:
        cc = op_remqti (opnd, acc);
        break;

/* String instructions */

    case MOVC3: case MOVC5:
        cc = op_movc (opnd, opc & 4, acc);
        break;

    case CMPC3: case CMPC5:
        cc = op_cmpc (opnd, opc & 4, acc);
        break;

    case LOCC: case SKPC:
        cc = op_locskp (opnd, opc & 1, acc);
        break;

    case SCANC: case SPANC:
        cc = op_scnspn (opnd, opc & 1, acc);
        break;

/* Floating point instructions */

    case TSTF: case TSTD:
        r = op_movfd (op0);
        CC_IIZZ_FP (r);
        break;

    case TSTG:
        r = op_movg (op0);
        CC_IIZZ_FP (r);
        break;

    case MOVF:
        r = op_movfd (op0);
        WRITE_L (r);
        CC_IIZP_FP (r);
        break;

    case MOVD:
        if ((r = op_movfd (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZP_FP (r);
        break;

    case MOVG:
        if ((r = op_movg (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZP_FP (r);
        break;

    case MNEGF:
        r = op_mnegfd (op0);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case MNEGD:
        if ((r = op_mnegfd (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZZ_FP (r);
        break;

    case MNEGG:
        if ((r = op_mnegg (op0)) == 0)
            op1 = 0;
        WRITE_Q (r, op1);
        CC_IIZZ_FP (r);
        break;

    case CMPF:
        cc = op_cmpfd (op0, 0, op1, 0);
        break;

    case CMPD:
        cc = op_cmpfd (op0, op1, op2, op3);
        break;

    case CMPG:
        cc = op_cmpg (op0, op1, op2, op3);
        break;

    case CVTBF:
        r = op_cvtifdg (SXTB (op0), NULL, opc);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTWF:
        r = op_cvtifdg (SXTW (op0), NULL, opc);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTLF:
        r = op_cvtifdg (op0, NULL, opc);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTBD: case CVTBG:
        r = op_cvtifdg (SXTB (op0), &rh, opc);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTWD: case CVTWG:
        r = op_cvtifdg (SXTW (op0), &rh, opc);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTLD: case CVTLG:
        r = op_cvtifdg (op0, &rh, opc);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTFB: case CVTDB: case CVTGB:
        r = op_cvtfdgi (opnd, &flg, opc) & BMASK;
        WRITE_B (r);
        CC_IIZZ_B (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTFW: case CVTDW: case CVTGW:
        r = op_cvtfdgi (opnd, &flg, opc) & WMASK;
        WRITE_W (r);
        CC_IIZZ_W (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTFL: case CVTDL: case CVTGL:
    case CVTRFL: case CVTRDL: case CVTRGL:
        r = op_cvtfdgi (opnd, &flg, opc) & LMASK;
        WRITE_L (r);
        CC_IIZZ_L (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTFD:
        r = op_movfd (op0);
        WRITE_Q (r, 0);
        CC_IIZZ_FP (r);
        break;

    case CVTDF:
        r = op_cvtdf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case CVTFG:
        r = op_cvtfg (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case CVTGF:
        r = op_cvtgf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case ADDF2: case ADDF3:
        r = op_addf (opnd, FALSE);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case ADDD2: case ADDD3:
        r = op_addd (opnd, &rh, FALSE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case ADDG2: case ADDG3:
        r = op_addg (opnd, &rh, FALSE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case SUBF2: case SUBF3:
        r = op_addf (opnd, TRUE);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case SUBD2: case SUBD3:
        r = op_addd (opnd, &rh, TRUE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case SUBG2: case SUBG3:
        r = op_addg (opnd, &rh, TRUE);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case MULF2: case MULF3:
        r = op_mulf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case MULD2: case MULD3:
        r = op_muld (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case MULG2: case MULG3:
        r = op_mulg (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case DIVF2: case DIVF3:
        r = op_divf (opnd);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        break;

    case DIVD2: case DIVD3:
        r = op_divd (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case DIVG2: case DIVG3:
        r = op_divg (opnd, &rh);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        break;

    case ACBF:
        r = op_addf (opnd + 1, FALSE);                  /* add + index */
        temp = op_cmpfd (r, 0, op0, 0);                 /* result : limit */
        WRITE_L (r);                                    /* write result */
        CC_IIZP_FP (r);                                 /* set cc's */
        if ((temp & CC_Z) || ((op1 & FPSIGN)?           /* test br cond */
           !(temp & CC_N): (temp & CC_N)))
           BRANCHW (brdisp);
        break;

    case ACBD:
        r = op_addd (opnd + 2, &rh, FALSE);
        temp = op_cmpfd (r, rh, op0, op1);
        WRITE_Q (r, rh);
        CC_IIZP_FP (r);
        if ((temp & CC_Z) || ((op2 & FPSIGN)?           /* test br cond */
           !(temp & CC_N): (temp & CC_N)))
           BRANCHW (brdisp);
        break;

    case ACBG:
        r = op_addg (opnd + 2, &rh, FALSE);
        temp = op_cmpg (r, rh, op0, op1);
        WRITE_Q (r, rh);
        CC_IIZP_FP (r);
        if ((temp & CC_Z) || ((op2 & FPSIGN)?           /* test br cond */
           !(temp & CC_N): (temp & CC_N)))
           BRANCHW (brdisp);
        break;

/* EMODF

        op0     =       multiplier
        op1     =       extension
        op2     =       multiplicand
        op3:op4 =       integer destination (int.wl)
        op5:op6 =       floating destination (flt.wl)
*/

    case EMODF:
        r = op_emodf (opnd, &temp, &flg);
        if (op5 < 0)
            Read (op6, L_LONG, WA);
        if (op3 >= 0)
            R[op3] = temp;
        else Write (op4, temp, L_LONG, WA);
        WRITE_L (r);
        CC_IIZZ_FP (r);
        if (flg) {
            V_INTOV;
            }
        break;

/* EMODD, EMODG

        op0:op1 =       multiplier
        op2     =       extension
        op3:op4 =       multiplicand
        op5:op6 =       integer destination (int.wl)
        op7:op8 =       floating destination (flt.wq)
*/

    case EMODD:
        r = op_emodd (opnd, &rh, &temp, &flg);
        if (op7 < 0) {
            Read (op8, L_BYTE, WA);
            Read ((op8 + 7) & LMASK, L_BYTE, WA);
			}
        if (op5 >= 0)
            R[op5] = temp;
        else Write (op6, temp, L_LONG, WA);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        if (flg) {
            V_INTOV;
            }
        break;

    case EMODG:
        r = op_emodg (opnd, &rh, &temp, &flg);
        if (op7 < 0) {
            Read (op8, L_BYTE, WA);
            Read ((op8 + 7) & LMASK, L_BYTE, WA);
			}
        if (op5 >= 0)
            R[op5] = temp;
        else Write (op6, temp, L_LONG, WA);
        WRITE_Q (r, rh);
        CC_IIZZ_FP (r);
        if (flg) {
            V_INTOV;
            }
        break;

/* POLY */

    case POLYF:
        op_polyf (opnd, acc);
        CC_IIZZ_FP (R[0]);
        break;

    case POLYD:
        op_polyd (opnd, acc);
        CC_IIZZ_FP (R[0]);
        break;

    case POLYG:
        op_polyg (opnd, acc);
        CC_IIZZ_FP (R[0]);
        break;

/* Operating system instructions */

    case CHMK: case CHME: case CHMS: case CHMU:
        cc = op_chm (opnd, cc, opc);                    /* CHMx */
        GET_CUR;                                        /* update cur mode */
        SET_IRQL;                                       /* update intreq */
        break;

    case REI:
        cc = op_rei (acc);                              /* REI */
        GET_CUR;                                        /* update cur mode */
        SET_IRQL;                                       /* update intreq */
        break;

    case LDPCTX:
        op_ldpctx (acc);
        break;

    case SVPCTX:
        op_svpctx (acc);
        break;

    case PROBER: case PROBEW:
        cc = (cc & CC_C) | op_probe (opnd, opc & 1);
        break;

    case MTPR:
        cc = (cc & CC_C) | op_mtpr (opnd);
        SET_IRQL;                                       /* update intreq */
        break;

    case MFPR:
        r = op_mfpr (opnd);
        WRITE_L (r);
        CC_IIZP_L (r);
        break;

/* CIS or emulated instructions */

    case CVTPL:
    case MOVP: case CMPP3: case CMPP4: case CVTLP:
    case CVTPS: case CVTSP: case CVTTP: case CVTPT:
    case ADDP4: case ADDP6: case SUBP4: case SUBP6:
    case MULP: case DIVP: case ASHP: case CRC:
    case MOVTC: case MOVTUC: case MATCHC: case EDITPC:
        cc = op_cis (opnd, cc, opc, acc);
        break;

/* Octaword or reserved instructions */

    case PUSHAO: case MOVAO: case CLRO: case MOVO:
    case TSTH: case MOVH: case MNEGH: case CMPH:
    case CVTBH: case CVTWH: case CVTLH:
    case CVTHB: case CVTHW: case CVTHL: case CVTRHL:
    case CVTFH: case CVTDH: case CVTGH:
    case CVTHF: case CVTHD: case CVTHG:
    case ADDH2: case ADDH3: case SUBH2: case SUBH3:
    case MULH2: case MULH3: case DIVH2: case DIVH3:
    case ACBH: case POLYH: case EMODH:
        cc = op_octa (opnd, cc, opc, acc, spec, va);
        if (cc & LSIGN) {                               /* ACBH branch? */
            BRANCHW (brdisp);
            cc = cc & CC_MASK;                          /* mask off flag */
            }
        break;

    default:
        RSVD_INST_FAULT;
        break;
        }                                               /* end case op */
    }                                                   /* end for */
ABORT (STOP_UNKNOWN);
}                                                       /* end sim_instr */

/* Prefetch buffer routine

   Prefetch buffer state

        ibufl, ibufh    =       the prefetch buffer
        ibcnt           =       number of bytes available (0, 4, 8)
        ppc             =       physical PC

   The get_istr routines fetches the indicated number of bytes from
   the prefetch buffer.  Although it is complicated, it is faster
   than calling Read on every byte of the instruction stream.

   If the prefetch buffer has enough bytes, the required bytes are
   extracted from the prefetch buffer and returned. If it does not
   have enough bytes, enough prefetch words are fetched until there
   are.  A longword is only prefetched if data is needed from it,
   so any translation errors are real.
*/

SIM_INLINE int32 get_istr (int32 lnt, int32 acc)
{
int32 bo = PC & 3;
int32 sc, val, t;

while ((bo + lnt) > ibcnt) {                            /* until enuf bytes */
    if ((ppc < 0) || (VA_GETOFF (ppc) == 0)) {          /* PPC inv, xpg? */
        ppc = Test ((PC + ibcnt) & ~03, RD, &t);        /* xlate PC */
        if (ppc < 0)
            Read ((PC + ibcnt) & ~03, L_LONG, RA);
        }
    if (ibcnt == 0)                                     /* fill low */
        ibufl = ReadLP (ppc);
    else ibufh = ReadLP (ppc);                          /* or high */
    ppc = ppc + 4;                                      /* incr phys PC */
    ibcnt = ibcnt + 4;                                  /* incr ibuf cnt */
    }
PC = PC + lnt;                                          /* incr PC */
if (lnt == L_BYTE)                                      /* byte? */
    val = (ibufl >> (bo << 3)) & BMASK;
else if (lnt == L_WORD) {                               /* word? */
    if (bo == 3)
        val = ((ibufl >> 24) & 0xFF) | ((ibufh & 0xFF) << 8);
    else val = (ibufl >> (bo << 3)) & WMASK;
    }
else if (bo) {                                          /* unaligned lw? */
    sc = bo << 3;
    val =  (((ibufl >> sc) & align[bo]) | (((uint32) ibufh) << (32 - sc)));
    }
else val = ibufl;                                       /* aligned lw */
if ((bo + lnt) >= 4) {                                  /* retire ibufl? */
    ibufl = ibufh;
    ibcnt = ibcnt - 4;
    }
return val;
}

/* Read octaword specifier */

int32 ReadOcta (int32 va, int32 *opnd, int32 j, int32 acc)
{
opnd[j++] = Read (va, L_LONG, acc);
opnd[j++] = Read (va + 4, L_LONG, acc);
opnd[j++] = Read (va + 8, L_LONG, acc);  
opnd[j++] = Read (va + 12, L_LONG, acc);  
return j;
}

/* Check new PSL IPL for idle start
   Checked only on exception or REI, not on MTPR #IPL,
   to allow for local locking within the idle loop */

int32 cpu_psl_ipl_idle (int32 newpsl)
{
if (((newpsl ^ PSL) & PSL_IPL) != 0) {
    sim_cancel (&cpu_unit);
    if (sim_idle_enab && ((newpsl & PSL_CUR) == 0)) {
        uint32 newipl = PSL_GETIPL (newpsl);
        if (cpu_idle_ipl_mask & (1u << newipl))
            sim_activate (&cpu_unit, cpu_idle_wait);
        }
    }
return newpsl;
}

/* Idle timer has expired with no PSL change */

t_stat cpu_idle_svc (UNIT *uptr)
{
if (sim_idle_enab)
    sim_idle (TMR_CLK, FALSE);
return SCPE_OK;
}

/* Reset */

t_stat cpu_reset (DEVICE *dptr)
{
hlt_pin = 0;
mem_err = 0;
crd_err = 0;
PSL = PSL_IS | PSL_IPL1F;
SISR = 0;
ASTLVL = 4;
mapen = 0;
if (M == NULL)
    M = (uint32 *) calloc (((uint32) MEMSIZE) >> 2, sizeof (uint32));
if (M == NULL)
    return SCPE_MEM;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return build_dib_tab ();
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
int32 st;
uint32 addr = (uint32) exta;

if (vptr == NULL) 
    return SCPE_ARG;
if (sw & SWMASK ('V')) {
    int32 acc = cpu_get_vsw (sw);
    addr = Test (addr, acc, &st);
    }
else addr = addr & PAMASK;
if (ADDR_IS_MEM (addr) || ADDR_IS_CDG (addr) ||
    ADDR_IS_ROM (addr) || ADDR_IS_NVR (addr)) {
    *vptr = (uint32) ReadB (addr);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
int32 st;
uint32 addr = (uint32) exta;

if (sw & SWMASK ('V')) {
    int32 acc = cpu_get_vsw (sw);
    addr = Test (addr, acc, &st);
    }
else addr = addr & PAMASK;
if (ADDR_IS_MEM (addr) || ADDR_IS_CDG (addr) ||
    ADDR_IS_NVR (addr)) {
    WriteB (addr, (int32) val);
    return SCPE_OK;
    }
if (ADDR_IS_ROM (addr)) {
    rom_wr_B (addr, (int32) val);
    return SCPE_OK;
    }
return SCPE_NXM;
}

/* Memory allocation */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
uint32 i, clim;
uint32 *nM = NULL;

if ((val <= 0) || (val > MAXMEMSIZE_X))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i = i + 4)
    mc = mc | M[i >> 2];
if ((mc != 0) && !get_yn ("Really truncate memory [N]?", FALSE))
    return SCPE_OK;
nM = (uint32 *) calloc (val >> 2, sizeof (uint32));
if (nM == NULL)
    return SCPE_MEM;
clim = (uint32) ((((uint32) val) < MEMSIZE)? val: MEMSIZE);
for (i = 0; i < clim; i = i + 4)
    nM[i >> 2] = M[i >> 2];
free (M);
M = nM;
MEMSIZE = val; 
return SCPE_OK;
}

/* Virtual address translation */

t_stat cpu_show_virt (FILE *of, UNIT *uptr, int32 val, void *desc)
{
t_stat r;
char *cptr = (char *) desc;
uint32 va, pa;
int32 st;
static const char *mm_str[] = {
    "Access control violation",
    "Length violation",
    "Process PTE access control violation",
    "Process PTE length violation",
    "Translation not valid",
    "Internal error",
    "Process PTE translation not valid"
    };

if (cptr) {
    va = (uint32) get_uint (cptr, 16, 0xFFFFFFFF, &r);
    if (r == SCPE_OK) {
        int32 acc = cpu_get_vsw (sim_switches);
        pa = Test (va, acc, &st);
        if (st == PR_OK)
            fprintf (of, "Virtual %-X = physical %-X\n", va, pa);
        else fprintf (of, "Virtual %-X: %s\n", va, mm_str[st]);
        return SCPE_OK;
        }
    }
fprintf (of, "Invalid argument\n");
return SCPE_OK;
}

/* Get access mode for examine, deposit, show virtual */

int32 cpu_get_vsw (int32 sw)
{
int32 md;

set_map_reg ();                                         /* update dyn reg */
if (sw & SWMASK ('K'))
    md = KERN;
else if (sw & SWMASK ('E')) 
    md = EXEC;
else if (sw & SWMASK ('S'))
    md = SUPV;
else if (sw & SWMASK ('U'))
    md = USER;
else md = PSL_GETCUR (PSL);
return ACC_MASK (md);
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].iPC = 0;
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

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 i, k, di, lnt, numspec;
char *cptr = (char *) desc;
t_stat r;
InstHistory *h;
extern const char *opcode[];
extern t_value *sim_eval;
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
fprintf (st, "PC       PSL       IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(di++) % hst_lnt];                         /* entry pointer */
    if (h->iPC == 0)                                    /* filled in? */
        continue;
    fprintf(st, "%08X %08X| ", h->iPC, h->PSL);         /* PC, PSL */
    numspec = drom[h->opc][0] & DR_NSPMASK;             /* #specifiers */
    if (opcode[h->opc] == NULL)                         /* undefined? */
        fprintf (st, "%03X (undefined)", h->opc);
    else if (h->PSL & PSL_FPD)                          /* FPD set? */
        fprintf (st, "%s FPD set", opcode[h->opc]);
    else {                                              /* normal */
        for (i = 0; i < INST_SIZE; i++)
            sim_eval[i] = h->inst[i];
        if ((fprint_sym (st, h->iPC, sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "%03X (undefined)", h->opc);
        if ((numspec > 1) ||
            ((numspec == 1) && (drom[h->opc][1] < BB))) {
            if (cpu_show_opnd (st, h, 0)) {             /* operands; more? */
                if (cpu_show_opnd (st, h, 1)) {         /* 2nd line; more? */
                    cpu_show_opnd (st, h, 2);           /* octa, 3rd/4th */
                    cpu_show_opnd (st, h, 3);
                    }
                }
            }
        }                                               /* end else */
    fputc ('\n', st);                                   /* end line */
    }                                                   /* end for */
return SCPE_OK;
}

t_bool cpu_show_opnd (FILE *st, InstHistory *h, int32 line)
{

int32 numspec, i, j, disp;
t_bool more;

numspec = drom[h->opc][0] & DR_NSPMASK;                 /* #specifiers */
fputs ("\n                  ", st);                     /* space */
for (i = 1, j = 0, more = FALSE; i <= numspec; i++) {   /* loop thru specs */
    disp = drom[h->opc][i];                             /* specifier type */
    if (disp == RG)                                     /* fix specials */
        disp = RQ;
    else if (disp >= BB)
        break;                         /* ignore branches */
    else switch (disp & (DR_LNMASK|DR_ACMASK)) {

    case RB: case RW: case RL:                          /* read */
    case AB: case AW: case AL: case AQ: case AO:        /* address */
    case MB: case MW: case ML:                          /* modify */
        if (line == 0)
            fprintf (st, " %08X", h->opnd[j]);
        else fputs ("         ", st);
        j = j + 1;
        break;
    case RQ: case MQ:                                   /* read, modify quad */
        if (line <= 1)
            fprintf (st, " %08X", h->opnd[j + line]);
        else fputs ("         ", st);
        if (line == 0)
            more = TRUE;
        j = j + 2;
        break;
    case RO: case MO:                                   /* read, modify octa */
        fprintf (st, " %08X", h->opnd[j + line]);
        more = TRUE;
        j = j + 4;
        break;
    case WB: case WW: case WL: case WQ: case WO:        /* write */
        if (line == 0)
            fprintf (st, " %08X", h->opnd[j + 1]);
        else fputs ("         ", st);
        j = j + 2;
        break;
        }                                       /* end case */
    }                                           /* end for */
return more;
}

struct os_idle {
    char        *name;
    uint32      mask;
    };

static struct os_idle os_tab[] = {
    { "VMS", 0x8 },
    { "NETBSD", 0x2 },
    { "ULTRIX", 0x2 },
    { "OPENBSD", 0x1 },
    { "32V", 0x1 },
    { NULL, 0 }
    };

/* Set and show idle */

t_stat cpu_set_idle (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 i;

if (cptr != NULL) {
    for (i = 0; os_tab[i].name != NULL; i++) {
        if (strcmp (os_tab[i].name, cptr) == 0) {
            cpu_idle_type = i + 1;
            cpu_idle_ipl_mask = os_tab[i].mask;
            return sim_set_idle (uptr, val, cptr, desc);
            }
        }
    return SCPE_ARG;
    }
return sim_set_idle (uptr, val, cptr, desc);
}

t_stat cpu_show_idle (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (sim_idle_enab && (cpu_idle_type != 0))
    fprintf (st, "idle enabled=%s", os_tab[cpu_idle_type - 1].name);
else fprintf (st, "idle disabled");
return SCPE_OK;
}
