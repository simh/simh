/* h316_cpu.c: Honeywell 316/516 CPU simulator

   Copyright (c) 1999-2008, Robert M. Supnik

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

   cpu          H316/H516 CPU

   28-Apr-07    RMS     Removed clock initialization
   03-Apr-06    RMS     Fixed bugs in LLL, LRL (from Theo Engel)
   22-Sep-05    RMS     Fixed declarations (from Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   15-Feb-05    RMS     Added start button interrupt
   01-Dec-04    RMS     Fixed bug in DIV
   06-Nov-04    RMS     Added =n to SHOW HISTORY
   04-Jan-04    RMS     Removed unnecessary compare
   31-Dec-03    RMS     Fixed bug in cpu_set_hist
   24-Oct-03    RMS     Added DMA/DMC support, instruction history
   30-Dec-01    RMS     Added old PC queue
   03-Nov-01    RMS     Fixed NOHSA modifier
   30-Nov-01    RMS     Added extended SET/SHOW support

   The register state for the Honeywell 316/516 CPU is:

   AR<1:16>             A register
   BR<1:16>             B register
   XR<1:16>             X register
   PC<1:16>             P register (program counter)
   Y<1:16>              memory address register
   MB<1:16>             memory data register
   C                    overflow flag
   EXT                  extend mode flag
   DP                   double precision mode flag
   SC<1:5>              shift count
   SR[1:4]<0>           sense switches 1-4

   The Honeywell 316/516 has six instruction formats: memory reference,
   I/O, control, shift, skip, and operate.

   The memory reference format is:

     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|xr|     op    |sc|           offset         | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <13:10>      mnemonic        action

   0000         (other)         see control, shift, skip, operate instructions
   0001         JMP             P = MA
   0010         LDA             A = M[MA]
   0011         ANA             A = A & M[MA]
   0100         STA             M[MA] = A
   0101         ERA             A = A ^ M[MA]
   0110         ADD             A = A + M[MA]
   0111         SUB             A = A - M[MA]
   1000         JST             M[MA] = P, P = MA + 1
   1001         CAS             skip if A == M[MA], double skip if A < M[MA]
   1010         IRS             M[MA] = M[MA] + 1, skip if M[MA] == 0
   1011         IMA             A <=> M[MA]
   1100         (I/O)           see I/O instructions
   1101         LDX/STX         X = M[MA] (xr = 1), M[MA] = x (xr = 0)
   1110         MPY             multiply
   1111         DIV             divide

   In non-extend mode, memory reference instructions can access an address
   space of 16K words.  Multiple levels of indirection are supported, and
   each indirect word supplies its own indirect and index bits.

   <1,2,7>      mode                            action

   0,0,0        sector zero direct              MA = IR<8:0>
   0,0,1        current direct                  MA = P<13:9>'IR<8:0>
   0,1,0        sector zero indexed             MA = IR<8:0> + X
   0,1,1        current direct                  MA = P<13:9>'IR<8:0> + X
   1,0,0        sector zero indirect            MA = M[IR<8:0>]
   1,0,1        current indirect                MA = M[P<13:9>'IR<8:0>]
   1,1,0        sector zero indirect indexed    MA = M[IR<8:0> + X]
   1,1,1        current indirect indexed        MA = M[MA = P<13:9>'IR<8:0> + X]

   In extend mode, memory reference instructions can access an address
   space of 32K words.  Multiple levels of indirection are supported, but
   only post-indexing, based on the original instruction word index flag,
   is allowed.

   The control format is:

     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0  0  0|           opcode            | control
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The shift format is:

     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  1  0  0  0  0|dr|sz|type |   shift count   | shift
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                       |  | \-+-/
                       |  |   |
                       |  |   +--------------------- type
                       |  +------------------------- long/A only
                       +---------------------------- right/left

   The skip format is:

     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  0  0  0  0  0|rv|po|pe|ev|ze|s1|s2|s3|s4|cz| skip
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                       |  |  |  |  |  |  |  |  |  |
                       |  |  |  |  |  |  |  |  |  +- skip if C = 0
                       |  |  |  |  |  |  |  |  +---- skip if ssw 4 = 0
                       |  |  |  |  |  |  |  +------- skip if ssw 3 = 0
                       |  |  |  |  |  |  +---------- skip if ssw 2 = 0
                       |  |  |  |  |  +------------- skip if ssw 1 = 0
                       |  |  |  |  +---------------- skip if A == 0
                       |  |  |  +------------------- skip if A<0> == 0
                       |  |  +---------------------- skip if mem par err
                       |  +------------------------- skip if A<15> = 0
                       +---------------------------- reverse skip sense

   The operate format is:

     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  0  0  0  0|           opcode            | operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The I/O format is:

     1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | op  | 1  1  0  0|  function |      device     | I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IO transfer instruction controls the specified device.
   Depending on the opcode, the instruction may set or clear
   the device flag, start or stop I/O, or read or write data.

   This routine is the instruction decode routine for the Honeywell
   316/516.  It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        infinite indirection loop
        unimplemented instruction and stop_inst flag set
        unknown I/O device and stop_dev flag set
        I/O error in I/O simulator

   2. Interrupts.  Interrupts are maintained by two parallel variables:

        dev_int         device interrupt flags
        dev_enb         device interrupt enable flags

      In addition, dev_int contains the interrupt enable and interrupt no
      defer flags.  If interrupt enable and interrupt no defer are set, and
      at least one interrupt request is pending, then an interrupt occurs.
      The order of flags in these variables corresponds to the order
      in the SMK instruction.
 
   3. Non-existent memory.  On the H316/516, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        h316_defs.h     add interrupt request definition
        h316_cpu.c      add device dispatch table entry
        h316_sys.c      add sim_devices table entry
*/

#include "h316_defs.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC
#define PCQ_TOP         pcq[pcq_p]
#define m7              0001000                         /* for generics */
#define m8              0000400
#define m9              0000200
#define m10             0000100
#define m11             0000040
#define m12             0000020
#define m13             0000010
#define m14             0000004
#define m15             0000002
#define m16             0000001

#define HIST_PC         0x40000000
#define HIST_C          0x20000000
#define HIST_EA         0x10000000
#define HIST_MIN        64
#define HIST_MAX        65536

typedef struct {
    int32               pc;
    int32               ir;
    int32               ar;
    int32               br;
    int32               xr;
    int32               ea;
    int32               opnd;
    } InstHistory;

uint16 M[MAXMEMSIZE] = { 0 };                           /* memory */
int32 saved_AR = 0;                                     /* A register */
int32 saved_BR = 0;                                     /* B register */
int32 saved_XR = 0;                                     /* X register */
int32 PC = 0;                                           /* P register */
int32 C = 0;                                            /* C register */
int32 ext = 0;                                          /* extend mode */
int32 pme = 0;                                          /* prev mode extend */
int32 extoff_pending = 0;                               /* extend off pending */
int32 dp = 0;                                           /* double mode */
int32 sc = 0;                                           /* shift count */
int32 ss[4];                                            /* sense switches */
int32 dev_int = 0;                                      /* dev ready */
int32 dev_enb = 0;                                      /* dev enable */
int32 ind_max = 8;                                      /* iadr nest limit */
int32 stop_inst = 1;                                    /* stop on ill inst */
int32 stop_dev = 2;                                     /* stop on ill dev */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
uint32 dma_nch = DMA_MAX;                               /* number of chan */
uint32 dma_ad[DMA_MAX] = { 0 };                         /* DMA addresses */
uint32 dma_wc[DMA_MAX] = { 0 };                         /* DMA word count */
uint32 dma_eor[DMA_MAX] = { 0 };                        /* DMA end of range */
uint32 chan_req = 0;                                    /* channel requests */
uint32 chan_map[DMA_MAX + DMC_MAX] = { 0 };             /* chan->dev map */
int32 (*iotab[DEV_MAX])(int32 inst, int32 fnc, int32 dat, int32 dev) = { NULL };
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */

extern int32 sim_int_char;
extern int32 sim_interval;
extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */
extern FILE *sim_log;
extern DEVICE *sim_devices[];

t_bool devtab_init (void);
int32 dmaio (int32 inst, int32 fnc, int32 dat, int32 dev);
int32 undio (int32 inst, int32 fnc, int32 dat, int32 dev);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_noext (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_show_dma (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_set_nchan (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_nchan (FILE *st, UNIT *uptr, int32 val, void *desc);

extern t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

DIB cpu_dib = { DMA, IOBUS, 1, &dmaio };

UNIT cpu_unit = {
    UDATA (NULL, UNIT_FIX+UNIT_BINK+UNIT_EXT+UNIT_HSA+UNIT_DMC, MAXMEMSIZE)
    };

REG cpu_reg[] = {
    { ORDATA (P, PC, 15) },
    { ORDATA (A, saved_AR, 16) },
    { ORDATA (B, saved_BR, 16) },
    { ORDATA (X, XR, 16) },
    { ORDATA (SC, sc, 16) },
    { FLDATA (C, C, 0) },
    { FLDATA (EXT, ext, 0) },
    { FLDATA (PME, pme, 0) },
    { FLDATA (EXT_OFF, extoff_pending, 0) },
    { FLDATA (DP, dp, 0) },
    { FLDATA (SS1, ss[0], 0) },
    { FLDATA (SS2, ss[1], 0) },
    { FLDATA (SS3, ss[2], 0) },
    { FLDATA (SS4, ss[3], 0) },
    { FLDATA (ION, dev_int, INT_V_ON) },
    { FLDATA (INODEF, dev_int, INT_V_NODEF) },
    { FLDATA (START, dev_int, INT_V_START) },
    { ORDATA (DEVINT, dev_int, 16), REG_RO },
    { ORDATA (DEVENB, dev_enb, 16), REG_RO },
    { ORDATA (CHREQ, chan_req, DMA_MAX + DMC_MAX) },
    { BRDATA (DMAAD, dma_ad, 8, 16, DMA_MAX) },
    { BRDATA (DMAWC, dma_wc, 8, 16, DMA_MAX) },
    { BRDATA (DMAEOR, dma_eor, 8, 1, DMA_MAX) },
    { ORDATA (DMANCH, dma_nch, 3), REG_HRO },
    { FLDATA (MPERDY, dev_int, INT_V_MPE) },
    { FLDATA (MPEENB, dev_enb, INT_V_MPE) },
    { FLDATA (STOP_INST, stop_inst, 0) },
    { FLDATA (STOP_DEV, stop_dev, 1) },
    { DRDATA (INDMAX, ind_max, 8), REG_NZ + PV_LEFT },
    { BRDATA (PCQ, pcq, 8, 15, PCQ_SIZE), REG_RO + REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { ORDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_EXT, 0, "no extend", "NOEXTEND", &cpu_set_noext },
    { UNIT_EXT, UNIT_EXT, "extend", "EXTEND", NULL },
    { UNIT_HSA, 0, "no HSA", "NOHSA", NULL },
    { UNIT_HSA, UNIT_HSA, "HSA", "HSA", NULL },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
    { MTAB_XTD | MTAB_VDV, 0, "channels", "CHANNELS",
      &cpu_set_nchan, &cpu_show_nchan, NULL },
    { UNIT_DMC, 0, "no DMC", "NODMC", NULL },
    { UNIT_DMC, UNIT_DMC, "DMC", "DMC", NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "DMA1", NULL,
      NULL, &cpu_show_dma, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "DMA2", NULL,
      NULL, &cpu_show_dma, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 2, "DMA3", NULL,
      NULL, &cpu_show_dma, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 3, "DMA4", NULL,
      NULL, &cpu_show_dma, NULL },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 15, 1, 8, 16,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    &cpu_dib, 0
    };

t_stat sim_instr (void)
{
int32 AR, BR, MB, Y, t1, t2, t3, skip, dev;
uint32 ut;
t_stat reason;
t_stat Ea (int32 inst, int32 *addr);
void Write (int32 addr, int32 val);
int32 Add16 (int32 val1, int32 val2);
int32 Add31 (int32 val1, int32 val2);
int32 Operate (int32 MB, int32 AR);

#define Read(ad)        M[(ad)]
#define GETDBL_S(h,l)   (((h) << 15) | ((l) & MMASK))
#define GETDBL_U(h,l)   (((h) << 16) | (l))
#define PUTDBL_S(x)     AR = ((x) >> 15) & DMASK; \
                        BR = (BR & SIGN) | ((x) & MMASK)
#define PUTDBL_U(x)     AR = ((x) >> 16) & DMASK; \
                        BR = (x) & DMASK
#define SEXT(x)         (((x) & SIGN)? ((x) | ~DMASK): ((x) & DMASK))
#define NEWA(c,n)       (ext? (((c) & ~X_AMASK) | ((n) & X_AMASK)): \
                        (((c) & ~NX_AMASK) | ((n) & NX_AMASK)))

/* Restore register state */

if (devtab_init ())                                     /* init tables */
    return SCPE_STOP;
AR = saved_AR & DMASK;                                  /* restore reg */
BR = saved_BR & DMASK;
XR = saved_XR & DMASK;
PC = PC & ((cpu_unit.flags & UNIT_EXT)? X_AMASK: NX_AMASK); /* mask PC */
reason = 0;

/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until halted */

if (sim_interval <= 0) {                                /* check clock queue */
    if (reason = sim_process_event ())
        break;
    }

/* Channel breaks (DMA and DMC) */

if (chan_req) {                                         /* channel request? */
    int32 i, t, ch, dev, st, end, ad, dmcad;
    t_stat r;
    for (i = 0, ch = chan_req; ch != 0; i++, ch = ch >> 1) {
        if (ch & 1) {                                   /* req on chan i? */
            dev = chan_map[i];                          /* get dev for chan */
            if (iotab[dev] == &undio)
                return SCPE_IERR;
            chan_req = chan_req & ~(1 << i);            /* clear req */
            if (Q_DMA (i))                              /* DMA? */
                st = dma_ad[i];
            else {                                      /* DMC */
                dmcad = DMC_BASE + ((i - DMC_V_DMC1) << 1);
                st = Read (dmcad);                      /* DMC ctrl word */
                }
            ad = st & X_AMASK;                          /* get curr addr */
            if (st & DMA_IN) {                          /* input? */
                t = iotab[dev] (ioINA, 0, 0, dev);      /* input word */
                if ((t & IOT_SKIP) == 0)
                    return STOP_DMAER;
                if ((r = t >> IOT_V_REASON) != 0)
                    return r;
                Write (ad, t & DMASK);                  /* write to mem */
                }
            else {                                      /* no, output */
                t = iotab[dev] (ioOTA, 0, Read (ad), dev);      /* output word */
                if ((t & IOT_SKIP) == 0)
                    return STOP_DMAER;
                if (r = (t >> IOT_V_REASON))
                    return r;
                }
            if (Q_DMA (i)) {                            /* DMA? */
                dma_ad[i] = (dma_ad[i] & DMA_IN) | ((ad + 1) & X_AMASK);
                dma_wc[i] = (dma_wc[i] + 1) & 077777;   /* update wc */
                if (dma_wc[i] == 0) {                   /* done? */
                    dma_eor[i] = 1;                     /* set end of range */
                    t = iotab[dev] (ioEND, 0, 0, dev);  /* send end range */
                    if ((r = t >> IOT_V_REASON) != 0)
                        return r;
                    }
                }
            else {                                      /* DMC */
                st = (st & DMA_IN) | ((ad + 1) & X_AMASK);
                Write (dmcad, st);                      /* update start */
                end = Read (dmcad + 1);                 /* get end */
                if (((ad ^ end) & X_AMASK) == 0) {      /* start == end? */
                    t = iotab[dev] (ioEND, 0, 0, dev);  /* send end range */
                    if ((r = t >> IOT_V_REASON) != 0)
                        return r;
                    }                                   /* end if end range */
                }                                       /* end else DMC */
            }                                           /* end if chan i */
        }                                               /* end for */
    }                                                   /* end if chan_req */


/* Interrupts */

if ((dev_int & (INT_PEND|INT_NMI|dev_enb)) > INT_PEND) {/* int req? */
    pme = ext;                                          /* save extend */
    if (cpu_unit.flags & UNIT_EXT)                      /* ext opt? extend on */
        ext = 1;
    dev_int = dev_int & ~INT_ON;                        /* intr off */
    MB = 0120000 | M_INT;                               /* inst = JST* 63 */
    }

/* Instruction fetch */

else {
    if (sim_brk_summ &&
        sim_brk_test (PC, SWMASK ('E'))) {              /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }
    Y = PC;                                             /* set mem addr */
    MB = Read (Y);                                      /* fetch instr */
    PC = NEWA (Y, Y + 1);                               /* incr PC */
    dev_int = dev_int | INT_NODEF;
    }

dev_int = dev_int & ~INT_START;                         /* clr start button int */
sim_interval = sim_interval - 1;
if (hst_lnt) {                                          /* instr hist? */
    hst_p = (hst_p + 1);                                /* next entry */
    if (hst_p >= hst_lnt)
        hst_p = 0;
    hst[hst_p].pc = Y | HIST_PC | (C? HIST_C: 0);       /* fill slots */
    hst[hst_p].ir = MB;
    hst[hst_p].ar = AR;
    hst[hst_p].br = BR;
    hst[hst_p].xr = XR;
    }

/* Memory reference instructions */

switch (I_GETOP (MB)) {                                 /* case on <1:6> */

    case 001: case 021: case 041: case 061:             /* JMP */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        PCQ_ENTRY;                                      /* save PC */
        PC = NEWA (PC, Y);                              /* set new PC */
        if (extoff_pending)                             /* cond ext off */
            ext = extoff_pending = 0;
        break;

    case 002: case 022: case 042: case 062:             /* LDA */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        if (dp) {                                       /* double prec? */
            AR = Read (Y & ~1);                         /* get doubleword */
            BR = Read (Y | 1);
            sc = 0;
            }
        else AR = Read (Y);                             /* no, get word */
        break;

    case 003: case 023: case 043: case 063:             /* ANA */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        AR = AR & Read (Y);
        break;

    case 004: case 024: case 044: case 064:             /* STA */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        Write (Y, AR);                                  /* store A */
        if (dp) {                                       /* double prec? */
            Write (Y | 1, BR);                          /* store B */
            sc = 0;
            }
        break;

    case 005: case 025: case 045: case 065:             /* ERA */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        AR = AR ^ Read (Y);
        break;

    case 006: case 026: case 046: case 066:             /* ADD */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        if (dp) {                                       /* double prec? */
            t1 = GETDBL_S (AR, BR);                     /* get A'B */
            t2 = GETDBL_S (Read (Y & ~1), Read (Y | 1));
            t1 = Add31 (t1, t2);                        /* 31b add */
            PUTDBL_S (t1);
            sc = 0;
            }
        else AR = Add16 (AR, Read (Y));                 /* no, 16b add */
        break;

    case 007: case 027: case 047: case 067:             /* SUB */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        if (dp) {                                       /* double prec? */
            t1 = GETDBL_S (AR, BR);                     /* get A'B */
            t2 = GETDBL_S (Read (Y & ~1), Read (Y | 1));
            t1 = Add31 (t1, -t2);                       /* 31b sub */
            PUTDBL_S (t1);
            sc = 0;
            }
        else AR = Add16 (AR, (-Read (Y)) & DMASK);      /* no, 16b sub */
        break;

    case 010: case 030: case 050: case 070:             /* JST */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        MB = NEWA (Read (Y), PC);                       /* merge old PC */
        Write (Y, MB);
        PCQ_ENTRY;
        PC = NEWA (PC, Y + 1);                          /* set new PC */
        break;

    case 011: case 031: case 051: case 071:             /* CAS */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        MB = Read (Y);
        if (AR == MB)
            PC = NEWA (PC, PC + 1);
        else if (SEXT (AR) < SEXT (MB))
            PC = NEWA (PC, PC + 2);
        break;

    case 012: case 032: case 052: case 072:             /* IRS */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        MB = (Read (Y) + 1) & DMASK;                    /* incr, rewrite */
        Write (Y, MB);
        if (MB == 0)                                    /* skip if zero */
            PC = NEWA (PC, PC + 1);
        break;

    case 013: case 033: case 053: case 073:             /* IMA */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        MB = Read (Y);
        Write (Y, AR);                                  /* A to mem */
        AR = MB;                                        /* mem to A */
        break;

    case 015: case 055:                                 /* STX */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        Write (Y, XR);                                  /* store XR */
        break;

    case 035: case 075:                                 /* LDX */
        if (reason = Ea (MB, &Y))                       /* eff addr */
            break;
        XR = Read (Y);                                  /* load XR */
        break;

    case 016: case 036: case 056: case 076:             /* MPY */
        if (cpu_unit.flags & UNIT_HSA) {                /* installed? */
            if (reason = Ea (MB, &Y))                   /* eff addr */
                break;
            t1 = SEXT (AR) * SEXT (Read (Y));
            PUTDBL_S (t1);
            sc = 0;
            }
        else reason = stop_inst;
        break;

    case 017: case 037: case 057: case 077:             /* DIV */
        if (cpu_unit.flags & UNIT_HSA) {                /* installed? */
            if (reason = Ea (MB, &Y))                   /* eff addr */
                break;
            t2 = SEXT (Read (Y));                       /* divr */
            if (t2) {                                   /* divr != 0? */
                t1 = GETDBL_S (SEXT (AR), BR);          /* get A'B signed */
                BR = (t1 % t2) & DMASK;                 /* remainder */
                t1 = t1 / t2;                           /* quotient */
                AR = t1 & DMASK;
                if ((t1 > MMASK) || (t1 < (-SIGN)))
                    C = 1;
                else C = 0;
                sc = 0;
                }
            else C = 1;
            }
        else reason = stop_inst;
        break;

/* I/O instructions */

    case 014:                                           /* OCP */
        dev = MB & DEVMASK;
        t2 = iotab[dev] (ioOCP, I_GETFNC (MB), AR, dev);
        reason = t2 >> IOT_V_REASON;
        break;

    case 034:                                           /* SKS */
        dev = MB & DEVMASK;
        t2 = iotab[dev] (ioSKS, I_GETFNC (MB), AR, dev);
        reason = t2 >> IOT_V_REASON;
        if (t2 & IOT_SKIP)                              /* skip? */
            PC = NEWA (PC, PC + 1);
        break;

    case 054:                                           /* INA */
        dev = MB & DEVMASK;
        if (MB & INCLRA)
            AR = 0;
        t2 = iotab[dev] (ioINA, I_GETFNC (MB & ~INCLRA), AR, dev);
        reason = t2 >> IOT_V_REASON;
        if (t2 & IOT_SKIP)                              /* skip? */
            PC = NEWA (PC, PC + 1);
        AR = t2 & DMASK;                                /* data */
        break;

    case 074:                                           /* OTA */
        dev = MB & DEVMASK;
        t2 = iotab[dev] (ioOTA, I_GETFNC (MB), AR, dev);
        reason = t2 >> IOT_V_REASON;
        if (t2 & IOT_SKIP)                              /* skip? */
            PC = NEWA (PC, PC + 1);
        break;

/* Control */

    case 000:
        if ((MB & 1) == 0) {                            /* HLT */
            if ((reason = sim_process_event ()) != SCPE_OK)
                break;
            reason = STOP_HALT;
            break;
            }
        if (MB & m14) {                                 /* SGL, DBL */
            if (cpu_unit.flags & UNIT_HSA)
                dp = (MB & m15)? 1: 0;
            else reason = stop_inst;
            }
        if (MB & m13) {                                 /* DXA, EXA */
            if (!(cpu_unit.flags & UNIT_EXT))
                reason = stop_inst;
            else if (MB & m15) {                        /* EXA */
                ext = 1;
                extoff_pending = 0;                     /* DXA */
                }
            else extoff_pending = 1;
            }
        if (MB & m12)                                   /* RMP */
            CLR_INT (INT_MPE);
        if (MB & m11) {                                 /* SCA, INK */
            if (MB & m15)                               /* INK */
                AR = (C << 15) | (dp << 14) | (pme << 13) | (sc & 037);
            else if (cpu_unit.flags & UNIT_HSA)         /* SCA */
                AR = sc & 037;
            else reason = stop_inst; 
            }
        else if (MB & m10) {                            /* NRM */
            if (cpu_unit.flags & UNIT_HSA) {
                for (sc = 0;
                    (sc <= 32) && ((AR & SIGN) != ((AR << 1) & SIGN));
                     sc++) {
                    AR = (AR & SIGN) | ((AR << 1) & MMASK) |
                        ((BR >> 14) & 1);
                    BR = (BR & SIGN) | ((BR << 1) & MMASK);
                    }
                sc = sc & 037;
                }
            else reason = stop_inst;
            }
        else if (MB & m9) {                             /* IAB */
            sc = BR;
            BR = AR;
            AR = sc;
            }
        if (MB & m8)                                    /* ENB */
            dev_int = (dev_int | INT_ON) & ~INT_NODEF;
        if (MB & m7)                                    /* INH */
            dev_int = dev_int & ~INT_ON;
        break;

/* Shift

   Shifts are microcoded as follows:

        op<7>   =       right/left
        op<8>   =       long/short
        op<9>   =       shift/rotate (rotate bits "or" into new position)
        op<10>  =       logical/arithmetic

   If !op<7> && op<10> (right arithmetic), A<1> propagates rightward
   If op<7> && op<10> (left arithmetic), C is set if A<1> changes state
   If !op<8> && op<10> (long arithmetic), B<1> is skipped

   This microcoding "explains" how the 4 undefined opcodes actually work
        003     =       long arith rotate right, skip B<1>, propagate A<1>,
                        bits rotated out "or" into A<1>
        007     =       short arith rotate right, propagate A<1>,
                        bits rotated out "or" into A<1>
        013     =       long arith rotate left, skip B<1>, C = overflow
        017     =       short arith rotate left, C = overflow
*/

    case 020:
        C = 0;                                          /* clear C */
        sc = 0;                                         /* clear sc */
        if ((t1 = (-MB) & SHFMASK) == 0)                /* shift count */
            break;
        switch (I_GETFNC (MB)) {                        /* case shift fnc */

        case 000:                                       /* LRL */
            if (t1 > 32)                                /* >32? all 0 */
                ut = 0;
            else {
                ut = GETDBL_U (AR, BR);                 /* get A'B */
                C = (ut >> (t1 - 1)) & 1;               /* C = last out */
                if (t1 == 32)                           /* =32? all 0 */
                    ut = 0;
                else ut = ut >> t1;                      /* log right */
                }
            PUTDBL_U (ut);                              /* store A,B */
            break;

        case 001:                                       /* LRS */
            if (t1 > 31)                                /* limit to 31 */
                t1 = 31;
            t2 = GETDBL_S (SEXT (AR), BR);              /* get A'B signed */
            C = (t2 >> (t1 - 1)) & 1;                   /* C = last out */
            t2 = t2 >> t1;                              /* arith right */
            PUTDBL_S (t2);                              /* store A,B */
            break;

        case 002:                                       /* LRR */
            t2 = t1 % 32;                               /* mod 32 */
            ut = GETDBL_U (AR, BR);                     /* get A'B */
            ut = (ut >> t2) | (ut << (32 - t2));        /* rot right */
            C = (ut >> 31) & 1;                         /* C = A<1> */
            PUTDBL_U (ut);                              /* store A,B */
            break;

        case 003:                                       /* "long right arot" */
            if (reason = stop_inst)                     /* stop on undef? */
                break;
            for (t2 = 0; t2 < t1; t2++) {               /* bit by bit */
                C = BR & 1;                             /* C = last out */
                BR = (BR & SIGN) | ((AR & 1) << 14) |
                     ((BR & MMASK) >> 1);
                AR = ((AR & SIGN) | (C << 15)) | (AR >> 1);
                }
            break;

        case 004:                                       /* LGR */
            if (t1 > 16)                                /* > 16? all 0 */
                AR = 0;
            else {
                C = (AR >> (t1 - 1)) & 1;               /* C = last out */
                AR = (AR >> t1) & DMASK;                /* log right */
                }
            break;

        case 005:                                       /* ARS */
            if (t1 > 16)                                /* limit to 16 */
                t1 = 16;
            C = ((SEXT (AR)) >> (t1 - 1)) & 1;          /* C = last out */
            AR = ((SEXT (AR)) >> t1) & DMASK;           /* arith right */
            break; 

        case 006:                                       /* ARR */
            t2 = t1 % 16;                               /* mod 16 */
            AR = ((AR >> t2) | (AR << (16 - t2))) & DMASK;
            C = (AR >> 15) & 1;                         /* C = A<1> */
            break;

        case 007:                                       /* "short right arot" */
            if (reason = stop_inst)                     /* stop on undef? */
                break;
            for (t2 = 0; t2 < t1; t2++) {               /* bit by bit */
                C = AR & 1;                             /* C = last out */
                AR = ((AR & SIGN) | (C << 15)) | (AR >> 1);
                }
            break;

        case 010:                                       /* LLL */
            if (t1 > 32)                                /* > 32? all 0 */
                ut = 0;
            else {
                ut = GETDBL_U (AR, BR);                 /* get A'B */
                C = (ut >> (32 - t1)) & 1;              /* C = last out */
                if (t1 == 32)                           /* =32? all 0 */
                    ut = 0;
                else ut = ut << t1;                     /* log left */
                }
            PUTDBL_U (ut);                              /* store A,B */
            break;

        case 011:                                       /* LLS */
            if (t1 > 31)                                /* limit to 31 */
                t1 = 31;
            t2 = GETDBL_S (SEXT (AR), BR);              /* get A'B */
            t3 = t2 << t1;                              /* "arith" left */
            PUTDBL_S (t3);                              /* store A'B */
            if ((t2 >> (31 - t1)) !=                    /* shf out = sgn? */
                ((AR & SIGN)? -1: 0)) C = 1;
            break;

        case 012:                                       /* LLR */
            t2 = t1 % 32;                               /* mod 32 */
            ut = GETDBL_U (AR, BR);                     /* get A'B */
            ut = (ut << t2) | (ut >> (32 - t2));        /* rot left */
            C = ut & 1;                                 /* C = B<16> */
            PUTDBL_U (ut);                              /* store A,B */
            break;

        case 013:                                       /* "long left arot" */
            if (reason = stop_inst)                     /* stop on undef? */
                break;
            for (t2 = 0; t2 < t1; t2++) {               /* bit by bit */
                AR = (AR << 1) | ((BR >> 14) & 1);
                BR = (BR & SIGN) | ((BR << 1) & MMASK) |
                     ((AR >> 16) & 1);
                if ((AR & SIGN) != ((AR >> 1) & SIGN)) C = 1;
                AR = AR & DMASK;
                }
            break;

        case 014:                                       /* LGL */
            if (t1 > 16)                                /* > 16? all 0 */
                AR = 0;
            else {
                C = (AR >> (16 - t1)) & 1;              /* C = last out */
                AR = (AR << t1) & DMASK;                /* log left */
                }
            break;

        case 015:                                       /* ALS */
            if (t1 > 16)                                /* limit to 16 */
                t1 = 16;
            t2 = SEXT (AR);                             /* save AR */
            AR = (AR << t1) & DMASK;                    /* "arith" left */
            if ((t2 >> (16 - t1)) !=                    /* shf out + sgn */
                ((AR & SIGN)? -1: 0)) C = 1;
            break;

        case 016:                                       /* ALR */
            t2 = t1 % 16;                               /* mod 16 */
            AR = ((AR << t2) | (AR >> (16 - t2))) & DMASK;
            C = AR & 1;                                 /* C = A<16> */
            break;

        case 017:                                       /* "short left arot" */
            if (reason = stop_inst)                     /* stop on undef? */
                break;
            for (t2 = 0; t2 < t1; t2++) {               /* bit by bit */
                if ((AR & SIGN) != ((AR << 1) & SIGN)) C = 1;
                AR = ((AR << 1) | (AR >> 15)) & DMASK;
                }
            break;                                      /* end case fnc */
            }
        break;

/* Skip */

    case 040:
        skip = 0;
        if (((MB & 000001) && C) ||                     /* SSC */
            ((MB & 000002) && ss[3]) ||                 /* SS4 */
            ((MB & 000004) && ss[2]) ||                 /* SS3 */
            ((MB & 000010) && ss[1]) ||                 /* SS2 */
            ((MB & 000020) && ss[0]) ||                 /* SS1 */
            ((MB & 000040) && AR) ||                    /* SNZ */
            ((MB & 000100) && (AR & 1)) ||              /* SLN */
            ((MB & 000200) && (TST_INTREQ (INT_MPE))) || /* SPS */
            ((MB & 000400) && (AR & SIGN)))             /* SMI */
            skip = 1;
        if ((MB & 001000) == 0)                         /* reverse? */
            skip = skip ^ 1;
        PC = NEWA (PC, PC + skip);
        break;

/* Operate */

    case 060:
        if (MB == 0140024)                              /* CHS */
            AR = AR ^ SIGN;
        else if (MB == 0140040)                         /* CRA */
            AR = 0;
        else if (MB == 0140100)                         /* SSP */
            AR = AR & ~SIGN;
        else if (MB == 0140200)                         /* RCB */
            C = 0;
        else if (MB == 0140320) {                       /* CSA */
            C = (AR & SIGN) >> 15;
            AR = AR & ~SIGN;
            }
        else if (MB == 0140401)                         /* CMA */
            AR = AR ^ DMASK;
        else if (MB == 0140407) {                       /* TCA */
            AR = (-AR) & DMASK;
            sc = 0;
            }
        else if (MB == 0140500)                         /* SSM */
            AR = AR | SIGN;
        else if (MB == 0140600)                         /* SCB */
            C = 1;
        else if (MB == 0141044)                         /* CAR */
            AR = AR & 0177400;
        else if (MB == 0141050)                         /* CAL */
            AR = AR & 0377;
        else if (MB == 0141140)                         /* ICL */
            AR = AR >> 8;
        else if (MB == 0141206)                         /* AOA */
            AR = Add16 (AR, 1);
        else if (MB == 0141216)                         /* ACA */
            AR = Add16 (AR, C);
        else if (MB == 0141240)                         /* ICR */
            AR = (AR << 8) & DMASK;
        else if (MB == 0141340)                         /* ICA */
            AR = ((AR << 8) | (AR >> 8)) & DMASK;
        else if (reason = stop_inst)
            break;
        else AR = Operate (MB, AR);                     /* undefined */
        break;
        }                                               /* end case op */
    }                                                   /* end while */

saved_AR = AR & DMASK;
saved_BR = BR & DMASK;
saved_XR = XR & DMASK;
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}

/* Effective address

   The effective address calculation consists of three phases:
   - base address calculation: 0/pagenumber'displacement
   - (extend): indirect address resolution
     (non-extend): pre-indexing
   - (extend): post-indexing
     (non-extend): indirect address/post-indexing resolution
 
   In extend mode, address calculations are carried out to 16b
   and masked to 15b at exit.  In non-extend mode, address bits
   <1:2> are preserved by the NEWA macro; address bit <1> is
   masked at exit.
*/

t_stat Ea (int32 IR, int32 *addr)
{
int32 i;
int32 Y = IR & (IA | DISP);                             /* ind + disp */

if (IR & SC) Y = ((PC - 1) & PAGENO) | Y;               /* cur sec? + pageno */
if (ext) {                                              /* extend mode? */
    for (i = 0; (i < ind_max) && (Y & IA); i++) {       /* resolve ind addr */
        Y = Read (Y & X_AMASK);                         /* get ind addr */
        }
    if (IR & IDX) Y = Y + XR;                           /* post-index */
    }                                                   /* end if ext */
else {                                                  /* non-extend */
    Y = NEWA (PC, Y + ((IR & IDX)? XR: 0));             /* pre-index */
    for (i = 0; (i < ind_max) && (IR & IA); i++) {      /* resolve ind addr */
        IR = Read (Y & X_AMASK);                        /* get ind addr */
        Y = NEWA (Y, IR + ((IR & IDX)? XR: 0));         /* post-index */
        }
    }                                                   /* end else */
*addr = Y = Y & X_AMASK;                                /* return addr */
if (hst_lnt) {                                          /* history? */
    hst[hst_p].pc = hst[hst_p].pc | HIST_EA;
    hst[hst_p].ea = Y;
    hst[hst_p].opnd = Read (Y);
    }
if (i >= ind_max)
    return STOP_IND;                                    /* too many ind? */
return SCPE_OK;
}

/* Write memory */

void Write (int32 addr, int32 val)
{
if (((addr == 0) || (addr >= 020)) && MEM_ADDR_OK (addr))
    M[addr] = val;
return;
}

/* Add */

int32 Add16 (int32 v1, int32 v2)
{
int32 r = v1 + v2;

if (((v1 ^ ~v2) & (v1 ^ r)) & SIGN)
    C = 1;
else C = 0;
return (r & DMASK);
}

int32 Add31 (int32 v1, int32 v2)
{
int32 r = v1 + v2;

if (((v1 ^ ~v2) & (v1 ^ r)) & DP_SIGN)
    C = 1;
else C = 0;
return r;
}

/* Unimplemented I/O device */

int32 undio (int32 op, int32 fnc, int32 val, int32 dev)
{
return ((stop_dev << IOT_V_REASON) | val);
}

/* DMA control */

int32 dmaio (int32 inst, int32 fnc, int32 dat, int32 dev)
{
int32 ch = (fnc - 1) & 03;

switch (inst) {                                         /* case on opcode */

    case ioOCP:                                         /* OCP */
        if ((fnc >= 001) && (fnc <= 004)) {             /* load addr ctr */
            dma_ad[ch] = dat;
            dma_wc[ch] = 0;
            dma_eor[ch] = 0;
            }
        else if ((fnc >= 011) && (fnc <= 014))          /* load range ctr */
            dma_wc[ch] = (dma_wc[ch] | dat) & 077777;
        else return IOBADFNC (dat);                     /* undefined */
        break;

    case ioINA:                                         /* INA */
        if ((fnc >= 011) && (fnc <= 014)) {
            if (dma_eor[ch])                            /* end range? nop */
                return dat;
            return IOSKIP (0100000 | dma_wc[ch]);       /* return range */
            }
        else return IOBADFNC (dat);
        }

return dat;
}

/* Undefined operate instruction.  This code is reached when the
   opcode does not correspond to a standard operate instruction.
   It simulates the behavior of the actual logic.

   An operate instruction executes in 4 or 6 phases.  A 'normal'
   instruction takes 4 phases:

        t1              t1
        t2/tlate        t2/t2 extended into t3
        t3/tlate        t3
        t4              t4

   A '1.5 cycle' instruction takes 6 phases:

        t1              t1
        t2/tlate        t2/t2 extended into t3
        t3/tlate        t3
        t2/tlate        'special' t2/t2 extended into t3
        t3/tlate        t3
        t4              t4

   The key signals, by phase, are the following

        tlate   EASTL   enable A to sum leg 1 (else 0)
                        (((m12+m16)x!azzzz)+(m9+m11+azzzz)
                EASBM   enable 0 to sum leg 2 (else 177777)
                        (m9+m11+azzzz)
                JAMKN   jam carry network to 0 = force XOR
                        ((m12+m16)x!azzzz)
                EIKI7   force carry into adder
                        ((m15x(C+!m13))x!JAMKN)

        t3      CLDTR   set D to 177777 (always)
                ESDTS   enable adder sum to D (always)
                SETAZ   enable repeat cycle = set azzzz
                        (m8xm15)

   if azzzz {
        t2      CLATR   clear A register (due to azzzz)
                EDAHS   enable D high to A high register (due to azzzz)
                EDALS   enable D low to A low register (due to azzzz)
                
        tlate, t3 as above
        }

        t4      CLATR   clear A register
                        (m11+m15+m16)
                CLA1R   clear A1 register
                        (m10+m14)
                EDAHS   enable D high to A high register
                        ((m11xm14)+m15+m16)
                EDALS   enable D low to A low register
                        ((m11xm13)+m15+m16)
                ETAHS   enable D transposed to A high register
                        (m9xm11)
                ETALS   enable D transposed to A low register
                        (m10xm11)
                EDA1R   enable D1 to A1 register
                        ((m8xm10)+m14)
                CBITL   clear C, conditionally set C from adder output
                        (m9x!m11)
                CBITG   conditionally set C if D1
                        (m10xm12xD1)
                CBITE   unconditionally set C
                        (m8xm9)
*/

int32 Operate (int32 MB, int32 AR)
{
int32 D, jamkn, eiki7, easbm, eastl, setaz;
int32 clatr, cla1r, edahs, edals, etahs, etals, eda1r;
int32 cbitl, cbitg, cbite;
int32 aleg, bleg, ARx;

/* Phase tlate */

ARx = AR;                                               /* default */
jamkn = (MB & (m12+m16)) != 0;                          /* m12+m16 */
easbm = (MB & (m9+m11)) != 0;                           /* m9+m11 */
eastl = jamkn || easbm;                                 /* m9+m11+m12+m16 */
setaz = (MB & (m8+m15)) == (m8+m15);                    /* m8xm15*/
eiki7 = (MB & m15) && (C || !(MB & m13));               /* cin */
aleg = eastl? AR: 0;                                    /* a input */
bleg = easbm? 0: DMASK;                                 /* b input */
if (jamkn)                                              /* jammin? xor */
    D = aleg ^ bleg;
else D = (aleg + bleg + eiki7) & DMASK;                 /* else add */

/* Possible repeat at end of tlate - special t2, repeat tlate */

if (setaz) {
    ARx = D;                                            /* forced: t2 */
    aleg = ARx;                                         /* forced: tlate */
    bleg = 0;                                           /* forced */
    jamkn = 0;                                          /* forced */
    D = (aleg + bleg + eiki7) & DMASK;                  /* forced add */
    sc = 0;                                             /* ends repeat */
    }

/* Phase t4 */

clatr = (MB & (m11+m15+m16)) != 0;                      /* m11+m15+m16 */
cla1r = (MB & (m10+m14)) != 0;                          /* m10+m14 */
edahs = ((MB & (m11+m14)) == (m11+m14)) ||              /* (m11xm14)+m15+m16 */
    (MB & (m15+m16));
edals = ((MB & (m11+m13)) == (m11+m13)) ||              /* (m11xm13)+m15+m16 */
    (MB & (m15+m16));
etahs = (MB & (m9+m11)) == (m9+m11);                    /* m9xm11 */
etals = (MB & (m10+m11)) == (m10+m11);                  /* m10xm11 */
eda1r = ((MB & (m8+m10)) == (m8+m10)) || (MB & m14);    /* (m8xm10)+m14 */
cbitl = (MB & (m9+m11)) == m9;                          /* m9x!m11 */
cbite = (MB & (m8+m9)) == (m8+m9);                      /* m8xm9 */
cbitg = (MB & (m10+m12)) == (m10+m12);                  /* m10xm12 */

if (clatr)                                              /* clear A */
    ARx = 0;
if (cla1r)                                              /* clear A1 */
    ARx = ARx & ~SIGN;
if (edahs)                                              /* D hi to A hi */
    ARx = ARx | (D & 0177400);
if (edals)                                              /* D lo to A lo */
    ARx = ARx | (D & 0000377);
if (etahs)                                              /* D lo to A hi */
    ARx = ARx | ((D << 8) & 0177400);
if (etals)                                              /* D hi to A lo */
    ARx = ARx | ((D >> 8) & 0000377);
if (eda1r)                                              /* D1 to A1 */
    ARx = ARx | (D & SIGN);
if (cbitl) {                                            /* ovflo to C */

/* Overflow calculation.  Cases:

        aleg    bleg    cin     overflow

        0       x       x       can't overflow
        A       0       0       can't overflow
        A       -1      1       can't overflow
        A       0       1       overflow if 77777->100000
        A       -1      0       overflow if 100000->77777
*/

    if (!jamkn &&
        ((bleg && !eiki7 && (D == 0077777)) ||
        (!bleg && eiki7 && (D == 0100000))))
        C = 1;
    else C = 0;
    }
if (cbite || (cbitg && (D & SIGN)))                     /* C = 1 */
    C = 1;
return ARx;
}

/* Reset routines */

t_stat cpu_reset (DEVICE *dptr)
{
int32 i;

saved_AR = saved_BR = saved_XR = 0;
C = 0;
dp = 0;
ext = pme = extoff_pending = 0;
dev_int = dev_int & ~(INT_PEND|INT_NMI);
dev_enb = 0;
for (i = 0; i < DMA_MAX; i++)
    dma_ad[i] = dma_wc[i] = dma_eor[i] = 0;
chan_req = 0;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 d;

if (addr >= MEMSIZE)
    return SCPE_NXM;
if (addr == 0)
    d = saved_XR;
else d = M[addr];
if (vptr != NULL)
    *vptr = d & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (addr == 0)
    saved_XR = val & DMASK;
else M[addr] = val & DMASK;
return SCPE_OK;
}

/* Option processors */

t_stat cpu_set_noext (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (MEMSIZE > (NX_AMASK + 1))
    return SCPE_ARG;
return SCPE_OK;
}

t_stat cpu_set_size (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0) ||
    (((cpu_unit.flags & UNIT_EXT) == 0) && (val > (NX_AMASK + 1))))
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

t_stat cpu_set_nchan (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 i, newmax;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
newmax = get_uint (cptr, 10, DMA_MAX, &r);              /* get new max */
if ((r != SCPE_OK) || (newmax == dma_nch))              /* err or no chg? */
    return r;
dma_nch = newmax;                                       /* set new max */
for (i = newmax; i < DMA_MAX; i++) {                    /* reset chan */
    dma_ad[i] = dma_wc[i] = dma_eor[i] = 0;
    chan_req = chan_req & ~(1 << i);
    }
return SCPE_OK;
}

/* Show DMA channels */

t_stat cpu_show_nchan (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if (dma_nch)
    fprintf (st, "DMA channels = %d", dma_nch);
else fprintf (st, "no DMA channels");
return SCPE_OK;
}

/* Show channel state */

t_stat cpu_show_dma (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if ((val < 0) || (val >= DMA_MAX))
    return SCPE_IERR;
fputs ((dma_ad[val] & DMA_IN)? "Input": "Output", st);
fprintf (st, ", addr = %06o, count = %06o, ", dma_ad[val] & X_AMASK, dma_wc[val]);
fprintf (st, "end of range %s\n", (dma_eor[val]? "set": "clear"));
return SCPE_OK;
}

/* Set I/O device to IOBUS / DMA channel / DMC channel */

t_stat io_set_iobus (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (val || cptr || (uptr == NULL))
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
dibp->chan = 0;
return SCPE_OK;
}

t_stat io_set_dma (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newc;
t_stat r;

if ((cptr == NULL) || (uptr == NULL))
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
if (dma_nch == 0)
    return SCPE_NOFNC;
newc = get_uint (cptr, 10, DMA_MAX, &r);                /* get new */
if ((r != SCPE_OK) || (newc == 0) || (newc > dma_nch))
    return SCPE_ARG;
dibp->chan = (newc - DMA_MIN) + DMA_V_DMA1 + 1;         /* store */
return SCPE_OK;
}

t_stat io_set_dmc (UNIT *uptr, int32 val, char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newc;
t_stat r;

if ((cptr == NULL) || (uptr == NULL))
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
if (!(cpu_unit.flags & UNIT_DMC))
    return SCPE_NOFNC;
newc = get_uint (cptr, 10, DMC_MAX, &r);                /* get new */
if ((r != SCPE_OK) || (newc == 0))
    return SCPE_ARG;
dibp->chan = (newc - DMC_MIN) + DMC_V_DMC1 + 1;         /* store */
return SCPE_OK;
}

/* Show channel configuration */

t_stat io_show_chan (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
if (dibp->chan == 0)
    fprintf (st, "IO bus");
else if (dibp->chan < (DMC_V_DMC1 + 1))
    fprintf (st, "DMA channel %d", dibp->chan);
else fprintf (st, "DMC channel %d", dibp->chan - DMC_V_DMC1);
return SCPE_OK;
}

/* Set up I/O dispatch and channel maps */

t_bool devtab_init (void)
{
DEVICE *dptr;
DIB *dibp;
uint32 i, j, dno, chan;

for (i = 0; i < DEV_MAX; i++)
    iotab[i] = NULL;
for (i = 0; i < (DMA_MAX + DMC_MAX); i++)
    chan_map[i] = 0;
for (i = 0; dptr = sim_devices[i]; i++) {               /* loop thru devices */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if ((dibp == NULL) || (dptr->flags & DEV_DIS))      /* exist, enabled? */
        continue;
    dno = dibp->dev;                                    /* device number */
    for (j = 0; j < dibp->num; j++) {                   /* repeat for slots */
        if (iotab[dno + j]) {                           /* conflict? */
            printf ("%s device number conflict, devno = %02o\n",
                    sim_dname (dptr), dno + j);
            if (sim_log)
                fprintf (sim_log, "%s device number conflict, devno = %02o\n",
                         sim_dname (dptr), dno + j);
            return TRUE;
            }
        iotab[dno + j] = dibp->io;                      /* set I/O routine */
        }                                               /* end for */
    if (dibp->chan) {                                   /* DMA/DMC? */
        chan = dibp->chan - 1;
        if ((chan < DMC_V_DMC1) && (chan >= dma_nch)) {
            printf ("%s configured for DMA channel %d\n",
                    sim_dname (dptr), chan + 1);
            if (sim_log)
                fprintf (sim_log, "%s configured for DMA channel %d\n",
                         sim_dname (dptr), chan + 1);
            return TRUE;
            }
        if ((chan >= DMC_V_DMC1) && !(cpu_unit.flags & UNIT_DMC)) {
            printf ("%s configured for DMC, option disabled\n",
                     sim_dname (dptr));
            if (sim_log)
                fprintf (sim_log, "%s configured for DMC, option disabled\n",
                         sim_dname (dptr));
            return TRUE;
            }
        if (chan_map[chan]) {                           /* channel conflict? */
            printf ("%s DMA/DMC channel conflict, devno = %02o\n",
                    sim_dname (dptr), dno);
            if (sim_log)
                fprintf (sim_log, "%s DMA/DMC channel conflict, devno = %02o\n",
                         sim_dname (dptr), dno);
            return TRUE;
            }
        chan_map[chan] = dno;                           /* channel back map */
        }
    }                                                   /* end for */
for (i = 0; i < DEV_MAX; i++) {                         /* fill in blanks */
    if (iotab[i] == NULL)
        iotab[i] = &undio;
    }
return FALSE;
}

/* Set history */

t_stat cpu_set_hist (UNIT *uptr, int32 val, char *cptr, void *desc)
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

t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, void *desc)
{
int32 cr, k, di, op, lnt;
char *cptr = (char *) desc;
t_value sim_eval;
t_stat r;
InstHistory *h;
extern t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw);
static uint8 has_opnd[16] = {
    0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1
    };

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
fprintf (st, "PC     C A       B       X       ea     IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        cr = (h->pc & HIST_C)? 1: 0;                    /* carry */
        fprintf (st, "%05o  %o %06o  %06o  %06o  ",
            h->pc & X_AMASK, cr, h->ar, h->br, h->xr);
        if (h->pc & HIST_EA)
            fprintf (st, "%05o  ", h->ea);
        else fprintf (st, "       ");
        sim_eval = h->ir;
        if ((fprint_sym (st, h->pc & X_AMASK, &sim_eval,
            &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "(undefined) %06o", h->ir);
        op = I_GETOP (h->ir) & 017;                     /* base op */
        if (has_opnd[op])
            fprintf (st, "  [%06o]", h->opnd);
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}
