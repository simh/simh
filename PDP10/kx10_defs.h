/* ka10_defs.h: PDP-10 simulator definitions

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

#ifndef KLA
#define KLA 0
#endif

#ifndef KLB
#define KLB 0
#endif

#ifndef KL               /* Either KL10A or KL10B */
#define KL (KLA+KLB)
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

#ifndef PDP6_DEV       /* Include PDP6 devices */
#define PDP6_DEV PDP6|WAITS
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
#define BIT1     00200000000000LL
#define BIT7     00002000000000LL
#define BIT8     00001000000000LL
#define BIT9     00000400000000LL
#define BIT10    00000200000000LL
#define BIT10_35 00000377777777LL
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
#else
#define AMASK           RMASK
#define WMASK           RMASK
#define CSHIFT          18
#endif

#define API_MASK        0000000007
#define PI_ENABLE       0000000010      /* Clear DONE */
#define BUSY            0000000020      /* STOP */
#define CCW_COMP        0000000040      /* Write Final CCW */

#if KI
#define DEF_SERIAL      514             /* Default DEC test machine */
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

/* Flags for CPU unit */
#define UNIT_V_MSIZE    (UNIT_V_UF + 0)
#define UNIT_MSIZE      (0177 << UNIT_V_MSIZE)
#define UNIT_V_MAOFF    (UNIT_V_MSIZE + 8)
#define UNIT_V_PAGE     (UNIT_V_MAOFF + 1)
#define UNIT_MAOFF      (1 << UNIT_V_MAOFF)
#define UNIT_TWOSEG     (1 << UNIT_V_PAGE)
#define UNIT_ITSPAGE    (2 << UNIT_V_PAGE)
#define UNIT_BBNPAGE    (4 << UNIT_V_PAGE)
#define UNIT_M_PAGE     (007 << UNIT_V_PAGE)
#define UNIT_V_WAITS    (UNIT_V_PAGE + 3)
#define UNIT_M_WAITS    (1 << UNIT_V_WAITS)
#define UNIT_WAITS      (UNIT_M_WAITS)        /* Support for WAITS xct and fix */
#define UNIT_V_MPX      (UNIT_V_WAITS + 1)
#define UNIT_M_MPX      (1 << UNIT_V_MPX)
#define UNIT_MPX        (UNIT_M_MPX)          /* MPX Device for ITS */


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
extern UNIT     auxcpu_unit[];
extern DEVICE   cpu_dev;
extern DEVICE   cty_dev;
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
extern DEVICE   dpy_dev;
extern DEVICE   imx_dev;
extern DEVICE   imp_dev;
extern DEVICE   ch10_dev;
extern DEVICE   stk_dev;
extern DEVICE   tk10_dev;
extern DEVICE   mty_dev;
extern DEVICE   ten11_dev;
extern DEVICE   dkb_dev;
extern DEVICE   auxcpu_dev;
extern DEVICE   dpk_dev;
extern DEVICE   wcnsls_dev;             /* MIT Spacewar Consoles */
extern DEVICE   dct_dev;                /* PDP6 devices. */
extern DEVICE   dtc_dev;
extern DEVICE   mtc_dev;
extern DEVICE   dsk_dev;
extern DEVICE   dcs_dev;

extern t_stat (*dev_tab[128])(uint32 dev, t_uint64 *data);

#define VEC_DEVMAX      8                               /* max device vec */

/* Device context block */
struct pdp_dib {
    uint32              dev_num;                        /* device address */
    uint32              num_devs;                       /* length */
    t_stat              (*io)(uint32 dev, t_uint64 *data);
    int                 (*irq)(uint32 dev, int addr);
};

#define RH10_DEV        01000
struct rh_dev {
    uint32              dev_num;
    DEVICE             *dev;
};


typedef struct pdp_dib DIB;


/* DF10 Interface */
struct df10 {
        uint32  status;
        uint32  cia;
        uint32  ccw;
        uint32  wcr;
        uint32  cda;
        uint32  devnum;
        t_uint64  buf;
        uint8   nxmerr;
        uint8   ccw_comp;
} ;


void df10_setirq(struct df10 *df) ;
void df10_writecw(struct df10 *df) ;
void df10_finish_op(struct df10 *df, int flags) ;
void df10_setup(struct df10 *df, uint32 addr);
int  df10_fetch(struct df10 *df);
int  df10_read(struct df10 *df);
int  df10_write(struct df10 *df);
#if PDP6_DEV
int  dct_read(int u, t_uint64 *data, int c);
int  dct_write(int u, t_uint64 *data, int c);
int  dct_is_connect(int u);
#endif

int ten11_read (int addr, t_uint64 *data);
int ten11_write (int addr, t_uint64 data);

/* Console lights. */
extern void ka10_lights_init (void);
extern void ka10_lights_main (t_uint64);
extern void ka10_lights_set_aux (int);
extern void ka10_lights_clear_aux (int);

int auxcpu_read (int addr, t_uint64 *);
int auxcpu_write (int addr, t_uint64);

/* I/O system parameters */
#define NUM_DEVS_LP     1
#define NUM_DEVS_PT     1
#define NUM_DEVS_CR     1
#define NUM_DEVS_CP     1
#define NUM_DEVS_DPY    USE_DISPLAY
#define NUM_DEVS_WCNSLS USE_DISPLAY
#if PDP6_DEV
#define NUM_DEVS_DTC    1
#define NUM_DEVS_DCT    2
#define NUM_DEVS_MTC    1
#define NUM_DEVS_DSK    1
#define NUM_DEVS_DCS    1
#endif
#if !PDP6
#define NUM_DEVS_DC     1
#define NUM_DEVS_MT     1
#define NUM_DEVS_RC     1
#define NUM_DEVS_DT     1
#define NUM_DEVS_DK     1
#define NUM_DEVS_DP     2
#define NUM_DEVS_RP     4
#define NUM_DEVS_RS     1
#define NUM_DEVS_TU     1
#define NUM_DEVS_PMP    WAITS
#define NUM_DEVS_DKB    WAITS
#define NUM_DEVS_PD     ITS
#define NUM_DEVS_IMX    ITS
#define NUM_DEVS_STK    ITS
#define NUM_DEVS_TK10   ITS
#define NUM_DEVS_MTY    ITS
#define NUM_DEVS_TEN11  ITS
#define NUM_DEVS_AUXCPU ITS
#define NUM_DEVS_IMP    1
#define NUM_DEVS_CH10   ITS
#define NUM_DEVS_DPK    ITS
#endif
/* Global data */


extern t_bool sim_idle_enab;
extern struct rh_dev rh[];
extern t_uint64   M[MAXMEMSIZE];
extern t_uint64   FM[];
extern uint32   PC;
extern uint32   FLAGS;

#endif
