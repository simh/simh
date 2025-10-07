/* kx10_cpu.c: PDP-10 CPU simulator

   Copyright (c) 2011-2021, Richard Cornwell

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

   cpu          KA10/KI10/KL10/KS10 central processor


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

#define HIST_PC         0x40000000
#define HIST_PC2        0x80000000
#define HIST_PCE        0x20000000
#define HIST_MIN        64
#define HIST_MAX        5000000
#define TMR_RTC         0
#define TMR_QUA         1


uint64  M[MAXMEMSIZE];                        /* Memory */
#if KL | KS
uint64  FM[128];                              /* Fast memory register */
#elif KI
uint64  FM[64];                               /* Fast memory register */
#else
uint64  FM[16];                               /* Fast memory register */
#endif
uint64  AR;                                   /* Primary work register */
uint64  MQ;                                   /* Extension to AR */
uint64  BR;                                   /* Secondary operand */
uint64  AD;                                   /* Address Data */
uint64  MB;                                   /* Memory Bufer Register */
t_addr  AB;                                   /* Memory address buffer */
t_addr  PC;                                   /* Program counter */
uint32  IR;                                   /* Instruction register */
uint64  MI;                                   /* Monitor lights */
uint8   MI_flag;                              /* Monitor flags */
uint8   MI_disable;                           /* Monitor flag disable */
uint32  FLAGS;                                /* Flags */
uint32  AC;                                   /* Operand accumulator */
uint64  SW;                                   /* Switch register */
uint8   RUN;                                  /* Run flag */
uint8   prog_stop;                            /* Programmed stop */
#if PIDP10
uint8   sing_inst_sw;                         /* Execute single inst */
uint8   examine_sw;                           /* Examine memory */
uint8   deposit_sw;                           /* Deposit memory */
uint8   xct_sw;                               /* Execute SW */
uint8   stop_sw;                              /* Stop simulation */
uint32  rdrin_dev;                            /* Read in device */
uint8   IX;                                   /* Index register */
uint8   IND;                                  /* Indirect flag */
#endif
t_addr  AS;                                   /* Address switches */
int     BYF5;                                 /* Flag for second half of LDB/DPB instruction */
int     uuo_cycle;                            /* Uuo cycle in progress */
int     SC;                                   /* Shift count */
int     SCAD;                                 /* Shift count extension */
int     FE;                                   /* Exponent */
t_addr  last_addr;                            /* Last addressed accessed */
#if KA | PDP6
t_addr  Pl, Ph, Rl, Rh, Pflag;                /* Protection registers */
int     push_ovf;                             /* Push stack overflow */
int     mem_prot;                             /* Memory protection flag */
#endif
int     nxm_flag;                             /* Non-existant memory flag */
#if KA | KI
int     nxm_stop;                             /* Non-existant memory stop flag */
int     adr_flag;                             /* Address break flag */
int     adr_cond;                             /* Address condition swiches */
#endif
int     clk_flg;                              /* Clock flag */
int     ov_irq;                               /* Trap overflow */
int     fov_irq;                              /* Trap floating overflow */
#if PDP6
int     pcchg_irq;                            /* PC Change flag */
int     ill_op;                               /* Illegal opcode */
int     user_io;                              /* User IO flag */
int     ex_uuo_sync;                          /* Execute a UUO op */
#endif
uint16  IOB_PI;                               /* Input bus PI signals */
uint8   PIR;                                  /* Current priority level */
uint8   PIH;                                  /* Highest priority */
uint8   PIE;                                  /* Priority enable mask */
int     pi_cycle;                             /* Executing an interrupt */
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
int     pi_vect;                              /* Last pi location used for IRQ */
#if KI | KL | KS
uint64  ARX;                                  /* Extension to AR */
uint64  BRX;                                  /* Extension to BR */
uint64  ADX;                                  /* Extension to AD */
t_addr  ub_ptr;                               /* User base pointer */
t_addr  eb_ptr;                               /* Executive base pointer */
uint8   fm_sel;                               /* User fast memory block */
int32   apr_serial = -1;                      /* CPU Serial number */
int     inout_fail;                           /* In out fail flag */
#if KS
int     ext_ac;                               /* Extended instruction AC */
uint8   prev_ctx;                             /* Previous AC context */
uint16  irq_enable;                           /* Apr IRQ enable bits */
uint16  irq_flags;                            /* Apr IRQ bits */
uint64  tim_low;                              /* Low order timer word */
uint64  tim_high;                             /* High order timer word */
uint64  int_val;                              /* Interval timer */
uint64  int_cur;                              /* Current interval */
int     t20_page;                             /* Tops 20 paging selected */
int     ptr_flg;                              /* Access to pointer value */
int     extend = 0;                           /* Process extended instruction */
int     fe_xct = 0;                           /* Execute instruction at address */
#if KS_ITS
uint64  qua_time;                             /* Quantum clock value */
uint8   pi_act;                               /* Current active PI level */
#endif
#elif KL
int     ext_ac;                               /* Extended instruction AC */
uint8   prev_ctx;                             /* Previous AC context */
uint16  irq_enable;                           /* Apr IRQ enable bits */
uint16  irq_flags;                            /* Apr IRQ bits */
int     mtr_irq;                              /* Timer IRQ */
int     mtr_enable;                           /* Enable Timer */
int     mtr_flags;                            /* Flags for accounting */
int     tim_per;                              /* Timer period */
int     tim_val;                              /* Current timer value */
int     rtc_tim;                              /* Time till next 60hz clock */
uint32  brk_addr;                             /* Address break */
int     brk_flags;                            /* Break flags */
int     t20_page;                             /* Tops 20 paging selected */
int     ptr_flg;                              /* Access to pointer value */
int     extend = 0;                           /* Process extended instruction */
int     sect;                                 /* Actual resolved section */
int     cur_sect;                             /* Current section */
int     prev_sect;                            /* Previous section */
int     pc_sect;                              /* Program counter section */
int     glb_sect;                             /* Global section access */
#elif KI
int     small_user;                           /* Small user flag */
#endif
int     user_addr_cmp;                        /* User address compare flag */
#endif
#if KI | KL | ITS | BBN | KS
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
#if MAGIC_SWITCH
int     MAGIC = 1;                            /* Magic switch. */
#endif /* MAGIC_SWITCH */
#endif /* ITS */
#if KL_ITS
#define dbr1    FM[(6<<4)|1]
#define dbr2    FM[(6<<4)|2]
#define dbr3    FM[(6<<4)|3]
#define dbr4    FM[(6<<4)|4]
#define jpc     FM[(6<<4)|15]
#define mar     brk_addr;
#endif

#if KL
#define spt       FM[(06<<4)|3]
#define cst       FM[(06<<4)|2]
#define cst_msk   FM[(06<<4)|0]
#define cst_dat   FM[(06<<4)|1]
#endif

#if KS
uint64 spt;
uint64 cst;
uint64 cst_msk;
uint64 cst_dat;
uint64 hsb;
#endif

#if KS_ITS
#define dbr1    spt
#define dbr2    cst
#define dbr3    cst_dat
#define dbr4    cst_msk
uint64 pcst;
#endif

int     watch_stop;                           /* Stop at memory watch point */
int     maoff = 0;                            /* Offset for traps */

uint16  dev_irq[128];                         /* Pending irq by device */
t_stat  (*dev_tab[128])(uint32 dev, uint64 *data);
t_addr  (*dev_irqv[128])(uint32 dev, t_addr addr);
t_stat  cpu_detach(UNIT *uptr);
t_stat  rtc_srv(UNIT * uptr);
#if KS
int32   rtc_tps = 500;
#else
int32   rtc_tps = 60;
#endif
#if ITS
t_stat  qua_srv(UNIT * uptr);
int32   qua_tps = 125000;
#endif
#if KL
t_stat  tim_srv(UNIT * uptr);
#endif
int32   tmxr_poll = 10000;

/* Physical address range for Rubin 10-11 interface. */
#define T11RANGE(addr)  ((addr) >= ten11_base && (addr) < ten11_end)
/* Physical address range for auxiliary PDP-6. */
#define AUXCPURANGE(addr)  ((addr) >= auxcpu_base && (addr) < (auxcpu_base + 040000))

#if (NUM_DEVS_RP + NUM_DEVS_RS + NUM_DEVS_TU) > 0
#if KA | KI | KL
/* List of RH10 & RH20 devices */
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
#if (NUM_DEVS_NIA > 0)
    &nia_dev,
#endif
    NULL,
};
/* RH10 device numbers */
int rh_nums[] = { 0270, 0274, 0360, 0364, 0370, 0374, 0};
/* Maps RH10 & RH20 device number to DEVICE structure */
struct rh_dev rh[8];
#endif
#endif

typedef struct {
    uint32      pc;
    uint32      ea;
    uint64      ir;
    uint64      ac;
    uint32      flags;
    uint64      mb;
    uint64      fmb;
    uint16      prev_sect;
    } InstHistory;

int32 hst_p = 0;                         /* history pointer */
int32 hst_lnt = 0;                       /* history length */
InstHistory *hst = NULL;                 /* instruction history */

/* Forward and external declarations */

#if KL | KS
int    do_extend(uint32 IA);
#endif
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#if KI | KL | KS
t_stat cpu_set_serial (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_serial (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
#endif
t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
                     const char *cptr);
const char          *cpu_description (DEVICE *dptr);
void set_ac_display (uint64 *acbase);
#if KA
int (*Mem_read)(int flag, int cur_context, int fetch, int mod);
int (*Mem_write)(int flag, int cur_context);
#else
int Mem_read(int flag, int cur_context, int fetch, int mod);
int Mem_write(int flag, int cur_context);
#endif

t_bool build_dev_tab (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

#if KL
#define DEFMEM 4096
#elif KS
#define DEFMEM 512
#else
#define DEFMEM 256
#endif

#if KI_22BIT
#define DF_FLAG UNIT_DF10C
#else
#define DF_FLAG 0
#endif

UNIT cpu_unit[] = { { UDATA (&rtc_srv,
            UNIT_IDLE|UNIT_FIX|UNIT_BINK|UNIT_TWOSEG|DF_FLAG, DEFMEM * 1024) },
#if ITS
                    { UDATA (&qua_srv, UNIT_IDLE|UNIT_DIS, 0) }
#endif
#if KL
                    { UDATA (&tim_srv, UNIT_IDLE|UNIT_DIS, 0) }
#endif
                   };

REG cpu_reg[] = {
    { ORDATAD (PC, PC, 18, "Program Counter") },
    { ORDATAD (FLAGS, FLAGS, 18, "Flags") },
    { ORDATAD (FM0, FM[00], 36, "Fast Memory"), REG_VMIO },       /* addr in memory */
    { ORDATA (FM1, FM[01], 36), REG_VMIO },                       /* modified at exit */
    { ORDATA (FM2, FM[02], 36), REG_VMIO },                       /* to SCP */
    { ORDATA (FM3, FM[03], 36), REG_VMIO },
    { ORDATA (FM4, FM[04], 36), REG_VMIO },
    { ORDATA (FM5, FM[05], 36), REG_VMIO },
    { ORDATA (FM6, FM[06], 36), REG_VMIO },
    { ORDATA (FM7, FM[07], 36), REG_VMIO },
    { ORDATA (FM10, FM[010], 36), REG_VMIO },
    { ORDATA (FM11, FM[011], 36), REG_VMIO },
    { ORDATA (FM12, FM[012], 36), REG_VMIO },
    { ORDATA (FM13, FM[013], 36), REG_VMIO },
    { ORDATA (FM14, FM[014], 36), REG_VMIO },
    { ORDATA (FM15, FM[015], 36), REG_VMIO },
    { ORDATA (FM16, FM[016], 36), REG_VMIO },
    { ORDATA (FM17, FM[017], 36), REG_VMIO },
#if KL | KS
    { BRDATA (FM, FM, 8, 36, 128), REG_VMIO},
#elif KI
    { BRDATA (FM, FM, 8, 36, 64), REG_VMIO},
#else
    { BRDATA (FM, FM, 8, 36, 16), REG_VMIO},
#endif
    { ORDATAD (PIR, PIR, 8, "Priority Interrupt Request") },
    { ORDATAD (PIH, PIH, 8, "Priority Interrupt Hold") },
    { ORDATAD (PIE, PIE, 8, "Priority Interrupt Enable") },
    { ORDATAD (PIENB, pi_enable, 7, "Enable Priority System") },
    { ORDATAD (SW, SW, 36, "Console SW Register"), REG_FIT},
    { ORDATAD (MI, MI, 36, "Memory Indicators"), REG_FIT},
    { FLDATAD (MIFLAG, MI_flag, 0, "Memory indicator flag") },
    { FLDATAD (MIDISABLE, MI_disable, 0, "Memory indicator disable") },
#if PDP6 | KA | KI
    { ORDATAD (AS, AS, 18, "Console AS Register"), REG_FIT},
#endif
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
#if KA | KI
    { FLDATAD (NXMSTOP, nxm_stop, 0, "Stop on non-existing memory") },
    { FLDATAD (ABRK, adr_flag, 0, "Address break") },
    { ORDATAD (ACOND, adr_cond, 5, "Address condition switches") },
#endif
    { FLDATAD (CLK, clk_flg, 0, "Clock interrupt") },
    { FLDATAD (OV, ov_irq, 0, "Overflow enable") },
#if PDP6
    { FLDATAD (PCCHG, pcchg_irq, 0, "PC Change interrupt") },
    { FLDATAD (USERIO, user_io, 0, "User I/O") },
    { FLDATAD (UUOSYNC, ex_uuo_sync, 0, "UUO Op") },
#else
    { FLDATAD (FOV, fov_irq, 0, "Floating overflow enable") },
#endif
    { FLDATA (PIPEND, pi_pending, 0), REG_HRO},
    { FLDATA (PARITY, parity_irq, 0) },
    { ORDATAD (APRIRQ, apr_irq, 3, "APR Interrupt number") },
    { ORDATAD (CLKIRQ, clk_irq, 3, "CLK Interrupt number") },
    { FLDATA (CLKEN, clk_en, 0), REG_HRO},
    { FLDATA (XCT, xct_flag, 0), REG_HRO},
    { BRDATA (IRQV, dev_irq, 8, 16, 128 ), REG_HRO},
    { ORDATA (PIEN, pi_enc, 8), REG_HRO},
    { FLDATA (PIHOLD, pi_hold, 0), REG_HRO},
    { FLDATA (PIREST, pi_restore, 0), REG_HRO},
    { FLDATA (PICYC, pi_cycle, 0), REG_HRO},
#if MPX_DEV
    { FLDATA (MPX, mpx_enable, 0), REG_HRO},
#endif
#if KI
    { ORDATAD (UB, ub_ptr, 18, "User Base Pointer") },
    { ORDATAD (EB, eb_ptr, 18, "Executive Base Pointer") },
#endif
#if KL | KS
    { ORDATAD (UB, ub_ptr, 22, "User Base Pointer") },
    { ORDATAD (EB, eb_ptr, 22, "Executive Base Pointer") },
#endif
#if KI | KL | KS
    { ORDATAD (FMSEL, fm_sel, 8, "Register set select") },
    { ORDATAD (SERIAL, apr_serial, 10, "System Serial Number") },
    { FLDATA (INOUT, inout_fail, 0), REG_RO},
#if KI
    { FLDATA (SMALL, small_user, 0), REG_RO},
#endif
    { FLDATA (ADRCMP, user_addr_cmp, 0), REG_HRO},
#endif
#if KL | KI | ITS | BBN | KS
    { FLDATAD (PAGE_ENABLE, page_enable, 0, "Paging enabled")},
    { FLDATAD (PAGE_FAULT, page_fault, 0, "Page fault"), REG_RO},
#if KI | ITS | BBN
    { ORDATAD (AC_STACK, ac_stack, 18, "AC Stack"), REG_RO},
#endif
    { ORDATAD (PAGE_RELOAD, pag_reload, 18, "Page reload"), REG_HRO},
    { ORDATAD (FAULT_DATA, fault_data, 36, "Page fault data"), REG_RO},
    { FLDATAD (TRP_FLG, trap_flag, 0, "Trap flag"), REG_HRO},
#if !(KL | KS)
    { ORDATAD (LST_PAGE, last_page, 9, "Last page"), REG_HRO},
#endif
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
    { ORDATAD (QUA_TIME, qua_time, 32, "Quantum timer"), REG_RO},
#if MAGIC_SWITCH
    { ORDATAD (MAGIC, MAGIC, 1, "Magic switch"), REG_FIT},
#endif /* MAGIC_SWITCH */
#endif /* ITS */
#if KS
    { ORDATAD (EXT_AC, ext_ac, 4, "Extended Instruction AC"), REG_HRO},
    { ORDATAD (PREV_CTX, prev_ctx, 5, "Previous context"), REG_HRO},
    { ORDATAD (ITQ_EN, irq_enable, 16, "Interrupt enable"), REG_HRO},
    { ORDATAD (ITQ_FLGS, irq_flags, 16, "Interrupt Flags"), REG_HRO},
    { ORDATAD (T20_PAGE, t20_page, 1, "TOPS20 paging"), REG_HRO},
    { ORDATAD (PTR_FLG, ptr_flg, 1, "Accessing pointer"), REG_HRO},
    { ORDATAD (EXTEND, extend, 1, "Execute Extend"), REG_HRO},
    { ORDATAD (SPT, spt, 18, "Special Page table"),},
    { ORDATAD (CST, cst, 18, "Memory status table"),},
    { ORDATAD (PU, cst_dat, 36, "User data"),},
    { ORDATAD (CSTM, cst_msk, 36, "Status mask"),},
#endif
#if KL
    { ORDATAD (EXT_AC, ext_ac, 4, "Extended Instruction AC"), REG_HRO},
    { ORDATAD (PREV_CTX, prev_ctx, 5, "Previous context"), REG_HRO},
    { ORDATAD (ITQ_EN, irq_enable, 16, "Interrupt enable"), REG_HRO},
    { ORDATAD (ITQ_FLGS, irq_flags, 16, "Interrupt Flags"), REG_HRO},
    { ORDATAD (MTR_IRQ, mtr_irq, 1, "Timer IRQ"), REG_HRO},
    { ORDATAD (MTR_EN, mtr_enable, 1, "Timer Enable"), REG_HRO},
    { ORDATAD (MTR_FLGS, mtr_flags, 3, "Timer Flags"), REG_HRO},
    { ORDATAD (TIM_PER, tim_per, 12, "Timer period"), REG_HRO},
    { ORDATAD (TIM_VAl, tim_val, 12, "Timer period"), REG_HRO},
    { ORDATAD (RTC_TIM, rtc_tim, 12, "RTC timer"), REG_HRO},
    { ORDATAD (BRK_ADDR, brk_addr, 18, "Break address"), REG_HRO},
    { ORDATAD (BRK_FLGS, brk_flags, 18, "Break address"), REG_HRO},
    { ORDATAD (T20_PAGE, t20_page, 1, "TOPS20 paging"), REG_HRO},
    { ORDATAD (PTR_FLG, ptr_flg, 1, "Accessing pointer"), REG_HRO},
    { ORDATAD (EXTEND, extend, 1, "Execute Extend"), REG_HRO},
    { ORDATAD (SECT, sect, 12, "access section"), REG_HRO},
    { ORDATAD (CUR_SECT, cur_sect, 12, "Current section"), REG_HRO},
    { ORDATAD (PREV_SECT, prev_sect, 12, "Previous section"), REG_HRO},
    { ORDATAD (PC_SECT, pc_sect, 12, "PC section"), REG_HRO},
    { ORDATAD (GLB_SECT, glb_sect, 1, "Global section"), REG_HRO},
#endif
#if !PDP6
    { BRDATA (ETLB, e_tlb, 8, 32, 512), REG_HRO},
    { BRDATA (UTLB, u_tlb, 8, 32, 546), REG_HRO},
#endif
#if PIDP10
    { ORDATAD (READIN, rdrin_dev, 9, "Readin device")},
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
#if KI_22BIT|KI|ITS|KS
    { UNIT_MSIZE, 32, "512K", "512K", &cpu_set_size },
    { UNIT_MSIZE, 48, "768K", "768K", &cpu_set_size },
    { UNIT_MSIZE, 64, "1024K", "1024K", &cpu_set_size },
#endif
#if KI_22BIT|KI|KL
    { UNIT_MSIZE, 128, "2048K", "2048K", &cpu_set_size },
    { UNIT_MSIZE, 256, "4096K", "4096K", &cpu_set_size },
#endif
#if KI|KL|KS
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "SERIAL", "SERIAL",
          &cpu_set_serial, &cpu_show_serial, NULL, "CPU Serial Number" },
#if KL
    { UNIT_M_PAGE, 0, "KL10A", "KL10A", NULL, NULL, NULL,
              "Base KL10"},
    { UNIT_M_PAGE, UNIT_KL10B, "KL10B", "KL10B", NULL, NULL, NULL,
              "Extended addressing support for KL10"},
#endif
#endif
#if KA
    { UNIT_M_PAGE, 0, "ONESEG", "ONESEG", NULL, NULL, NULL,
             "One Relocation Register"},
    { UNIT_M_PAGE, UNIT_TWOSEG, "TWOSEG", "TWOSEG", NULL, NULL,
              NULL, "Two Relocation Registers"},
#endif
#if ITS | KL_ITS | KS_ITS
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
#if KI | KL
    { UNIT_M_DF10, 0, "DF10", "DF10", NULL, NULL, NULL,
              "18 bit DF10"},
    { UNIT_M_DF10, UNIT_DF10C, "DF10C", "DF10C", NULL, NULL, NULL,
              "22 bit DF10C"},
#endif
#if PDP6 | KA | KI
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
    {"CONO", DEBUG_CONO, "Show cono instructions"},
    {"DATAIO", DEBUG_DATAIO, "Show datai and datao instructions"},
#if KS
    {"DATA", DEBUG_DATA, "Show data transfers"},
    {"DETAIL", DEBUG_DETAIL, "Show details about device"},
    {"EXP", DEBUG_EXP, "Show exception information"},
#endif
    {0, 0}
};


DEVICE cpu_dev = {
    "CPU", &cpu_unit[0], cpu_reg, cpu_mod,
    1+ITS+KL, 8, 22, 1, 8, 36,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, &cpu_detach, NULL, DEV_DEBUG, 0, cpu_debug,
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

#if PDP6
#define PC_CHANGE       FLAGS |= PCHNG; check_apr_irq();
#else
#define PC_CHANGE
#endif
#define SWAP_AR         ((RMASK & AR) << 18) | ((AR >> 18) & RMASK)
#define SMEAR_SIGN(x)   x = ((x) & SMASK) ? (x) | EXPO : (x) & MANT
#define GET_EXPO(x)     ((((x) & SMASK) ? 0377 : 0 )  \
                                        ^ (((x) >> 27) & 0377))
#if KI | KL | KS
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
#if KL_ITS | KS_ITS
#define QITS            (cpu_unit[0].flags & UNIT_ITSPAGE)
#else
#define QITS            0
#endif
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
#if KL
#define QKLB            (cpu_unit[0].flags & UNIT_KL10B)
#else
#define QKLB            0
#endif
#if PDP6
#define QSLAVE          (slave_unit[0].flags & UNIT_ATT)
#else
#define QSLAVE          0
#endif
#if PIDP10
                        /* Update MI register if address matches */
#define UPDATE_MI(a)    if (!MI_flag && a == AS) { \
                             MI = MB; \
                        }
#else
#define UPDATE_MI(a)
#endif
#define MAX_DEV 128

#if KL
struct _byte {
    int p;
    int s;
} _byte_adj[] = {
    { /* 37 */  36, 6 }, /* 45 */
    { /* 38 */  30, 6 }, /* 46 */
    { /* 39 */  24, 6 }, /* 47 */
    { /* 40 */  18, 6 }, /* 50 */
    { /* 41 */  12, 6 }, /* 51 */
    { /* 42 */   6, 6 }, /* 52 */
    { /* 43 */   0, 6 }, /* 53 */

    { /* 44 */  36, 8 }, /* 54 */
    { /* 45 */  28, 8 }, /* 55 */
    { /* 46 */  20, 8 }, /* 56 */
    { /* 47 */  12, 8 }, /* 57 */
    { /* 48 */   4, 8 }, /* 60 */

    { /* 49 */  36, 7 }, /* 61 */
    { /* 50 */  29, 7 }, /* 62 */
    { /* 51 */  22, 7 }, /* 63 */
    { /* 52 */  15, 7 }, /* 64 */
    { /* 53 */   8, 7 }, /* 65 */
    { /* 54 */   1, 7 }, /* 66 */

    { /* 55 */  36, 9 }, /* 67 */
    { /* 56 */  27, 9 }, /* 70 */
    { /* 57 */  18, 9 }, /* 71 */
    { /* 58 */   9, 9 }, /* 72 */
    { /* 59 */   0, 9 }, /* 73 */

    { /* 60 */  36,18 }, /* 74 */
    { /* 61 */  18,18 }, /* 75 */
    { /* 62 */   0,18 }  /* 76 */
};
#endif

#if ITS
/*
 * Set quantum clock to qua_time.
 */

void
set_quantum()
{
    double us;
    sim_cancel(&cpu_unit[1]);
    if (qua_time & BIT17)
       return;
    us = (double)(BIT17 - qua_time);
    (void)sim_activate_after_d(&cpu_unit[1], us);
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
       if ((uint32)us > BIT17)
          qua_time = BIT17;
       else
          qua_time = (BIT17 - (uint32)us) & RMASK;
       sim_cancel(&cpu_unit[1]);
    }
}

/*
 * Get the current quantum time.
 */
uint32
get_quantum()
{
    uint32  t = qua_time;
    if (sim_is_active(&cpu_unit[1])) {
       double us;
       us = sim_activate_time_usecs (&cpu_unit[1]);
       t = (BIT17 - (uint32)us) & RMASK;
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
       IOB_PI |= 0200 >> lvl;
#if DEBUG
       sim_debug(DEBUG_IRQ, &cpu_dev, "set irq %o %o %03o %03o %03o\n",
              dev & 0774, lvl, PIE, PIR, PIH);
#endif
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
       IOB_PI |= 0200 >> lvl;
#if DEBUG
       sim_debug(DEBUG_IRQ, &cpu_dev, "set mpx irq %o %o %o %03o %03o %03o\n",
              dev & 0774, lvl, mpx, PIE, PIR, PIH);
#endif
    }
}
#endif

/*
 * Clear the interrupt flag for a device
 */
void clr_interrupt(int dev) {
    uint16   lvl;
    int      i;
    dev_irq[dev>>2] = 0;
    /* Update bus PI flags */
    for (lvl = i = 0; i < MAX_DEV; i++)
        lvl |= dev_irq[i];
    IOB_PI = lvl;
#if DEBUG
    if (dev > 4)
        sim_debug(DEBUG_IRQ, &cpu_dev, "clear irq %o\n", dev & 0774);
#endif
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
    lvl = IOB_PI;
    if (lvl == 0)
       pi_pending = 0;
    pi_req = (lvl & PIE) | PIR;
#if MPX_DEV
    /* Check if interrupt on PI channel 1 */
    if (mpx_enable && cpu_unit[0].flags & UNIT_MPX &&
                (pi_req & 0100) && (PIH & 0100) == 0) {
        pi_enc = 010;
        for(i = lvl = 0; i < MAX_DEV; i++) {
            if (dev_irq[i] & 0100) {
               int l = dev_irq[i] >> 8;
               if (l != 0 && l < pi_enc)
                   pi_enc = l;
            }
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
#if DEBUG
            sim_debug(DEBUG_IRQ, &cpu_dev, "restore irq %o %03o\n", lvl, PIH);
#endif
            PIH &= ~lvl;
#if KS_ITS
            pi_act &= ~lvl;
#endif
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

#if !KS
/*
 * PI device for KA and KI and KL
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
        if (res & 01000) { /* Bit 26 */
           PIE &= ~(*data & 0177);
        }
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
#if KI | KL
        if (res & 020000) { /* Bit 22 */
           PIR &= ~(*data & 0177);
        }
#endif
#if !KL
        if (res & 040000)   /* Bit 21 */
           parity_irq = 1;
        if (res & 0100000)  /* Bit 20 */
           parity_irq = 0;
#endif
        check_apr_irq();
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO PI %012llo\n", *data);
        break;

     case CONI:
        res = PIE;
        res |= (pi_enable << 7);
        res |= (PIH << 8);
#if KI | KL
        res |= ((uint64)(PIR) << 18);
#endif
#if KI
        res |= ((uint64)adr_flag << 31);
#endif
#if !KL
        res |= ((uint64)parity_irq << 15);
#endif
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI PI %012llo\n", *data);
        break;

    case DATAO:
#if KL
        if (dev & 040) {   /* SBDIAG */
            AB = (AB + 1) & RMASK;
            res = 0;
            if (((*data >> 31) & 030) == 010) {
                int mc = MEMSIZE / 262144;
                int c = (*data >> 31) & 07;
                int s = 0;
                if (c < mc) {
                    switch(*data & 037) {
                    case 0:  res = 06000000000LL; break;
                    case 1:  res = 00500000000LL; break;
                    case 2:  res = 0; break;
                    case 012:
                           res = 0;
                           s = (int)(0176000 & *data) << 6;
                           s /= 262144;
                           if (s != c)
                              res = 010000000LL;
                           break;
                    default: res = 0; break;
                    }
                }
            }
            MB = res;
            (void)Mem_write(0, 0);
            break;
        }
#else
        MI = *data;
        MI_flag = !MI_disable;
#ifdef PANDA_LIGHTS
        /* Set lights */
        ka10_lights_main (*data);
#endif
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
#endif

#if KL
void
update_times(int tim)
{
    uint64    temp;
    if (page_enable)  {
        temp = (M[eb_ptr + 0511] & CMASK) + (tim << 12);
        if (temp & SMASK)
           M[eb_ptr + 0510] = (M[eb_ptr+0510] + 1) & FMASK;
        M[eb_ptr + 0511] = temp & CMASK;
        if (FLAGS & USER) {
            temp = (M[ub_ptr + 0506] & CMASK) + (tim << 12);
            if (temp & SMASK)
               M[ub_ptr + 0505] = (M[ub_ptr+0505] + 1) & FMASK;
            M[ub_ptr + 0506] = temp & CMASK;
        }
    }
}

/*
 * Page device for KL10.
 */
t_stat dev_pag(uint32 dev, uint64 *data) {
    uint64 res = 0;
    int    i;
    switch(dev & 03) {
    case CONI:
        res = (eb_ptr >> 9);
        if (page_enable)
            res |= 020000;
        if (t20_page)
            res |= 040000;
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI PAG %012llo\n", *data);
        break;

     case CONO:
        eb_ptr = (*data & 017777) << 9;
        for (i = 0; i < 512; i++) {
            e_tlb[i] = 0;
            u_tlb[i] = 0;
        }
        for (;i < 546; i++)
            u_tlb[i] = 0;
        page_enable = (*data & 020000) != 0;
        t20_page = (*data & 040000) != 0;
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO PAG %012llo\n", *data);
        break;

    case DATAO:
        if (dev & 040) { /* CLRPT */
           int      page = (RMASK & AB) >> 9;
           int      i;

           page &= ~7;
           /* Map the page */
           for(i = 0; i < 8; i++) {
              u_tlb[page+i] = 0;
              e_tlb[page+i] = 0;
           }
           /* If not user do exec mappping */
           if (!t20_page && (page & 0740) == 0340) {
              /* Pages 340-377 via UBT */
              page += 01000 - 0340;
              for(i = 0; i < 8; i++)
                 u_tlb[page+i] = 0;
           }
        } else {
            res = *data;
            if (res & SMASK) {
                fm_sel = (uint8)(res >> 23) & 0160;
                prev_ctx = (res >> 20) & 0160;
            }
            if (QKLB && (res & BIT1) != 0) {
                /* Load previous section */
                prev_sect = (res >> 18) & 037;
            }
            if (res & BIT2) {
                if ((res & RSIGN) == 0) {
                    int   t;
                    double us = sim_activate_time_usecs (&cpu_unit[0]);
                    t = rtc_tim - ((int)us);
                    update_times(t);
                    rtc_tim = ((int)us);
                }
                ub_ptr = (res & 017777) << 9;
                for (i = 0; i < 512; i++) {
                   u_tlb[i] = 0;
                   e_tlb[i] = 0;
                }
                for (;i < 546; i++)
                   u_tlb[i] = 0;
           }
           sim_debug(DEBUG_DATAIO, &cpu_dev,
                    "DATAO PAG %012llo ebr=%06o ubr=%06o\n",
                    *data, eb_ptr, ub_ptr);
       }
       break;

    case DATAI:
       if (dev & 040) {
          /* Convert to MMU */
       }
       res = (ub_ptr >> 9);
       /* Set previous section */
       res |= ((uint64)(prev_ctx & 0160)) << 20;
       res |= ((uint64)(fm_sel & 0160)) << 23;
       res |= SMASK|BIT1|BIT2;
       if (QKLB)
           res |= ((uint64)prev_sect & 037) << 18;
       *data = res;
       sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAI PAG %012llo\n", *data);
       break;
    }
    return SCPE_OK;
}

/*
 * Cache control.
 * All operations set sweep done.
 */
t_stat dev_cca(uint32 dev, uint64 *data) {
    irq_flags |= SWP_DONE;
    *data = 0;
    check_apr_irq();
    return SCPE_OK;
}


/*
 * Check if the last operation caused a APR IRQ to be generated.
 */
void check_apr_irq() {
     if (pi_enable && apr_irq) {
         int flg = 0;
         clr_interrupt(0);
         flg = irq_enable & irq_flags;
         if (flg)
             set_interrupt(0, apr_irq);
     }
}


/*
 * APR device for KL10.
 */
t_stat dev_apr(uint32 dev, uint64 *data) {
    uint64 res = 0;

    switch(dev & 03) {
    case CONI:
        /* Read trap conditions */
        res = irq_flags | apr_irq;
        res |= ((uint64)irq_enable) << 18;
        if (irq_flags & irq_enable)
            res |= 010;
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI APR %012llo\n", *data);
        break;

     case CONO:
        /* Set trap conditions */
        res = *data;
        apr_irq = res & 07;
        clr_interrupt(0);
        if (res & 0200000)
            reset_all(1);
        if (res & 0100000) {  /* Enable interrupts */
            irq_enable |= 07760 & res;
        }
        if (res & 0040000) {  /* Disable interrupts */
            irq_enable &= ~(07760 & res);
        }
        if (res & 0020000) {   /* Clear interrupt */
            irq_flags &= ~(07760 & res);
        }
        if (res & 0010000) {   /* Set interrupt */
            irq_flags |= (07760 & res);
        }
        check_apr_irq();
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO APR %012llo\n", *data);
        break;

    case DATAO:
        brk_addr = *data & RMASK;
        brk_flags = 017 & (*data >> 23);
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAO APR %012llo\n", *data);
        break;

    case DATAI:
        *data = ((uint64)brk_flags) << 23;
        *data |= (uint64)brk_addr;
        sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAI APR %012llo\n", *data);
        break;
    }
    return SCPE_OK;
}

/*
 * MTR device for KL10.
 */
t_stat dev_mtr(uint32 dev, uint64 *data) {

    switch(dev & 03) {
    case CONI:
        /* Reader meters */
        *data = mtr_irq;
        if (mtr_enable)
            *data |= 02000;
        *data |= ((uint64)mtr_flags) << 12;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI MTR %012llo\n", *data);
        break;

     case CONO:
        /* WRTIME */
        mtr_irq = *data & 07;
        if (*data & 02000)
           mtr_enable = 1;
        if (*data & 04000)
           mtr_enable = 0;
        if (*data & RSIGN)
           mtr_flags = (*data >> 12) & 07;
        clr_interrupt(4 << 2);
        if (tim_val & 030000)
           set_interrupt(4 << 2, mtr_irq);
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO MTR %012llo\n", *data);
        break;

    case DATAO:
        /* MUUO */
        if (dev & 040) {
            sim_debug(DEBUG_DATAIO, &cpu_dev, "BLKO MTR %012llo\n", *data);
        } else {
            sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAO MTR %012llo\n", *data);
        }
        break;

    case DATAI:
        break;
    }
    return SCPE_OK;
}

/*
 * TIM device for KL10.
 */
t_stat dev_tim(uint32 dev, uint64 *data) {
    uint64 res;
    double us;
    UNIT   *uptr = &cpu_unit[1 + ITS];

    /* Update current timer count */
    if (sim_is_active(uptr)) {
       us = sim_activate_time_usecs (uptr) / 10;
       /* Check if going to period or overflow */
       if (tim_val & 0100000)
           tim_val = (tim_val & 0070000) + tim_per - (int)us;
       else
           tim_val = (tim_val & 0070000) + 010000 - (int)us;
    }
    /* Interval counter */
    clr_interrupt(4 << 2);
    sim_cancel(uptr);
    switch(dev & 03) {
    case CONI:
        /* Interval counter */
        res = tim_per;
        res |= tim_val & 070000;
        res |= ((uint64)(tim_val & 07777)) << 18;
        *data = res;
        sim_debug(DEBUG_CONI, &cpu_dev, "CONI TIM %012llo\n", *data);
        break;

     case CONO:
        tim_val &= 037777;   /* Clear run bit */
        tim_per = *data & 07777;
        if (*data & 020000)  /* Clear overflow and done */
            tim_val &= 07777;
        if (*data & 0400000) /* Clear counter */
            tim_val = 0;
        if (*data & 040000)  /* Enable counter */
            tim_val |= 040000;
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO TIM %012llo\n", *data);
        break;

    case DATAO:
        break;

    case DATAI:
        break;
    }
    /* If timer is on, figure out when it will go off */
    if (tim_val & 040000) {
        /* If we have already passed time, schedule to overflow */
        if ((tim_val & 07777) >= tim_per) {
           us = (float)((010000 - (tim_val & 07777)) * 10);
           tim_val &= 0077777;
        } else {
           us = (float)((tim_per - (tim_val & 07777)) * 10);
           tim_val |= 0100000;
        }
        (void)sim_activate_after_d(uptr, us);
    }
    if (tim_val & 030000)
        set_interrupt(4 << 2, mtr_irq);
    return SCPE_OK;
}

t_addr
tim_irq(uint32 dev, t_addr addr)
{
    return 0514;
}

#endif

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
            user_addr_cmp = (res & BIT4) != 0;
            small_user =    (res & BIT3) != 0;
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
           res |= BIT4;
       if (small_user)
           res |= BIT3;
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
     if (nxm_stop && nxm_flag) {
         RUN = 0;
     }
     if (pi_enable && apr_irq) {
         int flg = 0;
         clr_interrupt(0);
         flg |= inout_fail | nxm_flag | adr_flag;
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
                 sim_interval--;
                 res = M[071];
                 mon_base_reg = (res & 03777) << 9;
                 ac_stack = (res >> 9) & 0760;
                 user_base_reg = (res >> 9) & 03777000;
                 user_limit = page_limit[(res >> 30) & 07];
                 sim_interval--;
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
     if (nxm_stop && nxm_flag) {
         RUN = 0;
     }
     if (pi_enable && apr_irq) {
         int flg = 0;
         clr_interrupt(0);
         flg |= ((FLAGS & OVR) != 0) & ov_irq;
         flg |= ((FLAGS & FLTOVR) != 0) & fov_irq;
         flg |= nxm_flag | mem_prot | push_ovf | adr_flag;
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
        res |= (adr_flag << 14) | (push_ovf << 16) | (maoff >> 1);
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
        if (res & 040000)
            adr_flag = 0;
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

#if KS

/*
 * Check if the last operation caused a APR IRQ to be generated.
 */
void check_apr_irq() {
     if (pi_enable && apr_irq) {
         int flg = 0;
         clr_interrupt(0);
         flg = irq_enable & irq_flags;
         if (flg)
             set_interrupt(0, apr_irq);
     }
}

/*
 * Set interrupt from CTY.
 */
void cty_interrupt()
{
     irq_flags |= CON_IRQ;
     sim_debug(DEBUG_IRQ, &cpu_dev, "cty interrupt %06o\n", irq_enable);
     check_apr_irq();
}

/*
 * Execute instruction at location 071.
 */
void cty_execute(int addr)
{
     fe_xct = addr;
}

int
load_tlb(int uf, int page, int wr)
{
    uint64  data;

#if KS_ITS
    if (QITS) {
        uint64     dbr;
        int        pg;

        dbr = (uf)? ((page & 0400) ? dbr2 : dbr1) :
                    ((page & 0400) ? dbr3 : dbr4) ;
        pg = (page & 0377) >> 2;   /* 2 1024 word page entries */
        sim_interval--;
        data = M[dbr + pg];
        if ((page & 02) == 0)
            data &= ~(0020000LL << 18);
        else
            data &= ~0020000LL;
        M[dbr + pg] = data;
        if ((page & 02) == 0)
            data >>= 18;
        data &= RMASK;
        pg = 0;
        switch(data >> 16) {
        case 0:
                 fault_data = 0;
                 page_fault = 1;
                 return 0;           /* No access */
        case 2:                      /* R/W First */
                 pg |= KL_PAG_S;     /* Indicate writable */
                 /* Fall through */
        case 1:                      /* Read Only */
                 if (wr) {
                     fault_data = ((data >> 16) << 28) | (010000LL << 18);
                     page_fault = 1;
                     return 0;
                 }
                 /* Remap the flag bits */
                 pg |= KL_PAG_A;
                 break;
        case 3:  pg = KL_PAG_A|KL_PAG_W|KL_PAG_S;  break; /* R/W */
        }
        pg |= (data & 001777) << 1;
        /* Create 2 page table entries. */
        if (uf) {
            u_tlb[page & 0776] = pg;
            u_tlb[(page & 0776)|1] = pg|1;
            data = u_tlb[page];
        } else {
            e_tlb[page & 0776] = pg;
            e_tlb[(page & 0776)|1] = pg|1;
            data = e_tlb[page];
        }
    } else
#endif
#define PG_PUB   0040000
#define PG_WRT   0020000
#define PG_KEP   0010000
#define PG_CAC   0004000
#define PG_STG   (0000077LL << 18)
#define PG_IDX   0000777

#define PG_MASK  0000003777777LL
#define PG_AGE   0770000000000LL
#define PG_PAG   0003777
    if (t20_page) { /* Start with full access */
         int acc_bits = PG_WRT|PG_KEP|PG_CAC;
         uint64 cst_val = 0;
         int   index;
         int   pg;

        /* Get segment pointer */
        /* And save it */
        sim_interval--;
        if (uf)
           data = M[ub_ptr + 0540];
        else
           data = M[eb_ptr + 0540];
        /* Find correct page table */
sect_loop:
       switch ((data >> 33) & 07) {
       default:     /* Invalid page */
            fault_data = 0;
            page_fault = 1;
            return 0;
       case 1:      /* Direct page */
            /* Bit 4 = execute */
            /* Bit 3 = Write */
            /* Bit 2 = Read */
            acc_bits &= (data >> 18) & RMASK;
            break;

       case 2:      /* Shared page */
            acc_bits &= (data >> 18) & RMASK;
            sim_interval--;
            index = data & RMASK;
            data = M[index + (spt & PG_MASK)];
            break;

       case 3:      /* Indirect page */
            acc_bits &= (data >> 18) & RMASK;
            index = (data >> 18) & PG_IDX;
            sim_interval--;
            if (index != 0) {
                fault_data = 037LL << 30 | BIT8 |
                          ((data & ((PG_IDX << 18)|RMASK)) + (spt & PG_MASK));
                page_fault = 1;
                return 0;
            }
            data = M[(data & RMASK) + (spt & PG_MASK)];
            if ((data & PG_STG) != 0) {
                fault_data = 0;
                page_fault = 1;
                return 0;
            }
            pg = data & PG_PAG;
            sim_interval--;
            data = M[(pg << 9) | index];
            goto sect_loop;
        }
        if ((data & PG_STG) != 0) {
            fault_data = 0;
            page_fault = 1;
            return 0;
        }
        pg = data & PG_PAG;

        /* Update CST entry if needed */
        if ((cst & PG_MASK)) {
            sim_interval--;
            cst_val = M[(cst & PG_MASK) + pg];
            if ((cst_val & PG_AGE) == 0) {
                fault_data = 0;
                page_fault = 1;
                return 0;
            }
            M[(cst & PG_MASK) + pg] = (cst_val & cst_msk) | cst_dat;
        }

        /* Get address of page */
        sim_interval--;
        data = M[(pg << 9) | page];
pg_loop:

        /* Decode map pointer */
        switch ((data >> 33) & 07) {
        default:     /* Invalid page */
             fault_data = 0;
             page_fault = 1;
             return 0;
        case 1:      /* Direct page */
             /* Bit 4 = execute */
             /* Bit 3 = Write */
             /* Bit 2 = Read */
             acc_bits &= (data >> 18) & RMASK;
             break;

        case 2:      /* Shared page */
             acc_bits &= (data >> 18) & RMASK;
             sim_interval--;
             index = data & RMASK;
             data = M[index + (spt & PG_MASK)];
             break;

        case 3:      /* Indirect page */
             acc_bits &= (data >> 18) & RMASK;
             index = (data >> 18) & PG_IDX;
             sim_interval--;
             data = M[(data & RMASK) + (spt & PG_MASK)];
             if ((data & PG_STG) != 0) {
                 fault_data = 0;
                 page_fault = 1;
                 return 0;
             }
             pg = data & RMASK;
             sim_interval--;
             data = M[(pg << 9) | index];
             goto pg_loop;
        }

        /* Now have final page */
        if ((data & PG_STG) != 0) {
           fault_data = 0;
           page_fault = 1;
           return 0;
        }
        pg = data & PG_PAG;
        /* Check outside of memory */
        /* Update CST entry if needed */
        if ((cst & PG_MASK)) {
           sim_interval--;
           cst_val = M[(cst & PG_MASK) + pg];
           if ((cst_val  & PG_AGE) == 0) {
               fault_data = 0;
               page_fault = 1;
               return 0;
           }
           if (acc_bits & PG_WRT) {
               if (wr)
                  cst_val  |= 1;
          } else if (wr) { /* Trying to write and not writable */
               fault_data = 0 /* Write fault */;
               page_fault = 1;
               return 0;
           }
           M[(cst & PG_MASK) + pg] = (cst_val  & cst_msk) | cst_dat;
        } else {
           if (acc_bits & PG_WRT) {
               cst_val = 1;
           }
        }
        /* Now construct a TBL entry */
        /* A = accessable */
        /* W = writable */
        /* S = user */
        /* C = cache */
        data = pg | KL_PAG_A;
        if (acc_bits & PG_WRT) {
           if (cst_val & 1)
               data |= KL_PAG_W;   /* Set Modified page */
           data |= KL_PAG_S;      /* Set Writeable bit */
        }
        if (acc_bits & PG_CAC)
           data |= KL_PAG_C;
        /* And save it */
        if (uf)
           u_tlb[page] = data & RMASK;
        else
           e_tlb[page] = data & RMASK;
    } else {

       /* Map the page */
       sim_interval--;
       if (uf) {
           data = M[ub_ptr + (page >> 1)];
           u_tlb[page & 01776] = (uint32)(RMASK & (data >> 18));
           u_tlb[page | 1] = (uint32)(RMASK & data);
           data = u_tlb[page];
       } else {
           if (page & 0400)
               data = M[eb_ptr + (page >> 1)];
           else
               data = M[eb_ptr + (page >> 1) + 0600];
           e_tlb[page & 01776] = (uint32)(RMASK & (data >> 18));
           e_tlb[page | 1] = (uint32)(RMASK & data);
           data = e_tlb[page];
       }
    }
    return (int)(data);
}

/*
 * Handle page lookup on KS10
 *
 * addr is address to look up.
 * flag is set for pi cycle and user overide.
 * loc  is final address.
 * wr   indicates whether cycle is read or write.
 * cur_context is set when access should ignore xct_flag
 * fetch is set for instruction fetches.
 */
int page_lookup(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch) {
    int      data;
    int      page = (RMASK & addr) >> 9;
    int      uf = (FLAGS & USER) != 0;
    int      upmp = 0;

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* Figure out if this is a user space access */
    /*  AC = 1 use BYF5  */
    /*  AC = 2 use ptr_flg */
    /*  AC = 4 all general access */
    /*  AC = 8 only in cur_context EA calculations */
    if (flag) {
        uf = 0;
    } else if (xct_flag != 0 && !fetch) {
        if (((xct_flag & 8) != 0 && cur_context && !ptr_flg) ||
            ((xct_flag & 4) != 0 && !cur_context && !BYF5 && !ptr_flg) ||
            ((xct_flag & 2) != 0 && !cur_context && ptr_flg) ||
            ((xct_flag & 1) != 0 && !cur_context && BYF5 )) {
            uf = (FLAGS & USERIO) != 0;
        }
    }

#if KS_ITS
    if (!QITS) {     /* Do not do this on ITS */
#endif
    /* Handle KI paging odditiy */
    if (!uf && !t20_page && (page & 0740) == 0340) {
        /* Pages 340-377 via UBT */
        page += 01000 - 0340;
        upmp = 1;
    }
#if KS_ITS
    }
#endif

    /* Map the page */
    if (uf || upmp)
       data = u_tlb[page];
    else
       data = e_tlb[page];

    /* If not valid, go refill it */
    if (data == 0) {
        data = load_tlb(uf | upmp, page, wr);
        if (data == 0 && page_fault) {
            fault_data |= ((uint64)addr);
            if (uf)                      /* U */
                fault_data |= SMASK;
#if KS_ITS
            if (QITS) {
                return 0;
            }
#endif
            fault_data |= BIT8;
            if (fault_data & BIT1)
                return 0;
            if (wr)                      /* T */
               fault_data |= BIT5;       /* BIT5 */
            return 0;
        }
    }

    /* Check if we need to modify TLB entry for TOPS 20 */
    if (t20_page && (data & KL_PAG_A) && (wr & ((data & KL_PAG_W) == 0)) && (data & KL_PAG_S)) {
        /* Update CST entry if needed */
        if ((cst & PG_MASK)) {
           uint64 cst_val;
           int  pg = data & 03777;
           sim_interval--;
           cst_val = M[(cst & PG_MASK) + pg];
           M[(cst & PG_MASK) + pg] = (cst_msk & cst_val) | cst_dat | 1;
        }
        data |= KL_PAG_W;
        /* Map the page */
        if (uf || upmp)
           u_tlb[page] = data;
        else
           e_tlb[page] = data;
    }

    /* create location. */
    *loc = ((data & 03777) << 9) + (addr & 0777);

    /* KS does page fault if accessing outside of memory */
    if (*loc >= MEMSIZE) {
        fault_data = 037LL << 30 | BIT8 | *loc;
        page_fault = 1;
        irq_flags |= NXM_MEM;
        return 0;
    }

    /* Check for access error */
    if ((data & KL_PAG_A) == 0 || (wr != 0 && ((data & KL_PAG_W) == 0))) {
        fault_data = (uint64)addr;
        if (uf) {                    /* U */
           fault_data |= SMASK;      /*  BIT0 */
           u_tlb[page] = 0;
        } else {
           e_tlb[page] = 0;
        }
#if KS_ITS
        if (QITS) {
           /* Access bits:
            *    KL_PAG_A  means valid page.
            *    KL_PAG_S  means read write first
            *    KL_PAG_W  means read/write
            *
            *    00 no access = 0
            *    01 Read only = KL_PAG_A
            *    10 Read write first = KL_PAG_A|KL_PAG_S
            *    11 R/W = KL_PAG_A|KL_PAG_S|KL_PAG_W
            */
            /* Check if accessable */
            if ((data & KL_PAG_A) != 0) {
               if ((data & KL_PAG_S) != 0) {
                  fault_data |= 004000LL << 18;        /* PF2.9 */
               } else if ((data & KL_PAG_W) == 0) {
                  fault_data |= 002000LL << 18;        /* PF2.8 */
               }
            }
            if (wr) {
                fault_data |= 010000LL << 18;
            }
            page_fault = 1;
            return 0;
        }
#endif
        fault_data |= BIT8;
        if (wr)                      /* T */
           fault_data |= BIT5;       /* BIT5 */
        if (data & KL_PAG_A) {       /* A */
           fault_data |= BIT2;       /* BIT2 */
           if (data & KL_PAG_S)         /* S */
              fault_data |= BIT4;       /* BIT4 */
        }
        page_fault = 1;
        return 0;
    }

    return 1;
}

/*
 * Register access on KS 10
 */
#define get_reg(reg) FM[fm_sel|((reg) & 017)]

#define set_reg(reg, value) FM[fm_sel|((reg) & 017)] = (value)

int Mem_read(int flag, int cur_context, int fetch, int mod) {
    t_addr addr;

    if (AB < 020) {
        if (xct_flag != 0 && !fetch) {
            if (((xct_flag & 8) != 0 && cur_context && !ptr_flg) ||
                ((xct_flag & 4) != 0 && !cur_context && !BYF5 && !ptr_flg) ||
                ((xct_flag & 2) != 0 && !cur_context && ptr_flg) ||
                ((xct_flag & 1) != 0 && !cur_context && BYF5 )) {
               MB = FM[prev_ctx|AB];
               if (fetch == 0 && hst_lnt) {
                   hst[hst_p].mb = MB;
               }
               return 0;
            }
        }
        /* Check if invalid section */
        MB = get_reg(AB);
        UPDATE_MI(AB);
    } else {
        if (!page_lookup(AB, flag, &addr, mod, cur_context, fetch))
            return 1;
        if (addr >= MEMSIZE) {
            irq_flags |= NXM_MEM;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(addr, SWMASK('R')))
            watch_stop = 1;
        sim_interval--;
        MB = M[addr];
        modify = mod;
        last_addr = addr;
        UPDATE_MI(addr);
    }
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    return 0;
}

int Mem_write(int flag, int cur_context) {
    t_addr addr;

    if (AB < 020) {
        if (xct_flag != 0) {
            if (((xct_flag & 8) != 0 && cur_context && !ptr_flg) ||
                ((xct_flag & 4) != 0 && !cur_context && !BYF5 && !ptr_flg) ||
                ((xct_flag & 2) != 0 && !cur_context && ptr_flg) ||
                ((xct_flag & 1) != 0 && !cur_context && BYF5 )) {
               FM[prev_ctx|AB] = MB;
               return 0;
            }
        }
        set_reg(AB, MB);
        UPDATE_MI(AB);
    } else {
        if (modify) {
            if (sim_brk_summ && sim_brk_test(last_addr, SWMASK('W')))
                watch_stop = 1;
            M[last_addr] = MB;
            UPDATE_MI(last_addr);
            modify = 0;
            return 0;
        }

        if (!page_lookup(AB, flag, &addr, 1, cur_context, 0))
            return 1;
        if (addr >= MEMSIZE) {
            irq_flags |= NXM_MEM;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(addr, SWMASK('W')))
            watch_stop = 1;
        sim_interval--;
        M[addr] = MB;
        UPDATE_MI(addr);
    }
    return 0;
}
#endif

#if KL
int
load_tlb(int uf, int page, int wr)
{
    uint64  data;

#if KL_ITS
    if (QITS && t20_page) {
        uint64     dbr;
        int        pg;

        dbr = (uf)? ((page & 0400) ? dbr2 : dbr1) :
                    ((page & 0400) ? dbr3 : dbr4) ;
        pg = (page & 0377) >> 2;   /* 2 1024 word page entries */
        sim_interval--;
        data = M[dbr + pg];
        if ((page & 02) == 0)
            data &= ~0160000000000LL;
        else
            data &= ~0160000LL;
        M[dbr + pg] = data;
        if ((page & 02) == 0)
            data >>= 18;
        data &= RMASK;
        pg = 0;
        switch(data >> 16) {
        case 0:
                 fault_data = 033LL << 30;
                 page_fault = 1;
                 return 0;           /* No access */
        case 1:                      /* Read Only */
        case 2:                      /* R/W First */
                 if (wr) {
                     fault_data = 024LL << 30;
                     page_fault = 1;
                     return 0;
                 }
                 pg = KL_PAG_A;
                 break;
        case 3:  pg = KL_PAG_A|KL_PAG_W;  break; /* R/W */
        }
        pg |= (data & 017777) << 1;
        /* Create 2 page table entries. */
        if (uf) {
            u_tlb[page & 0776] = pg;
            u_tlb[(page & 0776)|1] = pg|1;
            data = u_tlb[page];
        } else {
            e_tlb[page & 0776] = pg;
            e_tlb[(page & 0776)|1] = pg|1;
            data = e_tlb[page];
        }
    } else
#endif
#define PG_PUB   0040000
#define PG_WRT   0020000
#define PG_KEP   0010000
#define PG_CAC   0004000
#define PG_STG   (0000077LL << 18)
#define PG_IDX   0000777

#define PG_MASK  0000003777777LL
#define PG_AGE   0770000000000LL
#define PG_PAG   0017777
    if (t20_page) { /* Start with full access */
         int acc_bits = PG_PUB|PG_WRT|PG_KEP|PG_CAC;
         uint64 cst_val = 0;
         int   index;
         int   pg;
#if EPT440
         int   base = 0440;
#else
         int   base = 0540;
#endif

        /* Get segment pointer */
        /* And save it */
        if (QKLB)
            base = 0540 + (sect & 037);
        sim_interval--;
        if (uf)
           data = M[ub_ptr + base];
        else
           data = M[eb_ptr + base];
        /* Find correct page table */
sect_loop:
       switch ((data >> 33) & 07) {
       default:     /* Invalid page */
            fault_data = 0;
            page_fault = 1;
            return 0;
       case 1:      /* Direct page */
            /* Bit 4 = execute */
            /* Bit 3 = Write */
            /* Bit 2 = Read */
            acc_bits &= (data >> 18) & RMASK;
            break;

       case 2:      /* Shared page */
            acc_bits &= (data >> 18) & RMASK;
            sim_interval--;
            index = data & RMASK;
            data = M[index + (spt & PG_MASK)];
            break;

       case 3:      /* Indirect page */
            acc_bits &= (data >> 18) & RMASK;
            index = (data >> 18) & PG_IDX;
            sim_interval--;
            data = M[(data & RMASK) + (spt & PG_MASK)];
            if ((data & PG_STG) != 0) {
                fault_data = 0;
                page_fault = 1;
                return 0;
            }
            pg = data & PG_PAG;
            sim_interval--;
            data = M[(pg << 9) + index];
            goto sect_loop;
        }
        if ((data & PG_STG) != 0) {
            fault_data = 0;
            page_fault = 1;
            return 0;
        }
        pg = data & PG_PAG;

        /* Update CST entry if needed */
        if ((cst & PG_MASK)) {
            sim_interval--;
            cst_val = M[(cst & PG_MASK) + pg];
            if ((cst_val & PG_AGE) == 0) {
                fault_data = 0;
                page_fault = 1;
                return 0;
            }
            M[(cst & PG_MASK) + pg] = (cst_val & cst_msk) | cst_dat;
        }

        /* Get address of page */
        sim_interval--;
        data = M[(pg << 9) | page];
pg_loop:

        /* Decode map pointer */
        switch ((data >> 33) & 07) {
        default:     /* Invalid page */
             fault_data = 0;
             page_fault = 1;
             return 0;
        case 1:      /* Direct page */
             /* Bit 4 = execute */
             /* Bit 3 = Write */
             /* Bit 2 = Read */
             acc_bits &= (data >> 18) & RMASK;
             break;

        case 2:      /* Shared page */
             acc_bits &= (data >> 18) & RMASK;
             sim_interval--;
             index = data & RMASK;
             data = M[index + (spt & PG_MASK)];
             break;

        case 3:      /* Indirect page */
             acc_bits &= (data >> 18) & RMASK;
             index = (data >> 18) & PG_IDX;
             sim_interval--;
             data = M[(data & RMASK) + (spt & PG_MASK)];
             if ((data & PG_STG) != 0) {
                 fault_data = 0;
                 page_fault = 1;
                 return 0;
             }
             pg = data & RMASK;
             sim_interval--;
             data = M[(pg << 9) + index];
             goto pg_loop;
        }

        /* Now have final page */
        if ((data & PG_STG) != 0) {
           fault_data = 0;
           page_fault = 1;
           return 0;
        }
        pg = data & PG_PAG;
        /* Check outside of memory */
        /* Update CST entry if needed */
        if ((cst & PG_MASK)) {
           sim_interval--;
           cst_val = M[(cst & PG_MASK) + pg];
           if ((cst_val  & PG_AGE) == 0) {
               fault_data = 0;
               page_fault = 1;
               return 0;
           }
           if (acc_bits & PG_WRT) {
               if (wr)
                  cst_val  |= 1;
          } else if (wr) { /* Trying to write and not writable */
               fault_data = 0 /* Write fault */;
               page_fault = 1;
               return 0;
           }
           M[(cst & PG_MASK) + pg] = (cst_val  & cst_msk) | cst_dat;
        } else {
           if (acc_bits & PG_WRT) {
               cst_val = 1;
           }
        }
        /* Now construct a TBL entry */
        /* A = accessable */
        /* P = public */
        /* W = writable */
        /* S = user */
        /* C = cache */
        data = pg | KL_PAG_A;
        if (acc_bits & PG_PUB)
           data |= KL_PAG_P;                /* P */
        if (acc_bits & PG_WRT) {
           if (cst_val & 1)
               data |= KL_PAG_W;   /* Set Modified page */
           data |= KL_PAG_S;      /* Set Writeable bit */
        }
        if (acc_bits & PG_CAC)
           data |= KL_PAG_C;
        if (QKLB)
           data |= (sect & 037) << 18;
        /* And save it */
        if (uf)
           u_tlb[page] = data & (SECTM|RMASK);
        else
           e_tlb[page] = data & (SECTM|RMASK);
    } else {

       /* Map the page */
       sim_interval--;
       if (uf) {
           data = M[ub_ptr + (page >> 1)];
           u_tlb[page & 01776] = (uint32)(RMASK & (data >> 18));
           u_tlb[page | 1] = (uint32)(RMASK & data);
           data = u_tlb[page];
       } else {
           if (page & 0400)
               data = M[eb_ptr + (page >> 1)];
           else
               data = M[eb_ptr + (page >> 1) + 0600];
           e_tlb[page & 01776] = (uint32)(RMASK & (data >> 18));
           e_tlb[page | 1] = (uint32)(RMASK & data);
           data = e_tlb[page];
       }
    }
    return (int)(data);
}

/*
 * Handle page lookup on KL10
 *
 * addr is address to look up.
 * flag is set for pi cycle and user overide.
 * loc  is final address.
 * wr   indicates whether cycle is read or write.
 * cur_context is set when access should ignore xct_flag
 * fetch is set for instruction fetches.
 */
int page_lookup(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch) {
    int      data;
    int      page = (RMASK & addr) >> 9;
    int      uf = (FLAGS & USER) != 0;
    int      pub = (FLAGS & PUBLIC) != 0;
    int      upmp = 0;

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* Handle address breaks */
    if (addr == brk_addr && uf == (brk_flags & 1) && (FLAGS & ADRFLT) == 0) {
        if ((fetch && (brk_flags & 010) != 0) ||
            (!fetch && !wr && (brk_flags & 04) != 0) ||
            (wr && (brk_flags & 02) != 0)) {
            fault_data = ((uint64)addr) | 023LL << 30 |((uf)?SMASK:0);
            page_fault = 1;
            return 0;
        }
    }

    /* Figure out if this is a user space access */

    /*  AC = 1 use BYF5  */
    /*  AC = 2 use ptr_flg */
    /*  AC = 4 all general access */
    /*  AC = 8 only in cur_context EA calculations */
    if (flag) {
        uf = 0;
        sect = 0;
    } else if (xct_flag != 0 && !fetch) {
        if (((xct_flag & 8) != 0 && cur_context && !ptr_flg) ||
            ((xct_flag & 4) != 0 && !cur_context && !BYF5 && !ptr_flg) ||
            ((xct_flag & 2) != 0 && !cur_context && ptr_flg) ||
            ((xct_flag & 1) != 0 && !cur_context && BYF5 )) {
            uf = (FLAGS & USERIO) != 0;
            pub = (FLAGS & PRV_PUB) != 0;

            if ((xct_flag & 014) == 04 && !cur_context && !ptr_flg && glb_sect == 0)
                sect = prev_sect;
            if ((xct_flag & 03) == 01 && BYF5 && glb_sect == 0)
                sect = prev_sect;
        }
    }

    /* Check if invalid section */
    if (QKLB && t20_page && (sect & 07740) != 0) {
        fault_data = (027LL << 30) | (((uint64)sect) << 18) | (uint64)addr;
        if (uf)                      /* U */
           fault_data |= SMASK;      /*  BIT0 */
        page_fault = 1;
        return 0;
    }

#if KL_ITS
    if (!QITS) {     /* Do not do this on ITS */
#endif
    /* Handle KI paging odditiy */
    if (!uf && !t20_page && (page & 0740) == 0340) {
        /* Pages 340-377 via UBT */
        page += 01000 - 0340;
        upmp = 1;
    }
#if KL_ITS
    }
#endif

    /* Map the page */
    if (uf || upmp)
       data = u_tlb[page];
    else
       data = e_tlb[page];

    if (QKLB && t20_page && ((data >> 18) & 037) != sect)
        data = 0;
    /* If not valid, go refill it */
    if (data == 0) {
        data = load_tlb(uf | upmp, page, wr);
        if (data == 0 && page_fault) {
            fault_data |= ((uint64)addr);
            if (uf)                      /* U */
                fault_data |= SMASK;
#if KL_ITS
            if (QITS)
                return 0;
#endif
            fault_data |= BIT8;
            if (QKLB && t20_page)
                fault_data |= (((uint64)sect) << 18);
            if (fault_data & BIT1)
                return 0;
            if (wr)                      /* T */
               fault_data |= BIT5;       /* BIT5 */
            return 0;
        }
    }

    /* Check if we need to modify TLB entry for TOPS 20 */
    if (t20_page && (data & KL_PAG_A) && (wr & ((data & KL_PAG_W) == 0)) && (data & KL_PAG_S)) {
        /* Update CST entry if needed */
        if ((cst & PG_MASK)) {
           uint64 cst_val;
           int  pg = data & 017777;
           sim_interval--;
           cst_val = M[(cst & PG_MASK) + pg];
           M[(cst & PG_MASK) + pg] = (cst_msk & cst_val) | cst_dat | 1;
        }
        data |= KL_PAG_W;
        /* Map the page */
        if (uf || upmp)
           u_tlb[page] = data;
        else
           e_tlb[page] = data;
    }

    /* create location. */
    *loc = ((data & 017777) << 9) + (addr & 0777);

    /* If PUBLIC and private page, make sure we are fetching a Portal */
    if ((data & KL_PAG_A) && !flag && pub && ((data & KL_PAG_P) == 0) &&
         (!fetch || !OP_PORTAL(M[*loc]))) {
        /* Handle public violation */
        fault_data = ((uint64)addr) | 021LL << 30 | BIT8 |((uf)?SMASK:0);
        if (QKLB && t20_page)
            fault_data |= (((uint64)sect) << 18);
        page_fault = 1;
        return 0;
    }


    /* Check for access error */
    if ((data & KL_PAG_A) == 0 || (wr & ((data & KL_PAG_W) == 0))) {
#if KL_ITS
        if (QITS) {
            /* Remap the flag bits */
            if (uf) {                    /* U */
               u_tlb[page] = 0;
            } else {
               e_tlb[page] = 0;
            }
            if ((data & KL_PAG_A) == 0) {
                fault_data = ((uint64)addr) | 033LL << 30 |((uf)?SMASK:0);
            } else {
                fault_data = ((uint64)addr) | 024LL << 30 |((uf)?SMASK:0);
            }
            page_fault = 1;
            return 0;
        }
#endif
        fault_data = BIT8 | (uint64)addr;
        if (QKLB && t20_page)
            fault_data |= (((uint64)sect) << 18);
        /* Remap the flag bits */
        if (uf) {                    /* U */
           fault_data |= SMASK;      /*  BIT0 */
           u_tlb[page] = 0;
        } else {
           e_tlb[page] = 0;
        }
        if (data & KL_PAG_C)         /* C */
           fault_data |= BIT7;       /* BIT7 */
        if (data & KL_PAG_P)         /* P */
           fault_data |= BIT6;       /* BIT6 */
        if (wr)                      /* T */
           fault_data |= BIT5;       /* BIT5 */
        if (data & KL_PAG_S)         /* S */
           fault_data |= BIT4;       /* BIT4 */
        if (data & KL_PAG_W)         /* W */
           fault_data |= BIT3;       /* BIT3 */
        if (data & KL_PAG_A)         /* A */
           fault_data |= BIT2;       /* BIT2 */
        page_fault = 1;
        return 0;
    }


    /* If fetching from public page, set public flag */
    if (fetch && ((data & KL_PAG_P) != 0))
        FLAGS |= PUBLIC;
    return 1;
}

/*
 * Register access on KL 10
 */
#define get_reg(reg) FM[fm_sel|((reg) & 017)]

#define set_reg(reg, value) FM[fm_sel|((reg) & 017)] = (value)

int Mem_read(int flag, int cur_context, int fetch, int mod) {
    t_addr addr;

    if (AB < 020 && ((QKLB && (glb_sect == 0 || sect == 0 ||
              (glb_sect && sect == 1))) || !QKLB)) {
        if (xct_flag != 0 && !fetch) {
            if (((xct_flag & 8) != 0 && cur_context && !ptr_flg) ||
                ((xct_flag & 4) != 0 && !cur_context && !BYF5 && !ptr_flg) ||
                ((xct_flag & 2) != 0 && !cur_context && ptr_flg) ||
                ((xct_flag & 1) != 0 && !cur_context && BYF5 )) {
               MB = FM[prev_ctx|AB];
               if (fetch == 0 && hst_lnt) {
                   hst[hst_p].mb = MB;
               }
               return 0;
            }
        }
        /* Check if invalid section */
        if (QKLB && t20_page && !flag && (sect & 07740) != 0) {
            fault_data = (027LL << 30) | (uint64)AB  | (((uint64)sect) << 18);
            if (USER==0)                 /* U */
               fault_data |= SMASK;      /*  BIT0 */
            page_fault = 1;
            return 1;
        }
        MB = get_reg(AB);
        UPDATE_MI(AB);
    } else {
        if (!page_lookup(AB, flag, &addr, mod, cur_context, fetch))
            return 1;
        if (addr >= MEMSIZE) {
            irq_flags |= NXM_MEM;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        sim_interval--;
        MB = M[addr];
        modify = mod;
        last_addr = addr;
        UPDATE_MI(addr);
    }
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    return 0;
}

int Mem_write(int flag, int cur_context) {
    t_addr addr;

    if (AB < 020 && ((QKLB && (glb_sect == 0 || sect == 0 ||
                        (glb_sect && sect == 1))) || !QKLB)) {
        if (xct_flag != 0) {
            if (((xct_flag & 8) != 0 && cur_context && !ptr_flg) ||
                ((xct_flag & 4) != 0 && !cur_context && !BYF5 && !ptr_flg) ||
                ((xct_flag & 2) != 0 && !cur_context && ptr_flg) ||
                ((xct_flag & 1) != 0 && !cur_context && BYF5 )) {
               FM[prev_ctx|AB] = MB;
               return 0;
            }
        }
        /* Check if invalid section */
        if (QKLB && t20_page && !flag && (sect & 07740) != 0) {
            fault_data = (027LL << 30) | (uint64)AB  | (((uint64)sect) << 18);
            if (USER==0)                 /* U */
               fault_data |= SMASK;      /*  BIT0 */
            page_fault = 1;
            return 0;
        }
        set_reg(AB, MB);
        UPDATE_MI(AB);
    } else {
        if (modify) {
            if (sim_brk_summ && sim_brk_test(last_addr, SWMASK('W')))
                watch_stop = 1;
            M[last_addr] = MB;
            UPDATE_MI(last_addr);
            modify = 0;
            return 0;
        }
        if (!page_lookup(AB, flag, &addr, 1, cur_context, 0))
            return 1;
        if (addr >= MEMSIZE) {
            irq_flags |= NXM_MEM;
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        sim_interval--;
        M[addr] = MB;
        UPDATE_MI(addr);
    }
    return 0;
}

/* executive page table lookup */
int exec_page_lookup(t_addr addr, int wr, t_addr *loc)
{
    int      data;
    int      page = (RMASK & addr) >> 9;
    int      upmp = 0;
    int      sav_sect = sect;

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 0;
    }

    /* Handle KI paging odditiy */
    if (!t20_page && (page & 0740) == 0340) {
        /* Pages 340-377 via UBT */
        page += 01000 - 0340;
        upmp = 1;
    }

    /* Map the page */
    if (upmp)
       data = u_tlb[page];
    else
       data = e_tlb[page];

    /* If not valid, go refill it */
    if (data == 0 || (data & 037) != 0) {
        sect = 0;
        data = load_tlb(upmp, page, wr);
        if (data == 0) {
           page_fault = 0;
           return 1;
        }
        sect = sav_sect;
    }
    *loc = ((data & 017777) << 9) + (addr & 0777);
    return 0;
}

int Mem_examine_word(int n, int wrd, uint64 *data) {
    t_addr   addr = 0144 + (8 * n) + eb_ptr;

    if (addr >= MEMSIZE)
        return 1;
    if (M[addr] == 0 || (uint64)wrd > M[addr])
        return 1;
    addr = (M[addr+1] + wrd) & RMASK;
    if (exec_page_lookup(addr, 0, &addr))
        return 1;
    *data = M[addr];
    return 0;
}

int Mem_deposit_word(int n, int wrd, uint64 *data) {
    t_addr   addr = 0146 + (8 * n) + eb_ptr;

    if (addr >= MEMSIZE)
        return 1;
    if (M[addr] == 0 || (uint64)wrd > M[addr])
        return 1;
    addr = (M[addr+1] + wrd) & RMASK;
    if (exec_page_lookup(addr, 1, &addr))
        return 1;
    M[addr] = *data;
    return 0;
}

/*
 * Read in 16 bits of data from a byte pointer.
 */
int Mem_read_byte(int n, uint16 *data, int byte) {
    t_addr   addr;
    uint64   val;
    uint64   msk;
    int      p, s, np;
    int      need = byte? 8: 16;

    *data = 0;
    while (need > 0) {
        addr = 0140 + (8 * n) + eb_ptr;
        if (addr >= MEMSIZE)
            return 0;
        val = M[addr];
        s = (val >> 24) & 077;
        p = (((val >> 30) & 077) + (0777 ^ s) + 1) & 0777;
        if (p & 0400) {
            p = np = (36 + (0777 ^ s) + 1) & 0777;
            val = (val & LMASK) | ((val + 1) & RMASK);
        } else
            np = p;
        np &= 077;
        val &= PMASK;
        val |= (uint64)(np) << 30;
        M[addr] = val;
        addr = val & RMASK;
        if (exec_page_lookup((int)(val & RMASK), 0, &addr))
            return 0;
        /* Generate mask for given size */
        msk = (uint64)(1) << s;
        msk--;
        val = M[addr];
        val = (val >> p) & msk;
        if (s > 8)
           need -= 16;
        else
           need -= 8;
        if (need >= 0)
           *data |= val << need;
        UPDATE_MI(addr);
    }
    return s;
}

int Mem_write_byte(int n, uint16 *data) {
    t_addr   addr;
    uint64   val;
    uint64   msk;
    int      p, s, np;
    int      need = 16;
    uint16   dat = *data;

    dat = ((dat >> 8) & 0377) | ((dat & 0377) << 8);
    while (need > 0) {
        addr = 0141 + (8 * n) + eb_ptr;
        if (addr >= MEMSIZE)
            return 0;
        val = M[addr];
        if (val == 0)
            return 1;
        s = (val >> 24) & 077;
        p = (((val >> 30) & 077) + (0777 ^ s) + 1) & 0777;
        if (p & 0400) {
            p = np = (36 + (0777 ^ s) + 1) & 0777;
            val = (val & LMASK) | ((val + 1) & RMASK);
        } else
            np = p;
        np &= 077;
        val &= PMASK;
        val |= (uint64)(np) << 30;
        M[addr] = val;
        addr = val & RMASK;
        if (exec_page_lookup((int)(val & RMASK), 1, &addr))
            return 0;
        /* Generate mask for given size */
        msk = (uint64)(1) << s;
        msk--;
        msk <<= p;
        val = M[addr];
        val &= CM(msk);
        val |= msk & (((uint64)(dat >> (need - s))) << p);
        M[addr] = val;
        need -= s;
        UPDATE_MI(addr);
    }
    return s;
}

#endif

#if KA | KI
static void
address_conditions (int fetch, int write)
{
    int cond;
    if (fetch)
        cond = ADR_IFETCH;
    else if (write)
        cond = ADR_WRITE;
    else
        cond = ADR_DFETCH;
    if (adr_cond & cond) {
        if (adr_cond & ADR_STOP)
            watch_stop = 1;
        if (adr_cond & ADR_BREAK)
            adr_flag = 1;
    }
    check_apr_irq();
}
#endif

#if KI
/*
 * Load the TLB entry, used for both page_lookup and MAP.
 * Do not call this for direct map executive pages.
 */
int
load_tlb(int uf, int page)
{
    uint64  data;
    int     base = 0;
    int     upmp = 0;

    if (!uf) {
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
            /* Return what MAP wants to see */
            return (KI_PAG_A | KI_PAG_X | page);
        }
    }
    /* Map the page */
    sim_interval--;
    if (base) {
        data = M[eb_ptr + (page >> 1)];
        e_tlb[page & 0776] = RMASK & (data >> 18);
        e_tlb[page | 1] = RMASK & data;
        data = e_tlb[page];
        pag_reload = ((pag_reload + 1) & 037) | 040;
        last_page = ((page ^ 0777) << 1)|1;
    } else {
        data = M[ub_ptr + (page >> 1)];
        u_tlb[page & 01776] = RMASK & (data >> 18);
        u_tlb[page | 1] = RMASK & data;
        data = u_tlb[page];
        pag_reload = ((pag_reload + 1) & 037) | 040;
        if (upmp)
           last_page = (((page-0440) ^ 0777) << 1) | 1;
        else
           last_page = ((page ^ 0777) << 1);
    }
    return (int)(data & RMASK);
}

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
int page_lookup(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch, int modify) {
    int      data;
    int      page = (RMASK & addr) >> 9;
    int      uf = (FLAGS & USER) != 0;
    int      pub = (FLAGS & PUBLIC) != 0;

    if (page_fault)
        return 0;

    if (adr_cond && addr == AS)
        address_conditions (fetch, wr);

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else if (xct_flag != 0 && !cur_context) {
             if (((xct_flag & 2) != 0 && wr != 0) ||
                 ((xct_flag & 1) != 0 && (wr == 0 || modify))) {
                 uf = (FLAGS & USERIO) != 0;
                 pub = (FLAGS & PRV_PUB) != 0;
             }
    }

    /* If this is modify instruction use write access */
    wr |= modify;

    /* If user, check if small user enabled */
    if (uf) {
        if (small_user && (page & 0340) != 0) {
            fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 27) | 020LL;
            page_fault = 1;
            return 0;
        }
    }

    /* Handle direct pages */
    if (!uf && page < 0340) {
        /* Check if supervisory mode */
        *loc = addr;
        /* If PUBLIC and private page, make sure we are fetching a Portal */
        if (!flag && pub &&
            (!fetch || (M[addr] & 00777040000000LL) != 0254040000000LL)) {
           /* Handle public violation */
            fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 27)
                                  | 021LL;
            page_fault = 1;
            return !wr;
        }
        return 1;
    }
    data = load_tlb(uf, page);
    *loc = ((data & 017777) << 9) + (addr & 0777);

    /* Check for access error */
    if ((data & KI_PAG_A) == 0 || (wr & ((data & KI_PAG_W) == 0))) {
        page = (RMASK & addr) >> 9;
        fault_data = ((((uint64)(page))<<18) | ((uint64)(uf) << 27)) & LMASK;
        fault_data |= (data & KI_PAG_A) ? 010LL : 0LL;   /* A */
        fault_data |= (data & KI_PAG_W) ? 004LL : 0LL;   /* W */
        fault_data |= (data & KI_PAG_S) ? 002LL : 0LL;   /* S */
        fault_data |= wr;
        page_fault = 1;
        return 0;
    }

    /* If PUBLIC and private page, make sure we are fetching a Portal */
    if (!flag && pub && ((data & KI_PAG_P) == 0) && (!fetch || !OP_PORTAL(M[*loc]))) {
        /* Handle public violation */
        fault_data = (((uint64)(page))<<18) | ((uint64)(uf) << 27) | 021LL;
        page_fault = 1;
        return 0;
    }

    /* If fetching from public page, set public flag */
    if (fetch && ((data & KI_PAG_P) != 0))
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

int Mem_read(int flag, int cur_context, int fetch, int mod) {
    t_addr addr;

    if (AB < 020) {
        if ((FLAGS & USER) == 0) {
            if (!cur_context && ((xct_flag & 1) != 0)) {
                if (FLAGS & USERIO) {
                    if (fm_sel == 0)
                       goto read;
                    MB = FM[fm_sel|AB];
                } else {
                    MB = M[ub_ptr + ac_stack + AB];
                    --sim_interval;
                }
                if (fetch == 0 && hst_lnt) {
                    hst[hst_p].mb = MB;
                }
                MB = get_reg(AB);
                return 0;
            }
        }
        MB = get_reg(AB);
    } else {
read:
        if (!page_lookup(AB, flag, &addr, 0, cur_context, fetch, mod))
            return 1;
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        sim_interval--;
        MB = M[addr];
        modify = mod;
        last_addr = addr;
    }
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    UPDATE_MI(AB);
    return 0;
}

int Mem_write(int flag, int cur_context) {
    t_addr addr;

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
        UPDATE_MI(AB);
    } else {
        if (modify) {
            if (sim_brk_summ && sim_brk_test(last_addr, SWMASK('W')))
                watch_stop = 1;
            M[last_addr] = MB;
            UPDATE_MI(last_addr);
            modify = 0;
            return 0;
        }
write:
        if (!page_lookup(AB, flag, &addr, 1, cur_context, 0, 0))
            return 1;
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
         sim_interval--;
        M[addr] = MB;
        UPDATE_MI(addr);
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
    unsigned int entry = (reg & 01777777) + ((page & 0377) >> 1);
    if ((page >> 1) > len) {
       fault_data |= 0200;
       return 1;
    }
    if (entry >= MEMSIZE) {
        nxm_flag = 1;
        fault_data |= 0400;
        check_apr_irq();
        return 1;
    }
    sim_interval--;
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

int page_lookup_its(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch, int modify) {
    uint64   data;
    int      page = (RMASK & addr) >> 10;
    int      acc;
    int      uf = (FLAGS & USER) != 0;
    int      fstr = (fault_data & 0770) == 0;

    if (adr_cond && addr == AS)
        address_conditions (fetch, wr);

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else if (xct_flag != 0 && !cur_context) {
             if (((xct_flag & 2) != 0 && wr != 0) ||
                 ((xct_flag & 1) != 0 && (wr == 0 || modify))) {
                 uf = 1;
             }
    }

    /* If this is modify instruction use write access */
    wr |= modify;

    /* AC & 1 = ??? */
    /* AC & 2 = Read User */
    /* AC & 4 = Write User */
    /* AC & 8 = Inhibit mem protect, skip */

    /* Add in MAR checking */
    if (addr == (mar & RMASK) && uf == (((mar >> 18) & 04) != 0)) {
       switch((mar >> 18) & 03) {
       case 0: break;
       case 1: if (fetch) {
                  mem_prot = 1;
                  fault_data |= 2;
                  check_apr_irq();
               }
               break;
       case 2: if (!wr)
                  break;
               /* Fall through */
       case 3: mem_prot = 1;
               fault_data |= 2;
               check_apr_irq();
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
    if (fstr)
        fault_addr = (page) | ((uf)? 0400 : 0) | ((data & 01777) << 9);
    if ((xct_flag & 04) == 0) {
        mem_prot = 1;
        fault_data |= 01000;
        check_apr_irq();
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
int Mem_read_its(int flag, int cur_context, int fetch, int mod) {
    t_addr addr;

    if (AB < 020) {
        if ((xct_flag & 1) != 0 && !cur_context) {
           MB = M[(ac_stack & 01777777) + AB];
           if (fetch == 0 && hst_lnt) {
               hst[hst_p].mb = MB;
           }
           return 0;
        }
        MB = get_reg(AB);
        UPDATE_MI(AB);
    } else {
        if (!page_lookup_its(AB, flag, &addr, 0, cur_context, fetch, mod))
            return 1;
#if NUM_DEVS_AUXCPU > 0
        if (AUXCPURANGE(addr) && QAUXCPU) {
            if (auxcpu_read (addr, &MB)) {
                nxm_flag = 1;
                check_apr_irq();
                return 1;
            }
            return 0;
        }
#endif
#if NUM_DEVS_TEN11 > 0
        if (T11RANGE(addr) && QTEN11) {
            if (ten11_read (addr, &MB)) {
                nxm_flag = 1;
                check_apr_irq();
                return 1;
            }
            return 0;
        }
#endif
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        sim_interval--;
        MB = M[addr];
        last_addr = addr;
        modify = mod;
        UPDATE_MI(addr);
    }
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_write_its(int flag, int cur_context) {
    t_addr addr;

    if (AB < 020) {
        if ((xct_flag & 2) != 0 && !cur_context) {
            M[(ac_stack & 01777777) + AB] = MB;
            UPDATE_MI((ac_stack & 01777777) + AB);
            return 0;
        }
        set_reg(AB, MB);
        UPDATE_MI(AB);
    } else {
        if (modify) {
            if (sim_brk_summ && sim_brk_test(last_addr, SWMASK('W')))
                watch_stop = 1;
            M[last_addr] = MB;
            UPDATE_MI(last_addr);
            modify = 0;
            return 0;
        }
        if (!page_lookup_its(AB, flag, &addr, 1, cur_context, 0, 0))
            return 1;
        UPDATE_MI(addr);
#if NUM_DEVS_AUXCPU > 0
        if (AUXCPURANGE(addr) && QAUXCPU) {
            if (auxcpu_write (addr, MB)) {
                nxm_flag = 1;
                check_apr_irq();
                return 1;
            }
            return 0;
        }
#endif
#if NUM_DEVS_TEN11 > 0
        if (T11RANGE(addr) && QTEN11) {
            if (ten11_write (addr, MB)) {
                nxm_flag = 1;
                check_apr_irq();
                return 1;
            }
            return 0;
        }
#endif
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        sim_interval--;
        M[addr] = MB;
        UPDATE_MI(addr);
    }
    return 0;
}
#endif

#if BBN
int page_lookup_bbn(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch) {
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
    int      lvl = 0;
    int      page = (RMASK & addr) >> 9;
    int      uf = (FLAGS & USER) != 0;
    int      map = page;
    int      match;

    if (page_fault)
        return 0;

    if (adr_cond && addr == AS)
        address_conditions (fetch, wr);

    /* If paging is not enabled, address is direct */
    if (!page_enable) {
        *loc = addr;
        return 1;
    }

    /* Umove instructions handled here */
    if ((IR & 0774) == 0100 && (FLAGS & EXJSYS) == 0)
        uf = 1;
    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else {
         if (QWAITS && xct_flag != 0 && !fetch) {
             if (xct_flag & 010 && cur_context)   /* Indirect */
                 uf = 1;
             if (xct_flag & 004 && wr == 0)       /* XR */
                 uf = 1;
             if (xct_flag & 001 && (wr == 1 || BYF5))  /* XW or XLB or XDB */
                 uf = 1;
         }
         if (!QWAITS && (FLAGS & EXJSYS) == 0 && xct_flag != 0 && !fetch) {
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
        sim_interval--;
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
        sim_interval--;
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
        sim_interval--;
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
    sim_interval--;
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
int Mem_read_bbn(int flag, int cur_context, int fetch, int mod) {
    t_addr addr;

    /* If not doing any special access, just access register */
    if (AB < 020 && ((xct_flag == 0 || fetch || cur_context || (FLAGS & USER) != 0))) {
        MB = get_reg(AB);
        if (fetch == 0 && hst_lnt) {
            hst[hst_p].mb = MB;
        }
        UPDATE_MI(AB);
        return 0;
    }
    if (!page_lookup_bbn(AB, flag, &addr, mod, cur_context, fetch))
        return 1;
    if (addr < 020) {
        MB = get_reg(AB);
        if (fetch == 0 && hst_lnt) {
            hst[hst_p].mb = MB;
        }
        UPDATE_MI(AB);
        return 0;
    }
    if (addr >= MEMSIZE) {
        nxm_flag = 1;
        check_apr_irq();
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
        watch_stop = 1;
    sim_interval--;
    MB = M[addr];
    last_addr = addr;
    modify = mod;
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    UPDATE_MI(addr);
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_write_bbn(int flag, int cur_context) {
    t_addr addr;

    /* If not doing any special access, just access register */
    if (AB < 020 && ((xct_flag == 0 || cur_context || (FLAGS & USER) != 0))) {
        set_reg(AB, MB);
        UPDATE_MI(AB);
        return 0;
    }
    if (modify) {
        if (sim_brk_summ && sim_brk_test(last_addr, SWMASK('W')))
            watch_stop = 1;
        M[last_addr] = MB;
        UPDATE_MI(AB);
        modify = 0;
        return 0;
    }
    if (!page_lookup_bbn(AB, flag, &addr, 1, cur_context, 0))
        return 1;
    if (addr < 020) {
        set_reg(AB, MB);
        UPDATE_MI(AB);
        return 0;
    }
    if (addr >= MEMSIZE) {
        nxm_flag = 1;
        check_apr_irq();
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
        watch_stop = 1;
    sim_interval--;
    M[addr] = MB;
    UPDATE_MI(addr);
    return 0;
}
#endif

#if WAITS
int page_lookup_waits(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch) {
    int      uf = (FLAGS & USER) != 0;

    /* If this is modify instruction use write access */
    wr |= modify;

    if (adr_cond && addr == AS)
        address_conditions (fetch, wr);

    /* Figure out if this is a user space access */
    if (flag)
        uf = 0;
    else if (xct_flag != 0 && !fetch) {
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
        check_apr_irq();
        return 0;
    } else {
        *loc = addr;
    }
    return 1;
}

int Mem_read_waits(int flag, int cur_context, int fetch, int mod) {
    t_addr addr;

    if (AB < 020 && ((xct_flag == 0 || fetch || cur_context || (FLAGS & USER) != 0))) {
        MB = get_reg(AB);
        if (fetch == 0 && hst_lnt) {
            hst[hst_p].mb = MB;
        }
        UPDATE_MI(addr);
        return 0;
    }
    if (!page_lookup_waits(AB, flag, &addr, mod, cur_context, fetch))
        return 1;
    if (addr >= MEMSIZE) {
        nxm_flag = 1;
        check_apr_irq();
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
        watch_stop = 1;
    sim_interval--;
    MB = M[addr];
    modify = mod;
    last_addr = addr;
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    UPDATE_MI(addr);
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */

int Mem_write_waits(int flag, int cur_context) {
    t_addr addr;

    /* If not doing any special access, just access register */
    if (AB < 020 && ((xct_flag == 0 || cur_context || (FLAGS & USER) != 0))) {
        set_reg(AB, MB);
        UPDATE_MI(AB);
        return 0;
    }
    if (modify) {
        if (sim_brk_summ && sim_brk_test(last_addr, SWMASK('W')))
            watch_stop = 1;
        M[last_addr] = MB;
        modify = 0;
        UPDATE_MI(AB);
        return 0;
    }
    if (!page_lookup_waits(AB, flag, &addr, 1, cur_context, 0))
        return 1;
    if (addr >= MEMSIZE) {
        nxm_flag = 1;
        check_apr_irq();
        return 1;
    }
    if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
        watch_stop = 1;
    sim_interval--;
    M[addr] = MB;
    UPDATE_MI(addr);
    return 0;
}
#endif

int page_lookup_ka(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch) {
      if (adr_cond && addr == AS)
        address_conditions (fetch, wr);

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
          check_apr_irq();
          return 0;
      } else {
          *loc = addr;
      }
      return 1;
}

int Mem_read_ka(int flag, int cur_context, int fetch, int mod) {
    t_addr addr = AB;

    if (AB < 020) {
        MB = get_reg(AB);
    } else {
        if (!page_lookup_ka(AB, flag, &addr, mod, cur_context, fetch))
            return 1;
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        sim_interval--;
        MB = M[addr];
    }
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    UPDATE_MI(addr);
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */

int Mem_write_ka(int flag, int cur_context) {
    t_addr addr = AB;

    if (AB < 020) {
        set_reg(AB, MB);
    } else {
        if (!page_lookup_ka(AB, flag, &addr, 1, cur_context, 0))
            return 1;
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        sim_interval--;
        M[addr] = MB;
    }
    UPDATE_MI(addr);
    return 0;
}

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
        res |= ((uint64)push_ovf << 16);
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

int page_lookup(t_addr addr, int flag, t_addr *loc, int wr, int cur_context, int fetch) {
      if (!flag && (FLAGS & USER) != 0) {
          if (addr <= Pl) {
             *loc = (addr + Rl) & RMASK;
             return 1;
          }
          mem_prot = 1;
          check_apr_irq();
          return 0;
      } else {
         *loc = addr;
      }
      return 1;
}

int Mem_read(int flag, int cur_context, int fetch, int mod) {
    t_addr addr = AB;

    sim_interval--;
    if (AB < 020) {
        MB = get_reg(AB);
    } else {
        if (!page_lookup(AB, flag, &addr, 0, cur_context, fetch))
            return 1;
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('R')))
            watch_stop = 1;
        MB = M[addr];
    }
    if (fetch == 0 && hst_lnt) {
        hst[hst_p].mb = MB;
    }
    UPDATE_MI(addr);
    return 0;
}

/*
 * Write a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */

int Mem_write(int flag, int cur_context) {
    t_addr addr = AB;

    sim_interval--;
    if (AB < 020) {
        set_reg(AB, MB);
    } else {
        if (!page_lookup(AB, flag, &addr, 1, cur_context, 0))
            return 1;
        if (addr >= MEMSIZE) {
            nxm_flag = 1;
            check_apr_irq();
            return 1;
        }
        if (sim_brk_summ && sim_brk_test(AB, SWMASK('W')))
            watch_stop = 1;
        M[addr] = MB;
    }
    UPDATE_MI(addr);
    return 0;
}
#endif

/*
 * Read a location directly from memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_read_nopage() {
#if KA | KI
    if (adr_cond && AB == AS)
        address_conditions (0, 0);
#endif
    if (AB >= MEMSIZE) {
#if KL | KS
        irq_flags |= NXM_MEM;
#else
        nxm_flag = 1;
#endif
        check_apr_irq();
        return 1;
    }
    sim_interval--;
    MB = M[AB];
    UPDATE_MI(AB);
    return 0;
}

/*
 * Write a directly to a location in memory.
 *
 * Return of 0 if successful, 1 if there was an error.
 */
int Mem_write_nopage() {
#if KA | KI
    if (adr_cond && AB == AS)
        address_conditions (0, 1);
#endif
    if (AB >= MEMSIZE) {
#if KL | KS
        irq_flags |= NXM_MEM;
#else
        nxm_flag = 1;
#endif
        check_apr_irq();
        return 1;
    }
    sim_interval--;
    M[AB] = MB;
    UPDATE_MI(AB);
    return 0;
}

/*
 * Access main memory. Returns 0 if access ok, 1 if out of memory range.
 * On KI10 and KL10, optional EPT flag indicates address relative to ept.
 */
int Mem_read_word(t_addr addr, uint64 *data, int ept)
{
#if KL | KI | KS
    if (ept)
       addr += eb_ptr;
#endif
    if (addr >= MEMSIZE)
        return 1;
    *data = M[addr];
    return 0;
}

int Mem_write_word(t_addr addr, uint64 *data, int ept)
{
#if KL | KI | KS
    if (ept)
       addr += eb_ptr;
#endif
    if (addr >= MEMSIZE)
        return 1;
    M[addr] = *data;
    return 0;
}


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
int     pi_rq;                   /* Interrupt request */
int     pi_ov;                   /* Overflow during PI cycle */
int     ind;                     /* Indirect bit */
int     ix;                      /* Index register */
int     f_load_pc;               /* Load AB from PC at start of instruction */
int     f_inst_fetch;            /* Fetch new instruction */
int     f_pc_inh;                /* Inhibit PC increment after instruction */
int     nrf;                     /* Normalize flag */
int     fxu_hold_set;            /* Negitive exponent */
int     f;                       /* Temporary variables */
int     flag1;
int     flag3;
int     instr_count = 0;         /* Number of instructions to execute */
t_addr  IA;                      /* Initial address of first fetch */
#if ITS | KL_ITS | KS_ITS
char    one_p_arm = 0;           /* One proceed arm */
#endif

if (sim_step != 0) {
    instr_count = sim_step;
    sim_cancel_step();
}

RUN = 1;
prog_stop = 0;
#if KS
reason = SCPE_OK;
#else
/* Build device table */
if ((reason = build_dev_tab ()) != SCPE_OK)            /* build, chk dib_tab */
    return reason;
#endif

/* Main instruction fetch/decode loop: check clock queue, intr, trap, bkpt */
   f_load_pc = 1;
   f_inst_fetch = 1;
   ind = 0;
   uuo_cycle = 0;
   pi_cycle = 0;
   pi_rq = 0;
   pi_ov = 0;
   BYF5 = 0;
#if KI | KL | KS
   page_fault = 0;
#if KL | KS
   ptr_flg = 0;
#endif
#endif
#if ITS
   if (QITS) {
       one_p_arm = 0;
       set_quantum();
   }
#endif
#if KL_ITS | KS_ITS
   if (QITS)
       one_p_arm = 0;
#endif
   watch_stop = 0;

   while ( reason == 0) {                                /* loop until ABORT */
      AIO_CHECK_EVENT;                                   /* queue async events */
      if (sim_interval <= 0) {                           /* check clock queue */
         if ((reason = sim_process_event()) != SCPE_OK) {/* error?  stop sim */
#if ITS
             if (QITS)
                 load_quantum();
#endif
             RUN = 0;
             return reason;
         }
    }

    if (sim_brk_summ && f_load_pc && sim_brk_test(PC, SWMASK('E'))) {
         reason = STOP_IBKPT;
         RUN = 0;
         break;
    }

    if (watch_stop) {
         reason = STOP_IBKPT;
         RUN = 0;
         break;
    }

#if PIDP10
    if (examine_sw) {   /* Examine memory switch */
        AB = AS;
        (void)Mem_read_nopage();
        examine_sw = 0;
    }
    if (deposit_sw) {   /* Deposit memory switch */
        AB = AS;
        MB = SW;
        (void)Mem_write_nopage();
        deposit_sw = 0;
    }
    if (xct_sw) {    /* Handle Front panel xct switch */
        modify = 0;
        xct_flag = 0;
        uuo_cycle = 1;
        f_pc_inh = 1;
        f_load_pc = 0;
        MB = SW;
        goto no_fetch;
    }
    if (stop_sw) {    /* Stop switch set */
        RUN = 0;
        stop_sw = 0;
        reason = STOP_HALT;
    }
    if (sing_inst_sw) {  /* Handle Front panel single instruction */
        instr_count = 1;
    }
#endif


#if MAGIC_SWITCH
    if (!MAGIC) {
         reason = STOP_MAGIC;
         RUN = 0;
         break;
    }
#endif /* MAGIC_SWITCH */

    /* Normal instruction */
    if (f_load_pc) {
        modify = 0;
        xct_flag = 0;
#if KI | KL | KS
        trap_flag = 0;
#endif
#if KL | KS
        extend = 0;
        ptr_flg = 0;
#endif
#if KL
        sect = cur_sect = pc_sect;
        glb_sect = 0;
#endif
        AB = PC;
        uuo_cycle = 0;
        f_pc_inh = 0;
    }

    if (f_inst_fetch) {
#if !(KI|KL)
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
#if KS
        if (fe_xct != 0) {
           AB = (t_addr)fe_xct;
           fe_xct = 0;
        }
#if KS_ITS
        if (QITS && pi_cycle == 0 && (FLAGS & ADRFLT) != 0) {
           one_p_arm = 1;
           FLAGS &= ~ADRFLT;
        }
#endif
#endif
        if (Mem_read(pi_cycle | uuo_cycle, 1, 1, 0)) {
#if KA | PDP6
            pi_rq = check_irq_level();
            if (pi_rq)
               goto st_pi;
#endif
#if KL
            /* Handling for PUBLIC violation */
            if (((fault_data >> 30) & 037) == 021)
               PC = (PC + 1) & RMASK;
#endif
            goto last;
        }

no_fetch:
       IR = (MB >> 27) & 0777;
       AC = (MB >> 23) & 017;
       AD = MB;  /* Save for historical sake */
       IA = AB;
#if KL
       glb_sect = 0;
#endif
       BYF5 = 0;
    }

#if KI | KL | KS
    /* Handle page fault and traps */
    if (page_enable && trap_flag == 0 && (FLAGS & (TRP1|TRP2))) {
        if (FLAGS & ADRFLT) {
#if KL_ITS
            if (QITS && (FLAGS & (TRP1|TRP2|ADRFLT)) == (TRP1|TRP2|ADRFLT))
               one_p_arm = 1;
#endif
            FLAGS &= ~ADRFLT;
        } else {
            AB = 0420 + ((FLAGS & (TRP1|TRP2)) >> 2);
            trap_flag = FLAGS & (TRP1|TRP2);
            FLAGS &= ~(TRP1|TRP2);
            pi_cycle = 1;
            AB += (FLAGS & USER) ? ub_ptr : eb_ptr;
            Mem_read_nopage();
            goto no_fetch;
        }
    }
#endif

#if KL
    /* If we are doing a PXCT with E1 or E2 set, change section */
    if (QKLB && t20_page) {
        if (xct_flag != 0) {
            if (((xct_flag & 8) != 0 && !ptr_flg) ||
                ((xct_flag & 2) != 0 && ptr_flg))
            sect = cur_sect = prev_sect;
#if 1
         /* The following lines are needed to run Tops 20 V3 on KL/B */
         if (((xct_flag & 014) == 04 && !ptr_flg && prev_sect == 0)
             || ((xct_flag & 03)  == 01 && ptr_flg  && prev_sect == 0))
             sect = cur_sect = prev_sect;
#endif
        }
    }
#endif

    /* Handle indirection repeat until no longer indirect */
    do {
        ind = TST_IND(MB) != 0;
        AR = MB;
        AB = MB & RMASK;
        ix = GET_XR(MB);
#if PIDP10
        IX = ix;   /* Save these in variable so display can show them */
        IND = ind;
#endif
        if (ix) {
#if KL | KS
             if (((xct_flag & 8) != 0 && !ptr_flg) ||
                 ((xct_flag & 2) != 0 && ptr_flg))
                AR = FM[prev_ctx|ix];
             else
                AR = get_reg(ix);
#if KL
             /* Check if extended indexing */
             if (QKLB && t20_page && cur_sect != 0 && (AR & SMASK) == 0 && (AR & SECTM) != 0) {
                 AR = (AR + ((AB & RSIGN) ? SECTM|((uint64)AB): (uint64)AB)) & (SECTM|RMASK);
                 sect = cur_sect = (AR >> 18) & 07777;
                 glb_sect = 1;
                 AB = 0;
             } else
                 glb_sect = 0;
#endif
             /* For KL and KS */
             AR = MB = (AB + AR) & FMASK;
#else
             /* For KA & KI & PDP6 */
             AR = MB = (AB + get_reg(ix)) & FMASK;
#endif
#if KS
             if (ind == 0 && (IR & 0700) == 0700)
                 AB = MB & (IOCTL|RMASK);
             else
#endif
             AB = MB & RMASK;
         }
#if KL
in_loop:
#endif
         if (ind & !pi_rq) {
             if (Mem_read(pi_cycle | uuo_cycle, 1, 0, 0))
                 goto last;
#if KL
             /* Check if extended indexing */
             if (QKLB && t20_page && (cur_sect != 0 || glb_sect)) {
                 if (MB & SMASK || cur_sect == 0) {    /* Instruction format IFIW */
                     if (MB & BIT1 && cur_sect != 0) { /* Illegal index word */
                         fault_data = 024LL << 30 | (((FLAGS & USER) != 0)?SMASK:0) |
                                      BIT8 | (AB & RMASK) | ((uint64)cur_sect << 18);
                         page_fault = 1;
                         goto last;
                     }
                     ind = TST_IND(MB) != 0;
                     ix = GET_XR(MB);
                     AB = MB & RMASK;
                     if (ix) {
                         if (((xct_flag & 8) != 0 && !ptr_flg) ||
                             ((xct_flag & 2) != 0 && ptr_flg))
                             AR = FM[prev_ctx|ix];
                         else
                             AR = get_reg(ix);
                         /* Check if extended indexing */
                         if (cur_sect == 0 || (AR & SMASK) != 0 || (AR & SECTM) == 0) {
                              /* Local index word */
                              AR = (AR + AB) & RMASK;
                              glb_sect = 0;
                         } else {
                              AR = (AR + AB) & FMASK;
                              glb_sect = 1;
                              sect = cur_sect = (AR >> 18) & 07777;
                         }
                         MB = AR;
                     } else {
                         glb_sect = 0;
                         if ((MB & RMASK) < 020)
                            sect = cur_sect = 1;
                         AR = MB;
                     }
                     AB = AR & RMASK;
                 } else {             /* Extended index EFIW */
                     ind = (MB & BIT1) != 0;
                     ix = (MB >> 30) & 017;
                     AB = MB & (SECTM|RMASK);
                     if (ix) {
                         if (((xct_flag & 8) != 0 && !ptr_flg) ||
                             ((xct_flag & 2) != 0 && ptr_flg))
                            AR = FM[prev_ctx|ix];
                         else
                            AR = get_reg(ix);
                         if ((AR & SMASK) != 0 || (AR & SECTM) == 0) { /* Local index word */
                              AR = AB + (AR & RMASK);
                         } else
                              AR = (AR & ~(SECTM|RMASK)) |
                                     ((AR + AB) & (SECTM|RMASK));
                         AR &= FMASK;
                         MB = AR;
                     } else
                         AR = MB;
                     sect = cur_sect = (AR >> 18) & 07777;
                     AB = AR & RMASK;
                     glb_sect = 1;
                 }
                 if (ind)
                     goto in_loop;
             }
#endif
#if KS
#if KS_ITS
             /* ITS I/O instruction are standard */
             /* Instructions don't do repeat indirect */
             if (!QITS && (IR & 0700) == 0700) {
#else
             /* I/O Instructions don't do repeat indirect */
             if ((IR & 0700) == 0700) {
#endif
                 AR = MB & (IOCTL|RMASK);
                 AB = (t_addr)AR;
                 ind = 0;
             }
#endif
         }
         /* Handle events during a indirect loop */
         AIO_CHECK_EVENT;                                   /* queue async events */
         if (--sim_interval <= 0) {
              if ((reason = sim_process_event()) != SCPE_OK) {
                  return reason;
              }
         }

         if ((!pi_cycle) & pi_pending
#if KI | KL | KS
                         & (!trap_flag)
#endif
                          ) {
            pi_rq = check_irq_level();
         }
    } while (ind & !pi_rq);

    /* If there is a interrupt handle it. */
    if (pi_rq) {
#if KI | KL | KS
        int  pi_mask = (0200 >> pi_enc);
#endif
#if KA | PDP6
st_pi:
#endif
#if DEBUG
        sim_debug(DEBUG_IRQ, &cpu_dev, "trap irq %o %03o %03o \n",
                       pi_enc, PIR, PIH);
#endif
        pi_cycle = 1;
        pi_rq = 0;
        pi_hold = 0;
        pi_ov = 0;
        AB = 040 | (pi_enc << 1) | maoff;
        xct_flag = 0;
#if KS
        AB |= eb_ptr;
        extend = 0;
        if ((dev_irq[0] & pi_mask) == 0) {
            for (f = 1; f < MAX_DEV; f++) {
                if (dev_irq[f] & pi_mask) {
                    AB = uba_get_vect(AB, pi_mask, f);
                    clr_interrupt(f << 2);
                    break;
                }
            }
#if DEBUG
            sim_debug(DEBUG_IRQ, &cpu_dev, "vect irq %o %06o\n", pi_enc, AB);
#endif
        }
#if KS_ITS
        pi_act |= pi_mask;
#endif
        pi_vect = AB;
        goto fetch;
#endif
#if KI | KL
        /*
         * Scan through the devices and allow KI devices to have first
         * hit at a given level.
         */
        for (f = 0; f < MAX_DEV; f++) {
            if (dev_irqv[f] != 0 && dev_irq[f] & pi_mask) {
                AB = dev_irqv[f](f << 2, AB);
#if DEBUG
                sim_debug(DEBUG_IRQ, &cpu_dev, "vect irq %o %03o %06o\n",
                         pi_enc, dev_irq[f], AB);
#endif
                break;
            }
        }
        if (AB & RSIGN)
            AB &= 0777;
        else
            AB |= eb_ptr;
#if KL
        sect = cur_sect = 0;
        extend = 0;
#endif
        pi_vect = AB;
        Mem_read_nopage();
        goto no_fetch;
#elif PDP6 | KA
        pi_vect = AB;
        goto fetch;
#endif
    }


#if KI | KL | KS
    if (page_enable && page_fault) {
        if (!f_pc_inh && !pi_cycle)
            PC = (PC + 1) & RMASK;
        goto last;
    }
#endif

    /* Check if possible idle loop */
    if (sim_idle_enab &&
          ((PC < 020 && AB < 020 && (IR & 0740) == 0340) ||
           (uuo_cycle && (IR & 0740) == 0 && IA == 041))) {
       sim_idle (TMR_RTC, FALSE);
    }

    /* Update history */
    if (hst_lnt) {
            if (PC != 017)
                hst_p = hst_p + 1;
            if (hst_p >= hst_lnt) {
                hst_p = 0;
            }
            hst[hst_p].pc = HIST_PC | ((BYF5)? (HIST_PC2|PC) : IA);
            hst[hst_p].ea = AB;
#if KL | KS
            if (extend)
               hst[hst_p].pc |= HIST_PCE;
#endif
#if KL
            hst[hst_p].pc |= (pc_sect << 18);
            hst[hst_p].ea |= (sect << 18);
#endif
            hst[hst_p].ir = AD;
            hst[hst_p].flags = (FLAGS << 5)
#if KA | KI | PDP6
                                |(clk_flg << 2) | (nxm_flag << 1)
#if KA | PDP6
                                | (mem_prot << 4) | (push_ovf << 3)
#endif
#if PDP6
                                | ill_op
#endif
#endif
#if KL | KS
                                | (fm_sel >> 4)
#endif

                       ;
#if KL
            hst[hst_p].prev_sect = prev_sect;
#endif
            hst[hst_p].mb = AR;
            hst[hst_p].ac = get_reg(AC);
    }


    /* Set up to execute instruction */
    f_inst_fetch = 1;
    f_load_pc = 1;
    nrf = 0;
    fxu_hold_set = 0;
    modify = 0;
#if PIDP10
    if (xct_sw) {    /* Handle Front panel xct switch */
        xct_sw = 0;
    } else
#endif
    f_pc_inh = 0;
#if KL | KS
    if (extend) {
       if (IR == 0 || IR > 031 || AC != 0 || do_extend(IA)) {
           IR = 0123;
           AC = ext_ac;
           goto muuo;
       }
       goto last;
    }
#endif
    BR = get_reg(AC);

    /* Process the instruction */
    switch (IR) {
#if KL
    case 0052:  /* PMOVE */
    case 0053:  /* PMOVEM */
         if (QKLB && t20_page && (FLAGS & USER) == 0) {
             if (Mem_read(0, 0, 0, 0))
                goto last;
             AB = MB & (SECTM|RMASK);
             if (IR & 1) {
                 MB = get_reg(AC);
                 if (Mem_write_nopage())
                     goto last;
             } else {
                 if (Mem_read_nopage())
                     goto last;
                 set_reg(AC, MB);
             }
             break;
          }
          /* Fall through */
#else
    case 0052: case 0053:
          /* Fall through */
#endif
muuo:
    case 0000: /* UUO */
    case 0040: case 0041: case 0042: case 0043:
    case 0044: case 0045: case 0046: case 0047:
    case 0050: case 0051:
    case 0054: case 0055: case 0056: case 0057:
    case 0060: case 0061: case 0062: case 0063:
    case 0064: case 0065: case 0066: case 0067:
    case 0070: case 0071: case 0072: case 0073:
#if !KL_ITS
    case 0074: case 0075: case 0076: case 0077:
#endif

              /* MUUO */

#if KI | KL | KS
    case 0100:  /* UJEN */
    case 0101:
#if !KS_ITS
    case 0102: case 0103:
#endif
    case 0104:  /* JSYS */
    case 0106:
    case 0107:
#if !(KL_ITS | KS_ITS)
    case 0247: /* UUO  */
#endif
unasign:
              /* Save Opcode */
              modify = 0;
#if KL
              if (QKLB && t20_page) {
                  AR = (uint64)AB; /* Save address */
                  if (pc_sect != 0) {
                      if (glb_sect == 0 && AB < 020)
                          AR |= BIT17;
                      else
                          AR |= ((uint64)cur_sect) << 18;
                  }
                  MB = (((uint64)((IR << 9) | (AC << 5))) | ((uint64)(FLAGS) << 23)) & FMASK;
                  if ((FLAGS & USER) == 0) {
                      MB &= ~SMASK;
                      MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
                      MB |= (uint64)(prev_sect);
                  }
              } else
#elif KS
              if (t20_page) {
                  AR = (uint64)AB; /* Save address */
                  MB = (((uint64)((IR << 9) | (AC << 5))) | ((uint64)(FLAGS) << 23)) & FMASK;
              } else
#endif
              MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | (uint64)(AB);
              AB = ub_ptr + 0424;
#if KL
              /* If single sections KL10 UUO starts at 425 */
              if (!QKLB && !QITS && t20_page)
                 AB = AB + 1;
#endif
              Mem_write_nopage();
              /* Save flags */
              AB++;
#if KL
              if (QKLB && t20_page)
                  MB = ((uint64)(pc_sect) << 18) | ((PC + (trap_flag == 0)) & RMASK);
              else {
                  MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + (trap_flag == 0)) & RMASK);
                  /* Save public setting */
                  if ((FLAGS & USER) == 0) {
                      MB &= ~SMASK;
                      MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
                  }
              }
#elif KS
              if (t20_page)
                  MB = (PC + (trap_flag == 0)) & RMASK;
              else
                  MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + (trap_flag == 0)) & RMASK);
#else
              MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + (trap_flag == 0)) & RMASK);
#if KI
              /* Save public setting */
              if ((FLAGS & USER) == 0) {
                  MB &= ~SMASK;
                  MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
              }
#endif
#endif
              Mem_write_nopage();
#if KL
              extend = 0;
              if (QKLB && t20_page) { /* Restore address */
                  if (pc_sect != 0 && glb_sect == 0 && AR < 020)
                      AR |= BIT17;
                  else
                      AR |= ((uint64)cur_sect) << 18;
                  MB = AR;
                  AB ++;
                  Mem_write_nopage();
              }
              /* Save context */
              AB ++;
              MB = SMASK|
                   ((uint64)(fm_sel & 0160) << 23) |
                   ((uint64)(prev_ctx & 0160) << 20) |
                   (ub_ptr >> 9);
              if (QKLB && t20_page) {
                 MB |= BIT1|((uint64)(prev_sect & 037) << 18);
                 prev_sect = pc_sect & 037;
              }
              Mem_write_nopage();
#elif KS
              extend = 0;
              if (t20_page) { /* Restore address */
                  MB = AR;
                  AB ++;
                  Mem_write_nopage();
                  /* Save context */
                  AB ++;
                  MB = SMASK|
                       ((uint64)(fm_sel & 0160) << 23) |
                       ((uint64)(prev_ctx & 0160) << 20) |
                       (ub_ptr >> 9);
                  Mem_write_nopage();
#if KS_ITS
              } else if (QITS) {
                  /* Save context */
                  AB ++;
                  MB = SMASK|BIT2|
                       ((uint64)(fm_sel & 0160) << 23) |
                       ((uint64)(prev_ctx & 0160) << 20) |
                       (ub_ptr & 03777777);
                  Mem_write_nopage();
#endif
              }
#endif
              /* Read in new PC and flags */
              FLAGS &= ~ (PRV_PUB|BYTI|ADRFLT|TRP1|TRP2);
              AB = ub_ptr + 0430;
              if (trap_flag != 0)
                  AB |= 1;
#if !KS
              if (FLAGS & PUBLIC)
                  AB |= 2;
#endif
              if (FLAGS & USER)
                  AB |= 4;
              Mem_read_nopage();

#if KL
              if (QKLB && t20_page) {
                  pc_sect = (MB >> 18) & 00037;
                  FLAGS = 0;
              } else
#endif
              FLAGS = (MB >> 23) & 017777;
              /* If transistioning from user to executive adjust flags */
              if ((FLAGS & USER) == 0) {
                  if ((AB & 4) != 0)
                      FLAGS |= USERIO;
#if !KS
                  if ((AB & 2 || (FLAGS & OVR) != 0))
                      FLAGS |= PRV_PUB|OVR;
#endif
              }
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
#if KL
              /* LUUO's in non-zero section are different */
              if (QKLB && t20_page && pc_sect != 0) {
                  /* Save Effective address */
                  if (pc_sect != 0 && glb_sect == 0 && AR < 020)
                      AR = BIT17;
                  else
                      AR = ((uint64)cur_sect) << 18;
                  AR |= AB; /* Save address */
                  /* Grab address of LUUO block from user base 420 */
                  AB = ((FLAGS & USER) ? ub_ptr : eb_ptr) + 0420;
                  Mem_read_nopage();
                  /* Now save like MUUO */
                  AB = MB & (SECTM|RMASK);
                  MB = (((uint64)((IR << 9) | (AC << 5))) | ((uint64)(FLAGS) << 23)) & FMASK;
                  if ((FLAGS & USER) == 0) {
                      MB &= ~SMASK;
                      MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
                  }
                  Mem_write_nopage();
                  /* Save PC */
                  AB++;
                  MB = ((uint64)(pc_sect) << 18) | ((PC + (trap_flag == 0)) & RMASK);
                  Mem_write_nopage();
                  MB = AR;
                  AB ++;
                  Mem_write_nopage();
                  AB ++;
                  /* Read PC */
                  Mem_read_nopage();
                  pc_sect = (MB >> 18) & 07777;
                  PC = MB & RMASK;
                  f_pc_inh = 1;
                  break;
              }
#endif
#if PDP6
              ill_op = 1;
              ex_uuo_sync = 1;
#endif
              MB = ((uint64)(IR) << 27) | ((uint64)(AC) << 23) | (uint64)(AB);
#if KI
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
#if KL_ITS
    case 0074:     /* XCTR */
    case 0075:     /* XCTRI */
              if (QITS && (FLAGS & USER) == 0) {
                   f_load_pc = 0;
                   f_pc_inh = 1;
                   xct_flag = AC;
                   break;
              }
              goto unasign;

    case 0076:     /* LMR */
              if (QITS && (FLAGS & USER) == 0) {
                  /* Load store ITS pager info */
                  if ((AB + 8) >= MEMSIZE) {
                     break;
                  }
                  MB = M[AB];                /* WD 0 */
                  jpc = (MB & RMASK);
                  AB = (AB + 1) & RMASK;
                  MB = M[AB];                /* WD 1 */
                  brk_addr = MB & RMASK;
                  brk_flags = 017 & (MB >> 23);
                  AB = (AB + 1) & RMASK;
                  MB = M[AB];                /* WD 2 */
                  FM[(6<<4)|0] = MB;
                  AB = (AB + 1) & RMASK;
                  MB = M[AB];                /* WD 3 */
                  dbr1 = MB;
                  AB = (AB + 1) & RMASK;
                  MB = M[AB];                /* WD 4 */
                  dbr2 = MB;
                  for (f = 0; f < 512; f++)
                      u_tlb[f] = 0;
                  break;
              }
              goto unasign;
    case 0077:     /* SPM */
              if (QITS && (FLAGS & USER) == 0) {
                  if ((AB + 8) >= MEMSIZE) {
                     break;
                  }
                  MB = (uint64)jpc;
                  M[AB] = MB;                 /* WD 0 */
                  AB = (AB + 1) & RMASK;
                  MB = (uint64)brk_addr;
                  MB |= ((uint64)brk_flags) << 23;
                  M[AB] = MB;                 /* WD 1 */
                  AB = (AB + 1) & RMASK;
                  MB = FM[(6<<4)|0];
                  M[AB] = MB;                 /* WD 2 */
                  AB = (AB + 1) & RMASK;
                  MB = dbr1;
                  M[AB] = MB;                 /* WD 3 */
                  AB = (AB + 1) & RMASK;
                  MB = dbr2;
                  M[AB] = MB;                 /* WD 4 */
                  break;
              }
              goto unasign;
#endif
#if KS_ITS
    case 0102: /* XCTI */
    case 0103: /* XCT */
              if (QITS && (FLAGS & USER) == 0) {
                   f_load_pc = 0;
                   f_pc_inh = 1;
                   xct_flag = AC;
                   break;
              }
              goto unasign;
#endif


#if KI | KL | KS
#if KL | KS
    case 0105:       /* ADJSP */
              AR &= RMASK;
#if KL
              if (QKLB && t20_page && pc_sect != 0 && (BR & SMASK) == 0 && (BR & SECTM) != 0) {
                  AD = (((AR & RSIGN)?(LMASK|AR):AR) + BR) & (SECTM|RMASK);
                  AD |= BR & ~(SECTM|RMASK);
              } else {
#endif
                  AD = (BR + AR) & RMASK;
                  AD |= (BR & LMASK) + ((AR << 18) & LMASK);
#if KL
                  if (QKLB && pc_sect == 0 && ((BR ^ AD) & SMASK) != 0)
                      FLAGS |= TRP2;
              }
#endif
              AR = AD & FMASK;
              set_reg(AC, AR);
              break;
#endif

    case 0110:       /* DFAD */
    case 0111:       /* DFSB */
              /* On Load AR,MQ has memory operand */
              /* AR,MQ = AC  BR,MB  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              BR = AR;
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);

              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0, 0))
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
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              BR = AR;
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0, 0))
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
#if KL | KS
                  while (((AR & (FPSBIT|FPNBIT)) == (FPSBIT|FPNBIT)) ||
                      ((AR & (FPSBIT|FPNBIT)) == 0)) {
#else
                  if (((AR & (FPSBIT|FPNBIT)) == (FPSBIT|FPNBIT)) ||
                      ((AR & (FPSBIT|FPNBIT)) == 0)) {
#endif
                      SC --;
                      AR <<= 1;
                      if (MQ & BIT1)
                         AR |= 1;
                      MQ <<= 1;
                      MQ &= FMASK;
                      nrf = 1;
                  }
#if KL | KS
                  /* Handle special minus case */
                  if (AR == (FPHBIT|FPSBIT)) {
                      SC += 1;
                      if (AR & 1)
                          MQ |= SMASK;
                      MQ >>= 1;
                      AR = (AR & FPHBIT) | (AR >> 1);
                  }
#endif
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
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              BR = AR;
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0, 0))
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
#if KL | KS
              /* One extra bit for KL */
              AR <<= 1;
              BR <<= 1;
#endif
              /* Make both positive */
              flag1 = 0;
              if (AR & FPHBIT) {
                  AR = (FPFMASK ^ AR) + 1;
                  flag1 = 1;
              }
              if (BR & FPHBIT) {
                  BR = (FPFMASK ^ BR) + 1;
                  flag1 = !flag1;
              }
              /* Precheck if divide ok */
              if (AR >= (BR << 1)) {
                  if (!pi_cycle)
                      FLAGS |= OVR|FLTOVR|NODIV|TRP1;
                  AR = 0;      /* For clean history */
                  break;
              }
              /* Divide by zero */
              if (AR == 0)  {
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
              for (FE = 0; FE < (62 + KL + KS); FE++) {
                  AD <<= 1;
                  if (AR >= BR) {
                     AR = AR - BR;
                     AD |= 1;
                  }
                  AR <<= 1;
              }
              AR = AD;
              /* Fix sign of result */
              if (flag1) {
                  AR = (AR ^ FPFMASK) + 1;
              }
#if KL | KS
                else
                   AR++;  /* Round on KL & KS */
              AR = (AR & FPHBIT) | (AR >> 1); /* Remove extra bit */
#endif
              /* Check potential overflow */
              if (((SC & 0400) != 0) ^ ((SC & 0200) != 0) || SC == 0600)
                  fxu_hold_set = 1;
              /* Normalize */
              while (((AR & FPHBIT) != 0) != ((AR & FPSBIT) != 0)) {
                  SC += 1;
                  AR = (AR & FPHBIT) | (AR >> 1);
              }
              /* Extract halfs from 64bit word */
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
              /* Add in exponent */
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR &= SMASK|MMASK;
              if (AR != 0 || MQ != 0)
                  AR |= ((uint64)(SCAD & 0377)) << 27;

              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

#if KL | KS
    case 0114: /* DADD */
              flag1 = flag3 = 0;
              /* AR,ARX = AC  BR,BX  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BR = MB;
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BRX = MB;
              AR = get_reg(AC);
              ARX = get_reg(AC + 1);
              /* Add numbers */
              ARX = (ARX & CMASK) + (BRX & CMASK);
              f = (ARX & SMASK) != 0;
              if (((AR & CMASK) + (BR & CMASK) + f) & SMASK) {
                  FLAGS |= CRY1;
                  flag1 = 1;
              }
              AR = AR + BR + f;
              if (AR & C1) {
                  if (!pi_cycle)
                      FLAGS |= CRY0;
                  flag3 = 1;
              }
              AR &= FMASK;
              if (flag1 != flag3) {
                  if (!pi_cycle)
                      FLAGS |= OVR|TRP1;
              }
              ARX &= CMASK;
              ARX |= AR & SMASK;
              set_reg(AC, AR);
              set_reg(AC+1, ARX);
              break;

    case 0115: /* DSUB */
              flag1 = flag3 = 0;
              /* AR,AX = AC  BR,BX  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BR = MB;
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BRX = MB;
              AR = get_reg(AC);
              ARX = get_reg(AC + 1);
              /* Add numbers */
              ARX = (ARX & CMASK) + CCM(BRX) + 1;
              f = (ARX & SMASK) != 0;
              if (((AR & CMASK) + CCM(BR) + f) & SMASK) {
                  FLAGS |= CRY1;
                  flag1 = 1;
              }
              AR = AR + CM(BR) + f;
              if (AR & C1) {
                  if (!pi_cycle)
                      FLAGS |= CRY0;
                  flag3 = 1;
              }
              AR &= FMASK;
              if (flag1 != flag3) {
                  if (!pi_cycle)
                      FLAGS |= OVR|TRP1;
              }
              ARX &= CMASK;
              ARX |= AR & SMASK;
              set_reg(AC, AR);
              set_reg(AC+1, ARX);
              break;

    case 0116: /* DMUL */
              flag1 = flag3 = 0;
              /* AR,ARX = AC  BR,BRX  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BR = MB;
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BRX = MB;
              AR = get_reg(AC);
              ARX = get_reg(AC + 1);
              /* Make BR,BRX positive */
              if (BR & SMASK) {
                  /* Low */
                  BRX = CCM(BRX) + 1;   /* Low */
                  /* High */
                  BR = (CM(BR) + ((BRX & SMASK) != 0)) & FMASK;
                  flag1 = 1;
                  /* Can only occur if 2**-70 */
                  if (BR & SMASK)
#if KL
                      FLAGS |= OVR|TRP1;
#else
                      flag3 = 1;
#endif
              }
              /* Make AR,ARX positive */
              if (AR & SMASK) {
                  /* Low */
                  ARX = CCM(ARX) + 1;   /* Low */
                  /* High */
                  AR = (CM(AR) + ((ARX & SMASK) != 0)) & FMASK;
                  flag1 ^= 1;
                  /* Can only occur if 2**-70 */
                  if (AR & SMASK)
#if KL
                      FLAGS |= OVR|TRP1;
#else
                      flag3 |= 2;
#endif
              }
              /* Form product in AD,ADX,BR,BX */
              AD = ADX = 0;
              BRX &= CMASK; /* Clear sign of BX */
              ARX &= CMASK;
              /* Compute product */
              for (SC = 70; SC >= 0; SC--) {
                  /* Shift MQ,MB,BR,BX right one */
                  f = (BRX & 1);
                  if (BR & 1)
                      BRX |= SMASK;
                  if (ADX & 1)
                      BR |= SMASK;
                  if (AD & 1)
                      ADX |= SMASK;
                  BRX >>= 1;
                  BR >>= 1;
                  ADX >>= 1;
                  AD >>= 1;
                  if (f) { /* Add AR,ARX to AD,ADX */
                     ADX = ADX + ARX;
                     AD = AD + AR + ((ADX & SMASK) != 0);
                     ADX &= CMASK;
                  }
              }
              /* If minus, negate whole thing */
              if (flag1) {
                   BRX = CCM(BRX) + 1;   /* Low */
                   BR = CCM(BR) + ((BRX & SMASK) != 0);
                   ADX = CCM(ADX) + ((BR & SMASK) != 0);
                   AD = CM(AD) + ((ADX & SMASK) != 0);
              }
#if KS
              if (flag3 == 3)
                 FLAGS |= OVR|TRP1;
#endif
              /* Copy signs */
              BRX &= CMASK;
              BR &= CMASK;
              ADX &= CMASK;
              AD &= FMASK;
              BRX |= AD & SMASK;
              BR |= AD & SMASK;
              ADX |= AD & SMASK;
              /* Save results */
              set_reg(AC, AD);
              set_reg(AC+1, ADX);
              set_reg(AC+2, BR);
              set_reg(AC+3, BRX);
              break;

    case 0117: /* DDIV */
              flag1 = flag3 = 0;
              /* AR,ARX = AC  BR,BRX  = mem */
                    /* AR High */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BR = MB;
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BRX = MB;
              /* Make BR,BX positive */
              if (BR & SMASK) {
                  /* Low */
                  BRX = CCM(BRX) + 1;   /* Low */
                  /* High */
                  BR = (CM(BR) + ((BRX & SMASK) != 0)) & FMASK;
                  flag1 = 1;
                  /* Can only occur if 2**-70 */
                  if (BR & SMASK) {
                      FLAGS |= OVR|TRP1;
                  }
              }
              if ((BR | BRX) == 0) {
#if KL
                  FLAGS |= NODIV;
#else
                  FLAGS |= NODIV|OVR|TRP1;
#endif
                  break;
              }
              /* Get dividend */
              AR = get_reg(AC);
              ARX = get_reg(AC + 1);
              MB = get_reg(AC + 2);
              MQ = get_reg(AC + 3);

              /* Make MQ,MB,AR,ARX positive */
              if (AR & SMASK) {
                  /* Low */
                  MQ = CCM(MQ) + 1;
                  MB = CCM(MB) + ((MQ & SMASK) != 0);
                  ARX = CCM(ARX) + ((MB & SMASK) != 0);
                  AR = (CM(AR) + ((ARX & SMASK) != 0)) & FMASK;
                  flag1 ^= 1;
                  flag3 = 1;
                  if (AR & SMASK) {
                      FLAGS |= OVR|TRP1;
                  }
              }
              MQ &= CMASK;
              MB &= CMASK;
              ARX &= CMASK;
              /* Precheck divide ok */
              ADX = ARX + CCM(BRX) + 1;
              AD = AR + CM(BR) + ((ADX & SMASK) != 0);
              if ((AD & C1) != 0) {
                   FLAGS |= OVR|TRP1|NODIV;
                   break;
              }
              /* Do divide */
              for (SC = 70; SC > 0; SC--) {
                  AR <<= 1;
                  ARX <<= 1;
                  MB <<= 1;
                  MQ <<= 1;
                  if (ARX & SMASK)
                     AR |= 1;
                  if (MB & SMASK)
                     ARX |= 1;
                  if (MQ & SMASK)
                     MB |= 1;
                  ARX &= CMASK;
                  MB &= CMASK;
                  MQ &= CMASK;
                  ADX = ARX + CCM(BRX) + 1;
                  AD = AR + CM(BR) + ((ADX & SMASK) != 0);
                  if ((AD & SMASK) == 0) {
                     ARX = ADX;
                     AR = AD & CMASK;
                     MQ |= 1;
                  }
              }
              BRX &= CMASK; /* Clear sign of BX */
              ARX &= CMASK;
              /* Set sign of quotent */
              if (flag1) {
                   MQ = CCM(MQ) + 1;
                   MB = CM(MB) + ((MQ & SMASK) != 0);
                   MQ &= CMASK;
                   MB &= FMASK;
              }
              /* Set sign or remainder */
              if (flag3) {
                   ARX = CCM(ARX) + 1;   /* Low */
                   AR = CM(AR) + ((ARX & SMASK) != 0);
                   ARX &= CMASK;
                   AR &= FMASK;
              }
              MQ |= MB & SMASK;
              ARX |= AR & SMASK;
              /* Save results */
              set_reg(AC, MB);
              set_reg(AC+1, MQ);
              set_reg(AC+2, AR);
              set_reg(AC+3, ARX);
              break;

#else
    case 0114: /* DADD */
    case 0115: /* DSUB */
    case 0116: /* DMUL */
    case 0117: /* DDIV */
              goto unasign;
#endif

    case 0120: /* DMOVE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AB = (AB + 1) & RMASK;
#if KI
              FLAGS |= BYTI;
#endif
              if (Mem_read(0, 0, 0, 0))
                   goto last;
              FLAGS &= ~BYTI;
              MQ = MB;
              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0121: /* DMOVN */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AB = (AB + 1) & RMASK;
#if KI
              FLAGS |= BYTI;
#endif
              if (Mem_read(0, 0, 0, 0))
                   goto last;
              FLAGS &= ~BYTI;
              MQ = CCM(MB) + 1;   /* Low */
              /* High */
#if KL | KS
              flag1 = flag3 = 0;
              if ((CCM(AR) + ((MQ & SMASK) != 0)) & SMASK) {
                  FLAGS |= CRY1;
                  flag1 = 1;
              }
#endif
              AR = (CM(AR) + ((MQ & SMASK) != 0));
              MQ &= CMASK;
#if KL | KS
              if (AR & C1) {
                  FLAGS |= CRY0;
                  flag3 = 1;
              }
              if (flag1 != flag3 && !pi_cycle) {
                  FLAGS |= OVR|TRP1;
              }
              if ((AR == SMASK && MQ == 0) && !pi_cycle)
                  FLAGS |= TRP1;
#endif
              AR &= FMASK;
              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0123:  /* Extend */
#if KL | KS
#if KS_ITS
              if (QITS) {
                  goto unasign;
              }
#endif
              /* Handle like xct */
              f_load_pc = 0;
              f_pc_inh = 1;
              extend = 1;
              ext_ac = AC;
              BR = AB;    /* Save address of instruction */
              if (Mem_read(0, 1, 0, 0))
                  goto last;
              goto no_fetch;
#else
              goto unasign;
#endif

    case 0124: /* DMOVEM */
              MQ = get_reg(AC + 1);
#if KS
              if ((FLAGS & BYTI) == 0) {
                  IA = AB;
                  AB = (AB + 1) & RMASK;
                  MB = MQ;
                  if (Mem_write(0, 0))
                      goto last;
                  AB = IA;
                  FLAGS |= BYTI;
              }
              if ((FLAGS & BYTI)) {
                  MB = BR;
                  if (Mem_write(0, 0))
                      goto last;
                  FLAGS &= ~BYTI;
              }
#else
              /* Handle each half as seperate instruction */
              if ((FLAGS & BYTI) == 0) {
                  MB = BR;
                  if (Mem_write(0, 0))
                      goto last;
                  FLAGS |= BYTI;
              }
              if ((FLAGS & BYTI)) {
                  AB = (AB + 1) & RMASK;
                  MB = MQ;
                  if (Mem_write(0, 0))
                     goto last;
                  FLAGS &= ~BYTI;
              }
#endif
              break;

    case 0125: /* DMOVNM */
              AR = get_reg(AC);
              MQ = get_reg(AC + 1);
              /* Handle each half as seperate instruction */
              if ((FLAGS & BYTI) == 0) {
                  BR = AR = CM(AR);
                  BR = BR + 1;
                  MQ = CCM(MQ) + 1;
                  if (MQ & SMASK) {
#if KL | KS
                     flag1 = flag3 = 0;
                     if ((CCM(get_reg(AC)) + 1) & SMASK) {
                         FLAGS |= CRY1;
                         flag1 = 1;
                     }
#endif
                     AR = BR;
#if KL | KS
                     if (AR & C1) {
                         FLAGS |= CRY0;
                         flag3 = 1;
                     }
                     if (flag1 != flag3 && !pi_cycle) {
                         FLAGS |= OVR|TRP1;
                     }
                     if ((AR == SMASK && MQ == 0) && !pi_cycle)
                         FLAGS |= TRP1;
#endif
                  }
#if KS
                  IA = AB;
                  AB = (AB + 1) & RMASK;
                  if (Mem_read(0, 0, 0, 1))
                      goto last;
                  AB = IA;
                  modify = 0;
#endif
                  AR &= FMASK;
                  MB = AR;
                  if (Mem_write(0, 0))
                      goto last;
                  FLAGS |= BYTI;
#if KL | KS
                  AB = (AB + 1) & RMASK;
                  MB = MQ & CMASK;
                  if (Mem_write(0, 0))
                     goto last;
                  FLAGS &= ~BYTI;
                  break;
#endif
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
              if (Mem_read(0, 0, 0, 0))
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
                             ( MQ  != 0)))
                       AR ++;
              } else {
                  if (!pi_cycle)
                      FLAGS |= OVR|TRP1;        /* OV & T1 */
                  break;
              }
              set_reg(AC, AR & FMASK);
              break;

    case 0127: /* FLTR */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR <<= 27;
              if (AR & FPSBIT) {
                  flag1 = 1;
                  AR |= FPHBIT;
              } else
                  flag1 = 0;
              SC = 162;
              goto fnorm;
#else
    case 0100: /* TENEX UMOVE */
#if BBN
              if (QBBN) {
                   if (Mem_read(0, 0, 0, 0)) {
                      IR = 0;
                      goto last;
                   }
                   AR = MB;
                   set_reg(AC, AR);
                   IR = 0;
                   break;
              }
#endif
              goto unasign;
    case 0101: /* TENEX UMOVEI */
#if BBN
              if (QBBN) {
                   set_reg(AC, AR);
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
                      if ((AB + 8) >= MEMSIZE) {
                         fault_data |= 0400;
                         mem_prot = 1;
                         check_apr_irq();
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
                      if ((AB + 8) >= MEMSIZE) {
                         fault_data |= 0400;
                         mem_prot = 1;
                         check_apr_irq();
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
                      check_apr_irq();
                  }
                  /* AC & 2 = Clear TLB */
                  if (AC & 2) {
                      for (f = 0; f < 512; f++)
                         e_tlb[f] = u_tlb[f] = 0;
                      mem_prot = 0;
                      check_apr_irq();
                  }
                  /* AC & 4 = Set Prot Interrupt */
                  if (AC & 4) {
                      mem_prot = 1;
                      check_apr_irq();
                  }
                  break;
              }
#endif
#if BBN
              if (QBBN) {
                   AR = get_reg(AC);
                   MB = AR;
                   if (Mem_write(0, 0)) {
                      goto last;
                   }
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
                   if (Mem_read(0, 0, 0, 1)) {
                       goto last;
                   }
                   AR = MB;
                   if (Mem_write(0, 0)) {
                      goto last;
                   }
                   if (AC != 0)
                       set_reg(AC, AR);
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
                   if (Mem_read(0, 0, 0, 0)) {
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
#if ITS | KL_ITS | KS_ITS
              if (QITS) {
                  BR = AR & RMASK;
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
                  AR &= RMASK;
                  BR = get_reg(AC);
                  if (hst_lnt) {
                      hst[hst_p].mb = AR;
                  }
                  MQ = 0;
                  AR = AR << 18;  /* Move to upper half */
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
#if KS_ITS
              if (QITS && one_p_arm) {
                  FLAGS |= ADRFLT;
                  one_p_arm = 0;
              }
#endif
              f_load_pc = 0;
#endif
              break;
#endif

    case 0133: /* IBP/ADJBP */
#if KL | KS
              if (AC != 0) { /* ADJBP */
                  if (Mem_read(0, 0, 0, 0))
                      goto last;
                  AR = MB;
                  SC = (AR >> 24) & 077;   /* S */
                  FE = (AR >> 30) & 077;   /* P */
#if KL
                  if (SC || (QKLB && t20_page && FE > 36)) {
#else
                  if (SC) {
#endif
                      int  bpw, left, newb, adjw, adjb;

                      f = 0;
#if KL
                      if (QKLB && t20_page && FE > 36) {
                          if (FE == 077)
                              goto muuo;
                          f = 1;
                          SC = _byte_adj[(FE - 37)].s;
                          FE = _byte_adj[(FE - 37)].p;
                      }
#endif
                      left = (36 - FE) / SC;  /* Number bytes left (36 - P)/S */
                      bpw = left + (FE / SC); /* Bytes per word */
                      if (bpw == 0) {
                          FLAGS |= OVR|NODIV|TRP1;
                          break;
                      }
                      BR = get_reg(AC);        /* Grab amount to move */
                      /* Make BR into native integer */
                      if (BR & RSIGN)
                          adjw = -((int)(CM(BR) + 1) & RMASK);
                      else
                          adjw = (int)(BR & RMASK);
                      newb = adjw + left;      /* Adjust to word boundry */
                      adjw = newb / bpw;          /* Number of words to move */
                      adjb = (newb >= 0)? newb % bpw: -((-newb) % bpw);
                      if (adjb <= 0) {
                         adjb += bpw;           /* Move forward */
                         adjw--;
                      }
                      FE = 36 - (adjb * SC) - ((36 - FE) % SC);   /* New P */
#if KL
                      if (f) {
                          /* Short pointer */
                          for (f = 0; f < 28; f++) {
                             if (_byte_adj[f].s == SC && _byte_adj[f].p == FE) {
                                 FE = f + 37;
                                 break;
                             }
                          }
                          AR = (((uint64)(FE & 077)) << 30) |    /* Make new BP */
                                ((AR + adjw) & (SECTM|RMASK));
                          set_reg(AC, AR);
                          break;
                      } else if (QKLB && t20_page && pc_sect != 0 && (AR & BIT12) != 0) {
                          /* Full pointer */
                          AB = (AB + 1) & RMASK;
                          if (Mem_read(0, 0, 0, 0))
                              goto last;
                          AR = (((uint64)(FE & 077)) << 30) |    /* Make new BP */
                                          (AR & PMASK);  /* S and below */
                          if (MB & SMASK) {
                              if (MB & BIT1) {
                                  fault_data = 024LL << 30 | (((FLAGS & USER) != 0)?SMASK:0) |
                                                (AB & RMASK) | ((uint64)cur_sect << 18);
                                  page_fault = 1;
                                  goto last;
                              }
                              BR = ((MB + adjw) & RMASK) | (MB & LMASK);
                          } else
                              BR = ((MB + adjw) & (SECTM|RMASK)) | (MB & ~(SECTM|RMASK));
                          set_reg(AC, AR);
                          set_reg(AC+1, BR);
                          break;
                      }
#endif
                      AR = (((uint64)(FE & 077)) << 30) |    /* Make new BP */
                           (AR & PMASK & LMASK) |  /* S,IX,I */
                           ((AR + adjw) & RMASK);
                  }
                  set_reg(AC, AR);
                  break;
              }
#endif
    case 0134: /* ILDB */
    case 0136: /* IDPB */
              if ((FLAGS & BYTI) == 0) {      /* BYF6 */
#if KL | KS
                  if (Mem_read(0, 0, 0, 1)) {
#elif KI
                  if (Mem_read(0, 1, 0, 1)) {
#else
                  if (Mem_read(0, !QITS, 0, 1)) {
#endif
#if PDP6
                      FLAGS |= BYTI;
#endif
                      goto last;
                  }
                  AR = MB;
                  SCAD = (AR >> 30) & 077;
#if KL
                  if (QKLB && t20_page && SCAD > 36) {  /* Extended pointer */
                      f = SCAD - 37;
                      if (SCAD == 077)
                          goto muuo;
                      SC = _byte_adj[f].s;
                      SCAD = (_byte_adj[f].p + (0777 ^ SC) + 1) & 0777;
                      f++;
                      if (SCAD & 0400) {
                          SCAD = ((0777 ^ SC) + 044 + 1) & 0777;
                          AR++;
                          for(f = 0; f < 28; f++) {
                             if (_byte_adj[f].s == SC && _byte_adj[f].p == SCAD)
                                 break;
                          }
                      }
                      AR &= (SECTM|RMASK);
                      AR |= ((uint64)(f + 37)) << 30;
                      MB = AR;
                      if (Mem_write(0, 0))
                          goto last;
                      if ((IR & 04) == 0)
                          break;
                      goto ld_ptr;
                  }
#endif
                  SC = (AR >> 24) & 077;
                  SCAD = (SCAD + (0777 ^ SC) + 1) & 0777;
                  if (SCAD & 0400) {
                      SCAD = ((0777 ^ SC) + 044 + 1) & 0777;
#if KL
                      if (QKLB && t20_page && pc_sect != 0 && (AR & BIT12) != 0) { /* Full pointer */
                          AB = (AB + 1) & RMASK;
                          if (Mem_read(0, 0, 0, 0))
                              goto last;
                          if (MB & SMASK) {
                              if (MB & BIT1) {
                                  fault_data = 024LL << 30 | (((FLAGS & USER) != 0)?SMASK:0) |
                                                (AB & RMASK) | ((uint64)cur_sect << 18);
                                  page_fault = 1;
                                  goto last;
                              }
                              MB = ((MB + 1) & RMASK) | (MB & LMASK);
                          } else
                              MB = ((MB + 1) & (SECTM|RMASK)) | (MB & ~(SECTM|RMASK));
                          if (Mem_write(0,0))
                              goto last;
                          AB = (AB - 1) & RMASK;
                          AR &= PMASK;
                          AR |= (uint64)(SCAD & 077) << 30;
                          MB = AR;
                          if (Mem_write(0, 0))
                              goto last;
                          if ((IR & 04) == 0)
                              break;
                          goto ldb_ptr;
                      } else
                          AR = (AR & LMASK) | ((AR + 1) & RMASK);
#elif KI | KS
                      AR = (AR & LMASK) | ((AR + 1) & RMASK);
#else
                      AR = (AR + 1) & FMASK;
#endif
                  }
                  AR &= PMASK;
                  AR |= (uint64)(SCAD & 077) << 30;
                  MB = AR;
#if KL | KS
                  if (Mem_write(0, 0))
#elif KI
                  if (Mem_write(0, 1))
#else
                  if (Mem_write(0, !QITS))
#endif
                      goto last;
                  if ((IR & 04) == 0)
                      break;
                  goto ldb_ptr;
              }
              /* Fall through */

    case 0135:/* LDB */
    case 0137:/* DPB */
              if ((FLAGS & BYTI) == 0 || !BYF5) {
#if KL | KS
                  if (Mem_read(0, 0, 0, 0))
#elif KI
                  if (Mem_read(0, 1, 0, 0))
#else
                  if (Mem_read(0, !QITS, 0, 0))
#endif
                      goto last;
                  AR = MB;
                  SC = (AR >> 24) & 077;
                  SCAD = (AR >> 30) & 077;
#if KL
                  if (QKLB && t20_page && SCAD > 36) {   /* Extended pointer */
                      f = SCAD - 37;
                      if (SCAD == 077)
                          goto muuo;
                      SC = _byte_adj[f].s;
                      SCAD = _byte_adj[f].p;
ld_ptr:
                      glb_sect = 1;
                      sect = (AR >> 18) & 07777;
                      FLAGS |= BYTI;
                      BYF5 = 1;
                      goto ld_exe;
                  }
#endif
ldb_ptr:
                  f_load_pc = 0;
                  f_inst_fetch = 0;
                  f_pc_inh = 1;
#if KL_ITS
                  if (QITS && one_p_arm) {
                      FLAGS |= ADRFLT;
                      one_p_arm = 0;
                  }
#endif
                  FLAGS |= BYTI;
                  BYF5 = 1;
#if KL | KS
                  ptr_flg = 1;
#endif
#if KL
                  if (QKLB && t20_page && (SC < 36) &&
                       pc_sect != 0 && (AR & BIT12) != 0) {
                      /* Full pointer */
                      AB = (AB + 1) & RMASK;
                      ind = 1;
                      goto in_loop;
                  } else
                      glb_sect = 0;
#endif
#if ITS
                  if (QITS && pi_cycle == 0 && mem_prot == 0) {
                     opc = PC | (FLAGS << 18);
                  }
#endif
              } else {
#if KL
ld_exe:
#endif
#if KL | KS
                  ptr_flg = 0;
#endif
                  f = 0;
#if !KS
#if KL
                  if (!QKLB && (IR & 06) == 6)
#else
                  if ((IR & 06) == 6)
#endif
                      f = 1;
#endif
                  AB = AR & RMASK;
                  MQ = (uint64)(1) << SC;
                  MQ -= 1;
                  if (Mem_read(0, 0, 0, f))
                      goto last;
                  AR = MB;
                  if ((IR & 06) == 4) {
                      AR = AR >> SCAD;
                      AR &= MQ;
                      set_reg(AC, AR);
                  } else {
                      BR = get_reg(AC);
                      BR = BR << SCAD;
                      MQ = MQ << SCAD;
                      AR &= CM(MQ);
                      AR |= BR & MQ;
                      MB = AR & FMASK;
                      if (Mem_write(0, 0))
                          goto last;
                  }
                  FLAGS &= ~BYTI;
                  BYF5 = 0;
              }
              break;

    case 0131:/* DFN FCE|FAC */

#if !PDP6
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BR = MB;
              AR = get_reg(AC);
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

    case 0132:/* FSC FAC|SAC */
              BR = AR & RMASK;
              AR = get_reg(AC);
              SC = ((AB & RSIGN) ? 0400 : 0) | (AB & 0377);
              SCAD = GET_EXPO(AR);
#if KL | KS
              SC |= (SC & 0400) ? 0777000 : 0;
              SCAD |= (SC & 0400) ? 0777000 : 0;
              SC = SCAD + SC;
#else
              SC = (SCAD + SC) & 0777;
#endif
              flag1 = 0;
              if (AR & SMASK)
                 flag1 = 1;
#if KS
              if (((SC & 0400) != 0) ^ ((SC & 0200) != 0))
                  fxu_hold_set = 1;
#endif
#if PDP6
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
              set_reg(AC, AR);
              break;
#else
              SMEAR_SIGN(AR);
              AR <<= 34;
              goto fnorm;
#endif


    case 0150:  /* FSB */   /* SAC|FCE|FBR */
    case 0151:  /* FSBL */  /* SAC|SAC2|FCE|FBR */
    case 0152:  /* FSBM */  /* FCEPSE|FBR */
    case 0153:  /* FSBB */  /* SAC|FBR|FCEPSE */
    case 0154:  /* FSBR */  /* SAC|FCE|FBR */
    case 0155:  /* FSBRI, FSBRL on PDP6 */ /* SAC|SWAR|FBR  SAC|SAC2|FCE|FBR */
    case 0156:  /* FSBRM */ /* FBR|FCEPSE */
    case 0157:  /* FSBRB */ /* SAC|FBR|FCEPSE */

              switch (IR & 07) {
              case 5:
#if !PDP6
                      AR &= RMASK;
                      AR = SWAP_AR;
                      break;
#endif
              case 0:
              case 1:
              case 4:
                      if (Mem_read(0, 0, 0, 0))
                          goto last;
                      AR = MB;
                      break;
              case 2:
              case 3:
              case 6:
              case 7:
                      if (Mem_read(0, 0, 0, 1))
                          goto last;
                      AR = MB;
                      break;
              }

              AD = (CM(AR) + 1) & FMASK;
              AR = BR;
              BR = AD;
              goto fadd;
              /* Fall through */

#if !PDP6
    case 0130:  /* UFA */  /* FCE|FBR */
#endif
#if WAITS
ufa:
#endif
    case 0140:  /* FAD */ /* SAC|FCE|FBR */
    case 0141:  /* FADL */ /* SAC|SAC2|FCE|FBR */
    case 0142:  /* FADM */ /* FCEPSE|FBR */
    case 0143:  /* FADB */ /* SAC|FBR|FCEPSE */
    case 0144:  /* FADR */ /* SAC|FCE|FBR */
    case 0145:  /* FADRI FADRL on PDP6 */ /* SAC|SWAR|FBR  SAC|SAC2|FCE|FBR */
    case 0146:  /* FADRM*/ /* FBR|FCEPSE */
    case 0147:  /* FADRB*/ /* SAC|FBR|FCEPSE */
              switch (IR & 07) {
              case 5:
#if !PDP6
                      AR &= RMASK;
                      AR = SWAP_AR;
                      break;
#endif
              case 0:
              case 1:
              case 4:
                      if (Mem_read(0, 0, 0, 0))
                          goto last;
                      AR = MB;
                      break;
              case 2:
              case 3:
              case 6:
              case 7:
                      if (Mem_read(0, 0, 0, 1))
                          goto last;
                      AR = MB;
                      break;
              }

fadd:
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
#if KL | KS
              SC |= (SC & 0400) ? 0777000 : 0;   /* Extend sign */
#endif
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
#if !(KL | KS)
                  if (((SC & 0400) != 0) ^ ((SC & 0200) != 0))
                       fxu_hold_set = 1;
#endif
                  if (IR != 0130 && IR != 0247) {   /* !UFA and WAITS FIX */
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
                          SC ++;
                          AR = (AR & FPHBIT) | (AR >> 1);
                      }
                      if (!nrf && ((IR & 04) != 0)) {
                          f = (AR & FP1BIT) != 0;
                          if ((AR & FPRBIT2) != 0) {
#if !PDP6
                              /* FADR & FSBR do not round if negative and equal round */
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
#if KL | KS
              if (!pi_cycle && (SC & 0400) != 0)  {
                  FLAGS |= OVR|FLTOVR|TRP1;
                  if ((SC & RSIGN) != 0)
                      FLAGS |= FLTUND;
              }
#else
              if (((SC & 0400) != 0) && !pi_cycle) {
                  FLAGS |= OVR|FLTOVR|TRP1;
#if !PDP6
                  if (!fxu_hold_set) {
                      FLAGS |= FLTUND;
                      MQ = 0;
                  }
#endif
#if PDP6 | KA
                  check_apr_irq();
#endif
              }
#endif
#if WAITS
              /* WAITS FIX Instruction. This can't occur if WAITS not set */
              if (IR == 0247) {
                  /* Extend sign if negative */
                  if (flag1)
                     AR |= EMASK;
                  set_reg(AC, AR);
                  break;
              }
#endif

              /* Set exponent */
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
              /* Kill exponent if 0 */
              if ((AR & MMASK) == 0)
                 AR = 0;

              /* Handle UFA */
              if (IR == 0130) {
                  set_reg(AC + 1, AR);
                  break;
              }
              if (IR == 0127 || IR == 0132) { /* FLTR */
                  set_reg(AC, AR & FMASK);
                  break;
              }
              switch (IR & 07) {
#if PDP6
              case 5:
#endif
              case 1:
                      set_reg(AC + 1, MQ & FMASK);
                      /* Fall through */
#if !PDP6
              case 5:
#endif
              case 0:
              case 4:
                      set_reg(AC, AR & FMASK);
                      break;
              case 2:
              case 6:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      break;
              case 3:
              case 7:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      set_reg(AC, AR & FMASK);
                      break;
              }
              break;

    case 0160:      /* FMP */ /* SAC|FCE|FBR */
    case 0161:      /* FMPL */ /* SAC|SAC2|FCE|FBR */
    case 0162:      /* FMPM */ /* FCEPSE|FBR */
    case 0163:      /* FMPB */ /* SAC|FBR|FCEPSE */
    case 0164:      /* FMPR */ /* SAC|FCE|FBR */
    case 0165:      /* FMPRI FMPRL on PDP6 */ /* SAC|SWAR|FBR  SAC|SAC2|FCE|FBR */
    case 0166:      /* FMPRM */ /* FBR|FCEPSE */
    case 0167:      /* FMPRB */ /* SAC|FBR|FCEPSE */
              switch (IR & 07) {
              case 5:
#if !PDP6
                      AR &= RMASK;
                      AR = SWAP_AR;
                      break;
#endif
              case 0:
              case 1:
              case 4:
                      if (Mem_read(0, 0, 0, 0))
                          goto last;
                      AR = MB;
                      break;
              case 2:
              case 3:
              case 6:
              case 7:
                      if (Mem_read(0, 0, 0, 1))
                          goto last;
                      AR = MB;
                      break;
              }

              /* Compute exponent */
              SC = (((BR & SMASK) ? 0777 : 0) ^ (BR >> 27)) & 0777;
              SCAD = (((AR & SMASK) ? 0777 : 0) ^ (AR >> 27)) & 0777;
#if KL | KS
              SC |= (SC & 0400) ? 0777000 : 0;       /* Extend sign */
              SCAD |= (SCAD & 0400) ? 0777000 : 0;   /* Extend sign */
              SC = (SC + SCAD + (RMASK ^ 0200) + 1) & RMASK;
#else
              SC = (SC + SCAD + 0600) & 0777;
#endif
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

    case 0170:      /* FDV */ /* SAC|FCE|FBR */
    case 0172:      /* FDVM */ /* FCEPSE|FBR */
    case 0173:      /* FDVB */ /* SAC|FCEPSE|FBR */
    case 0174:      /* FDVR */ /* SAC|FBR|FCE */
#if !PDP6
    case 0175:      /* FDVR FDVL on PDP6 */ /* SAC|SWAR|FBR */
#endif
    case 0176:      /* FDVRM*/ /* FBR|FCEPSE */
    case 0177:      /* FDVRB */ /* SAC|FBR|FCEPSE */
              switch (IR & 07) {
              case 5:
#if !PDP6
                      AR &= RMASK;
                      AR = SWAP_AR;
                      break;
#endif
              case 0:
              case 1:
              case 4:
                      if (Mem_read(0, 0, 0, 0))
                          goto last;
                      AR = MB;
                      break;
              case 2:
              case 3:
              case 6:
              case 7:
                      if (Mem_read(0, 0, 0, 1))
                          goto last;
                      AR = MB;
                      break;
              }

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
              }
              if (AR & SMASK) {
                  if ((AR & MMASK) == 0) {
                      AR = BIT9;
                      SC--;
                  } else
                      AR = CM(AR) + 1;
                  flag1 = !flag1;
              }
#if KL | KS
              SC |= (SC & 0400) ? 0777000 : 0;       /* Extend sign */
              SCAD |= (SCAD & 0400) ? 0777000 : 0;   /* Extend sign */
              SC = (SC + ((RMASK ^ SCAD) + 1) + 0201) & RMASK;
#else
              SC = (SC + ((0777 ^ SCAD) + 1) + 0201) & 0777;
#endif
              /* Clear exponents */
              AR &= MMASK;
              BR &= MMASK;
              /* Check if we need to fix things */
              if (BR >= (AR << 1)) {
                  if (!pi_cycle) {
                      FLAGS |= OVR|NODIV|FLTOVR|TRP1;
#if PDP6 | KA
                      check_apr_irq();
#endif
                  }
                  break;      /* Done */
              }
              BR = (BR << 28);
              MB = AR;
              AR = BR / AR;
              if (AR != 0) {
#if KL | KS
                  /* KL and KS code */
                  if (flag1) {
                     AR = ((AR ^ FMASK) + 1) & FMASK;
                  }
                  AR = (AR >> 1) | (AR & SMASK);
                  if (IR & 04) {
                      AR++;
                      flag3 = AR & 1;
                  }
                  AR = (AR >> 1) | (AR & SMASK);
                  while (AR != 0 && ((AR & SMASK) != 0) == ((AR & BIT8) != 0) &&
                             ((AR & BIT8) != 0) == ((AR & BIT9) != 0)) {
                      AR <<= 1;
                      AR |= flag3;
                      flag3 = 0;
                      SC--;
                  }
                  AR &= FMASK;
                  if ((SC & 01600) != 01600)
                      fxu_hold_set = 1;
                  if (AR == (SMASK|EXPO)) {
                      AR = (AR >> 1) | (AR & SMASK);
                      SC ++;
                  }
                  AR &= SMASK|MMASK;
#elif KA | KI
                  /* KA and KI code */
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
#elif PDP6
                  /* PDP6 code */
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
              if (!pi_cycle && (SC & 0400) != 0)  {
                  FLAGS |= OVR|FLTOVR;
#if KL | KS
                  if ((SC & RSIGN) != 0)
#else
                  if (!fxu_hold_set)
#endif
                      FLAGS |= FLTUND;
#if PDP6 | KA
                  check_apr_irq();
#endif
              }
#if !(PDP6 | KL | KS)
              if (flag1)  {
                 AR = ((AR ^ MMASK) + 1) & MMASK;
                 AR |= SMASK;
              }
#endif
              SCAD = SC ^ ((AR & SMASK) ? 0377 : 0);
              AR |= ((uint64)(SCAD & 0377)) << 27;
              switch (IR & 07) {
#if PDP6
              case 5:
#endif
              case 1:
                      set_reg(AC + 1, MQ & FMASK);
                      /* Fall through */
#if !PDP6
              case 5:
#endif
              case 0:
              case 4:
                      set_reg(AC, AR & FMASK);
                      break;
              case 2:
              case 6:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      break;
              case 3:
              case 7:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      set_reg(AC, AR & FMASK);
                      break;
              }
              break;

    case 0171:      /* FDVL */ /* SAC|SAC2|FAC2|FCE|FBR */
#if KS
              goto muuo;
#elif PDP6
    case 0175:      /* FDVRL */ /* SAC|SAC2|FAC2||FCE|FBR */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              MQ = get_reg(AC + 1);

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
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              MQ = get_reg(AC + 1);

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
                  if (!pi_cycle) {
                      FLAGS |= OVR|NODIV|FLTOVR|TRP1;
#if KA
                      check_apr_irq();
#endif
                  }
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
#if KA
                  check_apr_irq();
#endif
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
              set_reg(AC + 1, MQ & FMASK);
              set_reg(AC, AR & FMASK);
              break;

                   /* FWT */
    case 0200:     /* MOVE */   /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0)) {
                  goto last;
              }
              AR = MB;
              set_reg(AC, AR);
              break;

    case 0201:     /* MOVEI */  /* SAC */
              AR &= RMASK;
              set_reg(AC, AR);
              break;

    case 0202:     /* MOVEM */  /* FAC|SCE */
              MB = BR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0203:     /* MOVES */  /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1)) {
                  goto last;
              }
              AR = MB;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0204:     /* MOVS */   /* SWAR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0)) {
                  goto last;
              }
              AR = MB;
              AR = SWAP_AR;
              set_reg(AC, AR);
              break;

    case 0205:     /* MOVSI */  /* SWAR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              set_reg(AC, AR);
              break;

    case 0206:     /* MOVSM */  /* SWAR|FAC|SCE */
              AR = get_reg(AC);
              AR = SWAP_AR;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0207:     /* MOVSS */  /* SWAR|SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1)) {
                  goto last;
              }
              AR = MB;
              AR = SWAP_AR;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

#if !PDP6
#define NEG   flag1 = flag3 = 0; \
              AD = CM(AR) + 1; \
              if ((CCM(AR) + 1) & SMASK) { \
                  FLAGS |= CRY1; \
                  flag1 = 1; \
              } \
              if (AD & C1) { \
                  FLAGS |= CRY0; \
                  flag3 = 1; \
              } \
              if (flag1 != flag3 && !pi_cycle) { \
                  FLAGS |= OVR|TRP1; \
              } \
              AR = AD & FMASK;
#else
#define NEG   flag1 = flag3 = 0; \
              AD = CM(AR) + 1; \
              if ((CCM(AR) + 1) & SMASK) { \
                  flag1 = 1; \
              } \
              if (AD & C1) { \
                  flag3 = 1; \
              } \
              if (flag1 != flag3 && !pi_cycle) { \
                  FLAGS |= OVR|TRP1; \
              } \
              AR = AD & FMASK;
#endif

    case 0214:     /* MOVM */  /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              if ((AR & SMASK) != 0) {
                     NEG;
#if PDP6 | KA
                   if (flag1 != flag3 && !pi_cycle)
                       check_apr_irq();
#endif
#if KI | KL | KS
                   if (AR == SMASK && !pi_cycle)
                       FLAGS |= TRP1;
#endif
              }
              set_reg(AC, AR);
              break;

    case 0215:     /* MOVMI */ /* SAC */
              AR &= RMASK;
              set_reg(AC, AR);
              break;

    case 0216:     /* MOVMM */ /* FAC|SCE */
              AR = get_reg(AC);
              if ((AR & SMASK) != 0) {
                       NEG;
#if PDP6 | KA
                   if (flag1 != flag3 && !pi_cycle)
                     check_apr_irq();
#endif
#if KI | KL | KS
                   if (AR == SMASK && !pi_cycle)
                       FLAGS |= TRP1;
#endif
              }
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0217:     /* MOVMS */ /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              if ((AR & SMASK) != 0) {
                   NEG;
#if PDP6 | KA
                   if (flag1 != flag3 && !pi_cycle)
                     check_apr_irq();
#endif
#if KI | KL | KS
                   if (AR == SMASK && !pi_cycle)
                       FLAGS |= TRP1;
#endif
              }
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0210:     /* MOVN */   /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              NEG;
#if PDP6 | KA
              if (flag1 != flag3 && !pi_cycle)
                check_apr_irq();
#endif
#if KI | KL | KS
              if (AR == SMASK && !pi_cycle)
                  FLAGS |= TRP1;
#endif
              set_reg(AC, AR);
              break;

    case 0211:     /* MOVNI */  /* SAC */
              AR &= RMASK;
              NEG;
#if PDP6 | KA
              if (flag1 != flag3 && !pi_cycle)
                check_apr_irq();
#endif
#if KI | KL | KS
              if (AR == SMASK && !pi_cycle)
                  FLAGS |= TRP1;
#endif
              set_reg(AC, AR);
              break;

    case 0212:     /* MOVNM */  /* SCE|FAC */
              AR = get_reg(AC);
              NEG;
#if PDP6 | KA
              if (flag1 != flag3 && !pi_cycle)
                check_apr_irq();
#endif
#if KI | KL | KS
              if (AR == SMASK && !pi_cycle)
                  FLAGS |= TRP1;
#endif
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0213:     /* MOVNS */  /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              NEG;
#if PDP6 | KA
              if (flag1 != flag3 && !pi_cycle)
                check_apr_irq();
#endif
#if KI | KL | KS
              if (AR == SMASK && !pi_cycle)
                  FLAGS |= TRP1;
#endif
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0220:      /* IMUL */   /* SAC|FCE|FBR */
    case 0221:      /* IMULI */  /* SAC|FBR */
    case 0222:      /* IMULM */  /* FCEPSE|FBR */
    case 0223:      /* IMULB */  /* SAC|FCEPSE|FBR */
    case 0224:      /* MUL */    /* SAC2|SAC|FCE|FBR */
    case 0225:      /* MULI */   /* SAC2|SAC|FBR */
    case 0226:      /* MULM */   /* FCEPSE|FBR */
    case 0227:      /* MULB */   /* SAC2|SAC|FCEPSR|FBR */
              switch (IR & 07) {
              case 1:
              case 5:
                      AR &= RMASK;
                      break;
              case 0:
              case 4:
                      if (Mem_read(0, 0, 0, 0))
                          goto last;
                      AR = MB;
                      break;
              case 2:
              case 3:
              case 6:
              case 7:
                      if (Mem_read(0, 0, 0, 1))
                          goto last;
                      AR = MB;
                      break;
              }

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
                 goto mul_done;
              }
#if KS
              if (AR == SMASK && BR == SMASK) /* Handle special case */
                 flag3 = !flag3;
#endif
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
                  if (AR > (uint64)flag3 && !pi_cycle) {
                     FLAGS |= OVR|TRP1;
#if PDP6 | KA
                     check_apr_irq();
#endif
                  }
                  if (flag3) {
                      MQ ^= CMASK;
                      MQ++;
                      MQ |= SMASK;
                  }
                  AR = MQ;
                  goto mul_done;
              }
              if ((AR & SMASK) != 0 && !pi_cycle) {
                 FLAGS |= OVR|TRP1;
#if PDP6 | KA
                 check_apr_irq();
#endif
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
              if (BR == SMASK && (AR & SMASK)) { /* Handle special case */
                  FLAGS |= OVR|TRP1;
                  check_apr_irq();
              }
#endif
mul_done:
              switch (IR & 07) {
              case 7:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      /* Fall through */
              case 5:
              case 4:
                      set_reg(AC + 1, MQ);
                      /* Fall through */
              case 1:
              case 0:
                      set_reg(AC, AR);
                      break;
              case 2:
              case 6:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      break;
              case 3:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      set_reg(AC, AR);
                      break;
              }
              break;

    case 0230:       /* IDIV */ /* SAC2|SAC|FCE|FAC */
    case 0231:       /* IDIVI */ /* SAC2|SAC|FAC */
    case 0232:       /* IDIVM */ /* FCEPSE|FAC */
    case 0233:       /* IDIVB */ /* SAC2|SAC|FCEPSE|FAC */
              switch (IR & 03) {
              case 0:
                      AR = BR;
                      if (Mem_read(0, 0, 0, 0))
                          goto last;
                      BR = MB;
                      break;
              case 1:
                      MQ = AR & RMASK;
                      AR = BR;
                      BR = MQ;
                      break;
              case 2:
              case 3:
                      AR = BR;
                      if (Mem_read(0, 0, 0, 1))
                          goto last;
                      BR = MB;
                      break;
              }

              flag1 = 0;
              flag3 = 0;
              if (BR & SMASK) {
                 BR = (CM(BR) + 1) & FMASK;
                 flag1 = !flag1;
              }

              if (BR == 0) {          /* Check for overflow */
                  FLAGS |= OVR|NODIV|TRP1; /* Overflow and No Divide */
#if PDP6 | KA
                  check_apr_irq();
#endif
                  break;              /* Done */
              }

#if !PDP6
              if (AR == SMASK && BR == 1) {
                  FLAGS |= OVR|NODIV|TRP1; /* Overflow and No Divide */
#if PDP6 | KA
                  check_apr_irq();
#endif
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
              switch (IR & 03) {
              case 3:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      /* Fall through */
              case 1:
              case 0:
                      set_reg(AC, AR);
                      set_reg(AC + 1, MQ);
                      break;
              case 2:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      break;
              }
              break;

    case 0234:       /* DIV */ /* SAC2|SAC|FCE|FAC|FAC2 */
    case 0235:       /* DIVI */ /* SAC2|SAC|FAC|FAC2 */
    case 0236:       /* DIVM */ /* FCEPSE|FAC|FAC2 */
    case 0237:       /* DIVB */ /* SAC2|SAC|FCEPSE|FAC|FAC */
              switch (IR & 3) {
              case 0:
                     AR = BR;
                     if (Mem_read(0, 0, 0, 0))
                         goto last;
                     BR = MB;
                     break;
              case 1:
                     MQ = AR & RMASK;
                     AR = BR;
                     BR = MQ;
                     break;
              case 2:
              case 3:
                     AR = BR;
                     if (Mem_read(0, 0, 0, 1))
                         goto last;
                     BR = MB;
                     break;
              }
              MQ = get_reg(AC + 1);

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
#if PDP6 | KA
                  check_apr_irq();
#endif
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
              switch (IR & 03) {
              case 3:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      /* Fall through */
              case 1:
              case 0:
                      set_reg(AC, AR);
                      set_reg(AC + 1, MQ);
                      break;
              case 2:
                      MB = AR;
                      if (Mem_write(0, 0)) {
                         goto last;
                      }
                      break;
              }
              break;

               /* Shift */
    case 0240: /* ASH */  /* FAC|SAC */
              SC = ((AB & RSIGN) ? (0377 ^ AB) + 1 : AB) & 0377;
              if (SC == 0)
                  break;
              AR = BR;
              AD = (AR & SMASK) ? FMASK : 0;
              if (AB & RSIGN) {
                  if (SC < 35)
                     AR = ((AR >> SC) | (AD << (36 - SC))) & FMASK;
                 else
                     AR = AD;
              } else {
                 if (((AD << SC) & ~CMASK) != ((AR << SC) & ~CMASK)) {
                     FLAGS |= OVR|TRP1;
#if PDP6 | KA
                     check_apr_irq();
#endif
                 }
                 AR = ((AR << SC) & CMASK) | (AR & SMASK);
              }
              set_reg(AC, AR);
              break;

    case 0241: /* ROT */ /* FAC|SAC */
              SC = (AB & RSIGN) ?
                     ((AB & 0377) ? (((0377 ^ AB) + 1) & 0377) : 0400)
                   : (AB & 0377);
              if (SC == 0)
                  break;
              SC = SC % 36;
              if (AB & RSIGN)
                  SC = 36 - SC;
              AR = ((BR << SC) | (BR >> (36 - SC))) & FMASK;
              set_reg(AC, AR);
              break;

    case 0242: /* LSH */ /* FAC|SAC */
              SC = ((AB & RSIGN) ? (0377 ^ AB) + 1 : AB) & 0377;
              if (SC != 0) {
                 if (SC > 36){
                     BR = 0;
                 } else if (AB & RSIGN) {
                     BR = BR >> SC;
                 } else {
                     BR = (BR << SC) & FMASK;
                 }
              }
              AR = BR;
              set_reg(AC, AR);
              break;

    case 0243:  /* JFFO */ /* FAC */
#if !PDP6
              SC = 0;
              if (BR != 0) {
#if ITS | KL_ITS
                  if (QITS && (FLAGS & USER)) {
                      jpc = PC;
                  }
#endif
                  PC = AB;
                  f_pc_inh = 1;
                  SC = nlzero(BR);
              }
              set_reg(AC + 1, SC);
#endif
              break;

    case 0244: /* ASHC */ /* FAC|SAC|SAC2|FAC2 */
              AR = BR;
              MQ = get_reg(AC + 1);
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
#if KA
                         check_apr_irq();
#endif
                      }
#endif
                      AR = (AD & SMASK) | ((MQ << (SC - 35)) & CMASK);
                      MQ = (AD & SMASK);
                 } else {
                      if ((((AD & CMASK) << SC) & ~CMASK) != ((AR << SC) & ~CMASK)) {
                         FLAGS |= OVR|TRP1;
#if PDP6 | KA
                         check_apr_irq();
#endif
                      }
                      AR = (AD & SMASK) | ((AR << SC) & CMASK) |
                             (MQ >> (35 - SC));
                      MQ = (AD & SMASK) | ((MQ << SC) & CMASK);
                 }
              }
              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0245: /* ROTC */ /* FAC|SAC|SAC2|FAC2 */
              AR = BR;
              MQ = get_reg(AC + 1);
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
              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

    case 0246: /* LSHC */ /* FAC|SAC|SAC2|FAC2 */
              AR = BR;
              MQ = get_reg(AC + 1);
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
              set_reg(AC, AR);
              set_reg(AC+1, MQ);
              break;

          /* Branch */
    case 0250:  /* EXCH */ /* FAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              MB = BR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0251: /* BLT */ /* FAC */
              AR = BR;
              BR = AB;
#if KL | KS
              /* Precompute end of transfer address */
              AD = (CM(AR) + BR + 1) & RMASK;
              AD = ((AR + (AD << 18)) & LMASK) | ((AR + AD) & RMASK);
              set_reg(AC, AOB(AD));
#endif
              do {
                  AIO_CHECK_EVENT;                    /* queue async events */
                  if (sim_interval <= 0) {
                      if ((reason = sim_process_event()) != SCPE_OK) {
                          f_pc_inh = 1;
                          set_reg(AC, AR);
                          break;
                      }
                      /* Allow for interrupt */
                      if (pi_pending) {
                          pi_rq = check_irq_level();
                          if (pi_rq) {
                              f_pc_inh = 1;
                              set_reg(AC, AR);
                              break;
                          }
                      }
                  }
                  AB = (AR >> 18) & RMASK;
#if KL | KS
                  BYF5 = 1;
#endif
                  if (Mem_read(0, 0, 0, 0)) {
#if KL | KS
                       BYF5 = 0;
#endif
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
#if KA | PDP6
#if ITS
                       if (QITS)
                           set_reg(AC, AR);
#endif
#else
                       set_reg(AC, AR);
#endif
                       goto last;
                  }
                  AB = (AR & RMASK);
#if KL | KS
                  BYF5 = 0;
#endif
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
#if KA | PDP6
#if ITS
                       if (QITS)
                           set_reg(AC, AR);
#endif
#else
                       set_reg(AC, AR);
#endif
                       goto last;
                  }
                  AD = (AR & RMASK) + CM(BR) + 1;
                  AR = AOB(AR);
              } while ((AD & C1) == 0);
              break;

    case 0252: /* AOBJP */ /* FAC|SAC */
              AR = AOB(BR);
              if ((AR & SMASK) == 0) {
#if ITS | KL_ITS
                  if (QITS && (FLAGS & USER)) {
                      jpc = PC;
                  }
#endif
                  PC_CHANGE
                  PC = AB;
                  f_pc_inh = 1;
              }
              AR &= FMASK;
              set_reg(AC, AR);
              break;

    case 0253: /* AOBJN */ /* FAC|SAC */
              AR = AOB(BR);
              if ((AR & SMASK) != 0) {
#if ITS | KL_ITS
                  if (QITS && (FLAGS & USER)) {
                      jpc = PC;
                  }
#endif
                  PC_CHANGE
                  PC = AB;
                  f_pc_inh = 1;
              }
              AR &= FMASK;
              set_reg(AC, AR);
              break;

    case 0254: /* JRST */      /* AR Frm PC */
#if KL | KS
#if KL_ITS | KS_ITS
              if (uuo_cycle | pi_cycle) {
                 if (QITS && one_p_arm) {
                     FLAGS |= ADRFLT;
                     one_p_arm = 0;
                 }
              }
#endif
              switch (AC) {
              case 000: /* JRST */
#if KL
                       if (QKLB && t20_page)
                           pc_sect = sect;
#endif
                       break;
              case 001: /* PORTAL */
#if KL
                       if (QKLB && t20_page)
                           pc_sect = sect;
                       FLAGS &= ~(PUBLIC|PRV_PUB);
#endif
                       break;
              case 005:  /* XJRSTF */
xjrstf:
                       if (Mem_read(0, 0, 0, 0))
                           goto last;
                       BR = MB;
                       AB = (AB + 1) & RMASK;
                       if (Mem_read(0, 0, 0, 0))
                           goto last;
                       AR = MB;       /* Get PC. */
#if KL
                       if (QKLB && t20_page) {
                          pc_sect = (AR >> 18) & 07777;
                          if (AC != 07 && (FLAGS & USER) == 0 && ((BR >> 23) & USER) == 0)
                              prev_sect = BR & 037;
                       }
#endif
                       BR = BR >> 23; /* Move flags into position */
                       goto jrstf;

              case 006:  /* XJEN */
              case 012:  /* JEN */
#if KL
                       /* Restore interrupt level. */
                       if ((FLAGS & (USER|USERIO)) == USER ||
                           (FLAGS & (USER|PUBLIC)) == PUBLIC) {
#else
                       /* Restore interrupt level. */
                       if ((FLAGS & (USER|USERIO)) == USER) {
#endif
                            goto muuo;
                       } else {
                            pi_restore = 1;
                       }
                       if (AC == 06)
                           goto xjrstf;
                       /* Fall through */

              case 002: /* JRSTF */
                       BR = AR >> 23; /* Move into position */
jrstf:
#if KL_ITS
                       if (QITS)
                           f = FLAGS & (TRP1|TRP2);
#endif
                       FLAGS &= ~(OVR|NODIV|FLTUND|BYTI|FLTOVR|CRY1|CRY0|TRP1|TRP2|PCHNG|ADRFLT);
                       /* If executive mode, copy USER and UIO */
                       if ((FLAGS & (PUBLIC|USER)) == 0)
                          FLAGS |= BR & (USER|USERIO|PUBLIC);
                       /* Can always clear UIO */
                       if ((BR & USERIO) == 0)
                          FLAGS &= ~USERIO;
                       FLAGS |= BR & (OVR|NODIV|FLTUND|BYTI|FLTOVR|CRY1|CRY0|\
                                       TRP1|TRP2|PUBLIC|PCHNG|ADRFLT);
#if KL
                       FLAGS &= ~PRV_PUB;
                       if ((FLAGS & USER) == 0) {
                          FLAGS |= (BR & OVR) ? PRV_PUB : 0;
                       }
#endif
#if KL_ITS
                       if (QITS)
                           FLAGS |= f;
#endif
                       break;

              case 017:  /* Invalid */
#if KL_ITS
                       if (QITS) {
                           BR = AR >> 23; /* Move into position */
                           pi_enable = 1;
                           goto jrstf;
                       }
#endif
                       goto muuo;

              case 007:  /* XPCW */
                       MB = (((uint64)FLAGS) << 23) & FMASK;
                       /* Save Previous Public context */
#if KL
                       if ((FLAGS & USER) == 0) {
                           MB &= ~SMASK;
                           MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
                           if (QKLB && t20_page)
                              MB |= (uint64)(prev_sect & 037);
                       }
#endif
                       if (uuo_cycle | pi_cycle) {
                          FLAGS &= ~(USER|PUBLIC); /* Clear USER */
#if KL
                          sect = 0;                /* Force section zero on IRQ */
#endif
                       }
                       if (Mem_write(0, 0))
                          goto last;
                       AB = (AB + 1) & RMASK;
#if KL
                       if (QKLB && t20_page)
                           MB = (((((uint64)pc_sect) << 18) | PC) + !pi_cycle) & (SECTM|RMASK);
                       else
#endif
                           MB = (PC + !pi_cycle) & (RMASK);
                       if (Mem_write(0, 0))
                          goto last;
                       AB = (AB + 1) & RMASK;
                       goto xjrstf;

              case 015:  /* XJRST */
                       if (Mem_read(0, 0, 0, 0))
                           goto last;
                       AR = MB;       /* Get PC. */
#if KL
                       if (QKLB && t20_page) {
                          pc_sect = (AR >> 18) & 07777;
                       }
#endif
                       break;

              case 014:  /* SFM */
#if KS
                       if ((FLAGS & USER) != 0) {
                          goto muuo;
                       }
#endif
                       MB = (((uint64)FLAGS) << 23) & FMASK;
#if KL
                       if ((FLAGS & USER) == 0) {
                           MB &= ~SMASK;
                           MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
                           if (QKLB && t20_page)
                               MB |= (uint64)(prev_sect & 037);
                       }
#endif
                       (void)Mem_write(0, 0);
                       goto last;

              case 003:  /* Invalid */
              case 011:  /* Invalid */
              case 013:  /* Invalid */
              case 016:  /* Invalid */
                       goto muuo;

              case 004:  /* HALT */
#if KL
                       /* Restore interrupt level. */
                       if ((FLAGS & (USER|USERIO)) == USER ||
                           (FLAGS & (USER|PUBLIC)) == PUBLIC) {
#else
                       /* Restore interrupt level. */
                       if ((FLAGS & (USER|USERIO)) == USER) {
#endif
                            goto muuo;
                       } else {
                            RUN = 0;
                            prog_stop = 1;
                            reason = STOP_HALT;
                       }
                       break;
              case 010:  /* JEN */
#if KL
                       /* Restore interrupt level. */
                       if ((FLAGS & (USER|USERIO)) == USER ||
                           (FLAGS & (USER|PUBLIC)) == PUBLIC) {
#else
                       /* Restore interrupt level. */
                       if ((FLAGS & (USER)) == USER) {
#endif
                            goto muuo;
                       } else {
                            pi_restore = 1;
                       }
                       break;
              }
#if KL_ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
              PC = AR & RMASK;
#if KL
              if (QKLB && t20_page && glb_sect)
                  pc_sect = (AR >> 18) & 07777;
#endif
#else
              /* JRST for PDP6, KA and KI */
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
#if KI
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
#if KI
                 if ((FLAGS & (USER|USERIO)) == USER ||
                     (FLAGS & (USER|PUBLIC)) == PUBLIC) {
#else
                 if ((FLAGS & (USER|USERIO)) == USER) {
#endif
                      goto muuo;
                 } else {
                      RUN = 0;
                      prog_stop = 1;
                      reason = STOP_HALT;
                 }
              }
#if ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
              PC = AR & RMASK;
              PC_CHANGE
              /* JRSTF */
              if (AC & 02) {
                 FLAGS &= ~(OVR|NODIV|FLTUND|BYTI|FLTOVR|CRY1|CRY0|TRP1|TRP2|PCHNG|ADRFLT);
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
                                 TRP1|TRP2|PUBLIC|PCHNG|ADRFLT);
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
#if KI
                 FLAGS &= ~(PUBLIC|PRV_PUB);
#else
                 FLAGS |= USER;
#endif
              }
#endif
              f_pc_inh = 1;
              break;

    case 0255: /* JFCL */
              if ((FLAGS >> 9) & AC) {
#if ITS | KL_ITS
                  if (QITS && (FLAGS & USER)) {
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
#if KS
#if KS_ITS
              if (!QITS && (FLAGS & USER) == 0)
#else
              if ((FLAGS & USER) == 0)
#endif
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
#if KL_ITS | KS_ITS
              if (QITS && one_p_arm) {
                  FLAGS |= ADRFLT;
                  one_p_arm = 0;
              }
#endif
              break;

    case 0257:  /* MAP */
#if KI | KL | KS
#if KS_ITS
              if (QITS)
                  goto muuo;
#endif
              f = AB >> 9;
              flag1 = (FLAGS & USER) != 0;
              flag3 = 0;
#if KL
              /* Invalid in user unless USERIO set, or not in supervisor mode */
              if ((FLAGS & (USER|USERIO)) == USER || (FLAGS & (USER|PUBLIC)) == PUBLIC)
                  goto muuo;

              /* Figure out if this is a user space access */
              if (xct_flag & 4) {
                  flag1 = (FLAGS & USERIO) != 0;
                  sect = prev_sect;
              }
#endif
#if KS
              if (xct_flag & 4) {
                  flag1 = (FLAGS & USERIO) != 0;
              }
#endif
#if KL | KS
              /* Check if Paging Enabled */
              if (!page_enable) {
                  AR = AB; /* direct map */
                  if (flag1)                 /* U */
                     AR |= SMASK;            /* BIT0 */
                  AR |= BIT2|BIT3|BIT4|BIT8;
                  set_reg(AC, AR);
                  break;
              }

              /* Handle KI paging odditiy */
              if (!flag1 && !t20_page && (f & 0740) == 0340) {
                  /* Pages 340-377 via UBT */
                  f += 01000 - 0340;
                  flag3 = 1;
              }

              AR = load_tlb(flag1 | flag3, f, 0);
              if (page_fault) {
                  page_fault = 0;
                  AR |= fault_data;
                  if (flag1)                      /* U */
                      AR |= SMASK;
                  set_reg(AC, AR);
                  break;
              }
              BR = AR;
              /* Remap the flag bits */
              if (BR & KL_PAG_A) {      /* A */
#if KL
                 AR = ((AR & 017777LL) << 9) + (AB & 0777);
#else
                 AR = ((AR & 003777LL) << 9) + (AB & 0777);
#endif
                 if (flag1)                 /* U */
                    AR |= SMASK;            /* BIT0 */
                 AR |= BIT2;                /* BIT2 */
#if KL
                 if (BR & KL_PAG_P)         /* P */
                    AR |= BIT6;             /* BIT6 */
#endif
                 if (BR & KL_PAG_W)         /* W */
                    AR |= BIT3;             /* BIT3 */
                 if (BR & KL_PAG_S)         /* S */
                    AR |= BIT4;             /* BIT4 */
                 if (BR & KL_PAG_C)         /* C */
                    AR |= BIT7;             /* BIT7 */
              } else
#if KS
                 AR = AB;
#else
                 AR = (f & 01740) ? 0 : 0377777LL;
#endif
              AR |= BIT8;
#else
              /* Check if Paging Enabled */
              if (!page_enable || AB < 020) {
                  AR = 0020000LL + f; /* direct map */
                  set_reg(AC, AR);
                  break;
              }
              /* Figure out if this is a user space access */
              if (xct_flag != 0 && !flag1) {
                  if ((xct_flag & 2) != 0) {
                      flag1 = (FLAGS & USERIO) != 0;
                  }
              }

              /* If user, check if small user enabled */
              if (flag1) {
                  if (small_user && (f & 0340) != 0) {
                      AR = 0420000LL;     /* Outside small user space registers */
                      set_reg(AC, AR);
                      break;
                  }
              }

              /* Get translation */
              AR = load_tlb(flag1, f);
              if (AR == 0) {
                  AR = 0437777LL;
              } else {
                  if ((AR & 0400000LL) == 0)
                      AR &= 0437777LL; /* Return valid entry for page */
                  AR ^= 0400000LL;  /* Flip access status. */
              }
#endif
              set_reg(AC, AR);
#endif
              break;

              /* Stack, JUMP */
    case 0260:  /* PUSHJ */  /* FAC|SAC */
#if KL
              if (QKLB && t20_page)
                  AR = (sect << 18) | (AR & RMASK);;
              if (QKLB && t20_page && pc_sect != 0) {
                  MB = ((uint64)pc_sect << 18) + (PC + !pi_cycle);
              } else {
#endif
              MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + !pi_cycle) & RMASK);
#if KI | KL
              if ((FLAGS & USER) == 0) {
                  MB &= ~SMASK;
                  MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
                  MB &= FMASK;
              }
#if KL
              }
#endif
#endif
#if KL | KS
              BYF5 = 1;
#endif
#if KL
              if (QKLB && t20_page && pc_sect != 0 && (BR & SMASK) == 0 && (BR & SECTM) != 0) {
                  BR = (BR + 1) & FMASK;
                  sect = (BR >> 18) & 07777;
                  glb_sect = 1;
              } else {
                  sect = pc_sect;
                  glb_sect = 0;
#endif
              BR = AOB(BR);
#if KL_ITS
              if (QITS && one_p_arm)    /* Don't clear traps if 1proc */
                 FLAGS &= ~ (BYTI);
              else
#endif
              FLAGS &= ~ (BYTI|ADRFLT|TRP1|TRP2);
              if (BR & C1) {
#if KI | KL | KS
                 if (!pi_cycle)
                     FLAGS |= TRP2;
#else
                 push_ovf = 1;
                 check_apr_irq();
#endif
              }
#if KL
              }
#endif
              AB = BR & RMASK;
              if (hst_lnt)
                  hst[hst_p].mb = MB;
              if (Mem_write(uuo_cycle | pi_cycle, 0))
                 goto last;
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
#if ITS | KL_ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
#if KL
              if (QKLB && t20_page)
                  pc_sect = (AR >> 18) & 0037;
#endif
              PC = AR & RMASK;
              PC_CHANGE
              f_pc_inh = 1;
              AR = BR & FMASK;
              set_reg(AC, AR);
              break;

    case 0261: /* PUSH */ /* FAC|FCE|SAC */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
#if KL | KS
              BYF5 = 1;
#endif
#if KL
              if (QKLB && t20_page &&pc_sect != 0 && (BR & SMASK) == 0 && (BR & SECTM) != 0) {
                  BR = (BR + 1) & FMASK;
                  sect = (BR >> 18) & 07777;
                  glb_sect = 1;
              } else {
                  sect = pc_sect;
#endif
              BR = AOB(BR);
              if (BR & C1) {
#if KI | KL | KS
                 if (!pi_cycle)
                     FLAGS |= TRP2;
#else
                 push_ovf = 1;
                 check_apr_irq();
#endif
              }
#if KL
              }
#endif
              AB = BR & RMASK;
              MB = AR;
              if (hst_lnt)
                  hst[hst_p].mb = MB;
              if (Mem_write(0, 0))
                 goto last;
              AR = BR & FMASK;
              set_reg(AC, AR);
              break;

    case 0262: /* POP */ /*  FAC */

#if KL | KS
              BYF5 = 1;   /* Tell PXCT that this is stack */
#endif
#if KL
              flag1 = glb_sect;
              glb_sect = 0;
              sect = pc_sect;
              /* Decide if our stack pointer is global or local */
              if (QKLB && t20_page) {
                  if ((xct_flag & 1) != 0)
                     sect = prev_sect;
                  if (sect != 0 && (BR & SMASK) == 0 && (BR & SECTM) != 0) {
                     sect = (BR >> 18) & 07777;
                     glb_sect = 1;
                  }
              }
#endif
              /* Fetch top of stack */
              AB = BR & RMASK;
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              if (hst_lnt)
                  hst[hst_p].mb = MB;

              /* Save in location */
              AB = AR & RMASK;
#if KL | KS
              BYF5 = 0;   /* Now back to data */
#endif
#if KL
              if (QKLB && t20_page) {
                  sect = cur_sect;
                  glb_sect = flag1;
              }
#endif

#if KA | KI
              /* On KA or KI the AC is stored before Memory */
              MQ = BR;   /* Save original stack in case fault on write */
              BR = SOB(BR);
              AR = BR & FMASK;
              set_reg(AC, AR);
#endif

              if (Mem_write(0, 0)) {
#if KA | KI
                  /* Restore AC if fault */
                  set_reg(AC, MQ);
#endif
                  goto last;
              }
#if KL
              /* Determine if we had global stack pointer or not */
              sect = pc_sect;
              if (QKLB && t20_page) {
                  if ((xct_flag & 1) != 0)
                     sect = prev_sect;
                  if (sect != 0 && (BR & SMASK) == 0 && (BR & SECTM) != 0) {
                     AR = (BR - 1) & FMASK;
                     set_reg(AC, AR);
                     break;
                  }
              }
#endif
#if PDP6 | KL | KS
              /* This has to after the check for KL10 B extended check */
              BR = SOB(BR);
              AR = BR & FMASK;
              set_reg(AC, AR);
#endif
              if ((BR & C1) == 0) {
#if KI | KL | KS
                  if (!pi_cycle)
                      FLAGS |= TRP2;
#else
                  push_ovf = 1;
                  check_apr_irq();
#endif
              }
              break;

    case 0263: /* POPJ */ /* FAC|SAC */
              AB = BR & RMASK;
#if KL | KS
              BYF5 = 1;   /* Tell PXCT that this is stack */
#endif
#if KL
              glb_sect = 0;
              sect = pc_sect;
              if (QKLB && t20_page && (xct_flag & 1) != 0)
                  sect = prev_sect;
              if (QKLB && t20_page && sect != 0 && (BR & SMASK) == 0 && (BR & SECTM) != 0) {
                  sect = (BR >> 18) & 07777;
                  glb_sect = 1;
                  BR = (BR - 1) & FMASK;
              } else
#endif
              BR = SOB(BR);

              if (hst_lnt) {
#if KL
                  hst[hst_p].ea = AB | (sect << 18);
#else
                  hst[hst_p].ea = AB;
#endif
              }
              if (Mem_read(0, 0, 0, 0))
                  goto last;
#if ITS | KL_ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
              f_pc_inh = 1;
              PC_CHANGE
              PC = MB & RMASK;
#if KL | KS
              BYF5 = 0;   /* Tell PXCT that this is stack */
#endif
#if KL
              if (QKLB && t20_page && pc_sect != 0) {
                  pc_sect = (MB >> 18) & 07777;
                  if ((BR & SMASK) == 0 && (BR & SECTM) != 0) {
                      AR = BR & FMASK;
                      set_reg(AC, AR);
                      break;
                  }
              }
#endif
              if ((BR & C1) == 0) {
#if KI | KL | KS
                  if (!pi_cycle)
                     FLAGS |= TRP2;
#else
                  push_ovf = 1;
                  check_apr_irq();
#endif
              }
              AR = BR & FMASK;
              set_reg(AC, AR);
              break;

    case 0264: /* JSR */
              AR &= RMASK;
#if KL
              if (QKLB && t20_page && pc_sect != 0)
                  MB = ((uint64)pc_sect << 18) + (PC + !pi_cycle);
              else {
#endif
              MB = (((uint64)(FLAGS) << 23) & LMASK) | ((PC + !pi_cycle) & RMASK);
#if KI | KL
              if ((FLAGS & USER) == 0) {
                  MB &= ~SMASK;
                  MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
              }
#if KL
              }
#endif
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
#if ITS | KL_ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
#if KL
              if (QKLB && t20_page) {
                  AR = AR + 1;
                  if (AR & BIT17)
                      cur_sect++;
                  if (glb_sect)
                      pc_sect = cur_sect;
                  PC = AR & RMASK;
              } else
#endif
              PC = (AR + 1) & RMASK;
              f_pc_inh = 1;
              break;

    case 0265: /* JSP */  /* SAC */
#if KL
              if (QKLB && t20_page && pc_sect != 0)
                  AD = ((uint64)pc_sect << 18) + (PC + !pi_cycle);
              else {
#endif
              AD = (((uint64)(FLAGS) << 23) & LMASK) |
                      ((PC + !pi_cycle) & RMASK);
              FLAGS &= ~ (BYTI|ADRFLT|TRP1|TRP2);
#if KI | KL
              if ((FLAGS & USER) == 0) {
                  AD &= ~SMASK;
                  AD |= (FLAGS & PRV_PUB) ? SMASK : 0;
              }
#if KL
              }
#endif
#endif
#if !PDP6
              if (uuo_cycle | pi_cycle) {
                 FLAGS &= ~(USER|PUBLIC); /* Clear USER */
              }
#endif
#if ITS | KL_ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
#if KL
              if (QKLB && t20_page && glb_sect)
                  pc_sect = cur_sect;
#endif
              PC = AR & RMASK;
              AR = AD;
              f_pc_inh = 1;
              set_reg(AC, AR);
              break;

    case 0266: /* JSA */ /* FBR|SCE */
              AR = ((AR & RMASK) << 18) | ((PC + 1) & RMASK);
              MB = BR;
              set_reg(AC, AR);
#if !PDP6
              if (uuo_cycle | pi_cycle) {
                 FLAGS &= ~(USER|PUBLIC); /* Clear USER */
              }
#endif
#if ITS | KL_ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
#if KL
              if (QKLB && t20_page && glb_sect)
                  pc_sect = cur_sect;
#endif
              PC = AB;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0267: /* JRA */
              AD = AB;
              AB = (get_reg(AC) >> 18) & RMASK;
              if (Mem_read(uuo_cycle | pi_cycle, 0, 0, 0))
                   goto last;
              set_reg(AC, MB);
#if ITS | KL_ITS
              if (QITS && (FLAGS & USER)) {
                  jpc = PC;
              }
#endif
              PC_CHANGE
              PC = (t_addr)(AD & RMASK);
              f_pc_inh = 1;
              break;

    case 0270: /* ADD */  /* FBR|SAC|FCE */
    case 0271: /* ADDI */ /* FBR|SAC */
    case 0272: /* ADDM */ /* FBR|FCEPSE */
    case 0273: /* ADDB */ /* FBR|SAC|FCEPSE */
              switch (IR & 3) {
              case 0:
                     if (Mem_read(0, 0, 0, 0))
                         goto last;
                     AR = MB;
                     break;
              case 1:
                     AR &= RMASK;
                     break;
              case 2:
              case 3:
                     if (Mem_read(0, 0, 0, 1))
                         goto last;
                     AR = MB;
                     break;
              }

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
                  if (!pi_cycle) {
                      FLAGS |= OVR|TRP1;
#if PDP6 | KA
                      check_apr_irq();
#endif
                  }
              }
              AR &= FMASK;
              if ((IR & 2) == 2) {
                  MB = AR;
                  if (Mem_write(0, 0)) {
                     goto last;
                  }
              }
              if ((IR & 3) != 2)
                  set_reg(AC, AR);
              break;

    case 0274: /* SUB */  /* FBR|SAC|FCE */
    case 0275: /* SUBI */ /* FBR|SAC */
    case 0276: /* SUBM */ /* FBR|FCEPSE */
    case 0277: /* SUBB */ /* FBR|SAC|FCEPSE */
              switch (IR & 3) {
              case 0:
                     if (Mem_read(0, 0, 0, 0))
                         goto last;
                     AR = MB;
                     break;
              case 1:
                     AR &= RMASK;
                     break;
              case 2:
              case 3:
                     if (Mem_read(0, 0, 0, 1))
                         goto last;
                     AR = MB;
                     break;
              }
              flag1 = flag3 = 0;
              if ((CCM(AR) + (BR & CMASK) + 1) & SMASK) {
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
                  if (!pi_cycle) {
                      FLAGS |= OVR|TRP1;
#if PDP6 | KA
                      check_apr_irq();
#endif
                  }
              }
              AR &= FMASK;
              if ((IR & 2) == 2) {
                  MB = AR;
                  if (Mem_write(0, 0)) {
                     goto last;
                  }
              }
              if ((IR & 3) != 2)
                  set_reg(AC, AR);
              break;

    case 0300:    /* CAI   */  /* FBR */
    case 0301:    /* CAIL  */  /* FBR */
    case 0302:    /* CAIE  */  /* FBR */
    case 0303:    /* CAILE */  /* FBR */
    case 0304:    /* CAIA  */  /* FBR */
    case 0305:    /* CAIGE */  /* FBR */
    case 0306:    /* CAIN  */  /* FBR */
    case 0307:    /* CAIG  */  /* FBR */
              AR &= RMASK;
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

    case 0310:    /* CAM   */  /* FBR|FCE */
    case 0311:    /* CAML  */  /* FBR|FCE */
    case 0312:    /* CAME  */  /* FBR|FCE */
    case 0313:    /* CAMLE */  /* FBR|FCE */
    case 0314:    /* CAMA  */  /* FBR|FCE */
    case 0315:    /* CAMGE */  /* FBR|FCE */
    case 0316:    /* CAMN  */  /* FBR|FCE */
    case 0317:    /* CAMG  */  /* FBR|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;

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

    case 0320:    /* JUMP   */  /* FAC */
    case 0321:    /* JUMPL  */  /* FAC */
    case 0322:    /* JUMPE  */  /* FAC */
    case 0323:    /* JUMPLE */  /* FAC */
    case 0324:    /* JUMPA  */  /* FAC */
    case 0325:    /* JUMPGE */  /* FAC */
    case 0326:    /* JUMPN  */  /* FAC */
    case 0327:    /* JUMPG  */  /* FAC */
              AD = BR;
              BR = AR & RMASK;
              f = ((AD & SMASK) != 0);
              goto jump_op;                   /* JUMP, SKIP */

    case 0330:    /* SKIP   */ /* SACZ|FCE */
    case 0331:    /* SKIPL  */ /* SACZ|FCE */
    case 0332:    /* SKIPE  */ /* SACZ|FCE */
    case 0333:    /* SKIPLE */ /* SACZ|FCE */
    case 0334:    /* SKIPA  */ /* SACZ|FCE */
    case 0335:    /* SKIPGE */ /* SACZ|FCE */
    case 0336:    /* SKIPN  */ /* SACZ|FCE */
    case 0337:    /* SKIPG  */ /* SACZ|FCE */
              if (Mem_read(0, 0, 0, 0))
                 goto last;
              AR = MB;
              AD = AR;
              f = ((AD & SMASK) != 0);
              goto skip_op;                   /* JUMP, SKIP */

    case 0340:     /* AOJ   */ /* SAC|FAC */
    case 0341:     /* AOJL  */ /* SAC|FAC */
    case 0342:     /* AOJE  */ /* SAC|FAC */
    case 0343:     /* AOJLE */ /* SAC|FAC */
    case 0344:     /* AOJA  */ /* SAC|FAC */
    case 0345:     /* AOJGE */ /* SAC|FAC */
    case 0346:     /* AOJN  */ /* SAC|FAC */
    case 0347:     /* AOJG  */ /* SAC|FAC */
    case 0360:     /* SOJ   */ /* SAC|FAC */
    case 0361:     /* SOJL  */ /* SAC|FAC */
    case 0362:     /* SOJE  */ /* SAC|FAC */
    case 0363:     /* SOJLE */ /* SAC|FAC */
    case 0364:     /* SOJA  */ /* SAC|FAC */
    case 0365:     /* SOJGE */ /* SAC|FAC */
    case 0366:     /* SOJN  */ /* SAC|FAC */
    case 0367:     /* SOJG  */ /* SAC|FAC */
              flag1 = flag3 = 0;
              AD = (IR & 020) ? FMASK : 1;
              if (((BR & CMASK) + (AD & CMASK)) & SMASK) {
                  if (!pi_cycle)
                     FLAGS |= CRY1;
                  flag1 = 1;
              }
              AD = BR + AD;
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
#if PDP6 | KA
                  check_apr_irq();
#endif
              }
              f = ((AD & SMASK) != 0);
jump_op:
              AD &= FMASK;
              AR = AD;
              f |= ((AD == 0) << 1);
              f = f & IR;
              if (((IR & 04) != 0) == (f == 0)) {
#if ITS | KL_ITS
                  if (QITS && (FLAGS & USER)) {
                      jpc = PC;
                  }
#endif
                  PC_CHANGE
                  PC = AB;
                  f_pc_inh = 1;
              }
              AR &= FMASK;
              if ((IR & 040) != 0)
                  set_reg(AC, AR);
              break;

    case 0350:     /* AOS   */ /* SACZ|FCEPSE */
    case 0351:     /* AOSL  */ /* SACZ|FCEPSE */
    case 0352:     /* AOSE  */ /* SACZ|FCEPSE */
    case 0353:     /* AOSLE */ /* SACZ|FCEPSE */
    case 0354:     /* AOSA  */ /* SACZ|FCEPSE */
    case 0355:     /* AOSGE */ /* SACZ|FCEPSE */
    case 0356:     /* AOSN  */ /* SACZ|FCEPSE */
    case 0357:     /* AOSG  */ /* SACZ|FCEPSE */
    case 0370:     /* SOS   */ /* SACZ|FCEPSE */
    case 0371:     /* SOSL  */ /* SACZ|FCEPSE */
    case 0372:     /* SOSE  */ /* SACZ|FCEPSE */
    case 0373:     /* SOSLE */ /* SACZ|FCEPSE */
    case 0374:     /* SOSA  */ /* SACZ|FCEPSE */
    case 0375:     /* SOSGE */ /* SACZ|FCEPSE */
    case 0376:     /* SOSN  */ /* SACZ|FCEPSE */
    case 0377:     /* SOSG  */ /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
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
#if PDP6 | KA
                  check_apr_irq();
#endif
              }
              f = ((AD & SMASK) != 0);
skip_op:
              AR = AD & FMASK;
              f |= ((AR == 0) << 1);
              f = f & IR;
              if (((IR & 04) != 0) == (f == 0)) {
#if PDP6
                  if (pi_cycle)
                     f_pc_inh = 1;
#endif
                  PC_CHANGE
                  PC = (PC + 1) & RMASK;
#if KI | KL
              } else if (trap_flag == 0 && pi_cycle) {
                   pi_ov = pi_hold = 1;
#endif
              }
              if (((IR & 060) != 0) && (AC != 0)) {
                  set_reg(AC, AR);
              }
              if ((IR & 040) != 0) {
                 MB = AR;
                 if (Mem_write(0, 0)) {
                    goto last;
                 }
              }
              break;

              /* Bool */
    case 0400:    /* SETZ  */ /* SAC */
    case 0401:    /* SETZI */ /* SAC */
              AR = 0;                   /* SETZ */
              set_reg(AC, AR);
              break;

    case 0402:    /* SETZM */ /* SCE */
              MB = AR = 0;              /* SETZ */
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0403:    /* SETZB */ /* SAC|SCE */
              MB = AR = 0;              /* SETZ */
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0404:    /* AND  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB & BR;             /* AND */
              set_reg(AC, AR);
              break;
    case 0405:    /* ANDI */ /* FBR|SAC */
              AR = (AR & RMASK) & BR;   /* AND */
              set_reg(AC, AR);
              break;
    case 0406:    /* ANDM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB & BR;             /* AND */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0407:    /* ANDB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB & BR;             /* AND */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0410:    /* ANDCA  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB & CM(BR);         /* ANDCA */
              set_reg(AC, AR);
              break;
    case 0411:    /* ANDCAI */ /* FBR|SAC */
              AR &= RMASK;
              AR = AR & CM(BR);         /* ANDCA */
              set_reg(AC, AR);
              break;
    case 0412:    /* ANDCAM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB & CM(BR);         /* ANDCA */
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              break;

    case 0413:    /* ANDCAB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB & CM(BR);         /* ANDCA */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0415:    /* SETMI */ /* SAC */
              AR &= RMASK;
#if KL
              /* XMOVEI for extended addressing */
              if (QKLB && t20_page && pc_sect != 0) {
                  if (glb_sect == 0 && AR < 020)
                      AR |= BIT17;
                  else
                      AR |= ((uint64)cur_sect) << 18;
              }
#endif
             set_reg(AC, AR);
             break;

    case 0414:    /* SETM  */ /* SAC|FCE */
             if (Mem_read(0, 0, 0, 0))
                 goto last;
             AR = MB;
             set_reg(AC, AR);
             break;

    case 0416:    /* SETMM */ /* FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              AR = MB;
              break;

    case 0417:    /* SETMB */ /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              set_reg(AC, AR);
              break;

    case 0420:    /* ANDCM  */  /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = CM(MB) & BR;         /* ANDCM */
              set_reg(AC, AR);
              break;

    case 0421:    /* ANDCMI */  /* FBR|SAC */
              AR &= RMASK;
              AR = CM(AR) & BR;         /* ANDCM */
              set_reg(AC, AR);
              break;

    case 0422:    /* ANDCMM */  /* FBR|FECPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = CM(MB) & BR;         /* ANDCM */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0423:    /* ANDCMB */  /* FBR|SAC|FECPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = CM(MB) & BR;         /* ANDCM */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0424:    /* SETA  */ /* FBR|SAC */
              AR = BR;                  /* SETA */
              set_reg(AC, AR);
              break;
    case 0425:    /* SETAI */ /* FBR|SAC */
              AR = BR;                  /* SETA */
              set_reg(AC, AR);
              break;
    case 0426:    /* SETAM */ /* FBR|SCE */
              AR = BR;                  /* SETA */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0427:    /* SETAB */ /* FBR|SAC|SCE */
              AR = BR;                  /* SETA */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0430:    /* XOR  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB ^ BR;             /* XOR */
              set_reg(AC, AR);
              break;
    case 0431:    /* XORI */ /* FBR|SAC */
              AR &= RMASK;
              AR = AR ^ BR;             /* XOR */
              set_reg(AC, AR);
              break;
    case 0432:    /* XORM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB ^ BR;             /* XOR */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0433:    /* XORB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB ^ BR;             /* XOR */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0434:    /* IOR  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = CM(CM(MB) & CM(BR)); /* IOR */
              set_reg(AC, AR);
              break;

    case 0435:    /* IORI */ /* FBR|SAC */
              AR &= RMASK;
              AR = CM(CM(AR) & CM(BR)); /* IOR */
              set_reg(AC, AR);
              break;

    case 0436:    /* IORM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = CM(CM(MB) & CM(BR)); /* IOR */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0437:    /* IORB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = CM(CM(MB) & CM(BR)); /* IOR */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0440:    /* ANDCB  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = CM(AR) & CM(BR);     /* ANDCB */
              set_reg(AC, AR);
              break;
    case 0441:    /* ANDCBI */ /* FBR|SAC */
              AR &= RMASK;
              AR = CM(AR) & CM(BR);     /* ANDCB */
              set_reg(AC, AR);
              break;
    case 0442:    /* ANDCBM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR) & CM(BR);     /* ANDCB */
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              break;
    case 0443:    /* ANDCBB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR) & CM(BR);     /* ANDCB */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0444:    /* EQV  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = CM(AR ^ BR);         /* EQV */
              set_reg(AC, AR);
              break;
    case 0445:    /* EQVI */ /* FBR|SAC */
              AR &= RMASK;
              AR = CM(AR ^ BR);         /* EQV */
              set_reg(AC, AR);
              break;
    case 0446:    /* EQVM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR ^ BR);         /* EQV */
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              break;
    case 0447:    /* EQVB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR ^ BR);         /* EQV */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0450:    /* SETCA  */ /* FBR|SAC */
              AR = CM(BR);              /* SETCA */
              set_reg(AC, AR);
              break;

    case 0451:    /* SETCAI */ /* FBR|SAC */
              AR = CM(BR);              /* SETCA */
              set_reg(AC, AR);
              break;

    case 0452:    /* SETCAM */ /* FBR|SCE */
              AR = CM(BR);              /* SETCA */
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              break;

    case 0453:    /* SETCAB */ /* FBR|SAC|SCE */
              AR = CM(BR);              /* SETCA */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0454:    /* ORCA  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = CM(CM(AR) & BR);     /* ORCA */
              set_reg(AC, AR);
              break;
    case 0455:    /* ORCAI */ /* FBR|SAC */
              AR &= RMASK;
              AR = CM(CM(AR) & BR);     /* ORCA */
              set_reg(AC, AR);
              break;
    case 0456:    /* ORCAM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(CM(AR) & BR);     /* ORCA */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0457:    /* ORCAB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(CM(AR) & BR);     /* ORCA */
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              set_reg(AC, AR);
              break;

    case 0460:    /* SETCM  */  /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = CM(AR);              /* SETCM */
              set_reg(AC, AR);
              break;

    case 0461:    /* SETCMI */  /* SAC */
              AR &= RMASK;
              AR = CM(AR);              /* SETCM */
              set_reg(AC, AR);
              break;
    case 0462:    /* SETCMM */  /* FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR);              /* SETCM */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0463:    /* SETCMB */  /* SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR);              /* SETCM */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0464:    /* ORCM  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = CM(AR & CM(BR));     /* ORCM */
              set_reg(AC, AR);
              break;

    case 0465:    /* ORCMI */ /* FBR|SAC */
              AR &= RMASK;
              AR = CM(AR & CM(BR));     /* ORCM */
              set_reg(AC, AR);
              break;
    case 0466:    /* ORCMM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR & CM(BR));     /* ORCM */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0467:    /* ORCMB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR & CM(BR));     /* ORCM */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0470:    /* ORCB  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = CM(AR & BR);         /* ORCB */
              set_reg(AC, AR);
              break;
    case 0471:    /* ORCBI */ /* FBR|SAC */
              AR &= RMASK;
              AR = CM(AR & BR);         /* ORCB */
              set_reg(AC, AR);
              break;
    case 0472:    /* ORCBM */ /* FBR|FCEPSE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = CM(AR & BR);         /* ORCB */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0473:    /* ORCBB */ /* FBR|SAC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = CM(AR & BR);         /* ORCB */
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              set_reg(AC, AR);
              break;

    case 0474:    /* SETO  */  /* SAC */
              AR = FMASK;               /* SETO */
              set_reg(AC, AR);
              break;

    case 0475:    /* SETOI */  /* SAC */
              AR = FMASK;               /* SETO */
              set_reg(AC, AR);
              break;

    case 0476:    /* SETOM */  /* SCE */
              MB = AR = FMASK;          /* SETO */
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;
    case 0477:    /* SETOB */  /* SAC|SCE */
              MB = AR = FMASK;          /* SETO */
              if (Mem_write(0, 0)) {
                  goto last;
              }
              set_reg(AC, AR);
              break;

    case 0500:    /* HLL  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0)) {
                  goto last;
              }
              AR = MB;
              AR = (AR & LMASK) | (BR & RMASK);
              set_reg(AC, AR);
              break;

    case 0501:    /* HLLI */ /* FBR|SAC */
              AR &= RMASK;
#if KL
              /* XHLLI for extended addressing */
              if (QKLB && t20_page && pc_sect != 0) {
                  if (glb_sect == 0 && AR < 020)
                      AR = BIT17;
                  else
                      AR = ((uint64)cur_sect) << 18;
              }
#endif
              AR = (AR & LMASK) | (BR & RMASK);
              set_reg(AC, AR);
              break;

    case 0502:    /* HLLM */ /* FAC|FCEPSE */
              AR = BR;
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = (AR & LMASK) | (MB & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0503:     /* HLLS */ /* SACZ|FCEPSE */
    case 0543:     /* HRRS */ /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0547:    /* HLRS */ /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              BR = SWAP_AR;
              AR = (AR & LMASK) | (BR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0504:    /* HRL  */ /* SWAR|FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = (AR & LMASK) | (BR & RMASK);
              set_reg(AC, AR);
              break;

    case 0505:    /* HRLI */ /* SWAR|FBR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              AR = (AR & LMASK) | (BR & RMASK);
              set_reg(AC, AR);
              break;

    case 0506:    /* HRLM */ /* SWAR|FAC|FCEPSE */
              AR = BR;
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              BR = MB;
              AR = SWAP_AR;
              AR = (AR & LMASK) | (BR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0507:    /* HRLS */ /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              BR = SWAP_AR;
              AR = (BR & LMASK) | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0510:    /* HLLZ  */ /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = (AR & LMASK);
              set_reg(AC, AR);
              break;

    case 0511:    /* HLLZI */ /* SAC */
              AR &= RMASK;
              AR = (AR & LMASK);
              set_reg(AC, AR);
              break;

    case 0512:    /* HLLZM */ /* FAC|SCE */
              AR &= RMASK;

              BR = AR;
              AR = get_reg(AC);
              AR = (AR & LMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0513:    /* HLLZS */ /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = (AR & LMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0514:    /* HRLZ  */ /* SWAR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = (AR & LMASK);
              set_reg(AC, AR);
              break;

    case 0515:    /* HRLZI */ /* SWAR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              AR = (AR & LMASK);
              set_reg(AC, AR);
              break;

    case 0516:    /* HRLZM */ /* SWAR|FAC|SCE */
              BR = AR;
              AR = get_reg(AC);
              AR = SWAP_AR;
              AR = (AR & LMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0517:    /* HRLZS */ /* SWAR|SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = SWAP_AR;

                    AR = (AR & LMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0520:    /* HLLO  */  /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = (AR & LMASK) | RMASK;
              set_reg(AC, AR);
              break;

    case 0521:    /* HLLOI */  /* SAC */
              AR &= RMASK;
              AR = (AR & LMASK) | RMASK;
              set_reg(AC, AR);
              break;

    case 0522:    /* HLLOM */  /* FAC|SCE */
              BR = AR;
              AR = get_reg(AC);
              AR = (AR & LMASK) | RMASK;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0523:    /* HLLOS */  /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = (AR & LMASK) | RMASK;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0524:    /* HRLO  */  /* SWAR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = (AR & LMASK) | RMASK;
              set_reg(AC, AR);
              break;

    case 0525:    /* HRLOI */  /* SWAR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              AR = (AR & LMASK) | RMASK;
              set_reg(AC, AR);
              break;

    case 0526:    /* HRLOM */  /* SWAR|FAC|SCE */
              BR = AR & RMASK;
              AR = get_reg(AC);
              AR = SWAP_AR;
              AR = (AR & LMASK) | RMASK;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0527:    /* HRLOS */  /* SWAR|SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = (AR & LMASK) | RMASK;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0530:    /* HLLE  */ /* SAC|FCE */
              AR &= RMASK;
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              set_reg(AC, AR);
              break;

    case 0531:    /* HLLEI */ /* SAC */
              AR &= RMASK;
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              set_reg(AC, AR);
              break;

    case 0532:    /* HLLEM */ /* FAC|SCE */
              BR = AR & RMASK;
              AR = get_reg(AC);
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0533:    /* HLLES */ /* SAZC|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0534:    /* HRLE  */ /* SAC|SWAR|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              set_reg(AC, AR);
              break;

    case 0535:    /* HRLEI */ /* SAC|SWAR*/
              AR &= RMASK;
              AR = SWAP_AR;
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              set_reg(AC, AR);
              break;

    case 0536:    /* HRLEM */ /* FAC|SWAR|SCE */
              BR = AR & RMASK;
              AR = get_reg(AC);
              AR = SWAP_AR;
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0537:    /* HRLES */ /* SACZ|SWAR|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AD = ((AR & SMASK) != 0) ? RMASK : 0;
              AR = (AR & LMASK) | AD;
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0540:    /* HRR  */ /* FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = (BR & LMASK) | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0541:    /* HRRI */ /* FBR|SAC */
              AR &= RMASK;
              AR = (BR & LMASK) | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0542:    /* HRRM */ /* FAC|FCEPSE */
              AR = BR;
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              BR = MB;
              AR = (BR & LMASK) | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0544:    /* HLR  */ /* SWAR|FBR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              BR = get_reg(AC);
              AR = SWAP_AR;
              AR = (BR & LMASK) | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0545:    /* HLRI */ /* SWAR|FBR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              AR = (BR & LMASK) | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0546:    /* HLRM */ /* SWAR|FAC|FCEPSE */
              AR = BR;
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              BR = MB;
              AR = SWAP_AR;
              AR = (BR & LMASK) | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              break;

    case 0550:    /* HRRZ  */ /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0551:    /* HRRZI */ /* SAC */
              AR &= RMASK;
              AR = (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0552:    /* HRRZM */ /* FAC|SCE */
              BR = AR;
              AR = get_reg(AC);
              AR = (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0553:    /* HRRZS */ /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0554:    /* HLRZ  */ /* SWAR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0555:    /* HLRZI */ /* SWAR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              AR = (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0556:    /* HLRZM */ /* SWAR|FAC|SCE */
              BR = AR;
              AR = get_reg(AC);
              AR = SWAP_AR;
              AR = (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0557:    /* HLRZS */ /* SWAR|SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);

              break;

    case 0560:    /* HRRO  */  /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = LMASK | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0561:    /* HRROI */  /* SAC */
              AR &= RMASK;
              AR = LMASK | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0562:    /* HRROM */  /* FAC|SCE */
              AR &= RMASK;
              BR = AR;
              AR = get_reg(AC);
              AR = LMASK | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              break;

    case 0563:    /* HRROS */  /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = LMASK | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0564:    /* HLRO  */  /* SWAR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = LMASK | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0565:    /* HLROI */  /* SWAR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              AR = LMASK | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0566:    /* HLROM */  /* SWAR|FAC|SCE */
              AR = BR;
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              BR = MB;
              AR = SWAP_AR;
              AR = LMASK | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0567:    /* HLROS */  /* SWAR|SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AR = LMASK | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0570:    /* HRRE  */  /* SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0571:    /* HRREI */  /* SAC */
              AR &= RMASK;
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0572:    /* HRREM */  /* FAC|SCE */
              AR &= RMASK;
              BR = AR;
              AR = get_reg(AC);
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                  goto last;
              }
              break;

    case 0573:    /* HRRES */  /* SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0574:    /* HLRE  */  /* SWAR|SAC|FCE */
              if (Mem_read(0, 0, 0, 0))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0575:    /* HLREI */  /* SWAR|SAC */
              AR &= RMASK;
              AR = SWAP_AR;
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              set_reg(AC, AR);
              break;

    case 0576:    /* HLREM */  /* SWAR|FAC|SCE */
              AR &= RMASK;
              BR = AR;
              AR = get_reg(AC);
              AR = SWAP_AR;
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              break;

    case 0577:    /* HLRES */  /* SWAR|SACZ|FCEPSE */
              if (Mem_read(0, 0, 0, 1))
                  goto last;
              AR = MB;
              AR = SWAP_AR;
              AD = ((AR & RSIGN) != 0) ? LMASK: 0;
              AR = AD | (AR & RMASK);
              MB = AR;
              if (Mem_write(0, 0)) {
                 goto last;
              }
              if (AC != 0)
                  set_reg(AC, AR);
              break;

    case 0600:     /* TRN  */ /* FBR */
    case 0601:     /* TLN  */ /* FBR|SWAR */
    case 0602:     /* TRNE */ /* FBR */
    case 0603:     /* TLNE */ /* FBR|SWAR */
    case 0604:     /* TRNA */ /* FBR */
    case 0605:     /* TLNA */ /* FBR|SWAR */
    case 0606:     /* TRNN */ /* FBR */
    case 0607:     /* TLNN */ /* FBR|SWAR */
    case 0610:     /* TDN  */ /* FBR|FCE */
    case 0611:     /* TSN  */ /* FBR|SWAR|FCE */
    case 0612:     /* TDNE */ /* FBR|FCE */
    case 0613:     /* TSNE */ /* FBR|SWAR|FCE */
    case 0614:     /* TDNA */ /* FBR|FCE */
    case 0615:     /* TSNA */ /* FBR|SWAR|FCE */
    case 0616:     /* TDNN */ /* FBR|FCE */
    case 0617:     /* TSNN */ /* FBR|SWAR|FCE */
    case 0620:     /* TRZ  */ /* FBR|SAC */
    case 0621:     /* TLZ  */ /* FBR|SWAR|SAC */
    case 0622:     /* TRZE */ /* FBR|SAC */
    case 0623:     /* TLZE */ /* FBR|SWAR|SAC */
    case 0624:     /* TRZA */ /* FBR|SAC */
    case 0625:     /* TLZA */ /* FBR|SWAR|SAC */
    case 0626:     /* TRZN */ /* FBR|SAC */
    case 0627:     /* TLZN */ /* FBR|SWAR|SAC */
    case 0630:     /* TDZ  */ /* FBR|SAC|FCE */
    case 0631:     /* TSZ  */ /* FBR|SWAR|SAC|FCE */
    case 0632:     /* TDZE */ /* FBR|SAC|FCE */
    case 0633:     /* TSZE */ /* FBR|SWAR|SAC|FCE */
    case 0634:     /* TDZA */ /* FBR|SAC|FCE */
    case 0635:     /* TSZA */ /* FBR|SWAR|SAC|FCE */
    case 0636:     /* TDZN */ /* FBR|SAC|FCE */
    case 0637:     /* TSZN */ /* FBR|SWAR|SAC|FCE */
    case 0640:     /* TRC  */ /* FBR|SAC */
    case 0641:     /* TLC  */ /* FBR|SWAR|SAC */
    case 0642:     /* TRCE */ /* FBR|SAC */
    case 0643:     /* TLCE */ /* FBR|SWAR|SAC */
    case 0644:     /* TRCA */ /* FBR|SAC */
    case 0645:     /* TLCA */ /* FBR|SWAR|SAC */
    case 0646:     /* TRCN */ /* FBR|SAC */
    case 0647:     /* TLCN */ /* FBR|SWAR|SAC */
    case 0650:     /* TDC  */ /* FBR|SAC|FCE */
    case 0651:     /* TSC  */ /* FBR|SWAR|SAC|FCE */
    case 0652:     /* TDCE */ /* FBR|SAC|FCE */
    case 0653:     /* TSCE */ /* FBR|SWAR|SAC|FCE */
    case 0654:     /* TDCA */ /* FBR|SAC|FCE */
    case 0655:     /* TSCA */ /* FBR|SWAR|SAC|FCE */
    case 0656:     /* TDCN */ /* FBR|SAC|FCE */
    case 0657:     /* TSCN */ /* FBR|SWAR|SAC|FCE */
    case 0660:     /* TRO  */ /* FBR|SAC */
    case 0661:     /* TLO  */ /* FBR|SWAR|SAC */
    case 0662:     /* TROE */ /* FBR|SAC */
    case 0663:     /* TLOE */ /* FBR|SWAR|SAC */
    case 0664:     /* TROA */ /* FBR|SAC */
    case 0665:     /* TLOA */ /* FBR|SWAR|SAC */
    case 0666:     /* TRON */ /* FBR|SAC */
    case 0667:     /* TLON */ /* FBR|SWAR|SAC */
    case 0670:     /* TDO  */ /* FBR|SAC|FCE */
    case 0671:     /* TSO  */ /* FBR|SWAR|SAC|FCE */
    case 0672:     /* TDOE */ /* FBR|SAC|FCE */
    case 0673:     /* TSOE */ /* FBR|SWAR|SAC|FCE */
    case 0674:     /* TDOA */ /* FBR|SAC|FCE */
    case 0675:     /* TSOA */ /* FBR|SWAR|SAC|FCE */
    case 0676:     /* TDON */ /* FBR|SAC|FCE */
    case 0677:     /* TSON */ /* FBR|SWAR|SAC|FCE */

              /* Load pseudo registers based on Opcode */
              if (IR & 010) {
                  if (Mem_read(0, 0, 0, 0))
                      goto last;
                  AR = MB;
              } else
                 AR &= RMASK;

              if (IR & 01) {
                  AR = SWAP_AR;
              }


              switch (IR & 060) {
              case 0:
                       break;
              case 020:
                       MQ = CM(AR) & BR;   /* Z */
                       break;
              case 040:
                       MQ = AR ^ BR;       /* C */
                       break;
              case 060:
                       MQ = AR | BR;       /* O */
                          break;
              }
              AR &= BR;
              f = ((AR == 0) & ((IR >> 1) & 1)) ^ ((IR >> 2) & 1);
              if (f) {
                  PC_CHANGE
                  PC = (PC + 1) & RMASK;
              }
              AR = MQ;
              if ((IR & 060) != 0)
                  set_reg(AC, AR);
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
#if KI | KL
              if (!pi_cycle && ((((FLAGS & (USER|USERIO)) == USER) && (IR & 040) == 0)
                    || (((FLAGS & (USER|PUBLIC)) == PUBLIC) && (IR & 076) != 0))) {

#elif PDP6
              if ((FLAGS & USER) != 0 && user_io == 0 && !pi_cycle) {
#else
              if ((FLAGS & (USER|USERIO)) == USER && !pi_cycle) {
#endif
                  /* User and not User I/O */
                  goto muuo;
              } else {
#if KS
                  int ctl = (int)((MB >> 18) & 017);
                  double us;
                  AB = AR & RMASK;

                  switch (IR & 077) {
                  case 000:       /* APR0 */
                           switch (AC) {
                           /* 7000 */
                           case 000:            /* APRID */
                                 /* APRID */
                                 MB = SMASK | BIT3 | BIT4 | BIT5 | (270LL << 18);   /* MC level 270 */
                                 /* Bit 0 Inhibit CST Update available */
                                 /* Bit 1 No CST at all */
                                 /* Bit 2 Exotic microcode */
                                 /* Bit 3 UBABLT Instructions present */
                                 /* Bit 4 KI style paging */
                                 /* Bit 5 for TOPS-20 paging */
#if KS_ITS
                                 if (QITS)
                                     MB |= BIT2;
#endif
                                 MB |= (uint64)((apr_serial == -1) ? DEF_SERIAL : apr_serial);
                                 sim_debug(DEBUG_DATAIO, &cpu_dev, "APRID %012llo\n", MB);
                                 if (Mem_write(0, 0))
                                    goto last;
                                 AR = MB;
                                 break;

                           /* 70020 */
                           case 004:            /* WRAPR */
                                 /* Set trap conditions */
                                 /* Bit 20  100000    Enable interrupts */
                                 /* Bit 21  040000    Disable interrupts */
                                 /* Bit 22  020000    Clear pending */
                                 /* Bit 23  010000    Set irq */
                                 /* Bit 24  004000    Flag 24 */
                                 /* Bit 25  002000    Wake up front end */
                                 /* Bit 26  001000    Power fail interrupt */
                                 /* Bit 27  000400    NXM */
                                 /* Bit 28  000200    Hard memory error */
                                 /* Bit 29  000100    Soft memory error */
                                 /* Bit 30  000040    Timer interrupt */
                                 /* Bit 31  000020    Interrupt from Front end */
                                 /* Bit 32  000010    Generate interrupt request */
                                 /*         000007    PIA */
                                 apr_irq = AR & 07;
                                 clr_interrupt(0);
                                 if (AR & 0100000) {  /* Enable interrupts */
                                     irq_enable |= 07760 & AR;
                                 }
                                 if (AR & 0040000) {  /* Disable interrupts */
                                     irq_enable &= ~(07760 & AR);
                                 }
                                 if (AR & 0020000) {   /* Clear interrupt */
                                     irq_flags &= ~(05760 & AR);
                                 }
                                 if (AR & 0010000) {   /* Set interrupt */
                                     irq_flags |= (05760 & AR);
                                     if (AR & 02000)
                                        cty_wakeup();
                                 }
                                 check_apr_irq();
                                 sim_debug(DEBUG_CONO, &cpu_dev, "WRAPR %012llo\n", AR);
                                 break;

                           /* 70024 */
                           case 005:            /* RDAPR */
                                 /* Read trap conditions */
                                 MB = irq_flags | apr_irq;
                                 MB |= ((uint64)irq_enable) << 18;
                                 if (irq_flags & irq_enable)
                                     MB |= 010;
                                 if (Mem_write(0, 0))
                                     goto last;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDAPR %012llo\n", MB);
                                 AR = MB;
                                 break;

                           /* 70030 */
                           case 006:            /* CONSZ APR */
                                 /* Read trap conditions */
                                 BR = irq_flags | apr_irq;
                                 BR |= ((uint64)irq_enable) << 18;
                                 if (irq_flags & irq_enable)
                                     BR |= 010;
                                 BR = (BR & AR) & RMASK;
                                 if (BR == 0)
                                     PC = (PC + 1) & RMASK;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "CONSZ %012llo\n", AR);
                                 break;

                           /* 70034 */
                           case 007:            /* CONSO APR */
                                 /* Read trap conditions */
                                 BR = irq_flags | apr_irq;
                                 BR |= ((uint64)irq_enable) << 18;
                                 if (irq_flags & irq_enable)
                                     BR |= 010;
                                 BR = (BR & AR) & RMASK;
                                 if (BR != 0)
                                     PC = (PC + 1) & RMASK;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "CONSO %012llo\n", AR);
                                 break;

                           /* 70060 */
                           case 014:            /* WRPI */
                                 /* Set PI flags */
                                 if (AR & 010000) { /* Bit 23 */
                                    PIR = PIH = PIE = 0;
                                    pi_enable = 0;
                                    parity_irq = 0;
                                 }
                                 if (AR & 0200) {  /* Bit 28 */
                                    pi_enable = 1;
                                 }
                                 if (AR & 0400)    /* Bit 27 */
                                    pi_enable = 0;
                                 if (AR & 01000) { /* Bit 26 */
                                    PIE &= ~(AR & 0177);
                                 }
                                 if (AR & 02000)   /* Bit 25 */
                                    PIE |= (AR & 0177);
                                 if (AR & 04000) { /* Bit 24 */
                                    PIR |= (AR & 0177);
                                    pi_pending = 1;
                                 }
                                 if (AR & 020000) { /* Bit 22 */
                                    PIR &= ~(AR & 0177);
                                 }
                                 check_apr_irq();
                                 sim_debug(DEBUG_IRQ, &cpu_dev, "WRPI %012llo\n", AR);
                                 break;

                           /* 70064 */
                           case 015:            /* RDPI */
                                 MB = PIE;
                                 MB |= (pi_enable << 7);
                                 MB |= (PIH << 8);
                                 MB |= ((uint64)(PIR) << 18);
                                 MB |= (parity_irq << 15);
                                 sim_debug(DEBUG_IRQ, &cpu_dev, "RDPI %012llo\n", MB);
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70070 */
                           case 016:            /* CONSZ PI */
                                 /* Read PI conditions */
                                 BR = PIE;
                                 BR |= (pi_enable << 7);
                                 BR |= (PIH << 8);
                                 BR = (BR & AR) & RMASK;
                                 if (BR == 0)
                                     PC = (PC + 1) & RMASK;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "CONSZ PI %012llo\n", AR);
                                 break;

                           /* 70074 */
                           case 017:            /* CONSO PI */
                                 /* Read PI conditions */
                                 BR = PIE;
                                 BR |= (pi_enable << 7);
                                 BR |= (PIH << 8);
                                 BR = (BR & AR) & RMASK;
                                 if (BR != 0)
                                     PC = (PC + 1) & RMASK;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "CONSO PI %012llo\n", AR);
                                 break;

                           default:
                                goto muuo;
                           }
                           break;

                  case 001:       /* APR1 */
                           switch (AC) {
                           case 000:            /* CLRSCH */
                                 break;

                           /* 70104 */
                           case 001:            /* RDUBR */
#if KS_ITS
                                 if (QITS)
                                     MB = ub_ptr & 03777777;
                                 else
#endif
                                 MB = (ub_ptr >> 9);
                                 /* Set previous section */
                                 MB |= ((uint64)(prev_ctx & 0160)) << 20;
                                 MB |= ((uint64)(fm_sel & 0160)) << 23;
                                 MB |= SMASK|BIT2;
                                 sim_debug(DEBUG_DATAIO, &cpu_dev, "RDUBR %012llo\n", MB);
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70110 */
                           case 002:            /* CLRPT */
                                 f = (RMASK & AB) >> 9;
                                 /* Map the page */
                                 u_tlb[f] = 0;
                                 e_tlb[f] = 0;
                                 /* If not user do exec mappping */
                                 if (!t20_page && (f & 0740) == 0340) {
                                    /* Pages 340-377 via UBT */
                                    f += 01000 - 0340;
                                    u_tlb[f] = 0;
                                 }
                                 break;

                           /* 70114 */
                           case 003:            /* WRUBR */
                                 if (Mem_read(0, 0, 0, 0))
                                    goto last;
                                 if (MB & SMASK) {
                                     fm_sel = (uint8)(MB >> 23) & 0160;
                                     prev_ctx = (uint8)(MB >> 20) & 0160;
                                 }
                                 if (MB & BIT2) {
#if KS_ITS
                                     if (QITS)
                                        ub_ptr = MB & 03777777;
                                     else
#endif
                                     ub_ptr = (MB & 03777) << 9;
                                     for (f = 0; f < 512; f++) {
                                        u_tlb[f] = 0;
                                        e_tlb[f] = 0;
                                     }
                                     for (;f < 546; f++)
                                        u_tlb[f] = 0;
                                 }
                                 sim_debug(DEBUG_DATAIO, &cpu_dev,
                                          "WRUBR  %012llo ebr=%06o ubr=%06o\n",
                                          MB, eb_ptr, ub_ptr);
                                 break;

                           /* 70120 */
                           case 004:            /* WREBR */
                                 eb_ptr = (AR & 03777) << 9;
                                 for (f = 0; f < 512; f++) {
                                     e_tlb[f] = 0;
                                     u_tlb[f] = 0;
                                 }
                                 for (;f < 546; f++)
                                     u_tlb[f] = 0;
                                 page_enable = (AR & 020000) != 0;
                                 t20_page = (AR & 040000) != 0;
                                 page_fault = 0;
                                 sim_debug(DEBUG_CONO, &cpu_dev, "WREBR %012llo\n", AR);
                                 break;

                           /* 70124 */
                           case 005:            /* RDEBR */
                                 MB = (eb_ptr >> 9);
                                 if (page_enable)
                                     MB |= 020000;
                                 if (t20_page)
                                     MB |= 040000;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDEBR %012llo\n", MB);
                                 if (Mem_write(0, 0))
                                    goto last;
                                 AR = MB;
                                 break;
#if KS_ITS
                           /* 70144 */
                           case 011:        /* RDPCST */
                                 if (QITS) {
                                     MB = pcst;
                                     if (Mem_write(0, 0))
                                          goto last;
                                     break;
                                 }
                                 /* Fall through */

                           /* 70154 */
                           case 013:        /* WRPCST */
                                 if (QITS) {
                                     if (Mem_read(0, 0, 0, 0))
                                          goto last;
                                     pcst = MB;
                                     break;
                                 }
#endif
                           default:
                                 goto muuo;
                           }
                           break;

                  case 002:       /* APR2 */
                           switch (AC) {
                           /* 70200 */
                           case 000:            /* RDSPB */    /* ITS SDBR1 */
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDSPD %012llo\n", spt);
                                 MB = spt;
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70204 */
                           case 001:            /* RDCSB */    /* ITS SDBR2 */
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDSPD %012llo\n", cst);
                                 MB = cst;
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70250 */
                           case 002:            /* RDPUR */    /* ITS SDBR3 */
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDSPD %012llo\n", cst_dat);
                                 MB = cst_dat;
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70214 */
                           case 003:            /* RDCSTM */   /* ITS SDBR4 */
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDSPD %012llo\n", cst_msk);
                                 MB = cst_msk;
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70220 */
                           case 004:            /* RDTIME */
                                 MB = tim_high;
                                 if (Mem_write(0, 0))
                                    goto last;
                                 us = sim_activate_time_usecs (&cpu_unit[0]);
                                 f = 2000 - ((int)us);
                                 MB = tim_low | (f << 2);
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDTIME %012llo %012llo\n", MB, tim_high);
                                 AB = (AB + 1) & RMASK;
                                 if (Mem_write(0, 0))
                                    goto last;
                                 AR = MB;
                                 break;

                           /* 70224 */
                           case 005:            /* RDINT */
                                 MB = int_val;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDINT %012llo\n", MB);
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70230 */
                           case 006:            /* RDHSB */
                                 MB = hsb;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "RDHSB %012llo\n", MB);
                                 if (Mem_write(0, 0))
                                     goto last;
                                 AR = MB;
                                 break;

                           /* 70234 */
                           case 007:            /* ITS SPM */
                                 if (QITS) {
                                     MB = dbr1;
                                     if (Mem_write(0, 0))
                                        goto last;
                                     AB = (AB + 1) & RMASK;
                                     MB = dbr2;
                                     if (Mem_write(0, 0))
                                        goto last;
                                     AB = (AB + 1) & RMASK;
                                     MB = qua_time;
                                     if (Mem_write(0, 0))
                                        goto last;
                                 }
                                 break;

                           /* 70240 */
                           case 010:            /* WRSPB */   /* ITS LDBR1 */
#if KS_ITS
                                 if (QITS) {
                                     dbr1 = AB;
                                     for (f = 0; f < 512; f++) {
                                        u_tlb[f] = 0;
                                        e_tlb[f] = 0;
                                     }
                                     sim_debug(DEBUG_CONI, &cpu_dev, "WRDBR1 %012llo\n", dbr1);
                                     break;
                                 }
#endif
                                 if (Mem_read(0, 0, 0, 0))
                                    goto last;
                                 spt = MB;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "WRSPB %012llo\n", spt);
                                 break;

                           /* 70244 */
                           case 011:            /* WRCSB */   /* ITS LDBR2 */
#if KS_ITS
                                 if (QITS) {
                                     dbr2 = AB;
                                     for (f = 0; f < 512; f++) {
                                        u_tlb[f] = 0;
                                        e_tlb[f] = 0;
                                     }
                                     sim_debug(DEBUG_CONI, &cpu_dev, "WRDBR2 %012llo\n", dbr2);
                                     break;
                                 }
#endif
                                 if (Mem_read(0, 0, 0, 0))
                                     goto last;
                                 cst = MB;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "WRCSB %012llo\n", cst);
                                 break;

                           /* 70250 */
                           case 012:            /* WRPUR */   /* ITS LDBR3 */
#if KS_ITS
                                 if (QITS) {
                                     dbr3 = AB;
                                     for (f = 0; f < 512; f++) {
                                        u_tlb[f] = 0;
                                        e_tlb[f] = 0;
                                     }
                                     sim_debug(DEBUG_CONI, &cpu_dev, "WRDBR3 %012llo\n", dbr3);
                                     break;
                                 }
#endif
                                 if (Mem_read(0, 0, 0, 0))
                                     goto last;
                                 cst_dat = MB;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "WRPUR %012llo\n", cst_dat);
                                 break;

                           /* 70254 */
                           case 013:            /* WRCSTM */   /* ITS LDBR4 */
#if KS_ITS
                                 if (QITS) {
                                     dbr4 = AB;
                                     for (f = 0; f < 512; f++) {
                                        u_tlb[f] = 0;
                                        e_tlb[f] = 0;
                                     }
                                     sim_debug(DEBUG_CONI, &cpu_dev, "WRDBR4 %012llo\n", dbr4);
                                     break;
                                 }
#endif
                                 if (Mem_read(0, 0, 0, 0))
                                     goto last;
                                 cst_msk = MB;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "WRCSTM %012llo\n", cst_msk);
                                 break;

                           /* 70264 */
                           case 015:            /* WRINT */
                                 if (Mem_read(0, 0, 0, 0))
                                    goto last;
                                 int_val = MB & ~07777;
                                 sim_debug(DEBUG_DATAIO, &cpu_dev, "WRINT %012llo\n", int_val);
                                 break;

                           /* 70260 */
                           case 014:            /* WRTIME */
                                 if (Mem_read(0, 0, 0, 0))
                                    goto last;
                                 tim_high = MB;
                                 AB = (AB + 1) & RMASK;
                                 if (Mem_read(0, 0, 0, 0))
                                    goto last;
                                 tim_low = MB & ~07777;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "WRTIME %012llo %012llo\n", tim_low, tim_high);
                                 break;
                           /* 70270 */
                           case 016:            /* WRHSB */
                                 if (Mem_read(0, 0, 0, 0))
                                    goto last;
                                 hsb = MB;
                                 sim_debug(DEBUG_CONI, &cpu_dev, "WRHSB %012llo\n", MB);
                                 break;

                           /* 70274 */
                           case 017:           /* ITS LPMR */
#if KS_ITS
                                 if (QITS) {
                                     if (Mem_read(0, 0, 0, 0))
                                        goto last;
                                     dbr1 = MB;
                                     AB = (AB + 1) & RMASK;
                                     if (Mem_read(0, 0, 0, 0))
                                        goto last;
                                     dbr2 = MB;
                                     AB = (AB + 1) & RMASK;
                                     if (Mem_read(0, 0, 0, 0))
                                        goto last;
                                     qua_time = MB;
                                     for (f = 0; f < 512; f++) {
                                        u_tlb[f] = 0;
                                        e_tlb[f] = 0;
                                     }
                                     for (;f < 546; f++)
                                        u_tlb[f] = 0;
                                     break;
                                 }
#endif
                           default:
                                 goto muuo;
                           }
                           break;

                          /* 70400 */
                  case 004:       /* UMOVE */
                           xct_flag = 4;
                           AB &= RMASK;
                           if (Mem_read(0, 0, 0, 0))
                              goto last;
                           AR = MB;
                           set_reg(AC, AR);
                           xct_flag = 0;
                           break;

                          /* 70500 */
                  case 005:       /* UMOVEM */
                           MB = BR;
                           AB &= RMASK;
                           xct_flag = 4;
                           if (Mem_write(0, 0))
                              goto last;
                           xct_flag = 0;
                           break;

                  case 010:       /* TIOE , ITS RDIOI */
#if KS_ITS
                           if (QITS) {
                               ctl = 3;
                               goto its_rd;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, WORD)) {
io_fault:
                               fault_data = (020LL << 30) | BIT8 | BIT10;
                               fault_data |= (uint64)((020 & IR) << 18);
                               fault_data |= AB | (ctl << 18);
                               page_fault = 1;
                               goto last;
                           }
                           BR &= 0177777;
                           if ((BR & MB) == 0)
                               PC = (PC + 1) & RMASK;
                           AR = MB;
                           break;

                  case 011:       /* TION, ITS RDIOQ  */
#if KS_ITS
                           if (QITS) {
                               ctl = 1;
                               goto its_rd;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, WORD))
                               goto io_fault;
                           BR &= 0177777;
                           if ((BR & MB) != 0)
                               PC = (PC + 1) & RMASK;
                           AR = MB;
                           break;

                  case 012:       /* RDIO */
#if KS_ITS
                           if (QITS) {
                               if (Mem_read(pi_cycle, 0, 0, 0))
                                   goto last;
                               AB = MB & RMASK;
                               ctl = (int)((MB >> 18) & 017);
                           }
its_rd:
#endif
                           if (uba_read(AB, ctl, &AR, WORD))
                               goto io_fault;
                           set_reg(AC, AR);
                           break;

                  case 013:       /* WRIO */
#if KS_ITS
                           if (QITS) {
                               if (Mem_read(pi_cycle, 0, 0, 0))
                                   goto last;
                               AB = MB & RMASK;
                               ctl = (int)((MB >> 18) & 017);
                           }
its_wr:
#endif
                           if (uba_write(AB, ctl, BR, WORD))
                               goto io_fault;
                           break;

                  case 014:       /* BSIO, ITS WRIOI */
#if KS_ITS
                           if (QITS) {
                               ctl = 3;
                               if (AB == 0777000)
                                   break;
                               goto its_wr;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, WORD))
                               goto io_fault;
                           MB |= BR;
                           if (uba_write(AB, ctl, MB, WORD))
                               goto io_fault;
                           AR = MB;
                           break;

                  case 015:       /* BCIO, ITS WRIOQ */
#if KS_ITS
                           if (QITS) {
                               ctl = 1;
                               goto its_wr;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, WORD))
                               goto io_fault;
                           MB &= ~BR;
                           if (uba_write(AB, ctl, MB, WORD))
                               goto io_fault;
                           AR = MB;
                           break;

                  case 016:       /* BLTBU */
                  case 017:       /* BLTUB */
                           AR = get_reg(AC);
                           BR = AB;
                           /* Precompute end of transfer address */
                           AD = (CM(AR) + BR + 1) & RMASK;
                           AD = ((AR + (AD << 18)) & LMASK) | ((AR + AD) & RMASK);
                           set_reg(AC, AOB(AD));
                           do {
                               AIO_CHECK_EVENT;                /* queue async events */
                               if (sim_interval <= 0) {
                                   if ((reason = sim_process_event()) != SCPE_OK) {
                                       f_pc_inh = 1;
                                       set_reg(AC, AR);
                                       break;
                                   }
                                   /* Allow for interrupt */
                                   if (pi_pending) {
                                       pi_rq = check_irq_level();
                                       if (pi_rq) {
                                           f_pc_inh = 1;
                                           set_reg(AC, AR);
                                           break;
                                       }
                                   }
                               }
                               AB = (AR >> 18) & RMASK;
                               BYF5 = 1;
                               if (Mem_read(0, 0, 0, 0)) {
                                    BYF5 = 0;
                                    f_pc_inh = 1;
                                    set_reg(AC, AR);
                                    goto last;
                               }
#define BMASK1  0776000000000LL
#define BMASK2  0001774000000LL
#define BMASK3  0000003770000LL
#define BMASK4  0000000007760LL
                               if (IR & 1) {
                                   MB = ((MB << 10) & BMASK1) |
                                        ((MB >>  6) & BMASK2) |
                                        ((MB << 12) & BMASK3) |
                                        ((MB >>  4) & BMASK4);
                               } else {
                                   MB = ((MB & BMASK1) >> 10) |
                                        ((MB & BMASK2) <<  6) |
                                        ((MB & BMASK3) >> 12) |
                                        ((MB & BMASK4) <<  4);
                               }
                               AB = (AR & RMASK);
                               BYF5 = 0;
                               if (Mem_write(0, 0)) {
                                    f_pc_inh = 1;
                                    set_reg(AC, AR);
                                    goto last;
                               }
                               AD = (AR & RMASK) + CM(BR) + 1;
                               AR = AOB(AR);
                           } while ((AD & C1) == 0);
                           break;

                  case 020:       /* TIOEB */
#if KS_ITS
                           if (QITS) {
                               ctl = 3;
                               goto its_rdb;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, BYTE))
                               goto io_fault;
                           BR &= 0377;
                           if ((BR & MB) == 0)
                               PC = (PC + 1) & RMASK;
                           AR = MB;
                           break;

                  case 021:       /* TIONB */
#if KS_ITS
                           if (QITS) {
                               ctl = 1;
                               goto its_rdb;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, BYTE))
                               goto io_fault;
                           BR &= 0377;
                           if ((BR & MB) != 0)
                               PC = (PC + 1) & RMASK;
                           break;

                  case 022:       /* RDIOB */
#if KS_ITS
                           if (QITS) {
                               if (Mem_read(pi_cycle, 0, 0, 0))
                                   goto last;
                               AB = MB & RMASK;
                               ctl = (int)((MB >> 18) & 017);
                           }
its_rdb:
#endif
                           if (uba_read(AB, ctl, &AR, BYTE))
                               goto io_fault;
                           set_reg(AC, AR);
                           break;

                  case 023:       /* WRIOB */
#if KS_ITS
                           if (QITS) {
                               if (Mem_read(pi_cycle, 0, 0, 0))
                                   goto last;
                               AB = MB & RMASK;
                               ctl = (int)((MB >> 18) & 017);
                           }
its_wrb:
#endif
                           if (uba_write(AB, ctl, BR, BYTE))
                               goto io_fault;
                           break;

                  case 024:       /* BSIOB */
#if KS_ITS
                           if (QITS) {
                               ctl = 3;
                               goto its_wrb;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, BYTE))
                               goto io_fault;
                           MB |= BR;
                           if (uba_write(AB, ctl, MB, BYTE))
                               goto io_fault;
                           AR = MB;
                           break;

                  case 025:       /* BCIOB */
#if KS_ITS
                           if (QITS) {
                               ctl = 1;
                               goto its_wrb;
                           }
#endif
                           if (uba_read(AB, ctl, &MB, BYTE))
                               goto io_fault;
                           MB &= ~(BR);
                           if (uba_write(AB, ctl, MB, BYTE))
                               goto io_fault;
                           AR = MB;
                           break;

                  default:
                       goto muuo;
                  }
#else
                  int d = ((IR & 077) << 1) | ((AC & 010) != 0);
                  AR &= RMASK;
#if KL
                  if (d == 3) {
                      irq_flags |= SWP_DONE;
                      goto last;
                  }
#endif
fetch_opr:
                  switch(AC & 07) {
                  case 0:     /* 00 BLKI */
#if KL
                          /* For KL10 special devices */
                          if (d <= 05) {
                              double us;
                              switch (d) {
                              case 0:    /* APR */
                                  /* BLKI APRID */
                                  AR = SMASK| (500LL << 18);   /* MC level 500 */
                                  /* Bit 0 for TOPS-20 paging */
                                  /* Bit 1 for extended addressing */
                                  /* Bit 2 Exotic microcode */
                                  /* Bit 3 KL10B */
                                  /* Bit 4 PMOVE/PMOVEM or ITS Style Paging */
                                  /* Bit 5 Tops-20 R5 microcode */
#if KL_ITS
                                  if (QITS)
                                      AR |= BIT4;
#endif
                                  /* Bit 18 50hz      */
                                  /* Bit 19 Cache     */
                                  /* Bit 20 Channel?  */
                                  /* Bit 21 Extended KL10 */
                                  /* Bit 22 Master Osc */
                                  if (QKLB)
                                      AR |= BIT1|BIT4|040000;
                                  AR |= (uint64)((apr_serial == -1) ? DEF_SERIAL : apr_serial);
                                  sim_debug(DEBUG_DATAIO, &cpu_dev, "APRID BLKI %012llo\n", MB);
                                  MB = AR;
                                  if (Mem_write(pi_cycle, 0))
                                      goto last;
                                  break;

                              case 1:    /* PI */
                                  /* BLKI RDERA */
                              case 2:    /* PAG */
                              case 3:    /* CCA */
                                  MB = 0;
                                  if (Mem_write(pi_cycle, 0))
                                      goto last;
                                  break;

                              case 4:    /* TIM */
                                  /* BLKI RDPERF */
                                  /* Read process execution time */
                                  us = sim_activate_time_usecs (&cpu_unit[0]);
                                  f = rtc_tim - ((int)us);
                                  update_times(f);
                                  rtc_tim = ((int)us);
                                  if (page_enable)  {
                                      AR = (M[ub_ptr + 0505]);
                                      BR = M[ub_ptr + 0504];
                                  } else {
                                      AR = 0 << 12;
                                      BR = f;
                                  }
                                  sim_debug(DEBUG_DATAIO, &cpu_dev, "RDPERF %012llo %012llo\n", AR, BR);
                                  MB = AR;
                                  if (Mem_write(pi_cycle, 0))
                                      goto last;
                                  AB = (AB + 1) & RMASK;
                                  MB = BR;
                                  if (Mem_write(pi_cycle, 0))
                                      goto last;
                                  break;
                              case 5:    /* MTR */
                                  /* BLKI  RDMACT */
                                  /* RDMACT */
                                  /* Read memory accounting */
                                  if (page_enable)  {
                                      AR = M[ub_ptr + 0507];
                                      BR = (M[ub_ptr + 0506] & CMASK);
                                  } else {
                                      AR = 0 << 12;
                                      BR = 0;
                                  }
                                  sim_debug(DEBUG_DATAIO, &cpu_dev, "RDMACT %012llo %012llo\n", AR, BR);
                                  MB = AR;
                                  if (Mem_write(pi_cycle, 0))
                                      goto last;
                                  AB = (AB + 1) & RMASK;
                                  MB = BR;
                                  if (Mem_write(pi_cycle, 0))
                                      goto last;
                                  MB = AR;
                                  break;
                              }
                              break;
                          }
                          /* Fall through */
#endif

                  case 2:     /* 10 BLKO */

#if KL
                         /* For special KL devices */
                          if (d <= 05) {
                              int  ctl;
                              int  fcn;
                              int  page;

                              switch (d) {
                              case 0:    /* APR */
                                    /* BLKO WRFIL */
                                    break;

                              case 1:    /* PI */
                                    /* BLKO SBDIAG */
                                    if (Mem_read(pi_cycle, 0, 0, 0))
                                        goto last;
                                    AB = (AB + 1) & RMASK;
                                    ctl = (int)(MB >> 31);
                                    fcn = (int)(MB & 037);
                                    if (ctl >= 010 && ctl < 030) {
                                        int mc = MEMSIZE / (512 * 1024);
                                        int c = (ctl - 010);
                                        int s = 0;
                                        if (c < mc) {
                                            switch(fcn) {
                                            case 0:  MB = 06000000000LL; break;
                                            case 1:  MB = 00500000000LL; break;
                                            case 2:  switch ((MB >> 21) & 077) {
                                                     case 000:  /* yyyyWWWW */
                                                     case 001:  /* ww###### */
                                                     case 002:  /* #####ppA */
                                                                MB = (ctl << 3) + 06;
                                                                break;
                                                     case 003:  /* bNNSSmmm */
                                                                MB = 0;
                                                                break;
                                                     }
                                                     break;
                                            case 012:
                                                   s = ((int)(0176000 & MB)) / 8192;
                                                   MB = 0;
                                                   if (s > mc || c != s)
                                                       MB = 010000000LL;
                                                   break;
                                            default: MB = 0; break;
                                            }
                                        } else {
                                            MB = 0;
                                        }
                                        if (Mem_write(pi_cycle, 0))
                                           goto last;
                                    }
                                    break;

                              case 2:    /* PAG */
                                    /* BLKO CLRPT */
                                    page = (RMASK & AB) >> 9;

                                    page &= ~7;
                                    /* Map the page */
                                    for(f = 0; f < 8; f++) {
                                       u_tlb[page+f] = 0;
                                       e_tlb[page+f] = 0;
                                    }
                                    /* If not user do exec mappping */
                                    if (!t20_page && (page & 0740) == 0340) {
                                       /* Pages 340-377 via UBT */
                                       page += 01000 - 0340;
                                       for(f = 0; f < 8; f++)
                                          u_tlb[page+f] = 0;
                                    }
                                    break;

                              case 3:    /* CCA */
                                  /* BLKO SWPVA */
                              case 4:    /* TIM */
                                  /* BLKO WRPAE */
                              case 5:    /* MTR */
                                    break;
                              }
                              break;
                          }

#endif
                          if (Mem_read(pi_cycle, 0, 0, 0))
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
#if KL
                          /* For KL10 special devices */
                          if (d == 4) { /* DATAI TIM is two words */
                              /* DATAI RDTIME */
                              double us = sim_activate_time_usecs (&cpu_unit[0]);
                              f = rtc_tim - ((int)us);
                              update_times(f);
                              rtc_tim = ((int)us);
                              if (page_enable)  {
                                  AR = (M[eb_ptr + 0510]);
                                  BR = M[eb_ptr + 0511];
                              } else {
                                  AR = 0;
                                  BR = f << 12;
                              }
                              sim_debug(DEBUG_DATAIO, &cpu_dev, "RDTIM %012llo, %012llo\n", AR, BR);
                              MB = AR;
                              if (Mem_write(pi_cycle, 0))
                                  goto last;
                              AB = (AB + 1) & RMASK;
                              MB = BR;
                              if (Mem_write(pi_cycle, 0))
                                  goto last;
                              break;
                          }
                          if (d == 5) { /* DATAI MTR is two words */
                              /* RDEACT */
                              /* Read executive accounting */
                              double us = sim_activate_time_usecs (&cpu_unit[0]);
                              f = rtc_tim - ((int)us);
                              update_times(f);
                              rtc_tim = ((int)us);
                              if (page_enable)  {
                                  AR = M[ub_ptr + 0505];
                                  BR = (M[ub_ptr + 0504] & CMASK);
                              } else {
                                  AR = 0;
                                  BR = f << 12;
                              }
                              sim_debug(DEBUG_DATAIO, &cpu_dev, "DATAI MTR %012llo %012llo\n", AR, BR);
                              MB = AR;
                              if (Mem_write(pi_cycle, 0))
                                  goto last;
                              AB = (AB + 1) & RMASK;
                              MB = BR;
                              if (Mem_write(pi_cycle, 0))
                                  goto last;
                              break;
                          }
#endif

                          dev_tab[d](DATAI|(d<<2), &AR);
                          MB = AR;
                          if (Mem_write(pi_cycle, 0))
                              goto last;
                          break;
                  case 3:     /* 14 DATAO */
                          if (Mem_read(pi_cycle, 0, 0, 0))
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
#endif
             }
             break;
    }

    if (hst_lnt) {
        hst[hst_p].fmb = AR;
    }

last:
    modify = 0;
#if BBN
    if (QBBN && page_fault) {
        page_fault = 0;
        AB = 070 + maoff;
        f_pc_inh = 1;
        pi_cycle = 1;
        goto fetch;
    }
#endif
#if KS
    /* Handle page fault and traps */
    if (page_enable && page_fault) {
        page_fault = 0;
        BYF5 = 0;
#if KS_ITS
        if (QITS) {
            AB = eb_ptr + 0440;
            if (pi_act != 0) {
               for(f = 0200; f != 0; f >>= 1) {
                   if (f & pi_act)
                      break;
                   AB += 3;
               }
            }
            if (one_p_arm)
                FLAGS |= ADRFLT;
            one_p_arm = 0;
        } else
#endif
        AB = ub_ptr + 0500;
        MB = fault_data;
        Mem_write_nopage();
        AB++;
        /* If fault on trap, kill the pi_cycle flag */
        if (trap_flag)
           pi_cycle = 0;
        FLAGS |= trap_flag & (TRP1|TRP2);
        trap_flag = (TRP1|TRP2);
        MB = (((uint64)(FLAGS) << 23) & LMASK);
        if (t20_page == 0)
            MB |= (PC & RMASK);
        Mem_write_nopage();
        if (t20_page) {
            AB++;
            MB = (PC & RMASK);
            Mem_write_nopage();
        }
        AB++;
        flag1 = 0;
        if (FLAGS & USER)
            flag1 = 1;
        Mem_read_nopage();
        if (t20_page)
            FLAGS = 0;
        else
            FLAGS = (MB >> 23) & 017777;
        /* If transistioning from user to executive adjust flags */
        if ((FLAGS & USER) == 0) {
            if (flag1)
                FLAGS |= USERIO;
        }
        PC = MB & RMASK;
        xct_flag = 0;
        f_load_pc = 1;
        f_inst_fetch = 1;
        f_pc_inh = 1;
    }
#endif
#if KL
    /* Handle page fault and traps */
    if (page_enable && page_fault) {
        page_fault = 0;
        BYF5 = 0;
#if KL_ITS
        if (QITS) {
            AB = eb_ptr | 0500;
            FM[(6<<4)|0] = fault_data;
        } else
#endif
        AB = ub_ptr | 0500;
        if (!QKLB && !QITS && t20_page)
            AB++;
        MB = fault_data;
        Mem_write_nopage();
        AB++;
        /* If fault on trap, kill the pi_cycle flag */
        if (trap_flag)
           pi_cycle = 0;
        FLAGS |= trap_flag & (TRP1|TRP2);
        trap_flag = (TRP1|TRP2);
        MB = (((uint64)(FLAGS) << 23) & LMASK);
        if ((FLAGS & USER) == 0) {
            MB &= ~SMASK;
            MB |= (FLAGS & PRV_PUB) ? SMASK : 0;
        }
        if (QKLB && t20_page) {
            if ((FLAGS & USER) == 0)
               MB |= (uint64)(prev_sect & 037);
        } else
            MB |= (PC & RMASK);
        Mem_write_nopage();
        AB++;
        if (QKLB && t20_page) {
            MB = (((uint64)pc_sect) << 18) | (PC & RMASK);
            Mem_write_nopage();
            AB++;
        }
        flag1 = flag3 = 0;
        if (FLAGS & PUBLIC)
            flag3 = 1;
        if (FLAGS & USER)
            flag1 = 1;
        Mem_read_nopage();
        if (QKLB && t20_page)
            FLAGS = 0;
        else
            FLAGS = (MB >> 23) & 017777;
        /* If transistioning from user to executive adjust flags */
        if ((FLAGS & USER) == 0) {
            if (flag1)
                FLAGS |= USERIO;
            if (flag3)
                FLAGS |= PRV_PUB;
        }
        PC = MB & RMASK;
        if (QKLB && t20_page)
            pc_sect = (MB >> 18) & 07777;
        xct_flag = 0;
        f_load_pc = 1;
        f_pc_inh = 1;
        f_inst_fetch = 1;
        if (pi_cycle) {
            pi_cycle = 0;
            FM[(7 << 4) | 2] = fault_data;
            pi_enable = 0;
        }
    }
#endif
#if KI
    /* Handle page fault and traps */
    if (page_enable && page_fault) {
        if (pi_cycle) {
            inout_fail = 1;
        }
        page_fault = 0;
        AB = ub_ptr + ((FLAGS & USER) ? 0427 : 0426);
        MB = fault_data;
        Mem_write_nopage();
        FLAGS |= trap_flag & (TRP1|TRP2);
        trap_flag = 1;
        AB = ((FLAGS & USER) ? ub_ptr : eb_ptr) + 0420;
        f_pc_inh = 1;
        pi_cycle = 1;
        Mem_read_nopage();
        goto no_fetch;
    }
#endif


#if KI | KL | KS
    if (!f_pc_inh && (trap_flag == 0)  && !pi_cycle) {
        FLAGS &= ~ADRFLT;
#else
    if (!f_pc_inh && !pi_cycle) {
#endif
        PC = (PC + 1) & RMASK;
    }

#if ITS
    /* Handle 1 proceed for KA ITS */
    if (QITS && one_p_arm && (FLAGS & BYTI) == 0) {
        fault_data |= 02000;
        mem_prot = 1;
        one_p_arm = 0;
        check_apr_irq();
    }
#endif

#if KS_ITS
    /* Handle 1 proceed for KS ITS */
    if (QITS && one_p_arm && (FLAGS & BYTI) == 0) {
        modify = 0;
        extend = 0;
        one_p_arm = 0;
        AB = ub_ptr + 0432;
        /* Save flags */
        MB = (((uint64)(FLAGS) << 23) & LMASK) | (PC & RMASK);
        Mem_write_nopage();
        /* Read in new PC and flags */
        AB ++;
        f = 0;
        if (FLAGS & USER)
            f = 1;
        Mem_read_nopage();

        FLAGS = (MB >> 23) & 017777;
        /* If transistioning from user to executive adjust flags */
        if ((FLAGS & USER) == 0 && f) {
            FLAGS |= USERIO;
        }
        PC = MB & RMASK;
    }
#endif

    /* Dismiss an interrupt */
    if (pi_cycle) {
#if KI | KL | KS
        if (trap_flag != 0) {
            pi_hold = pi_ov = 0;
            f_pc_inh = 0;
            trap_flag = 0;
        }
#endif
       /* Check if I/O and BLKI/O or DATAI/O */
       if ((IR & 0700) == 0700 && ((AC & 04) == 0)) {
           pi_hold = pi_ov;
           if ((!pi_hold) && f_inst_fetch) {
                pi_cycle = 0;
           } else {
                AB = pi_vect | pi_ov;
#if KI | KL
                Mem_read_nopage();
#elif KS
                Mem_read_word(AB, &MB, 1);
#else
                Mem_read(1, 0, 1, 0);
#endif
                goto no_fetch;
           }
       } else if (pi_hold && !f_pc_inh) {
#if KA | KI
            /* Check if I/O, then check if IRQ was raised */
            if ((IR & 0700) == 0700) {
                if (check_irq_level()) {
                    pi_vect = 040 | (pi_enc << 1) | maoff;
                }
            }
#endif
            AB = pi_vect | pi_ov;
            pi_ov = 0;
            pi_hold = 0;
#if KI | KL
            Mem_read_nopage();
#elif KS
            Mem_read_word(AB, &MB, 1);
#else
            Mem_read(1, 0, 1, 0);
#endif
            goto no_fetch;
       } else {
#if KI | KL | KS
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
    if (f_load_pc && !pi_cycle && instr_count != 0 && --instr_count == 0) {
#if ITS
        if (QITS)
            load_quantum();
#endif
        RUN = 0;
        return SCPE_STEP;
    }
}
/* Should never get here */
RUN = 0;
#if ITS
if (QITS)
    load_quantum();
#endif

return reason;
}

#if KL | KS

/* Handle indirection for extended byte instructions */
int
do_byte_setup(int n, int wr, int *pos, int *sz)
{
    uint64    val1;
    uint64    val2;
    uint64    temp;
    int       s;
    int       p;
    int       np;
    int       ix;
    int       ind;

    /* Get pointer */
    val1 = get_reg(n+1);
    val2 = get_reg(n+2);
    /* Extract index */
    *sz = s = (val1 >> 24) & 077;
    p = (val1 >> 30) & 077;
    np = (p + (0777 ^ s) + 1) & 0777;
    /* Advance pointer */
#if KL
    if (QKLB && t20_page) {
        if (p > 36) {  /* Extended pointer */
            int i = p - 37;
            *sz = s = _byte_adj[i].s;
            p = _byte_adj[i].p;
            np = p = (p + (0777 ^ s) + 1) & 0777;
            val2 = val1 & (SECTM|RMASK); /* Convert to long pointer */
            val1 = ((uint64)s << 24) | BIT12;
            if (p & 0400) {
                np = p = ((0777 ^ s) + 044 + 1) & 0777;
                val2 = (val2 & ~(SECTM|RMASK)) | ((val2 + 1) & (SECTM|RMASK));
           }
           ind = 0;
           ix = 0;
           MB = val2 & (SECTM|RMASK);
           sect = (MB >> 18) & 07777;
           glb_sect = 1;
        } else if ((val1 & BIT12) != 0) { /* Full pointer */
            if (np & 0400) {
                np = p = ((0777 ^ s) + 044 + 1) & 0777;
                if (val2 & SMASK)
                    val2 = (val2 & LMASK) | ((val2 + 1) & RMASK);
                else
                    val2 = (val2 & ~(SECTM|RMASK)) | ((val2 + 1) & (SECTM|RMASK));
            }
            if (val2 & SMASK) {
                if (val2 & BIT1) {
                    fault_data = 024LL << 30 | (((FLAGS & USER) != 0)?SMASK:0) |
                                     (val2 & RMASK) | ((uint64)sect << 18);
                    page_fault = 1;
                    return 1;
                }
                ind = TST_IND(val2) != 0;
                ix = GET_XR(val2);
                MB = (val2 & RMASK) | ((val2 & RSIGN)? LMASK:0);
                sect = cur_sect;
                glb_sect = 0;
            } else {
                ind = (val2 & BIT1) != 0;
                ix = (val2 >> 30) & 017;
                MB = val2 & (SECTM|RMASK);
                sect = (MB >> 18) & 07777;
                glb_sect = 1;
            }
        } else {
            if (np & 0400) {
                np = p = ((0777 ^ s) + 044 + 1) & 0777;
                val1 = (val1 & LMASK) | ((val1 + 1) & RMASK);
            }
            ix = GET_XR(val1);
            ind = TST_IND(val1) != 0;
            MB = (val1 & RMASK) | ((val1 & RSIGN)? LMASK:0);
            sect = cur_sect;
            glb_sect = 0;
        }
    } else {
#endif
        if (np & 0400) {
            np = p = ((0777 ^ s) + 044 + 1) & 0777;
            val1 = (val1 & LMASK) | ((val1 + 1) & RMASK);
        }
        ix = GET_XR(val1);
        ind = TST_IND(val1) != 0;
        MB = (val1 & RMASK) | ((val1 & RSIGN)? LMASK:0);
#if KL
        sect = cur_sect;
        glb_sect = 0;
    }
#endif
    *pos = np & 077;

    AB = MB & RMASK;
    if (ix) {
       temp = get_reg(ix);
       /* Check if extended indexing */
#if KL
       if (QKLB && t20_page && glb_sect != 0 && (temp & SMASK) == 0 && (temp & SECTM) != 0) {
           temp = (temp + MB) & (SECTM|RMASK);
           sect = (temp >> 18) & 07777;
           MB = 0;
           glb_sect = 1;
       } else
           glb_sect = 0;
#endif
       temp = MB = (MB + temp) & FMASK;
       AB = MB & RMASK;
    }
    if (ind) {
         if (Mem_read(0, 1, 0, 0)) {
             return 1;
         }
#if KL
         /* Check if extended indexing */
         if (QKLB && sect != 0) {
             if (MB & SMASK) {    /* Instruction format IFIW */
                 if (MB & BIT1) { /* Illegal index word */
                     fault_data = 024LL << 30 | (((FLAGS & USER) != 0)?SMASK:0) |
                                  AB | ((uint64)sect << 18);
                     page_fault = 1;
                     return 1;
                 }
                 glb_sect = 0;
                 ix = GET_XR(MB);
                 ind = TST_IND(MB) != 0;
                 AB = MB & RMASK;
                 if (ix) {
                     temp = get_reg(ix);
                     /* Check if extended indexing */
                     if ((temp & SMASK) != 0 || (temp & SECTM) == 0) { /* Local index word */
                         temp = (temp + AB) & RMASK;
                     } else {
                         temp = (temp + AB) & FMASK;
                         glb_sect = 1;
                         sect = cur_sect = (temp >> 18) & 07777;
                     }
                     MB = temp;
                 } else
                     temp = MB;
                 AB = temp & RMASK;
             } else {             /* Extended index EFIW */
                 ind = (MB & BIT1) != 0;
                 ix = (MB >> 30) & 017;
                 AB = MB & (SECTM|RMASK);
                 temp = MB;
                 if (ix) {
                     temp = get_reg(ix);
                     if ((temp & SMASK) != 0 || (temp & SECTM) == 0) { /* Local index word */
                          temp = AB + (temp & RMASK);
                     } else
                          temp = temp + AB;
                     temp &= FMASK;
                     MB = temp;
                 }
                 sect = cur_sect = (temp >> 18) & 07777;
                 AB = temp & RMASK;
                 glb_sect = 1;
             }
         } else {
#endif
             ix = GET_XR(MB);
             ind = TST_IND(MB) != 0;
             AB = MB & RMASK;
             if (ix) {
                temp = get_reg(ix);
#if KL
                /* Check if extended indexing */
                if (QKLB && sect != 0 && (temp & SMASK) == 0 && (temp & SECTM) != 0) {
                    temp = (temp + ((AB & RSIGN) ?
                          SECTM|((uint64)AB): (uint64)AB)) & (SECTM|RMASK);
                    sect = (temp >> 18) & 07777;
                    MB = 0;
                    glb_sect = 1;
                    AB = 0;
                } else
                    glb_sect = 0;
#endif
                temp = MB = (MB + temp) & FMASK;
                AB = MB & RMASK;
             }
#if KL
         }
#endif
    };
    /* Update pointer */
    val1 &= PMASK;
    val1 |= (uint64)(np) << 30;

    /* Save pointer */
    set_reg(n+1, val1);
    set_reg(n+2, val2);

    /* Read final value */
    ptr_flg = 1;
    BYF5 = wr;
    if (Mem_read(0, 0, 0, wr)) {
        ptr_flg = BYF5 = 0;
        return 1;
    }
    ptr_flg = BYF5 = 0;
    return 0;
}

/* Get data from pointer */
int
load_byte(int n, uint64 *data, uint64 fill, int cnt)
{
    uint64    val1, msk;
    int       s, p;

    /* Check if should return fill */
    val1 = get_reg(n);
    if (cnt && (val1 & MANT) == 0) {
        *data = fill;
        return 1;
    }

    /* Fetch Pointer word */
    if (do_byte_setup(n, 0, &p, &s))
        goto back;

    /* Generate mask for given size */
    msk = (uint64)(1) << s;
    msk--;
    *data = (MB >> p) & msk;
    if (cnt) {
        /* Decrement count */
        val1 = get_reg(n);
        val1--;
        set_reg(n, val1);
    }

    return 1;

back:
    val1 = get_reg(n+1);
    val1 &= PMASK;
    val1 |= (uint64)(p + s) << 30;
    set_reg(n+1, val1);
    return 0;
}

/* Store data into pointer */
int
store_byte(int n, uint64 data, int cnt)
{
    uint64    val1, msk;
    int       s, p;

    /* Fetch Pointer word */
    if (do_byte_setup(n, 1, &p, &s))
        goto back;

    /* Generate mask for given size */
    msk = (uint64)(1) << s;
    msk--;
    msk <<= p;
    MB &= CM(msk);
    MB |= msk & ((uint64)(data) << p);
    ptr_flg = BYF5 = 1;
    if (Mem_write(0, 0))
        goto back;

    ptr_flg = BYF5 = 0;
    if (cnt) {
       /* Decrement count */
       val1 = get_reg(n);
       val1--;
       set_reg(n, val1);
    }

    return 1;

back:
    ptr_flg = BYF5 = 0;
    val1 = get_reg(n+1);
    val1 &= PMASK;
    val1 |= (uint64)(p + s) << 30;
    set_reg(n+1, val1);
    return 0;
}

void
get_mask(int n, uint64 *msk)
{
    uint64   val;
    int      s;
    /* Get pointer */
    val = get_reg(n+1);
    /* Extract index */
    s = (val >> 24) & 077;

    /* Generate mask for given size */
    *msk = ((uint64)(1) << s) - 1;
}

/* Adjust a pointer to be valid */
void
adj_byte(int n)
{
    uint64    val1, val2;
    int       s, p, np;

    /* Get pointer */
    val1 = get_reg(n+1);
    val2 = get_reg(n+2);
    /* Extract index */
    s = (val1 >> 24) & 077;
    p = (val1 >> 30) & 077;
    /* Advance pointer */
    np = (p + (0777 ^ s) + 1) & 0777;
#if KL
    if (QKLB && t20_page) {
        if (p > 36) {  /* Extended pointer */
            int i = p - 37;
            s = _byte_adj[i].s;
            p = _byte_adj[i].p;
            val2 = val1 & (SECTM|RMASK); /* Convert to long pointer */
            val1 = ((uint64)s << 24) | BIT12;
            /* Save pointer */
            set_reg(n+1, val1);
            set_reg(n+2, val2);
            return;
        } else if ((val1 & BIT12) != 0) { /* Full pointer */
            if (np & 0400)
                val2 = (val2 & ~(SECTM|RMASK)) | ((val2 + 1) & (SECTM|RMASK));
        } else {
            if (np & 0400)
                val1 = (val1 & LMASK) | ((val1 + 1) & RMASK);
        }
    } else {
#endif
        if (np & 0400)
            val1 = (val1 & LMASK) | ((val1 + 1) & RMASK);
#if KL
    }
#endif
    if ((np & 0400) == 0)
        return;
    /* Update pointer */
    val1 &= PMASK;
    val1 |= (uint64)(044) << 30;

    /* Save pointer */
    set_reg(n+1, val1);
    set_reg(n+2, val2);
}


/* Advance a pointer by 1 */
void
adv_byte(int n)
{
    uint64    val1, val2;
    int       s, p, np;

    /* Check if should return fill */
    val1 = get_reg(n);
    if ((val1 & MANT) == 0)
        return;
    /* Decrement count */
    val1--;
    set_reg(n, val1);

    /* Get pointer */
    val1 = get_reg(n+1);
    val2 = get_reg(n+2);
    /* Extract index */
    s = (val1 >> 24) & 077;
    p = (val1 >> 30) & 077;
    /* Advance pointer */
    np = (p + (0777 ^ s) + 1) & 0777;
#if KL
    if (QKLB && t20_page) {
        if (p > 36) {  /* Extended pointer */
            int i = p - 37;
            s = _byte_adj[i].s;
            p = _byte_adj[i].p;
            np = (p + (0777 ^ s) + 1) & 0777;
            val2 = val1 & (SECTM|RMASK); /* Convert to long pointer */
            val1 = ((uint64)s << 24) | BIT12;
            if (np & 0400) {
                np = ((0777 ^ s) + 044 + 1) & 0777;
                val2 = (val2 & ~(SECTM|RMASK)) | ((val2 + 1) & (SECTM|RMASK));
            }
        } else if ((val1 & BIT12) != 0) { /* Full pointer */
            if (np & 0400) {
                np = ((0777 ^ s) + 044 + 1) & 0777;
                val2 = (val2 & ~(SECTM|RMASK)) | ((val2 + 1) & (SECTM|RMASK));
            }
        } else {
            if (np & 0400) {
                np = ((0777 ^ s) + 044 + 1) & 0777;
                val1 = (val1 & LMASK) | ((val1 + 1) & RMASK);
            }
        }
    } else {
#endif
        if (np & 0400) {
            np = ((0777 ^ s) + 044 + 1) & 0777;
            val1 = (val1 & LMASK) | ((val1 + 1) & RMASK);
        }
#if KL
    }
#endif
    np &= 077;
    /* Update pointer */
    val1 &= PMASK;
    val1 |= (uint64)(np) << 30;

    /* Save pointer */
    set_reg(n+1, val1);
    set_reg(n+2, val2);
}

/* back a pointer by 1 */
void
bak_byte(int n, int cnt)
{
    uint64    val;
    int       s, p;

    /* Increment count */
    if (cnt) {
        val = get_reg(n);
        val++;
        set_reg(n, val);
    }

    /* Get pointer */
    val = get_reg(n+1);
    /* Extract index */
    s = (val >> 24) & 077;
    p = (((val >> 30) & 077) + (s)) & 0777;
    /* Advance pointer */
    /* Update pointer */
    val &= PMASK;
    val |= (uint64)(p) << 30;
    MB = val;

    /* Save pointer */
    set_reg(n+1, val);
}

/* Preform a table lookup operation */
int
do_xlate(uint32 tbl, uint64 val, int mask)
{
    uint64 reg;
    int f;

    AB = (tbl + (val >> 1)) & RMASK;
    if (Mem_read(0, 0, 0, 0)) {
        /* Backup ext_ac */
        return -2;
    }
    if ((val & 1) == 0)
       MB >>= 18;
    val = MB & mask;
    reg = get_reg(ext_ac);
    f = 1;
    switch ((MB >> 15) & 07) {
    case 0:
            if ((reg & SMASK) == 0)  /* If S */
               f = 0;
            break;
    case 1: f = -1;                  /* Terminate */
            break;
    case 2:
            if ((reg & SMASK) == 0)  /* If S, clear M */
               f = 0;
            reg &= ~BIT2;
            break;
    case 3:
            if ((reg & SMASK) == 0)  /* If S, set M */
               f = 0;
            reg |= BIT2;
            break;
    case 4:
            reg |= SMASK|BIT1;       /* Set S & N */
            break;
    case 5:
            f = -1;                  /* Terminate, set N */
            reg |= BIT1;
            break;
    case 6:
            reg |= SMASK|BIT1;       /* Set S, N, Clear M */
            reg &= ~BIT2;
            break;
    case 7:
            reg |= SMASK|BIT1|BIT2;  /* Set S, N, M */
            break;
    }
    set_reg(ext_ac, reg);
    return f;
}

/* Table of powers of 10 for CVTBD opcodes */
uint64 pow10_tab[22][2] = {
     /*   0: */ { 0000000000000LL, 0000000000001LL },
     /*   1: */ { 0000000000000LL, 0000000000012LL },
     /*   2: */ { 0000000000000LL, 0000000000144LL },
     /*   3: */ { 0000000000000LL, 0000000001750LL },
     /*   4: */ { 0000000000000LL, 0000000023420LL },
     /*   5: */ { 0000000000000LL, 0000000303240LL },
     /*   6: */ { 0000000000000LL, 0000003641100LL },
     /*   7: */ { 0000000000000LL, 0000046113200LL },
     /*   8: */ { 0000000000000LL, 0000575360400LL },
     /*   9: */ { 0000000000000LL, 0007346545000LL },
     /*  10: */ { 0000000000000LL, 0112402762000LL },
     /*  11: */ { 0000000000002LL, 0351035564000LL },
     /*  12: */ { 0000000000035LL, 0032451210000LL },
     /*  13: */ { 0000000000443LL, 0011634520000LL },
     /*  14: */ { 0000000005536LL, 0142036440000LL },
     /*  15: */ { 0000000070657LL, 0324461500000LL },
     /*  16: */ { 0000001070336LL, 0115760200000LL },
     /*  17: */ { 0000013064257LL, 0013542400000LL },
     /*  18: */ { 0000157013326LL, 0164731000000LL },
     /*  19: */ { 0002126162140LL, 0221172000000LL },
     /*  20: */ { 0025536165705LL, 0254304000000LL },
     /*  21: */ { 0330656232670LL, 0273650000000LL }
};

/*
 * Process extended instruction.
 *
 * On entry BR = address of instruction.
 *          AB = value of E0.
 *          IR = opcode.
 */
int
do_extend(uint32 ia)
{
    uint64     fill1, fill2;
    uint64     val1, val2;
    uint64     msk;
    uint64     reg;
#if KL
    int        xlat_sect;
#endif
    int        f, i;


    switch(IR) {
    case 001:  /* CMPSL */
    case 002:  /* CMPSE */
    case 003:  /* CMPSLE */
    case 005:  /* CMPSGE */
    case 006:  /* CMPSN */
    case 007:  /* CMPSG */
              if (((get_reg(ext_ac) | get_reg(ext_ac+3)) & EMASK) != 0)
                  return 1;
              /* Fetch filler values */
              AB = (ia + 1) & RMASK;
              if (Mem_read(0, 1, 0, 0))
                  return 0;
              fill1 = MB;
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 1, 0, 0))
                  return 0;
              fill2 = MB;

              /* Compare the strings */
              f = 2;
              while (((get_reg(ext_ac) | get_reg(ext_ac+3)) & MANT) != 0) {
                  if (!load_byte(ext_ac, &val1, fill1, 1)) {
                      return 0;
                  }
                  if (!load_byte(ext_ac+3, &val2, fill2, 1)) {
                      /* Backup ext_ac */
                      bak_byte(ext_ac, 1);
                      return 0;
                  }
                  if (val1 != val2) {
                      f = (val1 < val2) ? 1: 0;
                      break;
                  }
              }
              /* Check if we should skip */
              switch (IR & 7) {
              case 1:    f = (f == 1); break;
              case 2:    f = (f == 2); break;
              case 3:    f = (f != 0); break;
              case 5:    f = (f != 1); break;
              case 6:    f = (f != 2); break;
              case 7:    f = (f == 0); break;
              default:   f = 0; break;
              }
              /* Skip if conditions match */
              if (f)
                  PC = (PC + 1) & RMASK;
              return 0;

    case 004:  /* EDIT */
              val2 = MB;  /* Save address of translate table */
#if KL
              if (QKLB && pc_sect != 0 && glb_sect)
                 xlat_sect = (val2 >> 18) & 07777;
              else
                 xlat_sect = cur_sect;
#endif
              /* Fetch filler values */
              AB = (ia + 1) & RMASK;
              if (Mem_read(0, 1, 0, 0))
                  return 0;
              fill1 = MB;
              /* Get floating character */
              AB = (AB + 1) & RMASK;
              if (Mem_read(0, 1, 0, 0))
                  return 0;
              fill2 = MB;
              f = 1;
              while (f) {
                 int   a;

                 /* Read in pattern control */
                 reg = get_reg(ext_ac);
#if KS
                 if ((reg & SECTM) != 0)
                     return 1;
#endif
                 AB = reg & RMASK;
#if KL
                 if (QKLB && pc_sect != 0) {
                     sect = (reg >> 18) & 07777;
                     glb_sect = 1;
                 } else {
                     sect = cur_sect;
                     glb_sect = 0;
                 }
#endif
                 if (Mem_read(0, 0, 0, 0))
                     return 0;
                 i = (reg >> 30) & 03;
                 reg &= ~(3LL << 30);  /* Clear byte number */
                 val1 = (MB >> ((3 - i) * 9)) & 0777;
                 i++;
                 if (i > 3) {
#if KL
                     if (QKLB && pc_sect != 0)
                         reg = (reg & ~(SECTM|RMASK)) | ((reg + 1) & (SECTM|RMASK));
                     else
#endif
                         reg = (reg & LMASK) | ((reg+1) & RMASK);
                     i = 0;
                 }
                 reg |= ((uint64)i) << 30;
                 i = 0;
                 a = 0;
                 switch ((val1 >> 6) & 07) {
                 case 0:   /* Individual options */
                         switch (val1 & 077) {
                         case 0:    /* Stop */
                                f = 0;
                                break;
                         case 1:    /* SELECT */
                                if (!load_byte(ext_ac, &val1, 0, 0))
                                   return 0;
                                a = 1;
                                AB = (val2 + (val1 >> 1)) & RMASK;
#if KL
                                sect = xlat_sect;
#endif
                                if (Mem_read(0, 0, 0, 0))
                                    return 0;
                                if ((val1 & 1) == 0)
                                    MB >>= 18;
                                val1 = MB & 07777;
                                switch ((MB >> 15) & 07) {
                                case 0:
     func0:
                                        if ((reg & SMASK) != 0) { /* If S */
                                            i = 1;
                                        } else if (fill1 != 0) {
                                            val1 = fill1;
                                            i = 1;
                                        }
                                        break;
                                case 1:
                                        set_reg(ext_ac, reg);
                                        return 0;                /* Next */
                                case 2:
                                        reg &= ~BIT2;            /* If S, clear M */
                                        goto func0;
                                case 3:
                                        reg |= BIT2;             /* If S, set M */
                                        goto func0;
                                case 4:
    func4:
                                        if ((reg & SMASK) == 0) {
                                            adj_byte(ext_ac+3);
                                            reg |= SMASK;
                                            AR = get_reg(ext_ac+3);
#if KL
                                            if (QKLB && pc_sect != 0) {
                                                sect = (AR >> 18) & 07777;
                                                glb_sect = 1;
                                            } else {
                                                sect = cur_sect;
                                                glb_sect = 0;
                                            }
#endif
                                            AB = AR & RMASK;
                                            MB = get_reg(ext_ac+4);
                                            if (Mem_write(0, 0))
                                                return 0;
#if KL
                                            if (QKLB && pc_sect != 0 && (MB & BIT12) != 0) {
                                                AB = (++AR) & RMASK;
                                                sect = (AR >> 18) & 07777;
                                                MB = get_reg(ext_ac+5);
                                                if (Mem_write(0,0))
                                                   return 0;
                                            }
#endif
                                            if (fill2 != 0) {
                                                if (!store_byte(ext_ac+3, fill1, 0)) {
                                                    return 0;
                                                }
                                            }
                                        }
                                        i = 1;
                                        reg |= SMASK|BIT1;       /* Set S & N */
                                        break;
                                case 5:
                                        reg |= BIT1;
                                        break;
                                case 6:
                                        reg &= ~BIT2;            /* Clear M */
                                        goto func4;
                                case 7:
                                        reg |= BIT2;             /* Set M */
                                        goto func4;
                                }
                                break;
                         case 2:                                 /* Set signifigance */
                                if ((reg & SMASK) == 0) {
                                    AR = get_reg(ext_ac+3);
#if KL
                                    if (QKLB && pc_sect != 0) {
                                        sect = (AR >> 18) & 07777;
                                        glb_sect = 1;
                                    } else {
                                        sect = cur_sect;
                                        glb_sect = 0;
                                    }
#endif
                                    AB = AR & RMASK;
                                    MB = get_reg(ext_ac+4);
                                    if (Mem_write(0, 0))
                                        return 0;
#if KL
                                    if (QKLB && pc_sect != 0 && (MB & BIT12) != 0) {
                                        AB = (++AR) & RMASK;
                                        sect = (AR >> 18) & 07777;
                                        MB = get_reg(ext_ac+5);
                                        if (Mem_write(0,0))
                                            return 0;
                                    }
#endif
                                    if (fill2 != 0) {
                                        val1 = fill2;
                                        i = 1;
                                    }
                                }
                                reg |= SMASK;
                                break;
                         case 3:                                 /* Field separater */
                                reg &= ~(SMASK|BIT1|BIT2);       /* Clear S & N */
                                break;
                         case 4:                                 /* Exchange Mark */
                                AR = get_reg(ext_ac+3);
#if KL
                                if (QKLB && pc_sect != 0) {
                                    sect = (AR >> 18) & 07777;
                                    glb_sect = 1;
                                } else {
                                    sect = cur_sect;
                                    glb_sect = 0;
                                }
#endif
                                AB = AR & RMASK;
                                if (Mem_read(0, 0, 0, 0))
                                    return 0;
                                BR = MB;
                                MB = get_reg(ext_ac+4);
                                /* Make sure byte pointers are same size */
                                if (QKLB && (MB & BIT12) != (BR & BIT12))
                                    return 0;
                                if (Mem_write(0, 0))
                                    return 0;
#if KL
                                if (QKLB && pc_sect != 0 && (BR & BIT12) != 0) {
                                    AB = (AR + 1) & RMASK;
                                    sect = ((AR + 1)>> 18) & 07777;
                                    if (Mem_read(0, 0, 0, 0)) {
                                        AB = AR & RMASK;   /* Restore lower pointer */
                                        sect = (AR >> 18) & 07777;
                                        MB = BR;
                                        (void)Mem_write(0, 0);
                                        return 0;
                                    }
                                    AD = MB;
                                    MB = get_reg(ext_ac+5);
                                    if (Mem_write(0, 0)) {
                                        AB = AR & RMASK;   /* Restore lower pointer */
                                        sect = (AR >> 18) & 07777;
                                        MB = BR;
                                        (void)Mem_write(0, 0);
                                        return 0;
                                    }
                                    set_reg(ext_ac+5, AD);
                                }
#endif
                                set_reg(ext_ac+4, BR);
                                break;
                         case 5:
                                i = 0;
                                break;
                         }
                         break;
                 case 1:   /* Insert Message char */
                         if ((reg & SMASK) != 0) {
                             AB = (ia + (val1 & 077) + 1) & RMASK;
#if KL
                             sect = cur_sect;
#endif
                             if (Mem_read(0, 0, 0, 0))
                                 return 0;
                             i = 1;
                             val1 = MB;
                         } else if (fill1 != 0) {
                             i = 1;
                             val1 = fill1;
                         }
                         break;
                 case 5:   /* Skip on M */
                         if ((reg & BIT2) != 0)
                             goto skipa;
                         break;
                 case 6:   /* Skip on N */
                         if ((reg & BIT1) == 0)
                             break;
                 case 7:   /* Skip allways */
    skipa:
                         /* Compute new byte number */
                         val1 = (val1 & 077) + 1;
                         val2 = ((reg >> 30) & 03) + val1;
                         reg &= ~(3LL << 30);  /* Clear byte number */
                         reg += (val2 >> 2);
                         reg |= (val2 & 3) << 30;
                         i = 0;
                 default:
                         break;
                 }
                 if (i) {
                     if (!store_byte(ext_ac+3, val1, 0)) {
                         if (a)
                            bak_byte(ext_ac, 0);
                         return 0;
                     }
                 }
                 set_reg(ext_ac, reg);
              }
              PC = (PC + 1) & RMASK;
              break;

    case 010:  /* CVTDBO */
    case 011:  /* CVTDBT */
#if KL
              if (QKLB && pc_sect != 0 && glb_sect)
                 xlat_sect = (AR >> 18) & 07777;
              else
                 xlat_sect = cur_sect;
#endif
              val2 = ((AR & RSIGN) ? LMASK : 0) | (AR & RMASK);
              /* Check if conversion started */
              if ((get_reg(ext_ac) & SMASK) == 0) {
                   set_reg(ext_ac+3, 0);
                   set_reg(ext_ac+4, 0);
              }
              AR = get_reg(ext_ac + 3);
              ARX = get_reg(ext_ac + 4);
              if (IR == 010) {
                 fill2 = get_reg(ext_ac);
                 fill2 |= SMASK;
                 set_reg(ext_ac, fill2);
              }
              while ((get_reg(ext_ac) & MANT) != 0) {
                  if (!load_byte(ext_ac, &val1, 0, 1)) {
                      set_reg(ext_ac+3, AR);
                      set_reg(ext_ac+4, ARX);
                      return 0;
                  }
                  if (IR == 010) {
                      val1 = (val1 + val2) & FMASK;
                  } else {
#if KL
                      sect = xlat_sect;
#endif
                      f = do_xlate((uint32)(val2 & RMASK), val1, 017);
                      if (f < 0)
                          break;
                      if (f)
                         val1 = MB & 017;
                  }
                  if ((val1 & RSIGN) != 0 || val1 > 9) {
                      ARX = (ARX & CMASK) | (AR & SMASK);
                      set_reg(ext_ac+3, AR);
                      set_reg(ext_ac+4, ARX);
                      return 0;
                  }
                  /* Multiply by 2 */
                  AR <<= 1;
                  ARX <<= 1;
                  if (ARX & SMASK)
                     AR |= 1;
                  ARX &= CMASK;
                  /* Compute times 4 */
                  BR = (AR << 2) | ((ARX >> 33) & 03);
                  BRX = (ARX << 2) & CMASK;
                  ARX = (ARX & CMASK) + (BRX & CMASK) + val1;
                  f = (ARX >> 35);
                  AR = AR + BR + f;
                  ARX &= CMASK;
                  AR &= FMASK;
              }
              ARX &= CMASK;
              if ((get_reg(ext_ac) & MANT) == 0) {
                  PC = (PC + 1) & RMASK;
                  if (get_reg(ext_ac) & BIT2) {
                      ARX = CCM(ARX) + 1;
                      AR = CM(AR) + ((ARX & SMASK) != 0);
                  }
              }
              ARX = (ARX & CMASK) | (AR & SMASK);
              AR &= FMASK;
              set_reg(ext_ac+3, AR);
              set_reg(ext_ac+4, ARX);
              break;
    case 012:  /* CVTBDO */
    case 013:  /* CVTBDT */
              /* Save E1 */
              if (IR == 012) {
                  val2 = ((AR & RSIGN) ? LMASK : 0) | (AR & RMASK);
#if KL
                  xlat_sect = cur_sect;
#endif
              } else {
                  val2 = AB;
#if KL
                  if (QKLB && pc_sect != 0 && glb_sect)
                     xlat_sect = (AR >> 18) & 07777;
                  else
                     xlat_sect = cur_sect;
#endif
              }
              /* Get fill */
              AB = (ia + 1) & RMASK;
              if (Mem_read(0, 1, 0, 0))
                  return 0;
              fill1 = MB;
              AR = get_reg(ext_ac);
              ARX = get_reg(ext_ac + 1);
              reg = get_reg(ext_ac + 3);
              /* Set M bit if minus */
              if ((AR & SMASK) != 0 && (reg & BIT2) == 0) {
                  reg |= BIT2;
                  ARX = CCM(ARX) + 1;
                  AR = CM(AR) + ((ARX & SMASK) != 0);
              }
              ARX &= CMASK;
              /* Set N bit if non-zero number */
              if ((AR | ARX) != 0)
                  reg |= BIT1;
#if KL
              set_reg(ext_ac+3, reg);
#endif
              /* Compute number of digits needed for value */
              for (f = 0; f < 22; f++) {
                  BRX = ARX + CCM(pow10_tab[f][1]) + 1;
                  BR = AR + CM(pow10_tab[f][0]) + ((BRX & SMASK) != 0);
                  if ((BR & C1) == 0)
                     break;
              }
              if (f == 0)
                  f = 1;
              /* Check if room to save it */
              if (f > (int)(reg & MANT))
                  return 0;
#if KS
              set_reg(ext_ac+3, reg);
#endif
              /* Fill out left justify */
              /* If L, fill leading zeros with fill char */
              while ((reg & SMASK) != 0 && (int)(reg & MANT) > f) {
                  if (!store_byte(ext_ac + 3, fill1, 1))
                     return 0;
                  reg = get_reg(ext_ac + 3);
              }
              /* Insert correct digit */
              for (f--; f >= 0; f--) {
                  /* Subtract closest power of 10 */
                  for (i = 0; i < 10; i++) {
                      BRX = ARX + CCM(pow10_tab[f][1]) + 1;
                      BR = AR + CM(pow10_tab[f][0]) + ((BRX & SMASK) != 0);
                      if ((BR & C1) == 0)
                         break;
                      ARX = BRX & CMASK;
                      AR = BR & FMASK;
                  }
                  val1 = (uint64)i;
                  if (IR == 013) {
                       /* Read first translation entry */
                       AB = (val1 + val2) & RMASK;
#if KL
                       sect = xlat_sect;
#endif
                       if (Mem_read(0, 0, 0, 0)) {
                           set_reg(ext_ac + 3, (reg & (SMASK|EXPO)) | (f+1));
                           return 0;
                       }
                       val1 = MB;
                       if (f == 0 && (get_reg(ext_ac + 3) & BIT2) != 0)
                           val1 >>= 12;
                       val1 &= 07777;
                  } else
                       val1 += val2;
                  if (!store_byte(ext_ac + 3, val1, 1)) {
                     set_reg(ext_ac + 3, (reg & (SMASK|EXPO)) | (f+1));
                     return 0;
                  }
                  set_reg(ext_ac, AR);
                  set_reg(ext_ac+1, ARX);
              }
              reg = get_reg(ext_ac+3);
              reg &= SMASK|EXPO;
              set_reg(ext_ac+3, reg);
              set_reg(ext_ac, 0);
              set_reg(ext_ac+1, 0);
              PC = (PC + 1) & RMASK;
              break;
    case 014:  /* MOVSO */
    case 015:  /* MOVST */
    case 016:  /* MOVSLJ */
              get_mask(ext_ac+3, &msk);
#if KL
              xlat_sect = cur_sect;
#endif
              if ((((get_reg(ext_ac) & (077LL << 26))| get_reg(ext_ac+3)) & EMASK) != 0)
                  return 1;
              if (IR == 014) {
                 val2 = ((AR & RSIGN) ? LMASK : 0) | (AR & RMASK);
              } else if (IR == 015) {
                  AB = ia;
#if KL
                  if (QKLB) {
                     if (pc_sect != 0 && glb_sect)
                         xlat_sect = (AR >> 18) & 07777;
                      else
                         xlat_sect = cur_sect;
                  } else
                      xlat_sect = 0;
#endif
                  if (Mem_read(0, 1, 0, 0))
                      return 0;
                  val2 = MB;
              } else {
                  val2 = AB;
              }
              /* Fetch filler values */
              AB = (ia + 1) & RMASK;
              if (Mem_read(0, 1, 0, 0))
                  return 0;
              fill1 = MB;
              while ((get_reg(ext_ac) & MANT) != 0) {
                  if ((get_reg(ext_ac+3) & MANT) == 0)
                      return 0;
                  if (!load_byte(ext_ac, &val1, fill1, 1))
                      return 0;
                  if (IR == 014) {
                      val1 = (val1 + val2) & FMASK;
                      /* Check if in range */
                      if ((val1 & ~msk) != 0)
                          return 0;
                  } else if (IR == 015) {
#if KL
                      sect = xlat_sect;
#endif
                      f = do_xlate((uint32)(val2), val1, 07777);
                      if (f < 0)
                          return 0;
                      if (f)
                          val1 = MB & 07777;
                  }
                  if (!store_byte(ext_ac+3, val1, 1)) {
                      bak_byte(ext_ac, 1);
                      return 0;
                  }
              }
              while ((get_reg(ext_ac+3) & MANT) != 0) {
                  if (!store_byte(ext_ac+3, fill1, 1))
                     return 0;
              }
              PC = (PC + 1) & RMASK;
              break;

    case 017:  /* MOVSRJ */
              /* Fetch filler values */
              if (((get_reg(ext_ac) | get_reg(ext_ac+3)) & EMASK) != 0)
                  return 1;
              AB = (ia + 1) & RMASK;
              if (Mem_read(0, 1, 0, 0))
                  return 0;
              fill1 = MB;
              /* While source is larger, skip source */
              val2 = get_reg(ext_ac+3);
              while (val2 != 0 && get_reg(ext_ac) > val2)
                  adv_byte(ext_ac);

              /* While destination is larger, fill destination */
              while (val2 != 0 && get_reg(ext_ac) < val2) {
                  if (!store_byte(ext_ac+3, fill1, 1)) {
                      return 0;
                  }
                  val2 = get_reg(ext_ac+3);
              }
              /* Copy rest of string */
              while (get_reg(ext_ac+3)) {
                  if (!load_byte(ext_ac, &val1, fill1, 1))
                      return 0;
                  if (!store_byte(ext_ac+3, val1, 1)) {
                      /* Backup ext_ac */
                      bak_byte(ext_ac, 1);
                      return 0;
                  }
              }
              PC = (PC + 1) & RMASK;
              break;

    case 020:  /* XBLT */
#if KL
              if (QKLB) {
                  glb_sect = 1;
                  reg = get_reg(ext_ac);
                  val1 = get_reg(ext_ac + 1);
                  val2 = get_reg(ext_ac + 2);
                  while (reg != 0) {
                      if (reg & SMASK) {
                          val1 = (val1 - 1) & (SECTM|RMASK);
                          sect = (val1 >> 18) & 00037;
                          AB = val1 & RMASK;
                          ptr_flg = 1;
                          if (Mem_read(0, 0, 0, 0)) {
                             val1 = (val1 + 1) & (SECTM|RMASK);
                             goto xblt_done;
                          }
                          val2 = (val2 - 1) & (SECTM|RMASK);
                          sect = (val2 >> 18) & 00037;
                          AB = val2 & RMASK;
                          ptr_flg = 0;
                          BYF5 = 1;
                          if (Mem_write(0, 0)) {
                             val1 = (val1 + 1) & (SECTM|RMASK);
                             val2 = (val2 + 1) & (SECTM|RMASK);
                             goto xblt_done;
                          }
                          BYF5 = 0;
                          reg = (reg + 1) & FMASK;
                      } else {
                          sect = (val1 >> 18) & 00037;
                          AB = val1 & RMASK;
                          ptr_flg = 1;
                          if (Mem_read(0, 0, 0, 0))
                             goto xblt_done;
                          sect = (val2 >> 18) & 00037;
                          AB = val2 & RMASK;
                          ptr_flg = 0;
                          BYF5 = 1;
                          if (Mem_write(0, 0))
                             goto xblt_done;
                          val1 = (val1 + 1) & (SECTM|RMASK);
                          val2 = (val2 + 1) & (SECTM|RMASK);
                          reg = (reg - 1) & FMASK;
                          BYF5 = 0;
                      }
                  }
xblt_done:
                  ptr_flg = BYF5 = 0;
                  set_reg(ext_ac, reg);
                  set_reg(ext_ac + 1, val1);
                  set_reg(ext_ac + 2, val2);
                  return 0;
              }
#endif
    case 021:  /* GSNGL */
    case 022:  /* GDBLE */
    case 023:  /* GDFIX */
    case 024:  /* GFIX */
    case 025:  /* GDFIXR */
    case 026:  /* GFIXR */
    case 027:  /* DGFLTR */
    case 030:  /* GFLTR */
    case 031:  /* GFSC */
    default:
              return 1;
    }
    return 0;
}
#endif

t_stat
rtc_srv(UNIT * uptr)
{
    int32 t;
    t = sim_rtcn_calb (rtc_tps, TMR_RTC);
    sim_activate_after(uptr, 1000000/rtc_tps);
    tmxr_poll = t/2;
#if PDP6 | KA | KI
    clk_flg = 1;
#if PIDP10
    if (clk_en && !sing_inst_sw) {
#else
    if (clk_en) {
#endif
        sim_debug(DEBUG_CONO, &cpu_dev, "CONO timmer\n");
        set_interrupt(4, clk_irq);
    }
#elif KS
    int_cur -= 2*4096;
    if (int_cur & C1) {
       irq_flags |= INT_DONE;
       int_cur = int_val;
       check_apr_irq();
    }
    tim_low += 2*4096;
    if (tim_low & SMASK) {
       tim_high += 1;
       tim_low = 0;
    }
#if KS_ITS
    qua_time += 2*4096;
#endif
#elif KL
    update_times(rtc_tim);
    rtc_tim = (1000000/rtc_tps);
#endif
    return SCPE_OK;
}

#if ITS
t_stat
qua_srv(UNIT * uptr)
{
    if ((fault_data & 1) == 0 && pi_enable && !pi_pending && (FLAGS & USER) != 0) {
       mem_prot = 1;
       check_apr_irq();
    }
    qua_time = BIT17;
    return SCPE_OK;
}
#endif

#if KL
t_stat
tim_srv(UNIT * uptr)
{
    double us;

    /* See if we are counting to overflow or period */
    if (tim_val & 0100000) {
        tim_val = 020000 | tim_per;
        us = (double)((010000 - tim_per) * 10);
    } else {
        tim_val = 0130000;
        us = (double)(tim_per * 10);
    }
    set_interrupt(4 << 2, mtr_irq);
    (void)sim_activate_after_d(uptr, us);
    return SCPE_OK;
}
#endif

/*
 * This sequence of instructions is a mix that hopefully
 * represents a resonable instruction set that is a close
 * estimate to the normal calibrated result.
 */

static const char *pdp10_clock_precalibrate_commands[] = {
    "-m 100 ADDM 0,110",
    "-m 101 ADDI 0,1",
    "-m 102 JRST 100",
    "PC 100",
    NULL};

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
    int          i;
    t_stat       r = SCPE_OK;
    static int   initialized = 0;

    if (!initialized) {
         initialized = 1;
#if PIDP10
         r = pi_panel_start();
         if (r != SCPE_OK) {
             return r;
         }
#endif
    }
    sim_debug(DEBUG_CONO, dptr, "CPU reset\n");
    RUN = BYF5 = uuo_cycle = 0;
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
#if KA | KI
    adr_flag = 0;
#endif
    MI_flag = prog_stop = nxm_flag = clk_flg = 0;
    IOB_PI = PIR = PIH = PIE = pi_enable = parity_irq = 0;
    pi_pending = pi_enc = apr_irq = 0;
    ov_irq =fov_irq =clk_en =clk_irq = 0;
    pi_restore = pi_hold = 0;
    FLAGS = 0;
#if KI | ITS | BBN
    ac_stack = 0;
#endif
#if KI | KL | KS
    ub_ptr = eb_ptr = 0;
    pag_reload = 0;
#if KI
    fm_sel = small_user = user_addr_cmp = page_enable = 0;
#else
    fm_sel = prev_ctx = user_addr_cmp = page_enable = t20_page = 0;
    irq_enable = irq_flags = 0;
#if KL
    sect = cur_sect = pc_sect = 0;
#endif
#endif
#endif
#if BBN
    exec_map = 0;
#endif
    for(i=0; i < 128; dev_irq[i++] = 0);
#if KS | KL
    cst = 0;
#endif
#if KS
    int_cur = int_val = 0;
    uba_reset();
#endif
#if KI | KL | ITS | BBN | KS
    for (i = 0; i < 512; i++) {
        e_tlb[i] = 0;
        u_tlb[i] = 0;
    }
    for (;i < 546; i++)
        u_tlb[i] = 0;
#endif

    sim_brk_types = SWMASK('E') | SWMASK('W') | SWMASK('R');
    sim_brk_dflt = SWMASK ('E');
    sim_clock_precalibrate_commands = pdp10_clock_precalibrate_commands;
    sim_vm_initial_ips = 4 * SIM_INITIAL_IPS;
    sim_rtcn_init_unit (&cpu_unit[0], cpu_unit[0].wait, TMR_RTC);
    sim_activate(&cpu_unit[0], 1000);
#if MPX_DEV
    mpx_enable = 0;
#endif
#ifdef PANDA_LIGHTS
    ka10_lights_init ();
#endif
    sim_vm_interval_units = "cycles";
    sim_vm_step_unit = "instruction";
    return r;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw)
{
if (vptr == NULL)
    return SCPE_ARG;
if (ea < 020)
    *vptr = FM[ea] & FMASK;
else {
#if KL | KI | KS
    if (sw & SWMASK ('V')) {
        int uf = ((sw & SWMASK('U')) != 0);
        int page = ea >> 9;
        uint32  tlb;
#if KL | KS
        if (!uf && !t20_page && (page & 0740) == 0340) {
#else
        if (!uf && (page & 0740) == 0340) {
#endif
             /* Pages 340-377 via UBT */
             page += 01000 - 0340;
             uf = 1;
        }
        if (uf)
           tlb = u_tlb[page];
        else
           tlb = e_tlb[page];
        if ((tlb & RSIGN) == 0)
           return 4;
        ea = ((tlb & 017777) << 9) + (ea & 0777);
    }
#endif

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
#if KL | KI | KS
    if (sw & SWMASK ('V')) {
        int uf = ((sw & SWMASK('U')) != 0);
        int page = ea >> 9;
        uint32  tlb;
#if KL | KS
        if (!uf && !t20_page && (page & 0740) == 0340) {
#else
        if (!uf && (page & 0740) == 0340) {
#endif
             /* Pages 340-377 via UBT */
             page += 01000 - 0340;
             uf = 1;
        }
        if (uf)
           tlb = u_tlb[page];
        else
           tlb = e_tlb[page];
        if ((tlb & RSIGN) == 0)
           return 4;
        ea = ((tlb & 017777) << 9) + (ea & 0777);
    }
#endif
    if (ea >= MEMSIZE)
        return SCPE_NXM;
    M[ea] = val & FMASK;
    }
return SCPE_OK;
}

/* Called at close of simulator */
t_stat cpu_detach (UNIT *uptr)
{
#if PIDP10
    pi_panel_stop();
#endif
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

#if !KS
/* Build device dispatch table */
t_bool build_dev_tab (void)
{
    DEVICE *dptr;
    DIB    *dibp;
    uint32 i, j, d;
#if KL
    uint32  rh20;
#endif
#if !PDP6
    int     rh_idx;
#endif

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
#if KL
    dev_tab[3] = &dev_cca;
    dev_tab[4] = &dev_tim;
    dev_irqv[4] = &tim_irq;
    dev_tab[5] = &dev_mtr;
#endif
#endif
#if BBN
    if (QBBN) {
       dev_tab[024>>2] = &dev_pag;
    }
#endif

#if (NUM_DEVS_RP + NUM_DEVS_RS + NUM_DEVS_TU) > 0
#if !PDP6
    /* Assign all RH10 & RH20  devices */
#if KL
    rh20 = 0540;
#endif
    rh_idx = 0;
    for (i = 0; (dptr = rh_devs[i]) != NULL; i++) {
        dibp = (DIB *) dptr->ctxt;
        if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
            d = dibp->dev_num;                              /* Check type */
            if (d & RH10_DEV) {                             /* Skip RH10 devices */
                d = rh_nums[rh_idx];
                if (d == 0) {
                    sim_printf ("To many RH10 devices %s\n", sim_dname (dptr));
                    return TRUE;
                }
#if KL
            } else if (d & RH20_DEV) {                      /* RH20, grab next device */
#if NUM_DEVS_NIA > 0
                /* If NIA20 installed, skip this slot */
                if ((nia_dev.flags & DEV_DIS) == 0 && dptr != &nia_dev &&
                    rh20 == (((DIB *)nia_dev.ctxt)->dev_num & 0777))
                    rh20 += 4;
                else
                /* If NIA20, then assign it to it's requested address */
                if ((nia_dev.flags & DEV_DIS) == 0 && dptr == &nia_dev)
                    d = dibp->dev_num & 0777;
                else
#endif
                d = rh20;
                rh20 += 4;
#endif
            }
            dev_tab[(d >> 2)] = dibp->io;
            dev_irqv[(d >> 2)] = dibp->irq;
            rh[rh_idx].dev_num = d;
            rh[rh_idx].dev = dptr;
            rh[rh_idx].rh = dibp->rh;
            dibp->rh->devnum = d;
            rh_idx++;
        }
    }
#endif
#endif

    /* Assign all remaining devices */
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        dibp = (DIB *) dptr->ctxt;
        if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
            for (j = 0; j < dibp->num_devs; j++) {          /* loop thru disp */
                if (dibp->io) {                             /* any dispatch? */
                    d = dibp->dev_num;
#if (NUM_DEVS_RP + NUM_DEVS_RS + NUM_DEVS_TU) > 0
                    if (d & (RH10_DEV|RH20_DEV))            /* Skip RH10 & RH20 devices */
                        continue;
#endif
                    if (dev_tab[(d >> 2) + j] != &null_dev) {
                                                            /* already filled? */
                        sim_printf ("%s device number conflict at %02o\n",
                               sim_dname (dptr), d + (j << 2));
                        return TRUE;
                    }
                    dev_tab[(d >> 2) + j] = dibp->io;       /* fill */
                    dev_irqv[(d >> 2) + j] = dibp->irq;
                }
            }
        }
    }
    return FALSE;
}
#endif

#if KI | KL | KS

/* Set serial */
t_stat cpu_set_serial (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 lnt;
t_stat r;

if (cptr == NULL) {
    apr_serial = -1;
    return SCPE_OK;
    }
#if KI
lnt = (int32) get_uint (cptr, 10, 001777, &r);
#elif KS
lnt = (int32) get_uint (cptr, 10, 077777, &r);
#else
lnt = (int32) get_uint (cptr, 10, 007777, &r);
#endif
if ((r != SCPE_OK) || (lnt <= 0))
    return SCPE_ARG;
apr_serial = lnt;
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
fprintf (st, "PC       AC             EA        AR            RES           FLAGS IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
#if KL
        if (QKLB)
            fprintf(st, "%08o ", h->pc & 0777777777);
        else
#endif
        fprintf (st, "%06o   ", h->pc & 0777777);
        fprint_val (st, h->ac, 8, 36, PV_RZRO);
        fputs ("  ", st);
#if KL
        if (QKLB)
            fprintf(st, "%08o ", h->ea & 077777777);
        else
#endif
#if KS
        fprintf (st, "%c", (h->ea & 07000000) ? ((h->ea >> 18) & 07) + '0': ' ');
        fprintf (st, "%06o   ", h->ea & 0777777);
#else
        fprintf (st, "%06o   ", h->ea);
#endif
        fputs ("  ", st);
        fprint_val (st, h->mb, 8, 36, PV_RZRO);
        fputs ("  ", st);
        fprint_val (st, h->fmb, 8, 36, PV_RZRO);
        fputs ("  ", st);
#if KI | KL
        fprintf (st, "%c%06o  ", ((h->flags & (PRV_PUB << 5))? 'p':' '), h->flags & 0777777);
#if KL
        fprintf (st, "%02o ", h->prev_sect);
#endif
#else
        fprintf (st, "%06o  ", h->flags);
#endif
        if ((h->pc & HIST_PCE) != 0) {
            sim_eval = h->ir;
            fprint_val (st, sim_eval, 8, 36, PV_RZRO);
        } else if ((h->pc & HIST_PC2) == 0) {
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
#if KS
    return "KS10 CPU";
#endif
#if KL
    return "KL10 CPU";
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
