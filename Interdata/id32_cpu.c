/* id32_cpu.c: Interdata 32b CPU simulator

   Copyright (c) 2000-2017, Robert M. Supnik

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

   cpu                  Interdata 32b CPU

   09-Mar-17    RMS     OC to display testing wrong argument (COVERITY)
   28-Apr-07    RMS     Removed clock initialization
   27-Oct-06    RMS     Added idle support
                        Removed separate PASLA clock
   09-Mar-06    RMS     Added 8 register bank support for 8/32
   06-Feb-06    RMS     Fixed bug in DH (Mark Hittinger)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   10-Mar-05    RMS     Fixed bug in initial memory allocation
                RMS     Fixed bug in show history routine (Mark Hittinger)
                RMS     Revised examine/deposit to do words rather than bytes
   18-Feb-05    RMS     Fixed branches to mask new PC (Greg Johnson)
   06-Nov-04    RMS     Added =n to SHOW HISTORY
   25-Jan-04    RMS     Revised for device debug support
   31-Dec-03    RMS     Fixed bug in cpu_set_hist
   22-Sep-03    RMS     Added additional instruction decode types
                        Added instruction history

   The register state for an Interdata 32b CPU is:

   REG[0:F][2]<0:31>    general register sets
   F[0:7]<0:31>         single precision floating point registers
   D[0:7]<0:63>         double precision floating point registers
   PSW<0:63>            processor status word, including
    STAT<0:11>          status flags
    CC<0:3>             condition codes
    PC<0:31>            program counter
   int_req[n]<0:31>     interrupt requests
   int_enb[n]<0:31>     interrupt enables
   
   The Interdata 32b systems have seven instruction formats: register to
   register, short format, register and memory (three formats), and register
   and immediate (two formats).  The formats are:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     R2    |    register-register
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     N     |    short format
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |    register-memory 1
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    (absolute 14b)
   | 0| 0|              address                    |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |    register-memory 2
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    (relative)
   | 1|                 address                    |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

      0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |    register-memory 3
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    (double index)
   | 0| 1| 0| 0|    RX2    |       address hi      |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                   address lo                  |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |    register-immediate 1
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                   immediate                   |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |    register-immediate 2
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                  immediate hi                 |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                  immediate lo                 |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   For register-memory 1 and register-immediate 1 and 2 an instructions, an
   effective address is calculated as follows:

        effective addr = address + RX (if RX > 0)

   For register-memory 2, an effective address is calculated as follows:

        effective addr = address + PC + RX (if RX > 0)

   For register-memory 3, an effective address is calculated as follows:

        effective addr = address + RX (if RX > 0) + RX2 (if RX2 > 0)

   Register-memory instructions can access an address space of 16M bytes.

   This routine is the instruction decode routine for the Interdata CPU.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        wait state and no I/O outstanding
        invalid instruction
        I/O error in I/O simulator

   2. Interrupts.  Each device has an interrupt armed flag, an interrupt
      request flag, and an interrupt enabled flag.  To facilitate evaluation,
      all interrupt requests are kept in int_req, and all enables in int_enb.
      Interrupt armed flags are local to devices.  If external interrupts are
      enabled in the PSW, and a request is pending, an interrupt occurs.

   3. Non-existent memory.  On the Interdata 32b, reads to non-existent
      memory return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        id_defs.h       add device interrupt definitions
        id32_sys.c      add sim_devices table entry
*/

#include "id_defs.h"
#include <setjmp.h>

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = oPC
#define VAMASK          VAMASK32
#define NRSETS          8                               /* up to 8 reg sets */
#define PSW_MASK        PSW_x32
#define ABORT(val)      longjmp (save_env, (val))
#define MPRO            (-1)

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)                 /* dummy mask */
#define UNIT_V_DPFP     (UNIT_V_UF + 1)
#define UNIT_V_832      (UNIT_V_UF + 2)
#define UNIT_V_8RS      (UNIT_V_UF + 3)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define UNIT_DPFP       (1 << UNIT_V_DPFP)
#define UNIT_832        (1 << UNIT_V_832)
#define UNIT_8RS        (1 << UNIT_V_8RS)
#define UNIT_TYPE       (UNIT_DPFP | UNIT_832)

#define HIST_PC         0x40000000
#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    uint32              pc;
    uint32              ir1;
    uint32              ir2;
    uint32              ir3;
    uint32              r1;
    uint32              ea;
    uint32              opnd;
    } InstHistory;

#define PSW_GETREG(x)   (((x) >> PSW_V_REG) & psw_reg_mask)
#define SEXT32(x)       (((x) & SIGN32)? ((int32) ((x) | ~0x7FFFFFFF)): \
                        ((int32) ((x) & 0x7FFFFFFF)))
#define SEXT16(x)       (((x) & SIGN16)? ((int32) ((x) | ~0x7FFF)): \
                        ((int32) ((x) & 0x7FFF)))
#define SEXT15(x)       (((x) & 0x4000)? ((int32) ((x) | ~0x3FFF)): \
                        ((int32) ((x) & 0x3FFF)))
#define CC_GL_16(x)     if ((x) & SIGN16) \
                            cc = CC_L; \
                        else if (x) \
                            cc = CC_G; \
                        else cc = 0
#define CC_GL_32(x)     if ((x) & SIGN32) \
                            cc = CC_L; \
                        else if (x) \
                            cc = CC_G; \
                        else cc = 0
#define BUILD_PSW(x)    (((PSW & ~CC_MASK) | (x)) & PSW_MASK)
#define NEG(x)          ((~(x) + 1) & DMASK32)
#define ABS(x)          (((x) & SIGN32)? NEG (x): (x))
#define DNEG(x,y)       y = NEG (y); \
                        x = (~(x) + (y == 0)) & DMASK32

/* Logging */

#define LOG_CPU_I       0x0001                          /* intr/exception */
#define LOG_CPU_C       0x0002                          /* context change */

uint32 GREG[16 * NRSETS] = { 0 };                       /* general registers */
uint32 *M = NULL;                                       /* memory */
uint32 *R = &GREG[0];                                   /* working reg set */
uint32 F[8] = { 0 };                                    /* sp fp registers */
dpr_t D[8] = { {0} };                                   /* dp fp registers */
uint32 PSW = 0;                                         /* processor status word */
uint32 PC = 0;                                          /* program counter */
uint32 oPC = 0;                                         /* PC at inst start */
uint32 SR = 0;                                          /* switch register */
uint32 DR = 0;                                          /* display register */
uint32 DRX = 0;                                         /* display extension */
uint32 drmod = 0;                                       /* mode */
uint32 srpos = 0;                                       /* switch register pos */
uint32 drpos = 0;                                       /* display register pos */
uint32 mac_reg[MAC_LNT] = { 0 };                        /* mac registers */
uint32 mac_sta = 0;                                     /* mac status */
uint32 int_req[INTSZ] = { 0 };                          /* interrupt requests */
uint32 int_enb[INTSZ] = { 0 };                          /* interrupt enables */
uint32 qevent = 0;                                      /* events */
uint32 stop_inst = 0;                                   /* stop on ill inst */
uint32 stop_wait = 0;                                   /* stop on wait */
uint32 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
uint32 dec_flgs = 0;                                    /* decode flags */
uint32 fp_in_hwre = 0;                                  /* ucode vs hwre fp */
uint32 pawidth = PAWIDTH32;                             /* addr mask */
uint32 hst_p = 0;                                       /* history pointer */
uint32 hst_lnt = 0;                                     /* history length */
uint32 psw_reg_mask = 1;                                /* PSW reg mask */
InstHistory *hst = NULL;                                /* instruction history */
jmp_buf save_env;                                       /* abort handler */
struct BlockIO blk_io;                                  /* block I/O status */
uint32 (*dev_tab[DEVNO])(uint32 dev, uint32 op, uint32 datout) = { NULL };

uint32 ReadB (uint32 loc, uint32 rel);
uint32 ReadH (uint32 loc, uint32 rel);
void WriteB (uint32 loc, uint32 val, uint32 rel);
void WriteH (uint32 loc, uint32 val, uint32 rel);
uint32 RelocT (uint32 va, uint32 base, uint32 rel, uint32 *pa);
uint32 int_auto (uint32 dev, uint32 cc);
uint32 addtoq (uint32 ea, uint32 val, uint32 flg);
uint32 remfmq (uint32 ea, uint32 r1, uint32 flg);
uint32 exception (uint32 loc, uint32 cc, uint32 flg);
uint32 newPSW (uint32 val);
uint32 testsysq (uint32 cc);
uint32 display (uint32 dev, uint32 op, uint32 dat);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_consint (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
void set_r_display (uint32 *rbase);

extern t_bool devtab_init (void);
extern void int_eval (void);
extern uint32 int_getdev (void);
extern void sch_cycle (uint32 ch);
extern t_bool sch_blk (uint32 dev);
extern uint32 f_l (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_c (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_as (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_m (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_d (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_fix32 (uint32 op, uint32 r1, uint32 r2);
extern uint32 f_flt32 (uint32 op, uint32 r1, uint32 r2);

/* Instruction decoding table */

const uint16 decrom[256] = {
    0,                                                  /* 00 */
    OP_RR,                                              /* BALR */
    OP_RR,                                              /* BTCR */
    OP_RR,                                              /* BFCR */
    OP_RR,                                              /* NR */
    OP_RR,                                              /* CLR */
    OP_RR,                                              /* OR */
    OP_RR,                                              /* XR */
    OP_RR,                                              /* LR */
    OP_RR,                                              /* CR */
    OP_RR,                                              /* AR */
    OP_RR,                                              /* SR */
    OP_RR,                                              /* MHR */
    OP_RR,                                              /* DHR */
    0, 0,                                               /* 0E:0F */
    OP_NO,                                              /* SRLS */
    OP_NO,                                              /* SLLS */
    OP_RR,                                              /* CHVR */
    0, 0, 0, 0, 0,                                      /* 13:17 */
    OP_RR | OP_PRV,                                     /* LPSWR */
    0, 0, 0,                                            /* 19:1B */
    OP_RR,                                              /* MR */
    OP_RR,                                              /* DR */
    0, 0,                                               /* 1E:1F */
    OP_NO,                                              /* BTBS */
    OP_NO,                                              /* BTFS */
    OP_NO,                                              /* BFBS */
    OP_NO,                                              /* BFFS */
    OP_NO,                                              /* LIS */
    OP_NO,                                              /* LCS */
    OP_NO,                                              /* AIS */
    OP_NO,                                              /* SIS */
    OP_NO,                                              /* LER */
    OP_NO,                                              /* CER */
    OP_NO,                                              /* AER */
    OP_NO,                                              /* SER */
    OP_NO,                                              /* MER */
    OP_NO,                                              /* DER */
    OP_NO,                                              /* FXR */
    OP_NO,                                              /* FLR */
    0,                                                  /* MPBSR - 8/32C */
    0,                                                  /* 31 */
    0,                                                  /* PBR - 8/32C */
    0,                                                  /* 33 */
    OP_RR,                                              /* EXHR */
    0, 0, 0,                                            /* 35:37 */
    OP_NO | OP_DPF,                                     /* LDR */
    OP_NO | OP_DPF,                                     /* CDR */
    OP_NO | OP_DPF,                                     /* ADR */
    OP_NO | OP_DPF,                                     /* SDR */
    OP_NO | OP_DPF,                                     /* MDR */
    OP_NO | OP_DPF,                                     /* DDR */
    OP_NO | OP_DPF,                                     /* FXDR */
    OP_NO | OP_DPF,                                     /* FLDR */
    OP_RX,                                              /* STH */
    OP_RX,                                              /* BAL */
    OP_RX,                                              /* BTC */
    OP_RX,                                              /* BFC */
    OP_RXH,                                             /* NH */
    OP_RXH,                                             /* CLH */
    OP_RXH,                                             /* OH */
    OP_RXH,                                             /* XH */
    OP_RXH,                                             /* LH */
    OP_RXH,                                             /* CH */
    OP_RXH,                                             /* AH */
    OP_RXH,                                             /* SH */
    OP_RXH,                                             /* MH */
    OP_RXH,                                             /* DH */
    0, 0,                                               /* 4E:4F */
    OP_RX,                                              /* ST */
    OP_RXF,                                             /* AM */
    0, 0,                                               /* 52:53 */
    OP_RXF,                                             /* N */
    OP_RXF,                                             /* CL */
    OP_RXF,                                             /* O */
    OP_RXF,                                             /* X */
    OP_RXF,                                             /* L */
    OP_RXF,                                             /* C */
    OP_RXF,                                             /* A */
    OP_RXF,                                             /* S */
    OP_RXF,                                             /* M */
    OP_RXF,                                             /* D */
    OP_RXH,                                             /* CRC12 */
    OP_RXH,                                             /* CRC16 */
    OP_RX,                                              /* STE */
    OP_RXH,                                             /* AHM */
    0,                                                  /* PB - 8/32C */
    OP_RX,                                              /* LRA */
    OP_RX,                                              /* ATL */
    OP_RX,                                              /* ABL */
    OP_RX,                                              /* RTL */
    OP_RX,                                              /* RBL */
    OP_RX,                                              /* LE */
    OP_RX,                                              /* CE */
    OP_RX,                                              /* AE */
    OP_RX,                                              /* SE */
    OP_RX,                                              /* ME */
    OP_RX,                                              /* DE */
    0, 0,                                               /* 6E:6F */
    OP_RX | OP_DPF,                                     /* STD */
    OP_RX,                                              /* SME */
    OP_RX,                                              /* LME */
    OP_RXH,                                             /* LHL */
    OP_RX,                                              /* TBT */
    OP_RX,                                              /* SBT */
    OP_RX,                                              /* RBT */
    OP_RX,                                              /* CBT */
    OP_RX | OP_DPF,                                     /* LD */
    OP_RX | OP_DPF,                                     /* CD */
    OP_RX | OP_DPF,                                     /* AD */
    OP_RX | OP_DPF,                                     /* SD */
    OP_RX | OP_DPF,                                     /* MD */
    OP_RX | OP_DPF,                                     /* DD */
    OP_RX | OP_DPF,                                     /* STMD */
    OP_RX | OP_DPF,                                     /* LMD */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 80:8F */
    0, 0, 0, 0, 0, 0, 0, 0,
    OP_NO,                                              /* SRHLS */
    OP_NO,                                              /* SLHLS */
    OP_NO,                                              /* STBR */
    OP_RR,                                              /* LDBR */
    OP_RR,                                              /* EXBR */
    OP_NO | OP_PRV,                                     /* EPSR */
    OP_RR | OP_PRV,                                     /* WBR */
    OP_RR | OP_PRV,                                     /* RBR */
    OP_RR | OP_PRV,                                     /* WHR */
    OP_RR | OP_PRV,                                     /* RHR */
    OP_RR | OP_PRV,                                     /* WDR */
    OP_RR | OP_PRV,                                     /* RDR */
    0,                                                  /* 9C */
    OP_RR | OP_PRV,                                     /* SSR */
    OP_RR | OP_PRV,                                     /* OCR */
    0,                                                  /* 9F */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* A0:AF */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,                             /* B0:BF */
    0, 0, 0, 0, 0, 0, 0, 0,
    OP_RX,                                              /* BXH */
    OP_RX,                                              /* BXLE */
    OP_RXF | OP_PRV,                                    /* LPSW */
    OP_RI1,                                             /* THI */
    OP_RI1,                                             /* NHI */
    OP_RI1,                                             /* CLHI */
    OP_RI1,                                             /* OHI */
    OP_RI1,                                             /* XHI */
    OP_RI1,                                             /* LHI */
    OP_RI1,                                             /* CHI */
    OP_RI1,                                             /* AHI */
    OP_RI1,                                             /* SHI */
    OP_RI1,                                             /* SRHL */
    OP_RI1,                                             /* SLHL */
    OP_RI1,                                             /* SRHA */
    OP_RI1,                                             /* SLHA */
    OP_RX,                                              /* STM */
    OP_RX,                                              /* LM */
    OP_RX,                                              /* STB */
    OP_RXB,                                             /* LDB */
    OP_RXB,                                             /* CLB */
    OP_RX | OP_PRV,                                     /* AL */
    OP_RXF | OP_PRV,                                    /* WB */
    OP_RXF | OP_PRV,                                    /* RB */
    OP_RX | OP_PRV,                                     /* WH */
    OP_RX | OP_PRV,                                     /* RH */
    OP_RX | OP_PRV,                                     /* WD */
    OP_RX | OP_PRV,                                     /* RD */
    0,                                                  /* DC */
    OP_RX | OP_PRV,                                     /* SS */
    OP_RX | OP_PRV,                                     /* OC */
    0,                                                  /* DF */
    OP_RXH,                                             /* TS */
    OP_RX,                                              /* SVC */
    OP_RI1 | OP_PRV,                                    /* SINT */
    OP_RXH | OP_PRV,                                    /* SCP */
    0, 0,                                               /* E4:E5 */
    OP_RX,                                              /* LA */
    OP_RXF,                                             /* TLATE */
    0, 0,                                               /* E8:E9 */
    OP_RI1,                                             /* RRL */
    OP_RI1,                                             /* RLL */
    OP_RI1,                                             /* SRL */
    OP_RI1,                                             /* SLL */
    OP_RI1,                                             /* SRA */
    OP_RI1,                                             /* SLA */
    0, 0, 0,                                            /* F0:F2 */
    OP_RI2,                                             /* TI */
    OP_RI2,                                             /* NI */
    OP_RI2,                                             /* CLI */
    OP_RI2,                                             /* OI */
    OP_RI2,                                             /* XI */
    OP_RI2,                                             /* LI */
    OP_RI2,                                             /* CI */
    OP_RI2,                                             /* AI */
    OP_RI2,                                             /* SI */
    0, 0, 0, 0                                          /* FC:FF */
    };

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

DIB cpu_dib = { d_DS, -1, v_DS, NULL, &display };

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX | UNIT_BINK, MAXMEMSIZE32) };

REG cpu_reg[] = {
    { HRDATA (PC, PC, 20) },
    { HRDATA (OPC, oPC, 20), REG_HRO },
    { HRDATA (R0, GREG[0], 32) },
    { HRDATA (R1, GREG[1], 32) },
    { HRDATA (R2, GREG[2], 32) },
    { HRDATA (R3, GREG[3], 32) },
    { HRDATA (R4, GREG[4], 32) },
    { HRDATA (R5, GREG[5], 32) },
    { HRDATA (R6, GREG[6], 32) },
    { HRDATA (R7, GREG[7], 32) },
    { HRDATA (R8, GREG[8], 32) },
    { HRDATA (R9, GREG[9], 32) },
    { HRDATA (R10, GREG[10], 32) },
    { HRDATA (R11, GREG[11], 32) },
    { HRDATA (R12, GREG[12], 32) },
    { HRDATA (R13, GREG[13], 32) },
    { HRDATA (R14, GREG[14], 32) },
    { HRDATA (R15, GREG[15], 32) },
    { HRDATA (FR0, F[0], 32) },
    { HRDATA (FR2, F[1], 32) },
    { HRDATA (FR4, F[2], 32) },
    { HRDATA (FR6, F[3], 32) },
    { HRDATA (FR8, F[4], 32) },
    { HRDATA (FR10, F[5], 32) },
    { HRDATA (FR12, F[6], 32) },
    { HRDATA (FR14, F[7], 32) },
    { HRDATA (D0H, D[0].h, 32) },
    { HRDATA (D0L, D[0].l, 32) },
    { HRDATA (D2H, D[1].h, 32) },
    { HRDATA (D2L, D[1].l, 32) },
    { HRDATA (D4H, D[2].h, 32) },
    { HRDATA (D4L, D[2].l, 32) },
    { HRDATA (D6H, D[3].h, 32) },
    { HRDATA (D6L, D[3].l, 32) },
    { HRDATA (D8H, D[4].h, 32) },
    { HRDATA (D8L, D[4].l, 32) },
    { HRDATA (D10H, D[5].h, 32) },
    { HRDATA (D10L, D[5].l, 32) },
    { HRDATA (D12L, D[6].l, 32) },
    { HRDATA (D12H, D[6].h, 32) },
    { HRDATA (D14H, D[7].h, 32) },
    { HRDATA (D14L, D[7].l, 32) },
    { HRDATA (PSW, PSW, 16) },
    { HRDATA (CC, PSW, 4) },
    { HRDATA (SR, SR, 32) },
    { HRDATA (DR, DR, 32) },
    { HRDATA (DRX, DRX, 8) },
    { FLDATA (DRMOD, drmod, 0) },
    { FLDATA (SRPOS, srpos, 0) },
    { HRDATA (DRPOS, drpos, 3) },
    { BRDATA (IRQ, int_req, 16, 32, INTSZ) },
    { BRDATA (IEN, int_enb, 16, 32, INTSZ) },
    { BRDATA (MACREG, mac_reg, 16, 32, MAC_LNT) },
    { HRDATA (MACSTA, mac_sta, 5) },
    { HRDATA (QEVENT, qevent, 4), REG_HRO },
    { FLDATA (STOP_INST, stop_inst, 0) },
    { FLDATA (STOP_WAIT, stop_wait, 0) },
    { BRDATA (PCQ, pcq, 16, 20, PCQ_SIZE), REG_RO+REG_CIRC },
    { HRDATA (PCQP, pcq_p, 6), REG_HRO },
    { HRDATA (WRU, sim_int_char, 8) },
    { HRDATA (BLKIOD, blk_io.dfl, 16), REG_HRO },
    { HRDATA (BLKIOC, blk_io.cur, 20), REG_HRO },
    { HRDATA (BLKIOE, blk_io.end, 20), REG_HRO },
    { BRDATA (GREG, GREG, 16, 32, 16 * NRSETS) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_8RS|UNIT_TYPE, 0, NULL, "732", NULL },
    { UNIT_DPFP, UNIT_DPFP, NULL, "DPFP", NULL },
    { UNIT_TYPE, 0, "7/32, single precision fp", "732", NULL },
    { UNIT_TYPE, UNIT_DPFP, "7/32, double precision fp", NULL, NULL },
    { UNIT_8RS|UNIT_TYPE, UNIT_8RS|UNIT_DPFP|UNIT_832, NULL, "832", NULL },
    { UNIT_8RS, 0, NULL, "2RS", NULL },
    { UNIT_8RS|UNIT_TYPE, UNIT_8RS|UNIT_DPFP|UNIT_832, "832, 8 register sets", NULL, NULL },
    { UNIT_8RS|UNIT_TYPE, UNIT_DPFP|UNIT_832, "832, 2 register sets", NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
    { UNIT_MSIZE, 131072, NULL, "128K", &cpu_set_size },
    { UNIT_MSIZE, 262144, NULL, "256K", &cpu_set_size },
    { UNIT_MSIZE, 524288, NULL, "512K", &cpu_set_size },
    { UNIT_MSIZE, 1048756, NULL, "1M", &cpu_set_size },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "CONSINT",
      &cpu_set_consint, NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEBTAB cpu_deb[] = {
    { "INTEXC", LOG_CPU_I },
    { "CONTEXT", LOG_CPU_C },
    { NULL, 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 20, 2, 16, 16,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    &cpu_dib, DEV_DEBUG, 0,
    cpu_deb, NULL, NULL
    };

t_stat sim_instr (void)
{
volatile uint32 cc;                                     /* set before setjmp */
t_stat reason;                                          /* set after setjmp */
int abortval;

/* Restore register state */

if (devtab_init ())                                     /* check conflicts */
    return SCPE_STOP;
if (cpu_unit.flags & (UNIT_DPFP | UNIT_832)) {
    fp_in_hwre = 1;                                     /* fp in hwre */
    dec_flgs = 0;                                       /* all instr ok */
    }
else {
    fp_in_hwre = 0;                                     /* fp in ucode */
    dec_flgs = OP_DPF;                                  /* sp only */
    }
if (cpu_unit.flags & UNIT_8RS)                          /* 8 register sets */
    psw_reg_mask = 7;
else psw_reg_mask = 1;                                  /* 2 register sets */
int_eval ();                                            /* eval interrupts */
cc = newPSW (PSW & PSW_MASK);                           /* split PSW, eval wait */
reason = 0;

/* Abort handling

   If an abort occurs in memory protection, the relocation routine
   executes a longjmp to this area OUTSIDE the main simulation loop.
   Memory protection errors are the only sources of aborts in the
   Interdata 32b systems.  All referenced variables must be globals,
   and all sim_instr scoped automatic variables must be volatile or
   set after the call on setjmp.
*/

abortval = setjmp (save_env);                           /* set abort hdlr */
if (abortval != 0) {                                    /* mem mgt abort? */
    qevent = qevent | EV_MAC;                           /* set MAC intr */
    if (cpu_unit.flags & UNIT_832)                      /* 832? restore PC */
        PC = oPC;
    }

/* Event handling */

while (reason == 0) {                                   /* loop until halted */

    uint32 dev, drom, opnd, inc, lim, bufa;
    uint32 op, r1, r1p1, r2, rx2, ea = 0;
    uint32 mpy, mpc, dvr;
    uint32 i, rslt, rlo, t;
    uint32 ir1, ir2, ir3, ityp;
    int32 sr, st;

    if (sim_interval <= 0) {                            /* check clock queue */
        if ((reason = sim_process_event ()))
            break;
        int_eval ();
        }

    if (qevent) {                                       /* any events? */
        if (qevent & EV_MAC) {                          /* MAC interrupt? */
            qevent = 0;                                 /* clr all events */
            cc = exception (MPRPSW, cc, 0);             /* take exception */
            int_eval ();                                /* re-eval intr */
            continue;
            }

        if (qevent & EV_BLK) {                          /* block I/O in prog? */
            dev = blk_io.dfl & DEV_MAX;                 /* get device */
            cc = dev_tab[dev] (dev, IO_SS, 0) & 0xF;    /* sense status */
            if (cc == STA_BSY) {                        /* just busy? */
                sim_interval = 0;                       /* force I/O event */
                continue;
                }
            else if (cc == 0) {                         /* ready, no err? */
                if (blk_io.dfl & BL_RD) {               /* read? */
                    t = dev_tab[dev] (dev, IO_RD, 0);   /* get byte */
                    if ((t == 0) && (blk_io.dfl & BL_LZ))
                        continue;
                    blk_io.dfl = blk_io.dfl & ~BL_LZ;   /* non-zero seen */
                    WriteB (blk_io.cur, t, VW);         /* write mem */
                    }
                else {                                  /* write */
                    t = ReadB (blk_io.cur, VR);         /* read mem */
                    dev_tab[dev] (dev, IO_WD, t);       /* put byte */
                    }
                if (blk_io.cur != blk_io.end) {         /* more to do? */
                    blk_io.cur = (blk_io.cur + 1) & VAMASK;     /* incr addr */
                    continue;
                    }
                }
            qevent = qevent & ~EV_BLK;                  /* clr blk I/O flag */
            int_eval ();                                /* re-eval intr */
            continue;
            }

        if ((qevent & EV_INT) && (PSW & PSW_EXI)) {     /* interrupt? */
            dev = int_getdev ();                        /* get int dev */
            cc = int_auto (dev, cc);                    /* do auto intr */
            int_eval ();                                /* re-eval intr */
            continue;
            }

        if (PSW & PSW_WAIT) {                           /* wait state? */
            sim_idle (TMR_LFC, TRUE);                   /* idling */
            continue;
            }

        qevent = 0;                                     /* no events */
        }

/* Instruction fetch and decode */

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

    sim_interval = sim_interval - 1;

    ir1 = ReadH (oPC = PC, VE);                         /* fetch instr */
    op = (ir1 >> 8) & 0xFF;                             /* extract op,R1,R2 */
    r1 = (ir1 >> 4) & 0xF;
    r2 = ir1 & 0xF;
    drom = decrom[op];                                  /* get decode flags */
    ityp = drom & OP_MASK;                              /* instruction type */

    if ((drom == 0) || (drom & dec_flgs)) {             /* not in model? */
        if (stop_inst)                                  /* stop or */
            reason = STOP_RSRV;
        else cc = exception (ILOPSW, cc, 0);            /* exception */
        continue;
        }
    if ((drom & OP_PRV) && (PSW & PSW_PRO)) {           /* priv & protected? */
        cc = exception (ILOPSW, cc, 0);                 /* exception */
        continue;
        }

    switch (ityp) {                                     /* decode instruction */

    case OP_NO:                                         /* no operand */
        opnd = r2;                                      /* assume short */
        PC = (PC + 2) & VAMASK;                         /* increment PC */
        break;

    case OP_RR:                                         /* reg-reg */
        opnd = R[r2];                                   /* ea/operand is R2 */
        PC = (PC + 2) & VAMASK;                         /* increment PC */
        break;

    case OP_RI1:                                        /* reg-imm 1 */
        ir2 = ReadH ((PC + 2) & VAMASK, VE);            /* fetch immed */
        opnd = SEXT16 (ir2);                            /* sign extend */
        if (r2)                                         /* index calculation */
            opnd = (opnd + R[r2]) & DMASK32;
        PC = (PC + 4) & VAMASK;                         /* increment PC */
        break;

    case OP_RI2:                                        /* reg-imm 2 */
        ir2 = ReadH ((PC + 2) & VAMASK, VE);            /* fetch imm hi */
        ir3 = ReadH ((PC + 4) & VAMASK, VE);            /* fetch imm lo */
        opnd = (ir2 << 16) | ir3;                       /* 32b immediate */
        if (r2)                                         /* index calculation */
            opnd = (opnd + R[r2]) & DMASK32;
        PC = (PC + 6) & VAMASK;                         /* increment PC */
        break;

    case OP_RX: case OP_RXB: case OP_RXH: case OP_RXF:  /* reg-mem */
        ir2 = ReadH ((PC + 2) & VAMASK, VE);            /* fetch addr */
        if ((ir2 & 0xC000) == 0) {                      /* displacement? */
            PC = (PC + 4) & VAMASK;                     /* increment PC */
            ea = ir2;                                   /* abs 14b displ */
            }
        else if (ir2 & 0x8000) {                        /* relative? */
            PC = (PC + 4) & VAMASK;                     /* increment PC */
            ea = PC + SEXT15 (ir2);                     /* add to incr PC */
            }
        else {                                          /* absolute */
            rx2 = (ir2 >> 8) & 0xF;                     /* get second index */
            ea = (ir2 & 0xFF) << 16;                    /* shift to place */
            ir3 = ReadH ((PC + 4) & VAMASK, VE);        /* fetch addr lo */
            ea = ea | ir3;                              /* finish addr */
            if (rx2)                                    /* index calc 2 */
                ea = ea + R[rx2];
            PC = (PC + 6) & VAMASK;                     /* increment PC */
            }
        if (r2)                                         /* index calculation */
            ea = ea + R[r2];
        ea = ea & VAMASK;
        if (ityp == OP_RXF)                             /* get fw operand? */
            opnd = ReadF (ea, VR);
        else if (ityp == OP_RXH) {                      /* get hw operand? */
            t = ReadH (ea, VR);                         /* read halfword */
            opnd = SEXT16 (t);                          /* sign extend */
            }
        else if (ityp == OP_RXB)                        /* get byte opnd? */
            opnd = ReadB (ea, VR);
        else opnd = ea;                                 /* just address */
        break;

    default:
        return SCPE_IERR;
        }

    if (hst_lnt) {                                      /* instruction history? */
        hst[hst_p].pc = oPC | HIST_PC;                  /* save decode state */
        hst[hst_p].ir1 = ir1;
        hst[hst_p].ir2 = ir2;
        hst[hst_p].ir3 = ir3;
        hst[hst_p].r1 = R[r1];
        hst[hst_p].ea = ea;
        hst[hst_p].opnd = opnd;
        hst_p = hst_p + 1;
        if (hst_p >= hst_lnt)
            hst_p = 0;
        }
    if (qevent & EV_MAC)                                /* MAC abort on fetch? */
        continue; 
    switch (op) {                                       /* case on opcode */

/* Load/store instructions */

    case 0x08:                                          /* LR - RR */
    case 0x24:                                          /* LIS - NO */
    case 0x48:                                          /* LH - RXH */
    case 0x58:                                          /* L - RXF */
    case 0xC8:                                          /* LHI - RI1 */
    case 0xF8:                                          /* LI - RI2 */
        R[r1] = opnd;                                   /* load operand */
        CC_GL_32 (R[r1]);                               /* set G,L */
        break;

    case 0x73:                                          /* LHL - RXH */
        R[r1] = opnd & DMASK16;                         /* get op, zero ext */
        CC_GL_32 (R[r1]);                               /* set G, L */
        break;

    case 0x25:                                          /* LCS - NO */
        R[r1] = NEG (opnd);                             /* load complement */
        CC_GL_32 (R[r1]);                               /* set G,L */
        break;

    case 0xE6:                                          /* LA - RX */
        R[r1] = ea;                                     /* load addr */
        break;

    case 0x63:                                          /* LRA - RX */
        cc = RelocT (R[r1] & VAMASK, ea, VR, &R[r1]);   /* test reloc */
        break;

    case 0x40:                                          /* STH - RX */
        WriteH (ea, R[r1], VW);                         /* store register */
        break;

    case 0x50:                                          /* ST - RX */
        WriteF (ea, R[r1], VW);                         /* store register */
        break;

    case 0xD1:                                          /* LM - RX */
        for ( ; r1 <= 0xF; r1++) {                      /* loop thru reg */
            R[r1] = ReadF (ea, VR);                     /* load register */
            ea = (ea + 4) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0xD0:                                          /* STM - RX */
        for ( ; r1 <= 0xF; r1++) {                      /* loop thru reg */
            WriteF (ea, R[r1], VW);                     /* store register */
            ea = (ea + 4) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0xE0:                                          /* TS - RXH */
        CC_GL_16 (opnd);                                /* set cc's */
        WriteH (ea, opnd | SIGN16, VW);                 /* set MSB */
        break;

    case 0x93:                                          /* LDBR - RR */
    case 0xD3:                                          /* LDB - RXB */
        R[r1] = opnd & DMASK8;                          /* load byte */
        break;

    case 0x92:                                          /* STBR - NO */
        R[r2] = (R[r2] & ~DMASK8) | (R[r1] & DMASK8);   /* store byte */
        break;
    case 0xD2:                                          /* STB - RX */
        WriteB (ea, R[r1], VW);                         /* store byte */
        break;

    case 0x34:                                          /* EXHR - RR */
        R[r1] = ((opnd >> 16) & DMASK16) | ((opnd & DMASK16) << 16);
        break;

    case 0x94:                                          /* EXBR - RR */
        R[r1] = (R[r1] & ~DMASK16) |
            ((opnd >> 8) & DMASK8) | ((opnd & DMASK8) << 8);
        break;

/* Control instructions */

    case 0x01:                                          /* BALR - RR */
    case 0x41:                                          /* BAL - RX */
        PCQ_ENTRY;                                      /* save old PC */
        R[r1] = PC;                                     /* save cur PC */
        PC = opnd & VAMASK;                             /* branch */
        break;

    case 0x02:                                          /* BTCR - RR */
    case 0x42:                                          /* BTC - RX */
        if (cc & r1) {                                  /* test CC's */
            PCQ_ENTRY;                                  /* branch if true */
            PC = opnd & VAMASK;
            }
        break;

    case 0x20:                                          /* BTBS - NO */
        if (cc & r1) {                                  /* test CC's */
            PCQ_ENTRY;                                  /* branch if true */
            PC = (oPC - r2 - r2) & VAMASK;
            }
        break;

    case 0x21:                                          /* BTFS - NO */
        if (cc & r1) {                                  /* test CC's */
            PCQ_ENTRY;                                  /* branch if true */
            PC = (oPC + r2 + r2) & VAMASK;
            }
        break;

    case 0x03:                                          /* BFCR - RR */
    case 0x43:                                          /* BFC - RX */
        if ((cc & r1) == 0) {                           /* test CC's */
            PCQ_ENTRY;                                  /* branch if false */
            PC = opnd & VAMASK;
            }
        break;

    case 0x22:                                          /* BFBS - NO */
        if ((cc & r1) == 0) {                           /* test CC's */
            PCQ_ENTRY;                                  /* branch if false */
            PC = (oPC - r2 - r2) & VAMASK;
            }
        break;

    case 0x23:                                          /* BFFS - NO */
        if ((cc & r1) == 0) {                           /* test CC's */
            PCQ_ENTRY;                                  /* branch if false */
            PC = (oPC + r2 + r2) & VAMASK;
            }
        break;

    case 0xC0:                                          /* BXH - RX */
        inc = R[(r1 + 1) & 0xF];                        /* inc = R1 + 1 */
        lim = R[(r1 + 2) & 0xF];                        /* lim = R1 + 2 */
        R[r1] = (R[r1] + inc) & DMASK32;                /* R1 = R1 + inc */
        if (R[r1] > lim) {                              /* if R1 > lim */
            PCQ_ENTRY;                                  /* branch */
            PC = opnd & VAMASK;
            }
        break;

    case 0xC1:                                          /* BXLE - RX */
        inc = R[(r1 + 1) & 0xF];                        /* inc = R1 + 1 */
        lim = R[(r1 + 2) & 0xF];                        /* lim = R1 + 2 */
        R[r1] = (R[r1] + inc) & DMASK32;                /* R1 = R1 + inc */
        if (R[r1] <= lim) {                             /* if R1 <= lim */
            PCQ_ENTRY;                                  /* branch */
            PC = opnd & VAMASK;
            }
        break;

/* Logical instructions */

    case 0x04:                                          /* NR - RR */
    case 0x44:                                          /* NH - RXH */
    case 0x54:                                          /* N - RXF */
    case 0xC4:                                          /* NHI - RI1 */
    case 0xF4:                                          /* NI - RI2 */
        R[r1] = R[r1] & opnd;                           /* result */
        CC_GL_32 (R[r1]);                               /* set G,L */
        break;

    case 0x06:                                          /* OR - RR */
    case 0x46:                                          /* OH - RXH */
    case 0x56:                                          /* O - RXF */
    case 0xC6:                                          /* OHI - RI1 */
    case 0xF6:                                          /* OI - RI2 */
        R[r1] = R[r1] | opnd;                           /* result */
        CC_GL_32 (R[r1]);                               /* set G,L */
        break;

    case 0x07:                                          /* XR - RR */
    case 0x47:                                          /* XH - RXH */
    case 0x57:                                          /* X - RXF */
    case 0xC7:                                          /* XHI - RI1 */
    case 0xF7:                                          /* XI - RI2 */
        R[r1] = R[r1] ^ opnd;                           /* result */
        CC_GL_32 (R[r1]);                               /* set G,L */
        break;

    case 0xC3:                                          /* THI - RI1 */
    case 0xF3:                                          /* TI - RI2 */
        rslt = R[r1] & opnd;                            /* result */
        CC_GL_32 (rslt);                                /* set G, L */
        break;

    case 0x05:                                          /* CLR - RR */
    case 0x45:                                          /* CLH - RXH */
    case 0x55:                                          /* CL - RXF */
    case 0xC5:                                          /* CLHI - RI1 */
    case 0xF5:                                          /* CI - RI2 */
        rslt = (R[r1] - opnd) & DMASK32;                /* result */
        CC_GL_32 (rslt);                                /* set G,L */
        if (R[r1] < opnd)                               /* set C if borrow */
            cc = cc | CC_C;
        if (((R[r1] ^ opnd) & (~opnd ^ rslt)) & SIGN32)
            cc = cc | CC_V;
        break;

    case 0xD4:                                          /* CLB - RXB */
        t = R[r1] & DMASK8;
        rslt = (t - opnd) & DMASK16;                    /* result */
        CC_GL_16 (rslt);                                /* set G,L 16b */
        if (t < opnd)                                   /* set C if borrow */
            cc = cc | CC_C;
        break;

    case 0x12:                                          /* CHVR - RR */
        t = cc & CC_C;                                  /* save C */
        R[r1] = (SEXT16 (opnd & DMASK16)) & DMASK32;    /* result */
        CC_GL_32 (R[r1]);                               /* set G, L */
        if (R[r1] != opnd)                              /* wont fit? set V */
            cc = cc | CC_V;
        cc = cc | t;                                    /* restore C */
        break;

/* Shift instructions */

    case 0xCC:                                          /* SRHL - RI1 */
        opnd = opnd & 0xF;                              /* shift count */
    case 0x90:                                          /* SRHLS - NO */
        rslt = (R[r1] & DMASK16) >> opnd;               /* result */
        CC_GL_16 (rslt);                                /* set G,L 16b */
        if (opnd && (((R[r1] & DMASK16) >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = (R[r1] & ~DMASK16) | rslt;              /* store result */
        break;

    case 0xCD:                                          /* SLHL - RI1 */
        opnd = opnd & 0xF;                              /* shift count */
    case 0x91:                                          /* SLHLS - NO */
        rslt = R[r1] << opnd;                           /* result */
        CC_GL_16 (rslt & DMASK16);                      /* set G,L 16b */
        if (opnd && (rslt & 0x10000))                   /* set C if shft out */
            cc = cc | CC_C;
        R[r1] = (R[r1] & ~DMASK16) | (rslt & DMASK16);  /* store result */
        break;

    case 0xCE:                                          /* SRHA - RI1 */
        opnd = opnd & 0xF;                              /* shift count */
        rslt = (SEXT16 (R[r1]) >> opnd) & DMASK16;      /* result */
        CC_GL_16 (rslt);                                /* set G,L 16b */
        if (opnd && ((R[r1] >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = (R[r1] & ~DMASK16) | rslt;              /* store result */
        break;

    case 0xCF:                                          /* SLHA - RI1 */
        opnd = opnd & 0xF;                              /* shift count */
        rslt = R[r1] << opnd;                           /* raw result */
        R[r1] = (R[r1] & ~MMASK16) | (rslt & MMASK16);
        CC_GL_16 (R[r1] & DMASK16);                     /* set G,L 16b */
        if (opnd && (rslt & SIGN16))                    /* set C if shft out */
            cc = cc | CC_C;
        break;

    case 0xEC:                                          /* SRL - RI1 */
        opnd = opnd & 0x1F;                             /* shift count */
    case 0x10:                                          /* SRLS - NO */
        rslt = R[r1] >> opnd;                           /* result */
        CC_GL_32 (rslt);                                /* set G,L */
        if (opnd && ((R[r1] >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = rslt;                                   /* store result */
        break;

    case 0xED:                                          /* SLL - RI1 */
        opnd = opnd & 0x1F;                             /* shift count */
    case 0x11:                                          /* SLLS - NO */
        rslt = (R[r1] << opnd) & DMASK32;               /* result */
        CC_GL_32 (rslt);                                /* set G,L */
        if (opnd && ((R[r1] << (opnd - 1)) & SIGN32))
            cc = cc | CC_C;
        R[r1] = rslt;                                   /* store result */
        break;

    case 0xEE:                                          /* SRA - RI1 */
        opnd = opnd & 0x1F;                             /* shift count */
        rslt = (SEXT32 (R[r1]) >> opnd) & DMASK32;      /* result */
        CC_GL_32 (rslt);                                /* set G,L */
        if (opnd && ((R[r1] >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = rslt;                                   /* store result */
        break;

    case 0xEF:                                          /* SLA - RI1 */
        opnd = opnd & 0x1F;                             /* shift count */
        rslt = (R[r1] << opnd) & DMASK32;               /* raw result */
        R[r1] = (R[r1] & SIGN32) | (rslt & MMASK32);    /* arith result */
        CC_GL_32 (R[r1]);                               /* set G,L */
        if (opnd && (rslt & SIGN32))                    /* set C if shft out */
            cc = cc | CC_C;
        break;

    case 0xEA:                                          /* RRL - RI1 */
        opnd = opnd & 0x1F;                             /* shift count */
        if (opnd)                                       /* if cnt > 0 */
            R[r1] = (R[r1] >> opnd) | ((R[r1] << (32 - opnd)) & DMASK32);
        CC_GL_32 (R[r1]);                               /* set G,L */
        break;

    case 0xEB:                                          /* RLL - RI1 */
        opnd = opnd & 0x1F;                             /* shift count */
        if (opnd)
            R[r1] = ((R[r1] << opnd) & DMASK32) | (R[r1] >> (32 - opnd));
        CC_GL_32 (R[r1]);                               /* set G,L */
        break;

/* Bit instructions */

    case 0x74:                                          /* TBT - RX */
        t = 1u << (15 - (R[r1] & 0xF));                 /* bit mask in HW */
        ea = (ea + ((R[r1] >> 3) & ~1)) & VAMASK;       /* HW location */
        opnd = ReadH (ea, VR);                          /* read HW */
        if (opnd & t)                                   /* test bit */
            cc = CC_G;
        else cc = 0;
        break;

    case 0x75:                                          /* SBT - RX */
        t = 1u << (15 - (R[r1] & 0xF));                 /* bit mask in HW */
        ea = (ea + ((R[r1] >> 3) & ~1)) & VAMASK;       /* HW location */
        opnd = ReadH (ea, VR);                          /* read HW */
        WriteH (ea, opnd | t, VW);                      /* set bit, rewr */
        if (opnd & t)                                   /* test bit */
            cc = CC_G;
        else cc = 0;
        break;

    case 0x76:                                          /* RBT - RX */
        t = 1u << (15 - (R[r1] & 0xF));                 /* bit mask in HW */
        ea = (ea + ((R[r1] >> 3) & ~1)) & VAMASK;       /* HW location */
        opnd = ReadH (ea, VR);                          /* read HW */
        WriteH (ea, opnd & ~t, VW);                     /* clr bit, rewr */
        if (opnd & t)                                   /* test bit */
            cc = CC_G;
        else cc = 0;
        break;

    case 0x77:                                          /* CBT - RX */
        t = 1u << (15 - (R[r1] & 0xF));                 /* bit mask in HW */
        ea = (ea + ((R[r1] >> 3) & ~1)) & VAMASK;       /* HW location */
        opnd = ReadH (ea, VR);                          /* read HW */
        WriteH (ea, opnd ^ t, VW);                      /* com bit, rewr */
        if (opnd & t)                                   /* test bit */
            cc = CC_G;
        else cc = 0;
        break;

/* Arithmetic instructions */

    case 0x0A:                                          /* AR - RR */
    case 0x26:                                          /* AIS - NO */
    case 0x4A:                                          /* AH - RXH */
    case 0x5A:                                          /* A - RXF */
    case 0xCA:                                          /* AHI - RI1 */
    case 0xFA:                                          /* AI - RI2 */
        rslt = (R[r1] + opnd) & DMASK32;                /* result */
        CC_GL_32 (rslt);                                /* set G,L */
        if (rslt < opnd)                                /* set C if carry */
            cc = cc | CC_C;
        if (((~R[r1] ^ opnd) & (R[r1] ^ rslt)) & SIGN32)
            cc = cc | CC_V;
        R[r1] = rslt;
        break;

    case 0x51:                                          /* AM - RXF */
        rslt = (R[r1] + opnd) & DMASK32;                /* result */
        WriteF (ea, rslt, VW);                          /* write result */
        CC_GL_32 (rslt);                                /* set G,L */
        if (rslt < opnd)                                /* set C if carry */
            cc = cc | CC_C;
        if (((~R[r1] ^ opnd) & (R[r1] ^ rslt)) & SIGN32)
            cc = cc | CC_V;
        break;

    case 0x61:                                          /* AHM - RXH */
        rslt = (R[r1] + opnd) & DMASK16;                /* result */
        WriteH (ea, rslt, VW);                          /* write result */
        CC_GL_16 (rslt);                                /* set G,L 16b */
        if (rslt < (opnd & DMASK16))                    /* set C if carry */
            cc = cc | CC_C;
        if (((~R[r1] ^ opnd) & (R[r1] ^ rslt)) & SIGN16)
            cc = cc | CC_V;
        break;

    case 0x0B:                                          /* SR - RR */
    case 0x27:                                          /* SIS - NO */
    case 0x4B:                                          /* SH - RXH */
    case 0x5B:                                          /* S - RXF */
    case 0xCB:                                          /* SHI - RI1 */
    case 0xFB:                                          /* SI - RI2 */
        rslt = (R[r1] - opnd) & DMASK32;                /* result */
        CC_GL_32 (rslt);                                /* set G,L */
        if (R[r1] < opnd)                               /* set C if borrow */
            cc = cc | CC_C;
        if (((R[r1] ^ opnd) & (~opnd ^ rslt)) & SIGN32)
            cc = cc | CC_V;
        R[r1] = rslt;
        break;

    case 0x09:                                          /* CR - RR */
    case 0x49:                                          /* CH - RXH */
    case 0x59:                                          /* C - RXF */
    case 0xC9:                                          /* CHI - RI1 */
    case 0xF9:                                          /* CI - RI2 */
        if (R[r1] == opnd) cc = 0;                      /* =? */
        else if ((R[r1] ^ opnd) & SIGN32)               /* unlike signs? */
            cc = (R[r1] & SIGN32)? (CC_C | CC_L): CC_G;
        else cc = (R[r1] > opnd)? CC_G: (CC_C | CC_L);  /* like signs */
        if (((R[r1] ^ opnd) & (~opnd ^ (R[r1] - opnd))) & SIGN32)
            cc = cc | CC_V;
        break;

    case 0x0C:                                          /* MHR - RR */
    case 0x4C:                                          /* MH - RXH */
        R[r1] = (SEXT16 (R[r1]) * SEXT16 (opnd)) & DMASK32;     /* multiply */
        break;

    case 0x1C:                                          /* MR - RR */
    case 0x5C:                                          /* M - RXF */
        r1p1 = (r1 + 1) & 0xF;
        mpc = ABS (opnd);                               /* |mpcnd| */
        mpy = ABS (R[r1p1]);                            /* |mplyr| */
        rslt = rlo = 0;                                 /* clr result */
        for (i = 0; i < 32; i++) {                      /* develop 32b */
            t = 0;                                      /* no cout */
            if (mpy & 1) {                              /* cond add */
                rslt = (rslt + mpc) & DMASK32;
                if (rslt < mpc)
                    t = SIGN32;
                }
            rlo = (rlo >> 1) | ((rslt & 1) << 31);      /* shift result */
            rslt = (rslt >> 1) | t;
            mpy = mpy >> 1;                             /* shift mpylr */
            }
        if ((opnd ^ R[r1p1]) & SIGN32) {
            DNEG (rslt, rlo);
            }
        R[r1] = rslt;                                   /* store result */
        R[r1p1] = rlo;
        break;

    case 0x0D:                                          /* DHR - RR */
    case 0x4D:                                          /* DH - RXH */
        opnd = opnd & DMASK16;                          /* force HW opnd */
        if ((opnd == 0) ||                              /* div by zero? */
            ((R[r1] == 0x80000000) && (opnd == 0xFFFF))) {
            if (PSW & PSW_AFI)                         /* div fault enabled? */
                cc = exception (AFIPSW, cc, 0);        /* exception */
            break;
            }
        r1p1 = (r1 + 1) & 0xF;
        st = SEXT32 (R[r1]) / SEXT16 (opnd);            /* quotient */
        sr = SEXT32 (R[r1]) % SEXT16 (opnd);            /* remainder */
        if ((st < 0x8000) && (st >= -0x8000)) {         /* if quo fits */
            R[r1] = sr & DMASK32;                       /* store remainder */
            R[r1p1] = st & DMASK32;                     /* store quotient */
            }
        else if (PSW & PSW_AFI)                         /* div fault enabled? */
            cc = exception (AFIPSW, cc, 0);             /* exception */
        break;

    case 0x1D:                                          /* DR - RR */
    case 0x5D:                                          /* D - RXF */
        r1p1 = (r1 + 1) & 0xF;
        rslt = R[r1];                                   /* get dividend */
        rlo = R[r1p1];
        if (R[r1] & SIGN32) { DNEG (rslt, rlo); }       /* |divd| */
        dvr = ABS (opnd);                               /* |divr| */
        if (rslt < dvr) {                               /* will div work? */
            uint32 quos = R[r1] ^ opnd;                 /* expected sign */
            for (i = t = 0; i < 32; i++) {              /* 32 iterations */
                rslt = ((rslt << 1) & DMASK32) |        /* shift divd */
                    ((rlo >> 31) & 1);
                rlo = (rlo << 1) & DMASK32;
                t = (t << 1) & DMASK32;                 /* shift quo */
                if (rslt >= dvr) {                      /* subtract work? */
                    rslt = rslt - dvr;                  /* divd -= divr */
                    t = t | 1;                          /* set quo bit */
                    }
                }
            if (quos & SIGN32)                          /* res -? neg quo */
                t = NEG (t);
            if (R[r1] & SIGN32)                         /* adj rem sign */
                rslt = NEG (rslt); 
            if (t && ((t ^ quos) & SIGN32)) {           /* res sign wrong? */
                if (PSW & PSW_AFI)                      /* if enabled, */
                    cc = exception (AFIPSW, cc, 0);     /* exception */
                break;
                }
            R[r1] = rslt;                               /* store rem */
            R[r1p1] = t;                                /* store quo */
            }
        else if (PSW & PSW_AFI)                         /* div fault enabled? */
            cc = exception (AFIPSW, cc, 0);             /* exception */
        break;

/* Floating point instructions */

    case 0x28:                                          /* LER - NO */
    case 0x38:                                          /* LDR - NO */
    case 0x68:                                          /* LE - RX */
    case 0x78:                                          /* LD - RX */
        cc = f_l (op, r1, r2, ea);                      /* load */
        if ((cc & CC_V) && (PSW & PSW_AFI))             /* V set? */
            cc = exception (AFIPSW, cc, 1);
        break;

    case 0x29:                                          /* CER - NO */
    case 0x39:                                          /* CDR - NO */
    case 0x69:                                          /* CE - RX */
    case 0x79:                                          /* CD - RX */
        cc = f_c (op, r1, r2, ea);                      /* compare */
        break;

    case 0x2A:                                          /* AER - NO */
    case 0x2B:                                          /* SER - NO */
    case 0x3A:                                          /* ADR - NO */
    case 0x3B:                                          /* SDR - NO */
    case 0x6A:                                          /* AE - RX */
    case 0x6B:                                          /* SE - RX */
    case 0x7A:                                          /* AD - RX */
    case 0x7B:                                          /* SD - RX */
        cc = f_as (op, r1, r2, ea);                     /* add/sub */
        if ((cc & CC_V) && (PSW & PSW_AFI))             /* V set? */
            cc = exception (AFIPSW, cc, 1);
        break;

    case 0x2C:                                          /* MER - NO */
    case 0x3C:                                          /* MDR - NO */
    case 0x6C:                                          /* ME - RX */
    case 0x7C:                                          /* MD - RX */
        cc = f_m (op, r1, r2, ea);                      /* multiply */
        if ((cc & CC_V) && (PSW & PSW_AFI))             /* V set? */
            cc = exception (AFIPSW, cc, 1);
        break;

    case 0x2D:                                          /* DER - NO */
    case 0x3D:                                          /* DDR - NO */
    case 0x6D:                                          /* DE - RX */
    case 0x7D:                                          /* DD - RX */
        cc = f_d (op, r1, r2, ea);                      /* perform divide */
        if ((cc & CC_V) && (PSW & PSW_AFI))             /* V set? */
            cc = exception (AFIPSW, cc, 1);
        break;

    case 0x2E:                                          /* FXR - NO */
    case 0x3E:                                          /* FXDR - NO */
        cc = f_fix32 (op, r1, r2);                      /* cvt to integer */
        break;

    case 0x2F:                                          /* FLR - NO */
    case 0x3F:                                          /* FLDR - NO */
        cc = f_flt32 (op, r1, r2);                      /* cvt to floating */
        break;

    case 0x60:                                          /* STE - RX */
        t = ReadFReg (r1);                              /* get sp reg */
        WriteF (ea, t, VW);                             /* write */
        break;

    case 0x70:                                          /* STD - RX */
        WriteF (ea, D[r1 >> 1].h, VW);                  /* write hi */
        WriteF ((ea + 4) & VAMASK, D[r1 >> 1].l, VW);   /* write lo */
        break;

    case 0x71:                                          /* STME - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            t = ReadFReg (r1);                          /* get sp reg */
            WriteF (ea, t, VW);                         /* write */
            ea = (ea + 4) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0x72:                                          /* LME - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            t = ReadF (ea, VR);                         /* get value */
            WriteFReg (r1, t);                          /* write reg */
            ea = (ea + 4) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0x7E:                                          /* STMD - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            WriteF (ea, D[r1 >> 1].h, VW);              /* write register */
            WriteF ((ea + 4) & VAMASK, D[r1 >> 1].l, VW);
            ea = (ea + 8) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0x7F:                                          /* LMD - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            D[r1 >> 1].h = ReadF (ea, VR);              /* load register */
            D[r1 >> 1].l = ReadF ((ea + 4) & VAMASK, VR);
            ea = (ea + 8) & VAMASK;                     /* incr mem addr */
            }
        break;

/* Miscellaneous */
    
    case 0xE1:                                          /* SVC - RX */
        PCQ_ENTRY;                                      /* effective branch */
        t = BUILD_PSW (cc);                             /* save PSW */
        cc = newPSW (ReadF (SVNPS32, P));               /* get new PSW */
        R[13] = ea & 0xFFFFFF;                          /* parameter */
        R[14] = t;                                      /* old PSW */
        R[15] = PC;                                     /* old PC */
        PC = ReadH (SVNPC + r1 + r1, P);                /* new PC */
        if (DEBUG_PRI (cpu_dev, LOG_CPU_C)) fprintf (sim_deb,
            ">>SVC: oPC = %X, oPSW = %X, nPC = %X, nPSW = %X\n",
            pcq[pcq_p], t, PC, PSW);
        break;

    case 0xE2:                                          /* SINT - RI1 */
        dev = opnd & DEV_MAX;                           /* get dev */
        cc = int_auto (dev, cc);                        /* auto int */
        int_eval ();
        break;

    case 0xE3:                                          /* SCP - RXH */
        opnd = opnd & DMASK16;                          /* zero ext operand */
        if (opnd & CCW32_B1)                            /* point to buf */
            t = ea + CCB32_B1C;
        else t = ea + CCB32_B0C;
        sr = ReadH (t & VAMASK, VR);                    /* get count */
        sr = SEXT16 (sr);                               /* sign extend */
        if (sr <= 0) {                                  /* <= 0? */
            bufa = ReadF ((t + 2) & VAMASK, VR);        /* get buf end */
            if (opnd & CCW32_WR)                        /* write? */
                R[r1] = ReadB ((bufa + sr) & VAMASK, VR);  /* R1 gets mem */
            else WriteB ((bufa + sr) & VAMASK, R[r1], VW); /* read, R1 to mem */
            sr = sr + 1;                                /* inc count */
            CC_GL_32 (sr & DMASK32);                    /* set cc's */
            WriteH (t & VAMASK, sr, VW);                /* rewrite */
            if ((sr > 0) && !(opnd & CCW32_FST))        /* buf switch? */
                WriteH (ea, opnd ^ CCW32_B1, VW);       /* flip CCW bit */
            }                                           /* end if */
        else cc = CC_V;
        break;

    case 0x18:                                          /* LPSWR - RR */
        PCQ_ENTRY;                                      /* effective branch */
        PC = R[(r2 + 1) & 0xF] & VAMASK;                /* new PC (old reg set) */
        if (DEBUG_PRI (cpu_dev, LOG_CPU_C))
            fprintf (sim_deb, ">>LPSWR: oPC = %X, oPSW = %X, nPC = %X, nPSW = %X\n",
                     pcq[pcq_p], BUILD_PSW (cc), PC, opnd);
        cc = newPSW (opnd);                             /* new PSW */
        if (PSW & PSW_SQI)                              /* test for q */
            cc = testsysq (cc);
        break;

    case 0xC2:                                          /* LPSW - RXF */
        PCQ_ENTRY;                                      /* effective branch */
        PC = ReadF ((ea + 4) & VAMASK, VR) & VAMASK;    /* new PC */
        if (DEBUG_PRI (cpu_dev, LOG_CPU_C))
            fprintf (sim_deb, ">>LPSW: oPC = %X, oPSW = %X, nPC = %X, nPSW = %X\n",
                     pcq[pcq_p], BUILD_PSW (cc), PC, opnd);
        cc = newPSW (opnd);                             /* new PSW */
        if (PSW & PSW_SQI)                              /* test for q */
            cc = testsysq (cc);
        break;

    case 0x95:                                          /* EPSR - NO */
        R[r1] = BUILD_PSW (cc);                         /* save PSW */
        cc = newPSW (R[r2]);                            /* load new PSW */
        if (PSW & PSW_SQI)                              /* test for q */
            cc = testsysq (cc);
        break;  

    case 0x64:                                          /* ATL - RX */
    case 0x65:                                          /* ABL - RX */
        cc = addtoq (ea, R[r1], op & 1);                /* add to q */
        break;

    case 0x66:                                          /* RTL - RX */
    case 0x67:                                          /* RBL - RX */
        cc = remfmq (ea, r1, op & 1);                   /* rem from q */
        break;

    case 0x5E:                                          /* CRC12 - RXH */
        opnd = opnd & DMASK16;                          /* zero ext opnd */
        t = (R[r1] & 0x3F) ^ opnd;
        for (i = 0; i < 6; i++) {
            if (t & 1)
                t = (t >> 1) ^ 0x0F01;
            else t = t >> 1;
            }
        WriteH (ea, t, VW);
        break;

    case 0x5F:                                          /* CRC16 - RXH */
        opnd = opnd & DMASK16;                          /* zero ext opnd */
        t = (R[r1] & 0xFF) ^ opnd;
        for (i = 0; i < 8; i++) {
            if (t & 1)
                t = (t >> 1) ^ 0xA001;
            else t = t >> 1;
            }
        WriteH (ea, t, VW);
        break;

    case 0xE7:                                          /* TLATE - RXF */
        t = (opnd + ((R[r1] & DMASK8) << 1)) & VAMASK;  /* table entry */
        rslt = ReadH (t, VR);                           /* get entry */
        if (rslt & SIGN16)                              /* direct xlate? */
            R[r1] = rslt & DMASK8;
        else {
            PCQ_ENTRY;                                  /* branch */
            PC = rslt << 1;
            }
        break;

/* I/O instructions */

    case 0xDE:                                          /* OC - RX */
        opnd = ReadB (ea, VR);                          /* fetch operand */
    case 0x9E:                                          /* OCR - RR */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {
            dev_tab[dev] (dev, IO_ADR, 0);              /* select */
            dev_tab[dev] (dev, IO_OC, opnd & DMASK8);   /* send command */
            cc = 0;
            }
        else cc = CC_V;
        int_eval ();                                    /* re-eval intr */
        break;

    case 0xDA:                                          /* WD - RX */
        opnd = ReadB (ea, VR);                          /* fetch operand */
    case 0x9A:                                          /* WDR - RR */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {
            dev_tab[dev] (dev, IO_ADR, 0);              /* select */
            dev_tab[dev] (dev, IO_WD, opnd & DMASK8);   /* send data */
            cc = 0;
            }
        else cc = CC_V;
        int_eval ();                                    /* re-eval intr */
        break;

    case 0xD8:                                          /* WH - RX */
        opnd = ReadH (ea, VR);                          /* fetch operand */
    case 0x98:                                          /* WHR - RR */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {
            if (dev_tab[dev] (dev, IO_ADR, 0))          /* select; hw ok? */
                dev_tab[dev] (dev, IO_WH, opnd & DMASK16); /* send data */
            else {                                      /* byte only */
                dev_tab[dev] (dev, IO_WD, (opnd >> 8) & DMASK8); /* hi */
                dev_tab[dev] (dev, IO_WD, opnd & DMASK8); /* send lo byte */
                }
            cc = 0;
            }
        else cc = CC_V;
        int_eval ();                                    /* re-eval intr */
        break;

    case 0x9B:                                          /* RDR - RR */
    case 0xDB:                                          /* RD - RX */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            dev_tab[dev] (dev, IO_ADR, 0);              /* select */
            t = dev_tab[dev] (dev, IO_RD, 0);           /* get data */
            cc = 0;
            }
        else {                                          /* no */
            t = 0;
            cc = CC_V;
            }
        if (OP_TYPE (op) != OP_RR)                      /* RX or RR? */
            WriteB (ea, t, VW);
        else R[r2] = t & DMASK8;
        int_eval ();                                    /* re-eval intr */
        break;

    case 0x99:                                          /* RHR - RR */
    case 0xD9:                                          /* RH - RX */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            if (dev_tab[dev] (dev, IO_ADR, 0))          /* select, hw ok? */
                t = dev_tab[dev] (dev, IO_RH, 0);       /* get data */
            else {                                      /* byte only */
                rslt = dev_tab[dev] (dev, IO_RD, 0);    /* get byte */
                t = dev_tab[dev] (dev, IO_RD, 0);       /* get byte */
                t = (rslt << 8) | t;                    /* merge */
                }
            cc = 0;
            }
        else {                                          /* no */
            t = 0;
            cc = CC_V;
            }
        if (OP_TYPE (op) != OP_RR)                      /* RX or RR? */
            WriteH (ea, t, VW);
        else R[r2] = t & DMASK16;
        int_eval ();                                    /* re-eval intr */
        break;

    case 0x9D:                                          /* SSR - RR */
    case 0xDD:                                          /* SS - RX */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            dev_tab[dev] (dev, IO_ADR, 0);              /* select */
            t = dev_tab[dev] (dev, IO_SS, 0);           /* get status */
            }
        else t = STA_EX;                                /* no */
        if (OP_TYPE (op) != OP_RR)                      /* RX or RR? */
            WriteB (ea, t, VW);
        else R[r2] = t & DMASK8;
        cc = t & 0xF;
        int_eval ();                                    /* re-eval intr */
        break;

/* Block I/O instructions
        
   On a real Interdata system, the block I/O instructions can't be
   interrupted or stopped.  To model this behavior, while allowing
   the instructions to go back through fetch for I/O processing and
   WRU testing, the simulator implements a 'block I/O in progress'
   flag and status block.  If a block I/O is in progress, normal
   interrupts and fetches are suppressed until the block I/O is done.
*/

    case 0x96:                                          /* WBR - RR */
    case 0xD6:                                          /* WB - RXF */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            if (OP_TYPE (op) != OP_RR)
                lim = ReadF ((ea + 4) & VAMASK, VR);
            else lim = R[(r2 + 1) & 0xF];
            if (opnd > lim)                             /* start > end? */
                cc = 0;
            else {                                      /* no, start I/O */
                dev_tab[dev] (dev, IO_ADR, 0);          /* select dev */
                blk_io.dfl = dev;                       /* set status block */
                blk_io.cur = opnd;
                blk_io.end = lim;
                qevent = qevent | EV_BLK;               /* I/O in prog */
                }
            }
        else cc = CC_V;                                 /* nx dev */
        break;

    case 0x97:                                          /* RBR - RR */
    case 0xD7:                                          /* RB - RXF */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            if (OP_TYPE (op) != OP_RR)
                lim = ReadF ((ea + 4) & VAMASK, VR);
            else lim = R[(r2 + 1) & 0xF];
            if (opnd > lim)                             /* start > end? */
                cc = 0;
            else {                                      /* no, start I/O */
                dev_tab[dev] (dev, IO_ADR, 0);          /* select dev */
                blk_io.dfl = dev | BL_RD;               /* set status block */
                blk_io.cur = opnd;
                blk_io.end = lim;
                qevent = qevent | EV_BLK;               /* I/O in prog */
                }
            }
        else cc = CC_V;                                 /* nx dev */
        break;

    case 0xD5:                                          /* AL - RX */
        dev = ReadB (AL_DEV, P);                        /* get device */
        t = ReadB (AL_IOC, P);                          /* get command */
        if (DEV_ACC (dev)) {                            /* dev exist? */
            if (AL_BUF > ea)                            /* start > end? */
                cc = 0;
            else {                                      /* no, start I/O */
                dev_tab[dev] (dev, IO_ADR, 0);          /* select dev */
                dev_tab[dev] (dev, IO_OC, t);           /* start dev */
                blk_io.dfl = dev | BL_RD | BL_LZ;       /* set status block */
                blk_io.cur = AL_BUF;
                blk_io.end = ea;
                qevent = qevent | EV_BLK;               /* I/O in prog */
                }
            }
        else cc = CC_V;                                 /* nx dev */
        break;
        }                                               /* end switch */
    }                                                   /* end while */

/* Simulation halted */

PSW = BUILD_PSW (cc);
PC = PC & VAMASK;
set_r_display (R);
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}

/* Load new PSW */

uint32 newPSW (uint32 val)
{
uint32 rs = PSW_GETREG (val);                           /* register set */

R = &GREG[rs * 16];                                     /* set register set */
PSW = val & PSW_MASK;                                   /* store PSW */
int_eval ();                                            /* update intreq */
if (PSW & PSW_WAIT)                                     /* wait state? */
    qevent = qevent | EV_WAIT;
else qevent = qevent & ~EV_WAIT;
if (PSW & PSW_EXI)                                      /* enable/disable */
    SET_ENB (v_DS);
else CLR_ENB (v_DS);                                    /* console intr */
return PSW & CC_MASK;
}

/* Exception handler - 7/32 always uses register set 0 */

uint32 exception (uint32 loc, uint32 cc, uint32 flg)
{
int32 oldPSW = BUILD_PSW (cc);                          /* save old PSW */
int32 oldPC = PC;                                       /* save old PC */

cc = newPSW (ReadF (loc, P));                           /* new PSW */
PC = ReadF (loc + 4, P) & VAMASK;                       /* new PC */
if (cpu_unit.flags & UNIT_832) {                        /* 8/32? */
    R[14] = oldPSW;                                     /* PSW to new 14 */
    R[15] = oldPC;                                      /* PC to new 15 */
    }
else {
    GREG[14] = oldPSW;                                  /* 7/32, PSW to set 0 14 */
    GREG[15] = oldPC;                                   /* PC to set 0 15 */
    }
if (DEBUG_PRI (cpu_dev, LOG_CPU_I))
    fprintf (sim_deb, ">>Exc %X: oPC = %X, oPSW = %X, nPC = %X, nPSW = %X\n",
             loc, oldPC, oldPSW, PC, PSW | cc | flg);
return cc | flg;                                        /* return CC */
}

/* Test for queue interrupts - system queue addresses are physical */

uint32 testsysq (uint32 cc)
{
int32 qb = ReadF (SQP, P);                              /* get sys q addr */
int32 usd = ReadH (qb + Q32_USD, P);                    /* get use count */

if (usd) {                                              /* entries? */
    cc = exception (SQTPSW, cc, 0);                     /* take sysq exc */
    if (cpu_unit.flags & UNIT_832)                      /* R13 = sys q addr */
        R[13] = qb;
    else GREG[13] = qb;
    }
return cc;
}

/* Add to queue */

uint32 addtoq (uint32 ea, uint32 val, uint32 flg)
{
uint32 slt, usd, wra, t;

t = ReadF (ea, VR);                                     /* slots/used */
slt = (t >> 16) & DMASK16;                              /* # slots */
usd = t & DMASK16;                                      /* # used */
if (usd >= slt)                                         /* list full? */
    return CC_V;
usd = (usd + 1) & DMASK16;                              /* inc # used */
WriteH (ea + Q32_USD, usd, VW);                         /* rewrite */
if (flg) {                                              /* ABL? */
    wra = ReadH ((ea + Q32_BOT) & VAMASK, VR);          /* get bottom */
    t = wra + 1;                                        /* adv bottom */
    if (t >= slt)                                       /* wrap if necc */
        t = 0;
    WriteH ((ea + Q32_BOT) & VAMASK, t, VW);            /* rewrite bottom */
    }
else {
    wra = ReadH ((ea + Q32_TOP) & VAMASK, VR);          /* ATL, get top */
    if (wra == 0)
        wra = (slt - 1) & DMASK16;                      /* wrap if necc */
    else wra = wra - 1;                                 /* dec top */
    WriteH ((ea + Q32_TOP) & VAMASK, wra, VW);          /* rewrite top */
    }
WriteF ((ea + Q32_BASE + (wra * Q32_SLNT)) & VAMASK, val, VW); /* write slot */
return 0;
}

/* Remove from queue */

uint32 remfmq (uint32 ea, uint32 r1, uint32 flg)
{
uint32 slt, usd, rda, t;

t = ReadF (ea, VR);                                     /* get slots/used */
slt = (t >> 16) & DMASK16;                              /* # slots */
usd = t & DMASK16;                                      /* # used */
if (usd == 0)                                           /* empty? */
    return CC_V;
usd = usd - 1;                                          /* dec used */
WriteH (ea + Q32_USD, usd, VW);                         /* rewrite */
if (flg) {                                              /* RBL? */
    rda = ReadH ((ea + Q32_BOT) & VAMASK, VR);          /* get bottom */
    if (rda == 0)                                       /* wrap if necc */
        rda = (slt - 1) & DMASK16;
    else rda = rda - 1;                                 /* dec bottom */
    WriteH ((ea + Q32_BOT) & VAMASK, rda, VW);          /* rewrite bottom */
    }
else {
    rda = ReadH ((ea + Q32_TOP) & VAMASK, VR);          /* RTL, get top */
    t = rda + 1;                                        /* adv top */
    if (t >= slt)                                       /* wrap if necc */
        t = 0;
    WriteH ((ea + Q32_TOP) & VAMASK, t, VW);            /* rewrite top */
    }
R[r1] = ReadF ((ea + Q32_BASE + (rda * Q32_SLNT)) & VAMASK, VR); /* read slot */
if (usd)
    return CC_G;
else return 0;
}

/* Automatic interrupt processing */

uint32 int_auto (uint32 dev, uint32 cc)
{
uint32 addr, vec, by, ccw, ccwa, ccwb;
uint32 i, hw, tblad, tblen, bufe, st, t;
int32 bufc;
uint32 oldPSW = BUILD_PSW (cc);

vec = ReadH (INTSVT + dev + dev, P);                    /* get vector */
newPSW (0x2800);                                        /* new PSW */
R[0] = oldPSW;                                          /* save old PSW */
R[1] = PC;                                              /* save PC */
R[2] = dev;                                             /* set dev # */
if (DEBUG_PRI (cpu_dev, LOG_CPU_I))
    fprintf (sim_deb, ">>Int %X: oPC = %X, oPSW = %X, nPC = %X, nPSW = %X\n",
             dev, PC, oldPSW, vec, 0x2800);
if (DEV_ACC (dev)) {                                    /* dev exist? */
    hw = dev_tab[dev] (dev, IO_ADR, 0);                 /* select, get hw */
    R[3] = st = dev_tab[dev] (dev, IO_SS, 0);           /* sense status */
    }
else {
    hw = 0;
    R[3] = CC_V;
    }
if ((vec & 1) == 0) {                                   /* immed int? */
    PC = vec;                                           /* new PC */
    return PSW & CC_MASK;                               /* exit */
    }
R[4] = ccwa = vec & ~1;                                 /* save CCW addr */
ccw = ReadH (ccwa, VR);                                 /* read CCW */
if ((ccw & CCW32_EXE) == 0) {                           /* exec clr? */
    PC = ReadH (ccwa + CCB32_SUB, VR);                  /* get subr */
    return 0;                                           /* CC = 0 */
    }
if (!DEV_ACC (dev) || (st & CCW32_STA (ccw))) {         /* bad status? */
    PC = ReadH (ccwa + CCB32_SUB, VR);                  /* get subr */
    return CC_L;                                        /* CC = L */
    }
if (ccw & CCW32_FST) {                                  /* fast mode? */
    t = ReadH (ccwa + CCB32_B0C, VR);                   /* get count */
    bufc = SEXT16 (t);                                  /* sign ext */
    if (bufc <= 0) {                                    /* still valid? */
        bufe = ReadF (ccwa + CCB32_B0E, VR);            /* get end addr */
        addr = (bufe + bufc) & VAMASK;
        if (hw) {                                       /* halfword? */
            if (ccw & CCW32_WR) {                       /* write? */
                t = ReadH (addr, VR);                   /* get hw */
                dev_tab[dev] (dev, IO_WH, t);           /* send to dev */
                }
            else {                                      /* read */
                t = dev_tab[dev] (dev, IO_RH, 0);       /* get hw */
                WriteH (addr, t, VW);                   /* write to mem */
                }
            bufc = bufc + 2;                            /* adv buf cnt */
            }
        else {                                          /* byte */
            if (ccw & CCW32_WR) {                       /* write? */
                t = ReadB (addr, VR);                   /* get byte */
                dev_tab[dev] (dev, IO_WD, t);           /* send to dev */
                }
            else {                                      /* read */
                t = dev_tab[dev] (dev, IO_RD, 0);       /* get byte */
                WriteB (addr, t, VW);                   /* write to mem */
                }
            bufc = bufc + 1;                            /* adv buf cnt */
            }
        WriteH (ccwa + CCB32_B0C, bufc, VW);            /* rewrite cnt */
        if (bufc > 0) {
            PC = ReadH (ccwa + CCB32_SUB, VR);          /* get subr */
            return CC_G;                                /* CC = G */
            }
        }                                               /* end if bufc <= 0 */
    }                                                   /* end fast */
else {                                                  /* slow mode */
    if (ccw & CCW32_B1)                                 /* which buf? */
        ccwb = ccwa + CCB32_B1C;
    else ccwb = ccwa + CCB32_B0C;
    t = ReadH (ccwb, VR);                               /* get count */
    bufc = SEXT16 (t);                                  /* sign ext */
    if (bufc <= 0) {                                    /* still valid? */
        bufe = ReadF (ccwb + 2, VR);                    /* get end addr */
        addr = (bufe + bufc) & VAMASK;
        if (ccw & CCW32_WR) {                           /* write? */
            by = ReadB (addr, VR);                      /* byte fm mem */
            if (ccw & CCW32_TL) {                       /* translate? */
                tblad = ReadF (ccwa + CCB32_TAB, VR);   /* get tbl addr */
                tblen = (tblad + (by << 1)) & VAMASK;   /* tbl entry addr */
                t = ReadH (tblen, VR);                  /* get tbl entry */
                if ((t & SIGN16) == 0) {                /* special xlate? */
                    PC = t << 1;                        /* change PC */
                    R[3] = by;                          /* untrans char */
                    return 0;                           /* CC = 0 */
                    }
                by = t & DMASK8;                        /* replace */
                }
            dev_tab[dev] (dev, IO_WD, by);              /* write to dev */
            }
        else {                                          /* read */
            by = dev_tab[dev] (dev, IO_RD, 0);          /* get from dev */
            if (ccw & CCW32_TL) {                       /* translate? */
                tblad = ReadF (ccwa + CCB32_TAB, VR);   /* get tbl addr */
                tblen = (tblad + (by << 1)) & VAMASK;   /* tbl entry addr */
                t = ReadH (tblen, VR);                  /* get tbl entry */
                if ((t & SIGN16) == 0) {                /* special xlate? */
                    PC = t << 1;                        /* change PC */
                    R[3] = by;                          /* untrans char */
                    return 0;                           /* CC = 0 */
                    }
                WriteB (addr, t, VW);                   /* wr trans */
                }
            else WriteB (addr, by, VW);                 /* wr orig */
            }
        t = ReadH (ccwa + CCB32_CHK, VR);               /* get check wd */
        t = t ^ by;                                     /* start LRC */
        if (ccw & CCW32_CRC) {                          /* CRC? */
            for (i = 0; i < 8; i++) {
                if (t & 1)
                    t = (t >> 1) ^ 0xA001;
                else t = t >> 1;
                }
            }
        WriteH (ccwa + CCB32_CHK, t, VW);               /* rewrite chk wd */
        bufc = bufc + 1;                                /* adv buf cnt */
        WriteH (ccwb, bufc, VW);                        /* rewrite cnt */
        if (bufc > 0) {                                 /* cnt pos? */
            ccw = ccw ^ CCW32_B1;                       /* flip buf */
            WriteH (ccwa, ccw, VW);                     /* rewrite */
            PC = ReadH (ccwa + CCB32_SUB, VR);          /* get subr */
            return CC_G;                                /* CC = G */
            }
        }                                               /* end if bufc */
    }                                                   /* end slow */
PC = R[1];                                              /* restore PC */
return newPSW (R[0]);                                   /* restore PSW, CC */
}

/* Display register device */

uint32 display (uint32 dev, uint32 op, uint32 dat)
{
int t;

switch (op) {

    case IO_ADR:                                        /* select */
        if (!drmod)                                     /* norm mode? clr */
            drpos = srpos = 0;
        return BY;                                      /* byte only */

    case IO_OC:                                         /* command */
        dat = dat & 0xC0;
        if (dat == 0x40) {                              /* x40 = inc */
            drmod = 1;
            drpos = srpos = 0;                          /* init cntrs */
            }
        else if (dat == 0x80)                           /* x80 = norm */
            drmod = 0;
        break;

    case IO_WD:                                         /* write */
        if (drpos < 4) 
            DR = (DR & ~(DMASK8 << (drpos * 8))) | (dat << (drpos * 8));
        else if (drpos == 4)
            DRX = dat;
        drpos = (drpos + 1) & 0x7;
        break;

    case IO_RD:                                         /* read */
        t = (SR >> (srpos * 8)) & DMASK8;
        srpos = srpos ^ 1;
        return t;

    case IO_SS:                                         /* status */
        return 0x80;
        }

return 0;
}       

/* Relocation and protection */

uint32 Reloc (uint32 va, uint32 rel)
{
uint32 seg, off, mapr, lim;

seg = VA_GETSEG (va);                                   /* get seg num */
off = VA_GETOFF (va);                                   /* get offset */
mapr = mac_reg[seg];                                    /* get seg reg */
lim = GET_SRL (mapr);                                   /* get limit */
if (off >= lim) {                                       /* limit viol? */
    mac_sta = MACS_L;                                   /* set status */
    ABORT (MPRO);                                       /* abort */
    }
if ((mapr & SR_PRS) == 0) {                             /* not present? */
    mac_sta = MACS_NP;                                  /* set status */
    ABORT (MPRO);                                       /* abort */
    }
if ((rel == VE) && (mapr & SR_EXP)) {                   /* exec, prot? */
    mac_sta = MACS_EX;                                  /* set status */
    qevent = qevent | EV_MAC;                           /* req intr */
    }
if ((rel == VW) && (mapr & (SR_WPI | SR_WRP))) {        /* write, prot? */
    if (mapr & SR_WRP) {                                /* write abort? */
        mac_sta = MACS_WP;                              /* set status */
        ABORT (MPRO);                                   /* abort */
        }
    else {                                              /* write intr */
        mac_sta = MACS_WI;                              /* set status */
        qevent = qevent | EV_MAC;                       /* req intr */
        }
    }
return (off + (mapr & SRF_MASK)) & PAMASK32;            /* relocate */
}

uint32 RelocT (uint32 va, uint32 base, uint32 rel, uint32 *pa)
{
uint32 seg, off, mapr, lim;

seg = VA_GETSEG (va);                                   /* get seg num */
off = VA_GETOFF (va);                                   /* get offset */
mapr = ReadF ((base + (seg << 2)) & VAMASK, rel);       /* get seg reg */
lim = GET_SRL (mapr);                                   /* get limit */
if (off >= lim)                                         /* limit viol? */
    return CC_C; 
if ((mapr & SR_PRS) == 0)                               /* not present? */
    return CC_V;
*pa = off + (mapr & SRF_MASK);                          /* translate */
if (mapr & (SR_WRP | SR_WPI))                           /* write prot? */
    return CC_G;
if (mapr & SR_EXP)                                      /* exec prot? */
    return CC_L;
return 0;                                               /* ok */
}

/* Memory interface routines

   ReadB        read byte (processor)
   ReadH        read halfword (processor)
   ReadF        read fullword (processor)
   WriteB       write byte (processor)
   WriteH       write halfword (processor)
   WriteF       write fullword (processor)
   IOReadB      read byte (IO)
   IOWriteB     write byte (IO)
   IOReadH      read halfword (IO)
   IOWriteH     write halfword (IO)
*/

uint32 ReadB (uint32 loc, uint32 rel)
{
uint32 val;
uint32 sc = (3 - (loc & 3)) << 3;

if ((PSW & PSW_REL) == 0) {                             /* reloc off? */
    if ((loc & ~03) == MAC_STA) {                       /* MAC status? */
        val = mac_sta;                                  /* read it */
        qevent = qevent & ~EV_MAC;                      /* clr MAC intr */
        }
    else val = M[loc >> 2];                             /* get mem word */
    }
else if (rel == 0)                                      /* phys ref? */
    val = M[loc >> 2];
else {
    uint32 pa = Reloc (loc, rel);                       /* relocate */
    val = M[pa >> 2];
    }
return (val >> sc) & DMASK8;
}

uint32 ReadH (uint32 loc, uint32 rel)
{
uint32 val;

if ((PSW & PSW_REL) == 0) {                             /* reloc off? */
    if ((loc & ~03) == MAC_STA) {                       /* MAC status? */
        val = mac_sta;                                  /* read it */
        qevent = qevent & ~EV_MAC;                      /* clr MAC intr */
        }
    else val = M[loc >> 2];                             /* get mem word */
    }
else if (rel == 0)                                      /* phys ref? */
    val = M[loc >> 2];
else {
    uint32 pa = Reloc (loc, rel);                       /* relocate */
    val = M[pa >> 2];
    }
return (val >> ((loc & 2)? 0: 16)) & DMASK16;
}

uint32 ReadF (uint32 loc, uint32 rel)
{
uint32 val;

if ((PSW & PSW_REL) == 0) {                             /* reloc off? */
    if ((loc & ~03) == MAC_STA) {                       /* MAC status? */
        val = mac_sta;                                  /* read it */
        qevent = qevent & ~EV_MAC;                      /* clr MAC intr */
        }
    else val = M[loc >> 2];                             /* get mem word */
    }
else if (rel == 0)                                      /* phys ref? */
    val = M[loc >> 2];
else {
    uint32 pa = Reloc (loc, rel);                       /* relocate */
    val = M[pa >> 2];
    }
return val;
}

void WriteB (uint32 loc, uint32 val, uint32 rel)
{
uint32 pa = loc;
uint32 sc = (3 - (loc & 3)) << 3;

val = val & DMASK8;
if ((PSW & PSW_REL) == 0) {                             /* reloc off? */
    uint32 idx = (pa - MAC_BASE) >> 2;                  /* check for MAC */
    if (idx <= MAC_LNT) {
        if (idx < MAC_LNT) mac_reg[idx] =
            ((mac_reg[idx] & ~(DMASK8 << sc)) | (val << sc)) & SR_MASK;
        else {
            mac_sta = 0;
            qevent = qevent & ~EV_MAC;
            }
        }
    }
else if (rel != 0)                                      /* !phys? relocate */
    pa = Reloc (loc, rel);
if (MEM_ADDR_OK (pa))
    M[pa >> 2] = (M[pa >> 2] & ~(DMASK8 << sc)) | (val << sc);
return;
}

void WriteH (uint32 loc, uint32 val, uint32 rel)
{
uint32 pa = loc;

val = val & DMASK16;
if ((PSW & PSW_REL) == 0) {                             /* reloc off? */
    uint32 idx = (pa - MAC_BASE) >> 2;                  /* check for MAC */
    if (idx <= MAC_LNT) {
        if (idx < MAC_LNT) mac_reg[idx] = ((loc & 2)?
            ((mac_reg[idx] & ~DMASK16) | val):
            ((mac_reg[idx] & DMASK16) | (val << 16))) & SR_MASK;
        else {
            mac_sta = 0;
            qevent = qevent & ~EV_MAC;
            }
        }
    }
else if (rel != 0)                                      /* !phys? relocate */
    pa = Reloc (loc, rel);
if (MEM_ADDR_OK (pa))
    M[pa >> 2] = (loc & 2)? ((M[pa >> 2] & ~DMASK16) | val):
                            ((M[pa >> 2] & DMASK16) | (val << 16));
return;
}

void WriteF (uint32 loc, uint32 val, uint32 rel)
{
uint32 pa = loc;

val = val & DMASK32;
if (loc & 2) {
    WriteH (loc & VAMASK, (val >> 16) & DMASK16, rel);
    WriteH ((loc + 2) & VAMASK, val & DMASK16, rel);
    return;
    }
if ((PSW & PSW_REL) == 0) {                             /* reloc off? */
    uint32 idx = (pa - MAC_BASE) >> 2;                  /* check for MAC */
    if (idx <= MAC_LNT) {
        if (idx < MAC_LNT) mac_reg[idx] = val & SR_MASK;
        else {
            mac_sta = 0;
            qevent = qevent & ~EV_MAC;
            }
        }
    }
else if (rel != 0)                                      /* !phys? relocate */
    pa = Reloc (loc, rel);
if (MEM_ADDR_OK (pa))
    M[pa >> 2] = val & DMASK32;
return;
}

uint32 IOReadB (uint32 loc)
{
uint32 sc = (3 - (loc & 3)) << 3;

return (M[loc >> 2] >> sc) & DMASK8;
}

uint32 IOReadH (uint32 loc)
{
return (M[loc >> 2] >> ((loc & 2)? 0: 16)) & DMASK16;
}

void IOWriteB (uint32 loc, uint32 val)
{
uint32 sc = (3 - (loc & 3)) << 3;

val = val & DMASK8;
M[loc >> 2] = (M[loc >> 2] & ~(DMASK8 << sc)) | (val << sc);
return;
}

void IOWriteH (uint32 loc, uint32 val)
{
uint32 sc = (loc & 2)? 0: 16;

val = val & DMASK16;
M[loc >> 2] = (M[loc >> 2] & ~(DMASK16 << sc)) | (val << sc);
return;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
qevent = 0;                                             /* no events */
mac_sta = 0;                                            /* clear MAC */
newPSW (0);                                             /* PSW = 0 */
set_r_display (R);
DR = 0;                                                 /* clear display */
drmod = 0;
blk_io.dfl = blk_io.cur = blk_io.end = 0;               /* no block I/O */
sim_brk_types = sim_brk_dflt = SWMASK ('E');            /* init bkpts */
if (M == NULL)
    M = (uint32 *) calloc (MAXMEMSIZE32 >> 2, sizeof (uint32));
if (M == NULL)
    return SCPE_MEM;
pcq_r = find_reg ("PCQ", NULL, dptr);                   /* init PCQ */
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if ((sw & SWMASK ('V')) && (PSW & PSW_REL)) {
    int32 cc = RelocT (addr, MAC_BASE, P, &addr);
    if (cc & (CC_C | CC_V))
        return SCPE_NXM;
    }
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = IOReadH (addr);
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if ((sw & SWMASK ('V')) && (PSW & PSW_REL)) {
    int32 cc = RelocT (addr, MAC_BASE, P, &addr);
    if (cc & (CC_C | CC_V))
        return SCPE_NXM;
    }
if (addr >= MEMSIZE)
    return SCPE_NXM;
IOWriteH (addr, val);
return SCPE_OK;
}

/* Change memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 mc = 0;
uint32 i;

if ((val <= 0) || (((unsigned)val) > MAXMEMSIZE32) || ((val & 0xFFFF) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i = i + 4)
    mc = mc | M[i >> 2];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE32; i = i + 4)
     M[i >> 2] = 0;
return SCPE_OK;
}

/* Set current R pointers for SCP */

void set_r_display (uint32 *rbase)
{
REG *rptr;
int32 i;

rptr = find_reg ("R0", NULL, &cpu_dev);
if (rptr == NULL)
    return;
for (i = 0; i < 16; i++, rptr++)
    rptr->loc = (void *) (rbase + i);
return;
}

/* Set console interrupt */

t_stat cpu_set_consint (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (PSW & PSW_EXI)
    SET_INT (v_DS);
return SCPE_OK;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uint32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].pc = 0;
    hst_p = 0;
    return SCPE_OK;
    }
lnt = (uint32) get_uint (cptr, 10, HIST_MAX, &r);
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
int32 op, k, di, lnt;
CONST char *cptr = (CONST char *) desc;
t_value sim_eval[3];
t_stat r;
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
fprintf (st, "PC     r1       operand  ea     IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(di++) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        fprintf (st, "%06X %08X %08X ", h->pc & VAMASK32, h->r1, h->opnd);
        op = (h->ir1 >> 8) & 0xFF;
        if (OP_TYPE (op) >= OP_RX)
            fprintf (st, "%06X ", h->ea);
        else fprintf (st, "       ");
        sim_eval[0] = h->ir1;
        sim_eval[1] = h->ir2;
        sim_eval[2] = h->ir3;
        if ((fprint_sym (st, h->pc & VAMASK32, sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "(undefined) %04X", h->ir1);
        fputc ('\n', st);                               /* end line */
        }                                               /* end if instruction */
    }                                                   /* end for */
return SCPE_OK;
}
