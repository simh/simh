/* pdp1_cpu.c: PDP-1 CPU simulator

   Copyright (c) 1993-2015, Robert M. Supnik

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

   cpu          PDP-1 central processor

   27-Mar-15    RMS     Backported changed from GitHub master
   21-Mar-12    RMS     Fixed & vs && in Ea_ch (Michael Bloom)
   30-May-07    RMS     Fixed typo in SBS clear (Norm Lastovica)
   28-Dec-06    RMS     Added 16-channel SBS support, PDP-1D support
   28-Jun-06    RMS     Fixed bugs in MUS and DIV
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   09-Nov-04    RMS     Added instruction history
   08-Feb-04    PLB     Added display device spacewar/test switches
   07-Sep-03    RMS     Added additional explanation on I/O simulation
   01-Sep-03    RMS     Added address switches for hardware readin
   23-Jul-03    RMS     Revised to detect I/O wait hang
   05-Dec-02    RMS     Added drum support
   06-Oct-02    RMS     Revised for V2.10
   20-Aug-02    RMS     Added DECtape support
   30-Dec-01    RMS     Added old PC queue
   07-Dec-01    RMS     Revised to use breakpoint package
   30-Nov-01    RMS     Added extended SET/SHOW support
   16-Dec-00    RMS     Fixed bug in XCT address calculation
   14-Apr-99    RMS     Changed t_addr to unsigned

   The PDP-1 was Digital's first computer.  Although Digital built four
   other 18b computers, the later systems (the PDP-4, PDP-7, PDP-9, and
   PDP-15) were similar to each other and quite different from the PDP-1.
   Accordingly, the PDP-1 requires a distinct simulator.

   The register state for the PDP-1 is:

        AC<0:17>        accumulator
        IO<0:17>        IO register
        OV              overflow flag
        PC<0:15>        program counter
        IOSTA           I/O status register
        SBS<0:2>        sequence break flip flops
        IOH             I/O halt flip flop
        IOS             I/O synchronizer (completion) flip flop
        EXTM            extend mode
        PF<1:6>         program flags
        SS<1:6>         sense switches
        TW<0:17>        test word (switch register)

   The 16-channel sequence break system adds additional state:

        sbs_req<0:15>   interrupt requests
        sbs_enb<0:15>   enabled levels
        sbs_act<0:15>   active levels

   The PDP-1D adds additional state:

        L               link (SN 45 only)
        RNG             ring mode
        RM              restrict mode
        RMASK           restrict mode mask
        RNAME           rename table (SN 45 only)
        RTB             restict mode trap buffer (SN 45 only)

   Questions:

        cks: which bits are line printer print done and space done?
        cks: is there a bit for sequence break enabled (yes, according
             to the 1963 Handbook)
        sbs: do sequence breaks accumulate while the system is disabled
             (yes, according to the Maintenance Manual)

   The PDP-1 has seven instruction formats: memory reference, skips,
   shifts, load immediate, I/O transfer, operate, and (PDP-1D) special.
   The memory reference format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |      op      |in|              address              | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4> <5>    mnemonic        action

   00
   02           AND             AC = AC & M[MA]
   04           IOR             AC = AC | M[MA]
   06           XOR             AC = AC ^ M[MA]
   10           XCT             M[MA] is executed as an instruction
   12           LCH             load character (PDP-1D)
   14           DCH             store character (PDP-1D)
   16     0     CAL             M[100] = AC, AC = PC, PC = 101
   16     1     JDA             M[MA] = AC, AC = PC, PC = MA + 1
   20           LAC             AC = M[MA]
   22           LIO             IO = M[MA]
   24           DAC             M[MA] = AC
   26           DAP             M[MA]<6:17> = AC<6:17>
   30           DIP             M[MA]<0:5> = AC<0:5>
   32           DIO             M[MA] = IO
   34           DZM             M[MA] = 0
   36           TAD             L'AC = AC + M[MA] + L
   40           ADD             AC = AC + M[MA]
   42           SUB             AC = AC - M[MA]
   44           IDX             AC = M[MA] = M[MA] + 1
   46           ISP             AC = M[MA] = M[MA] + 1, skip if AC >= 0
   50           SAD             skip if AC != M[MA]
   52           SAS             skip if AC == M[MA]
   54           MUL             AC'IO = AC * M[MA]
   56           DIV             AC, IO = AC'IO / M[MA]
   60           JMP             PC = MA
   62           JSP             AC = PC, PC = MA

   Memory reference instructions can access an address space of 64K words.
   The address space is divided into sixteen 4K word fields.  An
   instruction can directly address, via its 12b address, the entire
   current field.  If extend mode is off, indirect addresses access
   the current field, and indirect addressing is multi-level; if off,
   they can access all 64K, and indirect addressing is single level.

   The skip format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  0  1  0|  |  |  |  |  |  |  |  |  |  |  |  |  | skip
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                    |  |  |  |  |  |  | \______/ \______/
                    |  |  |  |  |  |  |     |        |
                    |  |  |  |  |  |  |     |        +---- program flags
                    |  |  |  |  |  |  |     +------------- sense switches
                    |  |  |  |  |  |  +------------------- AC == 0
                    |  |  |  |  |  +---------------------- AC >= 0
                    |  |  |  |  +------------------------- AC < 0
                    |  |  |  +---------------------------- OV == 0
                    |  |  +------------------------------- IO >= 0
                    |  +---------------------------------- IO != 0 (PDP-1D)
                    +------------------------------------- invert skip

   The shift format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  0  1  1| subopcode |      encoded count       | shift
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The load immediate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  0| S|           immediate               | LAW
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4>        mnemonic        action

   70           LAW             if S = 0, AC = IR<6:17>
                                else AC = ~IR<6:17>

   The I/O transfer format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  1| W| C|   subopcode  |      device     | I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IO transfer instruction sends the the specified subopcode to
   specified I/O device.  The I/O device may take data from the IO or
   return data to the IO, initiate or cancel operations, etc.  The
   W bit specifies whether the CPU waits for completion, the C bit
   whether a completion pulse will be returned from the device.

   The special operate format (PDP-1D) is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  1  0|  |  |  |  |  |  |  |  |  |  |  |  |  | special
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                    |  |  |  |  |  |  |  |  |  |  |
                    |  |  |  |  |  |  |  |  |  |  +------- CML (3)
                    |  |  |  |  |  |  |  |  |  +---------- CLL (1)
                    |  |  |  |  |  |  |  |  +------------- SZL (1)
                    |  |  |  |  |  |  |  +---------------- SCF (1)
                    |  |  |  |  |  |  +------------------- SCI (1)
                    |  |  |  |  |  +---------------------- SCM (2)
                    |  |  |  |  +------------------------- IDA (3)
                    |  |  |  +---------------------------- IDC (4)
                    |  |  +------------------------------- IFI (2)
                    |  +---------------------------------- IIF (2)
                    +------------------------------------- reverse skip

   The special operate instruction can be microprogrammed.

   The standard operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  1  1|  |  |  |  |  |  |  |  |  |  |  |  |  | operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                    |  |  |  |  |  |  |  |  |  | \______/
                    |  |  |  |  |  |  |  |  |  |     |
                    |  |  |  |  |  |  |  |  |  |     +---- PF select
                    |  |  |  |  |  |  |  |  |  +---------- clear/set PF
                    |  |  |  |  |  |  |  |  +------------- LIA (PDP-1D)
                    |  |  |  |  |  |  |  +---------------- LAI (PDP-1D)
                    |  |  |  |  |  |  +------------------- or PC
                    |  |  |  |  |  +---------------------- CLA
                    |  |  |  |  +------------------------- halt
                    |  |  |  +---------------------------- CMA
                    |  |  +------------------------------- or TW
                    |  +---------------------------------- CLI
                    +------------------------------------- CMI (PDP-1D)

   The standard operate instruction can be microprogrammed.

   This routine is the instruction decode routine for the PDP-1.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        unimplemented instruction and STOP_INST flag set
        XCT loop
        indirect address loop
        infinite wait state
        I/O error in I/O simulator

   2. Interrupts.  With a single channel sequence break system, the
      PDP-1 has a single break request (flop b2, here sbs<SB_V_RQ>).
      If sequence breaks are enabled (flop sbm, here sbs<SB_V_ON>),
      and one is not already in progress (flop b4, here sbs<SB_V_IP>),
      a sequence break occurs.  With a 16-channel sequence break
      system, the PDP-1 has 16 request flops (sbs_req), 16 enable
      flops (sbs_enb), and 16 active flops (sbs_act).  It also has
      16 synchronizer flops, which are not needed in simulation.

   3. Arithmetic.  The PDP-1 is a 1's complement system.  In 1's
      complement arithmetic, a negative number is represented by the
      complement (XOR 0777777) of its absolute value.  Addition of 1's
      complement numbers requires propagating the carry out of the high
      order bit back to the low order bit.

   4. Adding I/O devices.  Three modules must be modified:

        pdp1_defs.h     add interrupt request definition
        pdp1_cpu.c      add IOT dispatch code
        pdp1_sys.c      add sim_devices table entry
*/

#include "pdp1_defs.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC
#define UNIT_V_MDV      (UNIT_V_UF + 0)                 /* mul/div */
#define UNIT_V_SBS      (UNIT_V_UF + 1)
#define UNIT_V_1D       (UNIT_V_UF + 2)
#define UNIT_V_1D45     (UNIT_V_UF + 3)
#define UNIT_V_MSIZE    (UNIT_V_UF + 4)                 /* dummy mask */
#define UNIT_MDV        (1 << UNIT_V_MDV)
#define UNIT_SBS        (1 << UNIT_V_SBS)
#define UNIT_1D         (1 << UNIT_V_1D)
#define UNIT_1D45       (1 << UNIT_V_1D45)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

#define HIST_PC         0x40000000
#define HIST_V_SHF      18
#define HIST_MIN        64
#define HIST_MAX        65536

#define MA_GETBNK(x)    ((cpu_unit.flags & UNIT_1D45)? \
                         (((x) >> RM45_V_BNK) & RM45_M_BNK): \
                         (((x) >> RM48_V_BNK) & RM48_M_BNK))

typedef struct {
    uint32              pc;
    uint32              ir;
    uint32              ovac;
    uint32              pfio;
    uint32              ea;
    uint32              opnd;
    } InstHistory;

int32 M[MAXMEMSIZE] = { 0 };                            /* memory */
int32 AC = 0;                                           /* AC */
int32 IO = 0;                                           /* IO */
int32 PC = 0;                                           /* PC */
int32 MA = 0;                                           /* MA */
int32 MB = 0;                                           /* MB */
int32 OV = 0;                                           /* overflow */
int32 SS = 0;                                           /* sense switches */
int32 PF = 0;                                           /* program flags */
int32 TA = 0;                                           /* address switches */
int32 TW = 0;                                           /* test word */
int32 iosta = 0;                                        /* status reg */
int32 sbs = 0;                                          /* sequence break */
int32 sbs_init = 0;                                     /* seq break start */
int32 ioh = 0;                                          /* I/O halt */
int32 ios = 0;                                          /* I/O syncronizer */
int32 cpls = 0;                                         /* pending compl */
int32 sbs_req = 0;                                      /* sbs requests */
int32 sbs_enb = 0;                                      /* sbs enabled */
int32 sbs_act = 0;                                      /* sbs active */
int32 extm = 0;                                         /* ext mem mode */
int32 rm = 0;                                           /* restrict mode */
int32 rmask = 0;                                        /* restrict mask */
int32 rname[RN45_SIZE];                                 /* rename table */
int32 rtb = 0;                                          /* restr trap buf */
int32 extm_init = 0;                                    /* ext mem startup */
int32 stop_inst = 0;                                    /* stop rsrv inst */
int32 xct_max = 16;                                     /* XCT limit */
int32 ind_max = 16;                                     /* ind limit */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* inst history */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_1d (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat Ea (int32 IR);
t_stat Ea_ch (int32 IR, int32 *byte_num);
int32 inc_bp (int32 bp);
t_stat set_rmv (int32 code);
int32 sbs_eval (void);
int32 sbs_ffo (int32 mask);
t_stat Read (void);
t_stat Write (void);

extern int32 ptr (int32 inst, int32 dev, int32 dat);
extern int32 ptp (int32 inst, int32 dev, int32 dat);
extern int32 tti (int32 inst, int32 dev, int32 dat);
extern int32 tto (int32 inst, int32 dev, int32 dat);
extern int32 lpt (int32 inst, int32 dev, int32 dat);
extern int32 dt (int32 inst, int32 dev, int32 dat);
extern int32 drm (int32 inst, int32 dev, int32 dat);
extern int32 clk (int32 inst, int32 dev, int32 dat);
extern int32 dcs (int32 inst, int32 dev, int32 dat);
#ifdef USE_DISPLAY
extern int32 dpy (int32 inst, int32 dev, int32 dat, int32 dat2);
extern int32 spacewar (int32 inst, int32 dev, int32 dat);
#endif

const int32 sc_map[512] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,     /* 00000xxxx */
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,     /* 00001xxxx */
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,     /* 00010xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 00011xxxx */
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,     /* 00100xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 00101xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 00110xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 00111xxxx */
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,     /* 01000xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 01001xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 01010xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 01011xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 01100xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 01101xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 01110xxxx */
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,     /* 01111xxxx */
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,     /* 10000xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 10001xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 10010xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 10011xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 10100xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 10101xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 10110xxxx */
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,     /* 11011xxxx */
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,     /* 11000xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 11001xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 11010xxxx */
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,     /* 11011xxxx */
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,     /* 11100xxxx */
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,     /* 11101xxxx */
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8,     /* 11110xxxx */
    5, 6, 6, 7, 6, 7, 7, 8, 6, 7, 7, 8, 7, 8, 8, 9      /* 11111xxxx */
    };

const int32 ffo_map[256] = {
    8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

const int32 byt_shf[4] = { 0, 0, 6, 12 };

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATAD (PC, PC, ASIZE, "program counter") },
    { ORDATAD (AC, AC, 18, "accumulator") },
    { ORDATAD (IO, IO, 18, "I/O register") },
    { ORDATA (MA, MA, 16) },
    { ORDATA (MB, MB, 18) },
    { FLDATAD (OV, OV, 0, "overflow flag") },
    { ORDATAD (PF, PF, 8, "programs flags <1:6>") },
    { ORDATAD (SS, SS, 6, "sense switches <1:6>") },
    { ORDATAD (TA, TA, ASIZE, "address switches") },
    { ORDATAD (TW, TW, 18, "test word (front panel switches)") },
    { FLDATAD (EXTM, extm, 0, "extend mode") },
    { FLDATAD (RNGM, PF, PF_V_RNG, "ring mode (PDP-1D only)") },
    { FLDATAD (L, PF, PF_V_L, "link (PDP-1D #45 only)") },
    { FLDATAD (RM, rm, 0, "restrict mode (PDP-1D)") },
    { ORDATAD (RMASK, rmask, 18, "restrict memory mask (PDP-1D)") },
    { ORDATAD (RTB, rtb, 18, "restrict trap buffer (PDP-1D #45 only)") },
    { BRDATAD (RNAME, rname, 8, 2, RN45_SIZE, "rename map (PDP-1D #45 only)") },
    { FLDATAD (SBON, sbs, SB_V_ON, "sequence break enable") },
    { FLDATAD (SBRQ, sbs, SB_V_RQ, "sequence break request") },
    { FLDATAD (SBIP, sbs, SB_V_IP, "sequence break in progress") },
    { ORDATAD (SBSREQ, sbs_req, 16, "pending sequence break requests") },
    { ORDATAD (SBSENB, sbs_enb, 16, "enabled sequence break levels") },
    { ORDATAD (SBSACT, sbs_act, 16, "active sequence break levels") },
    { ORDATAD (IOSTA, iosta, 18, "I/O status register"), REG_RO },
    { ORDATA (CPLS, cpls, 6) },
    { FLDATAD (IOH, ioh, 0, "I/O halt in progress") },
    { FLDATAD (IOS, ios, 0, "I/O synchronizer (completion)") },
    { BRDATAD (PCQ, pcq, 8, ASIZE, PCQ_SIZE, "PC prior to last jump or interrupt; most recent PC change first"), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { FLDATAD (STOP_INST, stop_inst, 0, "stop on undefined instruction") },
    { FLDATAD (SBS_INIT, sbs_init, SB_V_ON, "initial state of sequence break enable") },
    { FLDATAD (EXTM_INIT, extm_init, 0, "initial state of extend mode") },
    { DRDATAD (XCT_MAX, xct_max, 8, "maximum XCT chain"), PV_LEFT + REG_NZ },
    { DRDATAD (IND_MAX, ind_max, 8, "maximum nested indirect addresses"), PV_LEFT + REG_NZ },
    { ORDATAD (WRU, sim_int_char, 8, "interrupt character") },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_1D+UNIT_1D45, 0, "standard CPU", "PDP1C" },
    { UNIT_1D+UNIT_1D45, UNIT_1D, "PDP-1D #48", "PDP1D48", &cpu_set_1d },
    { UNIT_1D+UNIT_1D45, UNIT_1D+UNIT_1D45, "PDP1D #45", "PDP1D45", &cpu_set_1d },
    { UNIT_MDV, UNIT_MDV, "multiply/divide", "MDV", NULL },
    { UNIT_MDV, 0, "no multiply/divide", "NOMDV", NULL },
    { UNIT_SBS, UNIT_SBS, "SBS", "SBS", NULL },
    { UNIT_SBS, 0, "no SBS", "NOSBS", NULL },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
    { UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size },
    { UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, ASIZE, 1, 8, 18,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

t_stat sim_instr (void)
{
int32 IR, op, i, t, xct_count;
int32 sign, signd, v, sbs_lvl, byno;
int32 dev, pulse, io_data, sc, skip;
t_stat reason;
static int32 fs_test[8] = {
    0,       PF_SS_1, PF_SS_2, PF_SS_3,
    PF_SS_4, PF_SS_5, PF_SS_6, PF_SS_ALL
    };

#define EPC_WORD        ((OV << 17) | (extm << 16) | PC)
#define INCR_ADDR(x)    (((x) & EPCMASK) | (((x) + 1) & DAMASK))
#define DECR_ADDR(x)    (((x) & EPCMASK) | (((x) - 1) & DAMASK))
#define ABS(x)          ((x) ^ (((x) & SIGN)? DMASK: 0))

if (cpu_unit.flags & UNIT_1D) {                         /* PDP-1D? */
    cpu_unit.flags |= UNIT_SBS|UNIT_MDV;                /* 16-chan SBS, mdv */
    if (!(cpu_unit.flags & UNIT_1D45)) {                /* SN 48? */
        PF &= ~PF_L;                                    /* no link */
        rtb = 0;                                        /* no RTB */
        for (i = 0; i < RN45_SIZE; i++)                 /* no rename */
            rname[i] = i;
        }
    }
else {                                                  /* standard PDP-1 */
    PF &= ~(PF_L|PF_RNG);                               /* no link, ring */
    rm = 0;                                             /* no restrict mode */
    rtb = 0;                                            /* no RTB */
    for (i = 0; i < RN45_SIZE; i++)                     /* no rename */
        rname[i] = i;
    }
if (cpu_unit.flags & UNIT_SBS) {                        /* 16-chan SBS? */
    sbs = sbs & SB_ON;                                  /* yes, only SB ON */
    sbs_lvl = sbs_eval ();                              /* eval SBS system */
    }
else sbs_lvl = sbs_req = sbs_enb = sbs_act = 0;         /* no, clr SBS sys */

/* Main instruction fetch/decode loop: check events and interrupts */

reason = 0;
while (reason == 0) {                                   /* loop until halted */

    if (sim_interval <= 0) {                            /* check clock queue */
        if ((reason = sim_process_event ()))
            break;
        sbs_lvl = sbs_eval ();                          /* eval sbs system */
        }

    if ((cpu_unit.flags & UNIT_SBS)?                    /* test interrupt */
        ((sbs & SB_ON) && sbs_lvl):                     /* 16-chan SBS? */
        (sbs == (SB_ON | SB_RQ))) {                     /* 1-chan SBS? */
        if (cpu_unit.flags & UNIT_SBS) {                /* 16-chan intr */
            int32 lvl = sbs_lvl - 1;                    /* get level */
            MA = lvl << 2;                              /* status block */
            sbs_req &= ~SBS_MASK (lvl);                 /* clr lvl request */
            sbs_act |= SBS_MASK (lvl);                  /* set lvl active */
            sbs_lvl = sbs_eval ();                      /* re-eval SBS */
            }
        else {                                          /* 1-chan intr */
            MA = 0;                                     /* always level 0 */
            sbs = SB_ON | SB_IP;                        /* set in prog flag */
            }
        PCQ_ENTRY;                                      /* save old PC */
        MB = AC;                                        /* save AC */
        Write ();
        MA = MA + 1;
        MB = EPC_WORD;                                  /* save OV'EXT'PC */
        Write ();
        MA = MA + 1;
        MB = IO;                                        /* save IO */
        Write ();
        PC = MA + 1;                                    /* PC = block + 3 */
        extm = 0;                                       /* extend off */
        OV = 0;                                         /* clear overflow */
        }

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

/* Fetch, decode instruction */

    MA = PC;
    if (Read ())                                        /* fetch inst */
        break;
    IR = MB;                                            /* save in IR */
    PC = INCR_ADDR (PC);                                /* increment PC */
    xct_count = 0;                                      /* track XCT's */
    sim_interval = sim_interval - 1;
    if (hst_lnt) {                                      /* history enabled? */
        hst_p = (hst_p + 1);                            /* next entry */
        if (hst_p >= hst_lnt)
            hst_p = 0;
        hst[hst_p].pc = MA | HIST_PC;                   /* save state */
        hst[hst_p].ir = IR;
        hst[hst_p].ovac = (OV << HIST_V_SHF) | AC;
        hst[hst_p].pfio = (PF << HIST_V_SHF) | IO;
        }

    xct_instr:                                          /* label for XCT */
    op = ((IR >> 13) & 037);                            /* get opcode */
    switch (op) {                                       /* decode IR<0:4> */

/* Logical, load, store instructions */

    case 001:                                           /* AND */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        AC = AC & MB;
        break;

    case 002:                                           /* IOR */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        AC = AC | MB;
        break;

    case 003:                                           /* XOR */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        AC = AC ^ MB;
        break;

    case 004:                                           /* XCT */
        if (xct_count >= xct_max) {                     /* too many XCT's? */
            reason = STOP_XCT;
            break;
            }
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        xct_count = xct_count + 1;                      /* count XCT's */
        IR = MB;                                        /* get instruction */
        goto xct_instr;                                 /* go execute */

    case 005:                                           /* LCH */
        if (cpu_unit.flags & UNIT_1D) {                 /* PDP-1D? */
            if ((reason = Ea_ch (IR, &byno)))           /* MA <- eff addr */
                break;
            if ((reason = Read ()))                     /* MB <- data */
                break;
            AC = (MB << byt_shf[byno]) & 0770000;       /* extract byte */
            }
        else reason = stop_inst;                        /* no, illegal */
        break;

    case 006:                                           /* DCH */
        if (cpu_unit.flags & UNIT_1D) {                 /* PDP-1D? */
            if ((reason = Ea_ch (IR, &byno)))           /* MA <- eff addr */
                break;
            if ((reason = Read ()))                     /* MB <- data */
                break;
            MB = (MB & ~(0770000 >> byt_shf[byno])) |   /* insert byte */
                ((AC & 0770000) >> byt_shf[byno]);
            Write ();                                   /* rewrite */
            AC = ((AC << 6) | (AC >> 12)) & DMASK;      /* rot AC left 6 */
            }
        else reason = stop_inst;                        /* no, illegal */
        break;

    case 007:                                           /* CAL, JDA */
        MA = (PC & EPCMASK) | ((IR & IA)? (IR & DAMASK): 0100);
        if (hst_p)                                      /* history enabled? */
            hst[hst_p].ea = MA;
        PCQ_ENTRY;
        MB = AC;                                        /* save AC */
        AC = EPC_WORD;
        PC = INCR_ADDR (MA);
        reason = Write ();
        break;

    case 010:                                           /* LAC */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        AC = MB;
        break;

    case 011:                                           /* LIO */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        IO = MB;
        break;

    case 012:                                           /* DAC */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        MB = AC;
        reason = Write ();
        break;

    case 013:                                           /* DAP */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        MB = (AC & DAMASK) | (MB & ~DAMASK);
        reason = Write ();
        break;

    case 014:                                           /* DIP */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        MB = (AC & ~DAMASK) | (MB & DAMASK);
        reason = Write ();
        break;

    case 015:                                           /* DIO */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        MB = IO;
        reason = Write ();
        break;

    case 016:                                           /* DZM */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        MB = 0;
        reason = Write ();
        break;

/* Add, subtract, control

   Add is performed in sequential steps, as follows:
        1. add
        2. end around carry propagate
        3. overflow check
        4. -0 cleanup

   Subtract is performed in sequential steps, as follows:
        1. complement AC
        2. add
        3. end around carry propagate
        4. overflow check
        5. complement AC
   Because no -0 check is done, (-0) - (+0) yields a result of -0 */

    case 017:                                           /* TAD */
        if (cpu_unit.flags & UNIT_1D) {                 /* PDP-1D? */
            if ((reason = Ea (IR)))                     /* MA <- eff addr */
                break;
            if ((reason = Read ()))                     /* MB <- data */
                break;
            AC = AC + MB + ((PF & PF_L)? 1: 0);         /* AC + opnd + L */
            if (AC > DMASK)                             /* carry? set L */
                PF = PF | PF_L;
            else PF = PF & ~PF_L;                       /* no, clear L */
            AC = AC & DMASK;                            /* mask AC */
            }
        else reason = stop_inst;                        /* no, illegal */
        break;

    case 020:                                           /* ADD */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        t = AC;
        AC = AC + MB;
        if (AC > 0777777)                               /* end around carry */
            AC = (AC + 1) & DMASK;
        if (((~t ^ MB) & (t ^ AC)) & SIGN)
            OV = 1;
        if (AC == DMASK)                                /* minus 0 cleanup */
            AC = 0;
        break;

    case 021:                                           /* SUB */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        t = AC ^ DMASK;                                 /* complement AC */
        AC = t + MB;                                    /* -AC + MB */
        if (AC > DMASK)                                 /* end around carry */
            AC = (AC + 1) & DMASK;
        if (((~t ^ MB) & (t ^ AC)) & SIGN)
            OV = 1;
        AC = AC ^ DMASK;                                /* recomplement AC */
        break;

    case 022:                                           /* IDX */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        AC = MB + 1;
        if (AC >= DMASK)
            AC = (AC + 1) & DMASK;
        MB = AC;
        reason = Write ();
        break;

    case 023:                                           /* ISP */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        AC = MB + 1;
        if (AC >= DMASK)
            AC = (AC + 1) & DMASK;
        MB = AC;
        if (!(AC & SIGN))
            PC = INCR_ADDR (PC);
        reason = Write ();
        break;

    case 024:                                           /* SAD */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        if (AC != MB)
            PC = INCR_ADDR (PC);
        break;

    case 025:                                           /* SAS */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        if (AC == MB)
            PC = INCR_ADDR (PC);
        break;

    case 030:                                           /* JMP */
        if (sbs &&                                      /* SBS enabled? */
            ((PC & EPCMASK) == 0) &&                    /* in bank 0? */
            ((IR & (IA|07703)) == (IA|00001)) &&        /* jmp i 00x1/5? */
            ((cpu_unit.flags & UNIT_SBS) ||             /* 16-chan SBS or */
             ((IR & 00074) == 0))) {                    /* jmp i 0001? */
            if (cpu_unit.flags & UNIT_SBS) {            /* 16-chan SBS dbk? */
                int32 lvl = (IR >> 2) & SBS_LVL_MASK;   /* lvl = MA<14:15> */
                sbs_act &= ~SBS_MASK (lvl);             /* clr level active */
                sbs_lvl = sbs_eval ();                  /* eval SBS system */
                }
            else sbs = sbs & ~SB_IP;                    /* 1-chan dbk */
            PCQ_ENTRY;                                  /* save old PC */
            MA = IR & DAMASK;                           /* ind addr */
            Read ();                                    /* eff addr word */
            OV = (MB >> 17) & 1;                        /* restore OV */
            extm = (MB >> 16) & 1;                      /* restore ext mode */
            PC = MB & AMASK;                            /* jmp i 00x1/5 */
            if (hst_p)                                  /* history enabled? */
                hst[hst_p].ea = PC;
            }
        else {                                          /* normal JMP */
            if ((reason = Ea (IR)))                     /* MA <- eff addr */
                break;
            PCQ_ENTRY;
            PC = MA;
            }
        break;

    case 031:                                           /* JSP */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        AC = EPC_WORD;
        PCQ_ENTRY;
        PC = MA;
        break;

    case 034:                                           /* LAW */
        AC = (IR & 07777) ^ ((IR & IA)? 0777777: 0);
        break;

/* Multiply and divide

   Multiply and divide step and hardware multiply are exact implementations.
   Hardware divide is a 2's complement analog to the actual hardware.
*/   

    case 026:                                           /* MUL */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        if (cpu_unit.flags & UNIT_MDV) {                /* hardware? */
            sign = AC ^ MB;                             /* result sign */
            IO = ABS (AC);                              /* IO = |AC| */
            v = ABS (MB);                               /* v = |mpy| */
            for (i = AC = 0; i < 17; i++) {
                if (IO & 1)
                    AC = AC + v;
                IO = (IO >> 1) | ((AC & 1) << 17);
                AC = AC >> 1;
                }
            if ((sign & SIGN) && (AC | IO)) {           /* negative, > 0? */
                AC = AC ^ DMASK;
                IO = IO ^ DMASK;
                }
            }
        else {                                          /* multiply step */
            if (IO & 1)
                AC = AC + MB;
            if (AC > DMASK)
                AC = (AC + 1) & DMASK;
            IO = (IO >> 1) | ((AC & 1) << 17);
            AC = AC >> 1;
            }
        break;

    case 027:                                           /* DIV */
        if ((reason = Ea (IR)))                         /* MA <- eff addr */
            break;
        if ((reason = Read ()))                         /* MB <- data */
            break;
        if (cpu_unit.flags & UNIT_MDV) {                /* hardware */
            sign = AC ^ MB;                             /* result sign */
            signd = AC;                                 /* remainder sign */
            v = ABS (MB);                               /* v = |divr| */
            if (ABS (AC) >= v)                          /* overflow? */
                break;
            if (AC & SIGN) {
                AC = AC ^ DMASK;                        /* AC'IO = |AC'IO| */
                IO = IO ^ DMASK;
                }
            for (i = t = 0; i < 18; i++) {
                if (t)
                    AC = (AC + v) & DMASK;
                else AC = (AC - v) & DMASK;
                t = AC >> 17;
                if (i != 17)
                    AC = ((AC << 1) | (IO >> 17)) & DMASK;
                IO = ((IO << 1) | (t ^ 1)) & 0777777;
                }
            if (t)                                      /* fix remainder */
                AC = (AC + v) & DMASK;
            t = ((signd & SIGN) && AC)? AC ^ DMASK: AC;
            AC = ((sign & SIGN) && IO)? IO ^ DMASK: IO;
            IO = t;
            PC = INCR_ADDR (PC);                        /* skip */
            }
        else {                                          /* divide step */
            t = AC >> 17;
            AC = ((AC << 1) | (IO >> 17)) & DMASK;
            IO = ((IO << 1) | (t ^ 1)) & DMASK;
            if (IO & 1)
                AC = AC + (MB ^ DMASK);
            else AC = AC + MB + 1;
            if (AC > DMASK)
                AC = (AC + 1) & DMASK;
            if (AC == DMASK)
                AC = 0;
            }
        break;

/* Skips */

    case 032:                                           /* skip */
        v = (IR >> 3) & 07;                             /* sense switches */
        t = IR & 07;                                    /* program flags */
        skip = (((cpu_unit.flags & UNIT_1D) &&
                 (IR & 04000) && (IO != 0)) ||          /* SNI (PDP-1D) */
                ((IR & 02000) && !(IO & SIGN)) ||       /* SPI */
                ((IR & 01000) && (OV == 0)) ||          /* SZO */
                ((IR & 00400) && (AC & SIGN)) ||        /* SMA */
                ((IR & 00200) && !(AC & SIGN)) ||       /* SPA */
                ((IR & 00100) && (AC == 0)) ||          /* SZA */
                (v && ((SS & fs_test[v]) == 0)) ||      /* SZSn */
                (t && ((PF & fs_test[t]) == 0)));       /* SZFn */
        if (IR & IA)                                    /* invert skip? */
            skip = skip ^ 1;
        if (skip)
            PC = INCR_ADDR (PC);
        if (IR & 01000)                                 /* SOV clears OV */
            OV = 0;
        break;

/* Shifts */

    case 033:
        sc = sc_map[IR & 0777];                         /* map shift count */
        switch ((IR >> 9) & 017) {                      /* case on IR<5:8> */

        case 001:                                       /* RAL */
            AC = ((AC << sc) | (AC >> (18 - sc))) & DMASK;
            break;

        case 002:                                       /* RIL */
            IO = ((IO << sc) | (IO >> (18 - sc))) & DMASK;
            break;

        case 003:                                       /* RCL */
            t = AC;
            AC = ((AC << sc) | (IO >> (18 - sc))) & DMASK;
            IO = ((IO << sc) | (t >> (18 - sc))) & DMASK;
            break;

        case 005:                                       /* SAL */
            t = (AC & SIGN)? DMASK: 0;
            AC = (AC & SIGN) | ((AC << sc) & 0377777) |
                (t >> (18 - sc));
            break;

        case 006:                                       /* SIL */
            t = (IO & SIGN)? DMASK: 0;
            IO = (IO & SIGN) | ((IO << sc) & 0377777) |
                (t >> (18 - sc));
            break;

        case 007:                                       /* SCL */
            t = (AC & SIGN)? DMASK: 0;
            AC = (AC & SIGN) | ((AC << sc) & 0377777) | 
                (IO >> (18 - sc));
            IO = ((IO << sc) | (t >> (18 - sc))) & DMASK;
            break;

        case 011:                                       /* RAR */
            AC = ((AC >> sc) | (AC << (18 - sc))) & DMASK;
            break;

        case 012:                                       /* RIR */
            IO = ((IO >> sc) | (IO << (18 - sc))) & DMASK;
            break;

        case 013:                                       /* RCR */
            t = IO;
            IO = ((IO >> sc) | (AC << (18 - sc))) & DMASK;
            AC = ((AC >> sc) | (t << (18 - sc))) & DMASK;
            break;

        case 015:                                       /* SAR */
            t = (AC & SIGN)? DMASK: 0;
            AC = ((AC >> sc) | (t << (18 - sc))) & DMASK;
            break;

        case 016:                                       /* SIR */
            t = (IO & SIGN)? DMASK: 0;
            IO = ((IO >> sc) | (t << (18 - sc))) & DMASK;
            break;

        case 017:                                       /* SCR */
            t = (AC & SIGN)? DMASK: 0;
            IO = ((IO >> sc) | (AC << (18 - sc))) & DMASK;
            AC = ((AC >> sc) | (t << (18 - sc))) & DMASK;
            break;

        default:                                        /* undefined */
            reason = stop_inst;
            break;
            }                                           /* end switch shf */
        break;

/* Special operates (PDP-1D) - performed in order shown */

    case 036:                                           /* special */
        if (cpu_unit.flags & UNIT_1D) {                 /* PDP-1D? */
            if (IR & 000100)                            /* SCI */
                IO = 0;
            if (IR & 000040)                            /* SCF */
                PF = 0;
            if (cpu_unit.flags & UNIT_1D45) {           /* SN 45? */
                if ((IR & 000020) &&                    /* SZL/SNL? */
                    (((PF & PF_L) == 0) == ((IR & IA) == 0)))
                    PC = INCR_ADDR (PC);
                if (IR & 000010)                        /* CLL */
                    PF = PF & ~PF_L;
                if (IR & 000200) {                      /* SCM */
                    AC = (AC ^ DMASK) + ((PF & PF_L)? 1: 0);
                    if (AC > DMASK)                     /* carry? set L */
                        PF = PF | PF_L;
                    else PF = PF & ~PF_L;               /* no, clear L */
                    AC = AC & DMASK;                    /* mask AC */
                    }
                }
            t = IO & PF_VR_ALL;
            if (IR & 004000)                            /* IIF */
                IO = IO | PF;
            if (IR & 002000)                            /* IFI */
                PF = PF | t;
            if (cpu_unit.flags & UNIT_1D45) {           /* SN 45? */
                if (IR & 000004)                        /* CML */
                    PF = PF ^ PF_L;
                if (IR & 000400)                        /* IDA */
                    AC = (PF & PF_RNG)?
                         (AC & 0777770) | ((AC + 1) & 07):
                         (AC + 1) & DMASK;
                }
            else PF = PF & ~PF_L;                       /* no link */
            if (IR & 01000)                             /* IDC */
                AC = inc_bp (AC);
            }
        else reason = stop_inst;                        /* no, illegal */
        break;

/* Operates - performed in the order shown */

    case 037:                                           /* operate */
        if (IR & 004000)                                /* CLI */
            IO = 0;
        if (IR & 000200)                                /* CLA */
            AC = 0;
        if (IR & 002000)                                /* LAT */
            AC = AC | TW;
        if (IR & 000100)                                /* LAP */
            AC = AC | EPC_WORD;
        if (IR & 001000)                                /* CMA */
            AC = AC ^ DMASK;
        if (cpu_unit.flags & UNIT_1D) {                 /* PDP-1D? */
            if (IR & 010000)                            /* CMI */
                IO = IO ^ DMASK;
            MB = IO;
            if (IR & 000020)                            /* LIA */
                IO = AC;
            if (IR & 000040)                            /* LAI */
                AC = MB;
            }
        t = IR & 07;                                    /* flag select */
        if (IR & 010)                                   /* STFn */
            PF = PF | fs_test[t];
        else PF = PF & ~fs_test[t];                     /* CLFn */
        if (IR & 000400) {                              /* HLT */
            if (rm && !sbs_act)                         /* restrict, ~brk? */
                reason = set_rmv (RTB_HLT);             /* violation */
            else reason = STOP_HALT;                    /* no, halt */
            }
        break;

/* IOT - The simulator behaves functionally like a real PDP-1 but does not
   use the same mechanisms or state bits.  In particular,

   - If an IOT does not specify IO_WAIT, the IOT will be executed, and the
     I/O halt flag (IOH) will not be disturbed.  On the real PDP-1, IOH is
     stored in IHS, IOH is cleared, the IOT is executed, and then IOH is
     restored from IHS.  Because IHS is not otherwise used, it is not
     explicitly simulated.
   - If an IOT does specify IO_WAIT, then IOH specifies whether an I/O halt
     (wait) is already in progress.
     > If already set, I/O wait is in progress.  The simulator looks for
       a completion pulse (IOS).  If there is a pulse, IOH is cleared.  If
       not, the IOT is fetched again.  In either case, execution of the
       IOT is skipped.
     > If not set, I/O wait must start.  IOH is set, the PC is backed up,
       and the IOT is executed.
   - On a real PDP-1, IOC is the I/O command enable and enables the IOT
     pulses.  In the simulator, the enabling of IOT pulses is done through
     code flow, and IOC is not explicitly simulated.
*/

    case 035:
        if (rm && !sbs_act) {                           /* restrict, ~brk? */
            reason = set_rmv (RTB_IOT);                 /* violation */
            break;
            }
        if (IR & IO_WAIT) {                             /* wait? */
            if (ioh) {                                  /* I/O halt? */
                if (ios)                                /* comp pulse? done */
                    ioh = 0;
                else {                                  /* wait more */
                    PC = DECR_ADDR (PC);                /* re-execute */
                    if (cpls == 0) {                    /* pending pulses? */
                        reason = STOP_WAIT;             /* no, CPU hangs */
                        break;
                        }
                    sim_interval = 0;                   /* force event */
                    }
                break;                                  /* skip iot */
                }
            ioh = 1;                                    /* turn on halt */
            PC = DECR_ADDR (PC);                        /* re-execute */
            }
        dev = IR & 077;                                 /* get dev addr */
        pulse = (IR >> 6) & 077;                        /* get pulse data */
        io_data = IO;                                   /* default data */
        switch (dev) {                                  /* case on dev */

        case 000:                                       /* I/O wait */
            break;

        case 001:
            if (IR & 003700)                            /* DECtape */
                io_data = dt (IR, dev, IO);
            else io_data = ptr (IR, dev, IO);           /* paper tape rdr */
            break;

        case 002: case 030:                             /* paper tape rdr */
            io_data = ptr (IR, dev, IO);
            break;

        case 003:                                       /* typewriter */
            io_data = tto (IR, dev, IO);
            break;

        case 004:                                       /* keyboard */
            io_data = tti (IR, dev, IO);
            break;

        case 005: case 006:                             /* paper tape punch */
            io_data = ptp (IR, dev, IO);
            break;

#ifdef USE_DISPLAY
        case 007:                                       /* display */
            io_data = dpy (IR, dev, IO, AC);
            break;
#endif
        case 010:                                       /* leave ring mode */
            if (cpu_unit.flags & UNIT_1D)
                PF = PF & ~PF_RNG;
            else reason = stop_inst;
            break;

        case 011:                                       /* enter ring mode */
            if (cpu_unit.flags & UNIT_1D)
                PF = PF | PF_RNG;
            else
#ifdef USE_DISPLAY
                io_data = spacewar (IR, dev, IO);
#else
                reason = stop_inst;
#endif
            break;

       case 022:                                        /* data comm sys */
           io_data = dcs (IR, dev, IO);
           break;

        case 032:                                       /* clock */
            io_data = clk (IR, dev, IO);
            break;

        case 033:                                       /* check status */
            io_data = iosta | ((sbs & SB_ON)? IOS_SQB: 0);
            break;

        case 035:                                       /* check trap buf */
            if (cpu_unit.flags & UNIT_1D45) {           /* SN 45? */
                io_data = rtb;
                rtb = 0;
                }
            else reason = stop_inst;
            break;

        case 045:                                       /* line printer */
            io_data = lpt (IR, dev, IO);
            break;

        case 050:                                       /* deact seq break */
            if (cpu_unit.flags & UNIT_SBS)
                sbs_enb &= ~SBS_MASK (pulse & SBS_LVL_MASK);
            else reason = stop_inst;
            break;

        case 051:                                       /* act seq break */
            if (cpu_unit.flags & UNIT_SBS)
                sbs_enb |= SBS_MASK (pulse & SBS_LVL_MASK);
            else reason = stop_inst;
            break;

        case 052:                                       /* start seq break */
            if (cpu_unit.flags & UNIT_SBS)
                sbs_req |= SBS_MASK (pulse & SBS_LVL_MASK);
            else reason = stop_inst;
            break;

        case 053:                                       /* clear all chan */
            if (cpu_unit.flags & UNIT_SBS)
                sbs_enb = 0;
            else reason = stop_inst;
            break;

        case 054:                                       /* seq brk off */
            sbs = sbs & ~SB_ON;
            break;

        case 055:                                       /* seq brk on */
            sbs = sbs | SB_ON;
            break;

        case 056:                                       /* clear seq brk */
            sbs = 0;                                    /* clear PI */
            sbs_req = 0;
            sbs_enb = 0;
            sbs_act = 0;
            break;

        case 061: case 062: case 063:                   /* drum */
            io_data = drm (IR, dev, IO);
            break;

        case 064:                                       /* drum/leave rm */
            if (cpu_unit.flags & UNIT_1D)
                rm = 0;
            else io_data = drm (IR, dev, IO);
            break;

        case 065:                                       /* enter rm */
            if (cpu_unit.flags & UNIT_1D) {
                rm = 1;
                rmask = IO;
                }
            else reason = stop_inst;
            break;

        case 066:                                       /* rename mem */
            if (cpu_unit.flags & UNIT_1D45) {           /* SN45? */
                int32 from = (IR >> 9) & RM45_M_BNK;
                int32 to = (IR >> 6) & RM45_M_BNK;
                rname[from] = to;
                }
            else reason = stop_inst;
            break;

        case 067:                                       /* reset renaming */
            if (cpu_unit.flags & UNIT_1D45) {           /* SN45 */
                for (i = 0; i < RN45_SIZE; i++)
                    rname[i] = i;
                }
            else reason = stop_inst;
            break;

        case 074:                                       /* extend mode */
            extm = (IR >> 11) & 1;                      /* set from IR<6> */
            break;

        default:                                        /* undefined */
            reason = stop_inst;
            break;
            }                                           /* end switch dev */

        IO = io_data & DMASK;
        if (io_data & IOT_SKP)                          /* skip? */
            PC = INCR_ADDR (PC);
        if (io_data >= IOT_REASON)
            reason = io_data >> IOT_V_REASON;
        sbs_lvl = sbs_eval ();                          /* eval SBS system */
        break;

    default:                                            /* undefined */
        if (rm && !sbs_act)                             /* restrict, ~brk? */
            reason = set_rmv (RTB_ILL);                 /* violation */
        else reason = STOP_RSRV;                        /* halt */
        break;
        }                                               /* end switch op */

    if (reason == ERR_RMV) {                            /* restrict viol? */
        sbs_req |= SBS_MASK (SBS_LVL_RMV);              /* request break */
        sbs_lvl = sbs_eval ();                          /* re-eval SBS */
        reason = 0;                                     /* continue */
        }
    }                                                   /* end while */
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}

/* Effective address routine for standard memory reference instructions */

t_stat Ea (int32 IR)
{
int32 i;
t_stat r;

MA = (PC & EPCMASK) | (IR & DAMASK);                    /* direct address */
if (IR & IA) {                                          /* indirect addr? */
    if (extm) {                                         /* extend? */
        if ((r = Read ()))                              /* read; err? */
            return r;
        MA = MB & AMASK;                                /* one level */
        }
    else {                                              /* multi-level */
        for (i = 0; i < ind_max; i++) {                 /* count indirects */
            if ((r = Read ()))                          /* get ind word */
                return r;
            MA = (PC & EPCMASK) | (MB & DAMASK);
            if ((MB & IA) == 0)
                break;
            }
        if (i >= ind_max)                               /* indirect loop? */
            return STOP_IND;
        }                                               /* end else !extm */
    }                                                   /* end if indirect */
if (hst_p)                                              /* history enabled? */
    hst[hst_p].ea = MA;
return SCPE_OK;
}

/* Effective address routine for character instructions */

t_stat Ea_ch (int32 IR, int32 *bn)
{
int32 i;
t_stat r;

MA = (PC & EPCMASK) | (IR & DAMASK);                    /* direct address */
if (extm) {                                             /* extend? */
    if ((r = Read ()))                                  /* read; err? */
        return r;
    }
else {                                                  /* multi-level */
    for (i = 0; i < ind_max; i++) {                     /* count indirects */
        if ((r = Read ()))                              /* get ind word */
            return r;
        if ((MB & IA) == 0)
            break;
        MA = (PC & EPCMASK) | (MB & DAMASK);
        }
    if (i >= ind_max)                                   /* indirect loop? */
        return STOP_IND;
    }                                                   /* end else !extm */
if (IR & IA) {                                          /* automatic mode? */
    if (rm && !sbs_act && ((MB & 0607777) == 0607777))  /* page cross? */
        return set_rmv (RTB_CHR);
    MB = inc_bp (MB);                                   /* incr byte ptr */
    Write ();                                           /* rewrite */
    }
*bn = (MB >> 16) & 03;                                  /* byte num */
if (extm)                                               /* final ea */
    MA = MB & AMASK;
else MA = (PC & EPCMASK) | (MB & DAMASK);
if (hst_p)                                              /* history enabled? */
    hst[hst_p].ea = MA;
return SCPE_OK;
}

/* Increment byte pointer, allowing for ring mode */

int32 inc_bp (int32 bp)
{
bp = bp + (1 << 16);                                    /* add to bit<1> */
if (bp > DMASK) {                                       /* carry out? */
    if (PF & PF_RNG)                                    /* ring mode? */
        bp = (1 << 16) | (bp & 0177770) | ((bp + 1) & 07);
    else bp = (1 << 16) | ((bp + 1) & AMASK);
    }
return bp;
}

/* Read and write memory */

t_stat Read (void)
{
if (rm && !sbs_act) {                                   /* restrict check? */
    int32 bnk = MA_GETBNK (MA);                         /* get bank */
    if ((rmask << bnk) & SIGN)
        return set_rmv (0);
    }
MB = M[MA];
if (hst_p)                                              /* history enabled? */
    hst[hst_p].opnd = MB;
return SCPE_OK;
}

t_stat Write (void)
{
if (hst_p)                                              /* hist? old contents */
    hst[hst_p].opnd = M[MA];
if (rm && !sbs_act) {                                   /* restrict check? */
    int32 bnk = MA_GETBNK (MA);                         /* get bank */
    if ((rmask << bnk) & SIGN)
        return set_rmv (0);
    }
if (MEM_ADDR_OK (MA))
    M[MA] = MB;
return SCPE_OK;
}

/* Restrict mode trap */

t_stat set_rmv (int32 code)
{
rtb = code | (MB & RTB_MB_MASK);
return ERR_RMV;
}

/* Evaluate SBS system */

int32 sbs_eval (void)
{
int32 hi;

if (cpu_unit.flags & UNIT_SBS) {                        /* SBS enabled? */
    if (sbs_req == 0)                                   /* any requests? */
        return 0;
    hi = sbs_ffo (sbs_req);                             /* find highest */
    if (hi < sbs_ffo (sbs_act))                         /* higher? */
        return hi + 1;
    }
return 0;
}

/* Find first one in a 16b field */

int32 sbs_ffo (int32 mask)
{
if (mask & 0177400)
    return ffo_map[(mask >> 8) & 0377];
else return (ffo_map[mask & 0377] + 8);
}

/* Device request interrupt */

t_stat dev_req_int (int32 lvl)
{
if (cpu_unit.flags & UNIT_SBS) {                        /* SBS enabled? */
    if (lvl >= SBS_LVLS)                                /* invalid level? */
        return SCPE_IERR;
    if (sbs_enb & SBS_MASK (lvl))                       /* level active? */
        sbs_req |= SBS_MASK (lvl);                      /* set SBS request */
    }
else sbs |= SB_RQ;                                      /* PI request */
return SCPE_OK;
}

/* Device set/show SBS level */

t_stat dev_set_sbs (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 *lvl = (int32 *) desc;
int32 newlvl;
t_stat r;

if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
newlvl = get_uint (cptr, 10, SBS_LVLS - 1, &r);
if (r != SCPE_OK)
    return SCPE_ARG;
*lvl = newlvl;
return SCPE_OK;
}

t_stat dev_show_sbs (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const int32 *lvl = (const int32 *) desc;

if (lvl == NULL)
    return SCPE_IERR;
fprintf (st, "SBS level %d", *lvl);
return SCPE_OK;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int32 i;

sbs = sbs_init;
extm = extm_init;
ioh = 0;
ios = 0;
cpls = 0;
sbs_act = 0;
sbs_req = 0;
sbs_enb = 0;
OV = 0;
PF = 0;
MA = 0;
MB = 0;
rm = 0;
rtb = 0;
rmask = 0;
for (i = 0; i < RN45_SIZE; i++)
    rname[i] = i;
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
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = M[addr] & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
M[addr] = val & DMASK;
return SCPE_OK;
}

/* Change memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;

if ((val <= 0) || (((size_t)val) > MAXMEMSIZE) || ((val & 07777) != 0))
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

/* Set PDP-1D */

t_stat cpu_set_1d (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
uptr->flags |= UNIT_SBS|UNIT_MDV;
return SCPE_OK;
}

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
int32 ov, pf, op, k, di, lnt;
const char *cptr = (const char *) desc;
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
fprintf (st, "PC      OV AC     IO      PF EA      IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(++di) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        ov = (h->ovac >> HIST_V_SHF) & 1;               /* overflow */
        pf = (h->pfio >> HIST_V_SHF) & PF_VR_ALL;       /* prog flags */
        op = ((h->ir >> 13) & 037);                     /* get opcode */
        fprintf (st, "%06o  %o  %06o %06o %03o ",
            h->pc & AMASK, ov, h->ovac & DMASK, h->pfio & DMASK, pf);
        if ((op < 032) && (op != 007))                  /* mem ref instr */
            fprintf (st, "%06o  ", h->ea);
        else fprintf (st, "        ");
        sim_eval = h->ir;
        if ((fprint_sym (st, h->pc & AMASK, &sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "(undefined) %06o", h->ir);
        else if (op < 030)                              /* mem ref instr */
            fprintf (st, " [%06o]", h->opnd);
        fputc ('\n', st);                               /* end line */
        }                                               /* end else instruction */
    }                                                   /* end for */
return SCPE_OK;
}

#ifdef USE_DISPLAY
/* set "test switches"; from display code */

void cpu_set_switches(unsigned long bits)
{
/* just what we want; smaller CPUs might want to shift down? */
TW = bits;
}

unsigned long cpu_get_switches(void)
{
return TW;
}
#endif
