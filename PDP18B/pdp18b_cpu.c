/* pdp18b_cpu.c: 18b PDP CPU simulator

   Copyright (c) 1993-2016, Robert M Supnik

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

   cpu          PDP-4/7/9/15 central processor

   10-Mar-16    RMS     Added 3-cycle databreak set/show routines
   07-Mar-16    RMS     Revised to allocate memory dynamically
   28-Mar-15    RMS     Revised to use sim_printf
   28-Apr-07    RMS     Removed clock initialization
   26-Dec-06    RMS     Fixed boundary test in KT15/XVM (Andrew Warkentin)
   30-Oct-06    RMS     Added idle and infinite loop detection
   08-Oct-06    RMS     Added RDCLK instruction
                        Fixed bug, PC off by one on fetch mem mmgt error
                        PDP-15 sets API 3 on mem mmgt trap (like PI)
                        PDP-15 sets API 4 on CAL only if 0-3 inactive
                        CAF clears memory management mode register
   27-Jun-06    RMS     Reset clears AC, L, and MQ
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   22-Jul-05    RMS     Removed AAS, error in V1 reference manual
   06-Nov-04    RMS     Added =n to SHOW HISTORY
   26-Mar-04    RMS     Fixed warning from -std=c99
   14-Jan-04    RMS     Fixed g_mode in XVM implementation
                        PDP-15 index, autoincrement generate 18b addresses
                        Revised IO device call interface
   31-Dec-03    RMS     Fixed bug in cpu_set_hist
   02-Nov-03    RMS     Changed PDP-9,-15 default to API
   26-Oct-03    RMS     Fixed bug in PDP-4,-7,-9 autoincrement addressing
   19-Sep-03    RMS     Changed instruction history to be dynamically sized
   31-Aug-03    RMS     Added instruction history
                        Fixed PDP-15-specific implementation of API priorities
   16-Aug-03    RMS     Fixed PDP-15-specific handling of EAE unsigned mul/div
   27-Jul-03    RMS     Added FP15 support
                        Added XVM support
                        Added EAE option to PDP-4
                        Added PDP-15 "re-entrancy ECO"
                        Fixed memory protect/skip interaction
                        Fixed CAF not to reset CPU
   12-Mar-03    RMS     Added logical name support
   18-Feb-03    RMS     Fixed three EAE bugs (Hans Pufal)
   05-Oct-02    RMS     Added DIBs, device number support
   25-Jul-02    RMS     Added DECtape support for PDP-4
   06-Jan-02    RMS     Revised enable/disable support
   30-Dec-01    RMS     Added old PC queue
   30-Nov-01    RMS     Added extended SET/SHOW support
   25-Nov-01    RMS     Revised interrupt structure
   19-Sep-01    RMS     Fixed bug in EAE (Dave Conroy)
   17-Sep-01    RMS     Fixed typo in conditional
   10-Aug-01    RMS     Removed register from declarations
   17-Jul-01    RMS     Moved function prototype
   27-May-01    RMS     Added second Teletype support, fixed bug in API
   18-May-01    RMS     Added PDP-9,-15 API option
   16-May-01    RMS     Fixed bugs in protection checks
   26-Apr-01    RMS     Added device enable/disable support
   25-Jan-01    RMS     Added DECtape support
   18-Dec-00    RMS     Added PDP-9,-15 memm init register
   30-Nov-00    RMS     Fixed numerous PDP-15 bugs
   14-Apr-99    RMS     Changed t_addr to unsigned

   The 18b PDP family has five distinct architectural variants: PDP-1,
   PDP-4, PDP-7, PDP-9, and PDP-15.  Of these, the PDP-1 is so unique
   as to require a different simulator.  The PDP-4, PDP-7, PDP-9, and
   PDP-15 are "upward compatible", with each new variant adding
   distinct architectural features and incompatibilities.

   The register state for the 18b PDP's is:

   all                  AC<0:17>        accumulator
   all                  MQ<0:17>        multiplier-quotient
   all                  L               link flag
   all                  PC<0:x>         program counter
   all                  IORS            I/O status register
   PDP-7, PDP-9         EXTM            extend mode
   PDP-15               BANKM           bank mode
   PDP-7                USMD            trap mode
   PDP-9, PDP-15        USMD            user mode
   PDP-9, PDP-15        BR              bounds register
   PDP-15               RR              relocation register
   PDP-15 XVM           MMR             memory management register
   PDP-15               XR              index register
   PDP-15               LR              limit register

   The PDP-4, PDP-7, and PDP-9 have five instruction formats: memory
   reference, load immediate, I/O transfer, EAE, and operate.  The PDP-15
   adds a sixth, index operate, and a seventh, floating point.  The memory
   reference format for the PDP-4, PDP-7, and PDP-9, and for the PDP-15
   in bank mode, is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |     op    |in|               address                | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The PDP-15 in page mode trades an address bit for indexing capability:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |     op    |in| X|             address               | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:3>        mnemonic        action

   00           CAL             JMS with MA = 20
   04           DAC             M[MA] = AC
   10           JMS             M[MA] = L'mem'user'PC, PC = MA + 1
   14           DZM             M[MA] = 0
   20           LAC             AC = M[MA]
   24           XOR             AC = AC ^ M[MA]
   30           ADD             L'AC = AC + M[MA] one's complement
   34           TAD             L'AC = AC + M[MA]
   40           XCT             M[MA] is executed as an instruction
   44           ISZ             M[MA] = M[MA] + 1, skip if M[MA] == 0
   50           AND             AC = AC & M[MA]
   54           SAD             skip if AC != M[MA]
   60           JMP             PC = MA

   On the PDP-4, PDP-7, and PDP-9, and the PDP-15 in bank mode, memory
   reference instructions can access an address space of 32K words.  The
   address space is divided into four 8K word fields.  An instruction can
   directly address, via its 13b address, the entire current field.  On the
   PDP-4, PDP-7, and PDP-9, if extend mode is off, indirect addresses access
   the current field; if on (or a PDP-15), they can access all 32K.

   On the PDP-15 in page mode, memory reference instructions can access
   an address space of 128K words.  The address is divided into four 32K
   word blocks, each of which consists of eight 4K pages.  An instruction
   can directly address, via its 12b address, the current page.  Indirect
   addresses can access the current block.  Indexed and autoincrement
   addresses can access all 128K.

   On the PDP-4 and PDP-7, if an indirect address in in locations 00010-
   00017 of any field, the indirect address is incremented and rewritten
   to memory before use.  On the PDP-9 and PDP-15, only locations 00010-
   00017 of field zero autoincrement; special logic will redirect indirect
   references to 00010-00017 to field zero, even if (on the PDP-9) extend
   mode is off.

   The EAE format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  0  1|  |  |  |  |  |  |  |  |  |  |  |  |  |  | EAE
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 |  |  |  |  |  |  |  |  |  |  |  |  |  |
                 |  |  |  |  |  |  |  |  |  |  |  |  |  +- or SC (3)
                 |  |  |  |  |  |  |  |  |  |  |  |  +---- or MQ (3)
                 |  |  |  |  |  |  |  |  |  |  |  +------- compl MQ (3)
                 |  |  |  |  |  |  |  |  \______________/
                 |  |  |  |  |  |  |  |         |
                 |  |  |  |  |  \_____/         +--------- shift count
                 |  |  |  |  |     |
                 |  |  |  |  |     +---------------------- EAE command (3)
                 |  |  |  |  +---------------------------- clear AC (2)
                 |  |  |  +------------------------------- or AC (2)
                 |  |  +---------------------------------- load EAE sign (1)
                 |  +------------------------------------- clear MQ (1)
                 +---------------------------------------- load link (1)

   The I/O transfer format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  0  0|      device     | sdv |cl|  pulse | I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IO transfer instruction sends the the specified pulse to the
   specified I/O device and sub-device.  The I/O device may take data
   from the AC, return data to the AC, initiate or cancel operations,
   or skip on status.  On the PDP-4, PDP-7, and PDP-9, bits <4:5>
   were designated as subdevice bits but were never used; the PDP-15
   requires them to be zero.

   On the PDP-15, the floating point format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  0  1|            subopcode              | floating point
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|                   address                        |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   Indirection is always single level.

   On the PDP-15, the index operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  1| subopcode |        immediate         | index operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The index operate instructions provide various operations on the
   index and limit registers.

   The operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  1  0|  |  |  |  |  |  |  |  |  |  |  |  |  | operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                    |  |  |  |  |  |  |  |  |  |  |  |  |
                    |  |  |  |  |  |  |  |  |  |  |  |  +- CMA (3)
                    |  |  |  |  |  |  |  |  |  |  |  +---- CML (3)
                    |  |  |  |  |  |  |  |  |  |  +------- OAS (3)
                    |  |  |  |  |  |  |  |  |  +---------- RAL (3)
                    |  |  |  |  |  |  |  |  +------------- RAR (3)
                    |  |  |  |  |  |  |  +---------------- HLT (4)
                    |  |  |  |  |  |  +------------------- SMA (1)
                    |  |  |  |  |  +---------------------- SZA (1)
                    |  |  |  |  +------------------------- SNL (1)
                    |  |  |  +---------------------------- invert skip (1)
                    |  |  +------------------------------- rotate twice (2)
                    |  +---------------------------------- CLL (2)
                    +------------------------------------- CLA (2)

   The operate instruction can be microprogrammed to perform operations
   on the AC and link.

   The load immediate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  1  1|            immediate                 | LAW
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4>        mnemonic        action

   76           LAW             AC = IR

   This routine is the instruction decode routine for the 18b PDP's.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        unimplemented instruction and STOP_INST flag set
        nested XCT's
        I/O error in I/O simulator

   2. Interrupts.  Interrupt requests are maintained in the int_hwre
      array.  int_hwre[0:3] corresponds to API levels 0-3; int_hwre[4]
      holds PI requests.

   3. Arithmetic.  The 18b PDP's implements both 1's and 2's complement
      arithmetic for signed numbers.  In 1's complement arithmetic, a
      negative number is represented by the complement (XOR 0777777) of
      its absolute value.  Addition of 1's complement numbers requires
      propagating the carry out of the high order bit back to the low
      order bit.

   4. Adding I/O devices.  Three modules must be modified:

        pdp18b_defs.h   add interrupt request definition
        pdp18b_sys.c    add sim_devices table entry
*/

#include "pdp18b_defs.h"

#define SEXT(x)         ((int32) (((x) & SIGN)? (x) | ~DMASK: (x) & DMASK))

#define UNIT_V_NOEAE    (UNIT_V_UF + 0)                 /* EAE absent */
#define UNIT_V_NOAPI    (UNIT_V_UF + 1)                 /* API absent */
#define UNIT_V_PROT     (UNIT_V_UF + 2)                 /* protection */
#define UNIT_V_RELOC    (UNIT_V_UF + 3)                 /* relocation */
#define UNIT_V_XVM      (UNIT_V_UF + 4)                 /* XVM */
#define UNIT_V_MSIZE    (UNIT_V_UF + 5)                 /* dummy mask */
#define UNIT_NOEAE      (1 << UNIT_V_NOEAE)
#define UNIT_NOAPI      (1 << UNIT_V_NOAPI)
#define UNIT_PROT       (1 << UNIT_V_PROT)
#define UNIT_RELOC      (1 << UNIT_V_RELOC)
#define UNIT_XVM        (1 << UNIT_V_XVM)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)
#define OP_KSF          0700301

#define HIST_API        0x40000000
#define HIST_PI         0x20000000
#define HIST_PC         0x10000000
#define HIST_MIN        64
#define HIST_MAX        65536
#define HIST_M_LVL      0x3F
#define HIST_V_LVL      6

typedef struct {
    int32               pc;
    int32               ir;
    int32               ir1;
    int32               lac;
    int32               mq;
    } InstHistory;

#define XVM             (cpu_unit.flags & UNIT_XVM)
#define RELOC           (cpu_unit.flags & UNIT_RELOC)
#define PROT            (cpu_unit.flags & UNIT_PROT)

#if defined (PDP4)
#define EAE_DFLT        UNIT_NOEAE
#else
#define EAE_DFLT        0
#endif
#if defined (PDP4) || defined (PDP7)
#define API_DFLT        UNIT_NOAPI
#define PROT_DFLT       0
#define ASW_DFLT        017763
#else
#define API_DFLT        0
#define PROT_DFLT       UNIT_PROT
#define ASW_DFLT        017720
#endif

int32 *M = NULL;                                        /* memory */
int32 LAC = 0;                                          /* link'AC */
int32 MQ = 0;                                           /* MQ */
int32 PC = 0;                                           /* PC */
int32 iors = 0;                                         /* IORS */
int32 ion = 0;                                          /* int on */
int32 ion_defer = 0;                                    /* int defer */
int32 ion_inh = 0;                                      /* int inhibit */
int32 int_pend = 0;                                     /* int pending */
int32 int_hwre[API_HLVL+1] = { 0 };                     /* int requests */
int32 api_enb = 0;                                      /* API enable */
int32 api_req = 0;                                      /* API requests */
int32 api_act = 0;                                      /* API active */
int32 memm = 0;                                         /* mem mode */
#if defined (PDP15)
int32 memm_init = 1;                                    /* mem init */
#else
int32 memm_init = 0;
#endif
int32 usmd = 0;                                         /* user mode */
int32 usmd_buf = 0;                                     /* user mode buffer */
int32 usmd_defer = 0;                                   /* user mode defer */
int32 trap_pending = 0;                                 /* trap pending */
int32 emir_pending = 0;                                 /* emir pending */
int32 rest_pending = 0;                                 /* restore pending */
int32 BR = 0;                                           /* mem mgt bounds */
int32 RR = 0;                                           /* mem mgt reloc */
int32 MMR = 0;                                          /* XVM mem mgt */
int32 nexm = 0;                                         /* nx mem flag */
int32 prvn = 0;                                         /* priv viol flag */
int32 SC = 0;                                           /* shift count */
int32 eae_ac_sign = 0;                                  /* EAE AC sign */
int32 SR = 0;                                           /* switch register */
int32 ASW = ASW_DFLT;                                   /* address switches */
int32 XR = 0;                                           /* index register */
int32 LR = 0;                                           /* limit register */
int32 stop_inst = 0;                                    /* stop on rsrv inst */
int32 xct_max = 16;                                     /* nested XCT limit */
#if defined (PDP15)
int32 pcq[PCQ_SIZE] = { 0 };                            /* PC queue */
#else
int16 pcq[PCQ_SIZE] = { 0 };                            /* PC queue */
#endif
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
int32 hst_p = 0;                                        /* history pointer */
int32 hst_lnt = 0;                                      /* history length */
InstHistory *hst = NULL;                                /* instruction history */

t_bool build_dev_tab (void);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
void cpu_caf (void);
void cpu_inst_hist (int32 addr, int32 inst);
void cpu_intr_hist (int32 flag, int32 lvl);
int32 upd_iors (void);
int32 api_eval (int32 *pend);
t_stat Read (int32 ma, int32 *dat, int32 cyc);
t_stat Write (int32 ma, int32 dat, int32 cyc);
t_stat Ia (int32 ma, int32 *ea, t_bool jmp);
int32 Incr_addr (int32 addr);
int32 Jms_word (int32 t);
#if defined (PDP15)
#define INDEX(i,x)      if (!memm && ((i) & I_IDX)) \
                            x = ((x) + XR) & DMASK
int32 Prot15 (int32 ma, t_bool bndchk);
int32 Reloc15 (int32 ma, int32 acc);
int32 RelocXVM (int32 ma, int32 acc);
extern t_stat fp15 (int32 ir);
extern int32 clk_task_upd (t_bool clr);
#else
#define INDEX(i,x)
#endif

extern int32 clk (int32 dev, int32 pulse, int32 AC);

int32 (*dev_tab[DEV_MAX])(int32 dev, int32 pulse, int32 AC);    /* device dispatch */

int32 (*dev_iors[DEV_MAX])(void);                       /* IORS dispatch */

static const int32 api_ffo[256] = {
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

int32 api_vec[API_HLVL][32] = {
 { ACH_PWRFL },                                         /* API 0 */
 { ACH_DTA, ACH_MTA, ACH_DRM, ACH_RF, ACH_RP, ACH_RB }, /* API 1 */
 { ACH_PTR, ACH_LPT, ACH_LPT },                         /* API 2 */
 { ACH_CLK, ACH_TTI1, ACH_TTO1 }                        /* API 3 */
 };

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/

UNIT cpu_unit = {
    UDATA (NULL, UNIT_FIX+UNIT_BINK+EAE_DFLT+API_DFLT+PROT_DFLT,
           MAXMEMSIZE)
    };

REG cpu_reg[] = {
    { ORDATAD (PC, PC, ADDRSIZE, "program counter") },
    { ORDATAD (AC, LAC, 18, "accumulator") },
    { FLDATAD (L, LAC, 18, "link") },
    { ORDATAD (MQ, MQ, 18, "multiplier-quotient") },
    { ORDATAD (SC, SC, 6, "shift counter") },
    { FLDATAD (EAE_AC_SIGN, eae_ac_sign, 18, "EAE AC sign") },
    { ORDATAD (SR, SR, 18, "front panel switches") },
    { ORDATAD (ASW, ASW, ADDRSIZE, "address switches for RIM load") },
    { ORDATAD (IORS, iors, 18, "IORS register"), REG_RO },
    { BRDATAD (INT, int_hwre, 8, 32, API_HLVL+1, "interrupt requests, 0:3 = API levels 0 to 3,                                    4 = PI level"), REG_RO },
    { FLDATAD (INT_PEND, int_pend, 0, "interrupt pending"), REG_RO },
    { FLDATAD (ION, ion, 0, "interrupt enable") },
    { ORDATAD (ION_DELAY, ion_defer, 2, "interrupt enable delay") },
#if defined (PDP7) 
    { FLDATAD (TRAPM, usmd, 0, "trap mode") },
    { FLDATAD (TRAPP, trap_pending, 0, "trap pending") },
    { FLDATAD (EXTM, memm, 0, "extend mode") },
    { FLDATAD (EXTM_INIT, memm_init, 0, "exit mode value after reset") },
    { FLDATAD (EMIRP, emir_pending, 0, "EMIR instruction pending") },
#endif
#if defined (PDP9)
    { FLDATAD (APIENB, api_enb, 0, "api enable") },
    { ORDATAD (APIREQ, api_req, 8, "api requesting levels") },
    { ORDATAD (APIACT, api_act, 8, "api active levels") },
    { ORDATAD (BR, BR, ADDRSIZE, "memory protection bounds") },
    { FLDATAD (USMD, usmd, 0, "user mode") },
    { FLDATAD (USMDBUF, usmd_buf, 0, "user mode buffer") },
    { FLDATAD (USMDDEF, usmd_defer, 0, "user mode load defer") },
    { FLDATAD (NEXM, nexm, 0, "non-existent memory violation") },
    { FLDATAD (PRVN, prvn, 0, "privilege violation") },
    { FLDATAD (TRAPP, trap_pending, 0, "trap pending") },
    { FLDATAD (EXTM, memm, 0, "extend mode") },
    { FLDATAD (EXTM_INIT, memm_init, 0, "extend mode value after reset") },
    { FLDATAD (EMIRP, emir_pending, 0, "EMIR instruction pending") },
    { FLDATAD (RESTP, rest_pending, 0, "DBR or RES instruction pending") },
    { FLDATAD (PWRFL, int_hwre[API_PWRFL], INT_V_PWRFL, "power fail pending") },
#endif
#if defined (PDP15)
    { FLDATAD (ION_INH, ion_inh, 0, "interrupt inhibit") },
    { FLDATAD (APIENB, api_enb, 0, "API enable") },
    { ORDATAD (APIREQ, api_req, 8, "API requesting levels") },
    { ORDATAD (APIACT, api_act, 8, "API active levels") },
    { ORDATAD (XR, XR, 18, "index register") },
    { ORDATAD (LR, LR, 18, "limit register") },
    { ORDATAD (BR, BR, 18, "memory protection bounds") },
    { ORDATAD (RR, RR, 18, "memory protection relocation") },
    { ORDATAD (MMR, MMR, 18, "memory protection control") },
    { FLDATAD (USMD, usmd, 0, "user mode") },
    { FLDATAD (USMDBUF, usmd_buf, 0, "user mode buffer") },
    { FLDATAD (USMDDEF, usmd_defer, 0, "user mode load defer") },
    { FLDATAD (NEXM, nexm, 0, "non-existent memory violation") },
    { FLDATAD (PRVN, prvn, 0, "privilege violation") },
    { FLDATAD (TRAPP, trap_pending, 0, "trap pending") },
    { FLDATAD (BANKM, memm, 0, "bank mode") },
    { FLDATAD (BANKM_INIT, memm_init, 0, "bank mode value after reset") },
    { FLDATAD (RESTP, rest_pending, 0, "DBR or RES instruction pending") },
    { FLDATAD (PWRFL, int_hwre[API_PWRFL], INT_V_PWRFL, "power fail flag") },
#endif
    { BRDATAD (PCQ, pcq, 8, ADDRSIZE, PCQ_SIZE, "PC prior to last JMP, JMS, CAL or interrupt; most recent                        PC change first stop on undefineds instruction"), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { FLDATAD (STOP_INST, stop_inst, 0, "stop on defined instruction") },
    { DRDATAD (XCT_MAX, xct_max, 8, "max numb of chained XCT's allowed"), PV_LEFT + REG_NZ },
    { ORDATAD (WRU, sim_int_char, 8, "interrupt character") },
    { NULL }  };

MTAB cpu_mod[] = {
    { UNIT_NOEAE, UNIT_NOEAE, "no EAE", "NOEAE", NULL },
    { UNIT_NOEAE, 0, "EAE", "EAE", NULL },
#if defined (PDP9) || defined (PDP15)
    { UNIT_NOAPI, UNIT_NOAPI, "no API", "NOAPI", NULL },
    { UNIT_NOAPI, 0, "API", "API", NULL },
    { UNIT_PROT+UNIT_RELOC+UNIT_XVM, 0, "no memory protect",
      "NOPROTECT", NULL },
    { UNIT_PROT+UNIT_RELOC+UNIT_XVM, UNIT_PROT, "memory protect",
      "PROTECT", NULL },
#endif
#if defined (PDP15)
    { UNIT_PROT+UNIT_RELOC+UNIT_XVM, UNIT_PROT+UNIT_RELOC,
      "memory relocation", "RELOCATION", NULL },
    { UNIT_PROT+UNIT_RELOC+UNIT_XVM, UNIT_PROT+UNIT_RELOC+UNIT_XVM,
      "XVM", "XVM", NULL },
#endif
    { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
#if defined (PDP4)
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
#endif
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
#if (MAXMEMSIZE > 8192)
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
#endif
#if (MAXMEMSIZE > 32768)
    { UNIT_MSIZE, 49152, NULL, "48K", &cpu_set_size },
    { UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
    { UNIT_MSIZE, 81920, NULL, "80K", &cpu_set_size },
    { UNIT_MSIZE, 98304, NULL, "96K", &cpu_set_size },
    { UNIT_MSIZE, 114688, NULL, "112K", &cpu_set_size },
    { UNIT_MSIZE, 131072, NULL, "128K", &cpu_set_size },
#endif
    { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
      &cpu_set_hist, &cpu_show_hist },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, ADDRSIZE, 1, 8, 18,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
    };

t_stat sim_instr (void)
{
int32 api_int, api_usmd, skp;
int32 iot_data, device, pulse;
int32 last_IR;
t_stat reason;

if (build_dev_tab ())                                   /* build, chk tables */
    return SCPE_STOP;
PC = PC & AMASK;                                        /* clean variables */
LAC = LAC & LACMASK;
MQ = MQ & DMASK;
reason = 0;
last_IR = -1;
if (cpu_unit.flags & UNIT_NOAPI)                        /* no API? */
    api_enb = api_req = api_act = 0;
api_int = api_eval (&int_pend);                         /* eval API */
api_usmd = 0;                                           /* not API user cycle */

/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until halted */

    int32 IR, MA, MB, esc, t, xct_count;
    int32 link_init, fill;

    if (sim_interval <= 0) {                            /* check clock queue */
        if ((reason = sim_process_event ()))
            break;
        api_int = api_eval (&int_pend);                 /* eval API */
        }

/* PDP-4 and PDP-7 traps and interrupts

   PDP-4                no trap
   PDP-7                trap: extend mode forced on, M[0] = PC, PC = 2
   PDP-4, PDP-7         programmable interrupts only */

#if defined (PDP4) || defined (PDP7)
#if defined (PDP7)

    if (trap_pending) {                                 /* trap pending? */
        PCQ_ENTRY;                                      /* save old PC */
        MB = Jms_word (1);                              /* save state */
        ion = 0;                                        /* interrupts off */
        memm = 1;                                       /* extend on */
        emir_pending = trap_pending = 0;                /* emir, trap off */
        usmd = usmd_buf = 0;                            /* user mode off */
        Write (0, MB, WR);                              /* save in 0 */
        PC = 2;                                         /* fetch next from 2 */
        }

#endif

    if (int_pend && ion && !ion_defer) {                /* interrupt? */
        PCQ_ENTRY;                                      /* save old PC */
        MB = Jms_word (usmd);                           /* save state */
        ion = 0;                                        /* interrupts off */
        memm = 0;                                       /* extend off */
        emir_pending = rest_pending = 0;                /* emir, restore off */
        usmd = usmd_buf = 0;                            /* user mode off */
        Write (0, MB, WR);                              /* physical write */
        PC = 1;                                         /* fetch next from 1 */
        }

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }

#endif                                                  /* end PDP-4/PDP-7 */

/* PDP-9 and PDP-15 traps and interrupts

   PDP-9                trap: extend mode ???, M[0/20] = PC, PC = 0/21
   PDP-15               trap: bank mode unchanged, M[0/20] = PC, PC = 0/21
   PDP-9, PDP-15        API and program interrupts */

#if defined (PDP9) || defined (PDP15)

    if (trap_pending) {                                 /* trap pending? */
        PCQ_ENTRY;                                      /* save old PC */
        MB = Jms_word (1);                              /* save state */
        if (ion) {                                      /* int on? */
            ion = 0;                                    /* interrupts off */
            MA = 0;                                     /* treat like PI */
#if defined (PDP15)
            ion_defer = 2;                              /* free instruction */
            if (!(cpu_unit.flags & UNIT_NOAPI)) {       /* API? */
                api_act = api_act | API_ML3;            /* set lev 3 active */
                api_int = api_eval (&int_pend);         /* re-evaluate */
                }
#endif
            }
        else MA = 020;                                  /* sortof like CAL */
        emir_pending = rest_pending = trap_pending = 0; /* emir,rest,trap off */
        usmd = usmd_buf = 0;                            /* user mode off */
        Write (MA, MB, WR);                             /* physical write */
        PC = MA + 1;                                    /* fetch next */
        }

    if (api_int && !ion_defer) {                        /* API intr? */
        int32 i, lvl = api_int - 1;                     /* get req level */
        if (hst_lnt)                                    /* record */
            cpu_intr_hist (HIST_API, lvl);
        api_act = api_act | (API_ML0 >> lvl);           /* set level active */
        if (lvl >= API_HLVL) {                          /* software req? */
            MA = ACH_SWRE + lvl - API_HLVL;             /* vec = 40:43 */
            api_req = api_req & ~(API_ML0 >> lvl);      /* remove request */
            }
        else {
            MA = 0;                                     /* assume fails */
            for (i = 0; i < 32; i++) {                  /* loop hi to lo */
                if ((int_hwre[lvl] >> i) & 1) {         /* int req set? */
                    MA = api_vec[lvl][i];               /* get vector */
                    break;                              /* and stop */
                    }
                }
            }
        if (MA == 0) {                                  /* bad channel? */
            reason = STOP_API;                          /* API error */
            break;
            }
        api_int = api_eval (&int_pend);                 /* no API int */
        api_usmd = usmd;                                /* API user mode cycle */
        usmd = usmd_buf = 0;                            /* user mode off */
        emir_pending = rest_pending = 0;                /* emir, restore off */
        xct_count = 0;
        Read (MA, &IR, FE);                             /* fetch instruction */
        goto xct_instr;
        }

    if (int_pend && ion && !ion_defer &&                /* int pending, enabled? */
        !(api_enb && (api_act & API_MASKPI))) {         /* API off or not masking PI? */
        PCQ_ENTRY;                                      /* save old PC */
        if (hst_lnt)                                    /* record */
            cpu_intr_hist (HIST_PI, 0);
        MB = Jms_word (usmd);                           /* save state */
        ion = 0;                                        /* interrupts off */
        ion_defer = 2;                                  /* free instruction */
#if defined (PDP9)                                      /* PDP-9, */
        memm = 0;                                       /* extend off */
#else                                                   /* PDP-15 */
        if (!(cpu_unit.flags & UNIT_NOAPI)) {           /* API? */
            api_act = api_act | API_ML3;                /* set lev 3 active */
            api_int = api_eval (&int_pend);             /* re-evaluate */
            }
#endif
        emir_pending = rest_pending = 0;                /* emir, restore off */
        usmd = usmd_buf = 0;                            /* user mode off */
        Write (0, MB, WR);                              /* physical write */
        PC = 1;                                         /* fetch next from 1 */
        }

    if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) { /* breakpoint? */
        reason = STOP_IBKPT;                            /* stop simulation */
        break;
        }
    if (!usmd_defer)                                    /* no IOT? load usmd */
        usmd = usmd_buf;
    else usmd_defer = 0;                                /* cancel defer */

#endif                                                  /* PDP-9/PDP-15 */

/* Instruction fetch and address decode */

    xct_count = 0;                                      /* track nested XCT's */
    MA = PC;                                            /* fetch at PC */
    if (Read (MA, &IR, FE))                             /* fetch instruction */
        continue;
    PC = Incr_addr (PC);                                /* increment PC */

    xct_instr:                                          /* label for API, XCT */
    if (hst_lnt)                                        /* history? */
        cpu_inst_hist (MA, IR);
    if (ion_defer)                                      /* count down defer */
        ion_defer = ion_defer - 1;
    if (sim_interval)
        sim_interval = sim_interval - 1;

#if defined (PDP15)                                     /* PDP15 */

    if (memm)                                           /* bank mode dir addr */
        MA = (MA & B_EPCMASK) | (IR & B_DAMASK);
    else MA = (MA & P_EPCMASK) | (IR & P_DAMASK);       /* page mode dir addr */

#else                                                   /* others */

    MA = (MA & B_EPCMASK) | (IR & B_DAMASK);            /* bank mode only */

#endif

    switch ((IR >> 13) & 037) {                         /* decode IR<0:4> */

/* LAC: opcode 20 */

    case 011:                                           /* LAC, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 010:                                           /* LAC, dir */
        INDEX (IR, MA);
        if (Read (MA, &MB, RD))
            break;
        LAC = (LAC & LINK) | MB;
        break;

/* DAC: opcode 04 */

    case 003:                                           /* DAC, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 002:                                           /* DAC, dir */
        INDEX (IR, MA);
        Write (MA, LAC & DMASK, WR);
        break;

/* DZM: opcode 14 */

    case 007:                                           /* DZM, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 006:                                           /* DZM, direct */
        INDEX (IR, MA);
        Write (MA, 0, WR);
        break;

/* AND: opcode 50 */

    case 025:                                           /* AND, ind */
        if (Ia (MA, &MA, 0))
            break;
    case 024:                                           /* AND, dir */
        INDEX (IR, MA);
        if (Read (MA, &MB, RD))
            break;
        LAC = LAC & (MB | LINK);
        break;

/* XOR: opcode 24 */

    case 013:                                           /* XOR, ind */
        if (Ia (MA, &MA, 0))
            break;
    case 012:                                           /* XOR, dir */
        INDEX (IR, MA);
        if (Read (MA, &MB, RD))
            break;
        LAC = LAC ^ MB;
        break;

/* ADD: opcode 30 */

    case 015:                                           /* ADD, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 014:                                           /* ADD, dir */
        INDEX (IR, MA);
        if (Read (MA, &MB, RD))
            break;
        t = (LAC & DMASK) + MB;
        if (t > DMASK)                                  /* end around carry */
            t = (t + 1) & DMASK;
        if (((~LAC ^ MB) & (LAC ^ t)) & SIGN)           /* overflow? */
            LAC = LINK | t;                             /* set link */
        else LAC = (LAC & LINK) | t;
        break;

/* TAD: opcode 34 */

    case 017:                                           /* TAD, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 016:                                           /* TAD, dir */
        INDEX (IR, MA);
        if (Read (MA, &MB, RD))
            break;
        LAC = (LAC + MB) & LACMASK;
        break;

/* ISZ: opcode 44 */

    case 023:                                           /* ISZ, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 022:                                           /* ISZ, dir */
        INDEX (IR, MA);
        if (Read (MA, &MB, RD))
            break;
        MB = (MB + 1) & DMASK;
        if (Write (MA, MB, WR))
            break;
        if (MB == 0)
            PC = Incr_addr (PC);
        break;

/* SAD: opcode 54 */

    case 027:                                           /* SAD, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 026:                                           /* SAD, dir */
        INDEX (IR, MA);
        if (Read (MA, &MB, RD))
            break;
        if ((LAC & DMASK) != MB)
            PC = Incr_addr (PC);
        break;

/* XCT: opcode 40 */

    case 021:                                           /* XCT, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 020:                                           /* XCT, dir  */
        INDEX (IR, MA);
        if ((api_usmd | usmd) && (xct_count != 0)) {    /* chained and usmd? */
            if (usmd)                                   /* trap if usmd */
                prvn = trap_pending = 1;
            break;                                      /* nop if api_usmd */
            }
        if (xct_count >= xct_max) {                     /* too many XCT's? */
            reason = STOP_XCT;
            break;
            }
        xct_count = xct_count + 1;                      /* count XCT's */
#if defined (PDP9)
        ion_defer = 1;                                  /* defer intr */
#endif
        if (Read (MA, &IR, FE))                         /* fetch inst, mm err? */
            break;
        goto xct_instr;                                 /* go execute */

/* CAL: opcode 00 - api_usmd records whether usmd = 1 at start of API cycle

   On the PDP-4 and PDP-7, CAL (I) is exactly the same as JMS (I) 20
   On the PDP-9 and PDP-15, CAL clears user mode
   On the PDP-9 and PDP-15 with API, CAL activates level 4
   On the PDP-15, CAL goes to absolute 20, regardless of mode */

    case 001: case 000:                                 /* CAL */
        t = usmd;                                       /* save user mode */
#if defined (PDP15)                                     /* PDP15 */
        MA = 020;                                       /* MA = abs 20 */
        ion_defer = 1;                                  /* "free instruction" */
#else                                                   /* others */
        if (memm)                                       /* if ext, abs 20 */
            MA = 020;
        else MA = (PC & B_EPCMASK) | 020;               /* else bank-rel 20 */
#endif
#if defined (PDP9) || defined (PDP15)
        usmd = usmd_buf = 0;                            /* clear user mode */
        if ((cpu_unit.flags & UNIT_NOAPI) == 0) {       /* if API, act lvl 4 */
#if defined (PDP15)                                     /* PDP15: if 0-3 inactive */
          if ((api_act & (API_ML0|API_ML1|API_ML2|API_ML3)) == 0)
#endif
            api_act = api_act | API_ML4;
            api_int = api_eval (&int_pend);
            }
#endif
        if (IR & I_IND) {                               /* indirect? */
            if (Ia (MA, &MA, 0))
                break;
            }
        PCQ_ENTRY;
        MB = Jms_word (api_usmd | t);                   /* save state */
        Write (MA, MB, WR);
        PC = Incr_addr (MA);
        break;

/* JMS: opcode 010 - api_usmd records whether usmd = 1 at start of API cycle */

    case 005:                                           /* JMS, indir */
        if (Ia (MA, &MA, 0))
            break;
    case 004:                                           /* JMS, dir */
        INDEX (IR, MA);
        PCQ_ENTRY;
#if defined (PDP15)                                     /* PDP15 */
        if (!usmd)                                      /* "free instruction" */
            ion_defer = 1;
#endif
        MB = Jms_word (api_usmd | usmd);                /* save state */
        if (Write (MA, MB, WR))
            break;
        PC = Incr_addr (MA) & AMASK;
        break;

/* JMP: opcode 60 */

    case 031:                                           /* JMP, indir */
        if (Ia (MA, &MA, 1))
            break;
        INDEX (IR, MA);
        PCQ_ENTRY;                                      /* save old PC */
        PC = MA & AMASK;
        break;

/* JMP direct - check for idle */

    case 030:                                           /* JMP, dir */
        INDEX (IR, MA);
        PCQ_ENTRY;                                      /* save old PC */
        if (sim_idle_enab) {                            /* idling enabled? */
            t_bool iof = (ion_inh != 0) ||              /* IOF if inhibited */
                ((ion == 0) && (api_enb == 0));         /* or PI and api off */
            if (((MA ^ (PC - 2)) & AMASK) == 0) {       /* 1) JMP *-1? */
                if (iof && (last_IR == OP_KSF) &&       /*    iof, prv KSF, */
                    !TST_INT (TTI))                     /*    no TTI flag? */
                    sim_idle (0, FALSE);                /* we're idle */
                }
            else if (((MA ^ (PC - 1)) & AMASK) == 0) {  /* 2) JMP *? */
                if (iof)                                /*    iof? inf loop */
                    reason = STOP_LOOP;
                else sim_idle (0, FALSE);               /*    ion? idle */                
                }
            }                                           /* end idle */
        PC = MA & AMASK;
        break;

/* OPR: opcode 74 */

    case 037:                                           /* OPR, indir */
        LAC = (LAC & LINK) | IR;                        /* LAW */
        break;

    case 036:                                           /* OPR, dir */
        skp = 0;                                        /* assume no skip */
        switch ((IR >> 6) & 017) {                      /* decode IR<8:11> */
        case 0:                                         /* nop */
            break;
        case 1:                                         /* SMA */
            if ((LAC & SIGN) != 0)
                skp = 1;
            break;
        case 2:                                         /* SZA */
            if ((LAC & DMASK) == 0)
                skp = 1;
            break;
        case 3:                                         /* SZA | SMA */
            if (((LAC & DMASK) == 0) || ((LAC & SIGN) != 0))
                skp = 1; 
            break;
        case 4:                                         /* SNL */
            if (LAC >= LINK)
                skp = 1;
            break;
        case 5:                                         /* SNL | SMA */
            if (LAC >= SIGN)
                skp = 1;
            break;
        case 6:                                         /* SNL | SZA */
            if ((LAC >= LINK) || (LAC == 0))
                skp = 1;
            break;
        case 7:                                         /* SNL | SZA | SMA */
            if ((LAC >= SIGN) || (LAC == 0))
                skp = 1;
            break;
        case 010:                                       /* SKP */
            skp = 1;
            break;
        case 011:                                       /* SPA */
            if ((LAC & SIGN) == 0)
                skp = 1;
            break;
        case 012:                                       /* SNA */
            if ((LAC & DMASK) != 0)
                skp = 1;
            break;
        case 013:                                       /* SNA & SPA */
            if (((LAC & DMASK) != 0) && ((LAC & SIGN) == 0))
                skp = 1;
            break;
        case 014:                                       /* SZL */
            if (LAC < LINK)
                skp = 1;
            break;
        case 015:                                       /* SZL & SPA */
            if (LAC < SIGN)
                skp = 1;
            break;
        case 016:                                       /* SZL & SNA */
            if ((LAC < LINK) && (LAC != 0))
                skp = 1;
            break;
        case 017:                                       /* SZL & SNA & SPA */
            if ((LAC < SIGN) && (LAC != 0))
                skp = 1;
            break;
            }                                           /* end switch skips */

        switch (((IR >> 9) & 014) | (IR & 03)) {        /* IR<5:6,16:17> */
        case 0:                                         /* NOP */
            break;
        case 1:                                         /* CMA */
            LAC = LAC ^ DMASK;
            break;
        case 2:                                         /* CML */
            LAC = LAC ^ LINK;
            break;
        case 3:                                         /* CML CMA */
            LAC = LAC ^ LACMASK;
            break;
        case 4:                                         /* CLL */
            LAC = LAC & DMASK;
            break;
        case 5:                                         /* CLL CMA */
            LAC = (LAC & DMASK) ^ DMASK;
            break;
        case 6:                                         /* CLL CML = STL */
            LAC = LAC | LINK;
            break;
        case 7:                                         /* CLL CML CMA */
            LAC = (LAC | LINK) ^ DMASK;
            break;
        case 010:                                       /* CLA */
            LAC = LAC & LINK;
            break;
        case 011:                                       /* CLA CMA = STA */
            LAC = LAC | DMASK;
            break;
        case 012:                                       /* CLA CML */
            LAC = (LAC & LINK) ^ LINK;
            break;
        case 013:                                       /* CLA CML CMA */
            LAC = (LAC | DMASK) ^ LINK;
            break;
        case 014:                                       /* CLA CLL */
            LAC = 0;
            break;
        case 015:                                       /* CLA CLL CMA */
            LAC = DMASK;
            break;
        case 016:                                       /* CLA CLL CML */
            LAC = LINK;
            break;
        case 017:                                       /* CLA CLL CML CMA */
            LAC = LACMASK;
            break;
            }                                           /* end decode */

        if (IR & 0000004) {                             /* OAS */
#if defined (PDP9) || defined (PDP15)
            if (usmd)                                   /* trap if usmd */
                prvn = trap_pending = 1;
            else if (!api_usmd)                         /* nop if api_usmd */
#endif
                LAC = LAC | SR;
            }

        switch (((IR >> 8) & 04) | ((IR >> 3) & 03)) {  /* decode IR<7,13:14> */
        case 1:                                         /* RAL */
            LAC = ((LAC << 1) | (LAC >> 18)) & LACMASK;
            break;
        case 2:                                         /* RAR */
            LAC = ((LAC >> 1) | (LAC << 18)) & LACMASK;
            break;
        case 3:                                         /* RAL RAR */
#if defined (PDP15)                                     /* PDP-15 */
            LAC = (LAC + 1) & LACMASK;                  /* IAC */
#else                                                   /* PDP-4,-7,-9 */
            reason = stop_inst;                         /* undefined */
#endif
            break;
        case 5:                                         /* RTL */
            LAC = ((LAC << 2) | (LAC >> 17)) & LACMASK;
            break;
        case 6:                                         /* RTR */
            LAC = ((LAC >> 2) | (LAC << 17)) & LACMASK;
            break;
        case 7:                                         /* RTL RTR */
#if defined (PDP15)                                     /* PDP-15 */
            LAC = ((LAC >> 9) & 0777) | ((LAC & 0777) << 9) |
                (LAC & LINK);                           /* BSW */
#else                                                   /* PDP-4,-7,-9 */
            reason = stop_inst;                         /* undefined */
#endif
            break;
            }                                           /* end switch rotate */

        if (IR & 0000040) {                             /* HLT */
            if (usmd)                                   /* trap if usmd */
                prvn = trap_pending = 1;
            else if (!api_usmd)                         /* nop if api_usmd */
                reason = STOP_HALT;
            }
        if (skp)                                        /* if skip, inc PC */
            PC = Incr_addr (PC);
        break;                                          /* end OPR */

/* EAE: opcode 64 

   The EAE is microprogrammed to execute variable length signed and
   unsigned shift, multiply, divide, and normalize.  Most commands are
   controlled by a six bit step counter (SC).  In the hardware, the step
   counter is complemented on load and then counted up to zero; timing
   guarantees an initial increment, which completes the two's complement
   load.  In the simulator, the SC is loaded normally and then counted
   down to zero; the read SC command compensates. */

    case 033: case 032:                                 /* EAE */
        if (cpu_unit.flags & UNIT_NOEAE)                /* disabled? */
            break;
        if (IR & 0020000)                               /* IR<4>? AC0 to L */
            LAC = ((LAC << 1) & LINK) | (LAC & DMASK);
        if (IR & 0010000)                               /* IR<5>? clear MQ */
            MQ = 0;
        if ((IR & 0004000) && (LAC & SIGN))             /* IR<6> and minus? */
            eae_ac_sign = LINK;                         /* set eae_ac_sign */
        else eae_ac_sign = 0;                           /* if not, unsigned */
        if (IR & 0002000)                               /* IR<7>? or AC */
            MQ = (MQ | LAC) & DMASK;
        else if (eae_ac_sign)                           /* if not, |AC| */
            LAC = LAC ^ DMASK;
        if (IR & 0001000)                               /* IR<8>? clear AC */
            LAC = LAC & LINK;
        link_init = LAC & LINK;                         /* link temporary */
        fill = link_init? DMASK: 0;                     /* fill = link */
        esc = IR & 077;                                 /* get eff SC */
        switch ((IR >> 6) & 07) {                       /* case on IR<9:11> */

        case 0:                                         /* setup */
            if (IR & 04)                                /* IR<15>? ~MQ */
                MQ = MQ ^ DMASK;
            if (IR & 02)                                /* IR<16>? or MQ */
                LAC = LAC | MQ;
            if (IR & 01)                                /* IR<17>? or SC */
                LAC = LAC | ((-SC) & 077);
            break;

/* Multiply uses a shift and add algorithm.  The PDP-15, unlike prior
   implementations, factors IR<6> (signed multiply) into the calculation
   of the result sign. */

        case 1:                                         /* multiply */
            if (Read (PC, &MB, FE))                     /* get next word */
                break;
            PC = Incr_addr (PC);                        /* increment PC */
            if (eae_ac_sign)                            /* EAE AC sign? ~MQ */
                MQ = MQ ^ DMASK;
            LAC = LAC & DMASK;                          /* clear link */
            SC = esc;                                   /* init SC */
            do {                                        /* loop */
                if (MQ & 1)                             /* MQ<17>? add */
                    LAC = LAC + MB;
                MQ = (MQ >> 1) | ((LAC & 1) << 17);
                LAC = LAC >> 1;                         /* shift AC'MQ right */
                SC = (SC - 1) & 077;                    /* decrement SC */
                } while (SC != 0);                      /* until SC = 0 */
#if defined (PDP15)
            if ((IR & 0004000) && (eae_ac_sign ^ link_init)) {
#else
            if (eae_ac_sign ^ link_init) {              /* result negative? */
#endif
                LAC = LAC ^ DMASK;
                MQ = MQ ^ DMASK;
                }
            break;

/* Divide uses a non-restoring divide.  Divide uses a subtract and shift
   algorithm.  The quotient is generated in true form.  The PDP-15, unlike
   prior implementations, factors IR<6> (signed divide) into the calculation
   of the result sign. */

        case 3:                                         /* divide */
            if (Read (PC, &MB, FE))                     /* get next word */
                break;
            PC = Incr_addr (PC);                        /* increment PC */
            if (eae_ac_sign)                            /* EAE AC sign? ~MQ */
                MQ = MQ ^ DMASK;
            if ((LAC & DMASK) >= MB) {                  /* overflow? */
                LAC = (LAC - MB) | LINK;                /* set link */
                break;
                }
            LAC = LAC & DMASK;                          /* clear link */
            t = 0;                                      /* init loop */
            SC = esc;                                   /* init SC */
            do {                                        /* loop */
                if (t)
                    LAC = (LAC + MB) & LACMASK;
                else LAC = (LAC - MB) & LACMASK;
                t = (LAC >> 18) & 1;                    /* quotient bit */
                if (SC > 1)                             /* skip if last */
                     LAC =((LAC << 1) | (MQ >> 17)) & LACMASK;
                MQ = ((MQ << 1) | (t ^ 1)) & DMASK;     /* shift in quo bit */
                SC = (SC - 1) & 077;                    /* decrement SC */
                } while (SC != 0);                      /* until SC = 0 */
            if (t)
                LAC = (LAC + MB) & LACMASK;
            if (eae_ac_sign)                            /* sgn rem = sgn divd */
                LAC = LAC ^ DMASK;
#if defined (PDP15)
            if ((IR & 0004000) && (eae_ac_sign ^ link_init))
#else
            if (eae_ac_sign ^ link_init)                /* result negative? */
#endif
                MQ = MQ ^ DMASK;
            break;

/* EAE shifts, whether left or right, fill from the link.  If the
   operand sign has been copied to the link, this provides correct
   sign extension for one's complement numbers. */

        case 4:                                         /* normalize */
#if defined (PDP15)
            if (!usmd)                                  /* free instructions */
                ion_defer = 2;
#endif
            for (SC = esc; ((LAC & SIGN) == ((LAC << 1) & SIGN)); ) {
                LAC = (LAC << 1) | ((MQ >> 17) & 1);
                MQ = (MQ << 1) | (link_init >> 18);
                SC = (SC - 1) & 077;
                if (SC == 0)
                    break;
                }
            LAC = link_init | (LAC & DMASK);            /* trim AC, restore L */
            MQ = MQ & DMASK;                            /* trim MQ */
            SC = SC & 077;                              /* trim SC */
            break;

        case 5:                                         /* long right shift */
            if (esc < 18) {
                MQ = ((LAC << (18 - esc)) | (MQ >> esc)) & DMASK;
                LAC = ((fill << (18 - esc)) | (LAC >> esc)) & LACMASK;
                }
            else {
                if (esc < 36)
                     MQ = ((fill << (36 - esc)) | (LAC >> (esc - 18))) & DMASK;
                else MQ = fill;
                LAC = link_init | fill;
                }
            SC = 0;                                     /* clear step count */
            break;

        case 6:                                         /* long left shift */
            if (esc < 18) {
                LAC = link_init | (((LAC << esc) | (MQ >> (18 - esc))) & DMASK);
                MQ = ((MQ << esc) | (fill >> (18 - esc))) & DMASK;
                }
            else {
                if (esc < 36)
                    LAC = link_init | (((MQ << (esc - 18)) | (fill >> (36 - esc))) & DMASK);
                else LAC = link_init | fill;
                MQ = fill;
                }
            SC = 0;                                     /* clear step count */
            break;

        case 7:                                         /* AC left shift */
            if (esc < 18)
                LAC = link_init | (((LAC << esc) | (fill >> (18 - esc))) & DMASK);
            else LAC = link_init | fill;
            SC = 0;                                     /* clear step count */
            break;
            }                                           /* end switch IR */
        break;                                          /* end case EAE */

/* PDP-15 index operates: opcode 72 */

    case 035:                                           /* index operates */

#if defined (PDP15)
        t = (IR & 0400)? (IR | 0777000): (IR & 0377);   /* sext immediate */
        switch ((IR >> 9) & 017) {                      /* case on IR<5:8> */
        case 001:                                       /* PAX */
            XR = LAC & DMASK;
            break;
        case 002:                                       /* PAL */
            LR = LAC & DMASK;
            break;
        case 003:                                       /* AAC */
            LAC = (LAC & LINK) | ((LAC + t) & DMASK);
            break;
        case 004:                                       /* PXA */
            LAC = (LAC & LINK) | XR;
            break;
        case 005:                                       /* AXS */
            XR = (XR + t) & DMASK;
            if (SEXT (XR) >= SEXT (LR))
                PC = Incr_addr (PC);
            break;
        case 006:                                       /* PXL */
            LR = XR;
            break;
        case 010:                                       /* PLA */
            LAC = (LAC & LINK) | LR;
            break;
        case 011:                                       /* PLX */
            XR = LR;
            break;
        case 014:                                       /* CLAC */
            LAC = LAC & LINK;
            break;
        case 015:                                       /* CLX */
            XR = 0;
            break;
        case 016:                                       /* CLLR */
            LR = 0;
            break;
        case 017:                                       /* AXR */
            XR = (XR + t) & DMASK;
            break;
            }                                           /* end switch IR */
        break;                                          /* end case */
#endif

/* IOT: opcode 70 

   The 18b PDP's have different definitions of various control IOT's.

   IOT          PDP-4           PDP-7           PDP-9           PDP-15

   700002       IOF             IOF             IOF             IOF
   700022       undefined       undefined       undefined       ORMM (XVM)
   700042       ION             ION             ION             ION
   700024       undefined       undefined       undefined       LDMM (XVM)
   700062       undefined       ITON            undefined       undefined
   701701       undefined       undefined       MPSK            MPSK
   701741       undefined       undefined       MPSNE           MPSNE
   701702       undefined       undefined       MPCV            MPCV
   701722       undefined       undefined       undefined       MPRC (XVM)
   701742       undefined       undefined       MPEU            MPEU
   701704       undefined       undefined       MPLD            MPLD
   701724       undefined       undefined       undefined       MPLR (KT15, XVM)
   701744       undefined       undefined       MPCNE           MPCNE
   701764       undefined       undefined       undefined       IPFH (XVM)
   703201       undefined       undefined       PFSF            PFSF
   703301       undefined       TTS             TTS             TTS
   703341       undefined       SKP7            SKP7            SPCO
   703302       undefined       CAF             CAF             CAF
   703304       undefined       undefined       DBK             DBK
   703344       undefined       undefined       DBR             DBR
   705501       undefined       undefined       SPI             SPI
   705521       undefined       undefined       undefined       ENB
   705502       undefined       undefined       RPL             RPL
   705522       undefined       undefined       undefined       INH
   705504       undefined       undefined       ISA             ISA
   707701       undefined       SEM             SEM             undefined
   707741       undefined       undefined       undefined       SKP15
   707761       undefined       undefined       undefined       SBA
   707702       undefined       EEM             EEM             undefined
   707742       undefined       EMIR            EMIR            RES
   707762       undefined       undefined       undefined       DBA
   707704       undefined       LEM             LEM             undefined
   707764       undefined       undefined       undefined       EBA */

    case 034:                                           /* IOT */
#if defined (PDP15)
        if (IR & 0010000) {                             /* floating point? */
            reason = fp15 (IR);                         /* process */
            break;
            }
#endif
        if ((api_usmd | usmd) &&                        /* user, not XVM UIOT? */
            (!XVM || !(MMR & MM_UIOT))) {
            if (usmd)                                   /* trap if user */
                prvn = trap_pending = 1;
            break;                                      /* nop if api_usmd */
            }
        device = (IR >> 6) & 077;                       /* device = IR<6:11> */
        pulse = IR & 067;                               /* pulse = IR<12:17> */
        if (IR & 0000010)                               /* clear AC? */
            LAC = LAC & LINK;
        iot_data = LAC & DMASK;                         /* AC unchanged */

/* PDP-4 system IOT's */

#if defined (PDP4)
        switch (device) {                               /* decode IR<6:11> */

        case 0:                                         /* CPU and clock */
            if (pulse == 002)                           /* IOF */
                ion = 0;
            else if (pulse == 042)                      /* ION */
                ion = ion_defer = 1;
            else iot_data = clk (device, pulse, iot_data);
            break;
#endif

/* PDP-7 system IOT's */

#if defined (PDP7)
        switch (device) {                               /* decode IR<6:11> */

        case 0:                                         /* CPU and clock */
            if (pulse == 002)                           /* IOF */
                ion = 0;
            else if (pulse == 042)                      /* ION */
                ion = ion_defer = 1;
            else if (pulse == 062)                      /* ITON */
                usmd = usmd_buf = ion = ion_defer = 1;
            else iot_data = clk (device, pulse, iot_data);
            break;

        case 033:                                       /* CPU control */
            if ((pulse == 001) || (pulse == 041))
                PC = Incr_addr (PC);
            else if (pulse == 002)                      /* CAF - skip CPU */
                reset_all (1);
            break;

        case 077:                                       /* extended memory */
            if ((pulse == 001) && memm)
                PC = Incr_addr (PC);
            else if (pulse == 002)                      /* EEM */
                memm = 1;
            else if (pulse == 042)                      /* EMIR */
                memm = emir_pending = 1;                /* ext on, restore */
            else if (pulse == 004)                      /* LEM */
                memm = 0;
            break;
#endif

/* PDP-9 system IOT's */

#if defined (PDP9)
        ion_defer = 1;                                  /* delay interrupts */
        usmd_defer = 1;                                 /* defer load user */
        switch (device) {                               /* decode IR<6:11> */
 
       case 000:                                       /* CPU and clock */
            if (pulse == 002)                           /* IOF */
                ion = 0;
            else if (pulse == 042)                      /* ION */
                ion = ion_defer = 1;
            else iot_data = clk (device, pulse, iot_data);
            break;

        case 017:                                       /* mem protection */
            if (PROT) {                                 /* enabled? */
                if ((pulse == 001) && prvn)             /* MPSK */
                    PC = Incr_addr (PC);
                else if ((pulse == 041) && nexm)        /* MPSNE */
                    PC = Incr_addr (PC);
                else if (pulse == 002)                  /* MPCV */
                    prvn = 0;
                else if (pulse == 042)                  /* MPEU */
                    usmd_buf = 1;
                else if (pulse == 004)                  /* MPLD */
                    BR = LAC & BRMASK;
                else if (pulse == 044)                  /* MPCNE */
                    nexm = 0;
                }
            else reason = stop_inst;
            break;

        case 032:                                       /* power fail */
            if ((pulse == 001) && (TST_INT (PWRFL)))
                 PC = Incr_addr (PC);
            break;

        case 033:                                       /* CPU control */
            if ((pulse == 001) || (pulse == 041))
                PC = Incr_addr (PC);
            else if (pulse == 002) {                    /* CAF */
                reset_all (1);                          /* reset all exc CPU */
                cpu_caf ();                             /* CAF to CPU */
                }
            else if (pulse == 044)                      /* DBR */
                rest_pending = 1;
            if (((cpu_unit.flags & UNIT_NOAPI) == 0) && (pulse & 004)) {
                int32 t = api_ffo[api_act & 0377];
                api_act = api_act & ~(API_ML0 >> t);
                }
            break;

        case 055:                                       /* API control */
            if (cpu_unit.flags & UNIT_NOAPI)
                reason = stop_inst;
            else if (pulse == 001) {                    /* SPI */
                if (((LAC & SIGN) && api_enb) ||
                    ((LAC & 0377) > api_act))
                    iot_data = iot_data | IOT_SKP;
                }
            else if (pulse == 002) {                    /* RPL */
                iot_data = iot_data | (api_enb << 17) |
                    (api_req << 8) | api_act;
                }
            else if (pulse == 004) {                    /* ISA */
                api_enb = (iot_data & SIGN)? 1: 0;
                api_req = api_req | ((LAC >> 8) & 017); /* swre levels only */
                api_act = api_act | (LAC & 0377);
                }
            break;

        case 077:                                       /* extended memory */
            if ((pulse == 001) && memm)
                PC = Incr_addr (PC);
            else if (pulse == 002)                      /* EEM */
                memm = 1;
            else if (pulse == 042)                      /* EMIR */
                memm = emir_pending = 1;                /* ext on, restore */
            else if (pulse == 004)                      /* LEM */
                memm = 0;
            break;
#endif

/* PDP-15 system IOT's - includes "re-entrancy ECO" ENB/INH as standard */

#if defined (PDP15)
        ion_defer = 1;                                  /* delay interrupts */
        usmd_defer = 1;                                 /* defer load user */
        switch (device) {                               /* decode IR<6:11> */

        case 000:                                       /* CPU and clock */
            if (pulse == 002)                           /* IOF */
                ion = 0;
            else if (pulse == 042)                      /* ION */
                ion = ion_defer = 1;
            else if (XVM && (pulse == 022))             /* ORMM/RDMM */
                iot_data = MMR;
            else if (XVM && (pulse == 024))             /* LDMM */
                MMR = iot_data;
            else iot_data = clk (device, pulse, iot_data);
            break;

        case 017:                                       /* mem protection */
            if (PROT) {                                 /* enabled? */
                t = XVM? BRMASK_XVM: BRMASK;
                if ((pulse == 001) && prvn)             /* MPSK */
                    PC = Incr_addr (PC);
                else if ((pulse == 041) && nexm)        /* MPSNE */
                    PC = Incr_addr (PC);
                else if (pulse == 002)                  /* MPCV */
                    prvn = 0;
                else if (pulse == 042)                  /* MPEU */
                     usmd_buf = 1;
                else if (XVM && (pulse == 062))         /* RDCLK */
                    iot_data = clk_task_upd (TRUE);
                else if (pulse == 004)                  /* MPLD */
                    BR = LAC & t;
                else if (RELOC && (pulse == 024))       /* MPLR */
                    RR = LAC & t;
                else if (pulse == 044)                  /* MPCNE */
                    nexm = 0;
                }
            else reason = stop_inst;
            break;

        case 032:                                       /* power fail */
            if ((pulse == 001) && (TST_INT (PWRFL)))
                 PC = Incr_addr (PC);
            break;

        case 033:                                       /* CPU control */
            if ((pulse == 001) || (pulse == 041))
                PC = Incr_addr (PC);
            else if (pulse == 002) {                    /* CAF */
                reset_all (2);                          /* reset all exc CPU, FP15 */
                cpu_caf ();                             /* CAF to CPU */
                }
            else if (pulse == 044)                      /* DBR */
                rest_pending = 1;
            if (((cpu_unit.flags & UNIT_NOAPI) == 0) && (pulse & 004)) {
                int32 t = api_ffo[api_act & 0377];
                api_act = api_act & ~(API_ML0 >> t);
                }
            break;

        case 055:                                       /* API control */
            if (cpu_unit.flags & UNIT_NOAPI)
                reason = stop_inst;
            else if (pulse == 001) {                    /* SPI */
                if (((LAC & SIGN) && api_enb) ||
                    ((LAC & 0377) > api_act))
                    iot_data = iot_data | IOT_SKP;
                }
            else if (pulse == 002) {                    /* RPL */
                iot_data = iot_data | (api_enb << 17) |
                    (api_req << 8) | api_act;
                }
            else if (pulse == 004) {                    /* ISA */
                api_enb = (iot_data & SIGN)? 1: 0;
                api_req = api_req | ((LAC >> 8) & 017); /* swre levels only */
                api_act = api_act | (LAC & 0377);
                }
            else if (pulse == 021)                      /* ENB */
                ion_inh = 0;
            else if (pulse == 022)                      /* INH */
                ion_inh = 1;
            break;

        case 077:                                       /* bank addressing */
            if ((pulse == 041) || ((pulse == 061) && memm))
                 PC = Incr_addr (PC);                   /* SKP15, SBA */
            else if (pulse == 042)                      /* RES */
                rest_pending = 1;
            else if (pulse == 062)                      /* DBA */
                memm = 0;
            else if (pulse == 064)                      /* EBA */
                memm = 1;
            break;
#endif

/* IOT, continued */

        default:                                        /* devices */
            if (dev_tab[device])                        /* defined? */
                iot_data = dev_tab[device] (device, pulse, iot_data);
            else reason = stop_inst;                    /* stop on flag */
            break;
            }                                           /* end switch device */

        LAC = LAC | (iot_data & DMASK);
        if (iot_data & IOT_SKP)
            PC = Incr_addr (PC);
        if (iot_data >= IOT_REASON)
            reason = iot_data >> IOT_V_REASON;
        api_int = api_eval (&int_pend);                 /* eval API */
        break;                                          /* end case IOT */
        }                                               /* end switch opcode */

    api_usmd = 0;                                       /* API cycle over */
    last_IR = IR;                                       /* save IR for next */
    }                                                   /* end while */

/* Simulation halted */

iors = upd_iors ();                                     /* get IORS */
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}

/* Evaluate API */

int32 api_eval (int32 *pend)
{
int32 i, hi;

*pend = 0;                                              /* assume no intr */
#if defined (PDP15)                                     /* PDP15 only */
if (ion_inh)                                            /* inhibited? */
    return 0;
#endif
for (i = 0; i < API_HLVL+1; i++) {                      /* any intr? */
    if (int_hwre[i])
        *pend = 1;
    }
if (api_enb == 0)                                       /* off? no req */
    return 0;
api_req = api_req & ~(API_ML0|API_ML1|API_ML2|API_ML3); /* clr req<0:3> */
for (i = 0; i < API_HLVL; i++) {                        /* loop thru levels */
    if (int_hwre[i])                                    /* req on level? */
        api_req = api_req | (API_ML0 >> i);             /* set api req */
    }
hi = api_ffo[api_req & 0377];                           /* find hi req */
if (hi < api_ffo[api_act & 0377])
    return (hi + 1);
return 0;
}

/* Process IORS instruction */

int32 upd_iors (void)
{
int32 d, p;

d = (ion? IOS_ION: 0);                                  /* ION */
for (p = 0; dev_iors[p] != NULL; p++)                   /* loop thru table */
    d = d | dev_iors[p]();                              /* OR in results */
return d;
}

#if defined (PDP4) || defined (PDP7)

/* Read, write, indirect, increment routines
   On the PDP-4 and PDP-7,
        There are autoincrement locations in every field.  If a field
                does not exist, it is impossible to generate an
                autoincrement reference (all instructions are CAL).
        Indirect addressing range is determined by extend mode.
        JMP I with EMIR pending can only clear extend
        There is no memory protection, nxm reads zero and ignores writes. */

t_stat Read (int32 ma, int32 *dat, int32 cyc)
{
ma = ma & AMASK;
if (MEM_ADDR_OK (ma))
    *dat = M[ma] & DMASK;
else *dat = 0;
return MM_OK;
}

t_stat Write (int32 ma, int32 dat, int32 cyc)
{
ma = ma & AMASK;
if (MEM_ADDR_OK (ma))
    M[ma] = dat & DMASK;
return MM_OK;
}

t_stat Ia (int32 ma, int32 *ea, t_bool jmp)
{
int32 t;
t_stat sta = MM_OK;

if ((ma & B_DAMASK & ~07) == 010) {                     /* autoindex? */
    Read (ma, &t, DF);                                  /* add 1 before use */
    t = (t + 1) & DMASK;
    sta = Write (ma, t, DF);
    }
else sta = Read (ma, &t, DF);                           /* fetch indirect */
if (jmp) {                                              /* jmp i? */
    if (emir_pending && (((t >> 16) & 1) == 0))
        memm = 0;
    emir_pending = rest_pending = 0;
    }
if (memm)                                               /* extend? 15b ia */
    *ea = t & IAMASK;
else *ea = (ma & B_EPCMASK) | (t & B_DAMASK);           /* bank-rel ia */
return sta;
}

int32 Incr_addr (int32 ma)
{
return ((ma & B_EPCMASK) | ((ma + 1) & B_DAMASK));
}

int32 Jms_word (int32 t)
{
return (((LAC & LINK) >> 1) | ((memm & 1) << 16) |
    ((t & 1) << 15) | (PC & IAMASK));
}

#endif

#if defined (PDP9)

/* Read, write, indirect, increment routines
   On the PDP-9,
        The autoincrement registers are in field zero only.  Regardless
                of extend mode, indirect addressing through 00010-00017
                will access absolute locations 00010-00017.
        Indirect addressing range is determined by extend mode.  If
                extend mode is off, and autoincrementing is used, the
                resolved address is in bank 0 (KG09B maintenance manual).
        JMP I with EMIR pending can only clear extend
        JMP I with DBK pending restores L, user mode, extend mode
        Memory protection is implemented for foreground/background operation. */

t_stat Read (int32 ma, int32 *dat, int32 cyc)
{
ma = ma & AMASK;
if (usmd) {                                             /* user mode? */
    if (!MEM_ADDR_OK (ma)) {                            /* nxm? */
        nexm = prvn = trap_pending = 1;                 /* set flags, trap */
        *dat = 0;
        return MM_ERR;
        }
    if ((cyc != DF) && (ma < BR)) {                     /* boundary viol? */
        prvn = trap_pending = 1;                        /* set flag, trap */
        *dat = 0;
        return MM_ERR;
        }
    }
if (MEM_ADDR_OK (ma))                                   /* valid mem? ok */
    *dat = M[ma] & DMASK;
else {
    *dat = 0;                                           /* set flag, no trap */
    nexm = 1;
    }
return MM_OK;
}

t_stat Write (int32 ma, int32 dat, int32 cyc)
{
ma = ma & AMASK;
if (usmd) {
    if (!MEM_ADDR_OK (ma)) {                            /* nxm? */
        nexm = prvn = trap_pending = 1;                 /* set flags, trap */
        return MM_ERR;
        }
    if ((cyc != DF) && (ma < BR)) {                     /* boundary viol? */
        prvn = trap_pending = 1;                        /* set flag, trap */
        return MM_ERR;
        }
    }
if (MEM_ADDR_OK (ma))                                   /* valid mem? ok */
    M[ma] = dat & DMASK;
else nexm = 1;                                          /* set flag, no trap */
return MM_OK;
}

t_stat Ia (int32 ma, int32 *ea, t_bool jmp)
{
int32 t;
t_stat sta = MM_OK;

if ((ma & B_DAMASK & ~07) == 010) {                     /* autoindex? */
    ma = ma & 017;                                      /* always in bank 0 */
    Read (ma, &t, DF);                                  /* +1 before use */
    t = (t + 1) & DMASK;
    sta = Write (ma, t, DF);
    }
else sta = Read (ma, &t, DF);
if (jmp) {                                              /* jmp i? */
    if (emir_pending && (((t >> 16) & 1) == 0))
        memm = 0;
    if (rest_pending) {                                 /* restore pending? */
        LAC = ((t << 1) & LINK) | (LAC & DMASK);        /* restore L */
        memm = (t >> 16) & 1;                           /* restore extend */
        usmd = usmd_buf = (t >> 15) & 1;                /* restore user */
        }
    emir_pending = rest_pending = 0;
    }
if (memm)                                               /* extend? 15b ia */
    *ea = t & IAMASK;
else *ea = (ma & B_EPCMASK) | (t & B_DAMASK);           /* bank-rel ia */
return sta;
}

int32 Incr_addr (int32 ma)
{
return ((ma & B_EPCMASK) | ((ma + 1) & B_DAMASK));
}

int32 Jms_word (int32 t)
{
return (((LAC & LINK) >> 1) | ((memm & 1) << 16) |
    ((t & 1) << 15) | (PC & IAMASK));
}

#endif

#if defined (PDP15)

/* Read, write, indirect, increment routines
   On the PDP-15,
        The autoincrement registers are in page zero only.  Regardless
                of bank mode, indirect addressing through 00010-00017
                will access absolute locations 00010-00017.
        Indirect addressing range is determined by autoincrementing.
        Any indirect can trigger a restore.
        Memory protection is implemented for foreground/background operation.
        Read and write mask addresses to 17b except for XVM systems */

t_stat Read (int32 ma, int32 *dat, int32 cyc)
{
int32 pa;

if (usmd) {                                             /* user mode? */
    if (XVM)                                            /* XVM relocation? */
        pa = RelocXVM (ma, REL_R);
    else if (RELOC)                                     /* PDP-15 relocation? */
        pa = Reloc15 (ma, REL_R);
    else pa = Prot15 (ma, cyc == FE);                   /* PDP-15 prot, fetch only */
    if (pa < 0) {                                       /* error? */
        *dat = 0;
        return MM_ERR;
        }
    }
else pa = ma & AMASK;                                   /* no prot or reloc */
if (MEM_ADDR_OK (pa))                                   /* valid mem? ok */
    *dat = M[pa] & DMASK;
else {
    nexm = 1;                                           /* set flag, no trap */
    *dat = 0;
    }
return MM_OK;
}

t_stat Write (int32 ma, int32 dat, int32 cyc)
{
int32 pa;

if (usmd) {                                             /* user mode? */
    if (XVM)                                            /* XVM relocation? */
        pa = RelocXVM (ma, REL_W);
    else if (RELOC)                                     /* PDP-15 relocation? */
        pa = Reloc15 (ma, REL_W);
    else pa = Prot15 (ma, cyc != DF);                   /* PDP-15 prot, !defer */
    if (pa < 0)                                         /* error? */
        return MM_ERR;
    }
else pa = ma & AMASK;                                   /* no prot or reloc */
if (MEM_ADDR_OK (pa))                                   /* valid mem? ok */
    M[pa] = dat & DMASK;
else nexm = 1;                                          /* set flag, no trap */
return MM_OK;
}

/* XVM will do 18b defers if user_mode and G_Mode != 0 */

t_stat Ia (int32 ma, int32 *ea, t_bool jmp)
{
int32 gmode, t;
int32 damask = memm? B_DAMASK: P_DAMASK;
static const int32 g_mask[4] = { MM_G_W0, MM_G_W1, MM_G_W2, MM_G_W3 };
t_stat sta = MM_OK;

if ((ma & damask & ~07) == 010) {                       /* autoincrement? */
    ma = ma & 017;                                      /* always in bank 0 */
    Read (ma, &t, DF);                                  /* +1 before use */
    t = (t + 1) & DMASK;
    sta = Write (ma, t, DF);
    }
else sta = Read (ma, &t, DF);
if (rest_pending) {                                     /* restore pending? */
    LAC = ((t << 1) & LINK) | (LAC & DMASK);            /* restore L */
    memm = (t >> 16) & 1;                               /* restore bank */
    usmd = usmd_buf = (t >> 15) & 1;                    /* restore user */
    emir_pending = rest_pending = 0;
    }
gmode = MM_GETGM (MMR);                                 /* get G_mode */
if (usmd && XVM && gmode)                               /* XVM user mode? */
    *ea = t & g_mask[gmode];                            /* mask ia to size */
else if ((ma & damask & ~07) == 010)                    /* autoindex? */
    *ea = t & DMASK;
else *ea = (PC & BLKMASK) | (t & IAMASK);               /* within 32K */
return sta;
}

int32 Incr_addr (int32 ma)
{
if (memm)
    return ((ma & B_EPCMASK) | ((ma + 1) & B_DAMASK));
return ((ma & P_EPCMASK) | ((ma + 1) & P_DAMASK));
}

/* XVM will store all 18b of PC if user mode and G_mode != 0 */

int32 Jms_word (int32 t)
{
if (usmd && XVM && (MMR & MM_GM))
    return PC;
return (((LAC & LINK) >> 1) | ((memm & 1) << 16) |
    ((t & 1) << 15) | (PC & IAMASK));
}

/* PDP-15 protection (KM15 option) */

int32 Prot15 (int32 ma, t_bool bndchk)
{
ma = ma & AMASK;                                        /* 17b addressing */
if (!MEM_ADDR_OK (ma)) {                                /* nxm? */
    nexm = prvn = trap_pending = 1;                     /* set flags, trap */
    return -1;
    }
if (bndchk && (ma < BR)) {                              /* boundary viol? */
    prvn = trap_pending = 1;                            /* set flag, trap */
    return -1;
    }
return ma;                                              /* no relocation */
}

/* PDP-15 relocation and protection (KT15 option) */

int32 Reloc15 (int32 ma, int32 rc)
{
int32 pa;

ma = ma & AMASK;                                        /* 17b addressing */
if (ma > (BR | 0377)) {                                 /* boundary viol? */
    if (rc != REL_C)                                    /* set flag, trap */
        prvn = trap_pending = 1;
    return -1;
    }
pa = (ma + RR) & AMASK;                                 /* relocate address */
if (!MEM_ADDR_OK (pa)) {                                /* nxm? */
    if (rc != REL_C)                                    /* set flags, trap */
        nexm = prvn = trap_pending = 1;
    return -1;
    }
return pa;
}

/* XVM relocation and protection option */

int32 RelocXVM (int32 ma, int32 rc)
{
int32 pa, gmode, slr;
static const int32 g_base[4] = { MM_G_B0, MM_G_B1, MM_G_B2, MM_G_B3 };
static const int32 slr_lnt[4] = { MM_SLR_L0, MM_SLR_L1, MM_SLR_L2, MM_SLR_L3 };

gmode = MM_GETGM (MMR);                                 /* get G_mode */
slr = MM_GETSLR (MMR);                                  /* get segment length */
if (MMR & MM_RDIS)                                      /* reloc disabled? */
    pa = ma;
else if ((MMR & MM_SH) &&                               /* shared enabled and */
    (ma >= g_base[gmode]) &&                            /* >= shared base and */
    (ma < (g_base[gmode] + slr_lnt[slr]))) {            /* < shared end? */
    if (ma & 017400) {                                  /* ESAS? */
        if ((rc == REL_W) && (MMR & MM_WP)) {           /* write and protected? */
            prvn = trap_pending = 1;                    /* set flag, trap */
            return -1;
            }
        pa = (((MMR & MM_SBR_MASK) << 8) + ma) & DMASK; /* ESAS reloc */
        }
    else pa = RR + (ma & 0377);                         /* no, ISAS reloc */
    }
else {
    if (ma > (BR | 0377)) {                             /* normal reloc, viol? */
        if (rc != REL_C)                                /* set flag, trap */
            prvn = trap_pending = 1;
        return -1;
        }
    pa = (RR + ma) & DMASK;                             /* relocate address */
    }
if (!MEM_ADDR_OK (pa)) {                                /* nxm? */
    if (rc != REL_C)                                    /* set flags, trap */
        nexm = prvn = trap_pending = 1;
    return -1;
    }
return pa;
}

#endif

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
LAC = 0;
MQ = 0;
SC = 0;
eae_ac_sign = 0;
ion = ion_defer = ion_inh = 0;
CLR_INT (PWRFL);
api_enb = api_req = api_act = 0;
BR = 0;
RR = 0;
MMR = 0;
usmd = usmd_buf = usmd_defer = 0;
memm = memm_init;
nexm = prvn = trap_pending = 0;
emir_pending = rest_pending = 0;
if (M == NULL)
    M = (int32 *) calloc (MEMSIZE, sizeof (int32));
if (M == NULL)
    return SCPE_MEM;
pcq_r = find_reg ("PCQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* CAF routine (CPU reset isn't called by CAF) */

void cpu_caf (void)
{
api_enb = api_req = api_act = 0;                    /* reset API system */
nexm = prvn = trap_pending = 0;                     /* reset MM system */
usmd = usmd_buf = usmd_defer = 0;
MMR = 0;
return;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
#if defined (PDP15)
if (usmd && (sw & SWMASK ('V'))) {
    if (XVM)
        addr = RelocXVM (addr, REL_C);
    else if (RELOC)
        addr = Reloc15 (addr, REL_C);
    if (((int32) addr) < 0)
        return STOP_MME;
    }
#endif
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = M[addr] & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
#if defined (PDP15)
if (usmd && (sw & SWMASK ('V'))) {
    if (XVM)
        addr = RelocXVM (addr, REL_C);
    else if (RELOC)
        addr = Reloc15 (addr, REL_C);
    if (((int32) addr) < 0)
        return STOP_MME;
    }
#endif
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

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
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

/* Change device number for a device */

t_stat set_devno (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
uint32 newdev;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
newdev = get_uint (cptr, 8, DEV_MAX - 1, &r);           /* get new */
if ((r != SCPE_OK) || (newdev == dibp->dev))
    return r;
dibp->dev = newdev;                                     /* store */
return SCPE_OK;
}

/* Show device number for a device */

t_stat show_devno (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
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
fprintf (st, "devno=%02o", dibp->dev);
if (dibp->num > 1)
    fprintf (st, "-%2o", dibp->dev + dibp->num - 1);
return SCPE_OK;
}

/* CPU device handler - should never get here! */

int32 bad_dev (int32 dev, int32 pulse, int32 AC)
{
return (SCPE_IERR << IOT_V_REASON) | AC;                /* broken! */
}

/* Build device dispatch table */

t_bool build_dev_tab (void)
{
DEVICE *dptr;
DIB *dibp;
uint32 i, j, p;
static const uint8 std_dev[] =
#if defined (PDP4)
    { 000 };
#elif defined (PDP7)
    { 000, 033, 077 };
#else
    { 000, 017, 033, 055, 077 };
#endif

for (i = 0; i < DEV_MAX; i++) {                         /* clr tables */
    dev_tab[i] = NULL;
    dev_iors[i] = NULL;
    }
for (i = 0; i < ((uint32) sizeof (std_dev)); i++)       /* std entries */
    dev_tab[std_dev[i]] = &bad_dev;
for (i = p = 0; (dptr = sim_devices[i]) != NULL; i++) { /* add devices */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* enabled? */
        if (dibp->iors)                                 /* if IORS, add */
            dev_iors[p++] = dibp->iors;
        for (j = 0; j < dibp->num; j++) {               /* loop thru disp */
            if (dibp->dsp[j]) {                         /* any dispatch? */
                if (dev_tab[dibp->dev + j]) {           /* already filled? */
                    sim_printf ("%s device number conflict at %02o\n",
                            sim_dname (dptr), dibp->dev + j);
                    return TRUE;
                    }
                dev_tab[dibp->dev + j] = dibp->dsp[j];  /* fill */
                }                                       /* end if dsp */
            }                                           /* end for j */
        }                                               /* end if enb */
    }                                                   /* end for i */
return FALSE;
}

/* Set in memory 3-cycle databreak register */

t_stat set_3cyc_reg (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
t_stat r;
int32 newv;

if (cptr == NULL)
    return SCPE_ARG;
newv = (int32) get_uint (cptr, 8, 0777777, &r);
if (r != SCPE_OK)
    return SCPE_ARG;
M[val] = newv;
return SCPE_OK;
}

/* Show in-memory 3-cycle databreak register */

t_stat show_3cyc_reg (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "%s=", (const char *) desc);
fprint_val (st, (t_value) M[val], 8, 18, PV_RZRO);
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
int32 l, j, k, di, lnt;

CONST char *cptr = (CONST char *) desc;
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
fprintf (st, "PC      L AC      MQ      IR\n\n");
for (k = 0; k < lnt; k++) {                             /* print specified */
    h = &hst[(di++) % hst_lnt];                         /* entry pointer */
    if (h->pc & HIST_PC) {                              /* instruction? */
        l = (h->lac >> 18) & 1;                         /* link */
        fprintf (st, "%06o  %o %06o  %06o  ", h->pc & AMASK, l, h->lac & DMASK, h->mq);
        sim_eval[0] = h->ir;
        sim_eval[1] = h->ir1;
        if ((fprint_sym (st, h->pc & AMASK, sim_eval, &cpu_unit, SWMASK ('M'))) > 0)
            fprintf (st, "(undefined) %06o", h->ir);
        }                                               /* end else instruction */
    else if (h->pc & (HIST_API | HIST_PI)) {            /* interrupt event? */
        if (h->pc & HIST_PI)                            /* PI? */
            fprintf (st, "%06o  PI LVL 0-4 =", h->pc & AMASK);
            else fprintf (st, "%06o  API %d LVL 0-4 =", h->pc & AMASK, h->mq);
        for (j = API_HLVL; j >= 0; j--)
            fprintf (st, " %02o", (h->ir >> (j * HIST_V_LVL)) & HIST_M_LVL);
        }
    else continue;                                      /* invalid */
    fputc ('\n', st);                                   /* end line */
    }                                                   /* end for */
return SCPE_OK;
}

/* Record events in history table */

void cpu_inst_hist (int32 addr, int32 inst)
{
t_value word = 0;

hst[hst_p].pc = addr | HIST_PC;
hst[hst_p].ir = inst;
if (cpu_ex (&word, (addr + 1) & AMASK, &cpu_unit, SWMASK ('V')))
    hst[hst_p].ir1 = 0;
else hst[hst_p].ir1 = word;
hst[hst_p].lac = LAC;
hst[hst_p].mq = MQ;
hst_p = (hst_p + 1);
if (hst_p >= hst_lnt)
    hst_p = 0;
return;
}

void cpu_intr_hist (int32 flag, int32 lvl)
{
int32 j;

hst[hst_p].pc = PC | flag;
hst[hst_p].ir = 0;
for (j = 0; j < API_HLVL+1; j++)
    hst[hst_p].ir = (hst[hst_p].ir << HIST_V_LVL) | (int_hwre[j] & HIST_M_LVL);
hst[hst_p].ir1 = 0;
hst[hst_p].lac = 0;
hst[hst_p].mq = lvl;
hst_p = (hst_p + 1);
if (hst_p >= hst_lnt)
    hst_p = 0;
return;
}
