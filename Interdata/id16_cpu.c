/* id16_cpu.c: Interdata 16b CPU simulator

   Copyright (c) 2000-2008, Robert M. Supnik

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

   cpu                  Interdata 16b CPU

   28-Apr-07    RMS     Removed clock initialization
   27-Oct-06    RMS     Added idle support
                        Removed separate PASLA clock
   06-Feb-06    RMS     Fixed bug in DH (Mark Hittinger)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   25-Aug-05    RMS     Fixed DH integer overflow cases
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   10-Mar-05    RMS     Fixed bug in show history routine (Mark Hittinger)
                        Revised examine/deposit to do words rather than bytes
   07-Nov-04    RMS     Added instruction history
   22-Sep-03    RMS     Added additional instruction decode types
   07-Feb-03    RMS     Fixed bug in SETM, SETMR (Mark Pizzolato)

   The register state for the Interdata 16b CPU is:

   R[0:F]<0:15>         general registers
   F[0:7]<0:31>         single precision floating point registers
   D[0:7]<0:63>         double precision floating point registers
   PSW<0:31>            processor status word, including
    STAT<0:11>          status flags
    CC<0:3>             condition codes
    PC<0:15>            program counter
   int_req[8]<0:31>     interrupt requests
   int_enb[8]<0:31>     interrupt enables
   
   The Interdata 16b systems have four instruction formats: register to
   register, short format, register to memory, and register to storage.
   The formats are:

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
   |           op          |     R1    |     RX    |    register-memory
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                    address                    |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |           op          |     R1    |     RX    |    register-storage
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                    address                    |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   For register-memory and register-storage instructions, an effective
   address is calculated as follows:

        effective addr = address + RX (if RX > 0)

   Register-memory instructions can access an address space of 64K bytes.

   The Interdata 16b product line had many different models, with varying
   instruction sets:

   instruction group    model = 3    4    5   70   80  716  816  816E
   base group (61)              y    y    y    y    y    y    y    y
   AL, LM, STM (3)              -    y    y    y    y    y    y    y
   single prec fp (13)          -    y    y    y    y    y    y    y
   model 5 group (36)           -    -    y    y    y    y    y    y
   double prec fp (17)          -    -    -    -    -    -    y    y
   memory extension (4)         -    -    -    -    -    -    -    y

   This allows the most common CPU options to be covered by just five
   model selections: I3, I4, I5/70/80/716, I816, and I816E.  Variations
   within a model (e.g., 816 with no floating point or just single
   precision floating point) are not implemented.

   The I3 kept its general registers in memory; this is not simulated.
   Single precision (only) floating point was implemented in microcode,
   did not have a guard digit, and kept the floating point registers in
   memory.  Double precision floating point was implemented in hardware,
   provided a guard digit for single precision (but not double), and
   kept the floating point registers in hardware.

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

   3. Non-existent memory.  On the Interdata 16b, reads to non-existent
      memory return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        id_defs.h       add device interrupt definitions
        id16_sys.c      add sim_devices table entry
*/

#include "id_defs.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = oPC
#define VAMASK          VAMASK16
#define VA_S1           0x8000                          /* S0/S1 flag */

#define UNIT_V_MSIZE    (UNIT_V_UF + 0)                 /* dummy mask */
#define UNIT_V_ID4      (UNIT_V_UF + 1)
#define UNIT_V_716      (UNIT_V_UF + 2)
#define UNIT_V_816      (UNIT_V_UF + 3)
#define UNIT_V_816E     (UNIT_V_UF + 4)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define UNIT_ID4        (1 << UNIT_V_ID4)
#define UNIT_716        (1 << UNIT_V_716)
#define UNIT_816        (1 << UNIT_V_816)
#define UNIT_816E       (1 << UNIT_V_816E)
#define UNIT_TYPE       (UNIT_ID4 | UNIT_716 | UNIT_816 | UNIT_816E)

#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    uint16              vld;
    uint16              pc;
    uint16              ir1;
    uint16              ir2;
    uint16              r1;
    uint16              ea;
    uint16              opnd;
    } InstHistory;

#define PSW_GETMAP(x)   (((x) >> PSW_V_MAP) & PSW_M_MAP)
#define SEXT16(x)       (((x) & SIGN16)? ((int32) ((x) | 0xFFFF8000)): \
                        ((int32) ((x) & 0x7FFF)))
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
#define BUILD_PSW(x)    (((PSW & ~CC_MASK) | (x)) & psw_mask)
#define CPU_x16         (cpu_unit.flags & (UNIT_716 | UNIT_816 | UNIT_816E))

uint32 GREG[16] = { 0 };                                /* general registers */
uint16 *M = NULL;                                       /* memory */
uint32 *R = &GREG[0];                                   /* register set ptr */
uint32 F[8] = { 0 };                                    /* sp fp registers */
dpr_t D[8] = { {0, 0} };                                /* dp fp registers */
uint32 PSW = 0;                                         /* processor status word */
uint32 psw_mask = PSW_x16;                              /* PSW mask */
uint32 PC = 0;                                          /* program counter */
uint32 SR = 0;                                          /* switch register */
uint32 DR = 0;                                          /* display register */
uint32 DRX = 0;                                         /* display extension */
uint32 drmod = 0;                                       /* mode */
uint32 srpos = 0;                                       /* switch register pos */
uint32 drpos = 0;                                       /* display register pos */
uint32 s0_rel = 0;                                      /* S0 relocation */
uint32 s1_rel = 0;                                      /* S1 relocation */
uint32 int_req[INTSZ] = { 0 };                          /* interrupt requests */
uint32 int_enb[INTSZ] = { 0 };                          /* interrupt enables */
int32 blkiop = -1;                                      /* block I/O in prog */
uint32 qevent = 0;                                      /* events */
uint32 stop_inst = 0;                                   /* stop on ill inst */
uint32 stop_wait = 0;                                   /* stop on wait */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
uint32 dec_flgs = 0;                                    /* decode flags */
uint32 fp_in_hwre = 0;                                  /* ucode/hwre fp */
uint32 pawidth = PAWIDTH16;                             /* phys addr mask */
uint32 hst_p = 0;                                       /* history pointer */
uint32 hst_lnt = 0;                                     /* history length */
InstHistory *hst = NULL;                                /* instruction history */
struct BlockIO blk_io;                                  /* block I/O status */
uint32 (*dev_tab[DEVNO])(uint32 dev, uint32 op, uint32 datout) = { NULL };

uint32 ReadB (uint32 loc);
uint32 ReadH (uint32 loc);
void WriteB (uint32 loc, uint32 val);
void WriteH (uint32 loc, uint32 val);
uint32 int_auto (uint32 dev, uint32 cc);
uint32 addtoq (uint32 ea, uint32 val, uint32 flg);
uint32 remfmq (uint32 ea, uint32 r1, uint32 flg);
uint32 newPSW (uint32 val);
uint32 swap_psw (uint32 loc, uint32 cc);
uint32 testsysq (uint32);
uint32 display (uint32 dev, uint32 op, uint32 dat);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_model (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_consint (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc);

extern t_bool devtab_init (void);
extern void int_eval (void);
extern uint32 int_getdev (void);
extern t_bool sch_blk (uint32 dev);
extern uint32 f_l (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_c (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_as (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_m (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_d (uint32 op, uint32 r1, uint32 r2, uint32 ea);
extern uint32 f_fix (uint32 op, uint32 r1, uint32 r2);
extern uint32 f_flt (uint32 op, uint32 r1, uint32 r2);

/* Instruction decoding table - flags are first implementation */

const uint16 decrom[256] = {
    0,                                                  /* 00 */
    OP_RR,                                              /* BALR */
    OP_RR,                                              /* BTCR */
    OP_RR,                                              /* BFCR */
    OP_RR,                                              /* NHR */
    OP_RR,                                              /* CLHR */
    OP_RR,                                              /* OHR */
    OP_RR,                                              /* XHR */
    OP_RR,                                              /* LHR */
    OP_RR | OP_716,                                     /* CHR */
    OP_RR,                                              /* AHR */
    OP_RR,                                              /* SHR */
    OP_RR,                                              /* MHR */
    OP_RR,                                              /* DHR */
    OP_RR,                                              /* ACHR */
    OP_RR,                                              /* SCHR */
    0, 0, 0,                                            /* 10:12 */
    OP_RR | OP_816E | OP_PRV,                           /* SETMR */
    0, 0, 0, 0,                                         /* 14:1F */
    0, 0, 0, 0, 0, 0, 0, 0,
    OP_NO | OP_716,                                     /* BTBS */
    OP_NO | OP_716,                                     /* BTFS */
    OP_NO | OP_716,                                     /* BFBS */
    OP_NO | OP_716,                                     /* BFFS */
    OP_NO | OP_716,                                     /* LIS */
    OP_NO | OP_716,                                     /* LCS */
    OP_NO | OP_716,                                     /* AIS */
    OP_NO | OP_716,                                     /* SIS */
    OP_NO | OP_ID4,                                     /* LER */
    OP_NO | OP_ID4,                                     /* CER */
    OP_NO | OP_ID4,                                     /* AER */
    OP_NO | OP_ID4,                                     /* SER */
    OP_NO | OP_ID4,                                     /* MER */
    OP_NO | OP_ID4,                                     /* DER */
    OP_NO | OP_816,                                     /* FXR */
    OP_NO | OP_816,                                     /* FLR */
    0, 0, 0,                                            /* 30:32 */
    OP_NO | OP_816E | OP_PRV,                           /* LPSR */
    0, 0, 0, 0,                                         /* 34:37 */
    OP_NO | OP_816 | OP_DPF,                            /* LDR */
    OP_NO | OP_816 | OP_DPF,                            /* CDR */
    OP_NO | OP_816 | OP_DPF,                            /* ADR */
    OP_NO | OP_816 | OP_DPF,                            /* SDR */
    OP_NO | OP_816 | OP_DPF,                            /* MDR */
    OP_NO | OP_816 | OP_DPF,                            /* DDR */
    OP_NO | OP_816 | OP_DPF,                            /* FXDR */
    OP_NO | OP_816 | OP_DPF,                            /* FLDR */
    OP_RX,                                              /* STH */
    OP_RX,                                              /* BAL */
    OP_RX,                                              /* BTC */
    OP_RX,                                              /* BFC */
    OP_RXH,                                             /* NH */
    OP_RXH,                                             /* CLH */
    OP_RXH,                                             /* OH */
    OP_RXH,                                             /* XH */
    OP_RXH,                                             /* LH */
    OP_RXH | OP_716,                                    /* CH */
    OP_RXH,                                             /* AH */
    OP_RXH,                                             /* SH */
    OP_RXH,                                             /* MH */
    OP_RXH,                                             /* DH */
    OP_RXH,                                             /* ACH */
    OP_RXH,                                             /* SCH */
    0, 0, 0,                                            /* 50:52 */
    OP_RXH | OP_816E | OP_PRV,                          /* SETM */
    0, 0, 0, 0,                                         /* 54:5F */
    0, 0, 0, 0, 0, 0, 0, 0,
    OP_RX | OP_ID4,                                     /* STE */
    OP_RXH | OP_716,                                    /* AHM */
    0, 0,                                               /* 62:63 */
    OP_RX | OP_716,                                     /* ATL */
    OP_RX | OP_716,                                     /* ABL */
    OP_RX | OP_716,                                     /* RTL */
    OP_RX | OP_716,                                     /* RBL */
    OP_RX | OP_ID4,                                     /* LE */
    OP_RX | OP_ID4,                                     /* CE */
    OP_RX | OP_ID4,                                     /* AE */
    OP_RX | OP_ID4,                                     /* SE */
    OP_RX | OP_ID4,                                     /* ME */
    OP_RX | OP_ID4,                                     /* DE */
    0, 0,                                               /* 6E:6F */
    OP_RX | OP_816 | OP_DPF,                            /* STD */
    OP_RX | OP_816,                                     /* SME */
    OP_RX | OP_816,                                     /* LME */
    OP_RXH | OP_816E | OP_PRV,                          /* LPS */
    0, 0, 0, 0,                                         /* 74:7F */
    OP_RX | OP_816 | OP_DPF,                            /* LD */
    OP_RX | OP_816 | OP_DPF,                            /* CD */
    OP_RX | OP_816 | OP_DPF,                            /* AD */
    OP_RX | OP_816 | OP_DPF,                            /* SD */
    OP_RX | OP_816 | OP_DPF,                            /* MD */
    OP_RX | OP_816 | OP_DPF,                            /* DD */
    OP_RX | OP_816 | OP_DPF,                            /* STMD */
    OP_RX | OP_816 | OP_DPF,                            /* LMD */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 80:8F */
    0, 0, 0, 0, 0, 0, 0, 0,
    OP_NO | OP_716,                                     /* SRLS */
    OP_NO | OP_716,                                     /* SLLS */
    OP_NO,                                              /* STBR */
    OP_RR,                                              /* LDBR */
    OP_RR | OP_716,                                     /* EXBR */
    OP_NO | OP_716 | OP_PRV,                            /* EPSR */
    OP_RR | OP_PRV,                                     /* WBR */
    OP_RR | OP_PRV,                                     /* RBR */
    OP_RR | OP_716 | OP_PRV,                            /* WHR */
    OP_RR | OP_716 | OP_PRV,                            /* RHR */
    OP_RR | OP_PRV,                                     /* WDR */
    OP_RR | OP_PRV,                                     /* RDR */
    OP_RR | OP_716,                                     /* MHUR */
    OP_RR | OP_PRV,                                     /* SSR */
    OP_RR | OP_PRV,                                     /* OCR */
    OP_RR | OP_PRV,                                     /* AIR */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* A0:AF */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,                             /* B0:BF */
    0, 0, 0, 0, 0, 0, 0, 0,
    OP_RX,                                              /* BXH */
    OP_RX,                                              /* BXLE */
    OP_RX | OP_PRV,                                     /* LPSW */
    OP_RS | OP_716,                                     /* THI */
    OP_RS,                                              /* NHI */
    OP_RS,                                              /* CLHI */
    OP_RS,                                              /* OHI */
    OP_RS,                                              /* XHI */
    OP_RS,                                              /* LHI */
    OP_RS | OP_716,                                     /* CHI */
    OP_RS,                                              /* AHI */
    OP_RS,                                              /* SHI */
    OP_RS,                                              /* SRHL */
    OP_RS,                                              /* SLHL */
    OP_RS,                                              /* SRHA */
    OP_RS,                                              /* SLHA */
    OP_RX | OP_ID4,                                     /* STM */
    OP_RX | OP_ID4,                                     /* LM */
    OP_RX,                                              /* STB */
    OP_RXB,                                             /* LDB */
    OP_RXB | OP_716,                                    /* CLB */
    OP_RX | OP_ID4 | OP_PRV,                            /* AL */
    OP_RXH | OP_PRV,                                    /* WB */
    OP_RXH | OP_PRV,                                    /* RB */
    OP_RX | OP_716 | OP_PRV,                            /* WH */
    OP_RX | OP_716 | OP_PRV,                            /* RH */
    OP_RX | OP_PRV,                                     /* WD */
    OP_RX | OP_PRV,                                     /* RD */
    OP_RXH | OP_716,                                    /* MHU */
    OP_RX | OP_PRV,                                     /* SS */
    OP_RX | OP_PRV,                                     /* OC */
    OP_RX | OP_PRV,                                     /* AI */
    0,                                                  /* E0 */
    OP_RX | OP_716,                                     /* SVC */
    OP_RS | OP_716 | OP_PRV,                            /* SINT */
    0, 0, 0, 0, 0, 0, 0,                                /* E3:E9 */
    OP_RS | OP_716,                                     /* RRL */
    OP_RS | OP_716,                                     /* RLL */
    OP_RS | OP_716,                                     /* SRL */
    OP_RS | OP_716,                                     /* SLL */
    OP_RS | OP_716,                                     /* SRA */
    OP_RS | OP_716,                                     /* SLA */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* F0:FF */
    0, 0, 0, 0, 0, 0, 0, 0
    };

/* 8/16E relocation constants for S0 and S1, indexed by PSW<8:11> */

static uint32 s0_rel_const[16] = {                      /* addr 0-7FFF */
    0x00000, 0x00000, 0x00000, 0x00000,                 /* 0 = no reloc */
    0x00000, 0x00000, 0x00000, 0x08000,                 /* 8000 = rel to S1 */
    0x08000, 0x08000, 0x08000, 0x08000,
    0x08000, 0x08000, 0x08000, 0x00000
    };

static uint32 s1_rel_const[16] = {                      /* addr 8000-FFFF */
    0x00000, 0x08000, 0x10000, 0x18000,                 /* reloc const must */
    0x20000, 0x28000, 0x30000, 0xFFF8000,               /* "sub" base addr */
    0x00000, 0x08000, 0x10000, 0x18000,
    0x20000, 0x28000, 0x30000, 0x00000
    };

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

DIB cpu_dib = { d_DS, -1, v_DS, NULL, &display, NULL };

UNIT cpu_unit = {
    UDATA (NULL, UNIT_FIX | UNIT_BINK | UNIT_716, MAXMEMSIZE16)
    };

REG cpu_reg[] = {
    { HRDATA (PC, PC, 16) },
    { HRDATA (R0, GREG[0], 16) },
    { HRDATA (R1, GREG[1], 16) },
    { HRDATA (R2, GREG[2], 16) },
    { HRDATA (R3, GREG[3], 16) },
    { HRDATA (R4, GREG[4], 16) },
    { HRDATA (R5, GREG[5], 16) },
    { HRDATA (R6, GREG[6], 16) },
    { HRDATA (R7, GREG[7], 16) },
    { HRDATA (R8, GREG[8], 16) },
    { HRDATA (R9, GREG[9], 16) },
    { HRDATA (R10, GREG[10], 16) },
    { HRDATA (R11, GREG[11], 16) },
    { HRDATA (R12, GREG[12], 16) },
    { HRDATA (R13, GREG[13], 16) },
    { HRDATA (R14, GREG[14], 16) },
    { HRDATA (R15, GREG[15], 16) },
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
    { HRDATA (SR, SR, 16) },
    { HRDATA (DR, DR, 32) },
    { HRDATA (DRX, DRX, 8) },
    { FLDATA (DRMOD, drmod, 0) },
    { FLDATA (SRPOS, srpos, 0) },
    { HRDATA (DRPOS, drpos, 3) },
    { BRDATA (IRQ, int_req, 16, 32, 8) },
    { BRDATA (IEN, int_enb, 16, 32, 8) },
    { HRDATA (QEVENT, qevent, 4), REG_HRO },
    { FLDATA (STOP_INST, stop_inst, 0) },
    { FLDATA (STOP_WAIT, stop_inst, 0) },
    { BRDATA (PCQ, pcq, 16, 16, PCQ_SIZE), REG_RO+REG_CIRC },
    { HRDATA (PCQP, pcq_p, 6), REG_HRO },
    { HRDATA (WRU, sim_int_char, 8) },
    { HRDATA (BLKIOD, blk_io.dfl, 16), REG_HRO },
    { HRDATA (BLKIOC, blk_io.cur, 16), REG_HRO },
    { HRDATA (BLKIOE, blk_io.end, 16), REG_HRO },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_TYPE, 0, "I3", "I3", &cpu_set_model },
    { UNIT_TYPE, UNIT_ID4, "I4", "I4", &cpu_set_model },
    { UNIT_TYPE, UNIT_716, "7/16", "716", &cpu_set_model },
    { UNIT_TYPE, UNIT_816, "8/16", "816", &cpu_set_model },
    { UNIT_TYPE, UNIT_816E, "8/16E", "816E", &cpu_set_model },
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
    { UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size },
    { UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
    { UNIT_MSIZE, 131072, NULL, "128K", &cpu_set_size },
    { UNIT_MSIZE, 262144, NULL, "256K", &cpu_set_size },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "CONSINT",
      &cpu_set_consint, NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 16, 18, 2, 16, 16,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    &cpu_dib, 0
    };

t_stat sim_instr (void)
{
uint32 cc;
t_stat reason;

/* Restore register state */

if (devtab_init ())                                     /* check conflicts */
    return SCPE_STOP;
pawidth = PAWIDTH16;                                    /* default width */
if (cpu_unit.flags & UNIT_816E) {                       /* 8/16E? */
    dec_flgs = 0;                                       /* all instr ok */
    fp_in_hwre = 1;                                     /* fp in hwre */
    pawidth = PAWIDTH16E;                               /* 18b phys addr */
    psw_mask = PSW_816E;                                /* mem ext bits */
    }
else if (cpu_unit.flags & UNIT_816) {                   /* 8/16? */
    dec_flgs = OP_816E;
    fp_in_hwre = 1;
    pawidth = PAWIDTH16;
    psw_mask = PSW_x16;
    }
else if (cpu_unit.flags & UNIT_716) {                   /* I5, 70, 80, 7/16? */
    dec_flgs = OP_816 | OP_816E;
    fp_in_hwre = 0;
    pawidth = PAWIDTH16;
    psw_mask = PSW_x16;
    }
else if (cpu_unit.flags & UNIT_ID4) {                   /* I4? */
    dec_flgs = OP_716 | OP_816 | OP_816E;
    fp_in_hwre = 0;
    pawidth = PAWIDTH16;
    psw_mask = PSW_ID4;
    }
else {
    dec_flgs = OP_ID4 | OP_716 | OP_816 | OP_816E;      /* I3 */
    fp_in_hwre = 0;
    pawidth = PAWIDTH16;
    psw_mask = PSW_ID4;
    }
int_eval ();                                            /* eval interrupts */
cc = newPSW (PSW & psw_mask);                           /* split PSW, eval wait */
reason = 0;

/* Process events */

while (reason == 0) {                                   /* loop until halted */
    uint32 dev, drom, inc, lim, opnd;
    uint32 op, r1, r1p1, r2, ea, oPC;
    uint32 rslt, t, map;
    uint32 ir1, ir2, ityp;
    int32 sr, st;

    if (sim_interval <= 0) {                            /* check clock queue */
        if ((reason = sim_process_event ()))
            break;
        int_eval ();
        }

    if (qevent) {                                       /* any events? */
        if (qevent & EV_BLK) {                          /* block I/O in prog? */
            dev = blk_io.dfl & DEV_MAX;                 /* get device */
            cc = dev_tab[dev] (dev, IO_SS, 0) & 0xF;    /* sense status */
            if (cc == STA_BSY) {                        /* just busy? */
                sim_interval = 0;                       /* force I/O event */
                continue;
                }
            else if (cc == 0) {                         /* ready? */
                if (blk_io.dfl & BL_RD) {               /* read? */
                    t = dev_tab[dev] (dev, IO_RD, 0);   /* get byte */
                    if ((t == 0) && (blk_io.dfl & BL_LZ))
                        continue;
                    blk_io.dfl = blk_io.dfl & ~BL_LZ;   /* non-zero seen */
                    WriteB (blk_io.cur, t);             /* write mem */
                    }
                else {                                  /* write */
                    t = ReadB (blk_io.cur);             /* read mem */
                    dev_tab[dev] (dev, IO_WD, t);       /* put byte */
                    }
                if (blk_io.cur != blk_io.end) {         /* more to do? */
                    blk_io.cur = (blk_io.cur + 1) & VAMASK;     /* incr addr */
                    continue;
                    }
                }
            qevent = qevent & ~EV_BLK;                  /* clr block I/O flg */
            int_eval ();                                /* re-eval intr */
            continue;
            }

        if ((qevent & EV_INT) && (PSW & PSW_EXI)) {     /* interrupt? */
            if (PSW & PSW_AIO) {                        /* auto enabled? */
                dev = int_getdev ();                    /* get int dev */
                cc = int_auto (dev, cc);                /* do auto intr */
                int_eval ();                            /* re-eval intr */
                }
            else cc = swap_psw (EXIPSW, cc);            /* old type, swap */
            continue;
            }

        if (PSW & PSW_WAIT) {                           /* wait state? */
            sim_idle (TMR_LFC, TRUE);                   /* idling */
            continue;
            }

        qevent = 0;                                     /* no events */
        }                                               /* end if event */

/* Fetch and decode instruction */

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
		}

    sim_interval = sim_interval - 1;

    ir1 = ReadH (oPC = PC);                             /* fetch instr */
    op = (ir1 >> 8) & 0xFF;                             /* isolate op, R1, R2 */
    r1 = (ir1 >> 4) & 0xF;
    r2 = ir1 & 0xF;
    drom = decrom[op];
    ityp = drom & OP_MASK;

    if ((drom == 0) || (drom & dec_flgs)) {             /* not in model? */
        if (stop_inst)                                  /* stop or */
            reason = STOP_RSRV;
        else cc = swap_psw (ILOPSW, cc);                /* swap PSW */
        continue;
        }
    if ((drom & OP_PRV) && (PSW & PSW_PRO)) {           /* priv & protected? */
        cc = swap_psw (ILOPSW, cc);                     /* swap PSW */
        continue;
        }

    switch (ityp) {                                     /* decode instruction */

    case OP_NO:                                         /* no operand */
        opnd = r2;                                      /* assume short */
        break;

    case OP_RR:                                         /* reg-reg */
        opnd = R[r2];                                   /* operand is R2 */
        break;

    case OP_RS:                                         /* reg-storage */
    case OP_RX:                                         /* reg-mem */
        PC = (PC + 2) & VAMASK;                         /* increment PC */
        ir2 = ea = ReadH (PC);                          /* fetch address */
        if (r2)                                         /* index calculation */
            ea = (ir2 + R[r2]) & VAMASK;
        opnd = ea;                                      /* operand is ea */
        break;

    case OP_RXB:                                        /* reg-mem byte */
        PC = (PC + 2) & VAMASK;                         /* increment PC */
        ir2 = ea = ReadH (PC);                          /* fetch address */
        if (r2)                                         /* index calculation */
            ea = (ir2 + R[r2]) & VAMASK;
        opnd = ReadB (ea);                              /* fetch operand */
        break;

    case OP_RXH:                                        /* reg-mem halfword */
        PC = (PC + 2) & VAMASK;                         /* increment PC */
        ir2 = ea = ReadH (PC);                          /* fetch address */
        if (r2)                                         /* index calculation */
            ea = (ir2 + R[r2]) & VAMASK;
        opnd = ReadH (ea);                              /* fetch operand */
        break;
        
    default:
        return SCPE_IERR;
        }

    if (hst_lnt) {                                      /* instruction history? */
        hst[hst_p].vld = 1;
        hst[hst_p].pc = oPC;
        hst[hst_p].ir1 = ir1;
        hst[hst_p].ir2 = ir2;
        hst[hst_p].r1 = R[r1];
        hst[hst_p].ea = ea;
        hst[hst_p].opnd = opnd;
        hst_p = hst_p + 1;
        if (hst_p >= hst_lnt)
            hst_p = 0;
        }

    PC = (PC + 2) & VAMASK;                             /* increment PC */
    switch (op) {                                       /* case on opcode */

/* Load/store instructions */

    case 0x08:                                          /* LHR - RR */
    case 0x24:                                          /* LIS - NO */
    case 0x48:                                          /* LH - RXH */
    case 0xC8:                                          /* LHI - RS */
        R[r1] = opnd;                                   /* load operand */
        CC_GL_16 (R[r1]);                               /* set G,L */
        break;

    case 0x25:                                          /* LCS - NO */
        R[r1] = (~opnd + 1) & DMASK16;                  /* load complement */
        CC_GL_16 (R[r1]);                               /* set G,L */
        break;

    case 0x40:                                          /* STH - RX */
        WriteH (ea, R[r1]);                             /* store register */
        break;

    case 0xD1:                                          /* LM - RX */
        for ( ; r1 <= 0xF; r1++) {                      /* loop thru reg */
            R[r1] = ReadH (ea);                         /* load register */
            ea = (ea + 2) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0xD0:                                          /* STM - RX */
        for ( ; r1 <= 0xF; r1++) {                      /* loop thru reg */
            WriteH (ea, R[r1]);                         /* store register */
            ea = (ea + 2) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0x93:                                          /* LDBR - RR */
    case 0xD3:                                          /* LDB - RXB */
        R[r1] = opnd & DMASK8;                          /* load byte */
        break;

    case 0x92:                                          /* STBR - NO */
        R[r2] = (R[r2] & ~DMASK8) | (R[r1] & DMASK8);   /* store byte */
        break;
    case 0xD2:                                          /* STB - RX */
        WriteB (ea, R[r1] & DMASK8);                    /* store byte */
        break;

    case 0x94:                                          /* EXBR - RR */
        R[r1] = (opnd >> 8) | ((opnd & DMASK8) << 8);
        break;

/* Control instructions */

    case 0x01:                                          /* BALR - RR */
    case 0x41:                                          /* BAL - RX */
        PCQ_ENTRY;                                      /* save old PC */
        R[r1] = PC;                                     /* save cur PC */
        PC = opnd;                                      /* branch */
        break;

    case 0x02:                                          /* BTCR - RR */
    case 0x42:                                          /* BTC - RX */
        if (cc & r1) {                                  /* test CC's */
            PCQ_ENTRY;                                  /* branch if true */
            PC = opnd;
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
            PC = opnd;
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
        R[r1] = (R[r1] + inc) & DMASK16;                /* R1 = R1 + inc */
        if (R[r1] > lim) {                              /* if R1 > lim */
            PCQ_ENTRY;                                  /* branch */
            PC = opnd;
            }
        break;

    case 0xC1:                                          /* BXLE - RX */
        inc = R[(r1 + 1) & 0xF];                        /* inc = R1 + 1 */
        lim = R[(r1 + 2) & 0xF];                        /* lim = R1 + 2 */
        R[r1] = (R[r1] + inc) & DMASK16;                /* R1 = R1 + inc */
        if (R[r1] <= lim) {                             /* if R1 <= lim */
            PCQ_ENTRY;                                  /* branch */
            PC = opnd;
            }
        break;

/* Logical instructions */

    case 0x04:                                          /* NHR - RR */
    case 0x44:                                          /* NH - RXH */
    case 0xC4:                                          /* NHI - RS */
        R[r1] = R[r1] & opnd;                           /* result */
        CC_GL_16 (R[r1]);                               /* set G,L */
        break;

    case 0x06:                                          /* OHR - RR */
    case 0x46:                                          /* OH - RXH */
    case 0xC6:                                          /* OHI - RS */
        R[r1] = R[r1] | opnd;                           /* result */
        CC_GL_16 (R[r1]);                               /* set G,L */
        break;

    case 0x07:                                          /* XHR - RR */
    case 0x47:                                          /* XH - RXH */
    case 0xC7:                                          /* XHI - RS */
        R[r1] = R[r1] ^ opnd;                           /* result */
        CC_GL_16 (R[r1]);                               /* set G,L */
        break;

    case 0xC3:                                          /* THI - RS */
        rslt = R[r1] & opnd;                            /* result */
        CC_GL_16 (rslt);                                /* set G, L */
        break;

    case 0x05:                                          /* CLHR - RR */
    case 0x45:                                          /* CLH - RXH */
    case 0xC5:                                          /* CLHI - RS */
        rslt = (R[r1] - opnd) & DMASK16;                /* result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (R[r1] < opnd)                               /* set C if borrow */
            cc = cc | CC_C;
        if (((R[r1] ^ opnd) & (~opnd ^ rslt)) & SIGN16)
            cc = cc | CC_V;
        break;

    case 0xD4:                                          /* CLB - RXB */
        t = R[r1] & DMASK8;
        rslt = (t - opnd) & DMASK16;                    /* result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (t < opnd)                                   /* set C if borrow */
            cc = cc | CC_C;
        break;

/* Shift instructions */

    case 0xCC:                                          /* SRHL - RS */
        opnd = opnd & 0xF;                              /* shift count */
    case 0x90:                                          /* SRLS - NO */
        rslt = R[r1] >> opnd;                           /* result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (opnd && ((R[r1] >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = rslt;                                   /* store result */
        break;

    case 0xCD:                                          /* SLHL - RS */
        opnd = opnd & 0xF;                              /* shift count */
    case 0x91:                                          /* SLLS - NO */
        rslt = R[r1] << opnd;                           /* raw result */
        R[r1] = rslt & DMASK16;                         /* masked result */
        CC_GL_16 (R[r1]);                               /* set G,L */
        if (opnd && (rslt & 0x10000))                   /* set C if shft out */
            cc = cc | CC_C;
        break;

    case 0xCE:                                          /* SRHA - RS */
        opnd = opnd & 0xF;                              /* shift count */
        rslt = (SEXT16 (R[r1]) >> opnd) & DMASK16;      /* result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (opnd && ((R[r1] >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = rslt;                                   /* store result */
        break;

    case 0xCF:                                          /* SLHA - RS */
        opnd = opnd & 0xF;                              /* shift count */
        rslt = R[r1] << opnd;                           /* raw result */
        R[r1] = (R[r1] & SIGN16) | (rslt & MMASK16);    /* arith result */
        CC_GL_16 (R[r1]);                               /* set G,L */
        if (opnd && (rslt & SIGN16))                    /* set C if shft out */
            cc = cc | CC_C;
        break;

    case 0xEA:                                          /* RRL - RS */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        opnd = opnd & 0x1F;                             /* shift count */
        t = (R[r1] << 16) | R[r1p1];                    /* form 32b op */
        if (opnd)                                       /* result */
            rslt = (t >> opnd) | (t << (32 - opnd));
        else rslt = t;                                  /* no shift */
        CC_GL_32 (rslt);                                /* set G,L 32b */
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

    case 0xEB:                                          /* RLL - RS */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        opnd = opnd & 0x1F;                             /* shift count */
        t = (R[r1] << 16) | R[r1p1];                    /* form 32b op */
        if (opnd)                                       /* result */
            rslt = (t << opnd) | (t >> (32 - opnd));
        else rslt = t;                                  /* no shift */
        CC_GL_32 (rslt);                                /* set G,L 32b */
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

    case 0xEC:                                          /* SRL - RS */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        opnd = opnd & 0x1F;                             /* shift count */
        t = (R[r1] << 16) | R[r1p1];                    /* form 32b op */
        rslt = t >> opnd;                               /* result */
        CC_GL_32 (rslt);                                /* set G,L 32b */
        if (opnd && ((t >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

    case 0xED:                                          /* SLL - RS */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        opnd = opnd & 0x1F;                             /* shift count */
        t = (R[r1] << 16) | R[r1p1];                    /* form 32b op */
        rslt = t << opnd;                               /* result */
        CC_GL_32 (rslt);                                /* set G,L 32b */
        if (opnd && ((t << (opnd - 1)) & SIGN32))
            cc = cc | CC_C;
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

    case 0xEE:                                          /* SRA - RS */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        opnd = opnd & 0x1F;                             /* shift count */
        t = (R[r1] << 16) | R[r1p1];                    /* form 32b op */
        rslt = ((int32) t) >> opnd;                     /* signed result */
        CC_GL_32 (rslt);                                /* set G,L 32b */
        if (opnd && ((t >> (opnd - 1)) & 1))
            cc = cc | CC_C;
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

    case 0xEF:                                          /* SLA - RS */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        opnd = opnd & 0x1F;                             /* shift count */
        t = (R[r1] << 16) | R[r1p1];                    /* form 32b op */
        rslt = (t & SIGN32) | ((t << opnd) & MMASK32);  /* signed result */
        CC_GL_32 (rslt);                                /* set G,L 32b */
        if (opnd && ((t << opnd) & SIGN32))
            cc = cc | CC_C;
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

/* Arithmetic instructions */

    case 0x0A:                                          /* AHR - RR */
    case 0x26:                                          /* AIS - NO */
    case 0x4A:                                          /* AH - RXH */
    case 0xCA:                                          /* AHI - RS */
        rslt = (R[r1] + opnd) & DMASK16;                /* result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (rslt < opnd)                                /* set C if carry */
            cc = cc | CC_C;
        if (((~R[r1] ^ opnd) & (R[r1] ^ rslt)) & SIGN16)
            cc = cc | CC_V;
        R[r1] = rslt;
        break;

    case 0x61:                                          /* AHM - RXH */
        rslt = (R[r1] + opnd) & DMASK16;                /* result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (rslt < opnd)                                /* set C if carry */
            cc = cc | CC_C;
        if (((~R[r1] ^ opnd) & (R[r1] ^ rslt)) & SIGN16)
            cc = cc | CC_V;
        WriteH (ea, rslt);                              /* store in memory */
        break;

    case 0x0B:                                          /* SHR - RR */
    case 0x27:                                          /* SIS - NO */
    case 0x4B:                                          /* SH - RXH */
    case 0xCB:                                          /* SHI - RS */
        rslt = (R[r1] - opnd) & DMASK16;                /* result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (R[r1] < opnd)                               /* set C if borrow */
            cc = cc | CC_C;
        if (((R[r1] ^ opnd) & (~opnd ^ rslt)) & SIGN16)
            cc = cc | CC_V;
        R[r1] = rslt;
        break;

    case 0x09:                                          /* CHR - RR */
    case 0x49:                                          /* CH - RXH */
    case 0xC9:                                          /* CHI - RS */
        sr = SEXT16 (R[r1]);                            /* sign ext */
        st = SEXT16 (opnd);
        if (sr < st)                                    /* < sets C, L */
            cc = CC_C | CC_L;
        else if (sr > st)                               /* > sets G */
            cc = CC_G;
        else cc = 0;
        if (((R[r1] ^ opnd) & (~opnd ^ (sr - st))) & SIGN16)
            cc = cc | CC_V;
        break;

    case 0x0C:                                          /* MHR - RR */
    case 0x4C:                                          /* MH - RXH */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        rslt = SEXT16 (R[r1p1]) * SEXT16 (opnd);        /* multiply */
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

    case 0x9C:                                          /* MHUR - RR */
    case 0xDC:                                          /* MHU - RXH */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        rslt = R[r1p1] * opnd;                          /* multiply, unsigned */
        R[r1] = (rslt >> 16) & DMASK16;                 /* hi result */
        R[r1p1] = rslt & DMASK16;                       /* lo result */
        break;

    case 0x0D:                                          /* DHR - RR */
    case 0x4D:                                          /* DH - RXH */
        r1p1 = (r1 + 1) & 0xF;                          /* R1 + 1 */
        if ((opnd == 0) ||
            ((R[r1] == 0x8000) && (R[r1p1] == 0) && (opnd == 0xFFFF))) {
            if (PSW & PSW_AFI)                          /* div fault enabled? */
                cc = swap_psw (AFIPSW, cc);             /* swap PSW */
            break;
            }
        sr = (R[r1] << 16) | R[r1p1];                   /* signed 32b divd */
        st = sr / SEXT16 (opnd);                        /* signed quotient */
        sr = sr % SEXT16 (opnd);                        /* remainder */
        if ((st < 0x8000) && (st >= -0x8000)) {         /* if quo fits */
            R[r1] = sr & DMASK16;                       /* store remainder */
            R[r1p1] = st & DMASK16;                     /* store quotient */
            }
        else if (PSW & PSW_AFI)                         /* div fault enabled? */
            cc = swap_psw (AFIPSW, cc);                 /* swap PSW */
        break;

    case 0x0E:                                          /* ACHR - RR */
    case 0x4E:                                          /* ACH - RXH */
        t = R[r1] + opnd + ((cc & CC_C) != 0);          /* raw result */
        rslt = t & DMASK16;                             /* masked result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (t > DMASK16)                                /* set C if carry */
            cc = cc | CC_C;
        if (((~R[r1] ^ opnd) & (R[r1] ^ rslt)) & SIGN16)
            cc = cc | CC_V;
        R[r1] = rslt;                                   /* store result */
        break;

    case 0x0F:                                          /* SCHR - RR */
    case 0x4F:                                          /* SCH - RXH */
        t = R[r1] - opnd - ((cc & CC_C) != 0);          /* raw result */
        rslt = t & DMASK16;                             /* masked result */
        CC_GL_16 (rslt);                                /* set G,L */
        if (t > DMASK16)                                /* set C if borrow */
            cc = cc | CC_C;
        if (((R[r1] ^ opnd) & (~opnd ^ rslt)) & SIGN16)
            cc = cc | CC_V;
        R[r1] = rslt;                                   /* store result */
        break;

/* Floating point instructions */

    case 0x28:                                          /* LER - NO */
    case 0x38:                                          /* LDR - NO */
    case 0x68:                                          /* LE - RX */
    case 0x78:                                          /* LD - RX */
        cc = f_l (op, r1, r2, ea);                      /* load */
        if ((cc & CC_V) && (PSW & PSW_FPF) && CPU_x16)  /* V set, x/16? */
            cc = swap_psw (FPFPSW, cc);
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
        if ((cc & CC_V) && (PSW & PSW_FPF) && CPU_x16)  /* V set, x/16? */
            cc = swap_psw (FPFPSW, cc);
        break;

    case 0x2C:                                          /* MER - NO */
    case 0x3C:                                          /* MDR - NO */
    case 0x6C:                                          /* ME - RX */
    case 0x7C:                                          /* MD - RX */
        cc = f_m (op, r1, r2, ea);                      /* multiply */
        if ((cc & CC_V) && (PSW & PSW_FPF) && CPU_x16)  /* V set, x/16? */
            cc = swap_psw (FPFPSW, cc);
        break;

    case 0x2D:                                          /* DER - NO */
    case 0x3D:                                          /* DDR - NO */
    case 0x6D:                                          /* DE - RX */
    case 0x7D:                                          /* DD - RX */
        cc = f_d (op, r1, r2, ea);                      /* perform divide */
        if ((cc & CC_V) && ((cc & CC_C) ||              /* V set, x/16 or */
            ((PSW & PSW_FPF) && CPU_x16)))              /* V & C set? */
            cc = swap_psw (FPFPSW, cc);
        break;

    case 0x2E:                                          /* FXR - NO */
    case 0x3E:                                          /* FXDR - NO */
        cc = f_fix (op, r1, r2);                        /* cvt to integer */
        break;

    case 0x2F:                                          /* FLR - NO */
    case 0x3F:                                          /* FLDR - NO */
        cc = f_flt (op, r1, r2);                        /* cvt to floating */
        break;

    case 0x60:                                          /* STE - RX */
        t = ReadFReg (r1);                              /* get fp reg */
        WriteF (ea, t, P);                              /* write */
        break;

    case 0x70:                                          /* STD - RX */
        WriteF (ea, D[r1 >> 1].h, P);                   /* write hi */
        WriteF ((ea + 4) & VAMASK, D[r1 >> 1].l, P);    /* write lo */
        break;

    case 0x71:                                          /* STME - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            t = ReadFReg (r1);                          /* get fp reg */
            WriteF (ea, t, P);                          /* write */
            ea = (ea + 4) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0x72:                                          /* LME - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            t = ReadF (ea, P);                          /* get value */
            WriteFReg (r1, t);                          /* write reg */
            ea = (ea + 4) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0x7E:                                          /* STMD - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            WriteF (ea, D[r1 >> 1].h, P);               /* write register */
            WriteF ((ea + 4) & VAMASK, D[r1 >> 1].l, P);
            ea = (ea + 8) & VAMASK;                     /* incr mem addr */
            }
        break;

    case 0x7F:                                          /* LMD - RX */
        for ( ; r1 <= 0xE; r1 = r1 + 2) {               /* loop thru reg */
            D[r1 >> 1].h = ReadF (ea, P);               /* load register */
            D[r1 >> 1].l = ReadF ((ea + 4) & VAMASK, P);
            ea = (ea + 8) & VAMASK;                     /* incr mem addr */
            }
        break;

/* Miscellaneous */
    
    case 0xE1:                                          /* SVC - RX */
        PCQ_ENTRY;                                      /* save PC */
        WriteH (SVCAP, ea);                             /* save opnd */
        WriteH (SVOPS, BUILD_PSW (cc));                 /* save PS */
        WriteH (SVOPC, PC);                             /* save PC */
        PC = ReadH (SVNPC + r1 + r1);                   /* new PC */
        cc = newPSW (ReadH (SVNPS));                    /* new PS */
        break;

    case 0xE2:                                          /* SINT - RS */
        dev = opnd & DEV_MAX;                           /* get dev */
        cc = int_auto (dev, cc);                        /* auto intr */
        int_eval ();                                    /* re-eval intr */
        break;

    case 0xC2:                                          /* LPSW - RX */
        PCQ_ENTRY;                                      /* effective branch */
        PC = ReadH ((ea + 2) & VAMASK);                 /* read PC */
        cc = newPSW (ReadH (ea));                       /* read PSW */
        if (PSW & PSW_SQI)                              /* test for q */
            cc = testsysq (cc);
        break;

    case 0x95:                                          /* EPSR - NO */
        R[r1] = BUILD_PSW (cc);                         /* save PSW */
    case 0x33:                                          /* LPSR - NO */
        cc = newPSW (R[r2]);                            /* load new PSW */
        if (PSW & PSW_SQI)                              /* test for q */
            cc = testsysq (cc);
        break;  

    case 0x73:                                          /* LPS - RXH */
        cc = newPSW (opnd);                             /* load new PSW */
        if (PSW & PSW_SQI)                              /* test for q */
            cc = testsysq (cc);
        break;  

    case 0x64:                                          /* ATL - RX */
    case 0x65:                                          /* ABL - RX */
        cc = addtoq (ea, R[r1], op & 1);                /* add to q */
        break;

    case 0x66:                                          /* RTL - RX */
    case 0x67:                                          /* RBL - RX */
        cc = remfmq (ea, r1, op & 1);                   /* remove from q */
        break;

    case 0x13:                                          /* SETMR - RR */
    case 0x53:                                          /* SETM - RXH */
        t = BUILD_PSW (cc);                             /* old PSW */
        map = PSW_GETMAP (opnd);                        /* get new map */
        switch (map) {                                  /* case on map */

        case 0x7:
            map = 0;                                    /* use 1:1 map */
            R[r1] = R[r1] ^ SIGN16;                     /* flip sign */
            break;

        case 0x8: case 0x9: case 0xA: case 0xB:
        case 0xC: case 0xD: case 0xE:
            if (R[r1] & SIGN16)                         /* S1? clr map<0> */
                map = map & ~0x8;
            else {
                map = 0;                                /* else 1:1 map */
                R[r1] = R[r1] | SIGN16;                 /* set sign */
                }
            break;

        default:
            break;
            }
        t = (t & ~PSW_MAP) | (map << PSW_V_MAP);        /* insert map */
        newPSW (t);                                     /* load new PSW */
        CC_GL_16 (R[r1]);                               /* set G,L */
        break;

/* I/O instructions */

case 0xDE:                                              /* OC - RX */
    opnd = ReadB (ea);                                  /* fetch operand */
    case 0x9E:                                          /* OCR - RR */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {
            dev_tab[dev] (dev, IO_ADR, 0);              /* select */
            dev_tab[dev] (dev, IO_OC, opnd & DMASK8);   /* send command */
            int_eval ();                                /* re-eval intr */
            cc = 0;
            }
        else cc = CC_V;
        break;

    case 0xDA:                                          /* WD - RX */
        opnd = ReadB (ea);                              /* fetch operand */
    case 0x9A:                                          /* WDR - RR */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {
            dev_tab[dev] (dev, IO_ADR, 0);              /* select */
            dev_tab[dev] (dev, IO_WD, opnd & DMASK8);   /* send data */
            int_eval ();                                /* re-eval intr */
            cc = 0;
            }
        else cc = CC_V;
        break;

    case 0xD8:                                          /* WH - RX */
        opnd = ReadH (ea);                              /* fetch operand */
    case 0x98:                                          /* WHR - RR */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {
            if (dev_tab[dev] (dev, IO_ADR, 0))          /* select; hw ok? */
                dev_tab[dev] (dev, IO_WH, opnd);        /* send data */
            else {                                      /* byte only */
                dev_tab[dev] (dev, IO_WD, opnd >> 8);   /* send hi byte */
                dev_tab[dev] (dev, IO_WD, opnd & DMASK8); /* send lo byte */
                }
            int_eval ();                                /* re-eval intr */
            cc = 0;
            }
        else cc = CC_V;
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
            t = 0;                                      /* read zero */
            cc = CC_V;                                  /* set V */
            }
        if (OP_TYPE (op) != OP_RR)                      /* RX or RR? */
            WriteB (ea, t);
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
            t = 0;                                      /* read zero */
            cc = CC_V;                                  /* set V */
            }
        if (OP_TYPE (op) != OP_RR)                      /* RX or RR? */ 
            WriteH (ea, t);
        else R[r2] = t;
        int_eval ();                                    /* re-eval intr */
        break;

    case 0x9F:                                          /* AIR - RR */
    case 0xDF:                                          /* AI - RX */
        R[r1] = int_getdev ();                          /* get int dev */
                                                        /* fall through */
    case 0x9D:                                          /* SSR - RR */
    case 0xDD:                                          /* SS - RX */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            dev_tab[dev] (dev, IO_ADR, 0);              /* select */
            t = dev_tab[dev] (dev, IO_SS, 0);           /* get status */
            }
        else t = STA_EX;                                /* no */
        if (OP_TYPE (op) != OP_RR)                      /* RR or RX? */
            WriteB (ea, t);
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
    case 0xD6:                                          /* WB - RXH */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            if (OP_TYPE (op) != OP_RR)
                lim = ReadH ((ea + 2) & VAMASK);
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
    case 0xD7:                                          /* RB - RXH */
        dev = R[r1] & DEV_MAX;
        if (DEV_ACC (dev)) {                            /* dev exist? */
            if (OP_TYPE (op) != OP_RR)
                lim = ReadH ((ea + 2) & VAMASK);
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
        dev = ReadB (AL_DEV);                           /* get device */
        t = ReadB (AL_IOC);                             /* get command */
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
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}

/* Load new PSW and memory map */

uint32 newPSW (uint32 val)
{
PSW = val & psw_mask;                                   /* store PSW */
int_eval ();                                            /* update intreq */
if (PSW & PSW_WAIT)                                     /* wait state? */
    qevent = qevent | EV_WAIT;
else qevent = qevent & ~EV_WAIT;
if (cpu_unit.flags & UNIT_816E) {                       /* mapping enabled? */
    uint32 map = PSW_GETMAP (PSW);                      /* get new map */
    s0_rel = s0_rel_const[map];                         /* set relocation */
    s1_rel = s1_rel_const[map];                         /* constants */
    }
else s0_rel = s1_rel = 0;                               /* no relocation */
if (PSW & PSW_AIO)                                      /* PSW<4> controls */
    SET_ENB (v_DS);
else CLR_ENB (v_DS);                                    /* DS interrupts */
return PSW & CC_MASK;
}

/* Swap PSW */

uint32 swap_psw (uint32 loc, uint32 cc)
{
WriteH (loc, BUILD_PSW (cc));                           /* write PSW, PC */
WriteH (loc + 2, PC);
cc = newPSW (ReadH (loc + 4));                          /* read PSW, PC */
PC = ReadH (loc + 6);
if (PSW & PSW_SQI)                                      /* sys q int enb? */
    cc = testsysq (cc);
return cc;                                              /* return CC */
}

/* Test for queue interrupts */

uint32 testsysq (uint32 cc)
{
int32 qb = ReadH (SQP);                                 /* get sys q addr */
int32 usd = ReadB (qb + Q16_USD);                       /* get use count */

if (usd) {                                              /* any entries? */
    WriteH (SQIPSW, BUILD_PSW (cc));                    /* swap PSW */
    WriteH (SQIPSW + 2, PC);
    cc = newPSW (ReadH (SQIPSW + 4));
    PC = ReadH (SQIPSW + 6);
    }
return cc;
}

/* Add to head of queue */

uint32 addtoq (uint32 ea, uint32 val, uint32 flg)
{
uint32 slt, usd, wra, t;

t = ReadH (ea);                                         /* slots/used */
slt = (t >> 8) & DMASK8;                                /* # slots */
usd = t & DMASK8;                                       /* # used */
if (usd >= slt)                                         /* list full? */
    return CC_V;
usd = usd + 1;                                          /* inc # used */
WriteB (ea + Q16_USD, usd);                             /* rewrite */
if (flg) {                                              /* ABL? */
    wra = ReadB ((ea + Q16_BOT) & VAMASK);              /* get bottom */
    t = wra + 1;                                        /* adv bottom */
    if (t >= slt)                                       /* wrap if necc */
        t = 0;
    WriteB ((ea + Q16_BOT) & VAMASK, t);                /* rewrite bottom */
    }
else {                                                  /* ATL */
    wra = ReadB ((ea + Q16_TOP) & VAMASK);              /* get top */
    if (wra == 0)                                       /* wrap if necc */
        wra = (slt - 1) & DMASK8;
    else wra = wra - 1;                                 /* dec top */
    WriteB ((ea + Q16_TOP) & VAMASK, wra);              /* rewrite top */
    }
WriteH ((ea + Q16_BASE + (wra * Q16_SLNT)) & VAMASK, val); /* write slot */
return 0;
}

uint32 remfmq (uint32 ea, uint32 r1, uint32 flg)
{
uint32 slt, usd, rda, t;

t = ReadH (ea);                                         /* get slots/used */
slt = (t >> 8) & DMASK8;                                /* # slots */
usd = t & DMASK8;                                       /* # used */
if (usd == 0)                                           /* empty? */
    return CC_V;
usd = usd - 1;                                          /* dec used */
WriteB (ea + Q16_USD, usd);                             /* rewrite */
if (flg) {                                              /* RBL? */
    rda = ReadB ((ea + Q16_BOT) & VAMASK);              /* get bottom */
    if (rda == 0)                                       /* wrap if necc */
        rda = (slt - 1) & DMASK8;
    else rda = rda - 1;                                 /* dec bottom */
    WriteB ((ea + Q16_BOT) & VAMASK, rda);              /* rewrite bottom */
    }
else {
    rda = ReadB ((ea + Q16_TOP) & VAMASK);              /* RTL, get top */
    t = rda + 1;                                        /* adv top */
    if (t >= slt)                                       /* wrap if necc */
        t = 0;
    WriteB ((ea + Q16_TOP) & VAMASK, t);                /* rewrite top */
    }
R[r1] = ReadH ((ea + Q16_BASE + (rda * Q16_SLNT)) & VAMASK); /* read slot */
if (usd)                                                /* set cc's */
    return CC_G;
else return 0;
}

/* Automatic interrupt processing */

#define CCW16_ERR(x)    (((x)|CCW16_INIT|CCW16_NOP|CCW16_Q) & \
                     ~(CCW16_CHN|CCW16_CON|CCW16_HI))

uint32 int_auto (uint32 dev, uint32 cc)
{
int32 ba, ea, by, vec, ccw, bpi, fnc, trm, st, i, t;
t_bool sysqe = FALSE;
t_bool rpt = FALSE;

do {
    vec = ReadH (INTSVT + dev + dev);                   /* get vector */
    if ((vec & 1) == 0) {                               /* immed int? */
        WriteH (vec, BUILD_PSW (cc));                   /* write PSW, PC */
        WriteH ((vec + 2) & VAMASK, PC);
        cc = newPSW (ReadH ((vec + 4) & VAMASK));       /* read PSW */
        PC = (vec + 6) & VAMASK;                        /* set new PC */
        return cc;
        }
    vec = vec & ~1;                                     /* get CCW addr */
    ccw = ReadH (vec);                                  /* read CCW */
    if (DEV_ACC (dev))                                  /* select dev */
        dev_tab[dev] (dev, IO_ADR, 0);
    if (ccw & CCW16_NOP)                                /* NOP? exit */
        break;
    if (ccw & CCW16_INIT) {                             /* init set? */
        ccw = ccw & ~CCW16_INIT;                        /* clr init */
        WriteH (vec, ccw);                              /* rewrite */
        if (ccw & CCW16_OC) {                           /* OC set? */
            if (DEV_ACC (dev)) {                        /* dev exist? */
                by = ReadB ((vec + CCB16_IOC) & VAMASK);/* read OC byte */
                dev_tab[dev] (dev, IO_OC, by);          /* send to dev */
				}
            break;                                      /* and exit */
            }
        }
    fnc = CCW16_FNC (ccw);                              /* get func */
    st = 0;                                             /* default status */
    if (fnc == CCW16_DMT) {                             /* DMT */
        ba = ReadH ((vec + CCB16_STR) & VAMASK);        /* get cnt wd */
        ba = (ba - 1) & DMASK16;                        /* decr */
        WriteH ((vec + CCB16_STR) & VAMASK, ba);        /* rewrite */
        if (ba)                                         /* nz? exit */
            break;
        }                                               /* end if dmt */
    else if (fnc != CCW16_NUL) {                        /* rd or wr? */
        if (DEV_ACC (dev))                              /* dev exist? */
            st = dev_tab[dev] (dev, IO_SS, 0);          /* sense status */
        else st = CC_V;                                 /* else timeout */
        if (st & 0xF) {                                 /* error? */
            ccw = CCW16_ERR (ccw);                      /* neuter CCW */
            WriteH (vec, ccw);                          /* rewrite CCW */
            }
        else {                                          /* ok, do xfer */
            bpi = CCW16_BPI (ccw);                      /* get bytes/int */
            if (bpi == 0)                               /* max 16B */
                bpi = 16;
            ba = ReadH ((vec + CCB16_STR) & VAMASK);    /* get start */
            for (i = 0; i < bpi; i++) {                 /* do # bytes */
                if (fnc == CCW16_RD) {                  /* chan read? */
                    by = dev_tab[dev] (dev, IO_RD, 0);  /* read byte */
                    WriteB (ba, by);                    /* store */
                    }
                else {                                  /* chan write */
                    by = ReadB (ba);                    /* fetch */
                    dev_tab[dev] (dev, IO_WD, by);      /* write byte */
                    }
                ba = (ba + 1) & VAMASK;                 /* incr addr */
                }
            WriteH ((vec + CCB16_STR) & VAMASK, ba);    /* rewrite */
            ea = ReadH ((vec + CCB16_END) & VAMASK);    /* get end */
            trm = ReadB ((vec + CCB16_TRM) & VAMASK);   /* get term chr */
            if ((ba <= ea) &&                           /* not at end? */
                (((ccw & CCW16_TRM) == 0) ||            /* not term chr? */
                (by != trm)))                           /* exit */
                break;
            ccw = ccw | CCW16_NOP;                      /* nop CCW */
            WriteH (vec, ccw);                          /* rewrite CCW */
            }                                           /* end else sta */
        }                                               /* end if r/w */

/* Termination phase */

    t = (dev << 8) | (st & DMASK8);                     /* form dev/sta */
    WriteH ((vec + CCB16_DEV) & VAMASK, t);             /* write dev/sta */
    if (ccw & CCW16_Q) {                                /* q request? */
        t = ReadH (SQP);                                /* get sys q addr */
        if (addtoq (t, vec, ccw & CCW16_HI)) {          /* add to sys q */
            WriteH (SQOP, vec);                         /* write to ovflo */
            return swap_psw (SQVPSW, cc);               /* take exception */
            }
        else sysqe = TRUE;                              /* made an entry */
        }
    if (ccw & CCW16_CHN) {                              /* chain */
        t = ReadH ((vec + CCB16_CHN) & VAMASK);         /* get chain wd */
        WriteH (INTSVT + dev + dev, t);                 /* wr int svc tab */
        if (ccw & CCW16_CON)                            /* cont? */
            rpt = TRUE;
        }
    } while (rpt);

/* Common exit */

if (sysqe && (PSW & PSW_SQI))                           /* sys q ent & enb? */
    return swap_psw (SQIPSW, cc);                       /* take sys q int */
return cc;
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
        op = op & 0xC0;
        if (op == 0x40) {                               /* x40 = inc */
            drmod = 1;
            drpos = srpos = 0;                          /* init cntrs */
            }
        else if (op == 0x80)                            /* x80 = norm */
            drmod = 0;
        break;

    case IO_WD:                                         /* write */
        if (drpos < 4) 
             DR = (DR & ~(DMASK8 << (drpos * 8))) | (dat << (drpos * 8));
        else if (drpos == 4)
            DRX = dat;
        drpos = (drpos + 1) &
            ((cpu_unit.flags & (UNIT_716 | UNIT_816))? 7: 3);
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

uint32 ReadB (uint32 loc)
{
uint32 pa = (loc + ((loc & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;

return ((M[pa >> 1] >> ((pa & 1)? 0: 8)) & DMASK8);
}

uint32 ReadH (uint32 loc)
{
uint32 pa = (loc + ((loc & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;

return M[pa >> 1];
}

uint32 ReadF (uint32 loc, uint32 rel)
{
uint32 pa, pa1;
uint32 loc1 = (loc + 2) & VAMASK;

loc = loc & VAMASK;                                     /* FP doesn't mask */
if (rel) {
    pa = (loc + ((loc & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;
    pa1 = (loc1 + ((loc1 & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;
    }
else {
    pa = loc;
    pa1 = loc1;
    }
return (((uint32) M[pa >> 1]) << 16) | ((uint32) M[pa1 >> 1]);
}

void WriteB (uint32 loc, uint32 val)
{
uint32 pa = (loc + ((loc & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;

val = val & DMASK8;
if (MEM_ADDR_OK (pa))
    M[pa >> 1] = ((pa & 1)? ((M[pa >> 1] & ~DMASK8) | val):
                            ((M[pa >> 1] & DMASK8) | (val << 8)));
return;
}

void WriteH (uint32 loc, uint32 val)
{
uint32 pa = (loc + ((loc & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;

if (MEM_ADDR_OK (pa))
    M[pa >> 1] = val & DMASK16;
return;
}

void WriteF (uint32 loc, uint32 val, uint32 rel)
{
uint32 pa, pa1;
uint32 loc1 = (loc + 2) & VAMASK;

loc = loc & VAMASK;                                     /* FP doesn't mask */
if (rel) {
    pa = (loc + ((loc & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;
    pa1 = (loc1 + ((loc1 & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;
    }
else {
    pa = loc;
    pa1 = loc1;
    }
if (MEM_ADDR_OK (pa))
    M[pa >> 1] = (val >> 16) & DMASK16;
if (MEM_ADDR_OK (pa1))
    M[pa1 >> 1] = val & DMASK16;
return;
}

uint32 IOReadB (uint32 loc)
{
return ((M[loc >> 1] >> ((loc & 1)? 0: 8)) & DMASK8);
}

void IOWriteB (uint32 loc, uint32 val)
{
val = val & DMASK8;
M[loc >> 1] = ((loc & 1)?
    ((M[loc >> 1] & ~DMASK8) | val):
    ((M[loc >> 1] & DMASK8) | (val << 8)));
return;
}

uint32 IOReadH (uint32 loc)
{
return (M[loc >> 1] & DMASK16);
}

void IOWriteH (uint32 loc, uint32 val)
{
M[loc >> 1] = val & DMASK16;
return;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
qevent = 0;                                             /* no events */
newPSW (0);                                             /* PSW = 0 */
DR = 0;                                                 /* clr display */
drmod = 0;
blk_io.dfl = blk_io.cur = blk_io.end = 0;               /* no block IO */
sim_brk_types = sim_brk_dflt = SWMASK ('E');            /* init bkpts */
if (M == NULL)
    M = (uint16 *) calloc (MAXMEMSIZE16E >> 1, sizeof (uint16));
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
if (sw & SWMASK ('V')) {
    if (addr > VAMASK)
        return SCPE_NXM;
    addr = (addr + ((addr & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;
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
if (sw & SWMASK ('V')) {
    if (addr > VAMASK)
        return SCPE_NXM;
    addr = (addr + ((addr & VA_S1)? s1_rel: s0_rel)) & PAMASK16E;
    }
if (addr >= MEMSIZE)
    return SCPE_NXM;
IOWriteH (addr, val);
return SCPE_OK;
}

/* Change memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || ((val & 0xFFF) != 0) ||
    (((uint32) val) > ((uptr->flags & UNIT_816E)? MAXMEMSIZE16E: MAXMEMSIZE16)))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i = i + 2)
    mc = mc | M[i >> 1];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE16E; i = i + 2)
    M[i >> 1] = 0;
return SCPE_OK;
}

/* Change CPU model */

t_stat cpu_set_model (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 i;

if (!(val & UNIT_816E) && (MEMSIZE > MAXMEMSIZE16)) {
    MEMSIZE = MAXMEMSIZE16;
    for (i = MEMSIZE; i < MAXMEMSIZE16E; i = i + 2)
        M[i >> 1] = 0;
    printf ("Reducing memory to 64KB\n");
    }
return SCPE_OK;
}

/* Set console interrupt */

t_stat cpu_set_consint (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if ((uptr->flags & (UNIT_716 | UNIT_816 | UNIT_816E)) == 0)
    return SCPE_NOFNC;
if (PSW & PSW_AIO)
    SET_INT (v_DS);
return SCPE_OK;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 i, lnt;
t_stat r;

if (cptr == NULL) {
    for (i = 0; i < hst_lnt; i++)
        hst[i].vld = 0;
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

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 op, k, di, lnt;
char *cptr = (char *) desc;
t_value sim_eval[2];
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
fprintf (st, "PC    r1    opnd  ea    IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(di++) % hst_lnt];                         /* entry pointer */
    if (h->vld) {                                       /* instruction? */
        fprintf (st, "%04X  %04X  %04X  ", h->pc, h->r1, h->opnd);
        op = (h->ir1 >> 8) & 0xFF;
        if (OP_TYPE (op) >= OP_RX)
            fprintf (st, "%04X  ", h->ea);
        else fprintf (st, "      ");
        sim_eval[0] = h->ir1;
        sim_eval[1] = h->ir2;
        if ((fprint_sym (st, h->pc, sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "(undefined) %04X", h->ir1);
        fputc ('\n', st);                               /* end line */
        }                                               /* end if instruction */
    }                                                   /* end for */
return SCPE_OK;
}
