/* kx10_defs.h: PDP-10 simulator definitions

   Copyright (c) 2011-2020, Richard Cornwell

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
   in this Software without prior written authorization from Richard Cornwell.

*/

#ifndef _KA10_DEFS_H_
#define _KA10_DEFS_H_  0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_ADDR64)
#error "PDP-10 does not support 64b addresses!"
#endif

#ifndef PDP6
#define PDP6 0
#endif

#ifndef KA
#define KA 0
#endif

#ifndef KI
#define KI 0
#endif

#ifndef KL
#define KL 0
#endif

#if KL
#define EPT440 0              /* Force KL10 to use as 440 section address */
#endif

#if (PDP6 + KA + KI + KL) != 1
#error "Please define only one type of CPU"
#endif

#ifndef KI_22BIT
#define KI_22BIT KI|KL
#endif

/* Support for ITS Pager */
#ifndef ITS
#define ITS KA
#endif

/* Support for TENEX Pager */
#ifndef BBN
#define BBN KA
#endif

/* Support for WAITS mods */
#ifndef WAITS
#define WAITS KA
#endif

/* Support for ITS on KL */
#ifndef KL_ITS
#define KL_ITS KL
#endif

#ifndef PDP6_DEV       /* Include PDP6 devices */
#define PDP6_DEV PDP6|WAITS
#endif

#ifndef MAGIC_SWITCH   /* Infamous MIT magic switch. */
#define MAGIC_SWITCH 0
#endif


/* MPX interrupt multiplexer for ITS systems */
#define MPX_DEV ITS

/* Digital Equipment Corporation's 36b family had six implementations:

   name         mips    comments

   PDP-6        0.25    Original 36b implementation, 1964
   KA10         0.38    First PDP-10, flip chips, 1967
   KI10         0.72    First paging system, flip chip + MSI, 1972
   KL10         1.8     First ECL system, ECL 10K, 1975
   KL10B        1.8     Expanded addressing, ECL 10K, 1978
   KS10         0.3     Last 36b system, 2901 based, 1979

   In addition, it ran four major (incompatible) operating systems:

   name         company comments

   TOPS-10      DEC     Original timesharing system
   ITS          MIT     "Incompatible Timesharing System"
   TENEX        BBN     ARPA-sponsored, became
   TOPS-20      DEC     Commercial version of TENEX

   All of the implementations differ from one another, in instruction set,
   I/O structure, and memory management.  Further, each of the operating
   systems customized the microcode of the paging systems (KI10, KL10, KS10)
   for additional instructions and specialized memory management.  As a
   result, there is no "reference implementation" for the 36b family that
   will run all programs and all operating systems.  The conditionalization
   and generality needed to support the full matrix of models and operating
   systems, and to support 36b hardware on 32b data types, is beyond the
   scope of this project.

*/

/* Abort codes, used to sort out longjmp's back to the main loop
   Codes > 0 are simulator stop codes
   Codes < 0 are internal aborts
   Code  = 0 stops execution for an interrupt check
*/

typedef t_uint64     uint64;

#define STOP_HALT       1                               /* halted */
#define STOP_IBKPT      2                               /* breakpoint */
#define STOP_ACCESS     3                               /* invalid access */
#if MAGIC_SWITCH
#define STOP_MAGIC      4                               /* low on magic */
#endif

/* Debuging controls */
#define DEBUG_CMD       0x0000001       /* Show device commands */
#define DEBUG_DATA      0x0000002       /* Show data transfers */
#define DEBUG_DETAIL    0x0000004       /* Show details */
#define DEBUG_EXP       0x0000008       /* Show error conditions */
#define DEBUG_CONI      0x0000020       /* Show CONI instructions */
#define DEBUG_CONO      0x0000040       /* Show CONO instructions */
#define DEBUG_DATAIO    0x0000100       /* Show DATAI/O instructions */
#define DEBUG_IRQ       0x0000200       /* Show IRQ requests */

extern DEBTAB dev_debug[];
extern DEBTAB crd_debug[];

/* Operating system flags, kept in cpu_unit.flags */

#define Q_IDLE          (sim_idle_enab)

/* Device information block */
#define LMASK    00777777000000LL
#define RMASK    00000000777777LL
#define FMASK    00777777777777LL
#define CMASK    00377777777777LL
#define SMASK    00400000000000LL
#define C1       01000000000000LL
#define RSIGN    00000000400000LL
#define PMASK    00007777777777LL
#define XMASK    03777777777777LL
#define EMASK    00777000000000LL
#define MMASK    00000777777777LL
#define SECTM    00007777000000LL
#define BIT1     00200000000000LL
#define BIT2     00100000000000LL
#define BIT3     00040000000000LL
#define BIT4     00020000000000LL
#define BIT5     00010000000000LL
#define BIT6     00004000000000LL
#define BIT7     00002000000000LL
#define BIT8     00001000000000LL
#define BIT9     00000400000000LL
#define BIT10    00000200000000LL
#define BIT10_35 00000377777777LL
#define BIT12    00000040000000LL
#define BIT17    00000001000000LL
#define MANT     00000777777777LL
#define EXPO     00377000000000LL
#define FPHBIT   01000000000000000000000LL
#define FPSBIT   00400000000000000000000LL
#define FPNBIT   00200000000000000000000LL
#define FP1BIT   00100000000000000000000LL
#define FPFMASK  01777777777777777777777LL
#define FPRMASK  00000000000177777777777LL
#define FPMMASK  00000000000077777777777LL
#define FPRBIT2  00000000000100000000000LL
#define FPRBIT1  00000000000200000000000LL

#define CM(x)   (FMASK ^ (x))
#define CCM(x)  ((CMASK ^ (x)) & CMASK)

#define INST_V_OP       27                              /* opcode */
#define INST_M_OP       0777
#define INST_V_DEV      26
#define INST_M_DEV      0177                            /* device */
#define INST_V_AC       23                              /* AC */
#define INST_M_AC       017
#define INST_V_IND      22                              /* indirect */
#define INST_IND        (1 << INST_V_IND)
#define INST_V_XR       18                              /* index */
#define INST_M_XR       017
#define OP_JRST         0254                            /* JRST */
#define OP_JUMPA        0324                            /* JUMPA */
#define AC_XPCW         07                              /* XPCW */
#define OP_JSR          0264                            /* JSR */
#define GET_OP(x)       ((int32) (((x) >> INST_V_OP) & INST_M_OP))
#define GET_DEV(x)      ((int32) (((x) >> INST_V_DEV) & INST_M_DEV))
#define GET_AC(x)       ((int32) (((x) >> INST_V_AC) & INST_M_AC))
#define TST_IND(x)      ((x) & INST_IND)
#define GET_XR(x)       ((int32) (((x) >> INST_V_XR) & INST_M_XR))
#define GET_ADDR(x)     ((uint32) ((x) & RMASK))
#define LRZ(x)          (((x) >> 18) & RMASK)
#define JRST1           (((uint64)OP_JRST << 27) + 1)

#define OP_PORTAL(x)    (((x) & 00777740000000LL) == 0254040000000LL)

#if PDP6
#define NODIV   000000
#define FLTUND  000000
#else
#define NODIV   000001        /* 000040 */
#define FLTUND  000002        /* 000100 */
#endif
#if KI|KL
#define TRP1    000004        /* 000200 */
#define TRP2    000010        /* 000400 */
#define ADRFLT  000020        /* 001000 */
#define PUBLIC  000040        /* 002000 */
#else
#define TRP1    000000
#define TRP2    000000
#define ADRFLT  000000
#define PUBLIC  000000
#endif
#ifdef BBN
#define EXJSYS  000040        /* 002000 */
#endif
#define USERIO  000100        /* 004000 */
#define USER    000200        /* 010000 */
#define BYTI    000400        /* 020000 */
#if PDP6
#define FLTOVR  010000
#define PCHNG   001000        /* 040000 */
#else
#define FLTOVR  001000        /* 040000 */
#define PCHNG   000000
#endif
#define CRY1    002000        /* 100000 */
#define CRY0    004000        /* 200000 */
#define OVR     010000        /* 400000 */
#if KI|KL
#define PRV_PUB 020000        /* Overflow in excutive mode */
#else
#define PRV_PUB 000000        /* Not on KA or PDP6 */
#endif
#ifdef ITS
#ifdef PURE
#undef PURE
#endif
#define ONEP    000010        /* 000400 */
#define PURE    000040        /* 002000 */
#endif

#define DATAI   00
#define DATAO   01
#define CONI    02
#define CONO    03

#define CTY_SWITCH      030

#if KI_22BIT|KI
#define MAXMEMSIZE      4096 * 1024
#else
#if PDP6
#define MAXMEMSIZE      256 * 1024
#else
#define MAXMEMSIZE      1024 * 1024
#endif
#endif
#define MEMSIZE         (cpu_unit[0].capac)

#define ICWA            0000000000776
#if KI_22BIT
#define AMASK           00000017777777LL
#define WMASK           0037777LL
#define CSHIFT          22
#if KL
#define RH20_WMASK      003777LL
#define RH20_XFER       SMASK
#define RH20_HALT       BIT1
#define RH20_REV        BIT2
#endif
#else
#define AMASK           RMASK
#define WMASK           RMASK
#define CSHIFT          18
#endif

#define API_MASK        0000000007
#define PI_ENABLE       0000000010      /* Clear DONE */
#define BUSY            0000000020      /* STOP */
#define CCW_COMP        0000000040      /* Write Final CCW */
/* RH10 / RH20 interrupt */
#define IADR_ATTN       0000000000040LL   /* Interrupt on attention */
#define IARD_RAE        0000000000100LL   /* Interrupt on register access error */
#define CCW_COMP_1      0000000040000LL   /* Control word written. */

#if KI
#define DEF_SERIAL      514             /* Default DEC test machine */
#endif

#if KL
#define DEF_SERIAL      1025            /* Default DEC test machine */
#endif

#if BBN
#define BBN_PAGE        0000017777777LL
#define BBN_TRPPG       0000017000000LL
#define BBN_SPT         0000017777000LL
#define BBN_PN          0000000000777LL
#define BBN_ACC         0000040000000LL
#define BBN_TRP1        0000100000000LL
#define BBN_TRP         0000200000000LL
#define BBN_TRPMOD      0000400000000LL
#define BBN_TRPUSR      0001000000000LL
#define BBN_EXEC        0020000000000LL
#define BBN_WRITE       0040000000000LL
#define BBN_READ        0100000000000LL
#define BBN_MERGE       0161740000000LL
#endif

#if KL
/* KL10 TLB paging bits */
#define KL_PAG_A        0400000    /* Access */
#define KL_PAG_P        0200000    /* Public */
#define KL_PAG_W        0100000    /* Writable (M Tops 20) */
#define KL_PAG_S        0040000    /* Software (W Writable Tops 20) */
#define KL_PAG_C        0020000    /* Cacheable */
#endif

#if KI
/* KI10 TLB paging bits */
#define KI_PAG_A        0400000    /* Access */
#define KI_PAG_P        0200000    /* Public */
#define KI_PAG_W        0100000    /* Writable */
#define KI_PAG_S        0040000    /* Software */
#define KI_PAG_X        0020000    /* Reserved */
#endif

/* Flags for CPU unit */
#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (0177 << UNIT_V_MSIZE)
#define UNIT_V_MAOFF    (UNIT_V_MSIZE + 8)
#define UNIT_V_PAGE     (UNIT_V_MAOFF + 1)
#define UNIT_MAOFF      (1 << UNIT_V_MAOFF)
#if KL
#define UNIT_KL10B      (1 << UNIT_V_PAGE)
#define UNIT_TWOSEG     (0)
#else
#define UNIT_TWOSEG     (1 << UNIT_V_PAGE)
#endif
#define UNIT_ITSPAGE    (2 << UNIT_V_PAGE)
#define UNIT_BBNPAGE    (4 << UNIT_V_PAGE)
#define UNIT_M_PAGE     (007 << UNIT_V_PAGE)
#define UNIT_V_WAITS    (UNIT_V_PAGE + 3)
#define UNIT_M_WAITS    (1 << UNIT_V_WAITS)
#define UNIT_WAITS      (UNIT_M_WAITS)        /* Support for WAITS xct and fix */
#define UNIT_V_MPX      (UNIT_V_WAITS + 1)
#define UNIT_M_MPX      (1 << UNIT_V_MPX)
#define UNIT_MPX        (UNIT_M_MPX)          /* MPX Device for ITS */
#define CNTRL_V_RH      (UNIT_V_UF + 4)
#define CNTRL_M_RH      7
#define GET_CNTRL_RH(x) (((x) >> CNTRL_V_RH) & CNTRL_M_RH)
#define CNTRL_RH(x)     (((x) & CNTRL_M_RH) << CNTRL_V_RH)
#define DEV_V_RH        (DEV_V_UF + 1)                 /* Type RH20 */
#define DEV_M_RH        (1 << DEV_V_RH)
#define TYPE_RH10       (0 << DEV_V_RH)
#define TYPE_RH20       (1 << DEV_V_RH)

#if KL
/* DTE memory access functions, n = DTE# */
extern int      Mem_examine_word(int n, int wrd, uint64 *data);
extern int      Mem_deposit_word(int n, int wrd, uint64 *data);
extern int      Mem_read_byte(int n, uint16 *data, int byte);
extern int      Mem_write_byte(int n, uint16 *data);
#endif

/*
 * Access main memory. Returns 0 if access ok, 1 if out of memory range.
 * On KI10 and KL10, optional EPT flag indicates address relative to ept.
 */
extern int      Mem_read_word(t_addr addr, uint64 *data, int ept);
extern int      Mem_write_word(t_addr addr, uint64 *data, int ept);

#if MPX_DEV
extern void set_interrupt_mpx(int dev, int lvl, int mpx);
#else
#define set_interrupt_mpx(d,l,m)   set_interrupt(d,l)
#endif
extern void     set_interrupt(int dev, int lvl);
extern void     clr_interrupt(int dev);
extern void     check_apr_irq();
extern int      check_irq_level();
extern void     restore_pi_hold();
extern void     set_pi_hold();
extern UNIT     cpu_unit[];
extern UNIT     ten11_unit[];
#if KL
extern DEVICE   dte_dev;
extern DEVICE   lp20_dev;
extern DEVICE   tty_dev;
extern DEVICE   nia_dev;
#else
extern DEVICE   cty_dev;
#endif
extern DEVICE   cpu_dev;
extern DEVICE   mt_dev;
extern DEVICE   dpa_dev;
extern DEVICE   dpb_dev;
extern DEVICE   dpc_dev;
extern DEVICE   dpd_dev;
extern DEVICE   imp_dev;
extern DEVICE   rpa_dev;
extern DEVICE   rpb_dev;
extern DEVICE   rpc_dev;
extern DEVICE   rpd_dev;
extern DEVICE   rsa_dev;
extern DEVICE   tua_dev;
extern DEVICE   lpt_dev;
extern DEVICE   ptp_dev;
extern DEVICE   ptr_dev;
extern DEVICE   cr_dev;
extern DEVICE   cp_dev;
extern DEVICE   rca_dev;
extern DEVICE   rcb_dev;
extern DEVICE   dc_dev;
extern DEVICE   dt_dev;
extern DEVICE   pmp_dev;
extern DEVICE   dk_dev;
extern DEVICE   pd_dev;
extern DEVICE   pclk_dev;
extern DEVICE   dpy_dev;
extern DEVICE   iii_dev;
extern DEVICE   imx_dev;
extern DEVICE   imp_dev;
extern DEVICE   ch10_dev;
extern DEVICE   stk_dev;
extern DEVICE   tk10_dev;
extern DEVICE   mty_dev;
extern DEVICE   ten11_dev;
extern DEVICE   dkb_dev;
extern DEVICE   auxcpu_dev;
extern DEVICE   slave_dev;
extern DEVICE   dpk_dev;
extern DEVICE   wcnsls_dev;             /* MIT Spacewar Consoles */
extern DEVICE   ocnsls_dev;             /* Old MIT Spacewar Consoles */
extern DEVICE   ai_dev;
extern DEVICE   dct_dev;                /* PDP6 devices. */
extern DEVICE   dtc_dev;
extern DEVICE   mtc_dev;
extern DEVICE   dsk_dev;
extern DEVICE   dcs_dev;

extern t_stat (*dev_tab[128])(uint32 dev, t_uint64 *data);

#define VEC_DEVMAX      8                               /* max device vec */

/* DF10 Interface */
struct df10 {
      uint32         status;     /* DF10 status word */
      uint32         cia;        /* Initial transfer address */
      uint32         ccw;        /* Next control word address */
      uint32         wcr;        /* CUrrent word count */
      uint32         cda;        /* Current transfer address */
      uint32         devnum;     /* Device number */
      t_uint64       buf;        /* Data buffer */
      uint8          nxmerr;     /* Bit to set for NXM */
      uint8          ccw_comp;   /* Have we written out CCW */
} ;

/* RH10/RH20 Interface */
struct rh_if {
      int            (*dev_write)(DEVICE *dptr, struct rh_if *rh, int reg, uint32 data);
      int            (*dev_read)(DEVICE *dptr, struct rh_if *rh, int reg, uint32 *data);
      void           (*dev_reset)(DEVICE *dptr);
      t_uint64       buf;        /* Data buffer */
      uint32         status;     /* DF10 status word */
      uint32         cia;        /* Initial transfer address */
      uint32         ccw;        /* Current word count */
      uint32         wcr;
      uint32         cda;        /* Current transfer address */
      uint32         devnum;     /* Device number */
      int            ivect;      /* Interrupt vector */
      uint8          imode;      /* Mode of vector */
      int            cop;        /* RH20 Channel operator */
      uint32         sbar;       /* RH20 Starting address */
      uint32         stcr;       /* RH20 Count */
      uint32         pbar;
      uint32         ptcr;
      int            reg;        /* Last register selected */
      int            drive;      /* Last drive selected */
      int            rae;        /* Access register error */
      int            attn;       /* Attention bits */
      int            xfer_drive; /* Current transfering drive */
};

/* Device context block */
struct pdp_dib {
    uint32              dev_num;                        /* device address */
    uint32              num_devs;                       /* length */
    t_stat              (*io)(uint32 dev, t_uint64 *data);
    t_addr              (*irq)(uint32 dev, t_addr addr);
    struct rh_if        *rh;
};
 
#define RH10_DEV        01000
#define RH20_DEV        02000
struct rh_dev {
    uint32              dev_num;
    DEVICE             *dev;
    struct rh_if       *rh;
};

typedef struct pdp_dib DIB;

void df10_setirq(struct df10 *df);
void df10_writecw(struct df10 *df);
void df10_finish_op(struct df10 *df, int flags);
void df10_setup(struct df10 *df, uint32 addr);
int  df10_fetch(struct df10 *df);
int  df10_read(struct df10 *df);
int  df10_write(struct df10 *df);
#if PDP6_DEV
int  dct_read(int u, t_uint64 *data, int c);
int  dct_write(int u, t_uint64 *data, int c);
int  dct_is_connect(int u);
#endif

/* Define RH10/RH20 functions */
t_stat  rh_set_type(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat  rh_show_type (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat  rh_devio(uint32 dev, t_uint64 *data);
t_addr  rh_devirq(uint32 dev, t_addr addr);
#if KL
void    rh20_setup(struct rh_if *rhc);
#endif
void    rh_setup(struct rh_if *rh, uint32 addr);
void    rh_setattn(struct rh_if *rh, int unit);
void    rh_error(struct rh_if *rh);
int     rh_blkend(struct rh_if *rh);
void    rh_setirq(struct rh_if *rh);
void    rh_writecw(struct rh_if *rh, int nxm);
void    rh_finish_op(struct rh_if *rh, int flags);
int     rh_read(struct rh_if *rh);
int     rh_write(struct rh_if *rh);


int ten11_read (t_addr addr, t_uint64 *data);
int ten11_write (t_addr addr, t_uint64 data);

/* Console lights. */
extern void ka10_lights_init (void);
extern void ka10_lights_main (t_uint64);
extern void ka10_lights_set_aux (int);
extern void ka10_lights_clear_aux (int);


/* I/O system parameters */
#define NUM_DEVS_LP     1
#if KL
#define NUM_DEVS_PT     0
#define NUM_DEVS_CR     0
#define NUM_DEVS_CP     0
#else
#define NUM_DEVS_PT     1
#define NUM_DEVS_CR     1
#define NUM_DEVS_CP     1
#endif
#define NUM_DEVS_DPY    USE_DISPLAY
#define NUM_DEVS_WCNSLS USE_DISPLAY
#define NUM_DEVS_OCNSLS USE_DISPLAY
#if PDP6_DEV
#define NUM_DEVS_DTC    1
#define NUM_DEVS_DCT    2
#define NUM_DEVS_MTC    1
#define NUM_DEVS_DSK    1
#define NUM_DEVS_DCS    1
#define NUM_DEVS_SLAVE  PDP6
#endif
#if !PDP6
#define NUM_DEVS_DC     1
#define NUM_DEVS_MT     1
#if KL
#define NUM_DEVS_RC     0
#define NUM_DEVS_DT     0
#define NUM_DEVS_DK     0
#define NUM_DEVS_DP     0
#define NUM_DEVS_LP20   1
#define NUM_DEVS_TTY    1
#define NUM_LINES_TTY   64
#define NUM_DEVS_NIA    1
#else
#define NUM_DEVS_RC     1
#define NUM_DEVS_DT     1
#define NUM_DEVS_DK     1
#define NUM_DEVS_DP     2
#define NUM_DEVS_LP20   0
#define NUM_DEVS_TTY    0
#define NUM_DEVS_NIA    0
#endif
#define NUM_DEVS_RP     4
#define NUM_DEVS_RS     1
#define NUM_DEVS_TU     1
#define NUM_DEVS_PMP    WAITS
#define NUM_DEVS_DKB    (WAITS * USE_DISPLAY)
#define NUM_DEVS_III    (WAITS * USE_DISPLAY)
#define NUM_DEVS_PD     ITS | KL_ITS
#define NUM_DEVS_PCLK   WAITS
#define NUM_DEVS_IMX    ITS
#define NUM_DEVS_STK    ITS
#define NUM_DEVS_TK10   ITS
#define NUM_DEVS_MTY    ITS
#define NUM_DEVS_TEN11  ITS
#define NUM_DEVS_AUXCPU ITS
#define NUM_DEVS_IMP    1
#define NUM_DEVS_CH10   ITS | KL_ITS
#define NUM_DEVS_DPK    ITS
#define NUM_DEVS_AI     ITS
#endif
#if MAGIC_SWITCH && !KA && !ITS
#error "Magic switch only valid on KA10 with ITS mods"
#endif

/* Global data */


extern t_bool sim_idle_enab;
extern struct rh_dev rh[];
extern t_uint64   M[MAXMEMSIZE];
extern t_uint64   FM[];
extern uint32   PC;
extern uint32   FLAGS;

#if NUM_DEVS_AUXCPU
extern t_addr   auxcpu_base;
int auxcpu_read (t_addr addr, uint64 *);
int auxcpu_write (t_addr addr, uint64);
extern UNIT     auxcpu_unit[];
#endif
#if NUM_DEVS_SLAVE
//int slave_read (t_addr addr);
//int slave_write (t_addr addr, uint64);
//extern UNIT     slave_unit[];
#endif

#endif
