/* ka10_cpu.c: PDP-10 CPU simulator

   Copyright (c) 2011-2017, Richard Cornwell

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

   Except as contained in this notice, the name of Richard Cornwell shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Richard Cornwell

   cpu          KA10/KL10 central processor


   The 36b system family had six different implementions: PDP-6, KA10, KI10,
   KL10, KL10 extended, and KS10.

   The register state for the KA10 is:

   AC[16]                       accumulators
   PC                           program counter
   flags<0:11>                  state flags
   pi_enb<1:7>                  enabled PI levels
   pi_act<1:7>                  active PI levels
   pi_prq<1:7>                  program PI requests
   apr_enb<0:7>                 enabled system flags
   apr_flg<0:7>                 system flags

   The PDP-10 had just two instruction formats: memory reference
   and I/O.

    000000000 0111 1 1111 112222222222333333
    012345678 9012 3 4567 890123456789012345
   +---------+----+-+----+------------------+
   |  opcode | ac |i| idx|     address      | memory reference
   +---------+----+-+----+------------------+

    000 0000000 111 1 1111 112222222222333333
    012 3456789 012 3 4567 890123456789012345
   +---+-------+---+-+----+------------------+
   |111|device |iop|i| idx|     address      | I/O
   +---+-------+---+-+----+------------------+

   This routine is the instruction decode routine for the PDP-10.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until an abort occurs.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        MUUO instruction in executive mode
        pager error in interrupt sequence
        invalid vector table in interrupt sequence
        illegal instruction in interrupt sequence
        breakpoint encountered
        nested indirects exceeding limit
        nested XCT's exceeding limit
        I/O error in I/O simulator

   2. Interrupts.  PDP-10's have a seven level priority interrupt
      system.  Interrupt requests can come from internal sources,
      such as APR program requests, or external sources, such as
      I/O devices.  The requests are stored in pi_prq for program
      requests, pi_apr for other internal flags, and pi_ioq for
      I/O device flags.  Internal and device (but not program)
      interrupts must be enabled on a level by level basis.  When
      an interrupt is granted on a level, interrupts at that level
      and below are masked until the interrupt is dismissed.


   3. Arithmetic.  The PDP-10 is a 2's complement system.

   4. Adding I/O devices.  These modules must be modified:

        ka10_defs.h    add device address and interrupt definitions
        ka10_sys.c     add sim_devices table entry

*/

#include "kx10_defs.h"
#include "sim_timer.h"
#include <time.h>

#define HIST_PC         0x40000000
#define HIST_PC2        0x80000000
#define HIST_MIN        64
#define HIST_MAX        500000
#define TMR_RTC         0
#define TMR_QUA         1


uint64  M[MAXMEMSIZE];                        /* Memory */
#if KI
uint64  FM[64];                               /* Fast memory register */
#else
uint64  FM[16];                               /* Fast memory register */
#endif
uint64  AR;                                   /* Primary work register */
uint64  MQ;                                   /* Extension to AR */
uint64  BR;                                   /* Secondary operand */
uint64  AD;                                   /* Address Data */
uint64  MB;                                   /* Memory Bufer Register */
uint32  AB;                                   /* Memory address buffer */
uint32  PC;                                   /* Program counter */
uint32  IR;                                   /* Instruction register */
uint64  MI;                                   /* Monitor lights */
uint32  FLAGS;                                /* Flags */
uint32  AC;                                   /* Operand accumulator */
uint64  SW;                                   /* Switch register */
int     BYF5;                                 /* Flag for second half of LDB/DPB instruction */
int     uuo_cycle;                            /* Uuo cycle in progress */
int     sac_inh;                              /* Don't store AR in AC */
int     SC;                                   /* Shift count */
int     SCAD;                                 /* Shift count extension */
int     FE;                                   /* Exponent */
#if KA | PDP6
int     Pl, Ph, Rl, Rh, Pflag;                /* Protection registers */
int     push_ovf;                             /* Push stack overflow */
int     mem_prot;                             /* Memory protection flag */
#endif
int     nxm_flag;                             /* Non-existant memory flag */
int     clk_flg;                              /* Clock flag */
int     ov_irq;                               /* Trap overflow */
int     fov_irq;                              /* Trap floating overflow */
#if PDP6
int     pcchg_irq;                            /* PC Change flag */
int     ill_op;                               /* Illegal opcode */
int     user_io;                              /* User IO flag */
int     ex_uuo_sync;                          /* Execute a UUO op */
#endif
uint8   PIR;                                  /* Current priority level */
uint8   PIH;                                  /* Highest priority */
uint8   PIE;                                  /* Priority enable mask */
int     pi_enable;                            /* Interrupts enabled */
int     parity_irq;                           /* Parity interupt */
int     pi_pending;                           /* Interrupt pending. */
int     pi_enc;                               /* Flag for pi */
int     apr_irq;                              /* Apr Irq level */
int     clk_en;                               /* Enable clock interrupts */
int     clk_irq;                              /* Clock interrupt */
int     pi_restore;                           /* Restore previous level */
int     pi_hold;                              /* Hold onto interrupt */
int     modify;                               /* Modify cycle */
int     xct_flag;                             /* XCT flags */
#if KI
uint64  ARX;                                  /* Extension to AR */
uint64  BRX;                                  /* Extension to BR */
uint64  ADX;                                  /* Extension to AD */
uint32  ub_ptr;                               /* User base pointer */
uint32  eb_ptr;                               /* Executive base pointer */
uint8   fm_sel;                               /* User fast memory block */
int32   apr_serial = -1;                      /* CPU Serial number */
int     inout_fail;                           /* In out fail flag */
int     small_user;                           /* Small user flag */
int     user_addr_cmp;                        /* User address compare flag */
#endif
#if KI | ITS | BBN
uint32  e_tlb[512];                           /* Executive TLB */
uint32  u_tlb[546];                           /* User TLB */
int     page_enable;                          /* Enable paging */
int     page_fault;                           /* Page fail */
uint32  ac_stack;                             /* Register stack pointer */
uint32  pag_reload;                           /* Page reload pointer */
uint64  fault_data;                           /* Fault data from last fault */
int     trap_flag;                            /* In trap cycle */
int     last_page;                            /* Last page mapped */
#endif
#if BBN
int     exec_map;                             /* Enable executive mapping */
int     next_write;                           /* Clear next write mapping */
int     mon_base_reg;                         /* Monitor base register */
int     user_base_reg;                        /* User base register */
int     user_limit;                           /* User limit register */
uint64  pur;                                  /* Process use register */
#endif
#if MPX_DEV
int     mpx_enable;                           /* Enable MPX device */
#endif
#if ITS
uint32  dbr1;                                 /* User Low Page Table Address */
uint32  dbr2;                                 /* User High Page Table Address */
uint32  dbr3;                                 /* Exec High Page Table Address */
uint32  jpc;                                  /* Jump program counter */
uint8   age;                                  /* Age word */
uint32  fault_addr;                           /* Fault address */
uint64  opc;                                  /* Saved PC and Flags */
uint64  mar;                                  /* Memory address compare */
uint32  qua_time;                             /* Quantum clock value */
#endif
int     watch_stop;                           /* Stop at memory watch point */
int     maoff = 0;                            /* Offset for traps */

uint16  dev_irq[128];                         /* Pending irq by device */
t_stat  (*dev_tab[128])(uint32 dev, uint64 *data);
int     (*dev_irqv[128])(uint32 dev, int addr);
t_stat  rtc_srv(UNIT * uptr);
int32   rtc_tps = 60;
#if ITS
t_stat  qua_srv(UNIT * uptr);
int32   qua_tps = 125000;
#endif
int32   tmxr_poll = 10000;

/* Physical address range for Rubin 10-11 interface. */
#define T11RANGE(addr)  ((addr) >= 03040000)
/* Physical address range for auxiliary PDP-6. */
#define AUXCPURANGE(addr)  ((addr) >= 03000000 && (addr) < 03040000)

DEVICE *rh_devs[] = {
#if (NUM_DEVS_RS > 0)
    &rsa_dev,
#endif
#if (NUM_DEVS_RP > 0)
    &rpa_dev,
#if (NUM_DEVS_RP > 1)
    &rpb_dev,
#if (NUM_DEVS_RP > 2)
    &rpc_dev,
#if (NUM_DEVS_RP > 3)
    &rpd_dev,
#endif
#endif
#endif
#endif
#if (NUM_DEVS_TU > 0)
    &tua_dev,
#endif
    NULL,
};

struct rh_dev rh[] = {
    { 0270, NULL, },
    { 0274, NULL, },
    { 0360, NULL, },
    { 0364, NULL, },
    { 0370, NULL, },
    { 0374, NULL, },
    { 0,    NULL, },
};

typedef struct {
    uint32      pc;
    uint32      ea;
    uint64      ir;
    uint64      ac;
    uint32      flags;
    uint64      mb;
    uint64      fmb;
    } InstHistory;

int32 hst_p = 0;                         /* history pointer */
int32 hst_lnt = 0;                       /* history length */
InstHistory *hst = NULL;                 /* instruction history */

/* Forward and external declarations */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#if KI
t_stat cpu_set_serial (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_serial (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#endif
t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char          *cpu_description (DEVICE *dptr);
void set_ac_display (uint64 *acbase);

t_bool build_dev_tab (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit[] = { { UDATA (&rtc_srv, UNIT_IDLE|UNIT_FIX|UNIT_BINK|UNIT_TWOSEG, 256 * 1024) },
#if ITS
                    { UDATA (&qua_srv, UNIT_IDLE|UNIT_DIS, 0) }
#endif
                   };

REG cpu_reg[] = {
    { ORDATAD (PC, PC, 18, "Program Counter") },
    { ORDATAD (FLAGS, FLAGS, 18, "Flags") },
    { ORDATAD (FM0, FM[00], 36, "Fast Memory") },       /* addr in memory */
    { ORDATA (FM1, FM[01], 36) },                       /* modified at exit */
    { ORDATA (FM2, FM[02], 36) },                       /* to SCP */
    { ORDATA (FM3, FM[03], 36) },
    { ORDATA (FM4, FM[04], 36) },
    { ORDATA (FM5, FM[05], 36) },
    { ORDATA (FM6, FM[06], 36) },
    { ORDATA (FM7, FM[07], 36) },
    { ORDATA (FM10, FM[010], 36) },
    { ORDATA (FM11, FM[011], 36) },
    { ORDATA (FM12, FM[012], 36) },
    { ORDATA (FM13, FM[013], 36) },
    { ORDATA (FM14, FM[014], 36) },
    { ORDATA (FM15, FM[015], 36) },
    { ORDATA (FM16, FM[016], 36) },
    { ORDATA (FM17, FM[017], 36) },
#if KI
    { BRDATA (FM, &FM[0], 8, 36, 64)},
#else
    { BRDATA (FM, &FM[0], 8, 36, 16)},
#endif
    { ORDATAD (PIR, PIR, 8, "Priority Interrupt Request") },
    { ORDATAD (PIH, PIH, 8, "Priority Interrupt Hold") },
    { ORDATAD (PIE, PIE, 8, "Priority Interrupt Enable") },
    { ORDATAD (PIENB, pi_enable, 7, "Enable Priority System") },
    { ORDATAD (SW, SW, 36, "Console SW Register"), REG_FIT},
    { ORDATAD (MI, MI, 36, "Monitor Display"), REG_FIT},
    { FLDATAD (BYF5, BYF5, 0, "Byte Flag") },
    { FLDATAD (UUO, uuo_cycle, 0, "UUO Cycle") },
#if KA | PDP6
    { ORDATAD (PL, Pl, 18, "Program Limit Low") },
    { ORDATAD (PH, Ph, 18, "Program Limit High") },
    { ORDATAD (RL, Rl, 18, "Program Relation Low") },
    { ORDATAD (RH, Rh, 18, "Program Relation High") },
    { FLDATAD (PFLAG, Pflag, 0, "Relocation enable") },
    { FLDATAD (PUSHOVER, push_ovf, 0, "Push overflow flag") },
    { FLDATAD (MEMPROT, mem_prot, 0, "Memory protection flag") },
#endif
    { FLDATAD (NXM, nxm_flag, 0, "Non-existing memory access") },
    { FLDATAD (CLK, clk_flg, 0, "Clock interrupt") },
    { FLDATAD (OV, ov_irq, 0, "Overflow enable") },
#if PDP6
    { FLDATAD (PCCHG, pcchg_irq, 0, "PC Change interrupt") },
    { FLDATAD (USERIO, user_io, 0, "User I/O") },
    { FLDATAD (UUOSYNC, ex_uuo_sync, 0, "UUO Op") },
#else
    { FLDATAD (FOV, fov_irq, 0, "Floating overflow enable") },
#endif
    { FLDATA (PI_PEND, pi_pending, 0), REG_HRO},
    { FLDATA (PARITY, parity_irq, 0) },
    { ORDATAD (APRIRQ, apr_irq, 0, "APR Interrupt number") },
    { ORDATAD (CLKIRQ, clk_irq, 0, "CLK Interrupt number") },
    { FLDATA (CLKEN, clk_en, 0), REG_HRO},
    { FLDATA (XCT, xct_flag, 0), REG_HRO},
#if MPX_DEV
    { FLDATA (MPX, mpx_enable, 0), REG_HRO},
#endif
    { FLDATA (PIHOLD, pi_hold, 0), REG_HRO},
    { FLDATA (PIREST, pi_restore, 0), REG_HRO},
#if KI
    { ORDATAD (UB, ub_ptr, 18, "User Base Pointer") },
    { ORDATAD (EB, eb_ptr, 18, "Executive Base Pointer") },
    { ORDATAD (FMSEL, fm_sel, 8, "Register set select") },
    { ORDATAD (SERIAL, apr_serial, 10, "System Serial Number") },
    { FLDATA (INOUT, inout_fail, 0), REG_RO},
    { FLDATA (SMALL, small_user, 0), REG_RO},
    { FLDATA (ADRCMP, user_addr_cmp, 0), REG_HRO},
#endif
#if KI | ITS | BBN
    { FLDATAD (PAGE_ENABLE, page_enable, 0, "Paging enabled")},
    { FLDATAD (PAGE_FAULT, page_fault, 0, "Page fault"), REG_RO},
    { ORDATAD (AC_STACK, ac_stack, 18, "AC Stack"), REG_RO},
    { ORDATAD (PAGE_RELOAD, pag_reload, 18, "Page reload"), REG_HRO},
    { ORDATAD (FAULT_DATA, fault_data, 36, "Page fault data"), REG_RO},
    { FLDATAD (TRP_FLG, trap_flag, 0, "Trap flag"), REG_HRO},
    { ORDATAD (LST_PAGE, last_page, 9, "Last page"), REG_HRO},
#endif
#if BBN
    { FLDATAD (EXEC_MAP, exec_map, 0, "Executive mapping"), REG_RO},
    { FLDATAD (NXT_WR, next_write, 0, "Map next write"), REG_RO},
    { ORDATAD (MON_BASE, mon_base_reg, 8, "Monitor base"), REG_RO},
    { ORDATAD (USER_BASE, user_base_reg, 8, "User base"), REG_RO},
    { ORDATAD (USER_LIMIT, user_limit, 3, "User limit"), REG_RO},
    { ORDATAD (PER_USER, pur, 36, "Per user data"), REG_RO},
#endif
#if ITS
    { ORDATAD (DBR1, dbr1, 18, "DB register 1")},
    { ORDATAD (DBR2, dbr2, 18, "DB register 2")},
    { ORDATAD (DBR3, dbr3, 18, "DB register 3")},
    { ORDATAD (JPC, jpc, 18, "Last Jump PC")},
    { ORDATAD (AGE, age, 4, "Age")},
    { ORDATAD (FAULT_ADDR, fault_addr, 18, "Fault address"), REG_RO},
    { ORDATAD (OPC, opc, 36, "Saved PC and flags")},
    { ORDATAD (MAR, mar, 18, "Memory address register")},
    { ORDATAD (QUA_TIME, qua_time, 36, "Quantum timer"), REG_RO},
#endif
    { NULL }
    };

MTAB cpu_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, 1, "16K", "16K", &cpu_set_size },
    { UNIT_MSIZE, 2, "32K", "32K", &cpu_set_size },
    { UNIT_MSIZE, 3, "48K", "48K", &cpu_set_size },
    { UNIT_MSIZE, 4, "64K", "64K", &cpu_set_size },
    { UNIT_MSIZE, 6, "96K", "96K", &cpu_set_size },
    { UNIT_MSIZE, 8, "128K", "128K", &cpu_set_size },
    { UNIT_MSIZE, 12, "196K", "196K", &cpu_set_size },
    { UNIT_MSIZE, 16, "256K", "256K", &cpu_set_size },
#if KI_22BIT|KI|ITS
    { UNIT_MSIZE, 32, "512K", "512K", &cpu_set_size },
    { UNIT_MSIZE, 48, "768K", "768K", &cpu_set_size },
    { UNIT_MSIZE, 64, "1024K", "1024K", &cpu_set_size },
#endif
#if KI_22BIT|KI|KL
    { UNIT_MSIZE, 128, "2048K", "2048K", &cpu_set_size },
    { UNIT_MSIZE, 256, "4096K", "4096K", &cpu_set_size },
#endif
#if KI|KL
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "SERIAL", "SERIAL",
          &cpu_set_serial, &cpu_show_serial, NULL, "CPU Serial Number" },
#endif
#if KA
    { UNIT_M_PAGE, 0, "ONESEG", "ONESEG", NULL, NULL, NULL,
             "One Relocation Register"},
    { UNIT_M_PAGE, UNIT_TWOSEG, "TWOSEG", "TWOSEG", NULL, NULL,
              NULL, "Two Relocation Registers"},
#if ITS
    { UNIT_M_PAGE, UNIT_ITSPAGE, "ITS", "ITS", NULL, NULL, NULL,
              "Paging hardware for ITS"},
#endif
#if BBN
    { UNIT_M_PAGE, UNIT_BBNPAGE, "BBN", "BBN", NULL, NULL, NULL,
              "Paging hardware for TENEX"},
#endif
#if WAITS
    { UNIT_M_WAITS, UNIT_WAITS, "WAITS", "WAITS", NULL, NULL, NULL,
              "Support for WAITS XCTR"},
    { UNIT_M_WAITS, 0, NULL, "NOWAITS", NULL, NULL, NULL,
              "No support for WAITS XCTR"},
#endif
#if MPX_DEV
    { UNIT_M_MPX, UNIT_MPX, "MPX", "MPX", NULL, NULL, NULL,
              "MPX Device for ITS"},
    { UNIT_M_MPX, 0, NULL, "NOMPX", NULL, NULL, NULL,
              "Disables the MPX device"},
#endif
    { UNIT_MAOFF, UNIT_MAOFF, "MAOFF", "MAOFF", NULL, NULL,
              NULL, "Interrupts relocated to 140"},
    { UNIT_MAOFF, 0, NULL, "NOMAOFF", NULL, NULL, NULL,
             "No interrupt relocation"},
#endif
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

/* Simulator debug controls */
DEBTAB              cpu_debug[] = {
    {"IRQ", DEBUG_IRQ, "Debug IRQ requests"},
    {"CONI", DEBUG_CONI, "Show coni instructions"},
    {"CONO", DEBUG_CONO, "Show coni instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
    {0, 0}
};

DEVICE cpu_dev = {
    "CPU", &cpu_unit[0], cpu_reg, cpu_mod,
    1, 8, 22, 1, 8, 36,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL, NULL, DEV_DEBUG, 0, cpu_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description
    };

/* Data arrays */
#define FCE     000001   /* Fetch memory into AR */
#define FCEPSE  000002   /* Fetch and store memory into AR */
#define SCE     000004   /* Save AR into memory */
#define FAC     000010   /* Copy AR to BR, then Fetch AC into AR */
#define FAC2    000020   /* Fetch AC+1 into MQ */
#define SAC     000040   /* Save AC into AR */
#define SACZ    000100   /* Save AC into AR if AC not 0 */
#define SAC2    000200   /* Save MQ into AC+1 */
#define SWAR    000400   /* Swap AR */
#define FBR     001000   /* Load AC into BR */

#if PDP6
#define P6(x) x
#define P10(x) 0
#else
#define P6(x) 0
#define P10(x) x
#endif

int opflags[] = {
        /* UUO Opcodes */
        /* UUO00 */       /* LUUO01 */      /* LUUO02 */    /* LUUO03 */
        0,                0,                0,              0,
        /* LUUO04 */      /* LUUO05 */      /* LUUO06 */    /* LUUO07 */
        0,                0,                0,              0,
        /* LUUO10 */      /* LUUO11 */      /* LUUO12 */    /* LUUO13 */
        0,                0,                0,              0,
        /* LUUO14 */      /* LUUO15 */      /* LUUO16 */    /* LUUO17 */
        0,                0,                0,              0,
        /* LUUO20 */      /* LUUO21 */      /* LUUO22 */    /* LUUO23 */
        0,                0,                0,              0,
        /* LUUO24 */      /* LUUO25 */      /* LUUO26 */    /* LUUO27 */
        0,                0,                0,              0,
        /* LUUO30 */      /* LUUO31 */      /* LUUO32 */    /* LUUO33 */
        0,                0,                0,              0,
        /* LUUO34 */      /* LUUO35 */      /* LUUO36 */    /* LUUO37 */
        0,                0,                0,              0,
        /* MUUO40 */      /* MUUO41 */      /* MUUO42 */    /* MUUO43 */
        0,                0,                0,              0,
        /* MUUO44 */      /* MUUO45 */      /* MUUO46 */    /* MUUO47 */
        0,                0,                0,              0,
        /* MUUO50 */      /* MUUO51 */      /* MUUO52 */    /* MUUO53 */
        0,                0,                0,              0,
        /* MUUO54 */      /* MUUO55 */      /* MUUO56 */    /* MUUO57 */
        0,                0,                0,              0,
        /* MUUO60 */      /* MUUO61 */      /* MUUO62 */    /* MUUO63 */
        0,                0,                0,              0,
        /* MUUO64 */      /* MUUO65 */      /* MUUO66 */    /* MUUO67 */
        0,                0,                0,              0,
        /* MUUO70 */      /* MUUO71 */      /* MUUO72 */    /* MUUO73 */
        0,                0,                0,              0,
        /* MUUO74 */      /* MUUO75 */      /* MUUO76 */    /* MUUO77 */
        0,                0,                0,              0,

        /* Double precsision math */
        /* UJEN */        /* UUO101 */      /* GFAD */      /* GFSB */
        0,                0,                0,              0,
        /* JSYS */        /* ADJSP */       /* GFMP */      /*GFDV */
        0,                0,                0,              0,
        /* DFAD */        /* DFSB */        /* DFMP */      /* DFDV */
        0,                0,                0,              0,
        /* DADD */        /* DSUB */        /* DMUL */      /* DDIV */
        0,                0,                0,              0,
        /* DMOVE */       /* DMOVN */       /* FIX */       /* EXTEND */
        0,                0,                0,              0,
        /* DMOVEM */      /* DMOVNM */      /* FIXR */      /* FLTR */
        0,                0,                0,              0,

        /* UFA */         /* DFN */         /* FSC */       /* IBP */
        P10(FCE|FBR),     P10(FCE|FAC),     FAC|SAC,        0,
        /* ILDB */        /* LDB */         /* IDPB */      /* DPB */
        0,                0,                0,              0,
        /* Floating point */
        /* FAD */         /* FADL */        /* FADM */      /* FADB */
        SAC|FCE|FBR,      SAC|SAC2|FCE|FBR, FCEPSE|FBR,     SAC|FBR|FCEPSE,
        /* FADR */        /* FADRI */       /* FADRM */     /* FADRB */
        SAC|FCE|FBR,      SAC|P6(SAC2|FCE)|P10(SWAR)|FBR,
                                            FCEPSE|FBR,     SAC|FBR|FCEPSE,
        /* FSB */         /* FSBL */        /* FSBM */      /* FSBB */
        SAC|FCE|FBR,      SAC|SAC2|FCE|FBR, FCEPSE|FBR,     SAC|FBR|FCEPSE,
        /* FSBR */        /* FSBRL */       /* FSBRM */     /* FSBRB */
        SAC|FCE|FBR,      SAC|P6(SAC2|FCE)|P10(SWAR)|FBR,
                                            FCEPSE|FBR,     SAC|FBR|FCEPSE,
        /* FMP */         /* FMPL */        /* FMPM */      /* FMPB */
        SAC|FCE|FBR,      SAC|SAC2|FCE|FBR, FCEPSE|FBR,     SAC|FBR|FCEPSE,
        /* FMPR */        /* FMPRI */       /* FMPRM */     /* FMPRB */
        SAC|FCE|FBR,      SAC|P6(SAC2|FCE)|P10(SWAR)|FBR,
                                             FCEPSE|FBR,     SAC|FBR|FCEPSE,
        /* FDV */         /* FDVL */        /* FDVM */      /* FDVB */
        SAC|FCE|FBR,      SAC|FAC2|SAC2|FCE|FBR, FCEPSE|FBR, SAC|FBR|FCEPSE,
        /* FDVR */        /* FDVRL */       /* FDVRM */     /* FDVRB */
        SAC|FCE|FBR,      SAC|P6(FAC2|SAC2|FCE)|P10(SWAR)|FBR,
                                           FCEPSE|FBR,     SAC|FBR|FCEPSE,

        /* Full word operators */
        /* MOVE */        /* MOVEI */       /* MOVEM */     /* MOVES */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* MOVS */        /* MOVSI */       /* MOVSM */     /* MOVSS */
        SWAR|SAC|FCE,     SWAR|SAC,         SWAR|FAC|SCE,   SWAR|SACZ|FCEPSE,
        /* MOVN */        /* MOVNI */       /* MOVNM */     /* MOVNS */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* MOVM */        /* MOVMI */       /* MOVMM */     /* MOVMS */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* IMUL */        /* IMULI */       /* IMULM */     /* IMULB */
        SAC|FCE|FBR,      SAC|FBR,          FCEPSE|FBR,     SAC|FCEPSE|FBR,
        /* MUL */         /* MULI */        /* MULM */      /* MULB */
        SAC2|SAC|FCE|FBR, SAC2|SAC|FBR,     FCEPSE|FBR,     SAC2|SAC|FCEPSE|FBR,
        /* IDIV */        /* IDIVI */       /* IDIVM */     /* IDIVB */
        SAC2|SAC|FCE|FAC, SAC2|SAC|FAC,     FCEPSE|FAC,     SAC2|SAC|FCEPSE|FAC,
        /* DIV */         /* DIVI */        /* DIVM */      /* DIVB */
        SAC2|SAC|FCE|FAC|FAC2, SAC2|SAC|FAC|FAC2,
                                          FCEPSE|FAC|FAC2, SAC2|SAC|FCEPSE|FAC\
                                                                 |FAC2,
        /* Shift operators */
        /* ASH */         /* ROT */         /* LSH */       /* JFFO */
        FAC|SAC,          FAC|SAC,          FAC|SAC,        FAC,
        /* ASHC */        /* ROTC */        /* LSHC */      /* UUO247 */
        FAC|SAC|SAC2|FAC2, FAC|SAC|SAC2|FAC2, FAC|SAC|SAC2|FAC2,  0,

        /* Branch operators */
        /* EXCH */        /* BLT */         /* AOBJP */     /* AOBJN */
        FAC|FCE,          FAC,              FAC|SAC,        FAC|SAC,
        /* JRST */        /* JFCL */        /* XCT */       /* MAP */
        0,                0,                0,              0,
        /* PUSHJ */       /* PUSH */        /* POP */       /* POPJ */
        FAC|SAC,          FAC|FCE|SAC,      FAC|SAC,        FAC|SAC,
        /* JSR */         /* JSP */         /* JSA */       /* JRA */
        0,                SAC,              FBR|SCE,        0,
        /* ADD */         /* ADDI */        /* ADDM */      /* ADDB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* SUB */         /* SUBI */        /* SUBM */      /* SUBB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,

        /* Compare operators */
        /* CAI */         /* CAIL */        /* CAIE */      /* CAILE */
        FBR,              FBR,              FBR,            FBR,
        /* CAIA */        /* CAIGE */       /* CAIN */      /* CAIG */
        FBR,              FBR,              FBR,            FBR,
        /* CAM */         /* CAML */        /* CAME */      /* CAMLE */
        FBR|FCE,          FBR|FCE,          FBR|FCE,        FBR|FCE,
        /* CAMA */        /* CAMGE */       /* CAMN */      /* CAMG */
        FBR|FCE,          FBR|FCE,          FBR|FCE,        FBR|FCE,

        /* Jump and skip operators */
        /* JUMP */        /* JUMPL */       /* JUMPE */     /* JUMPLE */
        FAC,              FAC,              FAC,            FAC,
        /* JUMPA */       /* JUMPGE */      /* JUMPN */     /* JUMPG */
        FAC,              FAC,              FAC,            FAC,
        /* SKIP */        /* SKIPL */       /* SKIPE */     /* SKIPLE */
        SACZ|FCE,         SACZ|FCE,         SACZ|FCE,       SACZ|FCE,
        /* SKIPA */       /* SKIPGE */      /* SKIPN */     /* SKIPG */
        SACZ|FCE,         SACZ|FCE,         SACZ|FCE,       SACZ|FCE,
        /* AOJ */         /* AOJL */        /* AOJE */      /* AOJLE */
        SAC|FAC,          SAC|FAC,          SAC|FAC,        SAC|FAC,
        /* AOJA */        /* AOJGE */       /* AOJN */      /* AOJG */
        SAC|FAC,          SAC|FAC,          SAC|FAC,        SAC|FAC,
        /* AOS */         /* AOSL */        /* AOSE */      /* AOSLE */
        SACZ|FCEPSE,      SACZ|FCEPSE,      SACZ|FCEPSE,    SACZ|FCEPSE,
        /* AOSA */        /* AOSGE */       /* AOSN */      /* AOSG */
        SACZ|FCEPSE,      SACZ|FCEPSE,      SACZ|FCEPSE,    SACZ|FCEPSE,
        /* SOJ */         /* SOJL */        /* SOJE */      /* SOJLE */
        SAC|FAC,          SAC|FAC,          SAC|FAC,        SAC|FAC,
        /* SOJA */        /* SOJGE */       /* SOJN */      /* SOJG */
        SAC|FAC,          SAC|FAC,          SAC|FAC,        SAC|FAC,
        /* SOS */         /* SOSL */        /* SOSE */      /* SOSLE */
        SACZ|FCEPSE,      SACZ|FCEPSE,      SACZ|FCEPSE,    SACZ|FCEPSE,
        /* SOSA */        /* SOSGE */       /* SOSN */      /* SOSG */
        SACZ|FCEPSE,      SACZ|FCEPSE,      SACZ|FCEPSE,    SACZ|FCEPSE,

        /* Boolean operators */
        /* SETZ */        /* SETZI */       /* SETZM */     /* SETZB */
        SAC,              SAC,              SCE,            SAC|SCE,
        /* AND */         /* ANDI */        /* ANDM */      /* ANDB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* ANDCA */       /* ANDCAI */      /* ANDCAM */    /* ANDCAB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* SETM */        /* SETMI */       /* SETMM */     /* SETMB */
        SAC|FCE,          SAC,              0,              SAC|FCE,
        /* ANDCM */       /* ANDCMI */      /* ANDCMM */    /* ANDCMB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* SETA */        /* SETAI */       /* SETAM */     /* SETAB */
        FBR|SAC,          FBR|SAC,          FBR|SCE,        FBR|SAC|SCE,
        /* XOR */         /* XORI */        /* XORM */      /* XORB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* IOR */         /* IORI */        /* IORM */      /* IORB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* ANDCB */       /* ANDCBI */      /* ANDCBM */    /* ANDCBB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* EQV */         /* EQVI */        /* EQVM */      /* EQVB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* SETCA */       /* SETCAI */      /* SETCAM */    /* SETCAB */
        FBR|SAC,          FBR|SAC,          FBR|SCE,        FBR|SAC|SCE,
        /* ORCA */        /* ORCAI */       /* ORCAM */     /* ORCAB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* SETCM */       /* SETCMI */      /* SETCMM */    /* SETCMB */
        SAC|FCE,          SAC,              FCEPSE,         SAC|FCEPSE,
        /* ORCM */        /* ORCMI */       /* ORCMM */     /* ORCMB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* ORCB */        /* ORCBI */       /* ORCBM */     /* ORCBB */
        FBR|SAC|FCE,      FBR|SAC,          FBR|FCEPSE,     FBR|SAC|FCEPSE,
        /* SETO */        /* SETOI */       /* SETOM */     /* SETOB */
        SAC,              SAC,              SCE,            SAC|SCE,

        /* Half word operators */
        /* HLL */         /* HLLI */        /* HLLM */      /* HLLS */
        FBR|SAC|FCE,      FBR|SAC,          FAC|FCEPSE,     SACZ|FCEPSE,
        /* HRL */         /* HRLI */        /* HRLM */      /* HRLS */
        SWAR|FBR|SAC|FCE, SWAR|FBR|SAC,     FAC|SWAR|FCEPSE,SACZ|FCEPSE,
        /* HLLZ */        /* HLLZI */       /* HLLZM */     /* HLLZS */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* HRLZ */        /* HRLZI */       /* HRLZM */     /* HRLZS */
        SWAR|SAC|FCE,     SWAR|SAC,         FAC|SWAR|SCE,   SWAR|SACZ|FCEPSE,
        /* HLLO */        /* HLLOI */       /* HLLOM */     /* HLLOS */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* HRLO */        /* HRLOI */       /* HRLOM */     /* HRLOS */
        SWAR|SAC|FCE,     SWAR|SAC,         FAC|SWAR|SCE,   SWAR|SACZ|FCEPSE,
        /* HLLE */        /* HLLEI */       /* HLLEM */     /* HLLES */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* HRLE */        /* HRLEI */       /* HRLEM */     /* HRLES */
        SWAR|SAC|FCE,     SWAR|SAC,         FAC|SWAR|SCE,   SWAR|SACZ|FCEPSE,
        /* HRR */         /* HRRI */        /* HRRM */      /* HRRS */
        FBR|SAC|FCE,      FBR|SAC,          FAC|FCEPSE,     SACZ|FCEPSE,
        /* HLR */         /* HLRI */        /* HLRM */      /* HLRS */
        SWAR|FBR|SAC|FCE, SWAR|FBR|SAC,     FAC|SWAR|FCEPSE,SACZ|FCEPSE,
        /* HRRZ */        /* HRRZI */       /* HRRZM */     /* HRRZS */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* HLRZ */        /* HLRZI */       /* HLRZM */     /* HLRZS */
        SWAR|SAC|FCE,     SWAR|SAC,         FAC|SWAR|SCE,   SWAR|SACZ|FCEPSE,
        /* HRRO */        /* HRROI */       /* HRROM */     /* HRROS */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* HLRO */        /* HLROI */       /* HLROM */     /* HLROS */
        SWAR|SAC|FCE,     SWAR|SAC,         FAC|SWAR|SCE,   SWAR|SACZ|FCEPSE,
        /* HRRE */        /* HRREI */       /* HRREM */     /* HRRES */
        SAC|FCE,          SAC,              FAC|SCE,        SACZ|FCEPSE,
        /* HLRE */        /* HLREI */       /* HLREM */     /* HLRES */
        SWAR|SAC|FCE,     SWAR|SAC,         FAC|SWAR|SCE,   SWAR|SACZ|FCEPSE,

        /* Test operators */
        /* TRN */         /* TLN */         /* TRNE */      /* TLNE */
        FBR,              FBR|SWAR,         FBR,            FBR|SWAR,
        /* TRNA */        /* TLNA */        /* TRNN */      /* TLNN */
        FBR,              FBR|SWAR,         FBR,            FBR|SWAR,
        /* TDN */         /* TSN */         /* TDNE */      /* TSNE */
        FBR|FCE,          FBR|SWAR|FCE,     FBR|FCE,        FBR|SWAR|FCE,
        /* TDNA */        /* TSNA */        /* TDNN */      /* TSNN */
        FBR|FCE,          FBR|SWAR|FCE,     FBR|FCE,        FBR|SWAR|FCE,
        /* TRZ */         /* TLZ */         /* TRZE */      /* TLZE */
        FBR|SAC,          FBR|SWAR|SAC,     FBR|SAC,        FBR|SWAR|SAC,
        /* TRZA */        /* TLZA */        /* TRZN */      /* TLZN */
        FBR|SAC,          FBR|SWAR|SAC,     FBR|SAC,        FBR|SWAR|SAC,
        /* TDZ */         /* TSZ */         /* TDZE */      /* TSZE */
        FBR|SAC|FCE,      FBR|SWAR|SAC|FCE, FBR|SAC|FCE,    FBR|SWAR|SAC|FCE,
        /* TDZA */        /* TSZA */        /* TDZN */      /* TSZN */
        FBR|SAC|FCE,      FBR|SWAR|SAC|FCE, FBR|SAC|FCE,    FBR|SWAR|SAC|FCE,
        /* TRC */         /* TLC */         /* TRCE */      /* TLCE */
        FBR|SAC,          FBR|SWAR|SAC,     FBR|SAC,        FBR|SWAR|SAC,
        /* TRCA */        /* TLCA */        /* TRCN */      /* TLCN */
        FBR|SAC,          FBR|SWAR|SAC,     FBR|SAC,        FBR|SWAR|SAC,
        /* TDC */         /* TSC */         /* TDCE */      /* TSCE */
        FBR|SAC|FCE,      FBR|SWAR|SAC|FCE, FBR|SAC|FCE,    FBR|SWAR|SAC|FCE,
        /* TDCA */        /* TSCA */        /* TDCN */      /* TSCN */
        FBR|SAC|FCE,      FBR|SWAR|SAC|FCE, FBR|SAC|FCE,    FBR|SWAR|SAC|FCE,
        /* TRO */         /* TLO */         /* TROE */      /* TLOE */
        FBR|SAC,          FBR|SWAR|SAC,     FBR|SAC,        FBR|SWAR|SAC,
        /* TROA */        /* TLOA */        /* TRON */      /* TLON */
        FBR|SAC,          FBR|SWAR|SAC,     FBR|SAC,        FBR|SWAR|SAC,
        /* TDO */         /* TSO */         /* TDOE */      /* TSOE */
        FBR|SAC|FCE,      FBR|SWAR|SAC|FCE, FBR|SAC|FCE,    FBR|SWAR|SAC|FCE,
        /* TDOA */        /* TSOA */        /* TDON */      /* TSON */
        FBR|SAC|FCE,      FBR|SWAR|SAC|FCE, FBR|SAC|FCE,    FBR|SWAR|SAC|FCE,

        /* IOT  Instructions */
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
        0,                0,                0,              0,
};

#if PDP6
#define PC_CHANGE       FLAGS |= PCHNG; check_apr_irq();
#else
#define PC_CHANGE
#endif
#define SWAP_AR         ((RMASK & AR) << 18) | ((AR >> 18) & RMASK)
#define SMEAR_SIGN(x)   x = ((x) & SMASK) ? (x) | EXPO : (x) & MANT
#define GET_EXPO(x)     ((((x) & SMASK) ? 0377 : 0 )  \
                                        ^ (((x) >> 27) & 0377))
#if KI
#define AOB(x)          ((x + 1) & RMASK) | ((x + 01000000LL) & (C1|LMASK))
#define SOB(x)          ((x + RMASK) & RMASK) | ((x + LMASK) & (C1|LMASK));
#else
#define AOB(x)          (x + 01000001LL)
#define SOB(x)          (x + 0777776777777LL)
#endif
#if ITS
#define QITS            (cpu_unit[0].flags & UNIT_ITSPAGE)
#define QTEN11          (ten11_unit[0].flags & UNIT_ATT)
#define QAUXCPU         (auxcpu_unit[0].flags & UNIT_ATT)
#else
#define QITS            0
#endif
#if BBN
#define QBBN            (cpu_unit[0].flags & UNIT_BBNPAGE)
#else
#define QBBN            0
#endif
#if WAITS
#define QWAITS          (cpu_unit[0].flags & UNIT_WAITS)
#else
#define QWAITS          0
#endif

#if ITS
/*
 * Set quantum clock to qua_time.
 */

void
set_quantum()
{
    sim_cancel(&cpu_unit[1]);
    if ((qua_time & RSIGN) == 0) {
        double us;
        us = (double)(RSIGN - qua_time);
        (void)sim_activate_after_d(&cpu_unit[1], us);
    }
}

/*
 * Update the qua_time variable.
 */
void
load_quantum()
{
    if (sim_is_active(&cpu_unit[1])) {
       double us;
       us = sim_activate_time_usecs (&cpu_unit[1]);
       qua_time = RSIGN - (uint32)us;
       sim_cancel(&cpu_unit[1]);
    }
}

/*
 * Get the current quantum time.
 */
uint32
get_quantum()
{
    uint32  t = 0;
    if (sim_is_active(&cpu_unit[1])) {
       double us;
       us = sim_activate_time_usecs (&cpu_unit[1]);
       t = RSIGN - (uint32)us;
    }
    return t;
}
#endif

/*
 * Set device to interrupt on a given level 1-7
 * Level 0 means that device interrupt is not enabled
 */
void set_interrupt(int dev, int lvl) {
    lvl &= 07;
    if (lvl) {
       dev_irq[dev>>2] = 0200 >> lvl;
       pi_pending = 1;
       sim_debug(DEBUG_IRQ, &cpu_dev, "set irq %o %o %03o %03o %03o\n",
              dev & 0774, lvl, PIE, PIR, PIH);
    }
}

#if MPX_DEV
void set_interrupt_mpx(int dev, int lvl, int mpx) {
    lvl &= 07;
    if (lvl) {
       dev_irq[dev>>2] = 0200 >> lvl;
       if (lvl == 1 && mpx != 0)
          dev_irq[dev>>2] |= mpx << 8;
       pi_pending = 1;
       sim_debug(DEBUG_IRQ, &cpu_dev, "set mpx irq %o %o %o %03o %03o %03o\n",
              dev & 0774, lvl, mpx, PIE, PIR, PIH);
    }
}
#endif

/*
 * Clear the interrupt flag for a device
 */
void clr_interrupt(int dev) {
    dev_irq[dev>>2] = 0;
    if (dev > 4)
        sim_debug(DEBUG_IRQ, &cpu_dev, "clear irq %o\n", dev & 0774);
}

/*
 * Check if there is any pending interrupts return 0 if none,
 * else set pi_enc to highest level and return 1.
 */
int check_irq_level() {
     int i, lvl;
     int pi_req;

     /* If PXCT don't check for interrupts */
     if (xct_flag != 0)
         return 0;
     check_apr_irq();

     /* If not enabled, check if any pending Processor IRQ */
     if (pi_enable == 0) {
#if !PDP6
        if (PIR != 0) {
            pi_enc = 1;
            for(lvl = 0100; lvl != 0; lvl >>= 1) {
                if (lvl & PIH)
                   break;
                if (PIR & lvl)
                   return 1;
                pi_enc++;
            }
        }
#endif
        return 0;
     }

     /* Scan all devices */
     for(i = lvl = 0; i < 128; i++)
        lvl |= dev_irq[i];
     if (lvl == 0)
        pi_pending = 0;
     pi_req = (lvl & PIE) | PIR;
#if MPX_DEV
     /* Check if interrupt on PI channel 1 */
     if (mpx_enable && cpu_unit[0].flags & UNIT_MPX &&
                 (pi_req & 0100) && (PIH & 0100) == 0) {
         pi_enc = 010;
         for(i = lvl = 0; i < 128; i++) {
             int l = dev_irq[i] >> 8;
             if (dev_irq[i] & 0100 && l != 0 && l < pi_enc)
                 pi_enc = l;
         }
         if (pi_enc != 010) {
            pi_enc += 010;
            return 1;
         }
     }
#endif
     /* Handle held interrupt requests */
     i = 1;
     for(lvl = 0100; lvl != 0; lvl >>= 1, i++) {
         if (lvl & PIH)
            break;
         if (pi_req & lvl) {
            pi_enc = i;
            return 1;
         }
     }
     return 0;
}

/*
 * Recover from held interrupt.
 */
void restore_pi_hold() {
     int lvl;

     if (!pi_enable)
        return;
     /* Clear HOLD flag for highest interrupt */
     for(lvl = 0100; lvl != 0; lvl >>= 1) {
        if (lvl & PIH) {
            PIR &= ~lvl;
            sim_debug(DEBUG_IRQ, &cpu_dev, "restore irq %o %03o\n", lvl, PIH);
            PIH &= ~lvl;
            break;
         }
     }
     pi_pending = 1;
}

/*
 * Hold interrupts at the current level.
 */
void set_pi_hold() {
     int pi = pi_enc;
#if MPX_DEV
     if (mpx_enable && cpu_unit[0].flags & UNIT_MPX && pi > 07)
        pi = 1;
#endif
     PIR &= ~(0200 >> pi);
     if (pi_enable)
        PIH |= (0200 >> pi);
}

/*
 * PI device for KA and KI
 */
t_stat dev_pi(uint32 dev, uint64 *data) {
    uint64 res = 0;
    switch(dev & 3) {
    case CONO:
        /* Set PI flags */
        res = *data;
        if (res & 010000) { /* Bit 23 */
           PIR = PIH = PIE = 0;
           pi_enable = 0;
#if MPX_DEV
           mpx_enable = 0;
#endif
           parity_irq = 0;
        }
        if (res & 0200) {  /* Bit 28 */
           pi_enable = 1;
        }
        if (res & 0400)    /* Bit 27 */
           pi_enable = 0;
        if (res & 01000)   /* Bit 26 */
           PIE &= ~(*data & 0177);
        if (res & 02000)   /* Bit 25 */
           PIE |= (*data & 0177);
        if (res & 04000) { /* Bit 24 */
           PIR |= (*data & 0177);
           pi_pending = 1;
        }
#if MPX_DEV
        if (res & 020000 && cpu_unit[0].flags & UNIT_MPX)
           mpx_enable = 1;
#endif
#if KI
        if (res & 020000) { /* Bit 22 */
           PIR &= ~(*data & 0177);
        }
#endif
        if (res & 040000)   /* Bit 21 */
           parity_irq = 1;
        if (res & 0100000)  /* Bit 20 */
           parity_irq = 0;
        check_apr_irq();
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO PI %012llo\n", *data);
        break;

     case CONI:
        res = PIE;
        res |= (pi_enable << 7);
        res |= (PIH << 8);
#if KI
        res |= ((uint64)(PIR) << 18);
#endif
        res |= (parity_irq << 15);
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI PI %012llo\n", *data);
        break;

    case DATAO:
        MI = *data;
#ifdef PANDA_LIGHTS
        /* Set lights */
        ka10_lights_main (*data);
#endif
        break;

    case DATAI:
        break;
    }
    return SCPE_OK;
}

/*
 * Non existent device
*/
t_stat null_dev(uint32 dev, uint64 *data) {
    switch(dev & 3) {
    case CONI:
    case DATAI:
         *data = 0;
         break;

    case CONO:
    case DATAO:
         break;
    }
    return SCPE_OK;
}


#if KI
static int      timer_irq, timer_flg;

/*
 * Page device for KI10.
 */
t_stat dev_pag(uint32 dev, uint64 *data) {
    uint64 res = 0;
    int    i;
    switch(dev & 03) {
    case CONI:
        /* Complement of vpn */
        *data = (uint64)(pag_reload ^ 040);
        *data |= ((uint64)last_page) << 8;
        *data |= (uint64)((apr_serial == -1) ? DEF_SERIAL : apr_serial) << 26;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI PAG %012llo\n", *data);
        break;

     case CONO:
        /* Set Stack AC and Page Table Reload Counter */
        ac_stack = (*data >> 9) & 0760;
        pag_reload = (*data & 037) | (pag_reload & 040);
        sim_debug(DEBUG_CONO, &cpu_dev, "CONI PAG %012llo\n", *data);
        break;

    case DATAO:
        res = *data;
        if (res & RSIGN) {
            eb_ptr = (res & 017777) << 9;
            for (i = 0; i < 512; i++)
               e_tlb[i] = u_tlb[i] = 0;
            for (;i < 546; i++)
               u_tlb[i] = 0;
            page_enable = (res & 020000) != 0;
        }
        if (res & SMASK) {
            ub_ptr = ((res >> 18) & 017777) << 9;
            for (i = 0; i < 512; i++)
               e_tlb[i] = u_tlb[i] = 0;
            for (;i < 546; i++)
               u_tlb[i] = 0;
            user_addr_cmp = (res & 00020000000000LL) != 0;
            small_user =    (res & 00040000000000LL) != 0;
            fm_sel = (uint8)(res >> 29) & 060;
       }
       pag_reload = 0;
       sim_debug(DEBUG_DATAIO, &cpu_dev,
                    "DATAO PAG %012llo ebr=%06o ubr=%06o\n",
                    *data, eb_ptr, ub_ptr);
       break;

    case DATAI:
       res = (eb_ptr >> 9);
       if (page_enable)
           res |= 020000;
       res |= ((uint64)(ub_ptr)) << 9;
       if (user_addr_cmp)
           res |= 00020000000000LL;
       if (small_user)
           res |= 00040000000000LL;
       res |= ((uint64)(fm_sel)) << 29;
       *data = res;
       sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAI PAG %012llo\n", *data);
       break;
    }
    return SCPE_OK;
}

/*
 * Check if the last operation caused a APR IRQ to be generated.
 */
void check_apr_irq() {
     if (pi_enable && apr_irq) {
         int flg = 0;
         clr_interrupt(0);
         flg |= inout_fail | nxm_flag;
         if (flg)
             set_interrupt(0, apr_irq);
     }
     if (pi_enable && clk_en && clk_flg)
         set_interrupt(4, clk_irq);
}


/*
 * APR device for KI10.
 */
t_stat dev_apr(uint32 dev, uint64 *data) {
    uint64 res = 0;
    switch(dev & 03) {
    case CONI:
        /* Read trap conditions */
        res = clk_irq | (apr_irq << 3) | (nxm_flag << 6);
        res |= (inout_fail << 7) | (clk_flg << 9) | (clk_en << 10);
        res |= (timer_irq << 14) | (parity_irq << 15) | (timer_flg << 17);
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI APR %012llo\n", *data);
        break;

     case CONO:
        /* Set trap conditions */
        res = *data;
        clk_irq = res & 07;
        apr_irq = (res >> 3) & 07;
        if (res & 0000100)
            nxm_flag = 0;
        if (res & 0000200)
            inout_fail = 0;
        if (res & 0001000) {
            clk_flg = 0;
            clr_interrupt(4);
        }
        if (res & 0002000) {
            clk_en = 1;
            if (clk_flg)
               set_interrupt(4, clk_irq);
        }
        if (res & 0004000) {
            clk_en = 0;
            clr_interrupt(4);
        }
        if (res & 0040000)
            timer_irq = 1;
        if (res & 0100000)
            timer_irq = 0;
        if (res & 0200000)
            reset_all(1);
        if (res & 0400000)
            timer_flg = 0;
        check_apr_irq();
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO APR %012llo\n", *data);
        break;

    case DATAO:
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAO APR %012llo\n", *data);
        break;

    case DATAI:
        /* Read switches */
        *data = SW;
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAI APR %012llo\n", *data);
        break;
    }
    return SCPE_OK;
}

#endif

#if KA

#if BBN
t_stat dev_pag(uint32 dev, uint64 *data) {
    uint64 res = 0;
    int    i;
    int    page_limit[] = {
        01000, 0040, 0100, 0140, 0200, 0240, 0300, 0340};
    switch(dev & 03) {
    case CONI:
        break;

     case CONO:
        switch (*data & 07) {
        case 0:  /* Clear page tables, reload from 71 & 72 */
                 for (i = 0; i < 512; i++)
                    e_tlb[i] = u_tlb[i] = 0;
                 res = M[071];
                 mon_base_reg = (res & 03777) << 9;
                 ac_stack = (res >> 9) & 0760;
                 user_base_reg = (res >> 9) & 03777000;
                 user_limit = page_limit[(res >> 30) & 07];
                 pur = M[072];
                 break;

        case 1:  /* Clear exec mapping */
                 for (i = 0; i < 512; i++)
                    e_tlb[i] = 0;
                 break;

        case 2:  /* Clear mapping for next write */
                 next_write = 1;
                 break;

        case 3:  /* Clear user mapping */
                 for (i = 0; i < 512; i++)
                     u_tlb[i] = 0;
                 break;

        case 4:  /* Turn off pager */
        case 5:  /* same as 4 */
                 page_enable = 0;
                 break;

        case 6:  /* Pager on, no resident mapping */
                 page_enable = 1;
                 exec_map = 0;
                 break;

        case 7:  /* Pager on, resident mapping */
                 page_enable = 1;
                 exec_map = 1;
                 break;
        }
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO PAG %012llo\n", *data);
        break;

    case DATAO:
       break;

    case DATAI:
       break;
    }
    return SCPE_OK;
}
#endif

/*
 * Check if the last operation caused a APR IRQ to be generated.
 */
void check_apr_irq() {
     if (pi_enable && apr_irq) {
         int flg = 0;
         clr_interrupt(0);
         flg |= ((FLAGS & OVR) != 0) & ov_irq;
         flg |= ((FLAGS & FLTOVR) != 0) & fov_irq;
         flg |= nxm_flag | mem_prot | push_ovf;
         if (flg)
             set_interrupt(0, apr_irq);
     }
}


/*
 * APR Device for KA10.
 */
t_stat dev_apr(uint32 dev, uint64 *data) {
    uint64 res = 0;
    switch(dev & 03) {
    case CONI:
        /* Read trap conditions */
        /* 000007 33-35 PIA */
        /* 000010 32 Overflow * */
        /* 000020 31 Overflow enable */
        /* 000040 30 Trap offset */
        /* 000100 29 Floating overflow * */
        /* 000200 28 Floating overflow enable */
        /* 000400 27 */
        /* 001000 26 Clock * */
        /* 002000 25 Clock enable */
        /* 004000 24 */
        /* 010000 23 NXM * */
        /* 020000 22 Memory protection * */
        /* 040000 21 Address break * */
        /* 100000 20 User In-Out */
        /* 200000 19 Push overflow * */
        /* 400000 18 */
        res = apr_irq | (((FLAGS & OVR) != 0) << 3) | (ov_irq << 4) ;
        res |= (((FLAGS & FLTOVR) != 0) << 6) | (fov_irq << 7) ;
        res |= (clk_flg << 9) | (((uint64)clk_en) << 10) | (nxm_flag << 12);
        res |= (mem_prot << 13) | (((FLAGS & USERIO) != 0) << 15);
        res |= (push_ovf << 16) | (maoff >> 1);
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI APR %012llo\n", *data);
        break;

     case CONO:
        /* Set trap conditions */
        res = *data;
        clk_irq = apr_irq = res & 07;
        clr_interrupt(0);
        if (res & 010)
            FLAGS &= ~OVR;
        if (res & 020)
            ov_irq = 1;
        if (res & 040)
            ov_irq = 0;
        if (res & 0100)
            FLAGS &= ~FLTOVR;
        if (res & 0200)
            fov_irq = 1;
        if (res & 0400)
            fov_irq = 0;
        if (res & 0001000) {
            clk_flg = 0;
            clr_interrupt(4);
        }
        if (res & 0002000) {
            clk_en = 1;
            if (clk_flg)
               set_interrupt(4, clk_irq);
        }
        if (res & 0004000) {
            clk_en = 0;
            clr_interrupt(4);
        }
        if (res & 010000)
            nxm_flag = 0;
        if (res & 020000)
            mem_prot = 0;
        if (res & 0200000) {
#if MPX_DEV
            mpx_enable = 0;
#endif
#if BBN
            if (QBBN)
               exec_map = 0;
#endif
            reset_all(1);
        }
        if (res & 0400000)
            push_ovf = 0;
        check_apr_irq();
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO APR %012llo\n", *data);
        break;

    case DATAO:
        /* Set protection registers */
        Rh = (0377 & (*data >> 1)) << 10;
        Rl = (0377 & (*data >> 10)) << 10;
        Pflag = 01 & (*data >> 18);
        Ph = ((0377 & (*data >> 19)) << 10) + 01777;
        Pl = ((0377 & (*data >> 28)) << 10) + 01777;
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAO APR %012llo\n", *data);
sim_debug(DEBUG_DATAIO, &cpu_dev, "Rl=%06o Pl=%06o, Rh=%06o, Ph=%06o\n", Rl, Pl, Rh, Ph);
        break;

    case DATAI:
        /* Read switches */
        *data = SW;
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAI APR %012llo\n", *data);
        break;
    }
    return SCPE_OK;
}
#endif


#if KI
/*
 * Handle page lookup on KI10
 *
 * addr is address to look up.
 * flag is set for pi cycle and user overide.
 * loc  is final address.
 * wr   indicates whether cycle is read or write.
 * cur_context is set when access should ignore xct_flag
 * fetch is set for instruction fetches.
 */
int page_lookup(int addr, int flag, int *loc, int wr, int cur_context, int fetch) {
    uint64   data;
    int      base = 0;
    int      page = (RMASK & addr) >> 9;
    int      uf = (FLAGS & USER) != 0;
    int      upmp = 0;

    if (page_fault)
        return 0;

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* If fetching byte data, use write access */
    if (BYF5 && (IR & 06) == 6)
        wr = 1;

    /* If this is modify instruction use write access */
    wr |= modify;

    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else if (xct_flag != 0 && !cur_context && !uf) {
             if (((xct_flag & 2) != 0 && wr != 0) ||
                 ((xct_flag & 1) != 0 && (wr == 0 || modify))) {
                 uf = (FLAGS & USERIO) != 0;
             }
    }

    /* If user, check if small user enabled */
    if (uf) {
        if (small_user && (page & 0340) != 0) {
            fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 27) | 020LL;
            page_fault = 1;
            return 0;
        }
    } else {
        /* Handle system mapping */
        /* Pages 340-377 via UBR */
        if ((page & 0740) == 0340) {
            page += 01000 - 0340;
            upmp = 1;
        /* Pages 400-777 via EBR */
        } else if (page & 0400) {
            base = 1;
        /* Pages 000-037 direct map */
        } else {
            /* Check if supervisory mode */
            *loc = addr;
            /* If PUBLIC and private page, make sure we are fetching a Portal */
            if (!flag && ((FLAGS & PUBLIC) != 0) &&
                (!fetch || (M[addr] & 00777040000000LL) != 0254040000000LL)) {
               /* Handle public violation */
                fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 27)
                                      | 021LL;
                page_fault = 1;
                return !wr;
            }
            return 1;
        }
    }
    /* Map the page */
    if (base) {
        data = e_tlb[page];
        if (data == 0) {
           data = M[eb_ptr + (page >> 1)];
           e_tlb[page & 0776] = RMASK & (data >> 18);
           e_tlb[page | 1] = RMASK & data;
           data = e_tlb[page];
           pag_reload = ((pag_reload + 1) & 037) | 040;
        }
        last_page = ((page ^ 0777) << 1)|1;
    } else {
        data = u_tlb[page];
        if (data == 0) {
           data = M[ub_ptr + (page >> 1)];
           u_tlb[page & 01776] = RMASK & (data >> 18);
           u_tlb[page | 1] = RMASK & data;
           data = u_tlb[page];
           pag_reload = ((pag_reload + 1) & 037) | 040;
        }
        if (upmp)
           last_page = (((page-0440) ^ 0777) << 1) | 1;
        else
           last_page = ((page ^ 0777) << 1);
    }
    *loc = ((data & 017777) << 9) + (addr & 0777);

    /* Check for access error */
    if ((data & RSIGN) == 0 || (wr & ((data & 0100000) == 0))) {
        page = (RMASK & addr) >> 9;
        fault_data = ((((uint64)(page))<<18) | ((uint64)(uf) << 27)) & LMASK;
        fault_data |= (data & 0400000) ? 010LL : 0LL;   /* A */
        fault_data |= (data & 0100000) ? 004LL : 0LL;   /* W */
        fault_data |= (data & 0040000) ? 002LL : 0LL;   /* S */
        fault_data |= wr;
        page_fault = 1;
        return 0;
    }

    /* If PUBLIC and private page, make sure we are fetching a Portal */
    if (!flag && ((FLAGS & PUBLIC) != 0) && ((data & 0200000) == 0) &&
         (!fetch || (M[*loc] & 00777040000000LL) != 0254040000000LL)) {
        /* Handle public violation */
        fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 27) | 021LL;
        page_fault = 1;
        return 0;
    }

    /* If fetching from public page, set public flag */
    if (fetch && ((data & 0200000) != 0))
        FLAGS |= PUBLIC;
    return 1;
}

/*
 * Register access on KI 10
 */
uint64 get_reg(int reg) {
    if (FLAGS & USER)
       return FM[fm_sel|(reg & 017)];
    else
       return FM[reg & 017];
}

void   set_reg(int reg, uint64 value) {
    if (FLAGS & USER)
        FM[fm_sel|(reg & 017)] = value;
    else
        FM[reg & 017] = value;
}

/*
 * Read a location directly from memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_read_nopage() {
    if (AB < 020) {
        MB =  FM[AB];
    } else {
        sim_interval--;
        if (AB >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        MB = M[AB];
    }
    return 0;
}

/*
 * Write a directly to a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_write_nopage() {

    if (AB < 020) {
        FM[AB] = MB;
    } else {
        sim_interval--;
        if (AB >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        M[AB] = MB;
    }
    return 0;
}

int Mem_read(int flag, int cur_context, int fetch) {
    int addr;

    if (AB < 020) {
        if (FLAGS & USER) {
           MB =  get_reg(AB);
           return 0;
        } else {
            if (!cur_context && ((xct_flag & 1) != 0)) {
               if (FLAGS & USERIO) {
                  if (fm_sel == 0)
                     goto read;
                  MB = FM[fm_sel|AB];
                  return 0;
               }
               MB = M[ub_ptr + ac_stack + AB];
               return 0;
            }
        }
        MB = get_reg(AB);
    } else {
read:
        sim_interval--;
        if (!page_lookup(AB, flag, &addr, 0, cur_context, fetch))
            return 1;
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        MB = M[addr];
    }
    return 0;
}

int Mem_write(int flag, int cur_context) {
    int addr;

    if (AB < 020) {
        if (FLAGS & USER) {
            set_reg(AB, MB);
            return 0;
        } else {
            if (!cur_context &&
                (((xct_flag & 1) != 0 && modify) ||
                      (xct_flag & 2) != 0)) {
                if (FLAGS & USERIO) {
                   if (fm_sel == 0)
                      goto write;
                   else
                      FM[fm_sel|AB] = MB;
                } else {
                   M[ub_ptr + ac_stack + AB] = MB;
                }
                return 0;
            }
        }
        set_reg(AB, MB);
    } else {
write:
        sim_interval--;
        if (!page_lookup(AB, flag, &addr, 1, cur_context, 0))
            return 1;
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        M[addr] = MB;
    }
    return 0;
}
#endif

#if KA

#define get_reg(reg)                 FM[(reg) & 017]
#define set_reg(reg, value)          FM[(reg) & 017] = value

#if ITS

/*
 * Load TBL entry for ITS.
 */
int its_load_tlb(uint32 reg, int page, uint32 *tlb) {
    uint64 data;
    int len = (reg >> 19) & 0177;
    int entry = (reg & 01777777) + ((page & 0377) >> 1);
    if ((page >> 1) > len) {
       fault_data |= 0200;
       return 1;
    }
    if (entry > (int)MEMSIZE) {
        nxm_flag = 1;
        fault_data |= 0400;
        return 1;
    }
    data = M[entry];
    if (page & 1) {
        data &= ~036000LL;
        data |= ((uint64)(age & 017)) << 10;
    } else {
        data &= ~(036000LL << 18);
        data |= ((uint64)(age & 017)) << (10+18);
    }
    M[entry] = data;
    if ((page & 1) == 0)
        data >>= 18;
    data &= RMASK;
    *tlb = (uint32)data;
    pag_reload = ((pag_reload + 1) & 017);
    return 0;
}

/*
 * Translation logic for KA10
 */

int page_lookup_its(int addr, int flag, int *loc, int wr, int cur_context, int fetch) {
    uint64   data;
    int      base = 0;
    int      page = (RMASK & addr) >> 10;
    int      acc;
    int      uf = (FLAGS & USER) != 0;
    int      ofd = (int)fault_data;

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* If fetching byte data, use write access */
    if (BYF5 && (IR & 06) == 6)
        wr = 1;

    /* If this is modify instruction use write access */
    wr |= modify;

    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else if (xct_flag != 0 && !cur_context && !uf) {
             if (((xct_flag & 2) != 0 && wr != 0) ||
                 ((xct_flag & 1) != 0 && (wr == 0 || modify))) {
                 uf = 1;
             }
    }

    /* AC & 1 = ??? */
    /* AC & 2 = Read User */
    /* AC & 4 = Write User */
    /* AC & 8 = Inhibit mem protect, skip */

    /* Add in MAR checking */
    if (addr == (mar & RMASK)) {
       switch((mar >> 18) & 03) {
       case 0: break;
       case 1: if (fetch) {
                  mem_prot = 1;
                  fault_data |= 2;
               }
               break;
       case 2: if (!wr)
                  break;
               /* Fall through */
       case 3: mem_prot = 1;
               fault_data |= 2;
               break;
       }
    }


    /* Map the page */
    if (!uf) {
        /* Handle system mapping */
        if ((page & 0200) == 0 || (fault_data & 04) == 0) {
        /* Direct map 0-377 or all if bit 2 off */
            *loc = addr;
            return 1;
        }
        data = e_tlb[page - 0200];
        if (data == 0) {
            if (its_load_tlb(dbr3, page - 0200, &e_tlb[page - 0200]))
                goto fault;
            data = e_tlb[page - 0200];
        }
    } else {
        data = u_tlb[page];
        if (data == 0) {
            if (page & 0200) {
                if (its_load_tlb(dbr2, page - 0200, &u_tlb[page]))
                   goto fault;
            } else {
                if (its_load_tlb(dbr1, page, &u_tlb[page]))
                   goto fault;
            }
            data = u_tlb[page];
        }
    }
    *loc = ((data & 01777) << 10) + (addr & 01777);
    acc = (data >> 16) & 03;

    /* Access check logic */
    switch(acc) {
    case 0:                     /* No access */
           fault_data |= 0010;
           break;
    case 1:                     /* Read Only Access */
           if (!wr)
               return 1;
           if ((fault_data & 00770) == 0)
               fault_data |= 0100;
           break;
    case 2:                     /* Read write first */
           if (fetch && (FLAGS & PURE)) {
               fault_data |= 0020;
               break;
           }
           if (!wr)            /* Read is OK */
               return 1;
           if ((fault_data & 00770) == 0)
               fault_data |= 040;
           break;
    case 3:                    /* All access */
           if (fetch && (FLAGS & PURE)) {
               fault_data |= 0020;
               break;
           }
           return 1;
    }
fault:
    /* Update fault data, fault address only if new fault */
    if ((ofd & 00770) == 0)
        fault_addr = (page) | ((uf)? 0400 : 0) | ((data & 01777) << 9);
    if ((xct_flag & 04) == 0) {
        mem_prot = 1;
        fault_data |= 01000;
    } else {
        PC = (PC + 1) & RMASK;
    }
    return 0;
}

/*
 * Read a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_read_its(int flag, int cur_context, int fetch) {
    int addr;

    if (AB < 020) {
        if ((xct_flag & 1) != 0 && !cur_context && (FLAGS & USER) == 0) {
           MB = M[(ac_stack & 01777777) + AB];
           return 0;
        }
        MB = get_reg(AB);
    } else {
        sim_interval--;
        if (!page_lookup_its(AB, flag, &addr, 0, cur_context, fetch))
            return 1;
#if NUM_DEVS_TEN11 > 0
        if (T11RANGE(addr) && QTEN11) {
            if (ten11_read (addr, &MB)) {
                nxm_flag = 1;
                return 1;
            }
            return 0;
        }
#endif
#if NUM_DEVS_AUXCPU > 0
        if (AUXCPURANGE(addr) && QAUXCPU) {
            if (auxcpu_read (addr, &MB)) {
                nxm_flag = 1;
                return 1;
            }
            return 0;
        }
#endif
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        MB = M[addr];
    }
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_write_its(int flag, int cur_context) {
    int addr;

    if (AB < 020) {
        if ((xct_flag & 2) != 0 && !cur_context && (FLAGS & USER) == 0) {
            M[(ac_stack & 01777777) + AB] = MB;
            return 0;
        }
        set_reg(AB, MB);
    } else {
        sim_interval--;
        if (!page_lookup_its(AB, flag, &addr, 1, cur_context, 0))
            return 1;
#if NUM_DEVS_TEN11 > 0
        if (T11RANGE(addr) && QTEN11) {
            if (ten11_write (addr, MB)) {
                nxm_flag = 1;
                return 1;
            }
            return 0;
        }
#endif
#if NUM_DEVS_AUXCPU > 0
        if (AUXCPURANGE(addr) && QAUXCPU) {
            if (auxcpu_write (addr, MB)) {
                nxm_flag = 1;
                return 1;
            }
            return 0;
        }
#endif
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        M[addr] = MB;
    }
    return 0;
}
#endif

#if BBN
int page_lookup_bbn(int addr, int flag, int *loc, int wr, int cur_context, int fetch) {
      /* Group 0, 01 = 00
                  bit 2 = Age 00x                                        0100000
                  bit 3 = Age 02x                                        0040000
                  bit 4 = Age 04x                                        0020000
                  bit 5 = Age 06x                                        0010000
                  bit 6 = Monitor after loading AR trap                  0004000 */
       /* Group 1, 01 = 01                                               0200000
                  bit 3 = Shared page not in core                        0040000
                  bit 4 = page table not in core (p.t.2)                 0020000
                  bit 5 = 2nd indirect, private not in core (p.t.3)      0010000
                  bit 6 = Indirect shared not in core (p.t.2 || p.t.3)   0004000
                  bit 7 = Indirect page table not in core (p.t.3)        0002000
                  bit 8 = Excessive indirect pointers (>2)               0001000 */
       /* Group 2, 01 = 10                                               0400000
                  bit 2 = Private not in core
                  bit 3 = Write copy trap (bit 9 in p.t.)
                  bit 4 = user trap (bit 8 in p.t.)
                  bit 5 = access trap (p.t. bit 12 = 0 or bits 10-11=3)
                  bit 6 = illegal read or execute
                  bit 7 = illegal write
                  bit 8 = address limit register violation or p.t. bits
                          0,1 = 3 (illegal format) */
        /* Group 3, 01 = 11  (in 2nd or 3rd p.t.)                        060000
                  bit 2 = private not in core
                  bit 3 = write copy trap (bit 9 in p.t.)
                  bit 4 = user trap (bit 8 in p.t.)
                  bit 5 = access trap (p.t. bit 12 = 0 or bits 10-11=3)
                  bit 6 = illegal read or execute
                  bit 7 = illegal write
                  bit 8 = address limit register violation or p.t. bits
                          0,1 = 3 (illegal format */
    uint64   data;
    uint32   tlb_data;
    uint64   traps;
    int      base = 0;
    int      trap = 0;
    int      lvl = 0;
    int      page = (RMASK & addr) >> 9;
    int      uf = (FLAGS & USER) != 0;
    int      map = page;
    int      match;

    if (page_fault)
        return 0;

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* If this is modify instruction use write access */
    wr |= modify;

    /* Umove instructions handled here */
    if ((IR & 0774) == 0100 && (FLAGS & EXJSYS) == 0)
        uf = 1;
    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else {
         if (QWAITS && xct_flag != 0 && !fetch && !uf) {
             if (xct_flag & 010 && cur_context)   /* Indirect */
                 uf = 1;
             if (xct_flag & 004 && wr == 0)       /* XR */
                 uf = 1;
             if (xct_flag & 001 && (wr == 1 || BYF5))  /* XW or XLB or XDB */
                 uf = 1;
         }
         if (!QWAITS && (FLAGS & EXJSYS) == 0 && uf == 0 && !fetch && xct_flag != 0) {
             if (xct_flag & 010 && cur_context)
                 uf = 1;
             if (xct_flag & 004 && wr == 0)
                 uf = 1;
             if (xct_flag & 002 && BYF5)
                 uf = 1;
             if (xct_flag & 001 && wr == 1)
                 uf = 1;
         }
    }

    /* If not really user mode and register access */
    if (addr < 020 && uf && (FLAGS & USER) == 0) {
        if (QWAITS)
           goto lookup;
        addr |= 0775000 | ac_stack;
        uf = 0;
    }

    /* If still access register, just return */
    if (addr < 020) {
       *loc = addr;
       return 1;
    }

lookup:
    if (uf) {
        if (page > user_limit) {
            /* over limit violation */
            fault_data = 0401000;
            goto fault_bbn;
        }
        base = user_base_reg;
        tlb_data = u_tlb[page];
    } else {
        /* 000 - 077 resident map */
        /* 100 - 177 per processor map */
        /* 200 - 577 monitor map */
        /* 600 - 777 per process map */
        if ((page & 0700) == 0 && exec_map == 0) {
             *loc = addr;
             return 1;
        }
        if ((page & 0600) == 0600)
             base = mon_base_reg;
        else
             base = 03000;
        tlb_data = e_tlb[page];
    }
    if (tlb_data != 0) {
access:
        *loc = ((tlb_data & 03777) << 9) + (addr & 0777);
        /* Check access */
        if (wr && (tlb_data & 0200000) == 0) {
            fault_data = 0402000;
            goto fault_bbn;
        } else if (fetch && (tlb_data & 0100000) == 0) {
            fault_data = 0404000;
            goto fault_bbn;
        } else if ((tlb_data & 0400000) == 0) {
            fault_data = 0404000;
            goto fault_bbn;
        }
        return 1;
    }
    traps = FMASK;
    /* Map the page */
    match = 0;
    while (!match) {
        data = M[base + map];

        switch ((data >> 34) & 03) {
        case 0:      /* Direct page */
             /* Bit 4 = execute */
             /* Bit 3 = Write */
             /* Bit 2 = Read */
             traps &= data & (BBN_MERGE|BBN_TRPPG);
             tlb_data = (uint32)(((data & (BBN_EXEC|BBN_WRITE|BBN_READ)) >> 16) |
                         (data & 03777));
             match = 1;
             break;

        case 1:      /* Shared page */
             /* Check trap */
             base = 020000;
             map = (data & BBN_SPT) >> 9;
             traps &= data & (BBN_MERGE|BBN_PAGE);
             data = 0;
             lvl ++;
             break;

        case 2:      /* Indirect page */
             if (lvl == 2) {
                 /* Trap */
                 fault_data =  0201000;
                 goto fault_bbn;
             }
             map = data & BBN_PN;
             base = 020000 + ((data & BBN_SPT) >> 9);
             traps &= data & (BBN_MERGE|BBN_PAGE);
             data = 0;
             lvl ++;
             break;

        case 3:      /* Invalid page */
             /* Trap all  */
             fault_data = ((lvl != 0)? 0200000: 0)  | 0401000;
             goto fault_bbn;
        }
        if ((traps & (BBN_TRP|BBN_TRP1)) == (BBN_TRP|BBN_TRP1)) {
           fault_data = 04000;
           goto fault_bbn;
        }
    }
    if (uf) {
        u_tlb[page] = tlb_data;
    } else {
        e_tlb[page] = tlb_data;
    }
    /* Handle traps */
    if (wr && (traps & BBN_TRPMOD)) {
        fault_data = ((lvl != 0)? 0200000: 0)  | 0440000;
        goto fault_bbn;
    }
    if ((traps & BBN_TRPUSR)) {
        fault_data = ((lvl != 0)? 0200000: 0)  | 0420000;
        goto fault_bbn;
    }
    if ((traps & BBN_ACC) == 0 || (traps & BBN_TRP)) {
        fault_data = ((lvl != 0)? 0200000: 0)  | 0410000;
        goto fault_bbn;
    }
    /* Update CST */
    data = M[04000 + (tlb_data & 03777)];
    if ((data & 00700000000000LL) == 0) {
        fault_data = 0100000 >> ((data >> 31) & 03);
        goto fault_bbn;
    }
    data &= ~00777000000000LL; /* Clear age */
    if (wr)
       data |= 00000400000000LL; /* Set modify */
    data |= pur;
    M[04000 + (tlb_data & 03777)] = data;
    goto access;
      /* Handle fault */
fault_bbn:
    /* Write location of trap to PSB 571 */
    /* If write write MB to PSB 752 */
    /* Force APR to execute at location 70 */

    /* Status word */
    /* RH = Effective address */
    /* Bit 17 = Exec Mode        0000001 */
    /* Bit 16 = Execute request  0000002 */
    /* Bit 15 = Write            0000004 */
    /* Bit 14 = Read             0000010 */
    /* Bit 13 = Ind              0000020 */
    /* Bit 12 = PI in progress   0000040 */
    /* Bit 11 = Key in progress  0000100 */
    /* Bit 10 = non-ex-mem       0000200 */
    /* Bit  9 = Parity           0000400 */
    /* Bit 0-8 = status */
    if ((FLAGS & USER) == 0)
       fault_data |= 01;
    if (fetch)
       fault_data |= 02;
    if (wr)
       fault_data |= 04;
    else
       fault_data |= 010;
    if (cur_context)
       fault_data |= 020;
    if (uuo_cycle)
       fault_data |= 040;
    page_fault = 1;
    M[mon_base_reg | 0571] = ((uint64)fault_data) << 18 | addr;
    if (wr)
        M[mon_base_reg | 0572] = MB;
    return 0;
}

/*
 * Read a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_read_bbn(int flag, int cur_context, int fetch) {
    int addr;

    /* If not doing any special access, just access register */
    if (AB < 020 && ((xct_flag == 0 || fetch || cur_context || (FLAGS & USER) != 0))) {
        MB = get_reg(AB);
        return 0;
    }
    sim_interval--;
    if (!page_lookup_bbn(AB, flag, &addr, 0, cur_context, fetch))
        return 1;
    if (addr < 020) {
        MB = get_reg(AB);
        return 0;
    }
    if (addr >= (int)MEMSIZE) {
        nxm_flag = 1;
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
        watch_stop = 1;
    MB = M[addr];
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_write_bbn(int flag, int cur_context) {
    int addr;

    /* If not doing any special access, just access register */
    if (AB < 020 && ((xct_flag == 0 || cur_context || (FLAGS & USER) != 0))) {
        set_reg(AB, MB);
        return 0;
    }
    sim_interval--;
    if (!page_lookup_bbn(AB, flag, &addr, 1, cur_context, 0))
        return 1;
    if (addr < 020) {
        set_reg(AB, MB);
        return 0;
    }
    if (addr >= (int)MEMSIZE) {
        nxm_flag = 1;
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
        watch_stop = 1;
    M[addr] = MB;
    return 0;
}
#endif

#if WAITS
int page_lookup_waits(int addr, int flag, int *loc, int wr, int cur_context, int fetch) {
    int      uf = (FLAGS & USER) != 0;

    /* If this is modify instruction use write access */
    wr |= modify;

    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else if (xct_flag != 0 && !fetch && !uf) {
         if (xct_flag & 010 && cur_context)   /* Indirect */
             uf = 1;
         if (xct_flag & 004 && wr == 0)       /* XR */
             uf = 1;
         if (xct_flag & 001 && (wr == 1 || BYF5))  /* XW or XLB or XDB */
             uf = 1;
    }

    if (uf) {
        if (addr <= Pl) {
           *loc = (addr + Rl) & RMASK;
           return 1;
        }
        if ((addr & 0400000) != 0 && (addr <= Ph)) {
           if ((Pflag == 0) || (Pflag == 1 && wr == 0)) {
              *loc = (addr + Rh) & RMASK;
              return 1;
           }
        }
        mem_prot = 1;
        return 0;
    } else {
        *loc = addr;
    }
    return 1;
}

int Mem_read_waits(int flag, int cur_context, int fetch) {
    int addr;

    if (AB < 020 && ((xct_flag == 0 || fetch || cur_context || (FLAGS & USER) != 0))) {
        MB = get_reg(AB);
        return 0;
    }
    sim_interval--;
    if (!page_lookup_waits(AB, flag, &addr, 0, cur_context, fetch))
        return 1;
    if (addr >= (int)MEMSIZE) {
        nxm_flag = 1;
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
        watch_stop = 1;
    MB = M[addr];
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */

int Mem_write_waits(int flag, int cur_context) {
    int addr;


    /* If not doing any special access, just access register */
    if (AB < 020 && ((xct_flag == 0 || cur_context || (FLAGS & USER) != 0))) {
        set_reg(AB, MB);
        return 0;
    }
    sim_interval--;
    if (!page_lookup_waits(AB, flag, &addr, 1, cur_context, 0))
        return 1;
    if (addr >= (int)MEMSIZE) {
        nxm_flag = 1;
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
        watch_stop = 1;
    M[addr] = MB;
    return 0;
}
#endif

int page_lookup_ka(int addr, int flag, int *loc, int wr, int cur_context, int fetch) {
      if (!flag && (FLAGS & USER) != 0) {
          if (addr <= Pl) {
             *loc = (addr + Rl) & RMASK;
             return 1;
          }
          if (cpu_unit[0].flags & UNIT_TWOSEG &&
             (addr & 0400000) != 0 && (addr <= Ph)) {
             if ((Pflag == 0) || (Pflag == 1 && wr == 0)) {
                *loc = (addr + Rh) & RMASK;
                return 1;
             }
          }
          mem_prot = 1;
          return 0;
      } else {
          *loc = addr;
      }
      return 1;
}

int Mem_read_ka(int flag, int cur_context, int fetch) {
    int addr;

    if (AB < 020) {
        MB = get_reg(AB);
    } else {
        sim_interval--;
        if (!page_lookup_ka(AB, flag, &addr, 0, cur_context, fetch))
            return 1;
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        MB = M[addr];
    }
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */

int Mem_write_ka(int flag, int cur_context) {
    int addr;

    if (AB < 020) {
        set_reg(AB, MB);
    } else {
        sim_interval--;
        if (!page_lookup_ka(AB, flag, &addr, 1, cur_context, 0))
            return 1;
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        M[addr] = MB;
    }
    return 0;
}

int (*Mem_read)(int flag, int cur_context, int fetch);
int (*Mem_write)(int flag, int cur_context);
#endif

#if PDP6
/*
 * Check if the last operation caused a APR IRQ to be generated.
 */
void check_apr_irq() {
     if (pi_enable && apr_irq) {
         int flg = 0;
         clr_interrupt(0);
         flg |= ((FLAGS & OVR) != 0) & ov_irq;
         flg |= ((FLAGS & PCHNG) != 0) & pcchg_irq;
         flg |= nxm_flag | mem_prot | push_ovf;
         if (flg)
             set_interrupt(0, apr_irq);
     }
}

/*
 * APR Device for PDP6.
 */
t_stat dev_apr(uint32 dev, uint64 *data) {
    uint64 res = 0;
    switch(dev & 03) {
    case CONI:
        /* Read trap conditions */
        res = apr_irq | (((FLAGS & OVR) != 0) << 3) | (ov_irq << 4) ;
        res |= (((FLAGS & PCHNG) != 0) << 6) | (pcchg_irq << 7) ;
        res |= (clk_flg << 9) | (((uint64)clk_en) << 10) | (nxm_flag << 12);
        res |= (mem_prot << 13) | (((FLAGS & USER) != 0) << 14) | (user_io << 15);
        res |= (push_ovf << 16);
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI APR %012llo\n", *data);
        break;

     case CONO:
        /* Set trap conditions */
        res = *data;
        clk_irq = apr_irq = res & 07;
        clr_interrupt(0);
        if (res & 010)       /* Bit 32 */
            FLAGS &= ~OVR;
        if (res & 020)       /* Bit 31 */
            ov_irq = 1;
        if (res & 040)       /* Bit 30 */
            ov_irq = 0;
        if (res & 0100)      /* Bit 29 */
            FLAGS &= ~PCHNG;
        if (res & 0200)      /* Bit 28 */
            pcchg_irq = 1;
        if (res & 0400)      /* Bit 27 */
            pcchg_irq = 0;
        if (res & 0001000) { /* Bit 26 */
            clk_flg = 0;
            clr_interrupt(4);
        }
        if (res & 0002000) { /* Bit 25 */
            clk_en = 1;
            if (clk_flg)
               set_interrupt(4, clk_irq);
        }
        if (res & 0004000) { /* Bit 24 */
            clk_en = 0;
            clr_interrupt(4);
        }
        if (res & 010000)    /* Bit 23 */
            nxm_flag = 0;
        if (res & 020000)    /* Bit 22 */
            mem_prot = 0;
        if (res & 040000)    /* Bit 21 */
            user_io = 0;
        if (res & 0100000)   /* Bit 20 */
            user_io = 1;
        if (res & 0200000) { /* Bit 19 */
            reset_all(1);
            mem_prot = 0;
            user_io = 0;
            FLAGS &= ~(USERIO);
        }
        if (res & 0400000)   /* Bit 18 */
            push_ovf = 0;
        check_apr_irq();
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO APR %012llo\n", *data);
        break;

    case DATAO:
        /* Set protection registers */
        Rl = 0776000 & *data;
        Pl = (0776000 & (*data >> 18)) + 01777;
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAO APR %012llo\n", *data);
        break;

    case DATAI:
        /* Read switches */
        *data = SW;
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAI APR %012llo\n", *data);
        break;
    }
    return SCPE_OK;
}

#define get_reg(reg)                 FM[(reg) & 017]
#define set_reg(reg, value)          FM[(reg) & 017] = value

int page_lookup(int addr, int flag, int *loc, int wr, int cur_context, int fetch) {
      if (!flag && (FLAGS & USER) != 0) {
          if (addr <= Pl) {
             *loc = (addr + Rl) & RMASK;
             return 1;
          }
          mem_prot = 1;
          return 0;
      } else {
         *loc = addr;
      }
      return 1;
}

int Mem_read(int flag, int cur_context, int fetch) {
    int addr;

    if (AB < 020) {
        MB = get_reg(AB);
    } else {
        sim_interval--;
        if (!page_lookup(AB, flag, &addr, 0, cur_context, fetch))
            return 1;
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        MB = M[addr];
    }
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */

int Mem_write(int flag, int cur_context) {
    int addr;

    if (AB < 020) {
        set_reg(AB, MB);
    } else {
        sim_interval--;
        if (!page_lookup(AB, flag, &addr, 1, cur_context, 0))
            return 1;
        if (addr >= (int)MEMSIZE) {
            nxm_flag = 1;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        M[addr] = MB;
    }
    return 0;
}
#endif

/*
 * Function to determine number of leading zero bits in a work
 */
int nlzero(uint64 w) {
    int n = 0;
    if (w == 0) return 36;
    if ((w & 00777777000000LL) == 0) { n += 18; w <<= 18; }
    if ((w & 00777000000000LL) == 0) { n += 9;  w <<= 9;  }
    if ((w & 00770000000000LL) == 0) { n += 6;  w <<= 6;  }
    if ((w & 00700000000000LL) == 0) { n += 3;  w <<= 3;  }
    if ((w & 00600000000000LL) == 0) { n ++;    w <<= 1;  }
    if ((w & 00400000000000LL) == 0) { n ++; }
    return n;
}

t_stat sim_instr (void)
{
t_stat reason;
int     i_flags;                 /* Instruction mode flags */
int     pi_rq;                   /* Interrupt request */
int     pi_ov;                   /* Overflow during PI cycle */
int     pi_cycle;                /* Executing an interrupt */
int     ind;                     /* Indirect bit */
int     f_load_pc;               /* Load AB from PC at start of instruction */
int     f_inst_fetch;            /* Fetch new instruction */
int     f_pc_inh;                /* Inhibit PC increment after instruction */
int     nrf;                     /* Normalize flag */
int     fxu_hold_set;            /* Negitive exponent */
int     sac_inh;                 /* Inihibit saving AC after instruction */
int     f;                       /* Temporary variables */
int     flag1;
int     flag3;
int     instr_count = 0;         /* Number of instructions to execute */
uint32  IA;
#if ITS
char    one_p_arm = 0;           /* One proceed arm */
#endif

if (sim_step != 0) {
    instr_count = sim_step;
    sim_cancel_step();
}

/* Build device table */
if ((reason = build_dev_tab ()) != SCPE_OK)            /* build, chk dib_tab */
    return reason;


/* Main instruction fetch/decode loop: check clock queue, intr, trap, bkpt */
   f_load_pc = 1;
   f_inst_fetch = 1;
   ind = 0;
   uuo_cycle = 0;
   pi_cycle = 0;
   pi_rq = 0;
   pi_ov = 0;
   BYF5 = 0;
#if KI | KL
   page_fault = 0;
#endif
#if ITS
   if (QITS) {
       one_p_arm = 0;
       set_quantum();
   }
#endif
  watch_stop = 0;

  while ( reason == 0) {                                /* loop until ABORT */
    if (sim_interval <= 0) {                           /* check clock queue */
         if ((reason = sim_process_event()) != SCPE_OK) {/* error?  stop sim */
#if ITS
             if (QITS)
                 load_quantum();
#endif
             return reason;
         }
    }

    if (sim_brk_summ && f_load_pc && sim_brk_test(PC, SWMASK('E'))) {
         reason = STOP_IBKPT;
         break;
    }

    if (watch_stop) {
         reason = STOP_IBKPT;
         break;
    }

    check_apr_irq();
    /* Normal instruction */
    if (f_load_pc) {
        modify = 0;
        xct_flag = 0;
#if KI | KL
        trap_flag = 0;
#endif
        AB = PC;
        uuo_cycle = 0;
        f_pc_inh = 0;
    }

    if (f_inst_fetch) {
#if !(KI | KL)
fetch:
#endif
#if ITS
        if (QITS && pi_cycle == 0 && mem_prot == 0) {
           opc = PC | (FLAGS << 18);
           if ((FLAGS & ONEP) != 0) {
              one_p_arm = 1;
              FLAGS &= ~ONEP;
           }
        }
#endif

       if (Mem_read(pi_cycle | uuo_cycle, 1, 1)) {
           pi_rq = check_irq_level();
           if (pi_rq)
              goto st_pi;
           goto last;
       }

no_fetch:
       IR = (MB >> 27) & 0777;
       AC = (MB >> 23) & 017;
       AD = MB;  /* Save for historical sake */
       IA = AB;
       i_flags = opflags[IR];
       BYF5 = 0;
    }

#if KI | KL
    /* Handle page fault and traps */
    if (page_enable && trap_flag == 0 && (FLAGS & (TRP1|TRP2))) {
        AB = 0420 + ((FLAGS & (TRP1|TRP2)) >> 2);
        trap_flag = FLAGS & (TRP1|TRP2);
        FLAGS &= ~(TRP1|TRP2);
        pi_cycle = 1;
        AB += (FLAGS & USER) ? ub_ptr : eb_ptr;
        Mem_read_nopage();
        goto no_fetch;
    }
#endif


    /* Handle indirection repeat until no longer indirect */
    do {
         if ((!pi_cycle) & pi_pending
#if KI | KL
                             & (!trap_flag)
#endif
                             ) {
            pi_rq = check_irq_level();
         }
         ind = (MB & 020000000) != 0;
         AR = MB;
         AB = MB & RMASK;
         if (MB &  017000000) {
             AR = MB = (AB + get_reg((MB >> 18) & 017)) & FMASK;
             AB = MB & RMASK;
         }
         if (IR != 0254)
             AR &= RMASK;
         if (ind & !pi_rq)
              if (Mem_read(pi_cycle | uuo_cycle, 1, 0))
                 goto last;
         /* Handle events during a indirect loop */
         if (sim_interval-- <= 0) {
              if ((reason = sim_process_event()) != SCPE_OK) {
                  return reason;
              }
         }
    } while (ind & !pi_rq);


    /* If there is a interrupt handle it. */
    if (pi_rq) {
st_pi:
        sim_debug(DEBUG_IRQ, &cpu_dev, "trap irq %o %03o %03o \n",
                       pi_enc, PIR, PIH);
        pi_cycle = 1;
        pi_rq = 0;
        pi_hold = 0;
        pi_ov = 0;
        AB = 040 | (pi_enc << 1) | maoff;
#if KI | KL
        xct_flag = 0;
        /*
         * Scan through the devices and allow KI devices to have first
         * hit at a given level.
         */
        for (f = 0; f < 128; f++) {
            if (dev_irqv[f] != 0 && dev_irq[f] & (0200 >> pi_enc)) {
                AB = dev_irqv[f](f << 2, AB);
                break;
            }
        }
        AB |= eb_ptr;
        Mem_read_nopage();
        goto no_fetch;
#else
        goto fetch;
#endif
    }


#if KI | KL
    if (page_enable && page_fault) {
        if (!f_pc_inh && !pi_cycle)
            PC = (PC + 1) & RMASK;
        goto last;
    }
#endif

    /* Check if possible idle loop */
    if (sim_idle_enab && (FLAGS & USER) != 0 && PC < 020 && AB < 020 &&
           (IR & 0760) == 0340) {
       sim_idle (TMR_RTC, FALSE);
    }

    /* Update history */
    if (hst_lnt) {
            hst_p = hst_p + 1;
            if (hst_p >= hst_lnt) {
                    hst_p = 0;
            }
            hst[hst_p].pc = HIST_PC | ((BYF5)? (HIST_PC2|PC) : IA);
            hst[hst_p].ea = AB;
            hst[hst_p].ir = AD;
            hst[hst_p].flags = (FLAGS << 5) |(clk_flg << 2) | (nxm_flag << 1)
#if KA | PDP6
                                | (mem_prot << 4) | (push_ovf << 3)
#endif
#if PDP6
                                | ill_op
#endif
                       ;
            hst[hst_p].ac = get_reg(AC);
    }


    /* Set up to execute instruction */
    f_inst_fetch = 1;
    f_load_pc = 1;
    nrf = 0;
    fxu_hold_set = 0;
    sac_inh = 0;
    modify = 0;
    f_pc_inh = 0;

    /* Load pseudo registers based on flags */
    if (i_flags & (FCEPSE|FCE)) {
        if (i_flags & FCEPSE)
            modify = 1;
        if (Mem_read(0, 0, 0))
            goto last;
        AR = MB;
    }

    if (i_flags & FAC) {
        BR = AR;
        AR = get_reg(AC);
    }

    if (i_flags & FBR) {
        BR = get_reg(AC);
    }

    if (hst_lnt) {
        hst[hst_p].mb = AR;
    }

    if (i_flags & FAC2) {
        MQ = get_reg(AC + 1);
    } else if (!BYF5) {
        MQ = 0;
    }

    if (i_flags & SWAR) {
        AR = SWAP_AR;
    }

    /* Process the instruction */
    switch (IR) {
muuo:
    case 0000: /* UUO */
    case 0040: case 0041: case 0042: case 0043:
    case 0044: case 0045: case 0046: case 0047:
    case 0050: case 0051: case 0052: case 0053:
    case 0054: case 0055: case 0056: case 0057:
    case 0060: case 0061: case 0062: case 0063:
    case 0064: case 0065: case 0066: case 0067:
    case 0070: case 0071: case 0072: case 0073:
    case 0074: case 0075: case 0076: case 0077:

              /* MUUO */

#if KI | KL
    case 0100: case 0101: case 0102: case 0103:
    case 0104: case 0105: case 0106: case 0107:
    case 0123:
    case 0247: /* UUO  */
unasign:
              MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | (uint64)(AB);
              AB = ub_ptr | 0424;
              Mem_write_nopage();
              AB |= 1;
              MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + (trap_flag == 0)) & RMASK);
              if ((FLAGS & USER) == 0) {
                  MB &= ~SMASK;
                  MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
              }
              Mem_write_nopage();
              FLAGS &= ~ (PRV_PUB|BYTI|ADRFLT|TRP1|TRP2);
              AB = ub_ptr | 0430;
              if (trap_flag != 0)
                  AB |= 1;
              if (FLAGS & PUBLIC)
                  AB |= 2;
              if (FLAGS & USER)
                  AB |= 4;
              Mem_read_nopage();
              FLAGS = (MB >> 23) & 017777;
              /* If transistioning from user to executive adjust flags */
              if ((FLAGS & USER) != 0 && (AB & 4) != 0) 
                  FLAGS |= USERIO;
              if ((FLAGS & USER) == 0 && (AB & 2 || (FLAGS & OVR) != 0))
                  FLAGS |= PRV_PUB|OVR;
              PC = MB & RMASK;
              f_pc_inh = 1;
              break;
#else
              uuo_cycle = 1;
#endif

              /* LUUO */
    case 0001: case 0002: case 0003:
    case 0004: case 0005: case 0006: case 0007:
    case 0010: case 0011: case 0012: case 0013:
    case 0014: case 0015: case 0016: case 0017:
    case 0020: case 0021: case 0022: case 0023:
    case 0024: case 0025: case 0026: case 0027:
    case 0030: case 0031: case 0032: case 0033:
    case 0034: case 0035: case 0036: case 0037:
#if PDP6
              ill_op = 1;
              ex_uuo_sync = 1;
#endif
              MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | (uint64)(AB);
#if KI | KL
              if ((FLAGS & USER) == 0) {
                  AB = eb_ptr + 040;
                  Mem_write_nopage();
                  AB += 1;
                  Mem_read_nopage();
                  uuo_cycle = 1;
                  goto no_fetch;
              }
#endif
              AB = 040;
              if (maoff && uuo_cycle)
                  AB |= maoff;
              Mem_write(uuo_cycle, 1);
              AB += 1;
              f_load_pc = 0;
#if ITS
              if (QITS && one_p_arm) {
                  FLAGS |= ONEP;
                  one_p_arm = 0;
              }
#endif
              f_pc_inh = 1;
              break;

#if KI | KL
    case 0110:       /* DFAD */
    case 0111:       /* DFSB */
              /* On Load AR,MQ has memory operand */
              /* AR,MQ = AC  BR,MB  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = MB;
              BR = AR;
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);

              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0))
                  goto last;
              /* Make into 64 bit numbers */
              SC = GET_EXPO(BR);
              SMEAR_SIGN(BR);
              BR <<= 35;
              BR |= (MB & CMASK);
              FE = GET_EXPO(AR);
              SMEAR_SIGN(AR);
              AR <<= 35;
              AR |= (MQ & CMASK);
              if (IR & 01) {
                  BR = (FPFMASK ^ BR) + 1;
              }
              SCAD = (SC - 200) + (FE - 200);
              if (FE > SC) {  /* Swap if BR larger */
                  AD = AR;
                  AR = BR;
                  BR = AD;
                  SCAD = FE;
                  FE = SC;
                  SC = SCAD;
              }
              SCAD = SC - FE;
              flag3 = 0;
              MQ = 0;
              if (SCAD > 0) {  /* Align numbers */
                  if (SCAD > 64) /* Outside range */
                      AR = 0;
                  else {
                      while (SCAD > 0) {
                          MQ >>= 1;
                          if (AR & 1)
                             MQ |= SMASK;
                          AR = (AR & (FPHBIT|FPSBIT)) | (AR >> 1);
                          SCAD--;
                       }
                  }
              }
              AR = AR + BR + flag3;
              /* Set flag1 to sign */
              flag1 = (AR & FPHBIT) != 0;
dpnorm:
              /* Make sure High bit and sign bit same */
              while (((AR & FPHBIT) != 0) != ((AR & FPSBIT) != 0)) {
                  SC += 1;
                  MQ >>= 1;
                  if (AR & 1)
                      MQ |= SMASK;
                  AR = (AR & FPHBIT) | (AR >> 1);
              }

              /* Check for potiential underflow */
              if (((SC & 0400) != 0) ^ ((SC & 0200) != 0))
                 fxu_hold_set = 1;
              if (AR != 0) {
                  while (AR != 0 &&
                         (((AR & (FPSBIT|FPNBIT)) == (FPSBIT|FPNBIT)) ||
                          ((AR & (FPSBIT|FPNBIT)) == 0))) {
                      SC --;
                      AR <<= 1;
                      if (MQ & SMASK)
                          AR |= 1;
                      MQ <<= 1;
                  }
                  /* Handle special minus case */
                  if (AR == (FPHBIT|FPSBIT)) {
                      SC += 1;
                      AR = (AR & FPHBIT) | (AR >> 1);
                  }
              } else {
                 AR = MQ = 0;
                 SC = 0;
              }

              /* Check if we need to round */
              if (!nrf && ((MQ & SMASK) != 0) && (((AR & FPSBIT) == 0) ||
                          (((AR & FPSBIT) != 0) && ((MQ & 0377700000000LL) != 0)))) {
                 AR++;
                 nrf = 1;
                /* Clean things up if we overflowed */
                if ((AR & FPHBIT) == 0)
                    goto dpnorm;
              }
              /* Extract result */
              MQ = (AR & CMASK);
              AR >>= 35;
              AR &= MMASK;
              if (flag1)   /* Append sign */
                 AR |= SMASK;
              /* Check for over/under flow */
              if (((SC & 0400) != 0) && !pi_cycle) {
                 FLAGS |= OVR|FLTOVR|TRP1;
                 if (!fxu_hold_set) {
                     FLAGS |= FLTUND;
                 }
              }
              /* Add exponent */
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR &= SMASK|MMASK;
              if (AR != 0 || MQ != 0)
                  AR |= ((uint64)(SCAD & 0377)) << 27;

              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0112: /* DFMP */
              /* On Load AR,MQ has memory operand */
              /* AR,MQ = AC  BR,MB  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = MB;
              BR = AR;
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0))
                  goto last;
              /* Make into 64 bit numbers */
              SC = GET_EXPO(AR);
              SMEAR_SIGN(AR);
              AR <<= 35;
              AR |= (MQ & CMASK);
              FE = GET_EXPO(BR);
              SMEAR_SIGN(BR);
              BR <<= 35;
              BR |= MB & CMASK;
              flag1 = 0;
              /* Make both numbers positive */
              if (AR & FPSBIT) {
                  AR = (FPFMASK ^ AR) + 1;
                  flag1 = 1;
              }
              if (BR & FPSBIT) {
                  BR = (FPFMASK ^ BR) + 1;
                  flag1 = !flag1;
              }
              /* Compute exponent */
              SC = SC + FE - 0200;
              ARX = 0;
              /* Do multiply */
              for (FE = 0; FE < 62; FE++) {
                  if (FE == 35)  /* Clear MQ so it has correct lower product digits */
                     MQ = 0;
                  if (BR & 1)
                     ARX += AR;
                  MQ >>= 1;
                  if (ARX & 1)
                     MQ |= BIT1;
                  ARX >>= 1;
                  BR >>= 1;
              }
              AR = ARX;
              /* Make result negative if needed */
              if (flag1) {
                  MQ = (MQ ^ CMASK) + 0400;
                  AR = (AR ^ FPFMASK);
                  if (MQ & SMASK) {
                     AR ++;
                     MQ &= FMASK;
                  }
                  /* Check for overflow */
                  if ((AR & (FPHBIT|FPSBIT)) == (FPHBIT)) {
                      SC += 1;
                      MQ >>= 1;
                      if (AR & 1)
                          MQ |= BIT1;
                      AR = (AR >> 1) | (FPHBIT & AR);
                  }
              }
              /* Check if we need to normalize */
              if (AR != 0) {
                  /* Check for fast shift */
                  if ((AR & ~MMASK) == 0 || ((AR & ~MMASK) + BIT8) == 0) {
                      SC -= 35;
                      AR <<= 35;
                      AR |= MQ & CMASK;
                      MQ = 0;
                      if ((AR & 0777) == 0777)
                          AR &= (FPFMASK << 8);
                  }
                  if (((AR & (FPSBIT|FPNBIT)) == (FPSBIT|FPNBIT)) ||
                      ((AR & (FPSBIT|FPNBIT)) == 0)) {
                      SC --;
                      AR <<= 1;
                      if (MQ & BIT1)
                         AR |= 1;
                      MQ <<= 1;
                      MQ &= FMASK;
                      nrf = 1;
                  }
              } else {
                 AR = MQ = 0;
                 SC = 0;
                 flag1 = 0;
              }
              /* Round if needed */
              if (MQ & BIT1)
                   AR++;
              /* Build results */
              MQ = (AR & CMASK);
              AR >>= 35;
              AR &= MMASK;
              if (flag1)
                  AR |= SMASK;
              if (((SC & 0400) != 0) && !pi_cycle) {
                 FLAGS |= OVR|FLTOVR|TRP1;
                 if (SC < 0) {
                     FLAGS |= FLTUND;
                 }
              }
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR &= SMASK|MMASK;
              if (AR != 0 || MQ != 0)
                  AR |= ((uint64)(SCAD & 0377)) << 27;

              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0113: /* DFDV */
              /* On Load AR,MQ has memory operand */
              /* AR,MQ = AC  BR,MB  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = MB;
              BR = AR;
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0))
                  goto last;
              /* Make into 64 bit numbers */
              SC = GET_EXPO(AR);
              SMEAR_SIGN(AR);
              AR <<= 35;
              AR |= (MQ & CMASK);
              FE = GET_EXPO(BR);
              SMEAR_SIGN(BR);
              BR <<= 35;
              BR |= MB & CMASK;
              /* Make both positive */
              flag1 = 0;
              if (AR & FPSBIT) {
                  AR = (FPFMASK ^ AR) + 1;
                  flag1 = 1;
              }
              if (BR & FPSBIT) {
                  BR = (FPFMASK ^ BR) + 1;
                  flag1 = !flag1;
              }
              /* Precheck if divide ok */
              if (AR >= (BR << 1)) {
                  if (!pi_cycle)
                      FLAGS |= OVR|FLTOVR|NODIV|TRP1;
                  AR = 0;      /* For clean history */
                  sac_inh = 1;
                  break;
              }
              /* Divide by zero */
              if (AR == 0)  {
                  sac_inh = 1;
                  break;
              }
              /* Compute exponents */
              SC = SC - FE + 0201;
              /* Precheck divider */
              if (AR < BR) {
                  AR <<= 1;
                  SC--;
              }
              if (SC < 0 && !pi_cycle)
                  FLAGS |= FLTUND|OVR|FLTOVR|TRP1;
              /* Do divide */
              AD = 0;
              for (FE = 0; FE < 62; FE++) {
                  AD <<= 1;
                  if (AR >= BR) {
                     AR = AR - BR;
                     AD |= 1;
                  }
                  AR <<= 1;
              }
              AR = AD;
              if (flag1) {
                  AR = (AR ^ FPFMASK) + 1;
              }
              if (((SC & 0400) != 0) ^ ((SC & 0200) != 0) || SC == 0600)
                  fxu_hold_set = 1;
              while (((AR & FPHBIT) != 0) != ((AR & FPSBIT) != 0)) {
                  SC += 1;
                  AR = (AR & FPHBIT) | (AR >> 1);
              }
              MQ = (AR & CMASK);
              AR >>= 35;
              AR &= MMASK;
              if (flag1)
                  AR |= SMASK;
              if (((SC & 0400) != 0) && !pi_cycle) {
                  FLAGS |= OVR|FLTOVR|TRP1;
                  if (!fxu_hold_set) {
                      FLAGS |= FLTUND;
                  }
              }
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR &= SMASK|MMASK;
              if (AR != 0 || MQ != 0)
                  AR |= ((uint64)(SCAD & 0377)) << 27;

              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0114: /* DADD */
    case 0115: /* DSUB */
    case 0116: /* DMUL */
    case 0117: /* DDIV */
              goto unasign;

    case 0120: /* DMOVE */
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = MB;
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0))
                   goto last;
              MQ = MB;
              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0121: /* DMOVN */
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = MB;
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0))
                   goto last;
              MQ = ((MB & CMASK) ^ CMASK) + 1;   /* Low */
              /* High */
              AR = (CM(AR) + ((MQ & SMASK) != 0)) & FMASK;
              MQ &= CMASK;
              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0124: /* DMOVEM */
              AR = get_reg(AC);
              /* Handle each half as seperate instruction */
              if ((FLAGS & BYTI) == 0) {
                  MB = AR;
                  if (Mem_write(0, 0))
                      goto last;
                  FLAGS |= BYTI;
              }
              MQ = get_reg(AC + 1);
              if ((FLAGS & BYTI)) {
                  AB = (AB + 1) & RMASK;
                  MB = MQ;
                  if (Mem_write(0, 0))
                     goto last;
                  FLAGS &= ~BYTI;
              }
              break;

    case 0125: /* DMOVNM */
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);
              /* Handle each half as seperate instruction */
              if ((FLAGS & BYTI) == 0) {
                  BR = AR = CM(AR);
                  BR = (BR + 1);
                  MQ = (((MQ & CMASK) ^ CMASK) + 1);
                  if (MQ & SMASK)
                     AR = BR;
                  AR &= FMASK;
                  MB = AR;
                  if (Mem_write(0, 0))
                      goto last;
                  FLAGS |= BYTI;
              }
              if ((FLAGS & BYTI)) {
                  MQ = get_reg(AC + 1);
                  MQ = (CM(MQ) + 1) & CMASK;
                  AB = (AB + 1) & RMASK;
                  MB = MQ;
                  if (Mem_write(0, 0))
                     goto last;
                  FLAGS &= ~BYTI;
              }
              break;

    case 0122: /* FIX */
    case 0126: /* FIXR */
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = MB;
              MQ = 0;
              SC = ((((AR & SMASK) ? 0377 : 0 )
                      ^ ((AR >> 27) & 0377)) + 0600) & 0777;
              SMEAR_SIGN(AR);
              SC -= 27;
              SC &= 0777;
              if (SC < 9) {
              /* 0 < N < 8 */
                  AR = (AR << SC) & FMASK;
              }  else if ((SC & 0400) != 0) {
              /* -27 < N < 0 */
                  SC = 01000 - SC;
                  if (SC > 27) {
                      AR = MQ = 0;
                  } else {
                      MQ = (AR << (36 - SC)) & FMASK /*- flag1*/ ;
                      AR = (AR >> SC) | (FMASK & (((AR & SMASK)? FMASK << (27 - SC): 0)));
                  }
                  if (((IR & 04) != 0 && (MQ & SMASK) != 0) ||
                      ((IR & 04) == 0 && (AR & SMASK) != 0 &&
                             ((MQ & CMASK) != 0 || (MQ & SMASK) != 0)))
                       AR ++;
              } else {
                  if (!pi_cycle)
                      FLAGS |= OVR|TRP1;        /* OV & T1 */
                  sac_inh = 1;
              }
              if (!sac_inh)
                 set_reg(AC, AR);
              break;

    case 0127: /* FLTR */
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = MB;
              AR <<= 27;
              if (AR & FPSBIT) {
                  flag1 = 1;
                  AR |= FPHBIT;
              } else
                  flag1 = 0;
              i_flags = SAC;
              SC = 162;
              goto fnorm;
#else
    case 0100: /* TENEX UMOVE */
#if BBN
              if (QBBN) {
                   if (Mem_read(0, 0, 0)) {
                      IR = 0;
                      goto last;
                   }
                   AR = MB;
                   set_reg(AC, AR);    /* blank, I, B */
                   IR = 0;
                   break;
              }
#endif
              goto unasign;
    case 0101: /* TENEX UMOVEI */
#if BBN
              if (QBBN) {
                   set_reg(AC, AR);    /* blank, I, B */
                   IR = 0;
                   break;
              }
#endif
              goto unasign;
    case 0102: /* TENEX UMOVEM */ /* ITS LPM */
#if ITS
              if (QITS && (FLAGS & USER) == 0) {
                  /* Load store ITS pager info */
                  /* AC & 1 = Store */
                  if (AC & 1) {
                      if ((AB + 8) > MEMSIZE) {
                         fault_data |= 0400;
                         mem_prot = 1;
                         break;
                      }
                      MB = ((uint64)age) << 27 |
                            ((uint64)fault_addr & 0777) << 18 |
                            (uint64)jpc;
                      M[AB] = MB;
                      AB = (AB + 1) & RMASK;
                      MB = opc;
                      M[AB] = MB;
                      AB = (AB + 1) & RMASK;
                      MB = (mar & 00777607777777LL) | ((uint64)pag_reload) << 21;
                      M[AB] = MB;
                      AB = (AB + 1) & RMASK;
                      MB = ((uint64)get_quantum()) | ((uint64)fault_data) << 18;
                      M[AB] = MB;
                      AB = (AB + 1) & RMASK;
                      MB = ((uint64)fault_addr & 00760000) << 13 |
                            (uint64)dbr1;
                      M[AB] = MB;
                      AB = (AB + 1) & RMASK;
                      MB = ((uint64)fault_addr & 00037000) << 17 |
                            (uint64)dbr2;
                      M[AB] = MB;
                      AB = (AB + 1) & RMASK;
                      MB = (uint64)dbr3;
                      M[AB] = MB;
                      AB = (AB + 1) & RMASK;
                      MB = (uint64)ac_stack;
                      M[AB] = MB;
                  } else {
                      if ((AB + 8) > MEMSIZE) {
                         fault_data |= 0400;
                         mem_prot = 1;
                         break;
                      }
                      MB = M[AB];                /* WD 0 */
                      age = (MB >> 27) & 017;
                      jpc = (MB & RMASK);
                      fault_addr = (MB >> 18) & 0777;
                      AB = (AB + 1) & RMASK;
                      MB = M[AB];
                      opc = MB;
                      AB = (AB + 1) & RMASK;
                      MB = M[AB];                /* WD 2 */
                      mar = /*03777777 &*/ MB;
                      pag_reload = 0;
                      AB = (AB + 1) & RMASK;
                      MB = M[AB];                /* WD 3 */
                      /* Store Quantum */
                      qua_time = MB & RMASK;
                      set_quantum();
                      fault_data = (MB >> 18) & RMASK;
                      mem_prot = 0;
                      if ((fault_data & 0777772) != 0)
                          mem_prot = 1;
                      AB = (AB + 1) & RMASK;
                      MB = M[AB];                /* WD 4 */
                      dbr1 = ((0377 << 18) | RMASK) & MB;
                      fault_addr |= (MB >> 13) & 00760000;
                      AB = (AB + 1) & RMASK;
                      MB = M[AB];                /* WD 5 */
                      fault_addr |= (MB >> 17) & 00037000;
                      dbr2 = ((0377 << 18) | RMASK) & MB;
                      AB = (AB + 1) & RMASK;
                      MB = M[AB];                /* WD 6 */
                      dbr3 = ((0377 << 18) | RMASK) & MB;
                      AB = (AB + 1) & RMASK;
                      MB = M[AB];                /* WD 7 */
                      ac_stack = (uint32)MB;
                      page_enable = 1;
                  }
                  /* AC & 2 = Clear TLB */
                  if (AC & 2) {
                     for (f = 0; f < 512; f++)
                        e_tlb[f] = u_tlb[f] = 0;
                     mem_prot = 0;
                  }
                  /* AC & 4 = Set Prot Interrupt */
                  if (AC & 4) {
                      mem_prot = 1;
                      set_interrupt(0, apr_irq);
                  }
                  break;
              }
#endif
#if BBN
              if (QBBN) {
                   AR = get_reg(AC);
                   MB = AR;
                   if (Mem_write(0, 0)) {
                      IR = 0;
                      goto last;
                   }
                   IR = 0;
                   break;
              }
#endif
              goto unasign;

    case 0103: /* TENEX UMOVES */ /* ITS XCTR */
#if ITS
              if (QITS && (FLAGS & USER) == 0) {
                   /* AC & 1 = Read User */
                   /* AC & 2 = Write User */
                   /* AC & 4 = Inhibit mem protect, skip */
                   /* AC & 8 = ??? */
                   f_load_pc = 0;
                   f_pc_inh = 1;
                   xct_flag = AC;
                   break;
              }
#endif
#if BBN
              if (QBBN) {
                   if (Mem_read(0, 0, 0)) {
                       IR = 0;
                       goto last;
                   }
                   modify = 1;
                   AR = MB;
                   if (Mem_write(0, 0)) {
                      IR = 0;
                      goto last;
                   }
                   if (AC != 0)
                       set_reg(AC, AR);    /* blank, I, B */
                   IR = 0;
                   break;
              }
#endif
              goto unasign;

              /* MUUO */
    case 0104: /* TENEX JSYS */
#if BBN
              if (QBBN) {
                   BR = ((uint64)(FLAGS) << 23) | ((PC + !pi_cycle) & RMASK);
                   if (AB < 01000) {
                      AB += 01000;
                      if ((FLAGS & USER) == 0)
                         FLAGS |= EXJSYS;
                      FLAGS &= ~USER;
                   }
                   if (Mem_read(0, 0, 0)) {
                       FLAGS = (uint32)(BR >> 23); /* On error restore flags */
                       goto last;
                   }
                   AR = MB;
                   AB = (AR >> 18) & RMASK;
                   MB = BR;
                   if (Mem_write(0, 0)) {
                       FLAGS = (uint32)(BR >> 23); /* On error restore flags */
                       goto last;
                   }
                   PC = AR & RMASK;
                   break;
              }
#endif
              goto unasign;

    case 0247: /* UUO  or ITS CIRC instruction */
#if ITS
              if (QITS) {
                  BR = AR;
                  AR = get_reg(AC);
                  if (hst_lnt) {
                      hst[hst_p].mb = AR;
                  }
                  MQ = get_reg(AC + 1);
                  SC = ((AB & RSIGN) ? (0777 ^ AB) + 1 : AB) & 0777;
                  if (SC == 0)
                      break;
                  SC = SC % 72;
                  if (AB & RSIGN)
                      SC = 72 - SC;
                  /* Have to do this the long way */
                  while (SC > 0) {
                      AD = ((AR << 1) | (MQ & 1)) & FMASK;
                      MQ = ((MQ >> 1) | (AR & SMASK)) & FMASK;
                      AR = AD;
                      SC--;
                  }
                  set_reg(AC, AR);
                  set_reg(AC+1, MQ);
                  break;
              }
#endif
#if WAITS
              if (QWAITS) {   /* WAITS FIX instruction */
                  if (Mem_read(0, 0, 0))
                      goto last;
                  AR = MB;
                  BR = get_reg(AC);
                  if (hst_lnt) {
                      hst[hst_p].mb = AR;
                  }
                  MQ = 0;
                  AR = SWAP_AR;
                  goto ufa;
              }
#endif
 
               /* UUO */
    case 0105: case 0106: case 0107: 
    case 0110: case 0111: case 0112: case 0113:
    case 0114: case 0115: case 0116: case 0117:
    case 0120: case 0121: case 0122: case 0123:
    case 0124: case 0125: case 0126: case 0127:
#if PDP6
    case 0130:  /* UFA */
#endif

unasign:
#if !PDP6
              MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | (uint64)(AB);
              AB = 060 | maoff;
              uuo_cycle = 1;
              Mem_write(uuo_cycle, 0);
              AB += 1;
#if ITS
              if (QITS && one_p_arm) {
                  FLAGS |= ONEP;
                  one_p_arm = 0;
              }
#endif
              f_load_pc = 0;
#endif
              break;
#endif

    case 0133: /* IBP/ADJBP */
    case 0134: /* ILDB */
    case 0136: /* IDPB */
              if ((FLAGS & BYTI) == 0) {      /* BYF6 */
                  modify = 1;
                  if (Mem_read(0, !QITS, 0)) {
#if PDP6
                      FLAGS |= BYTI;
#endif
                      goto last;
                  }
                  AR = MB;
                  SC = (AR >> 24) & 077;
                  SCAD = (((AR >> 30) & 077) + (0777 ^ SC) + 1) & 0777;
                  if (SCAD & 0400) {
                      SC = ((0777 ^ ((AR >> 24) & 077)) + 044 + 1) & 0777;
#if KI | KL
                      AR = (AR & LMASK) | ((AR + 1) & RMASK);
#else
                      AR = (AR + 1) & FMASK;
#endif
                  } else
                      SC = SCAD;
                  AR &= PMASK;
                  AR |= (uint64)(SC & 077) << 30;
                  MB = AR;
                  if (Mem_write(0, !QITS))
                      goto last;
                  if ((IR & 04) == 0)
                      break;
                  goto ldb_ptr;
              }
              /* Fall through */

    case 0135:/* LDB */
    case 0137:/* DPB */
              if ((FLAGS & BYTI) == 0 || !BYF5) {
                  if (Mem_read(0, !QITS, 0))
                      goto last;
                  AR = MB;
ldb_ptr:
                  SC = (AR >> 30) & 077;
                  MQ = (uint64)(1) << ( 077 & (AR >> 24));
                  MQ -= 1;
                  f_load_pc = 0;
                  f_inst_fetch = 0;
                  f_pc_inh = 1;
                  FLAGS |= BYTI;
                  BYF5 = 1;
#if ITS
                  if (QITS && pi_cycle == 0 && mem_prot == 0) {
                     opc = PC | (FLAGS << 18);
                  }
#endif
              } else {
                  AB = AR & RMASK;
                  if ((IR & 06) == 6)
                      modify = 1;
                  if (Mem_read(0, 0, 0))
                      goto last;
                  AR = MB;
                  if ((IR & 06) == 4) {
                      AR = AR >> SC;
                      AR &= MQ;
                      set_reg(AC, AR);
                  } else {
                      BR = get_reg(AC);
                      BR = BR << SC;
                      MQ = MQ << SC;
                      AR &= CM(MQ);
                      AR |= BR & MQ;
                      MB = AR & FMASK;
                      Mem_write(0, 0);
                  }
                  FLAGS &= ~BYTI;
                  BYF5 = 0;
              }
              break;

    case 0131:/* DFN */
#if !PDP6
              AD = (CM(BR) + 1) & FMASK;
              SC = (BR >> 27) & 0777;
              BR = AR;
              AR = AD;
              AD = (CM(BR) + ((AD & MANT) == 0)) & FMASK;
              AR &= MANT;
              AR |= ((uint64)(SC & 0777)) << 27;
              BR = AR;
              AR = AD;
              MB = BR;
              set_reg(AC, AR);
              if (Mem_write(0, 0))
                 goto last;
#endif
              break;

    case 0132:/* FSC */
              SC = ((AB & RSIGN) ? 0400 : 0) | (AB & 0377);
              SCAD = GET_EXPO(AR);
              SC = (SCAD + SC) & 0777;

              flag1 = 0;
              if (AR & SMASK)
                 flag1 = 1;
#if !PDP6
              SMEAR_SIGN(AR);
              AR <<= 34;
              goto fnorm;
#else
              if (((SC & 0400) != 0) ^ ((SC & 0200) != 0))
                  fxu_hold_set = 1;
              if ((SC & 0400) != 0 && !pi_cycle) {
                  FLAGS |= OVR|FLTOVR|TRP1;
                  if (!fxu_hold_set)
                      FLAGS |= FLTUND;
                  check_apr_irq();
              }
              if (flag1) {
                 SC ^= 0377;
              } else if (AR == 0)
                 SC = 0;
              AR &= SMASK|MMASK;
              AR |= ((uint64)((SC) & 0377)) << 27;
              break;
#endif


    case 0150:  /* FSB */
    case 0151:  /* FSBL */
    case 0152:  /* FSBM */
    case 0153:  /* FSBB */
    case 0154:  /* FSBR */
    case 0155:  /* FSBRI, FSBRL on PDP6 */
    case 0156:  /* FSBRM */
    case 0157:  /* FSBRB */
              AD = (CM(AR) + 1) & FMASK;
              AR = BR;
              BR = AD;
              /* Fall through */

#if !PDP6
    case 0130:  /* UFA */
#endif
#if WAITS
ufa:
#endif
    case 0140:  /* FAD */
    case 0141:  /* FADL */
    case 0142:  /* FADM */
    case 0143:  /* FADB */
    case 0144:  /* FADR */
    case 0145:  /* FADRI, FSBRL on PDP6 */
    case 0146:  /* FADRM */
    case 0147:  /* FADRB */
              flag3 = 0;
              SC = ((BR >> 27) & 0777);
              if ((BR & SMASK) == (AR & SMASK)) {
                  SCAD = SC + (((AR >> 27) & 0777) ^ 0777) + 1;
              } else {
                  SCAD = SC + ((AR >> 27) & 0777);
              }
              SCAD &= 0777;
              if (((BR & SMASK) != 0) == ((SCAD & 0400) != 0)) {
                  AD = AR;
                  AR = BR;
                  BR = AD;
              }
              if ((SCAD & 0400) == 0) {
                 if ((AR & SMASK) == (BR & SMASK))
                      SCAD = ((SCAD ^ 0777) + 1) & 0777;
                 else
                      SCAD = (SCAD ^ 0777);
              } else {
                 if ((AR & SMASK) != (BR & SMASK))
                      SCAD = (SCAD + 1) & 0777;
              }

              /* Get exponent */
              SC = GET_EXPO(AR);
              /* Smear the signs */
              SMEAR_SIGN(AR);
              SMEAR_SIGN(BR);
              AR <<= 34;
              BR <<= 34;

              /* Shift smaller right */
              if (SCAD & 0400) {
                  SCAD = (01000 - SCAD);
                  if (SCAD < 61) {
                      AD = (BR & FPSBIT)? FPFMASK : 0;
                      BR = (BR >> SCAD) | (AD << (61 - SCAD));
                  } else {
#if PDP6
                      if (SCAD < 64)   /* Under limit */
#else
                      if (SCAD < 65)   /* Under limit */
#endif
                         BR = (BR & FPSBIT)? FPFMASK: 0;
                      else
                         BR = 0;
                  }
              }
              /* Do the addition now */
              AR = (AR + BR);

              /* Set flag1 to sign and make positive */
              flag1 = (AR & FPSBIT) != 0;
fnorm:
              if (((AR & FPSBIT) != 0) != ((AR & FPNBIT) != 0)) {
                  SC += 1;
                  flag3 = AR & 1;
                  AR = (AR & FPHBIT) | (AR >> 1);
              }
              if (AR != 0) {
#if !PDP6
                  AR &= ~077;  /* Save one extra bit */
#endif
                  if (((SC & 0400) != 0) ^ ((SC & 0200) != 0))
                       fxu_hold_set = 1;
                  if (IR != 0130) {   /* !UFA */
fnormx:
                      while (AR != 0 && ((AR & FPSBIT) != 0) == ((AR & FPNBIT) != 0) &&
                             ((AR & FPNBIT) != 0) == ((AR & FP1BIT) != 0)) {
                          SC --;
                          AR <<= 1;
#if PDP6
                          AR |= flag3;
                          flag3 = 0;
#endif
                      }
                      /* Handle edge case of a - and overflow bit */
                      if ((AR & 000777777777600000000000LL) == (FPSBIT|FPNBIT)) {
                          SC += 1;
                          AR = (AR & FPHBIT) | (AR >> 1);
                      }
                      if (!nrf && ((IR & 04) != 0)) {
                          f = (AR & FP1BIT) != 0;
                          if ((AR & FPRBIT2) != 0) {
#if !PDP6
                              /* FADR & FSBR do not rount if negative and equal round */
                              /* FMPR does not round if result negative and equal round */
                              if (((IR & 070) != 070 &&
                                      (AR & FPSBIT) != 0 &&
                                      (AR & FPRMASK) != FPRBIT2) ||
                                  (AR & FPSBIT) == 0 ||
                                  (AR & FPRMASK) != FPRBIT2)
#endif
                              AR += FPRBIT1;
                              nrf = 1;
#if !PDP6
                              AR &= ~FPRMASK;
#endif
                              flag3 = 0;
                              if (((AR & FP1BIT) != 0) != f) {
                                  SC += 1;
                                  flag3 = AR & 1;
                                  AR = (AR & FPHBIT) | (AR >> 1);
                              }
                              goto fnormx;
                          }
                      }
                  }

                  MQ = AR & FPRMASK;
                  AR >>= 34;
                  if (flag1)
                      AR |= SMASK;
              } else {
                 AR = MQ = 0;
                 SC = 0;
              }
              if (((SC & 0400) != 0) && !pi_cycle) {
                  FLAGS |= OVR|FLTOVR|TRP1;
#if !PDP6
                  if (!fxu_hold_set) {
                      FLAGS |= FLTUND;
                      MQ = 0;
                  }
#endif
                  check_apr_irq();
              }
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR &= SMASK|MMASK;
              AR |= ((uint64)(SCAD & 0377)) << 27;
#if PDP6
              /* FADL FADRL FSBL FSBRL FMPL FMPRL */
              if ((IR & 03) == 1) {
                  MQ = ((MQ << 1) & CMASK) | flag3/*| (flag3 << nrf)*/;
                  if (flag1)
                     MQ |= SMASK;
              }
#else
              /* FADL FSBL FMPL */
              if ((IR & 07) == 1) {
                  SC = (SC + (0777 ^  26)) & 0777;
                  if ((SC & 0400) != 0)
                      MQ = 0;
                  MQ = (MQ >> 7) & MMASK;
                  if (MQ != 0) {
                      SC ^= (SC & SMASK) ? 0377 : 0;
                      MQ |= ((uint64)(SC & 0377)) << 27;
                  }
              }
#endif
              if ((AR & MMASK) == 0)
                 AR = 0;


              /* Handle UFA */
              if (IR == 0130) {
                  set_reg(AC + 1, AR);
                  break;
              }
              break;

    case 0160:      /* FMP */
    case 0161:      /* FMPL */
    case 0162:      /* FMPM */
    case 0163:      /* FMPB */
    case 0164:      /* FMPR */
    case 0165:      /* FMPRI, FMPRL on PDP6 */
    case 0166:      /* FMPRM */
    case 0167:      /* FMPRB */
              /* Compute exponent */
              SC = (((BR & SMASK) ? 0777 : 0) ^ (BR >> 27)) & 0777;
              SC += (((AR & SMASK) ? 0777 : 0) ^ (AR >> 27)) & 0777;
              SC += 0600;
              SC &= 0777;
              /* Make positive and compute result sign */
              flag1 = 0;
              flag3 = 0;
              if (AR & SMASK) {
                  if ((AR & MMASK) == 0) {
                     AR = BIT9;
                     SC++;
                 } else
                     AR = CM(AR) + 1;
                 flag1 = 1;
                 flag3 = 1;
              }
              if (BR & SMASK) {
                  if ((BR & MMASK) == 0) {
                     BR = BIT9;
                     SC++;
                 } else
                 BR = CM(BR) + 1;
                 flag1 = !flag1;
              }
              AR &= MMASK;
              BR &= MMASK;
              AR = (AR * BR) << 7;
              if (flag1) {
                  AR = (AR ^ FPFMASK) + 1;
              }
#if PDP6
              AR &= ~0177;
              if (flag3)
                  AR |= 0177;
#endif
              goto fnorm;

    case 0170:      /* FDV */
    case 0172:      /* FDVM */
    case 0173:      /* FDVB */
    case 0174:      /* FDVR */
#if !PDP6
    case 0175:      /* FDVRI */
#endif
    case 0176:      /* FDVRM */
    case 0177:      /* FDVRB */
              flag1 = 0;
              flag3 = 0;
              SC = (int)((((BR & SMASK) ? 0777 : 0) ^ (BR >> 27)) & 0777);
              SCAD = (int)((((AR & SMASK) ? 0777 : 0) ^ (AR >> 27)) & 0777);
              if ((BR & (MMASK)) == 0) {
                  if (BR == SMASK) {
                      BR = BIT9;
                      SC--;
                  } else {
                      AR = BR;
                      break;
                  }
              }
              if (BR & SMASK) {
                  BR = CM(BR) + 1;
                  flag1 = 1;
                  flag3 = 1;
              }
              if (AR & SMASK) {
                  if ((AR & MMASK) == 0) {
                      AR = BIT9;
                      SC--;
                  } else
                      AR = CM(AR) + 1;
                  flag1 = !flag1;
              }
              SC = (SC + ((0777 ^ SCAD) + 1) + 0201) & 0777;
              /* Clear exponents */
              AR &= MMASK;
              BR &= MMASK;
              /* Check if we need to fix things */
              if (BR >= (AR << 1)) {
                  if (!pi_cycle)
                      FLAGS |= OVR|NODIV|FLTOVR|TRP1;
                  check_apr_irq();
                  sac_inh = 1;
                  break;      /* Done */
              }
              BR = (BR << 28);
              MB = AR;
              AR = BR / AR;
              if (AR != 0) {
#if !PDP6
                  if ((AR & BIT7) != 0) {
                      AR >>= 1;
                  } else {
                      SC--;
                  }
                  if (((SC & 0400) != 0) ^ ((SC & 0200) != 0) || SC == 0600)
                      fxu_hold_set = 1;
                  if (IR & 04) {
                      AR++;
                  }
                  AR >>= 1;
                  while ((AR & BIT9) == 0) {
                      AR <<= 1;
                      SC--;
                  }
#else
                  if (flag1) {
                     AR = ((AR ^ FMASK) + 1) & FMASK;
                     if ((AR & BIT7) == 0) {
                         AR >>= 1;
                     } else {
                         SC--;
                     }
                  } else {
                     if ((AR & BIT7) != 0) {
                         AR >>= 1;
                     } else {
                         SC--;
                     }
                  }
                  if (IR & 04) {
                      AR++;
                  }
                  AR >>= 1;
                  while ((((AR << 1) ^ AR) & BIT8) == 0) {
                      AR <<= 1;
                      SC--;
                  }
                  AR &= MMASK;
                  if (flag1)  {
                      AR |= SMASK;
                  }
#endif
              } else if (flag1) {
                 AR =  SMASK | BIT9;
                 SC++;
                 flag1 = 0;
              } else {
                 AR = 0;
                 SC = 0;
              }
              if (((SC & 0400) != 0) && !pi_cycle) {
                  FLAGS |= OVR|FLTOVR|TRP1;
                  if (!fxu_hold_set) {
                      FLAGS |= FLTUND;
                  }
                  check_apr_irq();
              }
#if !PDP6
              if (flag1)  {
                 AR = ((AR ^ MMASK) + 1) & MMASK;
                 AR |= SMASK;
              }
#endif
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR |= ((uint64)(SCAD & 0377)) << 27;
              break;

    case 0171:      /* FDVL */
#if PDP6
    case 0175:      /* FDVRL */
              flag1 = flag3 = 0;
              MQ = 0;
              if (BR & SMASK) {
                  BR = CM(BR);
                  if (MQ == 0)
                      BR = BR + 1;
                  flag1 = 1;
                  flag3 = 1;
              }
              if (AR & SMASK)
                  flag1 = !flag1;
              SC = (int)((((BR & SMASK) ? 0777 : 0) ^ (BR >> 27)) & 0777);
              SC += (int)((((AR & SMASK) ? 0 : 0777) ^ (AR >> 27)) & 0777);
              SC = (SC + 0201) & 0777;
              FE = (int)((((BR & SMASK) ? 0777 : 0) ^ (BR >> 27)) & 0777) - 26;
              SMEAR_SIGN(AR);
              SMEAR_SIGN(BR);
              /* FDT1 */
              MQ = (BR & 1) ? SMASK : 0;
              BR >>= 1;
              if (((AR & SMASK) == 0))
                  AD = (CM(AR) + BR + 1) ;
              else
                  AD = (AR + BR) ;

              /* Do actual divide */
              /* DST14 & DST15 */
              for (SCAD = 0; SCAD < 29; SCAD++) {
                   BR = (AD << 1) | ((MQ & SMASK) ? 1 : 0);
                   BR &= FMASK;
                   MQ = (MQ << 1);
                   MQ |= (AD & SMASK) == 0;
                   MQ &= FMASK;
                   if (((AR & SMASK) != 0) ^ ((MQ & 1) != 0))
                       AD = (CM(AR) + BR + 1) ;
                   else
                       AD = (AR + BR) ;
              }
              /* DST16 */
              BR = AD | ((MQ & SMASK) ? 1 : 0);
              BR &= FMASK;
              MQ = (MQ << 1);
              MQ |= (AD & SMASK) == 0;
              MQ &= FMASK;
              if (((AR & SMASK) != 0) ^ ((MQ & 1) != 0))
                  AD = (CM(AR) + BR + 1) ;
              else
                  AD = (AR + BR) ;
              if ((AD & C1) != 0)
                  BR = AD & FMASK;
              AR = MQ;
              if (flag3)
                  BR = ((BR ^ FMASK) + 1) & FMASK;
              MQ = BR;
              if (flag1)
                  AR = ((AR ^ FMASK) + 1) & FMASK;

              /* FDT1 */
              if (AR != 0) {
                  MQ = (MQ >> 1) & (CMASK >> 1);
                  if (AR & 1)
                      MQ |= BIT1;
                  AR >>= 1;
                  if (AR & BIT1)
                      AR |= SMASK;
                  /* NRT0 */
left:
                  SC++;
                  MQ = (MQ >> 1) & (CMASK >> 1);
                  if (AR & 1)
                     MQ |= BIT1;
                  AR >>= 1;
                  if (AR & BIT1)
                     AR |= SMASK;
                  while ((((AR >> 1) ^ AR) & BIT9) == 0) {
                      AR = (AR << 1) & FMASK;
                      if (MQ & BIT1)
                         AR |= 1;
                      MQ = (MQ << 1) & CMASK;
                      SC--;
                  }
                  if (!nrf && IR & 04) {
                      nrf = 1;
                      if ((MQ & BIT1) != 0)  {
                          AR++;
                          goto left;
                      }
                  }
                  if (AR & SMASK)
                      MQ |= SMASK;
                  if (((SC & 0400) != 0) ^ ((SC & 0200) != 0))
                      fxu_hold_set = 1;
              } else {
                 SC = 0;
              }
              if (((SC & 0400) != 0) && !pi_cycle) {
                  FLAGS |= OVR|FLTOVR|TRP1;
                  if (!fxu_hold_set) {
                      FLAGS |= FLTUND;
                  }
                  check_apr_irq();
              }
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR &= SMASK|MMASK;
              AR |= ((uint64)(SCAD & 0377)) << 27;

#else
              flag1 = flag3 = 0;
              SC = (int)((((BR & SMASK) ? 0777 : 0) ^ (BR >> 27)) & 0777);
              SC += (int)((((AR & SMASK) ? 0 : 0777) ^ (AR >> 27)) & 0777);
              SC = (SC + 0201) & 0777;
              FE = (int)((((BR & SMASK) ? 0777 : 0) ^ (BR >> 27)) & 0777) - 26;
              if (BR & SMASK) {
                  MQ = (CM(MQ) + 1) & MMASK;
                  BR = CM(BR);
                  if (MQ == 0)
                      BR = BR + 1;
                  flag1 = 1;
                  flag3 = 1;
              }
              MQ &= MMASK;
              if (AR & SMASK) {
                  AR = CM(AR) + 1;
                  flag1 = !flag1;
              }
              /* Clear exponents */
              AR &= MMASK;
              BR &= MMASK;
              /* Check if we need to fix things */
              if (BR >= (AR << 1)) {
                  if (!pi_cycle)
                      FLAGS |= OVR|NODIV|FLTOVR|TRP1;
                  check_apr_irq();
                  sac_inh = 1;
                  break;      /* Done */
              }
              BR = (BR << 27) + MQ;
              MB = AR;
              AR <<= 27;
              AD = 0;
              if (BR < AR) {
                 BR <<= 1;
                 SC--;
                 FE--;
              }
              for (SCAD = 0; SCAD < 27; SCAD++) {
                  AD <<= 1;
                  if (BR >= AR) {
                     BR = BR - AR;
                     AD |= 1;
                  }
                  BR <<= 1;
              }
              MQ = BR >> 28;
              AR = AD;
              SC++;
              if (AR != 0) {
                  if ((AR & BIT8) != 0) {
                      SC++;
                      FE++;
                      AR >>= 1;
                  }
                  while ((AR & BIT9) == 0) {
                      AR <<= 1;
                      SC--;
                  }
                  if (((SC & 0400) != 0) ^ ((SC & 0200) != 0))
                      fxu_hold_set = 1;
                  if (flag1)  {
                      AR = (AR ^ MMASK) + 1;
                      AR |= SMASK;
                  }
              } else if (flag1) {
                  FE = SC = 0;
              } else {
                  AR = 0;
                  SC = 0;
                  FE = 0;
              }
              if (((SC & 0400) != 0) && !pi_cycle) {
                  FLAGS |= OVR|FLTOVR|TRP1;
                  if (!fxu_hold_set) {
                      FLAGS |= FLTUND;
                  }
                  check_apr_irq();
              }
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR &= SMASK|MMASK;
              AR |= ((uint64)(SCAD & 0377)) << 27;

              if (MQ != 0) {
                  MQ &= MMASK;
                  if (flag3) {
                      MQ = (MQ ^ MMASK) + 1;
                      MQ |= SMASK;
                  }
                  if (FE < 0 /*FE & 0400*/) {
                     MQ = 0;
                     FE = 0;
                  } else
                     FE ^= (flag3) ? 0377 : 0;
                  MQ |= ((uint64)(FE & 0377)) << 27;
              }
#endif
              break;

                   /* FWT */
    case 0200:     /* MOVE */
    case 0201:     /* MOVEI */
    case 0202:     /* MOVEM */
    case 0203:     /* MOVES */
    case 0204:     /* MOVS */
    case 0205:     /* MOVSI */
    case 0206:     /* MOVSM */
    case 0207:     /* MOVSS */
    case 0503:     /* HLLS */
    case 0543:     /* HRRS */
              break;

    case 0214:     /* MOVM */
    case 0215:     /* MOVMI */
    case 0216:     /* MOVMM */
    case 0217:     /* MOVMS */
              if ((AR & SMASK) == 0)
                  break;
              /* Fall though */

    case 0210:     /* MOVN */
    case 0211:     /* MOVNI */
    case 0212:     /* MOVNM */
    case 0213:     /* MOVNS */
              flag1 = flag3 = 0;
              AD = CM(AR) + 1;
              if ((((AR & CMASK) ^ CMASK) + 1) & SMASK) {
#if !PDP6
                  FLAGS |= CRY1;
#endif
                  flag1 = 1;
              }
              if (AD & C1) {
#if !PDP6
                  FLAGS |= CRY0;
#endif
                  flag3 = 1;
              }
              if (flag1 != flag3 && !pi_cycle) {
                  FLAGS |= OVR|TRP1;
                  check_apr_irq();
              }
#if KI | KL
              if (AR == SMASK && !pi_cycle)
                  FLAGS |= TRP1;
#endif
              AR = AD & FMASK;
              break;

    case 0220:      /* IMUL */
    case 0221:      /* IMULI */
    case 0222:      /* IMULM */
    case 0223:      /* IMULB */
    case 0224:      /* MUL */
    case 0225:      /* MULI */
    case 0226:      /* MULM */
    case 0227:      /* MULB */
              flag3 = 0;
              if (AR & SMASK) {
                 AR = (CM(AR) + 1) & FMASK;
                 flag3 = 1;
              }
              if (BR & SMASK) {
                 BR = (CM(BR) + 1) & FMASK;
                 flag3 = !flag3;
              }

              if ((AR == 0) || (BR == 0)) {
                 AR = MQ = 0;
                 break;
              }
#if KA
              if (BR == SMASK)                /* Handle special case */
                 flag3 = !flag3;
#endif
              MQ = AR * (BR & RMASK);         /* 36 * low 18 = 54 bits */
              AR = AR * ((BR >> 18) & RMASK); /* 36 * high 18 = 54 bits */
              MQ += (AR << 18) & LMASK;       /* low order bits */
              AR >>= 18;
              AR = (AR << 1) + (MQ >> 35);
              MQ &= CMASK;                   /* low order only has 35 bits */
              if ((IR & 4) == 0) {           /* IMUL */
                  if (AR > flag3 && !pi_cycle) {
                     FLAGS |= OVR|TRP1;
                     check_apr_irq();
                  }
                  if (flag3) {
                      MQ ^= CMASK;
                      MQ++;
                      MQ |= SMASK;
                  }
                  AR = MQ;
                  break;
              }
              if ((AR & SMASK) != 0 && !pi_cycle) {
                 FLAGS |= OVR|TRP1;
                 check_apr_irq();
              }
              if (flag3) {
                 AR ^= FMASK;
                 MQ ^= CMASK;
                 MQ += 1;
                 if ((MQ & SMASK) != 0) {
                    AR += 1;
                    MQ &= CMASK;
                 }
              }
              AR &= FMASK;
              MQ = (MQ & ~SMASK) | (AR & SMASK);
#if KA
              if (BR == SMASK && (AR & SMASK))  /* Handle special case */
                  FLAGS |= OVR;
#endif
              break;

    case 0230:       /* IDIV */
    case 0231:       /* IDIVI */
    case 0232:       /* IDIVM */
    case 0233:       /* IDIVB */
              flag1 = 0;
              flag3 = 0;
              if (BR & SMASK) {
                 BR = (CM(BR) + 1) & FMASK;
                 flag1 = !flag1;
              }

              if (BR == 0) {          /* Check for overflow */
                  FLAGS |= OVR|NODIV; /* Overflow and No Divide */
                  sac_inh=1;          /* Don't touch AC */
                  check_apr_irq();
                  break;              /* Done */
              }

#if !PDP6 
              if (AR == SMASK && BR == 1) {
                  FLAGS |= OVR|NODIV; /* Overflow and No Divide */
                  sac_inh=1;          /* Don't touch AC */
                  check_apr_irq();
                  break;              /* Done */
              }
#else
              if (AR == SMASK && BR == 1) {
                  MQ = 0;
                  AR = 0;
                  break;              /* Done */
              }
#endif

              if (AR & SMASK) {
                 AR = (CM(AR) + 1) & FMASK;
                 flag1 = !flag1;
                 flag3 = 1;
              }

              MQ = AR % BR;
              AR = AR / BR;
              if (flag1)
                 AR = (CM(AR) + 1) & FMASK;
              if (flag3)
                 MQ = (CM(MQ) + 1) & FMASK;
              break;

    case 0234:       /* DIV */
    case 0235:       /* DIVI */
    case 0236:       /* DIVM */
    case 0237:       /* DIVB */
              flag1 = 0;
              if (AR & SMASK) {
                  AD = (CM(MQ) + 1) & FMASK;
                  MQ = AR;
                  AR = AD;
                  AD = (CM(MQ)) & FMASK;
                  MQ = AR;
                  AR = AD;
                  if ((MQ & CMASK) == 0)
                      AR = (AR + 1) & FMASK;
                  flag1 = 1;
              }

              if (BR & SMASK)
                   AD = (AR + BR) & FMASK;
              else
                   AD = (AR + CM(BR) + 1) & FMASK;
              MQ = (MQ << 1) & FMASK;
              MQ |= (AD & SMASK) != 0;
              SC = 35;
              if ((AD & SMASK) == 0) {
                  FLAGS |= OVR|NODIV|TRP1; /* Overflow and No Divide */
                  i_flags = 0;
                  sac_inh=1;
                  check_apr_irq();
                  break;      /* Done */
              }

              while (SC != 0) {
                      if (((BR & SMASK) != 0) ^ ((MQ & 01) != 0))
                           AD = (AR + CM(BR) + 1);
                      else
                           AD = (AR + BR);
                      AR = (AD << 1) | ((MQ & SMASK) ? 1 : 0);
                      AR &= FMASK;
                      MQ = (MQ << 1) & FMASK;
                      MQ |= (AD & SMASK) == 0;
                      SC--;
              }
              if (((BR & SMASK) != 0) ^ ((MQ & 01) != 0))
                  AD = (AR + CM(BR) + 1);
              else
                  AD = (AR + BR);
              AR = AD & FMASK;
              MQ = (MQ << 1) & FMASK;
              MQ |= (AD & SMASK) == 0;
              if (AR & SMASK) {
                   if (BR & SMASK)
                        AD = (AR + CM(BR) + 1) & FMASK;
                   else
                        AD = (AR + BR) & FMASK;
                   AR = AD;
              }

              if (flag1)
                  AR = (CM(AR) + 1) & FMASK;
              if (flag1 ^ ((BR & SMASK) != 0)) {
                  AD = (CM(MQ) + 1) & FMASK;
                  MQ = AR;
                  AR = AD;
              } else {
                  AD = MQ;
                  MQ = AR;
                  AR = AD;
              }
              break;

               /* Shift */
    case 0240: /* ASH */
              SC = ((AB & RSIGN) ? (0377 ^ AB) + 1 : AB) & 0377;
              if (SC == 0)
                  break;
              AD = (AR & SMASK) ? FMASK : 0;
              if (AB & RSIGN) {
                  if (SC < 35)
                     AR = ((AR >> SC) | (AD << (36 - SC))) & FMASK;
                 else
                     AR = AD;
              } else {
                 if (((AD << SC) & ~CMASK) != ((AR << SC) & ~CMASK)) {
                     FLAGS |= OVR|TRP1;
                     check_apr_irq();
                 }
                 AR = ((AR << SC) & CMASK) | (AR & SMASK);
              }
              break;

    case 0241: /* ROT */
              SC = (AB & RSIGN) ?
                     ((AB & 0377) ? (((0377 ^ AB) + 1) & 0377) : 0400)
                   : (AB & 0377);
              if (SC == 0)
                  break;
              SC = SC % 36;
              if (AB & RSIGN)
                  SC = 36 - SC;
              AR = ((AR << SC) | (AR >> (36 - SC))) & FMASK;
              break;

    case 0242: /* LSH */
              SC = ((AB & RSIGN) ? (0377 ^ AB) + 1 : AB) & 0377;
              if (SC != 0) {
                 if (SC > 36){
                     AR = 0;
                 } else if (AB & RSIGN) {
                     AR = AR >> SC;
                 } else {
                     AR = (AR << SC) & FMASK;
                 }
              }
              break;

    case 0243:  /* JFFO */
#if !PDP6
              SC = 0;
              if (AR != 0) {
#if ITS
                  if ((FLAGS & USER) && QITS) {
                      jpc = PC;
                  }
#endif
                  PC = AB;
                  f_pc_inh = 1;
                  SC = nlzero(AR);
              }
              set_reg(AC + 1, SC);
#endif
              break;

    case 0244: /* ASHC */
              SC = ((AB & RSIGN) ? (0377 ^ AB) + 1 : AB) & 0377;
              if (SC == 0)
                  break;
              if (SC > 70)
                   SC = 70;
              AD = (AR & SMASK) ? FMASK : 0;
              AR &= CMASK;
              MQ &= CMASK;
              if (AB & RSIGN) {
                 if (SC >= 35) {
                     MQ = ((AR >> (SC - 35)) | (AD << (70 - SC))) & FMASK;
                     AR = AD;
                 } else {
                     MQ = (AD & SMASK) | (MQ >> SC) |
                             ((AR << (35 - SC)) & CMASK);
                     AR = ((AD & SMASK) |
                             ((AR >> SC) | (AD << (35 - SC)))) & FMASK;
                 }
              } else {
                 if (SC >= 35) {
#if !PDP6
                      if (((AD << SC) & ~CMASK) != ((AR << SC) & ~CMASK)) {
                         FLAGS |= OVR|TRP1;
                         check_apr_irq();
                      }
#endif
                      AR = (AD & SMASK) | ((MQ << (SC - 35)) & CMASK);
                      MQ = (AD & SMASK);
                 } else {
                      if ((((AD & CMASK) << SC) & ~CMASK) != ((AR << SC) & ~CMASK)) {
                         FLAGS |= OVR|TRP1;
                         check_apr_irq();
                      }
                      AR = (AD & SMASK) | ((AR << SC) & CMASK) |
                             (MQ >> (35 - SC));
                      MQ = (AD & SMASK) | ((MQ << SC) & CMASK);
                 }
              }
              break;

    case 0245: /* ROTC */
              SC = (AB & RSIGN) ?
                      ((AB & 0377) ? (((0377 ^ AB) + 1) & 0377) : 0400)
                    : (AB & 0377);
              if (SC == 0)
                  break;
              SC = SC % 72;
              if (AB & RSIGN)
                  SC = 72 - SC;
              if (SC >= 36) {
                  AD = MQ;
                  MQ = AR;
                  AR = AD;
                  SC -= 36;
              }
              AD = ((AR << SC) | (MQ >> (36 - SC))) & FMASK;
              MQ = ((MQ << SC) | (AR >> (36 - SC))) & FMASK;
              AR = AD;
              break;

    case 0246: /* LSHC */
              SC = ((AB & RSIGN) ? (0377 ^ AB) + 1 : AB) & 0377;
              if (SC == 0)
                  break;
              if (SC > 71) {
                  AR = 0;
                  MQ = 0;
              } else {
                  if (SC > 36) {
                     if (AB & RSIGN) {
                         MQ = AR;
                         AR = 0;
                     } else {
                         AR = MQ;
                         MQ = 0;
                     }
                     SC -= 36;
                 }
                 if (AB & RSIGN) {
                     MQ = ((MQ >> SC) | (AR << (36 - SC))) & FMASK;
                     AR = AR >> SC;
                 } else {
                     AR = ((AR << SC) | (MQ >> (36 - SC))) & FMASK;
                     MQ = (MQ << SC) & FMASK;
                 }
              }
              break;

          /* Branch */
    case 0250:  /* EXCH */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, BR);
              break;

    case 0251: /* BLT */
              BR = AB;
              do {
                  if (sim_interval <= 0) {
                       sim_process_event();
                  }
                  /* Allow for interrupt */
                  if (pi_pending) {
                      pi_rq = check_irq_level();
                      if (pi_rq) {
                          f_pc_inh = 1;
                          f_load_pc = 0;
                          f_inst_fetch = 0;
                          set_reg(AC, AR);
                          break;
                      }
                  }
                  AB = (AR >> 18) & RMASK;
                  if (Mem_read(0, 0, 0)) {
#if ITS
                       /* On ITS if access error, allow for skip */
                       if (QITS && (xct_flag & 04) != 0)
                            f_pc_inh =0;
                       else
#endif
#if PDP6
                       AR = AOB(AR) & FMASK;
#endif
                       f_pc_inh = 1;
#if KA & ITS
                       if (QITS)
                           set_reg(AC, AR);
#else
                       set_reg(AC, AR);
#endif
                       goto last;
                  }
                  AB = (AR & RMASK);
                  if (Mem_write(0, 0)) {
#if ITS
                       /* On ITS if access error, allow for skip */
                       if (QITS && (xct_flag & 04) != 0)
                            f_pc_inh =0;
                       else
#endif
#if PDP6
                       AR = AOB(AR) & FMASK;
#endif
                       f_pc_inh = 1;
#if KA & ITS
                       if (QITS)
                           set_reg(AC, AR);
#else
                       set_reg(AC, AR);
#endif
                       goto last;
                  }
                  AD = (AR & RMASK) + CM(BR) + 1;
                  AR = AOB(AR);
              } while ((AD & C1) == 0);
              break;

    case 0252: /* AOBJP */
              AR = AOB(AR);
              if ((AR & SMASK) == 0) {
#if ITS
                  if ((FLAGS & USER) && QITS) {
                      jpc = PC;
                  }
#endif
                  PC_CHANGE
                  PC = AB;
                  f_pc_inh = 1;
              }
              break;

    case 0253: /* AOBJN */
              AR = AOB(AR);
              if ((AR & SMASK) != 0) {
#if ITS
                  if ((FLAGS & USER) && QITS) {
                      jpc = PC;
                  }
#endif
                  PC_CHANGE
                  PC = AB;
                  f_pc_inh = 1;
              }
              break;

    case 0254: /* JRST */      /* AR Frm PC */
              if (uuo_cycle | pi_cycle) {
                 FLAGS &= ~USER; /* Clear USER */
#if ITS
                 if (QITS && one_p_arm) {
                     FLAGS |= ONEP;
                     one_p_arm = 0;
                 }
#endif
              }
              /* JEN */
              if (AC & 010) { /* Restore interrupt level. */
#if KI | KL
                 if ((FLAGS & (USER|USERIO)) == USER ||
                     (FLAGS & (USER|PUBLIC)) == PUBLIC) {
#else
                 if ((FLAGS & (USER|USERIO)) == USER) {
#endif
                      goto muuo;
                 } else {
                      pi_restore = 1;
                 }
              }
              /* HALT */
              if (AC & 04) {
#if KI | KL
                 if ((FLAGS & (USER|USERIO)) == USER ||
                     (FLAGS & (USER|PUBLIC)) == PUBLIC) {
#else
                 if ((FLAGS & (USER|USERIO)) == USER) {
#endif
                      goto muuo;
                 } else {
                      reason = STOP_HALT;
                 }
              }
#if ITS
              if ((FLAGS & USER) && QITS) {
                  jpc = PC;
              }
#endif
              PC = AR & RMASK;
              PC_CHANGE
              /* JRSTF */
              if (AC & 02) {
                 FLAGS &= ~(OVR|NODIV|FLTUND|BYTI|FLTOVR|CRY1|CRY0|TRP1|TRP2|PCHNG);
                 AR >>= 23; /* Move into position */
                 /* If executive mode, copy USER and UIO */
                 if ((FLAGS & (PUBLIC|USER)) == 0)
                    FLAGS |= AR & (USER|USERIO|PUBLIC);
                 /* Can always clear UIO */
                 if ((AR & USERIO) == 0) {
                    FLAGS &= ~USERIO;
                 }
#if PDP6
                 user_io = (FLAGS & USERIO) != 0;
#endif
                 FLAGS |= AR & (OVR|NODIV|FLTUND|BYTI|FLTOVR|CRY1|CRY0|\
                                 TRP1|TRP2|PUBLIC|PCHNG);
#if ITS
                 if (QITS)
                     FLAGS |= AR & (PURE|ONEP);
#endif
#if KI
                 FLAGS &= ~PRV_PUB;
                 if ((FLAGS & USER) == 0)
                    FLAGS |= (AR & OVR) ? PRV_PUB : 0;
#endif
                 check_apr_irq();
              }

              if (AC & 01) {  /* Enter User Mode */
#if KI | KL
                 FLAGS &= ~(PUBLIC|PRV_PUB);
#else
                 FLAGS |= USER;
#endif
              }
              f_pc_inh = 1;
              break;

    case 0255: /* JFCL */
              if ((FLAGS >> 9) & AC) {
#if ITS
                  if ((FLAGS & USER) && QITS) {
                      jpc = PC;
                  }
#endif
                  PC = AR & RMASK;
                  f_pc_inh = 1;
              }
              FLAGS &=  037777 ^ (AC << 9);
              break;

    case 0256: /* XCT */
              f_load_pc = 0;
              f_pc_inh = 1;
              xct_flag = 0;
#if BBN
              if (QBBN && (FLAGS & USER) == 0)
                   xct_flag = AC;
#endif
#if KI | KL
              if ((FLAGS & USER) == 0)
                  xct_flag = AC;
#endif
#if WAITS
              if (QWAITS && (FLAGS & USER) == 0)
                   xct_flag = AC;
#endif
#if ITS
              if (QITS && one_p_arm) {
                  FLAGS |= ONEP;
                  one_p_arm = 0;
              }
#endif
              break;

    case 0257:  /* MAP */
#if KI | KL
              f = AB >> 9;
              /* Check if Paging Enabled */
              if (!page_enable || AB < 020) {
                  AR = 0020000LL + f; /* direct map */
                  set_reg(AC, AR);
                  break;
              }
              flag1 = (FLAGS & USER) != 0;

              /* Figure out if this is a user space access */
              if (xct_flag != 0 && !flag1) {
                  if ((xct_flag & 2) != 0) {
                      flag1 = (FLAGS & USERIO) != 0;
                  }
              }

              flag3 = 0;
              /* If user, check if small user enabled */
              if (flag1) {
                  if (small_user && (f & 0340) != 0) {
                      AR = 0420000LL;     /* Outside small user space registers */
                      set_reg(AC, AR);
                      break;
                  }
              } else {
                  /* Handle system mapping */
                  /* Pages 340-377 via UBR */
                  if ((f & 0740) == 0340) {
                       f += 01000 - 0340;
                       flag3 = 2;
                  /* Pages 400-777 via EBR */
                  } else if (f & 0400) {
                       flag3 = 1;
                  /* Pages 000-037 direct map */
                  } else {
                       AR = 0020000LL + f; /* direct map */
                       set_reg(AC, AR);
                       break;
                   }
              }
              /* Map the page */
              if (flag3&1) {
                  AR = e_tlb[f];
                  if (AR == 0) {
                     AR = M[eb_ptr + (f >> 1)];
                     e_tlb[f & 0776] = RMASK & (AR >> 18);
                     e_tlb[f | 1] = RMASK & AR;
                     AR = e_tlb[f];
                     if (AR == 0) {
                         AR = 0437777;
                         set_reg(AC, AR);
                         break;
                     }
                     pag_reload = ((pag_reload + 1) & 037) | 040;
                  }
                  last_page = ((f ^ 0777) << 1) | 1;
              } else {
                  AR = u_tlb[f];
                  if (AR == 0) {
                     AR = M[ub_ptr + (f >> 1)];
                     u_tlb[f & 01776] = RMASK & (AR >> 18);
                     u_tlb[f | 1] = RMASK & AR;
                     AR = u_tlb[f];
                     if (AR == 0) {
                         AR = 0437777;
                         set_reg(AC, AR);
                         break;
                     }
                     pag_reload = ((pag_reload + 1) & 037) | 040;
                  }
                  if (flag3 & 2)
                      last_page = (((f-0440) ^ 0777) << 1) | 1;
                  else
                      last_page = ((f ^ 0777) << 1);
              }
              if ((AR & 0400000LL) == 0)
                  AR &= 0437777LL; /* Return valid entry for page */
              AR ^= 0400000LL;  /* Flip access status. */
              set_reg(AC, AR);
#endif
              break;

              /* Stack, JUMP */
    case 0260:  /* PUSHJ */     /* AR Frm PC */
              MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + !pi_cycle) & RMASK);
#if KI
              if ((FLAGS & USER) == 0) {
                  MB &= ~SMASK;
                  MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
              }
#endif
              BR = AB;
              AR = AOB(AR);
              AB = AR & RMASK;
              if (Mem_write(uuo_cycle | pi_cycle, 0))
                 goto last;
              FLAGS &= ~ (BYTI|ADRFLT|TRP1|TRP2);
              if (AR & C1) {
#if KI | KL
                 if (!pi_cycle)
                     FLAGS |= TRP2;
#else
                 push_ovf = 1;
                 check_apr_irq();
#endif
              }
#if !PDP6
              if (uuo_cycle | pi_cycle) {
                 FLAGS &= ~(USER|PUBLIC); /* Clear USER */
#if ITS
                 if (QITS && one_p_arm) {
                     FLAGS |= ONEP;
                     one_p_arm = 0;
                 }
#endif
              }
#endif
#if ITS
              if ((FLAGS & USER) && QITS) {
                  jpc = PC;
              }
#endif
              PC = BR & RMASK;
              PC_CHANGE
              f_pc_inh = 1;
              break;

    case 0261: /* PUSH */
              AR = AOB(AR);
              AB = AR & RMASK;
              if (AR & C1) {
#if KI | KL
                 if (!pi_cycle)
                     FLAGS |= TRP2;
#else
                 push_ovf = 1;
                 check_apr_irq();
#endif
              }
              MB = BR;
              if (Mem_write(0, 0))
                 goto last;
              break;

    case 0262: /* POP */
              AB = AR & RMASK;
              if (Mem_read(0, 0, 0))
                  goto last;
              AR = SOB(AR);
              AB = BR & RMASK;
              if (Mem_write(0, 0))
                  goto last;
              if ((AR & C1) == 0) {
#if KI | KL
                  if (!pi_cycle)
                      FLAGS |= TRP2;
#else
                  push_ovf = 1;
                  check_apr_irq();
#endif
              }
              break;

    case 0263: /* POPJ */
              AB = AR & RMASK;
              if (Mem_read(0, 0, 0))
                  goto last;
#if ITS
              if ((FLAGS & USER) && QITS) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
              PC = MB & RMASK;
              AR = SOB(AR);
              if ((AR & C1) == 0) {
#if KI | KL
                  if (!pi_cycle)
                     FLAGS |= TRP2;
#else
                  push_ovf = 1;
                  check_apr_irq();
#endif
              }
              f_pc_inh = 1;
              break;

    case 0264: /* JSR */       /* AR Frm PC */
              MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + !pi_cycle) & RMASK);
#if KI
              if ((FLAGS & USER) == 0) {
                  MB &= ~SMASK;
                  MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
              }
#endif
#if PDP6
              if (ill_op | uuo_cycle | pi_cycle | ex_uuo_sync) {
                 ill_op = 0;
                 ex_uuo_sync = 0;
#else
              if (uuo_cycle | pi_cycle) {
#endif
                 FLAGS &= ~(USER|PUBLIC); /* Clear USER */
              }
              if (Mem_write(0, 0))
                  goto last;
              FLAGS &= ~ (BYTI|ADRFLT|TRP1|TRP2);
#if ITS
              if ((FLAGS & USER) && QITS) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
              PC = (AR + 1) & RMASK;
              f_pc_inh = 1;
              break;

    case 0265: /* JSP */       /* AR Frm PC */
              AD = (((uint64)(FLAGS) << 23) & LMASK) |
                      ((PC + !pi_cycle) & RMASK);
              FLAGS &= ~ (BYTI|ADRFLT|TRP1|TRP2);
#if KI
              if ((FLAGS & USER) == 0) {
                  AD &= ~SMASK;
                  AD |= (FLAGS & PRV_PUB) ? SMASK : 0;
              }
#endif
#if !PDP6
              if (uuo_cycle | pi_cycle) {
                 FLAGS &= ~(USER|PUBLIC); /* Clear USER */
              }
#endif
#if ITS
              if ((FLAGS & USER) && QITS) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
              PC = AR & RMASK;
              AR = AD;
              f_pc_inh = 1;
              break;

    case 0266: /* JSA */       /* AR Frm PC */
              set_reg(AC, (AR << 18) | ((PC + 1) & RMASK));
#if !PDP6
              if (uuo_cycle | pi_cycle) {
                 FLAGS &= ~(USER|PUBLIC); /* Clear USER */
              }
#endif
#if ITS
              if ((FLAGS & USER) && QITS) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
              PC = AR & RMASK;
              AR = BR;
              break;

    case 0267: /* JRA */
              AD = AB;
              AB = (get_reg(AC) >> 18) & RMASK;
              if (Mem_read(uuo_cycle | pi_cycle, 0, 0))
                   goto last;
              set_reg(AC, MB);
#if ITS
              if ((FLAGS & USER) && QITS) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
              PC = AD & RMASK;
              f_pc_inh = 1;
              break;

    case 0270: /* ADD */
    case 0271: /* ADDI */
    case 0272: /* ADDM */
    case 0273: /* ADDB */
              flag1 = flag3 = 0;
              if (((AR & CMASK) + (BR & CMASK)) & SMASK) {
                  FLAGS |= CRY1;
                  flag1 = 1;
              }
              AR = AR + BR;
              if (AR & C1) {
                  if (!pi_cycle)
                      FLAGS |= CRY0;
                  flag3 = 1;
              }
              if (flag1 != flag3) {
                  if (!pi_cycle)
                      FLAGS |= OVR|TRP1;
                  check_apr_irq();
              }
              break;

    case 0274: /* SUB */
    case 0275: /* SUBI */
    case 0276: /* SUBM */
    case 0277: /* SUBB */
              flag1 = flag3 = 0;
              if ((((AR & CMASK) ^ CMASK) + (BR & CMASK) + 1) & SMASK) {
                  FLAGS |= CRY1;
                  flag1 = 1;
              }
              AR = CM(AR) + BR + 1;
              if (AR & C1) {
                  if (!pi_cycle)
                      FLAGS |= CRY0;
                  flag3 = 1;
              }
              if (flag1 != flag3) {
                  if (!pi_cycle)
                      FLAGS |= OVR|TRP1;
                  check_apr_irq();
              }
              break;

    case 0300:    /* CAI   */
    case 0301:    /* CAIL  */
    case 0302:    /* CAIE  */
    case 0303:    /* CAILE */
    case 0304:    /* CAIA  */
    case 0305:    /* CAIGE */
    case 0306:    /* CAIN  */
    case 0307:    /* CAIG  */
    case 0310:    /* CAM   */
    case 0311:    /* CAML  */
    case 0312:    /* CAME  */
    case 0313:    /* CAMLE */
    case 0314:    /* CAMA  */
    case 0315:    /* CAMGE */
    case 0316:    /* CAMN  */
    case 0317:    /* CAMG  */
              f = 0;
              AD = (CM(AR) + BR) + 1;
#if PDP6
              if (AD & C1)
                  FLAGS |= CRY0;
              if ((AR & SMASK) != (BR & SMASK))
                  FLAGS |= CRY1;
#endif
              if (((BR & SMASK) != 0) && (AR & SMASK) == 0)
                 f = 1;
              if (((BR & SMASK) == (AR & SMASK)) &&
                      (AD & SMASK) != 0)
                 f = 1;
              goto skip_op;

    case 0320:    /* JUMP   */
    case 0321:    /* JUMPL  */
    case 0322:    /* JUMPE  */
    case 0323:    /* JUMPLE */
    case 0324:    /* JUMPA  */
    case 0325:    /* JUMPGE */
    case 0326:    /* JUMPN  */
    case 0327:    /* JUMPG  */
              AD = AR;
              f = ((AD & SMASK) != 0);
              goto jump_op;                   /* JUMP, SKIP */

    case 0330:    /* SKIP   */
    case 0331:    /* SKIPL  */
    case 0332:    /* SKIPE  */
    case 0333:    /* SKIPLE */
    case 0334:    /* SKIPA  */
    case 0335:    /* SKIPGE */
    case 0336:    /* SKIPN  */
    case 0337:    /* SKIPG  */
              AD = AR;
              f = ((AD & SMASK) != 0);
              goto skip_op;                   /* JUMP, SKIP */

    case 0340:     /* AOJ   */
    case 0341:     /* AOJL  */
    case 0342:     /* AOJE  */
    case 0343:     /* AOJLE */
    case 0344:     /* AOJA  */
    case 0345:     /* AOJGE */
    case 0346:     /* AOJN  */
    case 0347:     /* AOJG  */
    case 0360:     /* SOJ   */
    case 0361:     /* SOJL  */
    case 0362:     /* SOJE  */
    case 0363:     /* SOJLE */
    case 0364:     /* SOJA  */
    case 0365:     /* SOJGE */
    case 0366:     /* SOJN  */
    case 0367:     /* SOJG  */
              flag1 = flag3 = 0;
              AD = (IR & 020) ? FMASK : 1;
              if (((AR & CMASK) + (AD & CMASK)) & SMASK) {
                  if (!pi_cycle)
                     FLAGS |= CRY1;
                  flag1 = 1;
              }
              AD = AR + AD;
#if PDP6
              if (AD == FMASK && !pi_cycle)
                     FLAGS |= CRY0;
              if ((AD & CMASK) == CMASK && !pi_cycle)
                     FLAGS |= CRY1;
#endif
              if (AD & C1) {
                  if (!pi_cycle)
                     FLAGS |= CRY0;
                  flag3 = 1;
              }
              if (flag1 != flag3  && !pi_cycle) {
                  FLAGS |= OVR|TRP1;
                  check_apr_irq();
              }
              f = ((AD & SMASK) != 0);
jump_op:
              AD &= FMASK;
              AR = AD;
              f |= ((AD == 0) << 1);
              f = f & IR;
              if (((IR & 04) != 0) == (f == 0)) {
#if ITS
                  if ((FLAGS & USER) && QITS) {
                      jpc = PC;
                  }
#endif
                  PC_CHANGE
                  PC = AB;
                  f_pc_inh = 1;
              }
              break;

    case 0350:     /* AOS   */
    case 0351:     /* AOSL  */
    case 0352:     /* AOSE  */
    case 0353:     /* AOSLE */
    case 0354:     /* AOSA  */
    case 0355:     /* AOSGE */
    case 0356:     /* AOSN  */
    case 0357:     /* AOSG  */
    case 0370:     /* SOS   */
    case 0371:     /* SOSL  */
    case 0372:     /* SOSE  */
    case 0373:     /* SOSLE */
    case 0374:     /* SOSA  */
    case 0375:     /* SOSGE */
    case 0376:     /* SOSN  */
    case 0377:     /* SOSG  */
              flag1 = flag3 = 0;
              AD = (IR & 020) ? FMASK : 1;
              if (((AR & CMASK) + (AD & CMASK)) & SMASK) {
                  if (!pi_cycle)
                     FLAGS |= CRY1;
                  flag1 = 1;
              }
              AD = AR + AD;
              if (AD & C1) {
                  if (!pi_cycle)
                      FLAGS |= CRY0;
                  flag3 = 1;
              }
              if (flag1 != flag3 && !pi_cycle) {
                  FLAGS |= OVR|TRP1;
                  check_apr_irq();
              }
              f = ((AD & SMASK) != 0);
skip_op:
              AD &= FMASK;
              AR = AD;
              f |= ((AD == 0) << 1);
              f = f & IR;
              if (((IR & 04) != 0) == (f == 0)) {
#if PDP6
                  if (pi_cycle)
                     f_pc_inh = 1;
#endif
                  PC_CHANGE
                  PC = (PC + 1) & RMASK;
#if KI | KL
              } else if (pi_cycle) {
                   pi_ov = pi_hold = 1;
#endif
              }
              break;

              /* Bool */
    case 0400:    /* SETZ  */
    case 0401:    /* SETZI */
    case 0402:    /* SETZM */
    case 0403:    /* SETZB */
              AR = 0;                   /* SETZ */
              break;

    case 0404:    /* AND  */
    case 0405:    /* ANDI */
    case 0406:    /* ANDM */
    case 0407:    /* ANDB */
              AR = AR & BR;             /* AND */
              break;

    case 0410:    /* ANDCA  */
    case 0411:    /* ANDCAI */
    case 0412:    /* ANDCAM */
    case 0413:    /* ANDCAB */
              AR = AR & CM(BR);         /* ANDCA */
              break;

    case 0414:    /* SETM  */
    case 0415:    /* SETMI */
    case 0416:    /* SETMM */
    case 0417:    /* SETMB */
                                         /* SETM */
              break;

    case 0420:    /* ANDCM  */
    case 0421:    /* ANDCMI */
    case 0422:    /* ANDCMM */
    case 0423:    /* ANDCMB */
              AR = CM(AR) & BR;         /* ANDCM */
              break;

    case 0424:    /* SETA  */
    case 0425:    /* SETAI */
    case 0426:    /* SETAM */
    case 0427:    /* SETAB */
              AR = BR;                  /* SETA */
              break;

    case 0430:    /* XOR  */
    case 0431:    /* XORI */
    case 0432:    /* XORM */
    case 0433:    /* XORB */
              AR = AR ^ BR;             /* XOR */
              break;

    case 0434:    /* IOR  */
    case 0435:    /* IORI */
    case 0436:    /* IORM */
    case 0437:    /* IORB */
              AR = CM(CM(AR) & CM(BR)); /* IOR */
              break;

    case 0440:    /* ANDCB  */
    case 0441:    /* ANDCBI */
    case 0442:    /* ANDCBM */
    case 0443:    /* ANDCBB */
              AR = CM(AR) & CM(BR);     /* ANDCB */
              break;

    case 0444:    /* EQV  */
    case 0445:    /* EQVI */
    case 0446:    /* EQVM */
    case 0447:    /* EQVB */
              AR = CM(AR ^ BR);         /* EQV */
              break;

    case 0450:    /* SETCA  */
    case 0451:    /* SETCAI */
    case 0452:    /* SETCAM */
    case 0453:    /* SETCAB */
              AR = CM(BR);              /* SETCA */
              break;

    case 0454:    /* ORCA  */
    case 0455:    /* ORCAI */
    case 0456:    /* ORCAM */
    case 0457:    /* ORCAB */
              AR = CM(CM(AR) & BR);     /* ORCA */
              break;

    case 0460:    /* SETCM  */
    case 0461:    /* SETCMI */
    case 0462:    /* SETCMM */
    case 0463:    /* SETCMB */
              AR = CM(AR);              /* SETCM */
              break;

    case 0464:    /* ORCM  */
    case 0465:    /* ORCMI */
    case 0466:    /* ORCMM */
    case 0467:    /* ORCMB */
              AR = CM(AR & CM(BR));     /* ORCM */
              break;

    case 0470:    /* ORCB  */
    case 0471:    /* ORCBI */
    case 0472:    /* ORCBM */
    case 0473:    /* ORCBB */
              AR = CM(AR & BR);         /* ORCB */
              break;

    case 0474:    /* SETO  */
    case 0475:    /* SETOI */
    case 0476:    /* SETOM */
    case 0477:    /* SETOB */
              AR = FMASK;               /* SETO */
              break;


    case 0547:    /* HLRS */
              BR = SWAP_AR;
              /* Fall Through */

    case 0500:    /* HLL  */
    case 0501:    /* HLLI */
    case 0502:    /* HLLM */
    case 0504:    /* HRL  */
    case 0505:    /* HRLI */
    case 0506:    /* HRLM */
              AR = (AR & LMASK) | (BR & RMASK);
              break;

    case 0510:    /* HLLZ  */
    case 0511:    /* HLLZI */
    case 0512:    /* HLLZM */
    case 0513:    /* HLLZS */
    case 0514:    /* HRLZ  */
    case 0515:    /* HRLZI */
    case 0516:    /* HRLZM */
    case 0517:    /* HRLZS */
              AR = (AR & LMASK);
              break;

    case 0520:    /* HLLO  */
    case 0521:    /* HLLOI */
    case 0522:    /* HLLOM */
    case 0523:    /* HLLOS */
    case 0524:    /* HRLO  */
    case 0525:    /* HRLOI */
    case 0526:    /* HRLOM */
    case 0527:    /* HRLOS */
              AR = (AR & LMASK) | RMASK;
              break;

    case 0530:    /* HLLE  */
    case 0531:    /* HLLEI */
    case 0532:    /* HLLEM */
    case 0533:    /* HLLES */
    case 0534:    /* HRLE  */
    case 0535:    /* HRLEI */
    case 0536:    /* HRLEM */
    case 0537:    /* HRLES */
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              break;

    case 0507:    /* HRLS */
              BR = SWAP_AR;
              /* Fall Through */

    case 0540:    /* HRR  */
    case 0541:    /* HRRI */
    case 0542:    /* HRRM */
    case 0544:    /* HLR  */
    case 0545:    /* HLRI */
    case 0546:    /* HLRM */
              AR = (BR & LMASK) | (AR & RMASK);
              break;

    case 0550:    /* HRRZ  */
    case 0551:    /* HRRZI */
    case 0552:    /* HRRZM */
    case 0553:    /* HRRZS */
    case 0554:    /* HLRZ  */
    case 0555:    /* HLRZI */
    case 0556:    /* HLRZM */
    case 0557:    /* HLRZS */
              AR = (AR & RMASK);
              break;

    case 0560:    /* HRRO  */
    case 0561:    /* HRROI */
    case 0562:    /* HRROM */
    case 0563:    /* HRROS */
    case 0564:    /* HLRO  */
    case 0565:    /* HLROI */
    case 0566:    /* HLROM */
    case 0567:    /* HLROS */
              AR = LMASK | (AR & RMASK);
              break;

    case 0570:    /* HRRE  */
    case 0571:    /* HRREI */
    case 0572:    /* HRREM */
    case 0573:    /* HRRES */
    case 0574:    /* HLRE  */
    case 0575:    /* HLREI */
    case 0576:    /* HLREM */
    case 0577:    /* HLRES */
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              break;

    case 0600:     /* TRN  */
    case 0601:     /* TLN  */
    case 0602:     /* TRNE */
    case 0603:     /* TLNE */
    case 0604:     /* TRNA */
    case 0605:     /* TLNA */
    case 0606:     /* TRNN */
    case 0607:     /* TLNN */
    case 0610:     /* TDN  */
    case 0611:     /* TSN  */
    case 0612:     /* TDNE */
    case 0613:     /* TSNE */
    case 0614:     /* TDNA */
    case 0615:     /* TSNA */
    case 0616:     /* TDNN */
    case 0617:     /* TSNN */
              MQ = AR;            /* N */
              goto test_op;

    case 0620:     /* TRZ  */
    case 0621:     /* TLZ  */
    case 0622:     /* TRZE */
    case 0623:     /* TLZE */
    case 0624:     /* TRZA */
    case 0625:     /* TLZA */
    case 0626:     /* TRZN */
    case 0627:     /* TLZN */
    case 0630:     /* TDZ  */
    case 0631:     /* TSZ  */
    case 0632:     /* TDZE */
    case 0633:     /* TSZE */
    case 0634:     /* TDZA */
    case 0635:     /* TSZA */
    case 0636:     /* TDZN */
    case 0637:     /* TSZN */
              MQ = CM(AR) & BR;   /* Z */
              goto test_op;

    case 0640:     /* TRC  */
    case 0641:     /* TLC  */
    case 0642:     /* TRCE */
    case 0643:     /* TLCE */
    case 0644:     /* TRCA */
    case 0645:     /* TLCA */
    case 0646:     /* TRCN */
    case 0647:     /* TLCN */
    case 0650:     /* TDC  */
    case 0651:     /* TSC  */
    case 0652:     /* TDCE */
    case 0653:     /* TSCE */
    case 0654:     /* TDCA */
    case 0655:     /* TSCA */
    case 0656:     /* TDCN */
    case 0657:     /* TSCN */
              MQ = AR ^ BR;       /* C */
              goto test_op;

    case 0660:     /* TRO  */
    case 0661:     /* TLO  */
    case 0662:     /* TROE */
    case 0663:     /* TLOE */
    case 0664:     /* TROA */
    case 0665:     /* TLOA */
    case 0666:     /* TRON */
    case 0667:     /* TLON */
    case 0670:     /* TDO  */
    case 0671:     /* TSO  */
    case 0672:     /* TDOE */
    case 0673:     /* TSOE */
    case 0674:     /* TDOA */
    case 0675:     /* TSOA */
    case 0676:     /* TDON */
    case 0677:     /* TSON */
              MQ = AR | BR;       /* O */
test_op:
              AR &= BR;
              f = ((AR == 0) & ((IR >> 1) & 1)) ^ ((IR >> 2) & 1);
              if (f) {
                  PC_CHANGE
                  PC = (PC + 1) & RMASK;
              }
              AR = MQ;
              break;

            /* IOT */
    case 0700: case 0701: case 0702: case 0703:
    case 0704: case 0705: case 0706: case 0707:
    case 0710: case 0711: case 0712: case 0713:
    case 0714: case 0715: case 0716: case 0717:
    case 0720: case 0721: case 0722: case 0723:
    case 0724: case 0725: case 0726: case 0727:
    case 0730: case 0731: case 0732: case 0733:
    case 0734: case 0735: case 0736: case 0737:
    case 0740: case 0741: case 0742: case 0743:
    case 0744: case 0745: case 0746: case 0747:
    case 0750: case 0751: case 0752: case 0753:
    case 0754: case 0755: case 0756: case 0757:
    case 0760: case 0761: case 0762: case 0763:
    case 0764: case 0765: case 0766: case 0767:
    case 0770: case 0771: case 0772: case 0773:
    case 0774: case 0775: case 0776: case 0777:
#if KI
              if (!pi_cycle && ((((FLAGS & (USER|USERIO)) == USER) && (IR & 040) == 0)
                    || ((FLAGS & (USER|PUBLIC)) == PUBLIC))) {

#elif PDP6
              if ((FLAGS & USER) != 0 && user_io == 0 && !pi_cycle) {
#else
              if ((FLAGS & (USER|USERIO)) == USER && !pi_cycle) {
#endif
                  /* User and not User I/O */
                  goto muuo;
              } else {
                  int d = ((IR & 077) << 1) | ((AC & 010) != 0);
fetch_opr:
                  switch(AC & 07) {
                  case 0:     /* 00 BLKI */
                  case 2:     /* 10 BLKO */
                          if (Mem_read(pi_cycle, 0, 0))
                              goto last;
                          AR = MB;
                          if (hst_lnt) {
                                  hst[hst_p].mb = AR;
                          }
                          AC |= 1;    /* Make into DATAI/DATAO */
                          AR = AOB(AR);
                          if (AR & C1) {
                              pi_ov = 1;
                          }
                          else if (!pi_cycle)
                              PC = (PC + 1) & RMASK;
                          AR &= FMASK;
                          MB = AR;
                          if (Mem_write(pi_cycle, 0))
                              goto last;
                          AB = AR & RMASK;
                          goto fetch_opr;

                  case 1:     /* 04 DATAI */
                          dev_tab[d](DATAI|(d<<2), &AR);
                          MB = AR;
                          if (Mem_write(pi_cycle, 0))
                              goto last;
                          break;
                  case 3:     /* 14 DATAO */
                          if (Mem_read(pi_cycle, 0, 0))
                             goto last;
                          AR = MB;
                          dev_tab[d](DATAO|(d<<2), &AR);
                          break;
                  case 4:     /* 20 CONO */
                          dev_tab[d](CONO|(d<<2), &AR);
                          break;
                  case 5:     /* 24 CONI */
                          dev_tab[d](CONI|(d<<2), &AR);
                          MB = AR;
                          if (Mem_write(pi_cycle, 0))
                              goto last;
                          break;
                  case 6:     /* 30 CONSZ */
                          dev_tab[d](CONI|(d<<2), &AR);
                          AR &= AB;
                          if (AR == 0)
                              PC = (PC + 1) & RMASK;
                          break;
                  case 7:     /* 34 CONSO */
                          dev_tab[d](CONI|(d<<2), &AR);
                          AR &= AB;
                          if (AR != 0)
                              PC = (PC + 1) & RMASK;
                          break;
                  }
              }
              break;
    }

    AR &= FMASK;
    if (!sac_inh && (i_flags & (SCE|FCEPSE))) {
        MB = AR;
        if (Mem_write(0, 0)) {
           goto last;
        }
    }
    if (!sac_inh && ((i_flags & SAC) || ((i_flags & SACZ) && AC != 0)))
        set_reg(AC, AR);    /* blank, I, B */

    if (!sac_inh && (i_flags & SAC2)) {
        MQ &= FMASK;
        set_reg(AC+1, MQ);
    }

    if (hst_lnt) {
        hst[hst_p].fmb = AR;
    }

last:
#if BBN
    if (QBBN && page_fault) {
        page_fault = 0;
        AB = 070 + maoff;
        f_pc_inh = 1;
        pi_cycle = 1;
        goto fetch;
    }
#endif
#if KI | KL
    /* Handle page fault and traps */
    if (page_enable && page_fault) {
        page_fault = 0;
        AB = ub_ptr + ((FLAGS & USER) ? 0427 : 0426);
        MB = fault_data;
        Mem_write_nopage();
        FLAGS |= trap_flag & (TRP1|TRP2);
        trap_flag = 1;
        AB = ((FLAGS & USER) ? ub_ptr : eb_ptr) | 0420;
        f_pc_inh = 1;
        pi_cycle = 1;
        Mem_read_nopage();
        goto no_fetch;
    }
#endif

#if KI | KL
    if (!f_pc_inh && (trap_flag == 0)  && !pi_cycle) {
#else
    if (!f_pc_inh && !pi_cycle) {
#endif
        PC = (PC + 1) & RMASK;
    }

#if ITS
    if (QITS && one_p_arm && (FLAGS & BYTI) == 0) {
        fault_data |= 02000;
        mem_prot = 1;
        one_p_arm = 0;
    }
#endif

    /* Dismiss an interrupt */
    if (pi_cycle) {
#if KI | KL
       if (page_enable && page_fault) {
            page_fault = 0;
            inout_fail = 1;
       }
#endif

       if ((IR & 0700) == 0700 && ((AC & 04) == 0)) {
           pi_hold = pi_ov;
           if ((!pi_hold) & f_inst_fetch) {
                pi_cycle = 0;
           } else {
                AB = 040 | (pi_enc << 1) | pi_ov | maoff;
#if KI | KL
                AB |= eb_ptr;
                Mem_read_nopage();
#else
                Mem_read(1, 0, 1);
#endif
                goto no_fetch;
           }
       } else if (pi_hold && !f_pc_inh) {
            if ((IR & 0700) == 0700) {
                (void)check_irq_level();
            }
            AB = 040 | (pi_enc << 1) | pi_ov | maoff;
            pi_ov = 0;
            pi_hold = 0;
#if KI | KL
            AB |= eb_ptr;
            Mem_read_nopage();
#else
            Mem_read(1, 0, 1);
#endif
            goto no_fetch;
       } else {
#if KI | KL
            if (f_pc_inh && trap_flag == 0)
                set_pi_hold(); /* Hold off all lower interrupts */
#else
            if (!QITS || f_pc_inh)
                set_pi_hold(); /* Hold off all lower interrupts */
#endif
#if PDP6
            if ((IR & 0700) == 0700)
                pi_cycle = 1;
            else
#endif
            pi_cycle = 0;
            f_inst_fetch = 1;
            f_load_pc = 1;
       }
    }

    if (pi_restore) {
        restore_pi_hold();
        pi_restore = 0;
    }
    sim_interval--;
    if (!pi_cycle && instr_count != 0 && --instr_count == 0) {
#if ITS
        if (QITS)
            load_quantum();
#endif
        return SCPE_STEP;
    }
}
/* Should never get here */
#if ITS
if (QITS)
    load_quantum();
#endif

return reason;
}

t_stat
rtc_srv(UNIT * uptr)
{
    int32 t;
    t = sim_rtcn_calb (rtc_tps, TMR_RTC);
    sim_activate_after(uptr, 1000000/rtc_tps);
    tmxr_poll = t/2;
    clk_flg = 1;
    if (clk_en) {
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO timmer\n");
        set_interrupt(4, clk_irq);
    }
    return SCPE_OK;
}

#if ITS
t_stat
qua_srv(UNIT * uptr)
{
    if ((fault_data & 1) == 0 && pi_enable && !pi_pending && (FLAGS & USER) != 0) {
       mem_prot = 1;
       fault_data |= 1;
    }
    qua_time = RSIGN;
    return SCPE_OK;
}
#endif


/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int     i;
BYF5 = uuo_cycle = 0;
#if KA | PDP6
Pl = Ph = 01777;
Rl = Rh = Pflag = 0;
push_ovf = mem_prot = 0;
#if PDP6
user_io = 0;
#endif
#if ITS | BBN
page_enable = 0;
#endif
#endif
nxm_flag = clk_flg = 0;
PIR = PIH = PIE = pi_enable = parity_irq = 0;
pi_pending = pi_enc = apr_irq = 0;
ov_irq =fov_irq =clk_en =clk_irq = 0;
pi_restore = pi_hold = 0;
#if KI | KL
ub_ptr = eb_ptr = 0;
pag_reload = ac_stack = 0;
fm_sel = small_user = user_addr_cmp = page_enable = 0;
#endif
#if BBN
exec_map = 0;
#endif

for(i=0; i < 128; dev_irq[i++] = 0);
sim_brk_types = SWMASK('E') | SWMASK('W') | SWMASK('R');
sim_brk_dflt = SWMASK ('E');
sim_rtcn_init_unit (&cpu_unit[0], cpu_unit[0].wait, TMR_RTC);
sim_activate(&cpu_unit[0], 10000);
#if MPX_DEV
mpx_enable = 0;
#endif
#ifdef PANDA_LIGHTS
ka10_lights_init ();
#endif
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw)
{
if (vptr == NULL)
    return SCPE_ARG;
if (ea < 020)
    *vptr = FM[ea] & FMASK;
else {
    if (sw & SWMASK ('V')) {
        if (ea >= MAXMEMSIZE)
            return SCPE_REL;
        }
    if (ea >= MEMSIZE)
        return SCPE_NXM;
    *vptr = M[ea] & FMASK;
    }
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw)
{
if (ea < 020)
    FM[ea] = val & FMASK;
else {
    if (sw & SWMASK ('V')) {
        if (ea >= MAXMEMSIZE)
            return SCPE_REL;
        }
    if (ea >= MEMSIZE)
        return SCPE_NXM;
    M[ea] = val & FMASK;
    }
return SCPE_OK;
}

/* Memory size change */

t_stat cpu_set_size (UNIT *uptr, int32 sval, CONST char *cptr, void *desc)
{
int32 i;
int32 val = (int32)sval;

if ((val <= 0) || ((val * 16 * 1024) > MAXMEMSIZE))
    return SCPE_ARG;
val = val * 16 * 1024;
if (val < (int32)MEMSIZE) {
    uint64 mc = 0;
    for (i = val-1; i < (int32)MEMSIZE; i++)
        mc = mc | M[i];
    if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
        return SCPE_OK;
}
for (i = (int32)MEMSIZE; i < val; i++)
    M[i] = 0;
cpu_unit[0].capac = (uint32)val;
return SCPE_OK;
}

/* Build device dispatch table */
t_bool build_dev_tab (void)
{
DEVICE *dptr;
DIB    *dibp;
uint32 i, j, d;

/* Set trap offset based on MAOFF flag */
maoff = (cpu_unit[0].flags & UNIT_MAOFF)? 0100 : 0;

#if KA
/* Set up memory access routines based on current CPU type. */

/* Default to KA */
Mem_read = &Mem_read_ka;
Mem_write = &Mem_write_ka;
#if ITS
if (QITS) {
    Mem_read = &Mem_read_its;
    Mem_write = &Mem_write_its;
}
#endif
#if BBN
if (QBBN) {
    Mem_read = &Mem_read_bbn;
    Mem_write = &Mem_write_bbn;
}
#endif
#if WAITS  /* Waits without BBN pager */
if (QWAITS && !QBBN) {
    Mem_read = &Mem_read_waits;
    Mem_write = &Mem_write_waits;
}
#endif
#endif

/* Clear device and interrupt table */
for (i = 0; i < 128; i++) {
    dev_tab[i] = &null_dev;
    dev_irqv[i] = NULL;
}

/* Set up basic devices. */
dev_tab[0] = &dev_apr;
dev_tab[1] = &dev_pi;
#if KI | KL
dev_tab[2] = &dev_pag;
#endif
#if BBN
if (QBBN)
   dev_tab[024>>2] = &dev_pag;
#endif

/* Assign all RH10 devices */
for (j = i = 0; (dptr = rh_devs[i]) != NULL; i++) {
    dibp = (DIB *) dptr->ctxt;
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
        if (rh[j].dev_num == 0)
           break;
        d = rh[j].dev_num;
        dev_tab[(d >> 2)] = dibp->io;
        dev_irqv[(d >> 2)] = dibp->irq;
        rh[j].dev = dptr;
        j++;
    }
}

/* Make sure all are assigned */
if (j == 4 && rh_devs[i] != NULL) {
    sim_printf ("To many RH10 devices %s\n", sim_dname (dptr));
    return TRUE;
}

/* Assign all remaining devices */
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    dibp = (DIB *) dptr->ctxt;
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
         for (j = 0; j < dibp->num_devs; j++) {         /* loop thru disp */
              if (dibp->io) {                           /* any dispatch? */
                   d = dibp->dev_num;
                   if (d & RH10_DEV)                    /* Skip RH10 devices */
                       continue;
                   if (dev_tab[(d >> 2) + j] != &null_dev) {
                                                        /* already filled? */
                       sim_printf ("%s device number conflict at %02o\n",
                              sim_dname (dptr), d + (j << 2));
                       return TRUE;
                   }
                   dev_tab[(d >> 2) + j] = dibp->io;    /* fill */
                   dev_irqv[(d >> 2) + j] = dibp->irq;
              }                                         /* end if dsp */
           }                                            /* end for j */
        }                                               /* end if enb */
    }                                                   /* end for i */
    return FALSE;
}

#if KI | KL

/* Set serial */
t_stat cpu_set_serial (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 lnt;
t_stat r;

if (cptr == NULL) {
    apr_serial = -1;
    return SCPE_OK;
    }
lnt = (int32) get_uint (cptr, 10, 001777, &r);
if ((r != SCPE_OK) || (lnt <= 0))
    return SCPE_ARG;
apr_serial = lnt & 01777;
return SCPE_OK;
}

/* Show serial */
t_stat cpu_show_serial (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Serial: " );
if (apr_serial == -1) {
    fprintf (st, "%d (default)", DEF_SERIAL);
    return SCPE_OK;
    }
fprintf (st, "%d", apr_serial);
return SCPE_OK;
}
#endif

/* Set history */
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].pc = 0;
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
int32 k, di, lnt;
char *cptr = (char *) desc;
t_stat r;
t_value sim_eval;
InstHistory *h;

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
fprintf (st, "PC      AC            EA        AR            RES           FLAGS IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        fprintf (st, "%06o  ", h->pc & 0777777);
        fprint_val (st, h->ac, 8, 36, PV_RZRO);
        fputs ("  ", st);
        fprintf (st, "%06o  ", h->ea);
        fputs ("  ", st);
        fprint_val (st, h->mb, 8, 36, PV_RZRO);
        fputs ("  ", st);
        fprint_val (st, h->fmb, 8, 36, PV_RZRO);
        fputs ("  ", st);
#if KI | KL
        fprintf (st, "%c%06o  ", ((h->flags & (PRV_PUB << 5))? 'p':' '), h->flags & 0777777);
#else
        fprintf (st, "%06o  ", h->flags);
#endif
        if ((h->pc & HIST_PC2) == 0) {
            sim_eval = h->ir;
            fprint_val (st, sim_eval, 8, 36, PV_RZRO);
            fputs ("  ", st);
            if ((fprint_sym (st, h->pc & RMASK, &sim_eval, &cpu_unit[0], SWMASK ('M'))) > 0) {
                fputs ("(undefined) ", st);
                fprint_val (st, h->ir, 8, 36, PV_RZRO);
            }
        }
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}

t_stat
cpu_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "%s\n\n", cpu_description(dptr));
    fprintf(st, "To stop the cpu use the command:\n\n");
    fprintf(st, "    sim> SET CTY STOP\n\n");
    fprintf(st, "This will write a 1 to location %03o, causing TOPS10 to stop\n", CTY_SWITCH);
    fprint_set_help(st, dptr);
    fprint_show_help(st, dptr);
    return SCPE_OK;
}

const char *
cpu_description (DEVICE *dptr)
{
#if KL
    return "KL10A CPU";
#endif
#if KI
    return "KI10 CPU";
#endif
#if KA
    return "KA10 CPU";
#endif
#if PDP6
    return "PDP6 CPU";
#endif
}
