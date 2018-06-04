/* sigma_cpu.c: XDS Sigma CPU simulator

   Copyright (c) 2007-2008, Robert M Supnik

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

   cpu          central processor

   The system state for the Sigma CPU is as follows:

   RF[0:15][0:31]<0:31> register blocks
   PSW1<0:31>           processor status word 1
     CC<0:3>              condition codes
     PC<0:17>             program counter (called IA in Sigma documentation)
   PSW2<0:31>           processor status word 2
     PSW2_WLK<0:3>        write key (2b on S5-9)
   PSW4<0:31>           processor status word 4 (5X0 only)
   MAP[0:511]<0:10>     memory map (8b on S5-8)
   WLK[0:2047]<0:3>     write locks (256 2b entries on S5-9)
   SSW<0:3>             sense switches
   PDF                  processor detected fault flag (S8-9, 5X0 only)

   Notes on features not documented in the Reference Manuals:

   1. Memory mapping was available for the Sigma 5 (see map diagnostic).
   2. The Sigma 6/7 were field retrofitted with the LAS/LMS instructions
      (see auto diagnostic).
   3. The Sigma 8/9 returned different results for WD .45 (see Telefile
      System exerciser).
   4. Expanded memory beyond 128KB was retrofitted to the Sigma 5/6/7,
      creating the so-called "Big 5/6/7." As a minimum, these systems
      also included the "mode altered" feature and the 11b relocation map.

   The Sigma CPU has two instruction formats, memory reference and immediate.
   The memory reference format is:

    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |I|             |       |     |                                 |
   |N|   opcode    |   R   |  X  |             address             | memory
   |D|             |       |     |                                 | reference
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   where

   IND      =   indirect flag
   opcode   =   operation code
   R        =   source/destination register
   X        =   index register (0 if none)
   address  =   operand address

   The immediate format is:

    3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
    1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   | |             |       |                                       |
   |0|   opcode    |   R   |               immediate               | immediate
   | |             |       |                                       |
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   This routine is the instruction decode routine for the Sigma CPU.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        invalid instruction and stop_op flag set
        I/O error in I/O simulator
        EXU loop exceeding limit
        illegal interrupt or trap instruction
        illegal register pointer
        illegal vector

   2. Interrupts.  The interrupt structure consists of the following:
      Each interrupt is part of a group that determines its priority.
      The interrupt group is either controlled by a PSW inhibit or is
      unconditional.  Interrupts can be armed or disarmed (which controls
      whether they are recognized at all) and enabled or disabled (which
      controls whether they occur).  See the sigma_io.c module for details.

   3. Channels.  The Sigma system has a channel-based I/O structure.  Each
      channel is represented by a set of registers.  Channels test the
      I/O transfer requests from devices.

   4. Non-existent memory.  On the Sigma, accesses to non-existent memory
      trap.

   5. Adding I/O devices.  These modules must be modified:

        sigma_defs.h    add definitions
        sigma_io.c      add dispatches
        sigma_sys.c     add pointer to data structures to sim_devices
*/

#include "sigma_io_defs.h"

#define CPUF_V_MODEL    (UNIT_V_UF + 6)                 /* CPU model */
#define CPUF_M_MODEL    0x7
#define CPUF_MODEL      (CPUF_M_MODEL << CPUF_V_MODEL)
#define CPUF_S5         (CPU_V_S5 << CPUF_V_MODEL)
#define CPUF_S6         (CPU_V_S6 << CPUF_V_MODEL)
#define CPUF_S7         (CPU_V_S7 << CPUF_V_MODEL)
#define CPUF_S8         (CPU_V_S8 << CPUF_V_MODEL)
#define CPUF_S7B        (CPU_V_S7B << CPUF_V_MODEL)
#define CPUF_S9         (CPU_V_S9 << CPUF_V_MODEL)
#define CPUF_550        (CPU_V_550 << CPUF_V_MODEL)
#define CPUF_560        (CPU_V_560 << CPUF_V_MODEL)
#define CPUF_GETMOD(x)  (((x) >> CPUF_V_MODEL) & CPUF_M_MODEL)

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = real_pc;

#define HIST_MIN        64
#define HIST_MAX        (1u << 20)
#define H_INST          0x00800000
#define H_CHAN          0x00400000
#define H_ITRP          0x00200000
#define H_ABRT          0x00100000

typedef struct {
    uint32              typ_cc_pc;
    uint32              ir;
    uint32              rn;
    uint32              rn1;
    uint32              x;                              /* unused */
    uint32              ea;
    uint32              op;
    uint32              op1;
    } InstHistory;

uint32 cpu_model = CPU_V_S7;                            /* CPU model */
uint32 *M;                                              /* memory */
uint32 rf[RF_NBLK * RF_NUM] = { 0 };                    /* register files */
uint32 *R = rf;                                         /* cur reg file */
uint32 PSW1 = PSW1_DFLT;                                /* PSD */
uint32 PSW2 = PSW2_DFLT;
uint32 PSW4 = 0;                                        /* 5X0 only */
uint32 CC;
uint32 PC;
uint32 PSW2_WLK = 0;                                    /* write lock key */
uint32 PSW_QRX9;                                        /* Sigma 9 real extended */
uint32 bvamqrx = BVAMASK;                               /* BVA mask, 17b/20b */
uint32 SSW = 0;                                         /* sense switches */
uint32 cpu_pdf = 0;                                     /* proc detected fault */
uint32 cons_alarm = 0;                                  /* console alarm */
uint32 cons_alarm_enb = 0;                              /* alarm enable */
uint32 cons_pcf = 0;
uint32 rf_bmax = 4;                                     /* num reg blocks */
uint32 exu_lim = 32;                                    /* nested EXU limit */
uint32 stop_op = 0;                                     /* stop on ill op */
uint32 cpu_astop = 0;                                   /* address stop */
uint32 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* inst history */

extern uint32 int_hiact;                                /* highest act int */
extern uint32 int_hireq;                                /* highest int req */

t_stat cpu_svc (UNIT *uptr);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_clr_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_rblks (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_rblks (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_set_alarm (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_alarm (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat cpu_show_addr (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
void set_rf_display (uint32 *rfbase);
void inst_hist (uint32 ir, uint32 pc, uint32 typ);
uint32 cpu_one_inst (uint32 real_pc, uint32 IR);
uint32 ImmOp (uint32 ir, uint32 *imm);
uint32 EaP20 (uint32 IR, uint32 *bva, uint32 lnt);
uint32 EaSh (uint32 ir, uint32 *stype, uint32 *sc);
uint32 Add32 (uint32 s1, uint32 s2, uint32 cin);
uint32 SMul64 (uint32 a, uint32 b, uint32 *lo);
t_bool SDiv64 (uint32 dvdh, uint32 dvdl, uint32 dvr, uint32 *res, uint32 *rem);
uint32 Cmp32 (uint32 a, uint32 b);
uint32 Shift (uint32 rn, uint32 stype, uint32 sc);
uint32 TestSP1 (uint32 sp1, int32 mod);
uint32 ModWrSP (uint32 bva, uint32 sp, uint32 sp1, int32 mod);
uint32 cpu_int_mtx (uint32 vec, uint32 *cc);
uint32 cpu_trap_or_int (uint32 vec);
uint32 cpu_xpsd (uint32 ir, uint32 bva, uint32 ra);
uint32 cpu_pss (uint32 ir, uint32 bva, uint32 acc);
uint32 cpu_pls (uint32 IR);
void cpu_assemble_PSD (void);
uint32 cpu_new_PSD (uint32 lrp, uint32 p1, uint32 p2);
uint32 cpu_new_RP (uint32 rp);
uint32 cpu_new_PC (uint32 bva);
uint32 cpu_add_PC (uint32 pc, uint32 val);
t_stat cpu_bad_rblk (UNIT *uptr);
void cpu_fprint_one_inst (FILE *st, uint32 tcp, uint32 ir, uint32 rn,
    uint32 rn1, uint32 ea, uint32 op, uint32 op1);

extern uint32 fp (uint32 op, uint32 rn, uint32 bva);
extern uint32 cis_dec (uint32 op, uint32 rn, uint32 bva);
extern uint32 cis_ebs (uint32 rn, uint32 disp);
extern void ShiftF (uint32 rn, uint32 stype, uint32 sc);
extern uint32 map_mmc (uint32 rn, uint32 map);
extern uint32 map_lra (uint32 rn, uint32 inst);
extern uint32 map_las (uint32 rn, uint32 bva);
extern uint32 map_lms (uint32 rn, uint32 bva);
extern t_stat io_init (void);
extern uint32 io_eval_int (void);
extern uint32 io_actv_int (void);
extern t_bool io_poss_int (void);
extern uint32 io_ackn_int (uint32 hireq);
extern uint32 io_rels_int (uint32 hiact, t_bool arm);
extern uint32 io_rwd (uint32 op, uint32 rn, uint32 bva);
extern uint32 io_sio (uint32 rn, uint32 bva);
extern uint32 io_tio (uint32 rn, uint32 bva);
extern uint32 io_tdv (uint32 rn, uint32 bva);
extern uint32 io_hio (uint32 rn, uint32 bva);
extern uint32 io_aio (uint32 rn, uint32 bva);
extern uint32 int_reset (DEVICE *dev);
extern void io_set_eimax (uint32 lnt);
extern void io_sclr_req (uint32 inum, uint32 val);
extern void io_sclr_arm (uint32 inum, uint32 val);
extern t_stat io_set_nchan (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat io_show_nchan (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = {
    UDATA (&cpu_svc, UNIT_FIX+CPUF_S7+CPUF_ALLOPT+UNIT_BINK, MAXMEMSIZE),
    };

UNIT cpu_rblk_unit = {
    UDATA (&cpu_bad_rblk, UNIT_DIS, 0)
    };

REG cpu_reg[] = {
    { GRDATA (PC, PSW1, 16, VASIZE, PSW1_V_PC) },
    { HRDATA (R0, rf[0], 32) },                         /* addr in memory */
    { HRDATA (R1, rf[1], 32) },                         /* modified at exit */
    { HRDATA (R2, rf[2], 32) },                         /* to SCP */
    { HRDATA (R3, rf[3], 32) },
    { HRDATA (R4, rf[4], 32) },
    { HRDATA (R5, rf[5], 32) },
    { HRDATA (R6, rf[6], 32) },
    { HRDATA (R7, rf[7], 32) },
    { HRDATA (R8, rf[8], 32) },
    { HRDATA (R9, rf[9], 32) },
    { HRDATA (R10, rf[10], 32) },
    { HRDATA (R11, rf[11], 32) },
    { HRDATA (R12, rf[12], 32) },
    { HRDATA (R13, rf[13], 32) },
    { HRDATA (R14, rf[14], 32) },
    { HRDATA (R15, rf[15], 32) },
    { HRDATA (PSW1, PSW1, 32) },
    { HRDATA (PSW2, PSW2, 32) },
    { HRDATA (PSW4, PSW4, 32) },
    { GRDATA (CC, PSW1, 16, 4, PSW1_V_CC) },
    { GRDATA (RP, PSW2, 16, 4, PSW2_V_RP) },
    { FLDATA (SSW1, SSW, 3) },
    { FLDATA (SSW2, SSW, 2) },
    { FLDATA (SSW3, SSW, 1) },
    { FLDATA (SSW4, SSW, 0) },
    { FLDATA (PDF, cpu_pdf, 0) },
    { FLDATA (ALARM, cons_alarm, 0) },
    { FLDATA (ALENB, cons_alarm_enb, 0), REG_HRO },
    { FLDATA (PCF, cons_pcf, 0) },
    { DRDATA (EXULIM, exu_lim, 8), PV_LEFT + REG_NZ },
    { FLDATA (STOP_ILL, stop_op, 0) },
    { BRDATA (REG, rf, 16, 32, RF_NUM * RF_NBLK) },
    { DRDATA (RBLKS, rf_bmax, 5), REG_HRO },
    { BRDATA (PCQ, pcq, 16, VASIZE, PCQ_SIZE), REG_RO+REG_CIRC },
    { DRDATA (PCQP, pcq_p, 6), REG_HRO },
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { CPUF_MODEL, CPUF_S5, "Sigma 5", "SIGMA5", &cpu_set_type },
    { CPUF_MODEL, CPUF_S6, "Sigma 6", "SIGMA6", &cpu_set_type },
    { CPUF_MODEL, CPUF_S7, "Sigma 7", "SIGMA7", &cpu_set_type },
    { CPUF_MODEL, CPUF_S7B, "Sigma 7 BigMem", "SIGMA7B", & cpu_set_type },
//    { CPUF_MODEL, CPUF_S8, "Sigma 8", "SIGMA8", &cpu_set_type },
//    { CPUF_MODEL, CPUF_S9, "Sigma 9", "SIGMA9", &cpu_set_type },
//    { CPUF_MODEL, CPUF_550, "550", "550", &cpu_set_type },
//    { CPUF_MODEL, CPUF_560, "560", "560", &cpu_set_type },
    { MTAB_XTD|MTAB_VDV, 0, "register blocks", "RBLKS",
      &cpu_set_rblks, &cpu_show_rblks },
    { MTAB_XTD|MTAB_VDV, 0, "channels", "CHANNELS",
      &io_set_nchan, &io_show_nchan },
    { CPUF_FP, CPUF_FP, "floating point", "FP", &cpu_set_opt },
    { CPUF_FP, 0,       "no floating point", NULL },
    { MTAB_XTD|MTAB_VDV, CPUF_FP, NULL, "NOFP", &cpu_clr_opt },
    { CPUF_DEC, CPUF_DEC, "decimal", "DECIMAL", &cpu_set_opt },
    { CPUF_DEC, 0,        "no decimal", NULL },
    { MTAB_XTD|MTAB_VDV, CPUF_DEC, NULL, "NODECIMAL", &cpu_clr_opt },
    { CPUF_LAMS, CPUF_LAMS, "LAS/LMS", "LASLMS", &cpu_set_opt },
    { CPUF_LAMS, 0,       "no LAS/LMS", NULL },
    { MTAB_XTD|MTAB_VDV, CPUF_LAMS, NULL, "NOLASLMS", &cpu_clr_opt },
    { CPUF_MAP, CPUF_MAP, "map", "MAP", &cpu_set_opt },
    { CPUF_MAP, 0,        "no map", NULL },
    { MTAB_XTD|MTAB_VDV, CPUF_MAP, NULL, "NOMAP", &cpu_clr_opt },
    { CPUF_WLK, CPUF_WLK, "write lock", "WRITELOCK", &cpu_set_opt },
    { CPUF_WLK, 0,        "no write lock", NULL },
    { MTAB_XTD|MTAB_VDV, CPUF_WLK, NULL, "NOWRITELOCK", &cpu_clr_opt },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "ALARM", "ALON",  &cpu_set_alarm, &cpu_show_alarm },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "ALOFF", & cpu_set_alarm },
    { CPUF_MSIZE, (1u << 15), NULL, "32K", &cpu_set_size },
    { CPUF_MSIZE, (1u << 16), NULL, "64K", &cpu_set_size },
    { CPUF_MSIZE, (1u << 17), NULL, "128K", &cpu_set_size },
    { CPUF_MSIZE, (1u << 18), NULL, "256K", &cpu_set_size },
    { CPUF_MSIZE, (1u << 19), NULL, "512K", &cpu_set_size },
    { CPUF_MSIZE, (1u << 20), NULL, "1M", &cpu_set_size },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, BY, "BA", NULL,
      NULL, &cpu_show_addr },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, HW, "HA", NULL,
      NULL, &cpu_show_addr },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, WD, "WA", NULL,
      NULL, &cpu_show_addr },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, DW, "DA", NULL,
      NULL, &cpu_show_addr },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 20, 1, 16, 32,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    };

static uint8 anlz_tab[128] = {
    0x9, 0x9, 0x9, 0x9, 0x8, 0x8, 0x8, 0x8,             /* 00 - 0F */
    0xC, 0xC, 0xC, 0xC, 0xC, 0xC, 0xC, 0xC,
    0xC, 0xC, 0xC, 0xC, 0xC, 0xC, 0xC, 0xC,             /* 10 - 1F */
    0xC, 0xC, 0xC, 0xC, 0xC, 0xC, 0xC, 0xC,
    0x9, 0x9, 0x9, 0x9, 0x8, 0x8, 0x8, 0x8,             /* 20 - 2F */
    0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
    0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,             /* 30 - 3F */
    0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
    0x1, 0x1, 0x1, 0x1, 0x8, 0x8, 0x8, 0x8,             /* 40 - 4F */
    0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
    0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4,             /* 50 - 5F */
    0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4,
    0x1, 0x1, 0x1, 0x1, 0x8, 0x8, 0x8, 0x8,             /* 60 - 6F */
    0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,             /* 70 - 7F */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
    };

cpu_var_t cpu_tab[] = {

/*    psw1_mbz      psw2_mbz    m_map1  pamask      eint    chan
      cc            standard                        optional */
    { 0x080E0000,   0xC8FFFE0F, 0x0FC,  PAMASK17,   14,     8,  /* S5 */
      CC1|CC2,      0,                              CPUF_MAP|CPUF_WLK|CPUF_FP },
    { 0x080E0000,   0xC8FFFE0F, 0x0FC,  PAMASK17,   14,     8,  /* S6 */
      CC1|CC2,      CPUF_STR|CPUF_MAP|CPUF_WLK|CPUF_DEC,    CPUF_FP|CPUF_LAMS },
    { 0x080E0000,   0xC8FFFE0F, 0x0FC,  PAMASK17,   14,     8,  /* S7 */
      CC1|CC2,      CPUF_STR|CPUF_MAP|CPUF_WLK,     CPUF_FP|CPUF_DEC|CPUF_LAMS },
    { 0x080E0000,   0xC8FFFE0F, 0x0FC,  PAMASK20,   14,     8,  /* S7B */
      CC1|CC2,      CPUF_STR|CPUF_MAP|CPUF_WLK,     CPUF_FP|CPUF_DEC|CPUF_LAMS },
    { 0x084E0000,   0xC8FF00C7, 0x0FC,  PAMASK17,   14,     8,  /* S8 */
      CC1|CC2|CC3,  CPUF_STR|CPUF_FP|CPUF_WLK|CPUF_LAMS,     0 },
    { 0x08060000,   0xC8400007, 0x0FC,  PAMASK22,   14,     8,  /* S9 */
      CC1|CC2|CC3,  CPUF_STR|CPUF_MAP|CPUF_WLK|CPUF_DEC|CPUF_FP|CPUF_LAMS,    0 },
    { 0x002E0000,   0x080FFFC3, 0x7FE,  PAMASK20,   4,      4,  /* 550 */
      CC1|CC2|CC3|CC4,  CPUF_MAP|CPUF_WLK|CPUF_LAMS,         CPUF_FP },
    { 0x000E0000,   0x080FFFC3, 0x7FE,  PAMASK20,   4,      4,  /* 560 */
      CC1|CC2|CC3|CC4,  CPUF_STR|CPUF_MAP|CPUF_WLK|CPUF_DEC|CPUF_FP|CPUF_LAMS,    0 }
    };

/* Simulation loop */

t_stat sim_instr (void)
{
uint32 ir, rpc, old_PC;
t_stat reason, tr, tr2;

/* Restore register state */

if (io_init ())                                         /* init IO; conflict? */
    return STOP_INVIOC;
reason = 0;
if (cpu_new_PSD (1, PSW1, PSW2))                        /* restore PSD, RP etc */
    return STOP_INVPSD;
int_hireq = io_eval_int ();

/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until stop */

    PSW2 &= ~PSW2_RA;                                   /* clr reg altered */
    if (cpu_astop) {                                    /* debug stop? */
        cpu_astop = 0;
        return STOP_ASTOP;
        }

    if (sim_interval <= 0) {                            /* event queue? */
        if ((reason = sim_process_event ()))            /* process */
            break;
        int_hireq = io_eval_int ();                     /* re-evaluate intr */
        }
    sim_interval = sim_interval - 1;                    /* count down */

    if (int_hireq < NO_INT) {                           /* interrupt req? */
        uint32 sav_hi, vec, wd, op;
        
        sav_hi = int_hireq;                             /* save level */
        vec = io_ackn_int (int_hireq);                  /* get vector */
        if (vec == 0) {                                 /* illegal vector? */
            reason = STOP_ILLVEC;                       /* something wrong */
            break;
            }
        ReadPW (vec, &wd);                              /* read vector */
        op = I_GETOP (wd);                              /* get opcode */
        if ((op == OP_MTB) || (op == OP_MTH) || (op == OP_MTW)) {
            uint32 res;
            tr2 = cpu_int_mtx (vec, &res);              /* do single cycle */
            io_sclr_req (sav_hi, 0);                    /* clear request */
            io_sclr_arm (sav_hi, 1);                    /* set armed */
            if ((res == 0) &&                           /* count overflow */
                ((vec >= VEC_C1P) && (vec <= VEC_C4P))) /* on clock? */
                io_sclr_req (INTV (INTG_CTR, vec - VEC_C1P), 1);
            int_hiact = io_actv_int ();                 /* re-eval active */
            int_hireq = io_eval_int ();                 /* re-eval intr */
            }
        else tr2 = cpu_trap_or_int (vec);               /* XPSD/PSS intr */
        if (tr2 & TR_FL) {                              /* trap? */
            if (QCPU_S89_5X0)                           /* S89 or 5X0? */
                tr2 = cpu_trap_or_int (tr2);            /* try again */
            reason = (tr2 == TR_INVTRP)? STOP_ILLTRP: STOP_TRPT;
            }
        else reason = tr2;                              /* normal status code */
        }
    else {                                              /* normal instruction */
        if (sim_brk_summ &&
            sim_brk_test (PC, SWMASK ('E'))) {          /* breakpoint? */
            reason = STOP_IBKPT;                        /* stop simulation */
            break;
            }
        if (PSW_QRX9 && (PC & PSW1_XA))                 /* S9 real ext && ext? */
            rpc = (PSW2 & PSW2_EA) | (PC & ~PSW1_XA);   /* 22b phys address */
        else rpc = PC;                                  /* standard 17b PC */
        old_PC = PC;                                    /* save PC */
        PC = cpu_add_PC (PC, 1);                        /* increment PC */
        if (((tr = ReadW (rpc << 2, &ir, VI)) != 0) ||  /* fetch inst, err? */
            ((tr = cpu_one_inst (rpc, ir)) != 0)) {     /* exec inst, error? */
            if (tr & TR_FL) {                           /* trap? */
                PC = old_PC;                            /* roll back PC */
                tr2 = cpu_trap_or_int (tr);             /* do trap */
                if (tr2 & TR_FL) {                      /* trap? */
                    if (QCPU_S89_5X0)                   /* S89 or 5X0? */
                        tr2 = cpu_trap_or_int (tr2);    /* try again */
                    reason = (tr2 == TR_INVTRP)? STOP_ILLTRP: STOP_TRPT;
                    }                                   /* end if trap-in-trap */
                else reason = tr2;                      /* normal status */
                }                                       /* end if trap */
            else reason = tr;                           /* normal status */
            if ((reason >= STOP_ROLLBACK) &&            /* roll back PC? */
                (reason <= STOP_MAX))
                PC = old_PC;
            }                                           /* end if abnormal status */
        }                                               /* end else normal */
    }                                                   /* end while */

/* Simulation halted */

pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
cpu_assemble_PSD ();                                    /* visible PSD */
set_rf_display (R);                                     /* visible registers */
return reason;
}

/* Execute one instruction */

uint32 cpu_one_inst (uint32 real_pc, uint32 IR)
{
uint32 op, rn, bva, opnd, opnd1, opnd2, t;
uint32 res, res1, tr, stype, sc, cnt;
uint32 sa, da, mask, c, c1, i, lim, aop, exu_cnt;
int32 sop, sop1;
t_bool mprot;

exu_cnt = 0;                                            /* init EXU count */
EXU_LOOP:
if (hst_lnt)                                            /* history? record */
    inst_hist (IR, real_pc, H_INST);
op = I_GETOP (IR);                                      /* get opcode */
rn = I_GETRN (IR);                                      /* get reg num */
switch (op) {

/* Loads and stores */

    case OP_LI:                                         /* load immediate */
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sext to 32b */
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        break;

    case OP_LB:                                         /* load byte */
        if ((tr = Ea (IR, &bva, VR, BY)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadB (bva, &opnd, VR)) != 0)         /* read byte */
            return tr;
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        break;

    case OP_LH:                                         /* load halfword */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        opnd = SEXT_H_W (opnd) & WMASK;                 /* sext to 32b */
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        break;

    case OP_LCH:                                        /* load comp hw */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        opnd = SEXT_H_W (opnd);                         /* sext to 32b */
        opnd = NEG_W (opnd);                            /* negate */
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        break;

    case OP_LAH:                                        /* load abs hw */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        if (opnd & HSIGN)                               /* negative? */
            opnd = NEG_W (opnd) & HMASK;                /* negate */
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        break;

    case OP_LW:                                         /* load word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        break;

    case OP_LCW:                                        /* load comp word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        opnd = NEG_W (opnd);                            /* negate */
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        if (opnd == WSIGN) {                            /* overflow? */
            CC |= CC2;                                  /* set CC2 */
            if (PSW1 & PSW1_AM)                         /* trap if enabled */
                return TR_FIX;
            }
        else CC &= ~CC2;                                /* no, clear CC2 */
        break;

    case OP_LAW:                                        /* load abs word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        if (opnd & WSIGN)                               /* negative? */
            opnd = NEG_W (opnd);                        /* take abs value */
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        if (opnd == WSIGN) {                            /* overflow? */
            CC |= CC2;                                  /* set CC2 */
            if (PSW1 & PSW1_AM)                         /* trap if enabled */
                return TR_FIX;
            }
        else CC &= ~CC2;                                /* no, clear CC2 */
        break;

    case OP_LS:                                         /* load selective */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        res = (R[rn] & ~R[rn|1]) | (opnd & R[rn|1]);    /* load under mask */ 
        CC34_W (res);                                   /* set cc's */
        R[rn] = res;                                    /* store result */
        break;

    case OP_LAS:                                        /* load and set */
        if ((cpu_unit.flags & CPUF_LAMS) == 0)          /* not included? */
            return TR_NXI;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = map_las (rn, bva)) != 0)              /* do instruction */
            return tr;
        CC34_W (R[rn]);                                 /* set CC's */
        break;

    case OP_LVAW:                                       /* load virt addr */
        if (!QCPU_5X0)                                  /* 5X0 only */
            return TR_NXI;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        R[rn] = bva >> 2;                               /* store */
        break;

    case OP_LD:                                         /* load doubleword */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* read doubleword */
            return tr;
        if ((opnd == 0) && (opnd1 != 0))                /* 0'non-zero? */
            CC = (CC & ~CC4) | CC3;                     /* set CC3 */
        else CC34_W (opnd);                             /* else hi sets CC */
        R[rn|1] = opnd1;                                /* store result */
        R[rn] = opnd;
        break;

    case OP_LCD:                                        /* load comp double */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* read doubleword */
            return tr;
        NEG_D (opnd, opnd1);                            /* negate */
        if ((opnd == 0) && (opnd1 != 0))                /* 0'non-zero? */
            CC = (CC & ~CC4) | CC3;                     /* set CC3 */
        else CC34_W (opnd);                             /* else hi sets CC */
        R[rn|1] = opnd1;                                /* store result */
        R[rn] = opnd;
        if ((opnd == WSIGN) && (opnd1 == 0)) {          /* overflow? */
            CC |= CC2;                                  /* set CC2 */
            if (PSW1 & PSW1_AM)                         /* trap if enabled */
                return TR_FIX;
            }
        else CC &= ~CC2;                                /* no, clear CC2 */
        break;

    case OP_LAD:                                        /* load abs double */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* read doubleword */
            return tr;
        if (opnd & WSIGN)                               /* negative? */
            NEG_D (opnd, opnd1);                        /* take abs value */
        if ((opnd == 0) && (opnd1 != 0))                /* 0'non-zero? */
            CC = (CC & ~CC4) | CC3;                     /* set CC3 */
        else CC34_W (opnd);                             /* else hi sets CC */
        R[rn|1] = opnd1;                                /* store result */
        R[rn] = opnd;
        if ((opnd == WSIGN) && (opnd1 == 0)) {          /* overflow? */
            CC |= CC2;                                  /* set CC2 */
            if (PSW1 & PSW1_AM)                         /* trap if enabled */
                return TR_FIX;
            }
        else CC &= ~CC2;                                /* no, clear CC2 */
        break;

/* Note: the Sigma 7 does not prove the instruction can execute successfully
   before starting to load registers; the Sigma 9 (and the simulator) do. */

    case OP_LM:                                         /* load multiple */
        lim = CC? CC: 16;                               /* CC sets count */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW ((bva + ((lim - 1) << 2)) & bvamqrx, &opnd, VR)) != 0)
            return tr;                                  /* test readability */
        for (i = 0; i < lim; i++) {                     /* loop thru reg */
            if ((tr = ReadW (bva, &opnd, VR)) != 0)     /* read next */
                return tr;
            R[rn] = opnd;                               /* store in reg */
            bva = (bva + 4) & bvamqrx;                  /* next word */
            rn = (rn + 1) & RNMASK;                     /* next reg */
            PSW2 |= PSW2_RA;                            /* reg altered */
            }
        break;

    case OP_LCFI:                                       /* load cc, flt immed */
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        if (IR & IRB(10))                               /* load CC's? */
            CC = (opnd >> 4) & 0xF;
        if (IR & IRB(11))                               /* load FP ctrls? */
            PSW1 = ((PSW1 & ~PSW1_FPC) |                /* set ctrls */
                ((opnd & PSW1_M_FPC) << PSW1_V_FPC)) &
                ~cpu_tab[cpu_model].psw1_mbz;           /* clear mbz */
        break;

    case OP_LCF:                                        /* load cc, flt */
        if ((tr = Ea (IR, &bva, VR, BY)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadB (bva, &opnd, VR)) != 0)         /* get byte */
            return tr;
        if (IR & IRB(10))                               /* load CC's? */
            CC = (opnd >> 4) & 0xF;
        if (IR & IRB(11))                               /* load FP ctrls? */
            PSW1 = ((PSW1 & ~PSW1_FPC) |                /* set ctrls */
                ((opnd & PSW1_M_FPC) << PSW1_V_FPC)) &
                ~cpu_tab[cpu_model].psw1_mbz;           /* clear mbz */
        break;

    case OP_XW:                                         /* exchange word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        if ((tr = WriteW (bva, R[rn], VW)) != 0)        /* write reg */
            return tr;
        CC34_W (opnd);                                  /* set cc's */
        R[rn] = opnd;                                   /* store result */
        break;

    case OP_STB:                                        /* store byte */
        if ((tr = Ea (IR, &bva, VR, BY)) != 0)          /* get eff addr */
            return tr;
        if ((tr = WriteB (bva, R[rn], VW)) != 0)        /* store */
            return tr;
        break;

    case OP_STH:                                        /* store halfword */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = WriteH (bva, R[rn], VW)) != 0)        /* store */
            return tr;
        if (R[rn] == (SEXT_H_W (R[rn]) & WMASK))        /* rn a sext hw? */
            CC &= ~CC2;                                 /* yes, clr CC2 */
        else CC |= CC2;
        break;

    case OP_STW:                                        /* store word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = WriteW (bva, R[rn], VW)) != 0)        /* store */
            return tr;
        break;

    case OP_STD:                                        /* store doubleword */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = WriteD (bva, R[rn], R[rn|1], VW)) != 0) /* store */
            return tr;
        break;

    case OP_STS:                                        /* store selective */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        res = (opnd & ~R[rn|1]) | (R[rn] & R[rn|1]);    /* set under mask */
        if ((tr = WriteW (bva, res, VW)) != 0)          /* store */
            return tr;
        break;

/* Note: the Sigma 7 does not prove the instruction can execute successfully
   before starting to store registers; the Sigma 9 (and the simulator) do. */

    case OP_STM:                                        /* store multiple */
        lim = CC? CC: 16;                               /* CC sets count */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW ((bva + ((lim - 1) << 2)) & bvamqrx, &opnd, VW)) != 0)
            return tr;                                  /* test writeability */
        for (i = 0; i < lim; i++) {                     /* loop thru reg */
            if ((tr = WriteW (bva, R[rn], VW)) != 0)    /* write reg */
                return tr;
            bva = (bva + 4) & bvamqrx;                  /* next address */
            rn = (rn + 1) & RNMASK;                     /* next register */
            }
        break;

    case OP_STCF:                                       /* store cc, flt */
        if ((tr = Ea (IR, &bva, VR, BY)) != 0)          /* get eff addr */
            return tr;
        res = (CC << 4) | ((PSW1 >> PSW1_V_FPC) & PSW1_M_FPC);
        if ((tr = WriteB (bva, res, VW)) != 0)          /* store */
            return tr;
        break;

/* Analyze: Sigma 9 uses <5:7> for trap codes, the 5X0 uses <1:3> */

    case OP_ANLZ:                                       /* analyze */
        mprot = ((PSW1 & (PSW1_MS|PSW1_MM)) == PSW1_MM) &&
                ((PSW2 & (PSW2_MA9|PSW2_MA5X0)) != 0);  /* mstr prot */
        sc = QCPU_5X0? 4: 0;                            /* 5X0 vs S9 */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0) {        /* get eff address */
            if (mprot && QCPU_S9) {                     /* S9 mprot? */
                R[rn] = 0x07000000 | (bva >> 2);        /* show failure */
                break;
                }
            return tr;                                  /* others trap */
            }
        if ((tr = ReadW (bva, &opnd, VR)) != 0) {       /* get word */
            if (mprot) {                                /* trap, mprot? */
                R[rn] = (0x30000000 >> sc) | (bva >> 2); /* show failure */
                break;
                }
            return tr;                                  /* others trap */
            }
        aop = I_GETOP (opnd);                           /* get opcode */
        CC = anlz_tab[aop] & (CC1|CC2|CC4);             /* set CC1,CC2,CC4 */
        if (TST_IND (opnd))                             /* indirect? */
            CC |= CC3;                                  /* set CC3 */
        if ((anlz_tab[aop] & CC4) == 0) {               /* mem ref? */
            uint32 aln = anlz_tab[aop] >> 2;            /* get length */
            if ((tr = Ea (opnd, &bva, VR, aln)) != 0) { /* get eff addr */
                if (mprot) {                            /* trap, mprot? */
                    R[rn] = (0x10000000 >> sc) | (bva >> 2); /* show failure */
                    break;
                    }
                return tr;                              /* others trap */
                }
            R[rn] = bva >> aln;                         /* cvt addr */
            }
        break;

/* Interpret */

    case OP_INT:                                        /* interpret */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* get word */
            return tr;
        CC = (opnd >> 28) & 0xF;                        /* <0:3> -> CC */
        R[rn] = (opnd >> 16) & 0xFFF;                   /* <4:15> -> Rn */
        R[rn|1] = opnd & 0xFFFF;                        /* <16:31> -> Rn|1 */
        break;

/* Arithmetic */

    case OP_AI:                                         /* add immediate */
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sext to 32b */
        res = Add32 (R[rn], opnd, 0);                   /* add, set CC's */
        R[rn] = res;                                    /* store result */
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovfo, enabled? */
            return TR_FIX;
        break;

    case OP_AH:                                         /* add halfword */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        opnd = SEXT_H_W (opnd) & WMASK;                 /* sext to 32b */
        res = Add32 (R[rn], opnd, 0);                   /* add, set CC's */
        R[rn] = res;                                    /* store result */
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_AW:                                         /* add word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        res = Add32 (R[rn], opnd, 0);                   /* add, set CC's */
        R[rn] = res;                                    /* store result */
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_AD:                                         /* add doubleword */
        if (QCPU_S89_5X0 && (rn & 1))                   /* invalid reg? */
            return TR_INVREG;
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* read doubleword */
            return tr;
        res1 = Add32 (R[rn|1], opnd1, 0);               /* add low, high */
        res = Add32 (R[rn], opnd, (CC & CC1) != 0);
        if ((res == 0) && (res1 != 0))                  /* 0'non-zero? */
            CC = (CC & ~CC4) | CC3;                     /* set CC3 */
        R[rn|1] = res1;                                 /* store */
        R[rn] = res;
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_AWM:                                        /* add word to memory */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        res = Add32 (R[rn], opnd, 0);                   /* add, set CC's */
        if ((tr = WriteW (bva, res, VW)) != 0)          /* store */
            return tr;
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_SH:                                         /* subtract halfword */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        opnd = SEXT_H_W (opnd) & WMASK;                 /* sext to 32b */
        res = Add32 (R[rn], opnd ^ WMASK, 1);           /* subtract, set CC's */
        R[rn] = res;                                    /* store */
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_SW:                                         /* subtract word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        res = Add32 (R[rn], opnd ^ WMASK, 1);           /* subtract */
        R[rn] = res;                                    /* store */
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_SD:                                         /* subtract doubleword */
        if (QCPU_S89_5X0 && (rn & 1))                   /* invalid reg? */
            return TR_INVREG;
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* read doubleword */
            return tr;
        res1 = Add32 (R[rn|1], opnd1 ^ WMASK, 1);       /* sub low, high */
        res = Add32 (R[rn], opnd ^ WMASK, (CC & CC1) != 0);
        if ((res == 0) && (res1 != 0))                  /* 0'non-zero? */
            CC = (CC & ~CC4) | CC3;                     /* set CC3 */
        R[rn|1] = res1;                                 /* store */
        R[rn] = res;
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_MI:                                         /* multiply immed */
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sext to 32b */
        res = SMul64 (R[rn|1], opnd, &res1);            /* 64b product */
        R[rn] = res;                                    /* store */
        R[rn|1] = res1;
        break;

    case OP_MH:                                         /* multiply half */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        sop = (int32) SEXT_H_W (R[rn]);                 /* sext operands */
        sop1 = (int32) SEXT_H_W (opnd);
        res = (uint32) ((sop * sop1) & WMASK);          /* product */
        CC34_W (res);                                   /* set CC's */
        R[rn|1] = res;                                  /* store */
        break;        

    case OP_MW:                                         /* multiply word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        res = SMul64 (R[rn|1], opnd, &res1);            /* 64b product */
        R[rn] = res;                                    /* store */
        R[rn|1] = res1;
        break;

    case OP_DH:                                         /* divide halfword */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        sop = (int32) R[rn];                            /* sext operands */
        sop1 = (int32) SEXT_H_W (opnd);
        if ((opnd == 0) ||                              /* div by zero? */
            ((R[rn] == WSIGN) &&                        /* -2^31 / -1? */
             (opnd == HMASK))) {
            CC |= CC2;                                  /* overflow */
            if (PSW1 & PSW1_AM)                         /* trap if enabled */
                return TR_FIX;
            }
        else {
            res = (uint32) ((sop / sop1) & WMASK);      /* quotient */
            CC &= ~CC2;                                 /* no overflow */
            CC34_W (res);                               /* set CC's */
            R[rn] = res;                                /* store */
            }
        break;

    case OP_DW:                                         /* divide word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd2, VR)) != 0)        /* get divisor */
            return tr;
        if (rn & 1)                                     /* odd reg? */
            opnd = (R[rn] & WSIGN)? WMASK: 0;           /* high is sext low */
        else opnd = R[rn];
        opnd1 = R[rn|1];                                /* low divd */
        if (SDiv64 (opnd, opnd1, opnd2, &res, &res1)) { /* 64b/32b divide */
            CC |= CC2;                                  /* overflow, set CC2 */
            if (PSW1 & PSW1_AM)                         /* trap if enabled */
                return TR_FIX;
            }
        else {
            CC &= ~CC2;                                 /* clear CC2 */
            CC34_W (res);                               /* set CC's from quo */
            R[rn] = res1;                               /* store */
            R[rn|1] = res;
            }
        break;

    case OP_MTB:                                        /* mod and test byte */
        if ((tr = Ea (IR, &bva, VR, BY)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadB (bva, &opnd, VR)) != 0)         /* read byte */
            return tr;
        opnd1 = SEXT_RN_W (rn) & BMASK;                 /* mod is sext rn */
        res = (opnd + opnd1) & BMASK;                   /* do zext add */
        if (res < opnd)                                 /* carry out? */
            CC = CC1;
        else CC = 0;
        CC34_W (res);                                   /* set cc's */
        if (rn &&                                       /* any mod? */
            ((tr = WriteB (bva, res, VW)) != 0))        /* rewrite */
            return tr;
        break;

    case OP_MTH:                                        /* mod and test half */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        opnd = opnd & HMASK;                            /* 16 operands */
        opnd1 = SEXT_RN_W (rn) & HMASK;                 /* mod is sext rn */
        res = opnd + opnd1;                             /* 16b add */
        if ((res & HMASK) == 0)                         /* 16b CC tests */
            CC = 0;
        else if (res & HSIGN)
            CC = CC4;
        else CC = CC3;
        if ((res & ~HMASK) != 0)                        /* carry? */
            CC |= CC1;
        if (((opnd ^ ~opnd1) & (opnd ^ res)) & HSIGN)   /* set overflow */
            CC |= CC2;
        if (rn &&                                       /* any mod? */
            ((tr = WriteH (bva, res, VW)) != 0))        /* rewrite */
            return tr;
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

    case OP_MTW:                                        /* mod and test word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* get word */
            return tr;
        opnd1 = SEXT_RN_W (rn) & WMASK;                 /* mod is sext rn */
        res = Add32 (opnd, opnd1, 0);                   /* do add */
        if (rn &&                                       /* any mod? */
            ((tr = WriteW (bva, res, VW)) != 0))        /* rewrite */
            return tr;
        if ((CC & CC2) && (PSW1 & PSW1_AM))             /* ovflo, enabled? */
            return TR_FIX;
        break;

/* Logical */

    case OP_AND:                                        /* and */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* get word */
            return tr;
        res = R[rn] & opnd;
        CC34_W (res);                                   /* set CC's */
        R[rn] = res;                                    /* store */
        break;

    case OP_OR:                                         /* or */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* get word */
            return tr;
        res = R[rn] | opnd;
        CC34_W (res);                                   /* set CC's */
        R[rn] = res;                                    /* store */
    break;

    case OP_EOR:                                        /* xor */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* get word */
            return tr;
        res = R[rn] ^ opnd;
        CC34_W (res);                                   /* set CC's */
        R[rn] = res;                                    /* store */
    break;

/* Compares */

    case OP_CI:                                         /* compare immed */
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sext to 32b */
        CC234_CMP (R[rn], opnd);                        /* set CC's */
        break;

    case OP_CB:                                         /* compare byte */
        if ((tr = Ea (IR, &bva, VR, BY)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadB (bva, &opnd, VR)) != 0)         /* read byte */
            return tr;
        opnd1 = R[rn] & BMASK;                          /* zext operand */
        CC234_CMP (opnd1, opnd);                        /* set CC's */
        break;

    case OP_CH:                                         /* compare halfword */
        if ((tr = Ea (IR, &bva, VR, HW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadH (bva, &opnd, VR)) != 0)         /* read halfword */
            return tr;
        opnd = SEXT_H_W (opnd) & WMASK;                 /* sext to 32b */
        CC234_CMP (R[rn], opnd);                        /* set CC's */
        break;

    case OP_CW:                                         /* compare word */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        CC234_CMP (R[rn], opnd);                        /* set CC's */
        break;

    case OP_CD:                                         /* compare double */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* read doubleword */
            return tr;
        CC &= ~(CC3|CC4);
        if (R[rn] != opnd)                              /* hi unequal? */
            CC |= Cmp32 (R[rn], opnd);
        else if (R[rn|1] != opnd1)                      /* low unequal? */
            CC |= (R[rn|1] < opnd1)? CC4: CC3;          /* like signs */
        break;

    case OP_CS:                                         /* compare select */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        opnd1 = R[rn] & R[rn|1];                        /* mask operands */
        opnd = opnd & R[rn|1];
        if (opnd1 < opnd)                               /* unsigned compare */
            CC = (CC & ~CC3) | CC4;
        else if (opnd1 > opnd)
            CC = (CC & ~CC4) | CC3;
        else CC &= ~(CC3|CC4);
        break;

    case OP_CLR:                                        /* compare limit reg */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        CC = Cmp32 (R[rn], opnd) |                      /* compare high reg */
            (Cmp32 (R[rn|1], opnd) << 2);               /* compare low reg */
        break;

    case OP_CLM:                                        /* compare limit mem */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* get doubleword */
            return tr;
        CC = Cmp32 (R[rn], opnd) |                      /* compare high mem */
            (Cmp32 (R[rn], opnd1) << 2);                /* compare low mem */
        break;

/* Shift and convert instructions */

    case OP_S:                                          /* shift */
        if ((tr = EaSh (IR, &stype, &sc)) != 0)         /* get type, count */
            return tr;
        if ((stype >= 0x6) && QCPU_S567)                /* invalid, S5-7? */
            stype = 0x4;                                /* treat as arith */
        CC = (CC & ~(CC1|CC2|CC4)) |                    /* shift, set CC's */
            Shift (rn, stype, sc);
        break;

    case OP_SF:                                         /* shift floating */
        if ((tr = EaSh (IR, &stype, &sc)) != 0)         /* get type, count */
            return tr;
        ShiftF (rn, stype & 1, sc);                     /* shift, set CC's */
        break;

    case OP_CVA:                                        /* cvt by addition */
        if (QCPU_S5)                                    /* not on Sigma 5 */
            return TR_NXI;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        CC &= ~CC1;                                     /* clear CC1 (carry) */
        for (i = 0, res = 0; i < 32; i++) {             /* 32 iterations */
            if ((R[rn|1] >> (31 - i)) & 1) {            /* bit set? */
                uint32 ad = (bva + (i << 2)) & bvamqrx; /* table offset */
                if ((tr = ReadW (ad, &opnd, VR)) != 0)
                    return tr;                          /* read table word */
                res = (res + opnd) & WMASK;             /* add into result */
                if (res < opnd)                         /* carry? set CC1 */
                    CC |= CC1;
                }                                       /* end bit set */
             }                                          /* end for */
        CC34_W (res);                                   /* set CC's */
        R[rn] = res;                                    /* store */
        break;

    case OP_CVS:                                        /* cvt by subtraction */
        if (QCPU_S5)                                    /* not on Sigma 5 */
            return TR_NXI;
       if ((tr = Ea (IR, &bva, VR, WD)) != 0)           /* get eff addr */
            return tr;
       for (i = 0, res = R[rn], res1 = 0; i < 32; i++) { /* 32 iterations */
            uint32 ad = (bva + (i << 2)) & bvamqrx;     /* table offset */
            if ((tr = ReadW (ad, &opnd, VR)) != 0)      /* read table word */
                return tr;
            if (opnd <= res) {                          /* residue >= entry? */
                res = (res - opnd) & WMASK;             /* subtract entry */
                res1 |= 1u << (31 - i);                 /* set bit */
                }
            }
       CC34_W (res1);                                   /* set CC's */
       R[rn] = res;                                     /* store */
       R[rn|1] = res1;
       break;

/* Push down instructions */

    case OP_PSW:                                        /* push word */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VW)) != 0) /* read stack ptr */
            return tr;
        if ((tr = TestSP1 (opnd1, 1)) != 0)             /* will push work? */
            return ((tr & WSIGN)? 0: tr);
        if ((tr = WriteW (((opnd + 1) << 2) & bvamqrx, R[rn], VW)) != 0)
            return tr;                                  /* push word */
        if ((tr = ModWrSP (bva, opnd, opnd1, 1)) != 0)  /* mod, rewrite sp */
            return tr;
        break;

    case OP_PLW:                                        /* pull word */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VW)) != 0) /* read stack ptr */
            return tr;
        if ((tr = TestSP1 (opnd1, -1)) != 0)            /* will pull work? */
            return ((tr & WSIGN)? 0: tr);
        if ((tr = ReadW (opnd << 2, &res, VR)) != 0)    /* pull word */
            return tr;
        if ((tr = ModWrSP (bva, opnd, opnd1, -1)) != 0) /* mod, rewrite sp */
            return tr;
        R[rn] = res;
        break;

    case OP_PSM:                                        /* push multiple */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VW)) != 0) /* read stack ptr */
            return tr;
        lim = CC? CC: 16;                               /* words to push */
        if ((tr = TestSP1 (opnd1, lim)) != 0)           /* will push work? */
            return ((tr & WSIGN)? 0: tr);
        if ((tr = ReadW (((opnd + lim) << 2) & bvamqrx, &res, VW)) != 0)
            return tr;                                  /* will last work? */
        for (i = 0; i < lim; i++) {                     /* loop thru reg */
            if ((tr = WriteW (((opnd + i + 1) << 2) & bvamqrx, R[rn], VW)) != 0)
                return tr;                             /* push word */
            rn = (rn + 1) & RNMASK;
            }
        if ((tr = ModWrSP (bva, opnd, opnd1, lim)) != 0) /* mod, rewrite sp */
            return tr;
        break;

    case OP_PLM:                                        /* pull multiple */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VW)) != 0) /* read stack ptr */
            return tr;
        lim = CC? CC: 16;                               /* words to pull */
        if ((tr = TestSP1 (opnd1, -((int32)lim))) != 0) /* will pull work? */
            return ((tr & WSIGN)? 0: tr);
        rn = (rn + lim - 1) & RNMASK;
        if ((tr = ReadW (((opnd - (lim - 1)) << 2) & bvamqrx, &res, VR)) != 0)
            return tr;                                  /* will last work? */
        for (i = 0; i < lim; i++) {                     /* loop thru reg */
            if ((tr = ReadW (((opnd - i) << 2) & bvamqrx, &res, VR)) != 0)
                return tr;                              /* pull word */
            R[rn] = res;
            rn = (rn - 1) & RNMASK;
            }
        if ((tr = ModWrSP (bva, opnd, opnd1, -((int32) lim))) != 0)
            return tr;
        break;

    case OP_MSP:                                        /* modify stack ptr */
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VW)) != 0) /* read stack ptr */
            return tr;
        sop = SEXT_H_W (R[rn]);                         /* get modifier */
        if ((tr = TestSP1 (opnd1, sop)) != 0)           /* will mod work? */
            return ((tr & WSIGN)? 0: tr);
        if ((tr = ModWrSP (bva, opnd, opnd1, sop)) != 0) /* mod, rewrite sp */
            return tr;
        break;

/* Control instructions */

    case OP_EXU:                                        /* execute */
        if (exu_cnt++ > exu_lim)                        /* too many? */
            return STOP_EXULIM;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        IR = opnd;                                      /* new instruction */
        goto EXU_LOOP;

    case OP_BCS:                                        /* branch cond set */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((CC & rn) != 0) {                           /* branch taken? */
            if ((tr = ReadW (bva, &opnd, VI)) != 0)     /* new PC readable? */
                return tr;
            PCQ_ENTRY;
            PC = cpu_new_PC (bva);                      /* branch */
            }
        break;

    case OP_BCR:                                        /* branch cond reset */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((CC & rn) == 0) {                           /* branch taken? */
            if ((tr = ReadW (bva, &opnd, VI)) != 0)     /* new PC readable? */
                return tr;
            PCQ_ENTRY;
            PC = cpu_new_PC (bva);                      /* branch */
            }
        break;

    case OP_BIR:                                        /* branch incr reg */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        res = (R[rn] + 1) & WMASK;                      /* ++R[rn] */
        if ((res & WSIGN) != 0) {                       /* < 0? */
            if ((tr = ReadW (bva, &opnd, VI)) != 0)     /* new PC readable? */
                return tr;
            PCQ_ENTRY;
            PC = cpu_new_PC (bva);                      /* branch */
            }
        R[rn] = res;                                    /* actual increment */
        break;

    case OP_BDR:                                        /* branch decr reg */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        res = (R[rn] - 1) & WMASK;                      /* --R[rn] */
        if (((res & WSIGN) == 0) && (res != 0)) {       /* > 0? */
            if ((tr = ReadW (bva, &opnd, VI)) != 0)     /* new PC readable? */
                return tr;
            PCQ_ENTRY;
            PC = cpu_new_PC (bva);                      /* branch */
            }
        R[rn] = res;                                    /* actual decrement */
        break;

    case OP_BAL:                                        /* branch and link */
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VI)) != 0)         /* new PC readable? */
            return tr;
        R[rn] = cpu_add_PC (real_pc, 1);                /* save incr PC */
        PCQ_ENTRY;
        PC = cpu_new_PC (bva);                          /* branch */
        break;

    case OP_CAL1:                                       /* CALL 1 */
        return TR_C1 (rn);

    case OP_CAL2:                                       /* CALL 2 */
        return TR_C2 (rn);

    case OP_CAL3:                                       /* CALL 3 */
        return TR_C3 (rn);

    case OP_CAL4:                                       /* CALL 4 */
        return TR_C4 (rn);

/* Privileged instructions */

    case OP_MMC:                                        /* MMC */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if (TST_IND (IR) &&                             /* indirect? */
            ((tr = ReadW (I_GETADDR (IR) << 2, &opnd, VNT)) != 0))
            return tr;
        return map_mmc (rn, I_GETXR (IR));

    case OP_LPSD:                                       /* load PSD */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadD (bva, &opnd, &opnd1, VR)) != 0) /* read stack ptr */
            return tr;
        if ((tr = cpu_new_PSD (IR & IRB (8), opnd, opnd1)) != 0)
            return tr;
        PCQ_ENTRY;                                      /* no traps, upd PCQ */
        if (IR & IRB (10))                              /* clr hi pri int? */
            int_hireq = io_rels_int (int_hiact, IR & IRB (11));
        else if (IR & IRB (11))                         /* clr PDF flag? */
            cpu_pdf = 0;
        break;

    case OP_XPSD:                                       /* exchange PSD */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        if ((tr = cpu_xpsd (IR & ~IRB (11), bva, VR)) != 0) /* do XPSD */
            return tr;
        PCQ_ENTRY;                                      /* no traps, upd PCQ */
        break;

    case OP_LRP:
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = ReadW (bva, &opnd, VR)) != 0)         /* read word */
            return tr;
        return cpu_new_RP (opnd);                       /* update RP */

    case OP_RD:                                         /* direct I/O */
    case OP_WD:
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = io_rwd (op, rn, bva)) != 0)           /* do direct I/O */
            return tr;
        int_hiact = io_actv_int ();                     /* re-eval active */
        int_hireq = io_eval_int ();                     /* re-eval intr */
        break;

    case OP_WAIT:                                       /* wait for int */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if (!io_poss_int ())                            /* intr possible? */
            return STOP_WAITNOINT;                      /* machine is hung */
// put idle here
        int_hireq = io_eval_int ();                     /* re-eval intr */
        break;

    case OP_AIO:                                        /* acknowledge int */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = io_aio (rn, bva)) != 0)               /* do AIO */
            return tr;
        int_hireq = io_eval_int ();                     /* re-eval intr */
        break;

    case OP_SIO:                                        /* start IO */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = io_sio (rn, bva)) != 0)               /* do SIO */
            return tr;
        int_hireq = io_eval_int ();                     /* re-eval intr */
        break;

    case OP_HIO:                                        /* halt IO */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = io_hio (rn, bva)) != 0)               /* do HIO */
            return tr;
        int_hireq = io_eval_int ();                     /* re-eval intr */
        break;

    case OP_TIO:                                        /* test IO */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = io_tio (rn, bva)) != 0)               /* do AIO */
            return tr;
        int_hireq = io_eval_int ();                     /* re-eval intr */
        break;

    case OP_TDV:                                        /* test device */
        if (PSW1 & PSW1_MS)                             /* slave mode? */
            return TR_PRV;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if ((tr = io_tdv (rn, bva)) != 0)               /* do I/O */
            return tr;
        int_hireq = io_eval_int ();                     /* re-eval intr */
        break;

    case OP_LRA:                                        /* load real addr */
        if (QCPU_S89_5X0) {                             /* late models only */
            if (PSW1 & PSW1_MS)                         /* slave mode? */
                return TR_PRV;
            return map_lra (rn, IR);                    /* in map */
            }
        return (PSW1 & PSW1_MS)? TR_NXI|TR_PRV: TR_NXI;

    case OP_LMS:                                        /* load mem system */
        if ((cpu_unit.flags & CPUF_LAMS) == 0)          /* implemented? */
            return (PSW1 & PSW1_MS)? TR_NXI|TR_PRV: TR_NXI;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        if (QCPU_S567)                                  /* old CPU? */
            R[rn] = IR;                                 /* loads inst to IR */
        else if (PSW1 & PSW1_MS)                        /* slave mode? */
            return TR_PRV;
        else return map_lms (rn, bva);                  /* in map */
        break;

    case OP_PSS:                                        /* push status */
        if (QCPU_5X0) {                                 /* 5X0 only */
            if (PSW1 & PSW1_MS)                         /* slave mode? */
                return TR_PRV;
            if ((tr = Ea (IR, &bva, VR, DW)) != 0)      /* get eff addr */
                return tr;
            if ((tr = cpu_pss (IR, bva, VR)) != 0)      /* push status */
                return tr;
            PCQ_ENTRY;
            break;
            }
        return (PSW1 & PSW1_MS)? TR_NXI|TR_PRV: TR_NXI;

    case OP_PLS:                                        /* pull status */
        if (QCPU_5X0) {                                 /* 5X0 only */
            if (PSW1 & PSW1_MS)                         /* slave mode? */
                return TR_PRV;
            if ((tr =  cpu_pls (IR)) != 0)              /* pull status */
                return tr;
            PCQ_ENTRY;
            break;
            }
        return (PSW1 & PSW1_MS)? TR_NXI|TR_PRV: TR_NXI;

/* String instructions */

    case OP_MBS:                                        /* move byte string */
        if ((cpu_unit.flags & CPUF_STR) == 0)           /* not implemented? */
            return TR_UNI;
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sign extend */
        if ((cnt = S_GETMCNT (R[rn|1])) != 0) {         /* any move? */
            sa = (opnd + (rn? R[rn] + cnt - 1: 0)) & bvamqrx; /* last src addr */
            da = (R[rn|1] + cnt - 1) & bvamqrx;         /* last dst addr */
            if (((tr = ReadB (sa, &c, VR)) != 0) ||     /* test last bytes */
                ((tr = ReadB (da, &c, VW)) != 0))
                return tr;
            }
        while (S_GETMCNT (R[rn|1])) {                   /* while count */
            sa = (opnd + (rn? R[rn]: 0)) & bvamqrx;     /* src addr */
            da = R[rn|1] & bvamqrx;                     /* dst addr */
            if ((tr = ReadB (sa, &c, VR)) != 0)         /* read src */
                return tr;
            if ((tr = WriteB (da, c, VW)) != 0)         /* write dst */
                return tr;
            if (rn && !(rn & 1))                        /* rn even, > 0? */
                R[rn] = (R[rn] + 1) & WMASK;            /* inc saddr */
            R[rn|1] = (R[rn|1] + S_ADDRINC) & WMASK;    /* inc daddr, dec cnt */
            }
        break;

    case OP_CBS:                                        /* compare byte str */
        if ((cpu_unit.flags & CPUF_STR) == 0)           /* not implemented? */
            return TR_UNI;
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sign extend */
        if ((cnt = S_GETMCNT (R[rn|1])) != 0) {         /* any compare? */
            sa = (opnd + (rn? R[rn] + cnt - 1: 0)) & bvamqrx; /* last src addr */
            da = (R[rn|1] + cnt - 1) & bvamqrx;         /* last dst addr */
            if (((tr = ReadB (sa, &c, VR)) != 0) ||     /* test last bytes */
                ((tr = ReadB (da, &c, VR)) != 0))
                return tr;
            }
        CC = CC & ~(CC3|CC4);                           /* assume equal */
        while (S_GETMCNT (R[rn|1])) {                   /* while count */
            sa = (opnd + (rn? R[rn]: 0)) & bvamqrx;     /* src addr */
            da = R[rn|1] & bvamqrx;                     /* dst addr */
            if ((tr = ReadB (sa, &c, VR)) != 0)         /* read src */
                return tr;
            if ((tr = ReadB (da, &c1, VR)) != 0)        /* read dst */
                return tr;
            if (c != c1) {                              /* not a match */
                CC |= ((c < c1)? CC4: CC3);
                break;                                  /* set CC's, done */
                }
            if (rn && !(rn & 1))                        /* rn even, > 0? */
                R[rn] = (R[rn] + 1) & WMASK;            /* inc saddr */
            R[rn|1] = (R[rn|1] + S_ADDRINC) & WMASK;    /* inc daddr, dec cnt */
            }
        break;

    case OP_TBS:                                        /* xlate byte string */
        if ((cpu_unit.flags & CPUF_STR) == 0)           /* not implemented? */
            return TR_UNI;
        if (QCPU_S89_5X0 && (rn & 1))                   /* invalid reg? */
            return TR_INVREG;
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sign extend */
        if ((cnt = S_GETMCNT (R[rn|1])) != 0) {         /* any translate? */
            da = (R[rn] + cnt - 1) & bvamqrx;           /* last dst addr */
            if ((tr = ReadB (da, &c, VW)) != 0)         /* test last byte */
                return tr;
            }
        while (S_GETMCNT (R[rn|1])) {                   /* while count */
            sa = (opnd + (rn? R[rn]: 0)) & bvamqrx;     /* src addr */
            da = R[rn|1] & bvamqrx;                     /* dst addr */
            if ((tr = ReadB (da, &c, VR)) != 0)         /* read dst */
                return tr;
            if ((tr = ReadB ((sa + c) & bvamqrx, &c1, VR)) != 0)
                return tr;                              /* translate byte */
            if ((tr = WriteB (da, c1, VW)) != 0)        /* write dst */
                return tr;
            R[rn|1] = (R[rn|1] + S_ADDRINC) & WMASK;    /* inc daddr, dec cnt */
            }
        break;

    case OP_TTBS:                                       /* xlate, test string */
        if ((cpu_unit.flags & CPUF_STR) == 0)           /* not implemented? */
            return TR_UNI;
        if (QCPU_S89_5X0 && (rn & 1))                   /* invalid reg? */
            return TR_INVREG;
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        opnd = SEXT_LIT_W (opnd) & WMASK;               /* sign extend */
        mask = rn? S_GETMCNT (R[rn]): 0xFF;             /* get mask */
        if ((cnt = S_GETMCNT (R[rn|1])) != 0) {         /* any translate? */
            da = (R[rn] + cnt - 1) & bvamqrx;           /* last dst addr */
            if ((tr = ReadB (da, &c, VR)) != 0)         /* test last byte */
                return tr;
            }
        CC &= ~CC4;                                     /* clear CC4 */
        while (S_GETMCNT (R[rn|1])) {                   /* while count */
            sa = (opnd + (rn? R[rn]: 0)) & bvamqrx;     /* src addr */
            da = R[rn|1] & bvamqrx;                     /* dst addr */
            if ((tr = ReadB (da, &c, VR)) != 0)         /* read dst */
                return tr;
            if ((tr = ReadB ((sa + c) & bvamqrx, &c1, VR)) != 0)
                return tr;                              /* translate byte */
            if ((t = c1 & mask) != 0) {                 /* byte & mask != 0? */
                if (rn)                                 /* if !r0, repl mask */
                    R[rn] = (R[rn] & ~S_MCNT) | (t << S_V_MCNT);
                CC |= CC4;                              /* set CC4, stop */
                break;
                }
            R[rn|1] = (R[rn|1] + S_ADDRINC) & WMASK;    /* inc daddr, dec cnt */
            }
        break;

/* Optional floating point instructions */

    case OP_FAS:
    case OP_FSS:
    case OP_FMS:
    case OP_FDS:                                        /* short fp */
        if((cpu_unit.flags & CPUF_FP) == 0)             /* option present? */
            return TR_UNI;
        if ((tr = Ea (IR, &bva, VR, WD)) != 0)          /* get eff addr */
            return tr;
        return fp (op, rn, bva);                        /* go process */
        
    case OP_FAL:
    case OP_FSL:
    case OP_FML:
    case OP_FDL:                                        /* long fp */
        if (QCPU_S89_5X0 && (rn & 1))                   /* invalid reg? */
            return TR_INVREG;
        if((cpu_unit.flags & CPUF_FP) == 0)             /* option present? */
            return TR_UNI;
        if ((tr = Ea (IR, &bva, VR, DW)) != 0)          /* get eff addr */
            return tr;
        return fp (op, rn, bva);                        /* go process */

/* Optional decimal instructions */

    case OP_DL:
    case OP_DST:
    case OP_DA:
    case OP_DS:
    case OP_DM:
    case OP_DD:
    case OP_DC:
    case OP_DSA:
    case OP_PACK:
    case OP_UNPK:                                       /* decimal */
        if((cpu_unit.flags & CPUF_DEC) == 0)            /* option present? */
            return TR_UNI;
        if ((tr = Ea (IR, &bva, VR, BY)) != 0)          /* get eff addr */
            return tr;
        if ((tr = cis_dec (op, rn, bva)) & WSIGN)       /* process, abort? */
            return 0;
        else return tr;

    case OP_EBS:                                        /* edit byte string */
        if ((tr = ImmOp (IR, &opnd)) != 0)              /* get immed opnd */
            return tr;
        if ((cpu_unit.flags & CPUF_DEC) == 0)           /* option present? */
            return TR_UNI;
        if (QCPU_S89_5X0 && ((rn == 0) || (rn & 1)))    /* invalid reg? */
            return TR_INVREG;
        if ((tr = cis_ebs (rn, opnd)) & WSIGN)          /* process, abort? */
            return 0;
        else return tr;
 
    default:                                            /* undefined inst */
        return (stop_op? STOP_ILLEG: TR_NXI);
       }
return 0;
}

/* Execute MTx in an interrupt location

   Sigma 5/6/7/8 -  17b virtual or real addressing
   Sigma 9/5X0 -    17b virtual or 20b real addressing, no indexing

   acc is either PH (physical) or VNT (no traps)
   Memory map traps are suppressed, NXM's cause undefined behavior
   (returns a nested trap fault) */

uint32 cpu_int_mtx (uint32 vec, uint32 *res)
{
uint32 IR, bva, wd, op, rn, lnt, acc;

ReadPW (vec, &IR);                                      /* get instruction */
op = I_GETOP (IR);                                      /* get op */
lnt = 3 - (op >> 5);                                    /* 73, 53, 33 */
acc = (vec == VEC_C4P)? VNT: PH;                        /* access */
rn = I_GETRN (IR);                                      /* register */
if (hst_lnt)                                            /* if history */
    inst_hist (IR, PC, H_ITRP);                         /* record inst */
if ((acc || QCPU_S567)?                                 /* virt or S5-7? */
    (Ea (IR, &bva, acc, lnt) != 0):                     /* get eff addr */
    (EaP20 (IR, &bva, lnt) != 0))                       /* get real addr */
    return TR_NESTED;

    switch (lnt) {
    case BY:
        if (ReadB (bva, &wd, acc) != 0)                 /* read byte */
            return TR_NESTED;
        wd = (wd + SEXT_RN_W (rn)) & BMASK;             /* modify */
        if (rn && (WriteB (bva, wd, acc) != 0))         /* if mod, rewrite */
            return TR_NESTED;
        break;
    case HW:
        if (ReadH (bva, &wd, acc) != 0)                 /* read halfword */
            return TR_NESTED;
        wd = (wd + SEXT_RN_W (rn)) & HMASK;             /* modify */
        if (rn && (WriteB (bva, wd, acc) != 0))         /* if mod, rewrite */
            return TR_NESTED;
        break;
    case WD:
        if (ReadW (bva, &wd, acc) != 0)                 /* read word */
            return TR_NESTED;
        wd = (wd + SEXT_RN_W (rn)) & WMASK;             /* modify */
        if (rn && (WriteW (bva, wd, acc) != 0))         /* if mod, rewrite */
            return TR_NESTED;
        break;
        }

*res = wd;
return 0;
}

/* Execute XSPD or PSS in trap or interrupt location */

uint32 cpu_trap_or_int (uint32 vec)
{
uint32 IR, op, bva, acc, cc;

ReadPW (TR_GETVEC (vec), &IR);                          /* read vector */
op = I_GETOP (IR);                                      /* get op */
if (hst_lnt) {                                          /* if history */
    if (vec & TR_FL)                                    /* trap? */
        hst[hst_p].typ_cc_pc |= H_ABRT;                 /* mark prev abt */
    inst_hist (IR, PC, H_ITRP);                         /* record inst */
    }
if (vec & TR_FL) {                                      /* trap? */
    if (QCPU_S89)                                       /* Sigma 89? */
        PSW2 = (PSW2 & ~PSW2_TSF) | ((vec & PSW2_M_TSF) << PSW2_V_TSF);
    if (vec == TR_INVRPN)                               /* non-trap reg ptr? */
        vec = TR_INVRPT;                                /* cvt to trapped */
    if (vec & TR_PDF)                                   /* trap sets PDF? */
        cpu_pdf = 1;
    }
if (op == OP_XPSD) {                                    /* XPSD? */
    acc = (IR & IRB (10))? VNT: PH;                     /* virt vs phys */
    if ((acc || QCPU_S567)?                             /* virt or S5-7? */
        (Ea (IR, &bva, acc, DW) != 0):                  /* get eff addr */
        (EaP20 (IR, &bva, DW) != 0))                    /* get real addr */
        return TR_NESTED;
    if (cpu_xpsd (IR, bva, acc) != 0)                   /* do XPSD */
        return TR_NESTED;
    if ((cc = TR_GETCC (vec)) != 0) {                   /* modify CC's? */
        CC = CC | cc;                                   /* modify new CC's */
        if (IR & IRB (9))                               /* and maybe new PC */
            PC = cpu_add_PC (PC, cc);
        }
    return 0;
    }
if (QCPU_5X0 && (op == OP_PSS)) {                       /* 5X0 PSS? */
    if (EaP20 (IR, &bva, DW) != 0)                      /* get real addr */
        return TR_NESTED;
    if (cpu_pss (IR, bva, PH))                          /* do PSS */
        return TR_NESTED;
    }
return TR_INVTRP;
}

/* Immediate operand */

uint32 ImmOp (uint32 IR, uint32 *imm)
{
if (TST_IND (IR))                                       /* indirect traps */
    return TR_NXI;
*imm = I_GETLIT (IR);
if (hst_lnt)                                            /* record history */
    hst[hst_p].ea = hst[hst_p].op = *imm;
return 0;
}

/* Calculate effective address for normal instructions
   Note that in the event of a failure reading the ind addr,
   Ea must return that value in bva (for ANLZ) */

uint32 Ea (uint32 IR, uint32 *bva, uint32 acc, uint32 lnt)
{
uint32 ad, xr, wd;
uint32 tr;

xr = I_GETXR (IR);                                      /* get index reg */
ad = I_GETADDR (IR) << 2;                               /* get byte address */
if (TST_IND (IR)) {                                     /* indirect */
    if ((tr = ReadW (ad, &wd, acc)) != 0) {             /* read ind word */
        *bva = ad;                                      /* err? return addr */
        return tr;
        }
    if (PSW_QRX9 && (wd & WSIGN)) {                     /* S9 real ext special? */
        wd = wd & VAMASK;                               /* use only 17b */
        ad = (wd & PSW1_XA)?                            /* extended word? */
            (PSW2 & PSW2_EA) | (wd & ~PSW1_XA): wd;
        ad = ad << 2;
        }
    else ad = (wd & bvamqrx) << 2;                      /* get byte address */
    }
*bva = (ad + (xr? (R[xr] << lnt): 0)) & bvamqrx;        /* index, mask */
if (hst_lnt) {                                          /* history? */
    hst[hst_p].ea = *bva;
    ReadHist (*bva, &hst[hst_p].op, &hst[hst_p].op1, acc? VNT: PH, lnt);
    }
return 0;
}

/* Calculate effective address for 20b interrupt/trap instructions */

uint32 EaP20 (uint32 IR, uint32 *bva, uint32 lnt)
{
uint32 pa, wd;

pa = I_GETADDR20 (IR);                                  /* get 20b ref addr */
if (TST_IND (IR)) {                                     /* indirect? */
    if (ReadPW (pa, &wd)) {                             /* valid? */
        *bva = pa << 2;
        return TR_NXM;
        }
    pa = wd & cpu_tab[cpu_model].pamask;                /* get indirect */
    }
*bva = pa << 2;
if (hst_lnt) {                                          /* history? */
    hst[hst_p].ea = *bva;
    ReadHist (*bva, &hst[hst_p].op, &hst[hst_p].op1, PH, lnt);
    }
return 0;
}

/* Calculate effective address for shift */

uint32 EaSh (uint32 IR, uint32 *stype, uint32 *sc)
{
uint32 ad, xr, wd, tr;

xr = I_GETXR (IR);                                      /* get index reg */
ad = I_GETADDR (IR);                                    /* get word addr */
if (TST_IND (IR)) {                                     /* indirect? */
    if ((tr = ReadW (ad << 2, &wd, VR)) != 0)           /* read ind word */
        return tr;
    ad = I_GETADDR (wd);                                /* get word addr */
    }
if (xr)
    ad = (ad & ~SHF_M_SC) | ((ad + R[xr]) & SHF_M_SC);  /* indexing? */
*stype = SHF_GETSOP (ad);                               /* extract type */
*sc = SHF_GETSC (ad);                                   /* extract count */
if (hst_lnt) {
    hst[hst_p].ea = ad << 2;
    hst[hst_p].op = ad;
    }
return 0;
}

/* Shift routines */

uint32 Shift (uint32 rn, uint32 stype, uint32 sc)
{
uint32 i, opnd, opnd1, t, cc;

opnd = R[rn];                                           /* get operand(s) */
opnd1 = R[rn|1];
cc = CC & CC4;

if (sc & SCSIGN) {                                      /* right? */
    sc = SHF_M_SC + 1 - sc;
    switch (stype) {

        case 0x0:                                       /* right log sgl */
            if (sc > 31)                                /* >31? res = 0 */
                R[rn] = 0;
            else R[rn] = R[rn] >> sc;
            break;

        case 0x1:                                       /* right log dbl */
            if (sc > 63)                                /* >63? res = 0 */
                opnd = opnd1 = 0;
            else if (sc > 31) {                         /* >31? */
                sc = sc - 32;
                opnd1 = opnd >> sc;
                opnd = 0;
                }
            else {
                opnd1 = ((opnd1 >> sc) | (opnd << (32 - sc))) & WMASK;
                opnd = opnd >> sc;
                }
            R[rn|1] = opnd1;
            R[rn] = opnd;
            break;

        case 0x2:                                       /* right circ sgl */
            sc = sc % 32;                               /* mod 32 */
            R[rn] = ((R[rn] >> sc) | (R[rn] << (32 - sc))) & WMASK;
            break;

        case 0x3:                                       /* right circ dbl */
            sc = sc % 64;                               /* mod 64 */
            t = opnd;
            if (sc > 31) {                              /* >31? */
                sc = sc - 32;
                opnd = ((opnd1 >> sc) | (opnd << (32 - sc))) & WMASK;
                opnd1 = ((t >> sc) | (opnd1 << (32 - sc))) & WMASK;
                }
            else {
                opnd = ((opnd >> sc) | (opnd1 << (32 - sc))) & WMASK;
                opnd1 = ((opnd1 >> sc) | (t << (32 - sc))) & WMASK;
                }
            R[rn|1] = opnd1;
            R[rn] = opnd;
            break;

        case 0x4:                                       /* right arith sgl */
            t = (R[rn] & WSIGN)? WMASK: 0;
            if (sc > 31)                                /* >31? res = sign */
                R[rn] = t;
            else R[rn] = ((R[rn] >> sc) | (t << (32 - sc))) & WMASK;
            break;

        case 0x5:                                       /* right arith dbl */
            t = (R[rn] & WSIGN)? WMASK: 0;
            if (sc > 63)                                /* >63? res = sign */
                opnd = opnd1 = t;
            else if (sc > 31) {                         /* >31? */
                sc = sc - 32;
                opnd1 = ((opnd >> sc) | (t << (32 - sc))) & WMASK;
                opnd = t;
                }
            else {
                opnd1 = ((opnd1 >> sc) | (opnd << (32 - sc))) & WMASK;
                opnd = ((opnd >> sc) | (t << (32 - sc))) & WMASK;
                }
            R[rn|1] = opnd1;
            R[rn] = opnd;
            break;
 
        case 0x6:                                       /* right search sgl */
            for (i = 0; (i < sc) && !(opnd & WSIGN); i++) {
                opnd = ((opnd >> 1) | (opnd << 31)) & WMASK;
                }
            cc = (opnd & WSIGN)? (cc | CC4): (cc & ~CC4);
            R[rn] = opnd;
            R[1] = sc - i;
            break;

        case 0x7:                                       /* right search dbl */
            for (i = 0; (i < sc) & !(opnd & WSIGN); i++) {
                t = opnd;
                opnd = ((opnd >> 1) | (opnd1 << 31)) & WMASK;
                opnd1 = ((opnd1 >> 1) | (t << 31)) & WMASK;
                }
            cc = (opnd & WSIGN)? (cc | CC4): (cc & ~CC4);
            R[rn|1] = opnd1;
            R[rn] = opnd;
            R[1] = sc - i;
            break;
            }
    }                                                   /* end if */

else {                                                  /* left shift */
    switch (stype) {                                    /* switch on type */

        case 0x0:                                       /* left log sgl */
        case 0x4:                                       /* left arith sgl */
            for (i = 0; i < sc; i++) {
                if (opnd & WSIGN)                       /* count 1's */
                    cc = cc ^ CC1;
                opnd = (opnd << 1) & WMASK;
                if ((opnd ^ R[rn]) & WSIGN)             /* sign change? */
                    cc |= CC2;
                }
            R[rn] = opnd;
            break;

        case 0x1:                                       /* left log dbl */
        case 0x5:                                       /* left arith dbl */
            for (i = 0; i < sc; i++) {
                if (opnd & WSIGN)                       /* count 1's */
                    cc = cc ^ CC1;
                opnd = ((opnd << 1) | (opnd1 >> 31)) & WMASK;
                opnd1 = (opnd1 << 1) & WMASK;
                if ((opnd ^ R[rn]) & WSIGN)             /* sign change? */
                    cc |= CC2;
                }
            R[rn|1] = opnd1;
            R[rn] = opnd;
            break;

        case 0x2:                                       /* left circ sgl */
            for (i = 0; i < sc; i++) {
                if (opnd & WSIGN)                       /* count 1's */
                    cc = cc ^ CC1;
                opnd = ((opnd << 1) | (opnd >> 31)) & WMASK;
                if ((opnd ^ R[rn]) & WSIGN)             /* sign change? */
                    cc |= CC2;
                }
            R[rn] = opnd;
            break;

        case 0x3:                                       /* left circ dbl */
            for (i = 0; i < sc; i++) {
                if ((t = opnd & WSIGN) != 0)            /* count 1's */
                    cc = cc ^ CC1;
                opnd = ((opnd << 1) | (opnd1 >> 31)) & WMASK;
                opnd1 = ((opnd1 << 1) | (t >> 31)) & WMASK;
                if ((opnd ^ R[rn]) & WSIGN)             /* sign change? */
                    cc |= CC2;
                }
            R[rn|1] = opnd1;
            R[rn] = opnd;
            break;

        case 0x6:                                       /* left search sgl */
            for (i = 0; (i < sc) & !(opnd & WSIGN); i++) {
                opnd = ((opnd << 1) | (opnd >> 31)) & WMASK;
                if ((opnd ^ R[rn]) & WSIGN)             /* sign change? */
                    cc |= CC2;
                }
            cc = (opnd & WSIGN)? (cc | CC4): (cc & ~CC4);
            R[rn] = opnd;
            R[1] = sc - i;
            break;

        case 0x7:                                       /* left search dbl */
            for (i = 0; (i < sc) & !(opnd & WSIGN); i++) {
                t = opnd;
                opnd = ((opnd << 1) | (opnd1 >> 31)) & WMASK;
                opnd1 = ((opnd1 << 1) | (t >> 31)) & WMASK;
                if ((opnd ^ R[rn]) & WSIGN)             /* sign change? */
                    cc |= CC2;
                }
            cc = (opnd & WSIGN)? (cc | CC4): (cc & ~CC4);
            R[rn|1] = opnd1;
            R[rn] = opnd;
            R[1] = sc - i;
            break;
            }                                           /* end switch */
    }                                                   /* end else */
return cc;
}

/* Arithmetic routines */

uint32 Add32 (uint32 s1, uint32 s2, uint32 cin)
{
uint32 t = (s1 + s2 + cin) & WMASK;                     /* add + carry in */

if (t & WSIGN)                                          /* set CC34 */
    CC = CC4;
else if (t != 0)
    CC = CC3;
else CC = 0;
if (cin? (t <= s1): (t < s1))                           /* set carry out */
    CC |= CC1;
if (((s1 ^ ~s2) & (s1 ^ t)) & WSIGN)                    /* set overflow */
    CC |= CC2;
return t;
}

uint32 SMul64 (uint32 a, uint32 b, uint32 *lo)
{
uint32 ah, bh, al, bl, rhi, rlo, rmid1, rmid2, sign;

CC &= CC1;                                              /* clr CC2-4 */
if ((a == 0) || (b == 0)) {                             /* zero argument? */
    *lo = 0;                                            /* result is zero */
    return 0;
    }
sign = a ^ b;                                           /* sign of result */
if (a & WSIGN)                                          /* |a|, |b| */
    a = NEG_W (a);
if (b & WSIGN)
    b = NEG_W (b);
ah = (a >> 16) & HMASK;                                 /* split operands */
bh = (b >> 16) & HMASK;                                 /* into 16b chunks */
al = a & HMASK;
bl = b & HMASK;
rhi = ah * bh;                                          /* high result */
rmid1 = ah * bl;
rmid2 = al * bh;
rlo = al * bl;
rhi = rhi + ((rmid1 >> 16) & HMASK) + ((rmid2 >> 16) & HMASK);
rmid1 = (rlo + (rmid1 << 16)) & WMASK;                  /* add mid1 to lo */
if (rmid1 < rlo)                                        /* carry? incr hi */
    rhi = rhi + 1;
rmid2 = (rmid1 + (rmid2 << 16)) & WMASK;                /* add mid2 to to */
if (rmid2 < rmid1)                                      /* carry? incr hi */
    rhi = rhi + 1;
rhi = rhi & WMASK;
if (sign & WSIGN)                                       /* neg result? */
    NEG_D (rhi, rmid2);
if (rhi & WSIGN)                                        /* < 0, set CC4 */
    CC |= CC4;
else if (rhi || rmid2)                                  /* > 0, set CC3 */
    CC |= CC3;
if (rhi != ((rmid2 & WSIGN)? WMASK: 0))                 /* fit in 32b? */
    CC |= CC2;                                          /* set CC2 */
*lo = rmid2;
return rhi;
}

t_bool SDiv64 (uint32 dvdh, uint32 dvdl, uint32 dvr, uint32 *res, uint32 *rem)
{
uint32 i, quo, quos, rems;

quos = dvdh ^ dvr;
rems = dvdh;
if (dvdh & WSIGN) {                                     /* |dividend| */
    NEG_D (dvdh, dvdl);
    }
if (dvr & WSIGN)                                        /* |divisor| */
    dvr = NEG_W (dvr);
if (dvdh >= dvr)                                        /* divide work? */
    return TRUE;
for (i = quo = 0; i < 32; i++) {                        /* 32 iterations */
    quo = (quo << 1) & WMASK;                           /* shift quotient */
    dvdh = ((dvdh << 1) | (dvdl >> 31)) & WMASK;        /* shift dividend */
    dvdl = (dvdl << 1) & WMASK;
    if (dvdh >= dvr) {                                  /* step work? */
        dvdh = (dvdh - dvr) & WMASK;                    /* subtract dvr */
        quo = quo + 1;
        }
    }
if (quo & WSIGN)                                        /* quotient ovflo? */
    return TRUE;
*rem = (rems & WSIGN)? NEG_W (dvdh): dvdh;              /* sign of rem */
*res = (quos & WSIGN)? NEG_W (quo): quo;
return FALSE;                                           /* no overflow */
}

uint32 Cmp32 (uint32 a, uint32 b)
{
if (a == b)                                             /* ==? */
    return 0;
if ((a ^ b) & WSIGN)                                    /* unlike signs? */
    return (a & WSIGN)? CC4: CC3;
return (a < b)? CC4: CC3;                               /* like signs */
}

/* Test stack pointer space/words to see if it can be modified -
   returns special abort status (WSIGN) */

uint32 TestSP1 (uint32 sp1, int32 mod)
{
int32 spc, wds;
uint32 cc;

cc = 0;
spc = (int32) SP_GETSPC (sp1);                          /* get space */
wds = (int32) SP_GETWDS (sp1);                          /* get words */
if (((wds + mod) > SP_M_WDS) || ((wds + mod) < 0)) {    /* words overflow? */
    if ((sp1 & SP_TW) == 0)                             /* trap if enabled */
        return TR_PSH;
    cc |= CC3;
    }
if (((spc - mod) > SP_M_WDS) || ((spc - mod) < 0)) {    /* space overflow? */
    if ((sp1 & SP_TS) == 0)                             /* trap if enabled */
        return TR_PSH;
    cc |= CC1;
    }
CC = cc;
if (cc || (mod == 0)) {                                 /* mod fails? */
    CC |= ((spc? 0: CC2) | (wds? 0: CC4));
    return WSIGN;
    }
return 0;
}

/* Actually modify stack pointer space/words and set CC's,
   used by PSW/PLW/PSM/PLM */

uint32 ModWrSP (uint32 bva, uint32 sp, uint32 sp1, int32 mod)
{
uint32 tr;

sp = (sp + mod) & WMASK;
sp1 = (sp1 & (SP_TS|SP_TW)) |
    (((SP_GETSPC (sp1) - mod) & SP_M_SPC) << SP_V_SPC) |
    (((SP_GETWDS (sp1) + mod) & SP_M_WDS) << SP_V_WDS);
if ((tr = WriteD (bva, sp, sp1, VW)) != 0)
    return tr;
if ((mod > 0) && SP_GETSPC (sp1) == 0)
    CC |= CC2;
if ((mod < 0) && SP_GETWDS (sp1) == 0)
    CC |= CC4;
return 0;
}

/* XPSD instruction */

uint32 cpu_xpsd (uint32 IR, uint32 bva, uint32 ra)
{
uint32 wa, wd, wd1, wd3;
uint32 tr;

if (ra == VR)                                           /* virtual? */
    wa = VW;                                            /* set write virt */
else wa = ra;                                           /* no, phys */
cpu_assemble_PSD ();
wd = PSW1;                                              /* no more changes */
wd1 = PSW2;
wd3 = PSW4;
if ((tr = WriteD (bva, wd, wd1, wa)) != 0)              /* write curr PSD */
    return tr;
bva = bva + 8;
if (QCPU_5X0 && (IR & IRB (11))) {                      /* extra words? */
    if ((tr = WriteW (bva | 4, wd3, VW)) != 0)
        return tr;
    bva = bva + 8;
    }
if ((tr = ReadD (bva, &wd, &wd1, ra)) != 0)             /* read new PSD */
    return tr;
wd1 = (wd1 & ~(cpu_tab[cpu_model].psw2_mbz)) |          /* merge inhibits */
    (PSW2 & PSW2_ALLINH);
if ((tr = cpu_new_PSD (IR & IRB (8), wd, wd1)) != 0)    /* update PSD */
    return tr;
return 0;
}

/* PSS instruction */

uint32 cpu_pss (uint32 IR, uint32 bva, uint32 acc)
{
uint32 i, wd, wd1, tos, swc;
uint32 tr;

cpu_assemble_PSD ();                                    /* get old PSD */
if ((tr = ReadD (bva, &wd, &wd1, acc)) != 0)            /* read new PSD */
    return tr;
ReadPW (SSP_TOS, &tos);                                 /* read system SP */
ReadPW (SSP_SWC, &swc);
for (i = 0; i < RF_NUM; i++) {                          /* push registers */
    if (WritePW (tos + SSP_FR_RN + i + 1, R[i]))
        return TR_NXM;
    }
if (WritePW (tos + SSP_FR_PSW1 + 1, PSW1) ||            /* push PSD */
    WritePW (tos + SSP_FR_PSW2 + 1, PSW2))
    return TR_NXM;
WritePW (SSP_TOS, (tos + SSP_FR_LNT) & WMASK);          /* tos + 28 */
swc = (swc & (SP_TS|SP_TW)) |                           /* spc-28, wds+28 */
    (((SP_GETWDS (swc) + SSP_FR_LNT) & SP_M_WDS) << SP_V_WDS) |
    (((SP_GETSPC (swc) - SSP_FR_LNT) & SP_M_SPC) << SP_V_SPC);
if (SP_GETWDS (swc) < SSP_FR_LNT)                       /* wds overflow? */
    swc |= SP_TW;                                       /* set sticky bit */
WritePW (SSP_SWC, swc);
wd1 = (wd1 & ~(cpu_tab[cpu_model].psw2_mbz)) |          /* merge inhibits */
    (PSW2 & PSW2_ALLINH);
if ((tr = cpu_new_PSD (IR & IRB (8), wd, wd1)) != 0)    /* update PSD */
    return tr;
return 0;
}

/* PLS instruction */

uint32 cpu_pls (uint32 IR)
{
uint32 i, wd, wd1, tos, swc, spc;
uint32 tr;

ReadPW (SSP_TOS, &tos);                                 /* read system SP */
ReadPW (SSP_SWC, &swc);
spc = SP_GETSPC (swc);                                  /* space left */
if (spc == 0) {                                         /* none? */
    ReadPW (SSP_DFLT_PSW1, &wd);                        /* use default PSD */
    ReadPW (SSP_DFLT_PSW2, &wd1);
    }
else if (spc < SSP_FR_LNT)                              /* not enough? */
    return TR_INVSSP;
else {
    tos = (tos - SSP_FR_LNT) & WMASK;                   /* modify TOS */
    for (i = 0; i < RF_NUM; i++) {                      /* pull registers */
        if (ReadPW (tos + SSP_FR_RN + i + 1, &wd))
            return TR_NXM;
        R[i] = wd;
        }
    if (ReadPW (tos + SSP_FR_PSW1 + 1, &wd) ||          /* pull new PSD */
        ReadPW (tos + SSP_FR_PSW2 + 1, &wd1))
        return TR_NXM;
    WritePW (SSP_TOS, tos);                             /* rewrite SP */
    swc = (swc & (SP_TS|SP_TW)) |                       /* spc+28, wds-28 */
        (((SP_GETWDS (swc) - SSP_FR_LNT) & SP_M_WDS) << SP_V_WDS) |
        (((SP_GETSPC (swc) + SSP_FR_LNT) & SP_M_SPC) << SP_V_SPC);
    if (SP_GETSPC (swc) < SSP_FR_LNT)                   /* spc overflow? */
        swc |= SP_TS;                                   /* set sticky bit */
    WritePW (SSP_SWC, swc);
    }
wd1 = (wd1 & ~(cpu_tab[cpu_model].psw2_mbz)) |          /* merge inhibits */
    (PSW2 & PSW2_ALLINH);
if ((tr = cpu_new_PSD (IR & IRB (8), wd, wd1)) != 0)    /* update PSD */
    return tr;
if (IR & IRB (10))                                      /* clr hi pri int? */
    int_hireq = io_rels_int (int_hiact, IR & IRB (11));
else if (IR & IRB (11))                                 /* clr PDF flag? */
    cpu_pdf = 0;

return 0;
}

/* Load new PSD */

uint32 cpu_new_PSD (uint32 lrp, uint32 p1, uint32 p2)
{
uint32 tr;

PSW1 = p1 & ~cpu_tab[cpu_model].psw1_mbz;               /* clear mbz bits */
PSW2 = ((p2 & ~PSW2_RP) | (PSW2 & PSW2_RP)) &           /* save reg ptr */
    ~cpu_tab[cpu_model].psw2_mbz;
if (lrp &&                                              /* load reg ptr? */
    ((tr = cpu_new_RP (p2)) != 0))                      /* invalid? */
    return tr;                                          /* trap */
CC = PSW1_GETCC (PSW1);                                 /* extract CC's */
PC = PSW1_GETPC (PSW1);                                 /* extract PC */
PSW2_WLK = PSW2_GETWLK (PSW2);                          /* extract lock */
int_hireq = io_eval_int ();                             /* update intr */
if ((PSW1 & PSW1_MM) ||                                 /* mapped or */
    ((PSW2 & (PSW2_MA9|PSW2_MA5X0)) == 0)) {            /* not real ext? */
    bvamqrx = BVAMASK;                                  /* 17b masks */
    PSW_QRX9 = 0;
    }
else {                                                  /* phys real ext */
    if ((PSW_QRX9 = PSW2 & PSW2_MA9) != 0)              /* Sigma 9? */
        bvamqrx = BPAMASK22;                            /* yes, 22b masks */
    else bvamqrx = BPAMASK20;                           /* no, 20b masks */
    }
return 0;
}

/* Load new RP */

uint32 cpu_new_RP (uint32 rp)
{
uint32 rp1, j;

PSW2 = (PSW2 & ~PSW2_RP) | (rp & PSW2_RP);              /* merge to PSW2 */
PSW2 = PSW2 & ~cpu_tab[cpu_model].psw2_mbz;             /* clear nx bits */
rp1 = PSW2_GETRP (rp);
if (rp1 >= rf_bmax) {                                   /* nx reg file? */
    if (QCPU_S89)
        return TR_INVRPN;
    if (QCPU_5X0)
        return TR_INVREG;
    for (j = 0; j < RF_NUM; j++)                        /* clear nx set */
        rf[(rp1 * RF_NUM) + j] = 0;
    sim_activate (&cpu_rblk_unit, 1);                   /* sched cleaner */
    }
R = rf + (rp1 * RF_NUM);
return 0;
}

/* This routine is scheduled if the current registr block doesn't exist */

t_stat cpu_bad_rblk (UNIT *uptr)
{
uint32 rp1, j;

rp1 = PSW2_GETRP (PSW2);                                /* get reg ptr */
if (rp1 >= rf_bmax) {                                   /* still bad? */
    for (j = 0; j < RF_NUM; j++)                        /* clear nx set */
        rf[(rp1 * RF_NUM) + j] = 0;
    sim_activate (uptr, 1);                             /* sched again */
    }
return SCPE_OK;
}

/* Load new PC for branch instruction */

uint32 cpu_new_PC (uint32 bva)
{
uint32 npc = bva >> 2;

if (PSW_QRX9 && (npc & PSW1_XA))                        /* S9 real ext, XA? */
    PSW2 = (PSW2 & ~PSW2_EA) | (npc & PSW2_EA);         /* change PSW2 EA */
return npc & BVAMASK;
}

/* Add value to PC for fetch, BAL, trap */

uint32 cpu_add_PC (uint32 pc, uint32 inc)
{
if (PSW_QRX9)                                           /* S9 real ext? */
    return ((pc & ~(PSW1_M_PC & ~PSW1_XA)) |            /* modulo 16b inc */
        ((pc + inc) & (PSW1_M_PC & ~PSW1_XA)));
return ((pc + inc) & BVAMASK);                          /* no, mod 17b inc */
}

/* Assemble PSD */

void cpu_assemble_PSD (void)
{
PSW1 = (PSW1 & ~(PSW1_CCMASK|PSW1_PCMASK|cpu_tab[cpu_model].psw1_mbz)) |
    (CC << PSW1_V_CC) | (PC << PSW1_V_PC);
PSW2 = (PSW2 & ~(PSW2_WLKMASK|cpu_tab[cpu_model].psw2_mbz)) |
    (PSW2_WLK << PSW2_V_WLK);
return;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
cpu_new_PSD (1, PSW1_DFLT | (PSW1 & PSW1_PCMASK), PSW2_DFLT);
cpu_pdf = 0;
cons_alarm = 0;
cons_pcf = 0;
set_rf_display (R);
if (M == NULL)
    M = (uint32 *) calloc (MAXMEMSIZE, sizeof (uint32));
if (M == NULL)
    return SCPE_MEM;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r) pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
rtc_register (RTC_ALARM, RTC_HZ_2, &cpu_unit);
return int_reset (dptr);
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
uint32 lnt;

if (sw & SWMASK ('C'))
    lnt = 2;
else if ((sw & SWMASK ('B')) || (sw & SWMASK ('A')) || (sw & SWMASK ('E')))
    lnt = 0;
else if (sw & SWMASK ('H'))
    lnt = 1;
else lnt = 2;
if (sw & SWMASK ('V')) {
    if (ReadW (addr << lnt, vptr, VNT) != 0)
        return SCPE_REL;
    }
else if (ReadW (addr << lnt, vptr, PH) != 0)
    return SCPE_NXM;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
uint32 lnt;

if (sw & SWMASK ('C'))
    lnt = 2;
else if ((sw & SWMASK ('B')) || (sw & SWMASK ('A')) || (sw & SWMASK ('E')))
    lnt = 0;
else if (sw & SWMASK ('H'))
    lnt = 1;
else lnt = 2;
if (sw & SWMASK ('V')) {
    if (WriteW (addr << lnt, val, VNT) != 0)
        return SCPE_REL;
    }
else if (WriteW (addr << lnt, val, PH) != 0)
    return SCPE_NXM;
return SCPE_OK;
}

/* CPU configuration management

   These routines (for type, memory size, options, number of reg blocks,
   number of external int blocks) must generate a consistent result.
   To assure this, all changes (except memory size) reset the CPU. */

/* Set CPU type */

t_stat cpu_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 model = CPUF_GETMOD (val);

if (model == cpu_model)                                 /* no change? */
    return SCPE_OK;
cpu_reset (&cpu_dev);
if (MEMSIZE > (cpu_tab[cpu_model].pamask + 1))
    cpu_set_size (uptr, cpu_tab[cpu_model].pamask + 1, NULL, (void *) uptr);
cpu_model = model;
cpu_unit.flags = (cpu_unit.flags | cpu_tab[model].std) & ~cpu_tab[model].opt;
rf_bmax = RF_DFLT;
io_set_eimax (EIGRP_DFLT);
return SCPE_OK;
}

/* Set memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 mc = 0;
uint32 i;

if ((val <= 0) || (val > (int32)(cpu_tab[cpu_model].pamask + 1)))
    return SCPE_ARG;
if (!desc) {                                            /* force trunc? */
    for (i = val; i < MEMSIZE; i++)
        mc = mc | M[i];
    if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
    }
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++)
    M[i] = 0;
return SCPE_OK;
}

/* Set and clear options */

t_stat cpu_set_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if ((val & (cpu_tab[cpu_model].std | cpu_tab[cpu_model].opt)) == 0)
    return SCPE_NOFNC;
cpu_unit.flags |= val;
return SCPE_OK;
}

t_stat cpu_clr_opt (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (val & cpu_tab[cpu_model].std)
    return SCPE_NOFNC;
cpu_unit.flags &= ~val;
return SCPE_OK;
}

/* Set/show register blocks */

t_stat cpu_set_rblks (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 invmask, lnt, i, j;
t_stat r;

if (QCPU_5X0)                                           /* 5X0 fixed */
    return SCPE_NOFNC;
if (cptr == NULL)
    return SCPE_ARG;
invmask = PSW2_GETRP (cpu_tab[cpu_model].psw2_mbz);
if (QCPU_S89)
    invmask |= 0x10;
lnt = (int32) get_uint (cptr, 10, RF_NBLK, &r);
if ((r != SCPE_OK) || (lnt == 0) || (lnt & invmask))
    return SCPE_ARG;
cpu_reset (&cpu_dev);
rf_bmax = lnt;
for (i = rf_bmax; i < RF_NBLK; i++) {                   /* zero unused */
    for (j = 0; j < RF_NUM; j++)
        rf[(i * RF_NUM) + j] = 0;
    }
return SCPE_OK;
}

t_stat cpu_show_rblks (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "register blocks=%d", rf_bmax);
return SCPE_OK;
}

/* Set current register file pointers for SCP */

void set_rf_display (uint32 *rfbase)
{
REG *rptr;
uint32 i;

rptr = find_reg ("R0", NULL, &cpu_dev);
if (rptr == NULL) return;
for (i = 0; i < RF_NUM; i++, rptr++)
    rptr->loc = (void *) (rfbase + i);
return;
}

/* Front panael alarm */

t_stat cpu_set_alarm (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
cons_alarm_enb = val;
return SCPE_OK;
}

t_stat cpu_show_alarm (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fputs (cons_alarm_enb? "alarm enabled\n": "alarm disabled\n", st);
return SCPE_OK;
}

t_stat cpu_svc (UNIT *uptr)
{
if (cons_alarm && cons_alarm_enb)
    sim_putchar ('\a');
return SCPE_OK;
}

/* Address converter and display */

/* Virtual address translation */

t_stat cpu_show_addr (FILE *of, UNIT *uptr, int32 val, CONST void *desc)
{
t_stat r;
char *cptr = (char *) desc;
uint32 ad, bpa, dlnt, virt;
static const char *lnt_str[] = {
    "byte",
    "halfword",
    "word",
    "doubleword",
    };
extern uint32 map_reloc (uint32 bva, uint32 acc, uint32 *bpa);

if ((val < 0) || (val > DW))
    return SCPE_IERR;
virt = (sim_switches & SWMASK ('V'))? 1: 0;
if (cptr) {
    ad = (uint32) get_uint (cptr, 16, virt? VAMASK: PAMASK22, &r);
    if (r == SCPE_OK) {
        if (sim_switches & SWMASK ('B'))
            dlnt = 0;
        else if (sim_switches & SWMASK ('H'))
            dlnt = 1;
        else if (sim_switches & SWMASK ('D'))
            dlnt = 3;
        else dlnt = 2;
        bpa = ad << val;
        if (virt && map_reloc (bpa, VNT, &bpa))
            fprintf (of, "Virtual address %-X: memory management error\n", ad);
        else fprintf (of, "%s %s %-X: physical %s %-X\n",
            ((virt)? "Virtual": "Physical"), lnt_str[val], ad, lnt_str[dlnt], bpa >> dlnt);
        return SCPE_OK;
        }
    }
fprintf (of, "Invalid argument\n");
return SCPE_OK;
}

/* Record history */

void inst_hist (uint32 ir, uint32 pc, uint32 tp)
{
uint32 rn = I_GETRN (ir);

hst_p = (hst_p + 1);                                    /* next entry */
if (hst_p >= hst_lnt)
    hst_p = 0;
hst[hst_p].typ_cc_pc = (CC << PSW1_V_CC) | pc | tp;
hst[hst_p].ir = ir;
hst[hst_p].rn = R[rn];
hst[hst_p].rn1 = R[rn|1];
return;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].typ_cc_pc = 0;
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

/* Print one instruction */

void cpu_fprint_one_inst (FILE *st, uint32 tcp, uint32 ir, uint32 rn, uint32 rn1,
    uint32 ea, uint32 opnd, uint32 opnd1)
{
t_value sim_val;

if (tcp & (H_INST|H_ITRP)) {                        /* instr or trap? */
    uint32 op = I_GETOP (ir);
    uint32 cc = PSW1_GETCC (tcp);
    uint32 pc = tcp & PAMASK20;
    uint8 fl = anlz_tab[op];

    fprintf (st, "%c %05X %X %08X %08X ",           /* standard fields */
        ((tcp & H_INST)? ' ': 'T'), pc, cc, rn, rn1);
    if (tcp & H_ABRT)                               /* aborted? */
        fputs ("aborted                 ", st);
    else {
        if (fl & CC4)                               /* immediate? */
            fprintf (st, "%05X                   ", ea);
        else if ((fl >> 2) != DW)                   /* byte/half/word? */
            fprintf (st, "%05X %08X          ", ea >> 2, opnd);
        else fprintf (st, "%05X %08X %08X ", ea >> 2, opnd, opnd1);
        }
    sim_val = ir;
    if ((fprint_sym (st, pc, &sim_val, NULL, SWMASK ('M'))) > 0)
        fprintf (st, "(undefined) %08X", ir);
    fputc ('\n', st);                               /* end line */
    }
return;
}

/* Show history */

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
int32 k, di, lnt;
t_stat r;
char *cptr = (char *) desc;
InstHistory *h;

if (hst_lnt == 0)                                   /* enabled? */
    return SCPE_NOFNC;
if (cptr) {
    lnt = (int32) get_uint (cptr, 10, hst_lnt, &r);
    if ((r != SCPE_OK) || (lnt == 0)) return SCPE_ARG;
    }
else lnt = hst_lnt;
di = hst_p - lnt;                                       /* work forward */
if (di < 0) di = di + hst_lnt;
fprintf (st, "  PC   CC Rn       Rn|1     EA    operand  operand1 IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->typ_cc_pc)                                   /* instruction? */
        cpu_fprint_one_inst (st, h->typ_cc_pc, h->ir, h->rn, h->rn1, h->ea, h->op, h->op1);
    }                                                   /* end for */
return SCPE_OK;
}
