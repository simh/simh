/* hp2100_cpu.c: HP 21xx CPU simulator

   Copyright (c) 1993-2007, Robert M. Supnik

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

   CPU          2114C/2115A/2116C/2100A/1000-M/E/F central processing unit
   MP           12581A/12892B memory protect
   DMA0,DMA1    12607B/12578A/12895A direct memory access controller
   DCPC0,DCPC1  12897B dual channel port controller

   11-Jan-07    JDB     Added 12578A DMA byte packing
   28-Dec-06    JDB     CLC 0 now sends CRS instead of CLC to devices
   26-Dec-06    JDB     Fixed improper IRQ deferral for 21xx CPUs
                        Fixed improper interrupt servicing in resolve
   21-Dec-06    JDB     Added 21xx loader enable/disable support
   16-Dec-06    JDB     Added 2114 and 2115 CPU options.
                        Added support for 12607B (2114) and 12578A (2115/6) DMA
   01-Dec-06    JDB     Added 1000-F CPU option (requires HAVE_INT64)
                        SHOW CPU displays 1000-M/E instead of 21MX-M/E
   16-Oct-06    JDB     Moved ReadF to hp2100_cpu1.c
   12-Oct-06    JDB     Fixed INDMAX off-by-one error in resolve
   26-Sep-06    JDB     Added iotrap parameter to UIG dispatchers for RTE microcode
   12-Sep-06    JDB     iogrp returns NOTE_IOG to recalc interrupts
                        resolve returns NOTE_INDINT to service held-off interrupt
   16-Aug-06    JDB     Added support for future microcode options, future F-Series
   09-Aug-06    JDB     Added double integer microcode, 1000-M/E synonyms
                        Enhanced CPU option validity checking
                        Added DCPC as a synonym for DMA for 21MX simulations
   26-Dec-05    JDB     Improved reporting in dev_conflict
   22-Sep-05    RMS     Fixed declarations (from Sterling Garwood)
   21-Jan-05    JDB     Reorganized CPU option flags
   15-Jan-05    RMS     Split out EAU and MAC instructions
   26-Dec-04    RMS     DMA reset doesn't clear alternate CTL flop (from Dave Bryan)
                        DMA reset shouldn't clear control words (from Dave Bryan)
                        Alternate CTL flop not visible as register (from Dave Bryan)
                        Fixed CBS, SBS, TBS to perform virtual reads
                        Separated A/B from M[0/1] for DMA IO (from Dave Bryan)
                        Fixed bug in JPY (from Dave Bryan)
   25-Dec-04    JDB     Added SET CPU 21MX-M, 21MX-E (21MX defaults to MX-E)
                        TIMER/EXECUTE/DIAG instructions disabled for 21MX-M
                        T-register reflects changes in M-register when halted
   25-Sep-04    JDB     Moved MP into its own device; added MP option jumpers
                        Modified DMA to allow disabling
                        Modified SET CPU 2100/2116 to truncate memory > 32K
                        Added -F switch to SET CPU to force memory truncation
                        Fixed S-register behavior on 2116
                        Fixed LIx/MIx behavior for DMA on 2116 and 2100
                        Fixed LIx/MIx behavior for empty I/O card slots
                        Modified WRU to be REG_HRO
                        Added BRK and DEL to save console settings
                        Fixed use of "unsigned int16" in cpu_reset
                        Modified memory size routine to return SCPE_INCOMP if
                         memory size truncation declined
   20-Jul-04    RMS     Fixed bug in breakpoint test (reported by Dave Bryan)
                        Back up PC on instruction errors (from Dave Bryan)
   14-May-04    RMS     Fixed bugs and added features from Dave Bryan
                        - SBT increments B after store
                        - DMS console map must check dms_enb
                        - SFS x,C and SFC x,C work
                        - MP violation clears automatically on interrupt
                        - SFS/SFC 5 is not gated by protection enabled
                        - DMS enable does not disable mem prot checks
                        - DMS status inconsistent at simulator halt
                        - Examine/deposit are checking wrong addresses
                        - Physical addresses are 20b not 15b
                        - Revised DMS to use memory rather than internal format
                        - Added instruction printout to HALT message
                        - Added M and T internal registers
                        - Added N, S, and U breakpoints
                        Revised IBL facility to conform to microcode
                        Added DMA EDT I/O pseudo-opcode
                        Separated DMA SRQ (service request) from FLG
   12-Mar-03    RMS     Added logical name support
   02-Feb-03    RMS     Fixed last cycle bug in DMA output (found by Mike Gemeny)
   22-Nov-02    RMS     Added 21MX IOP support
   24-Oct-02    RMS     Fixed bugs in IOP and extended instructions
                        Fixed bugs in memory protection and DMS
                        Added clock calibration
   25-Sep-02    RMS     Fixed bug in DMS decode (found by Robert Alan Byer)
   26-Jul-02    RMS     Restructured extended instructions, added IOP support
   22-Mar-02    RMS     Changed to allocate memory array dynamically
   11-Mar-02    RMS     Cleaned up setjmp/auto variable interaction
   17-Feb-02    RMS     Added DMS support
                        Fixed bugs in extended instructions
   03-Feb-02    RMS     Added terminal multiplexor support
                        Changed PCQ macro to use unmodified PC
                        Fixed flop restore logic (found by Bill McDermith)
                        Fixed SZx,SLx,RSS bug (found by Bill McDermith)
                        Added floating point support
   16-Jan-02    RMS     Added additional device support
   07-Jan-02    RMS     Fixed DMA register tables (found by Bill McDermith)
   07-Dec-01    RMS     Revised to use breakpoint package
   03-Dec-01    RMS     Added extended SET/SHOW support
   10-Aug-01    RMS     Removed register in declarations
   26-Nov-00    RMS     Fixed bug in dual device number routine
   21-Nov-00    RMS     Fixed bug in reset routine
   15-Oct-00    RMS     Added dynamic device number support

   References:
   - 2100A Computer Reference Manual (02100-90001, Dec-1971)
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
       (92851-90001, Mar-1981)
   - 12607A Direct Memory Access Operating and Service Manual
       (12607-90002, Jan-1970)
   - 12578A/12578A-01 Direct Memory Access Operating and Service Manual
       (12578-9001, Mar-1972)

   The register state for the HP 2116 CPU is:

   AR<15:0>             A register - addressable as location 0
   BR<15:0>             B register - addressable as location 1
   PC<14:0>             P register (program counter)
   SR<15:0>             S register
   MR<14:0>             M register - memory address
   TR<15:0>             T register - memory data
   E                    extend flag (carry out)
   O                    overflow flag

   The 2100 adds memory protection logic:

   mp_fence<14:0>       memory fence register
   mp_viol<15:0>        memory protection violation register (F register)

   The 21MX adds a pair of index registers and memory expansion logic:

   XR<15:0>             X register
   YR<15:0>             Y register
   dms_sr<15:0>         dynamic memory system status register
   dms_vr<15:0>         dynamic memory system violation register

   The original HP 2116 has four instruction formats: memory reference,
   shift, alter/skip, and I/O.  The HP 2100 added extended memory reference
   and extended arithmetic.  The HP21MX added extended byte, bit, and word
   instructions as well as extended memory.

   The memory reference format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|     op    |cp|           offset            | memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <14:11>      mnemonic        action

   0010         AND             A = A & M[MA]
   0011         JSB             M[MA] = P, P = MA + 1
   0100         XOR             A = A ^ M[MA]
   0101         JMP             P = MA
   0110         IOR             A = A | M[MA]
   0111         ISZ             M[MA] = M[MA] + 1, skip if M[MA] == 0
   1000         ADA             A = A + M[MA]
   1001         ADB             B = B + M[MA]
   1010         CPA             skip if A != M[MA]
   1011         CPB             skip if B != M[MA]
   1100         LDA             A = M[MA]
   1101         LDB             B = M[MA]
   1110         STA             M[MA] = A
   1111         STB             M[MA] = B

   <15,10>      mode            action

   0,0  page zero direct        MA = IR<9:0>
   0,1  current page direct     MA = PC<14:0>'IR,9:0>
   1,0  page zero indirect      MA = M[IR<9:0>]
   1,1  current page indirect   MA = M[PC<14:10>'IR<9:0>]

   Memory reference instructions can access an address space of 32K words.
   An instruction can directly reference the first 1024 words of memory
   (called page zero), as well as 1024 words of the current page; it can
   indirectly access all 32K.

   The shift format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0|ab| 0|s1|   op1  |ce|s2|sl|   op2  | shift
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 |     | \---+---/ |  |  | \---+---/
                 |     |     |     |  |  |     |
                 |     |     |     |  |  |     +---- shift 2 opcode
                 |     |     |     |  |  +---------- skip if low bit == 0
                 |     |     |     |  +------------- shift 2 enable
                 |     |     |     +---------------- clear Extend
                 |     |     +---------------------- shift 1 opcode
                 |     +---------------------------- shift 1 enable
                 +---------------------------------- A/B select

   The alter/skip format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0|ab| 1|regop| e op|se|ss|sl|in|sz|rs| alter/skip
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 |    \-+-/ \-+-/  |  |  |  |  |  |
                 |      |     |    |  |  |  |  |  +- reverse skip sense
                 |      |     |    |  |  |  |  +---- skip if register == 0
                 |      |     |    |  |  |  +------- increment register
                 |      |     |    |  |  +---------- skip if low bit == 0
                 |      |     |    |  +------------- skip if sign bit == 0
                 |      |     |    +---------------- skip if Extend == 0
                 |      |     +--------------------- clr/com/set Extend
                 |      +--------------------------- clr/com/set register
                 +---------------------------------- A/B select

   The I/O transfer format is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  0  0  0|ab| 1|hc| opcode |      device     | I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                 |     | \---+---/\-------+-------/
                 |     |     |            |
                 |     |     |            +--------- device select
                 |     |     +---------------------- opcode
                 |     +---------------------------- hold/clear flag
                 +---------------------------------- A/B select

   The IO transfer instruction controls the specified device.
   Depending on the opcode, the instruction may set or clear
   the device flag, start or stop I/O, or read or write data.

   The 2100 added an extended memory reference instruction;
   the 21MX added extended arithmetic, operate, byte, word,
   and bit instructions.  Note that the HP 21xx is, despite
   the right-to-left bit numbering, a big endian system.
   Bits <15:8> are byte 0, and bits <7:0> are byte 1.


   The extended memory reference format (HP 2100) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0|op| 0|            opcode           | extended mem ref
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended arithmetic format (HP 2100) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0  0  0|dr| 0  0| opcode |shift count| extended arithmetic
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended operate format (HP 21MX) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0|op| 0| 1  1  1  1  1|    opcode    | extended operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended byte and word format (HP 21MX) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0  1  0  1  1  1  1  1  1|   opcode  | extended byte/word
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  0  0  0  0  0  0  0  0  0  0  0  0  0  0  0|
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The extended bit operate format (HP 21MX) is:

    15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1| 0  0  0  1  0  1  1  1  1  1  1  1| opcode | extended bit operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|              operand address               |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   This routine is the instruction decode routine for the HP 2100.
   It is called from the simulator control program to execute
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

   2. Interrupts.  I/O devices are modelled as five parallel arrays:

        device commands as bit array dev_cmd[2][31..0]
        device flags as bit array dev_flg[2][31..0]
        device flag buffers as bit array dev_fbf[2][31..0]
        device controls as bit array dev_ctl[2][31..0]
        device service requests as bit array dev_srq[3][31..0]

      The HP 2100 interrupt structure is based on flag, flag buffer,
      and control.  If a device flag is set, the flag buffer is set,
      the control bit is set, and the device is the highest priority
      on the interrupt chain, it requests an interrupt.  When the
      interrupt is acknowledged, the flag buffer is cleared, preventing
      further interrupt requests from that device.  The combination of
      flag and control set blocks interrupts from lower priority devices.

      Command plays no direct role in interrupts.  The command flop
      tells whether a device is active.  It is set by STC and cleared
      by CLC; it is also cleared when the device flag is set.  Simple
      devices don't need to track command separately from control.

      Service requests are used to trigger the DMA service logic.

   3. Non-existent memory.  On the HP 2100, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against memory size.

      On the 21xx machines, doing SET CPU LOADERDISABLE decreases available
      memory size by 64 words.

   4. Adding I/O devices.  These modules must be modified:

        hp2100_defs.h   add interrupt request definition
        hp2100_sys.c    add sim_devices table entry

   5. Instruction interruptibility.  The simulator is fast enough, compared
      to the run-time of the longest instructions, for interruptibility not
      to matter.  But the HP diagnostics explicitly test interruptibility in
      EIS and DMS instructions, and long indirect address chains.  Accordingly,
      the simulator does "just enough" to pass these tests.  In particular, if
      an interrupt is pending but deferred at the beginning of an interruptible
      instruction, the interrupt is taken at the appropriate point; but there
      is no testing for new interrupts during execution (that is, the event
      timer is not called).

   6. Interrupt deferral.  At instruction fetch time, a pending interrupt
      request may be deferred if the previous instruction was a JMP indirect,
      JSB indirect, STC, CLC, STF, CLF, SFS (1000 only), or SFC (1000 only), or
      was executing from an interrupt trap cell.  If the CPU is a 1000, then the
      request is always deferred until after the current instruction completes.
      If the CPU is a 21xx, then the request is deferred unless the current
      instruction is an MRG instruction other than JMP or JMP,I or JSB,I.  Note
      that for the 21xx, SFS and SFC are not included in the deferral criteria.
*/

#include "hp2100_defs.h"
#include <setjmp.h>
#include "hp2100_cpu.h"

/* Memory protect constants */

#define UNIT_V_MP_JSB   (UNIT_V_UF + 0)                 /* MP jumper W5 out */
#define UNIT_V_MP_INT   (UNIT_V_UF + 1)                 /* MP jumper W6 out */
#define UNIT_V_MP_SEL1  (UNIT_V_UF + 2)                 /* MP jumper W7 out */
#define UNIT_MP_JSB     (1 << UNIT_V_MP_JSB)
#define UNIT_MP_INT     (1 << UNIT_V_MP_INT)
#define UNIT_MP_SEL1    (1 << UNIT_V_MP_SEL1)


#define ABORT(val)      longjmp (save_env, (val))

#define DMAR0           1
#define DMAR1           2

#define ALL_BKPTS       (SWMASK('E')|SWMASK('N')|SWMASK('S')|SWMASK('U'))

uint16 *M = NULL;                                       /* memory */
uint32 saved_AR = 0;                                    /* A register */
uint32 saved_BR = 0;                                    /* B register */
uint16 ABREG[2];                                        /* during execution */
uint32 PC = 0;                                          /* P register */
uint32 SR = 0;                                          /* S register */
uint32 MR = 0;                                          /* M register */
uint32 saved_MR = 0;                                    /* between executions */
uint32 TR = 0;                                          /* T register */
uint32 XR = 0;                                          /* X register */
uint32 YR = 0;                                          /* Y register */
uint32 E = 0;                                           /* E register */
uint32 O = 0;                                           /* O register */
uint32 dev_cmd[2] = { 0 };                              /* device command */
uint32 dev_ctl[2] = { 0 };                              /* device control */
uint32 dev_flg[2] = { 0 };                              /* device flags */
uint32 dev_fbf[2] = { 0 };                              /* device flag bufs */
uint32 dev_srq[2] = { 0 };                              /* device svc reqs */
struct DMA dmac[2] = { { 0 }, { 0 } };                  /* DMA channels */
uint32 ion = 0;                                         /* interrupt enable */
uint32 ion_defer = 0;                                   /* interrupt defer */
uint32 intaddr = 0;                                     /* interrupt addr */
uint32 mp_fence = 0;                                    /* mem prot fence */
uint32 mp_viol = 0;                                     /* mem prot viol reg */
uint32 mp_mevff = 0;                                    /* mem exp (dms) viol */
uint32 mp_evrff = 1;                                    /* update mp_viol */
uint32 err_PC = 0;                                      /* error PC */
uint32 dms_enb = 0;                                     /* dms enable */
uint32 dms_ump = 0;                                     /* dms user map */
uint32 dms_sr = 0;                                      /* dms status reg */
uint32 dms_vr = 0;                                      /* dms violation reg */
uint16 dms_map[MAP_NUM * MAP_LNT] = { 0 };              /* dms maps */
uint32 iop_sp = 0;                                      /* iop stack */
uint32 ind_max = 16;                                    /* iadr nest limit */
uint32 stop_inst = 1;                                   /* stop on ill inst */
uint32 stop_dev = 0;                                    /* stop on ill dev */
uint32 fwanxm = 0;                                      /* first word addr of nx mem */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
uint32 pcq_p = 0;                                       /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */
jmp_buf save_env;                                       /* abort handler */

/* Table of CPU features by model.

   Fields:
    - typ:    standard features plus typically configured options.
    - opt:    complete list of optional features.
    - maxmem: maximum configurable memory in 16-bit words.

   Features in the "typical" list are enabled when the CPU model is selected.
   If a feature appears in the "typical" list but NOT in the "optional" list,
   then it is standard equipment and cannot be disabled.  If a feature appears
   in the "optional" list, then it may be enabled or disabled as desired by the
   user.
*/

struct FEATURE_TABLE {                                  /* CPU model feature table: */
    uint32      typ;                                    /*  - typical features */
    uint32      opt;                                    /*  - optional features */
    uint32      maxmem;                                 /*  - maximum memory */
    };

static struct FEATURE_TABLE cpu_features[] = {          /* features in UNIT_xxxx order*/
  { UNIT_DMA | UNIT_MP,                                 /* UNIT_2116 */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP | UNIT_EAU,
    32768 },
  { UNIT_DMA,                                           /* UNIT_2115 */
    UNIT_PFAIL | UNIT_DMA | UNIT_EAU,
    8192 },
  { UNIT_DMA,                                           /* UNIT_2114 */
    UNIT_PFAIL | UNIT_DMA,
    16384 },
  { 0, 0, 0 },
  { UNIT_PFAIL | UNIT_MP | UNIT_DMA | UNIT_EAU,         /* UNIT_2100 */
    UNIT_DMA   | UNIT_FP | UNIT_IOP | UNIT_FFP,
    32768 },
  { 0, 0, 0 },
  { 0, 0, 0 },
  { 0, 0, 0 },
  { UNIT_MP | UNIT_DMA | UNIT_EAU | UNIT_FP | UNIT_DMS, /* UNIT_1000_M */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP  | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DBI | UNIT_DS,
    1048576 },
  { UNIT_MP | UNIT_DMA | UNIT_EAU | UNIT_FP | UNIT_DMS, /* UNIT_1000_E */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP  | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DBI | UNIT_DS  | UNIT_EMA_VMA,
    1048576 },
  { UNIT_MP  | UNIT_DMA | UNIT_EAU | UNIT_FP |          /* UNIT_1000_F */
    UNIT_FFP | UNIT_DBI | UNIT_DMS,
    UNIT_PFAIL | UNIT_DMA | UNIT_MP     |
    UNIT_IOP   | UNIT_DS  | UNIT_SIGNAL | UNIT_EMA_VMA,
    1048576 }
  };

extern int32 sim_interval;
extern int32 sim_int_char;
extern int32 sim_brk_char;
extern int32 sim_del_char;
extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */
extern FILE *sim_log;
extern DEVICE *sim_devices[];
extern int32 sim_switches;
extern char halt_msg[];

t_stat Ea (uint32 IR, uint32 *addr, uint32 irq);
uint16 ReadIO (uint32 addr, uint32 map);
uint16 ReadPW (uint32 addr);
uint16 ReadTAB (uint32 addr);
void WriteIO (uint32 addr, uint32 dat, uint32 map);
void WritePW (uint32 addr, uint32 dat);
uint32 dms (uint32 va, uint32 map, uint32 prot);
uint32 dms_io (uint32 va, uint32 map);
uint32 shift (uint32 inval, uint32 flag, uint32 oper);
void dma_cycle (uint32 chan, uint32 map);
uint32 calc_dma (void);
uint32 calc_int (void);
t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);
t_stat mp_reset (DEVICE *dptr);
t_stat dma0_reset (DEVICE *dptr);
t_stat dma1_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 new_size, char *cptr, void *desc);
t_stat cpu_set_model (UNIT *uptr, int32 new_model, char *cptr, void *desc);
t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_set_opt (UNIT *uptr, int32 option, char *cptr, void *desc);
t_stat cpu_clr_opt (UNIT *uptr, int32 option, char *cptr, void *desc);
t_stat cpu_set_ldr (UNIT *uptr, int32 enable, char *cptr, void *desc);
t_bool dev_conflict (void);
void hp_post_cmd (t_bool from_scp);

extern t_stat cpu_eau (uint32 IR, uint32 intrq);
extern t_stat cpu_uig_0 (uint32 IR, uint32 intrq, uint32 iotrap);
extern t_stat cpu_uig_1 (uint32 IR, uint32 intrq, uint32 iotrap);

extern int32 clk_delay (int32 flg);
extern void (*sim_vm_post) (t_bool from_scp);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, 0) };

REG cpu_reg[] = {
    { ORDATA (P, PC, 15) },
    { ORDATA (A, saved_AR, 16) },
    { ORDATA (B, saved_BR, 16) },
    { ORDATA (M, MR, 15) },
    { ORDATA (T, TR, 16), REG_RO },
    { ORDATA (X, XR, 16) },
    { ORDATA (Y, YR, 16) },
    { ORDATA (S, SR, 16) },
    { FLDATA (E, E, 0) },
    { FLDATA (O, O, 0) },
    { FLDATA (ION, ion, 0) },
    { FLDATA (ION_DEFER, ion_defer, 0) },
    { ORDATA (CIR, intaddr, 6) },
    { FLDATA (DMSENB, dms_enb, 0) },
    { FLDATA (DMSCUR, dms_ump, VA_N_PAG) },
    { ORDATA (DMSSR, dms_sr, 16) },
    { ORDATA (DMSVR, dms_vr, 16) },
    { BRDATA (DMSMAP, dms_map, 8, 16, MAP_NUM * MAP_LNT) },
    { ORDATA (IOPSP, iop_sp, 16) },
    { FLDATA (STOP_INST, stop_inst, 0) },
    { FLDATA (STOP_DEV, stop_dev, 1) },
    { DRDATA (INDMAX, ind_max, 16), REG_NZ + PV_LEFT },
    { ORDATA (FWANXM, fwanxm, 32), REG_HRO },
    { BRDATA (PCQ, pcq, 8, 15, PCQ_SIZE), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { ORDATA (WRU, sim_int_char, 8), REG_HRO },
    { ORDATA (BRK, sim_brk_char, 8), REG_HRO },
    { ORDATA (DEL, sim_del_char, 8), REG_HRO },
    { ORDATA (HCMD, dev_cmd[0], 32), REG_HRO },
    { ORDATA (LCMD, dev_cmd[1], 32), REG_HRO },
    { ORDATA (HCTL, dev_ctl[0], 32), REG_HRO },
    { ORDATA (LCTL, dev_ctl[1], 32), REG_HRO },
    { ORDATA (HFLG, dev_flg[0], 32), REG_HRO },
    { ORDATA (LFLG, dev_flg[1], 32), REG_HRO },
    { ORDATA (HFBF, dev_fbf[0], 32), REG_HRO },
    { ORDATA (LFBF, dev_fbf[1], 32), REG_HRO },
    { ORDATA (HSRQ, dev_srq[0], 32), REG_HRO },
    { ORDATA (LSRQ, dev_srq[1], 32), REG_HRO },
    { NULL }
    };

/* CPU modifier table.

   The 21MX monikers are deprecated in favor of the 1000 designations.  See the
   "HP 1000 Series Naming History" on the back inside cover of the Technical
   Reference Handbook. */

MTAB cpu_mod[] = {
    { UNIT_MODEL_MASK, UNIT_2116,   "",   "2116",   &cpu_set_model, &cpu_show_model, "2116"   },
    { UNIT_MODEL_MASK, UNIT_2115,   "",   "2115",   &cpu_set_model, &cpu_show_model, "2115"   },
    { UNIT_MODEL_MASK, UNIT_2114,   "",   "2114",   &cpu_set_model, &cpu_show_model, "2114"   },
    { UNIT_MODEL_MASK, UNIT_2100,   "",   "2100",   &cpu_set_model, &cpu_show_model, "2100"   },
    { UNIT_MODEL_MASK, UNIT_1000_E, "",   "1000-E", &cpu_set_model, &cpu_show_model, "1000-E" },
    { UNIT_MODEL_MASK, UNIT_1000_E, NULL, "21MX-E", &cpu_set_model, &cpu_show_model, "1000-E" },
    { UNIT_MODEL_MASK, UNIT_1000_M, "",   "1000-M", &cpu_set_model, &cpu_show_model, "1000-M" },
    { UNIT_MODEL_MASK, UNIT_1000_M, NULL, "21MX-M", &cpu_set_model, &cpu_show_model, "1000-M" },

#if defined (HAVE_INT64)
    { UNIT_MODEL_MASK, UNIT_1000_F, "",   "1000-F", &cpu_set_model, &cpu_show_model, "1000-F" },
#endif

    { MTAB_XTD | MTAB_VDV, 1, NULL, "LOADERENABLE",  &cpu_set_ldr, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 0, NULL, "LOADERDISABLE", &cpu_set_ldr, NULL, NULL },

    { UNIT_EAU,     UNIT_EAU,   "EAU",        "EAU",      &cpu_set_opt, NULL, NULL },
    { UNIT_EAU,     0,          "no EAU",     NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_EAU,    NULL, "NOEAU",    &cpu_clr_opt, NULL, NULL },

    { UNIT_FP,      UNIT_FP,    "FP",         "FP",       &cpu_set_opt, NULL, NULL },
    { UNIT_FP,      0,          "no FP",      NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_FP,     NULL, "NOFP",     &cpu_clr_opt, NULL, NULL },

    { UNIT_IOP,     UNIT_IOP,   "IOP",        "IOP",      &cpu_set_opt, NULL, NULL },
    { UNIT_IOP,     0,          "no IOP",     NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_IOP,    NULL, "NOIOP",    &cpu_clr_opt, NULL, NULL },

    { UNIT_DMS,     UNIT_DMS,   "DMS",        "DMS",      &cpu_set_opt, NULL, NULL },
    { UNIT_DMS,     0,          "no DMS",     NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_DMS,    NULL, "NODMS",    &cpu_clr_opt, NULL, NULL },

    { UNIT_FFP,     UNIT_FFP,   "FFP",        "FFP",      &cpu_set_opt, NULL, NULL },
    { UNIT_FFP,     0,          "no FFP",     NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_FFP,    NULL, "NOFFP",    &cpu_clr_opt, NULL, NULL },

    { UNIT_DBI,     UNIT_DBI,   "DBI",        "DBI",      &cpu_set_opt, NULL, NULL },
    { UNIT_DBI,     0,          "no DBI",     NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_DBI,    NULL, "NODBI",    &cpu_clr_opt, NULL, NULL },

/* Future microcode support.
    { UNIT_EMA_VMA, UNIT_EMA,   "EMA",        "EMA",      &cpu_set_opt, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_EMA,    NULL, "NOEMA",    &cpu_clr_opt, NULL, NULL },

    { UNIT_EMA_VMA, UNIT_VMAOS, "VMA",        "VMA",      &cpu_set_opt, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_VMAOS,  NULL, "NOVMA",    &cpu_clr_opt, NULL, NULL },

    { UNIT_EMA_VMA, 0,          "no EMA/VMA", NULL,       &cpu_set_opt, NULL, NULL },

    { UNIT_VIS,     UNIT_VIS,   "VIS",        "VIS",      &cpu_set_opt, NULL, NULL },
    { UNIT_VIS,     0,          "no VIS",     NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_VIS,    NULL, "NOVIS",    &cpu_clr_opt, NULL, NULL },

    { UNIT_DS,      UNIT_DS,    "DS",         "DS",       &cpu_set_opt, NULL, NULL },
    { UNIT_DS,      0,          "no DS",      NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_DS,     NULL, "NODS",     &cpu_clr_opt, NULL, NULL },

    { UNIT_SIGNAL,  UNIT_SIGNAL,"SIGNAL",     "SIGNAL",   &cpu_set_opt, NULL, NULL },
    { UNIT_SIGNAL,  0,          "no SIGNAL",  NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_SIGNAL, NULL, "NOSIGNAL", &cpu_clr_opt, NULL, NULL },
*/

    { MTAB_XTD | MTAB_VDV,    4096, NULL, "4K",    &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,    8192, NULL, "8K",    &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,   12288, NULL, "12K",   &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,   16384, NULL, "16K",   &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,   24576, NULL, "24K",   &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,   32768, NULL, "32K",   &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,   65536, NULL, "64K",   &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,  131072, NULL, "128K",  &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,  262144, NULL, "256K",  &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV,  524288, NULL, "512K",  &cpu_set_size, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, 1048576, NULL, "1024K", &cpu_set_size, NULL, NULL },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, PA_N_SIZE, 1, 8, 16,
    &cpu_ex, &cpu_dep, &cpu_reset,
    &cpu_boot, NULL, NULL
    };

/* Memory protect data structures

   mp_dev       MP device descriptor
   mp_unit      MP unit descriptor
   mp_reg       MP register list
   mp_mod       MP modifiers list
*/

UNIT mp_unit = { UDATA (NULL, UNIT_MP_SEL1, 0) };

REG mp_reg[] = {
    { FLDATA (CTL, dev_ctl[PRO/32], INT_V (PRO)) },
    { FLDATA (FLG, dev_flg[PRO/32], INT_V (PRO)) },
    { FLDATA (FBF, dev_fbf[PRO/32], INT_V (PRO)) },
    { ORDATA (FR, mp_fence, 15) },
    { ORDATA (VR, mp_viol, 16) },
    { FLDATA (MEV, mp_mevff, 0) },
    { FLDATA (EVR, mp_evrff, 0) },
    { NULL }
    };

MTAB mp_mod[] = {
    { UNIT_MP_JSB, UNIT_MP_JSB, "JSB (W5) in", "JSBIN", NULL },
    { UNIT_MP_JSB, 0, "JSB (W5) out", "JSBOUT", NULL },
    { UNIT_MP_INT, UNIT_MP_INT, "INT (W6) in", "INTIN", NULL },
    { UNIT_MP_INT, 0, "INT (W6) out", "INTOUT", NULL },
    { UNIT_MP_SEL1, UNIT_MP_SEL1, "SEL1 (W7) in", "SEL1IN", NULL },
    { UNIT_MP_SEL1, 0, "SEL1 (W7) out", "SEL1OUT", NULL },
    { 0 }
    };

DEVICE mp_dev = {
    "MP", &mp_unit, mp_reg, mp_mod,
    1, 8, 1, 1, 8, 16,
    NULL, NULL, &mp_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE | DEV_DIS
    };

/* DMA controller data structures

   dmax_dev     DMAx device descriptor
   dmax_reg     DMAx register list
*/

UNIT dma0_unit = { UDATA (NULL, 0, 0) };

REG dma0_reg[] = {
    { FLDATA (CMD, dev_cmd[DMA0/32], INT_V (DMA0)) },
    { FLDATA (CTL, dev_ctl[DMA0/32], INT_V (DMA0)) },
    { FLDATA (FLG, dev_flg[DMA0/32], INT_V (DMA0)) },
    { FLDATA (FBF, dev_fbf[DMA0/32], INT_V (DMA0)) },
    { FLDATA (CTLALT, dev_ctl[DMALT0/32], INT_V (DMALT0)) },
    { ORDATA (CW1, dmac[0].cw1, 16) },
    { ORDATA (CW2, dmac[0].cw2, 16) },
    { ORDATA (CW3, dmac[0].cw3, 16) },
    { DRDATA (LATENCY, dmac[0].latency, 8) },
    { FLDATA (BYTE, dmac[0].packer, 31) },
    { ORDATA (PACKER, dmac[0].packer, 8) },
    { NULL }
    };

DEVICE dma0_dev = {
    "DMA0", &dma0_unit, dma0_reg, NULL,
    1, 8, 1, 1, 8, 16,
    NULL, NULL, &dma0_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE
    };

UNIT dma1_unit = { UDATA (NULL, 0, 0) };

REG dma1_reg[] = {
    { FLDATA (CMD, dev_cmd[DMA1/32], INT_V (DMA1)) },
    { FLDATA (CTL, dev_ctl[DMA1/32], INT_V (DMA1)) },
    { FLDATA (FLG, dev_flg[DMA1/32], INT_V (DMA1)) },
    { FLDATA (FBF, dev_fbf[DMA1/32], INT_V (DMA1)) },
    { FLDATA (CTLALT, dev_ctl[DMALT1/32], INT_V (DMALT1)) },
    { ORDATA (CW1, dmac[1].cw1, 16) },
    { ORDATA (CW2, dmac[1].cw2, 16) },
    { ORDATA (CW3, dmac[1].cw3, 16) },
    { DRDATA (LATENCY, dmac[1].latency, 8) },
    { FLDATA (BYTE, dmac[1].packer, 31) },
    { ORDATA (PACKER, dmac[1].packer, 8) },
    { NULL }
    };

DEVICE dma1_dev = {
    "DMA1", &dma1_unit, dma1_reg, NULL,
    1, 8, 1, 1, 8, 16,
    NULL, NULL, &dma1_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE
    };

/* Interrupt defer table (1000 version) */

static int32 defer_tab[] = { 0, 1, 1, 1, 0, 0, 0, 1 };

/* Device dispatch table */

uint32 devdisp (uint32 devno, uint32 inst, uint32 IR, uint32 outdat);
int32 cpuio (int32 op, int32 IR, int32 outdat);
int32 ovfio (int32 op, int32 IR, int32 outdat);
int32 pwrio (int32 op, int32 IR, int32 outdat);
int32 proio (int32 op, int32 IR, int32 outdat);
int32 dmsio (int32 op, int32 IR, int32 outdat);
int32 dmpio (int32 op, int32 IR, int32 outdat);
int32 nulio (int32 op, int32 IR, int32 outdat);

int32 (*dtab[64])() = {
    &cpuio, &ovfio, &dmsio, &dmsio, &pwrio, &proio, &dmpio, &dmpio,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
    };

t_stat sim_instr (void)
{
uint32 intrq, dmarq;                                    /* set after setjmp */
uint32 iotrap = 0;                                      /* set after setjmp */
t_stat reason;                                          /* set after setjmp */
int32 i, dev;                                           /* temp */
DEVICE *dptr;                                           /* temp */
DIB *dibp;                                              /* temp */
int abortval;

/* Restore register state */

if (dev_conflict ()) return SCPE_STOP;                  /* check consistency */
AR = saved_AR & DMASK;                                  /* restore reg */
BR = saved_BR & DMASK;
err_PC = PC = PC & VAMASK;                              /* load local PC */
reason = 0;

/* Restore I/O state */

if (mp_dev.flags & DEV_DIS) dtab[PRO] = NULL;
else dtab[PRO] = &proio;                                /* set up MP dispatch */
if (dma0_dev.flags & DEV_DIS) dtab[DMA0] = dtab[DMALT0] = NULL;
else {
    dtab[DMA0] = &dmpio;                                /* set up DMA0 dispatch */
    dtab[DMALT0] = &dmsio;
    }
if (dma1_dev.flags & DEV_DIS) dtab[DMA1] = dtab[DMALT1] = NULL;
else {
    dtab[DMA1] = &dmpio;                                /* set up DMA1 dispatch */
    dtab[DMALT1] = &dmsio;
    }

for (i = VARDEV; i <= I_DEVMASK; i++) dtab[i] = NULL;   /* clr disp table */
dev_cmd[0] = dev_cmd[0] & M_FXDEV;                      /* clear dynamic info */
dev_ctl[0] = dev_ctl[0] & M_FXDEV;
dev_flg[0] = dev_flg[0] & M_FXDEV;
dev_fbf[0] = dev_fbf[0] & M_FXDEV;
dev_srq[0] = dev_srq[1] = 0;                            /* init svc requests */
dev_cmd[1] = dev_ctl[1] = dev_flg[1] = dev_fbf[1] = 0;
for (i = 0; dptr = sim_devices[i]; i++) {               /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp && !(dptr->flags & DEV_DIS)) {             /* exist, enabled? */
        dev = dibp->devno;                              /* get dev # */
        if (dibp->cmd) { setCMD (dev); }                /* restore cmd */
        if (dibp->ctl) { setCTL (dev); }                /* restore ctl */
        if (dibp->flg) { setFLG (dev); }                /* restore flg */
        clrFBF (dev);                                   /* also sets fbf */
        if (dibp->fbf) { setFBF (dev); }                /* restore fbf */
        if (dibp->srq) { setSRQ (dev); }                /* restore srq */
        dtab[dev] = dibp->iot;                          /* set I/O dispatch */
        }
    }
sim_rtc_init (clk_delay (0));                           /* recalibrate clock */

/* Configure interrupt deferral table */

if (UNIT_CPU_FAMILY == UNIT_FAMILY_21XX)                /* 21xx series? */
    defer_tab[ioSFC] = defer_tab[ioSFS] = 0;            /* SFC/S doesn't defer */
else                                                    /* 1000 series */
    defer_tab[ioSFC] = defer_tab[ioSFS] = 1;            /* SFC/S does defer */

/* Abort handling

   If an abort occurs in memory protection, the relocation routine
   executes a longjmp to this area OUTSIDE the main simulation loop.
   Memory protection errors are the only sources of aborts in the
   HP 2100.  All referenced variables must be globals, and all sim_instr
   scoped automatics must be set after the setjmp.
*/

abortval = setjmp (save_env);                           /* set abort hdlr */
if (abortval != 0) {                                    /* mem mgt abort? */
    setFLG (PRO);                                       /* req interrupt */
    mp_evrff = 0;                                       /* block mp_viol upd */
    }
dmarq = calc_dma ();                                    /* recalc DMA masks */
intrq = calc_int ();                                    /* recalc interrupts */

/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until halted */
    uint32 IR, MA, absel, v1, t, skip;

    if (sim_interval <= 0) {                            /* check clock queue */
        if (reason = sim_process_event ()) break;
        dmarq = calc_dma ();                            /* recalc DMA reqs */
        intrq = calc_int ();                            /* recalc interrupts */
        }

    if (dmarq) {
        if (dmarq & DMAR0) dma_cycle (0, PAMAP);        /* DMA1 cycle? */
        if (dmarq & DMAR1) dma_cycle (1, PBMAP);        /* DMA2 cycle? */
        dmarq = calc_dma ();                            /* recalc DMA reqs */
        intrq = calc_int ();                            /* recalc interrupts */
        }

/* Interrupt deferral rules differ on the 21xx vs. the 1000.  The 1000 always
   defers until the completion of the instruction following a deferring
   instruction.  The 21xx defers unless the current instruction is an MRG
   instruction other than JMP or JMP,I or JSB,I.  See the "Set Phase Logic
   Flowchart" in the 2100A Computer Reference Manual for details.
*/

    if (intrq &&                                        /* interrupt pending? */
       ion_defer &&                                     /* deferring instruction? */
       (UNIT_CPU_FAMILY == UNIT_FAMILY_21XX)) {         /* 21xx series? */
        IR = ReadW (PC);                                /* prefetch next instr */
        if (((IR & 0070000) != 0000000) &&              /* MRG instruction? */
            ((IR & 0174000) != 0114000) &&              /* JSB,I? */
            ((IR & 0074000) != 0024000))                /* JMP or JMP,I? */
            ion_defer = 0;                              /* no, so clear deferral */
            }

/*  (From Dave Bryan)
    Unlike most other I/O devices, the MP flag flip-flop is cleared
    automatically when the interrupt is acknowledged and not by a programmed
    instruction (CLF and STF affect the parity error enable FF instead).
    Section 4.4.3 "Memory Protect and I/O Interrupt Generation" of the "HP 1000
    M/E/F-Series Computers Engineering and Reference Documentation" (HP
    92851-90001) says:

      "When IAK occurs and IRQ5 is asserted, the FLAGBFF is cleared, FLAGFF
       clocked off at next T2, and IRQ5 will no longer occur."
*/

    if (intrq && ((intrq <= PRO) || !ion_defer)) {      /* interrupt request? */
        iotrap = 1;                                     /* I/O trap cell instr */
        clrFBF (intrq);                                 /* clear flag buffer */
        if (intrq == PRO) clrFLG (PRO);                 /* MP flag follows fbuf */
        intaddr = intrq;                                /* save int addr */
        if (dms_enb) dms_sr = dms_sr | MST_ENBI;        /* dms enabled? */
        else dms_sr = dms_sr & ~MST_ENBI;
        if (dms_ump) {                                  /* user map? */
            dms_sr = dms_sr | MST_UMPI;
            dms_ump = SMAP;                             /* switch to system */
            }
        else dms_sr = dms_sr & ~MST_UMPI;
        IR = ReadW (intrq);                             /* get dispatch instr */
        ion_defer = 1;                                  /* defer interrupts */
        intrq = 0;                                      /* clear request */
        if (((IR & I_NMRMASK) != I_IO) ||               /* if not I/O or */
            (I_GETIOOP (IR) == ioHLT))                  /* if halt, */
            clrCTL (PRO);                               /* protection off */
        }

    else {                                              /* normal instruction */
        iotrap = 0;
        err_PC = PC;                                    /* save PC for error */
        if (sim_brk_summ &&                             /* any breakpoints? */
            sim_brk_test (PC, SWMASK ('E') |            /* unconditional or */
            (dms_enb? (dms_ump? SWMASK ('U'): SWMASK ('S')):
                SWMASK ('N')))) {                       /* or right type for DMS? */
            reason = STOP_IBKPT;                        /* stop simulation */
            break;
            }
        if (mp_evrff) mp_viol = PC;                     /* if ok, upd mp_viol */
        IR = ReadW (PC);                                /* fetch instr */
        PC = (PC + 1) & VAMASK;
        sim_interval = sim_interval - 1;
        ion_defer = 0;
        }

/* Instruction decode.  The 21MX does a 256-way decode on IR<15:8>

   15 14 13 12 11 10 09 08      instruction

    x <-!= 0->  x  x  x  x      memory reference
    0  0  0  0  x  0  x  x      shift
    0  0  0  0  x  0  x  x      alter-skip
    1  0  0  0  x  1  x  x      IO
    1  0  0  0  0  0  x  0      extended arithmetic
    1  0  0  0  0  0  0  1      divide (decoded as 100400)
    1  0  0  0  1  0  0  0      double load (decoded as 104000)
    1  0  0  0  1  0  0  1      double store (decoded as 104400)
    1  0  0  0  1  0  1  0      extended instr group 0 (A/B must be set)
    1  0  0  0  x  0  1  1      extended instr group 1 (A/B ignored) */

    absel = (IR & I_AB)? 1: 0;                          /* get A/B select */
    switch ((IR >> 8) & 0377) {                         /* decode IR<15:8> */

/* Memory reference instructions */

    case 0020:case 0021:case 0022:case 0023:
    case 0024:case 0025:case 0026:case 0027:
    case 0220:case 0221:case 0222:case 0223:
    case 0224:case 0225:case 0226:case 0227:
        if (reason = Ea (IR, &MA, intrq)) break;        /* AND */
        AR = AR & ReadW (MA);
        break;

    case 0230:case 0231:case 0232:case 0233:
    case 0234:case 0235:case 0236:case 0237:
        ion_defer = 1;                                  /* defer if JSB,I */

    case 0030:case 0031:case 0032:case 0033:
    case 0034:case 0035:case 0036:case 0037:
        if (reason = Ea (IR, &MA, intrq)) break;        /* JSB */
        if ((mp_unit.flags & UNIT_MP_JSB) &&            /* MP if W7 (JSB) out */
            CTL (PRO) && (MA < mp_fence))
            ABORT (ABORT_PRO);
        WriteW (MA, PC);                                /* store PC */
        PCQ_ENTRY;
        PC = (MA + 1) & VAMASK;                         /* jump */
        break;

    case 0040:case 0041:case 0042:case 0043:
    case 0044:case 0045:case 0046:case 0047:
    case 0240:case 0241:case 0242:case 0243:
    case 0244:case 0245:case 0246:case 0247:
        if (reason = Ea (IR, &MA, intrq)) break;        /* XOR */
        AR = AR ^ ReadW (MA);
        break;

    case 0250:case 0251:case 0252:case 0253:
    case 0254:case 0255:case 0256:case 0257:
        ion_defer = 1;                                  /* defer if JMP,I */

    case 0050:case 0051:case 0052:case 0053:
    case 0054:case 0055:case 0056:case 0057:
        if (reason = Ea (IR, &MA, intrq)) break;        /* JMP */
        mp_dms_jmp (MA);                                /* validate jump addr */
        PCQ_ENTRY;
        PC = MA;                                        /* jump */
        break;

    case 0060:case 0061:case 0062:case 0063:
    case 0064:case 0065:case 0066:case 0067:
    case 0260:case 0261:case 0262:case 0263:
    case 0264:case 0265:case 0266:case 0267:
        if (reason = Ea (IR, &MA, intrq)) break;        /* IOR */
        AR = AR | ReadW (MA);
        break;

    case 0070:case 0071:case 0072:case 0073:
    case 0074:case 0075:case 0076:case 0077:
    case 0270:case 0271:case 0272:case 0273:
    case 0274:case 0275:case 0276:case 0277:
        if (reason = Ea (IR, &MA, intrq)) break;        /* ISZ */
        t = (ReadW (MA) + 1) & DMASK;
        WriteW (MA, t);
        if (t == 0) PC = (PC + 1) & VAMASK;
        break;

    case 0100:case 0101:case 0102:case 0103:
    case 0104:case 0105:case 0106:case 0107:
    case 0300:case 0301:case 0302:case 0303:
    case 0304:case 0305:case 0306:case 0307:
        if (reason = Ea (IR, &MA, intrq)) break;        /* ADA */
        v1 = ReadW (MA);
        t = AR + v1;
        if (t > DMASK) E = 1;
        if (((~AR ^ v1) & (AR ^ t)) & SIGN) O = 1;
        AR = t & DMASK;
        break;

    case 0110:case 0111:case 0112:case 0113:
    case 0114:case 0115:case 0116:case 0117:
    case 0310:case 0311:case 0312:case 0313:
    case 0314:case 0315:case 0316:case 0317:
        if (reason = Ea (IR, &MA, intrq)) break;        /* ADB */
        v1 = ReadW (MA);
        t = BR + v1;
        if (t > DMASK) E = 1;
        if (((~BR ^ v1) & (BR ^ t)) & SIGN) O = 1;
        BR = t & DMASK;
        break;

    case 0120:case 0121:case 0122:case 0123:
    case 0124:case 0125:case 0126:case 0127:
    case 0320:case 0321:case 0322:case 0323:
    case 0324:case 0325:case 0326:case 0327:
        if (reason = Ea (IR, &MA, intrq)) break;        /* CPA */
        if (AR != ReadW (MA)) PC = (PC + 1) & VAMASK;
        break;

    case 0130:case 0131:case 0132:case 0133:
    case 0134:case 0135:case 0136:case 0137:
    case 0330:case 0331:case 0332:case 0333:
    case 0334:case 0335:case 0336:case 0337:
        if (reason = Ea (IR, &MA, intrq)) break;        /* CPB */
        if (BR != ReadW (MA)) PC = (PC + 1) & VAMASK;
        break;

    case 0140:case 0141:case 0142:case 0143:
    case 0144:case 0145:case 0146:case 0147:
    case 0340:case 0341:case 0342:case 0343:
    case 0344:case 0345:case 0346:case 0347:
        if (reason = Ea (IR, &MA, intrq)) break;        /* LDA */
        AR = ReadW (MA);
        break;

    case 0150:case 0151:case 0152:case 0153:
    case 0154:case 0155:case 0156:case 0157:
    case 0350:case 0351:case 0352:case 0353:
    case 0354:case 0355:case 0356:case 0357:
        if (reason = Ea (IR, &MA, intrq)) break;        /* LDB */
        BR = ReadW (MA);
        break;

    case 0160:case 0161:case 0162:case 0163:
    case 0164:case 0165:case 0166:case 0167:
    case 0360:case 0361:case 0362:case 0363:
    case 0364:case 0365:case 0366:case 0367:
        if (reason = Ea (IR, &MA, intrq)) break;        /* STA */
        WriteW (MA, AR);
        break;

    case 0170:case 0171:case 0172:case 0173:
    case 0174:case 0175:case 0176:case 0177:
    case 0370:case 0371:case 0372:case 0373:
    case 0374:case 0375:case 0376:case 0377:
        if (reason = Ea (IR, &MA, intrq)) break;        /* STB */
        WriteW (MA, BR);
        break;

/* Alter/skip instructions */

    case 0004:case 0005:case 0006:case 0007:
    case 0014:case 0015:case 0016:case 0017:
        skip = 0;                                       /* no skip */
        if (IR & 000400) t = 0;                         /* CLx */
        else t = ABREG[absel];
        if (IR & 001000) t = t ^ DMASK;                 /* CMx */
        if (IR & 000001) {                              /* RSS? */
            if ((IR & 000040) && (E != 0)) skip = 1;    /* SEZ,RSS */
            if (IR & 000100) E = 0;                     /* CLE */
            if (IR & 000200) E = E ^ 1;                 /* CME */
            if (((IR & 000030) == 000030) &&            /* SSx,SLx,RSS */
                ((t & 0100001) == 0100001)) skip = 1;
            if (((IR & 000030) == 000020) &&            /* SSx,RSS */
                ((t & SIGN) != 0)) skip = 1;
            if (((IR & 000030) == 000010) &&            /* SLx,RSS */
                ((t & 1) != 0)) skip = 1;
            if (IR & 000004) {                          /* INx */
                t = (t + 1) & DMASK;
                if (t == 0) E = 1;
                if (t == SIGN) O = 1;
                }
            if ((IR & 000002) && (t != 0)) skip = 1;    /* SZx,RSS */
            if ((IR & 000072) == 0) skip = 1;           /* RSS */
            }                                           /* end if RSS */
        else {
            if ((IR & 000040) && (E == 0)) skip = 1;    /* SEZ */
            if (IR & 000100) E = 0;                     /* CLE */
            if (IR & 000200) E = E ^ 1;                 /* CME */
            if ((IR & 000020) &&                        /* SSx */
                ((t & SIGN) == 0)) skip = 1;
            if ((IR & 000010) &&                        /* SLx */
                 ((t & 1) == 0)) skip = 1;
            if (IR & 000004) {                          /* INx */
                t = (t + 1) & DMASK;
                if (t == 0) E = 1;
                if (t == SIGN) O = 1;
                }
            if ((IR & 000002) && (t == 0)) skip = 1;    /* SZx */
            }                                           /* end if ~RSS */
        ABREG[absel] = t;                               /* store result */
        PC = (PC + skip) & VAMASK;                      /* add in skip */
        break;                                          /* end if alter/skip */

/* Shift instructions */

    case 0000:case 0001:case 0002:case 0003:
    case 0010:case 0011:case 0012:case 0013:
        t = shift (ABREG[absel], IR & 01000, IR >> 6);  /* do first shift */
        if (IR & 000040) E = 0;                         /* CLE */
        if ((IR & 000010) && ((t & 1) == 0))            /* SLx */
            PC = (PC + 1) & VAMASK;
        ABREG[absel] = shift (t, IR & 00020, IR);       /* do second shift */
        break;                                          /* end if shift */

/* I/O instructions */

    case 0204:case 0205:case 0206:case 0207:
    case 0214:case 0215:case 0216:case 0217:
        reason = iogrp (IR, iotrap);                    /* execute instr */
        break;                                          /* end if I/O */

/* Extended arithmetic */

    case 0200:                                          /* EAU group 0 */
    case 0201:                                          /* divide */
    case 0202:                                          /* EAU group 2 */
    case 0210:                                          /* DLD */
    case 0211:                                          /* DST */
        reason = cpu_eau (IR, intrq);                   /* extended arith */
        break;

/* Extended instructions */

    case 0212:                                          /* UIG 0 extension */
        reason = cpu_uig_0 (IR, intrq, iotrap);         /* extended opcode */
        break;

    case 0203:                                          /* UIG 1 extension */
    case 0213:
        reason = cpu_uig_1 (IR, intrq, iotrap);         /* extended opcode */
        break;
        }                                               /* end case IR */

    if (reason == NOTE_IOG) {                           /* I/O instr exec? */
        dmarq = calc_dma ();                            /* recalc DMA masks */
        intrq = calc_int ();                            /* recalc interrupts */
        reason = 0;                                     /* continue */
        }

    else if (reason == NOTE_INDINT) {                   /* intr pend during indir? */
        PC = err_PC;                                    /* back out of inst */
        reason = 0;                                     /* continue */
        }
    }                                                   /* end while */

/* Simulation halted */

saved_AR = AR & DMASK;
saved_BR = BR & DMASK;
if (iotrap && (reason == STOP_HALT)) MR = intaddr;      /* HLT in trap cell? */
else MR = (PC - 1) & VAMASK;                            /* no, M = P - 1 */
TR = ReadTAB (MR);                                      /* last word fetched */
saved_MR = MR;                                          /* save for T cmd update */
if ((reason == STOP_RSRV) || (reason == STOP_IODV) ||   /* instr error? */
    (reason == STOP_IND)) PC = err_PC;                  /* back up PC */
dms_upd_sr ();                                          /* update dms_sr */
if (reason == STOP_HALT)                                /* programmed halt? */
    cpu_set_ldr (NULL, FALSE, NULL, NULL);              /* disable loader (ignore errors) */
for (i = 0; dptr = sim_devices[i]; i++) {               /* loop thru dev */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if (dibp) {                                         /* exist? */
        dev = dibp->devno;
        dibp->cmd = CMD (dev);
        dibp->ctl = CTL (dev);
        dibp->flg = FLG (dev);
        dibp->fbf = FBF (dev);
        dibp->srq = SRQ (dev);
        }
    }
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return reason;
}

/* Resolve indirect addresses.

   An indirect chain is followed until a direct address is obtained.  Under
   simulation, a maximum number of indirect levels are allowed (typically 16),
   after which the instruction will be aborted.

   If the memory protect feature is present, an indirect counter is used that
   allows a pending interrupt to be serviced if more than three levels of
   indirection are encountered.  If MP jumper W6 ("INT") is out and MP is
   enabled, then pending interrupts are serviced immediately.  When employing
   the indirect counter, the hardware clears a pending interrupt deferral after
   the third indirection and aborts the instruction after the fourth.
*/

t_stat resolve (uint32 MA, uint32 *addr, uint32 irq)
{
uint32 i;
t_bool pending = (irq && !(mp_unit.flags & DEV_DIS));
t_bool int_enable = ((mp_unit.flags & UNIT_MP_INT) && CTL(PRO));

for (i = 0; (i < ind_max) && (MA & I_IA); i++) {        /* resolve multilevel */
    if (pending) {                                      /* interrupt pending and MP enabled? */
        if ((i == 2) || int_enable)                     /* 3rd level indirect or INT out? */
            ion_defer = 0;                              /* reenable interrrupts */
        if ((i > 2) || int_enable)                      /* 4th or higher or INT out? */
            return NOTE_INDINT;                         /* break out now */
        }
    MA = ReadW (MA & VAMASK);                           /* follow address chain */
    }
if (MA & I_IA) return STOP_IND;                         /* indirect loop? */
*addr = MA;
return SCPE_OK;
}

/* Get effective address from IR */

t_stat Ea (uint32 IR, uint32 *addr, uint32 irq)
{
uint32 MA;

MA = IR & (I_IA | I_DISP);                              /* ind + disp */
if (IR & I_CP) MA = ((PC - 1) & I_PAGENO) | MA;         /* current page? */
return resolve (MA, addr, irq);                         /* resolve indirects */
}

/* Shift micro operation */

uint32 shift (uint32 t, uint32 flag, uint32 op)
{
uint32 oldE;

op = op & 07;                                           /* get shift op */
if (flag) {                                             /* enabled? */
    switch (op) {                                       /* case on operation */

    case 00:                                            /* signed left shift */
        return ((t & SIGN) | ((t << 1) & 077777));

    case 01:                                            /* signed right shift */
        return ((t & SIGN) | (t >> 1));

    case 02:                                            /* rotate left */
        return (((t << 1) | (t >> 15)) & DMASK);

    case 03:                                            /* rotate right */
        return (((t >> 1) | (t << 15)) & DMASK);

    case 04:                                            /* left shift, 0 sign */
        return ((t << 1) & 077777);

    case 05:                                            /* ext right rotate */
        oldE = E;
        E = t & 1;
        return ((t >> 1) | (oldE << 15));

    case 06:                                            /* ext left rotate */
        oldE = E;
        E = (t >> 15) & 1;
        return (((t << 1) | oldE) & DMASK);

    case 07:                                            /* rotate left four */
        return (((t << 4) | (t >> 12)) & DMASK);
        }                                               /* end case */
    }                                                   /* end if */
if (op == 05) E = t & 1;                                /* disabled ext rgt rot */
if (op == 06) E = (t >> 15) & 1;                        /* disabled ext lft rot */
return t;                                               /* input unchanged */
}

/* IO instruction decode */

t_stat iogrp (uint32 ir, uint32 iotrap)
{
uint32 dev, sop, iodata, iostat, ab;

ab = (ir & I_AB)? 1: 0;                                 /* get A/B select */
dev = ir & I_DEVMASK;                                   /* get device */
sop = I_GETIOOP (ir);                                   /* get subopcode */
if (!iotrap && CTL (PRO) &&                             /* protected? */
    ((sop == ioHLT) ||                                  /* halt or !ovf? */
    ((dev != OVF) && (mp_unit.flags & UNIT_MP_SEL1)))) {
        if (sop == ioLIX) ABREG[ab] = 0;                /* A/B writes anyway */
        ABORT (ABORT_PRO);
        }
iodata = devdisp (dev, sop, ir, ABREG[ab]);             /* process I/O */
ion_defer = defer_tab[sop];                             /* set defer */
if ((sop == ioMIX) || (sop == ioLIX))                   /* store ret data */
    ABREG[ab] = iodata & DMASK;
if (sop == ioHLT) {                                     /* halt? */
    int32 len = strlen (halt_msg);                      /* find end msg */
    sprintf (&halt_msg[len - 6], "%06o", ir);           /* add the halt */
    return STOP_HALT;
    }
iostat = iodata >> IOT_V_REASON;
if (iostat == SCPE_OK) return NOTE_IOG;                 /* normal status */
else return iostat;                                     /* abnormal status */
}

/* Device dispatch */

uint32 devdisp (uint32 devno, uint32 inst, uint32 IR, uint32 dat)
{
if (dtab[devno]) return dtab[devno] (inst, IR, dat);
else return nulio (inst, IR, dat);
}

/* Calculate DMA requests */

uint32 calc_dma (void)
{
uint32 r = 0;

if (CMD (DMA0) && SRQ (dmac[0].cw1 & I_DEVMASK))        /* check DMA0 cycle */
    r = r | DMAR0;
if (CMD (DMA1) && SRQ (dmac[1].cw1 & I_DEVMASK))        /* check DMA1 cycle */
    r = r | DMAR1;
return r;
}

/* Calculate interrupt requests

   This routine takes into account all the relevant state of the
   interrupt system: ion, dev_flg, dev_fbf, and dev_ctl.

   1. dev_flg & dev_ctl determines the end of the priority grant.
      The break in the chain will occur at the first device for
      which dev_flg & dev_ctl is true.  This is determined by
      AND'ing the set bits with their 2's complement; only the low
      order (highest priority) bit will differ.  1 less than
      that, or'd with the single set bit itself, is the mask of
      possible interrupting devices.  If ION is clear, only devices
      4 and 5 are eligible to interrupt.
   2. dev_flg & dev_ctl & dev_fbf determines the outstanding
      interrupt requests.  All three bits must be on for a device
      to request an interrupt.  This is the masked under the
      result from #1 to determine the highest priority interrupt,
      if any.
*/

uint32 calc_int (void)
{
int32 j, lomask, mask[2], req[2];

lomask = dev_flg[0] & dev_ctl[0] & ~M_NXDEV;            /* start chain calc */
req[0] = lomask & dev_fbf[0];                           /* calc requests */
lomask = lomask & (-lomask);                            /* chain & -chain */
mask[0] = lomask | (lomask - 1);                        /* enabled devices */
req[0] = req[0] & mask[0];                              /* highest request */
if (ion) {                                              /* ion? */
    if (lomask == 0) {                                  /* no break in chn? */
        mask[1] = dev_flg[1] & dev_ctl[1];              /* do all devices */
        req[1] = mask[1] & dev_fbf[1];
        mask[1] = mask[1] & (-mask[1]);
        mask[1] = mask[1] | (mask[1] - 1);
        req[1] = req[1] & mask[1];
        }
    else req[1] = 0;
    }
else {
    req[0] = req[0] & (INT_M (PWR) | INT_M (PRO));
    req[1] = 0;
    }
if (req[0]) {                                           /* if low request */
    for (j = 0; j < 32; j++) {                          /* find dev # */
        if (req[0] & INT_M (j)) return j;
        }
    }
if (req[1]) {                                           /* if hi request */
    for (j = 0; j < 32; j++) {                          /* find dev # */
        if (req[1] & INT_M (j)) return (32 + j);
        }
    }
return 0;
}

/* Memory access routines */

uint8 ReadB (uint32 va)
{
int32 pa;

if (dms_enb) pa = dms (va >> 1, dms_ump, RD);
else pa = va >> 1;
if (va & 1) return (ReadPW (pa) & 0377);
else return ((ReadPW (pa) >> 8) & 0377);
}

uint8 ReadBA (uint32 va)
{
uint32 pa;

if (dms_enb) pa = dms (va >> 1, dms_ump ^ MAP_LNT, RD);
else pa = va >> 1;
if (va & 1) return (ReadPW (pa) & 0377);
else return ((ReadPW (pa) >> 8) & 0377);
}

uint16 ReadW (uint32 va)
{
uint32 pa;

if (dms_enb) pa = dms (va, dms_ump, RD);
else pa = va;
return ReadPW (pa);
}

uint16 ReadWA (uint32 va)
{
uint32 pa;

if (dms_enb) pa = dms (va, dms_ump ^ MAP_LNT, RD);
else pa = va;
return ReadPW (pa);
}

uint16 ReadIO (uint32 va, uint32 map)
{
uint32 pa;

if (dms_enb) pa = dms_io (va, map);
else pa = va;
return M[pa];
}

uint16 ReadPW (uint32 pa)
{
if (pa <= 1) return ABREG[pa];
return M[pa];
}

uint16 ReadTAB (uint32 addr)
{
if (addr == 0) return saved_AR;
else if (addr == 1) return saved_BR;
else return ReadIO (addr, dms_ump);
}

/* Memory protection test for writes

   From Dave Bryan: The problem is that memory writes aren't being checked for
   an MP violation if DMS is enabled, i.e., if DMS is enabled, and the page is
   writable, then whether the target is below the MP fence is not checked. [The
   simulator must] do MP check on all writes after DMS translation and violation
   checks are done (so, to pass, the page must be writable AND the target must
   be above the MP fence).
*/

#define MP_TEST(x)      (CTL (PRO) && ((x) > 1) && ((x) < mp_fence))

void WriteB (uint32 va, uint32 dat)
{
uint32 pa, t;

if (dms_enb) pa = dms (va >> 1, dms_ump, WR);
else pa = va >> 1;
if (MP_TEST (va >> 1)) ABORT (ABORT_PRO);
if (MEM_ADDR_OK (pa)) {
    t = ReadPW (pa);
    if (va & 1) t = (t & 0177400) | (dat & 0377);
    else t = (t & 0377) | ((dat & 0377) << 8);
    WritePW (pa, t);
    }
return;
}

void WriteBA (uint32 va, uint32 dat)
{
uint32 pa, t;

if (dms_enb) {
    dms_viol (va >> 1, MVI_WPR);                        /* viol if prot */
    pa = dms (va >> 1, dms_ump ^ MAP_LNT, WR);
    }
else pa = va >> 1;
if (MP_TEST (va >> 1)) ABORT (ABORT_PRO);
if (MEM_ADDR_OK (pa)) {
    t = ReadPW (pa);
    if (va & 1) t = (t & 0177400) | (dat & 0377);
    else t = (t & 0377) | ((dat & 0377) << 8);
    WritePW (pa, t);
    }
return;
}

void WriteW (uint32 va, uint32 dat)
{
uint32 pa;

if (dms_enb) pa = dms (va, dms_ump, WR);
else pa = va;
if (MP_TEST (va)) ABORT (ABORT_PRO);
if (MEM_ADDR_OK (pa)) WritePW (pa, dat);
return;
}

void WriteWA (uint32 va, uint32 dat)
{
int32 pa;

if (dms_enb) {
    dms_viol (va, MVI_WPR);                             /* viol if prot */
    pa = dms (va, dms_ump ^ MAP_LNT, WR);
    }
else pa = va;
if (MP_TEST (va)) ABORT (ABORT_PRO);
if (MEM_ADDR_OK (pa)) WritePW (pa, dat);
return;
}

void WriteIO (uint32 va, uint32 dat, uint32 map)
{
uint32 pa;

if (dms_enb) pa = dms_io (va, map);
else pa = va;
if (MEM_ADDR_OK (pa)) M[pa] = dat & DMASK;
return;
}

void WritePW (uint32 pa, uint32 dat)
{
if (pa <= 1) ABREG[pa] = dat & DMASK;
else M[pa] = dat & DMASK;
return;
}

/* DMS relocation for CPU access */

uint32 dms (uint32 va, uint32 map, uint32 prot)
{
uint32 pgn, mpr;

if (va <= 1) return va;                                 /* A, B */
pgn = VA_GETPAG (va);                                   /* get page num */
if (pgn == 0) {                                         /* base page? */
    uint32 dms_fence = dms_sr & MST_FENCE;              /* get fence value */
    if ((dms_sr & MST_FLT)?                             /* check unmapped */
        (va >= dms_fence):                              /* 1B10: >= fence */
        (va < dms_fence)) {                             /* 0B10: < fence */
        if (prot == WR) dms_viol (va, MVI_BPG);         /* if W, viol */
        return va;                                      /* no mapping */
        }
    }
mpr = dms_map[map + pgn];                               /* get map reg */
if (mpr & prot) dms_viol (va, prot);                    /* prot violation? */
return (MAP_GETPAG (mpr) | VA_GETOFF (va));
}

/* DMS relocation for IO access */

uint32 dms_io (uint32 va, uint32 map)
{
uint32 pgn, mpr;

if (va <= 1) return va;                                 /* A, B */
pgn = VA_GETPAG (va);                                   /* get page num */
if (pgn == 0) {                                         /* base page? */
    uint32 dms_fence = dms_sr & MST_FENCE;              /* get fence value */
    if ((dms_sr & MST_FLT)?                             /* check unmapped */
        (va >= dms_fence):                              /* 1B10: >= fence */
        (va < dms_fence)) {                             /* 0B10: < fence */
        return va;                                      /* no mapping */
        }
    }
mpr = dms_map[map + pgn];                               /* get map reg */
return (MAP_GETPAG (mpr) | VA_GETOFF (va));
}

/* DMS relocation for console access */

uint32 dms_cons (uint32 va, int32 sw)
{
uint32 map_sel;

if (sw & SWMASK ('V')) map_sel = dms_ump;               /* switch? select map */
else if (sw & SWMASK ('S')) map_sel = SMAP;
else if (sw & SWMASK ('U')) map_sel = UMAP;
else if (sw & SWMASK ('P')) map_sel = PAMAP;
else if (sw & SWMASK ('Q')) map_sel = PBMAP;
else return va;                                         /* no switch, physical */
if (va >= VASIZE) return MEMSIZE;                       /* virtual, must be 15b */
else if (dms_enb) return dms_io (va, map_sel);          /* DMS on? go thru map */
else return va;                                         /* else return virtual */
}

/* Mem protect and DMS validation for jumps */

void mp_dms_jmp (uint32 va)
{
uint32 pgn = VA_GETPAG (va);                            /* get page num */

if ((pgn == 0) && (va > 1)) {                           /* base page? */
    uint32 dms_fence = dms_sr & MST_FENCE;              /* get fence value */
    if ((dms_sr & MST_FLT)?                             /* check unmapped */
        (va >= dms_fence):                              /* 1B10: >= fence */
        (va < dms_fence)) {                             /* 0B10: < fence */
        dms_viol (va, MVI_BPG);                         /* if W, viol */
        return;                                         /* PRO not set */
        }
    }
if (CTL (PRO) && (va < mp_fence)) ABORT (ABORT_PRO);    /* base page MPR */
return;
}

/* DMS read and write maps */

uint16 dms_rmap (uint32 mapi)
{
mapi = mapi & MAP_MASK;
return (dms_map[mapi] & ~MAP_MBZ);
}

void dms_wmap (uint32 mapi, uint32 dat)
{
mapi = mapi & MAP_MASK;
dms_map[mapi] = (uint16) (dat & ~MAP_MBZ);
return;
}

/* DMS violation */

void dms_viol (uint32 va, uint32 st)
{
dms_vr = st | VA_GETPAG (va) |
    ((st & (MVI_RPR | MVI_WPR))? MVI_MEB: 0) |          /* set MEB */
    (dms_enb? MVI_MEM: 0) |                             /* set MEM */
    (dms_ump? MVI_UMP: 0);                              /* set UMAP */
if (CTL (PRO)) {                                        /* protected? */
    mp_mevff = 1;                                       /* signal dms */
    ABORT (ABORT_PRO);                                  /* abort */
    }
return;
}

/* DMS update status */

uint32 dms_upd_sr (void)
{
dms_sr = dms_sr & ~(MST_ENB | MST_UMP | MST_PRO);
if (dms_enb) dms_sr = dms_sr | MST_ENB;
if (dms_ump) dms_sr = dms_sr | MST_UMP;
if (CTL (PRO)) dms_sr = dms_sr | MST_PRO;
return dms_sr;
}

/* Device 0 (CPU) I/O routine

   NOTE: LIx/MIx reads floating I/O bus (0 on all machines).

   NOTE: CLC 0 issues CRS to all devices, not CLC.  While most cards react
   identically to CRS and CLC, some do not, e.g., the 12566B when used as an
   I/O diagnostic target.  PRESET also issues CRS (with POPIO).

   From Dave Bryan: RTE uses the undocumented instruction "SFS 0,C" to both test
   and turn off the interrupt system.  This is confirmed in the "RTE-6/VM
   Technical Specifications" manual (HP 92084-90015), section 2.3.1 "Process
   the Interrupt", subsection "A.1 $CIC":

   "Test to see if the interrupt system is on or off. This is done with the
    SFS 0,C instruction.  In either case, turn it off (the ,C does it)."

   ...and in section 5.8, "Parity Error Detection":

   "Because parity error interrupts can occur even when the interrupt system
    is off, the code at $CIC must be able to save the complete system status.
    The major hole in being able to save the complete state is in saving the
    interrupt system state. In order to do this in both the 21MX and the 21XE
    the instruction 103300 was used to both test the interrupt system and
    turn it off."
*/

int32 cpuio (int32 inst, int32 IR, int32 dat)
{
int i;

switch (inst) {                                         /* case on opcode */

    case ioFLG:                                         /* flag */
        ion = (IR & I_HC)? 0: 1;                        /* interrupts off/on */
        return dat;

    case ioSFC:                                         /* skip flag clear */
        if (!ion) PC = (PC + 1) & VAMASK;
        break;

    case ioSFS:                                         /* skip flag set */
        if (ion) PC = (PC + 1) & VAMASK;
        break;

    case ioLIX:                                         /* load */
        dat = 0;                                        /* returns 0 */
        break;

    case ioCTL:                                         /* control */
        if (IR & I_CTL)                                 /* CLC 0 sends CRS */
            for (i = 0; i <= I_DEVMASK; i++)            /* to all devices */
                devdisp (i, ioCRS, I_CTL + i, 0);       /* IR -> "CLC i" for convenience */
        break;

    default:
        break;
        }

if (IR & I_HC) ion = 0;                                 /* HC option */
return dat;
}

/* Device 1 (overflow/S-register) I/O routine

   NOTE: The S register is read-only on the 2115/2116.  It is read/write on
   the 2114, 2100, and 1000.
*/

int32 ovfio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {                                         /* case on opcode */

    case ioFLG:                                         /* flag */
        O = (IR & I_HC)? 0: 1;                          /* clear/set overflow */
        return dat;

    case ioSFC:                                         /* skip flag clear */
        if (!O) PC = (PC + 1) & VAMASK;
        break;                                          /* can clear flag */

    case ioSFS:                                         /* skip flag set */
        if (O) PC = (PC + 1) & VAMASK;
        break;                                          /* can clear flag */

    case ioMIX:                                         /* merge */
        dat = dat | SR;
        break;

    case ioLIX:                                         /* load */
        dat = SR;
        break;

    case ioOTX:                                         /* output */
        if ((UNIT_CPU_MODEL != UNIT_2116) &&
            (UNIT_CPU_MODEL != UNIT_2115))
            SR = dat;
        break;

    default:
        break;
        }

if (IR & I_HC) O = 0;                                   /* HC option */
return dat;
}

/* Device 4 (power fail) I/O routine */

int32 pwrio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {                                         /* case on opcode */

    case ioMIX:                                         /* merge */
        dat = dat | intaddr;
        break;

    case ioLIX:                                         /* load */
        dat = intaddr;
        break;

    default:
        break;
        }

return dat;
}

/* Device 5 (memory protect) I/O routine

   From Dave Bryan: Examination of the schematics for the MP card in the
   engineering documentation shows that the SFS and SFC I/O backplane signals
   gate the output of the MEVFF onto the SKF line unconditionally.
*/

int32 proio (int32 inst, int32 IR, int32 dat)
{
switch (inst) {                                         /* case on opcode */

    case ioSFC:                                         /* skip flag clear */
        if (!mp_mevff) PC = (PC + 1) & VAMASK;          /* skip if mem prot */
        break;

    case ioSFS:                                         /* skip flag set */
        if (mp_mevff) PC = (PC + 1) & VAMASK;           /* skip if DMS */
        break;

    case ioMIX:                                         /* merge */
        dat = dat | mp_viol;
        break;

    case ioLIX:                                         /* load */
        dat = mp_viol;
        break;

    case ioOTX:                                         /* output */
        mp_fence = dat & VAMASK;
        if (cpu_unit.flags & UNIT_2100) iop_sp = mp_fence;
        break;

    case ioCRS:                                         /* control reset */
    case ioCTL:                                         /* control clear/set */
        if ((IR & I_CTL) == 0) {                        /* STC */
            setCTL (PRO);
            dms_vr = 0;
            mp_evrff = 1;                               /* allow mp_viol upd */
            mp_mevff = 0;                               /* clear DMS flag */
            }
        break;

    default:
        break;
        }

if (IR & I_HC) { clrFLG (PRO); }                        /* HC option */
return dat;
}

/* Devices 2,3 (secondary DMA) I/O routine.

   Implements control word 2 (memory address) and control word 3 (word count).

   The 12607B (2114) supports 14-bit addresses and 13-bit word counts.
   The 12578A (2115/6) supports 15-bit addresses and 14-bit word counts.
   The 12895A (2100) and 12897B (1000) support 15-bit addresses and 16-bit word
   counts.

   Note: because the I/O bus floats to zero on 211x computers, LIA/MIA (word
   count) returns zeros in the unused bit locations, even though the word count
   is a negative value.
*/

int32 dmsio (int32 inst, int32 IR, int32 dat)
{
int32 ch;

ch = IR & 1;                                            /* get channel num */
switch (inst) {                                         /* case on opcode */

    case ioLIX:                                         /* load remaining word count */
        dat = 0;

    case ioMIX:                                         /* merge */
        if (UNIT_CPU_MODEL == UNIT_2114)                /* 2114? */
            dat = dat | (dmac[ch].cw3 & 0017777);       /* only 13-bit count */
        else if (UNIT_CPU_TYPE == UNIT_TYPE_211X)       /* 2115/2116? */
            dat = dat | (dmac[ch].cw3 & 0037777);       /* only 14-bit count */
        else
            dat = dat | dmac[ch].cw3;                   /* rest use full value */
        break;

    case ioOTX:                                         /* output */
        if (CTL (DMALT0 + ch))                          /* word count selected? */
            dmac[ch].cw3 = dat;                         /* save count */
        else                                            /* memory address selected */
            if (UNIT_CPU_MODEL == UNIT_2114)            /* 2114? */
                dmac[ch].cw2 = dat & 0137777;           /* 14-bit address */
            else
                dmac[ch].cw2 = dat;                     /* full address stored */
        break;

    case ioCRS:                                         /* control reset */
    case ioCTL:                                         /* control clear/set */
        if (IR & I_CTL) { clrCTL (DMALT0 + ch); }       /* CLC */
        else { setCTL (DMALT0 + ch); }                  /* STC */
        break;

    default:
        break;
        }

return dat;
}

/* Devices 6,7 (primary DMA) I/O routine

   Implements control word 1 (device address) and DMA control.

   The 12607B (2114) stores only bits 2-0 of the select code and interprets them
   as select codes 10-16 (SRQ17 is not decoded).  The 12578A (2115/6), 12895A
   (2100), and 12897B (1000) support the full 10-77 range of select codes.

   The 12578A supports byte-sized transfers by setting bit 14.  Bit 14 is
   ignored by all other DMA cards, which support word transfers only.

   NOTE: LIx/MIx reads floating S-bus (1 on 21MX, 0 on 211x/2100).

   NOTE: CRS clears control and command flip-flops, whereas CLC clears only
   control.
*/

int32 dmpio (int32 inst, int32 IR, int32 dat)
{
int32 ch;

ch = IR & 1;                                            /* get channel number */

switch (inst) {                                         /* case on opcode */

    case ioFLG:                                         /* flag */
        if ((IR & I_HC) == 0) {                         /* set->abort */
            setFLG (DMA0 + ch);                         /* set flag */
            clrCMD (DMA0 + ch);                         /* clr cmd */
            }
        break;

    case ioSFC:                                         /* skip flag clear */
        if (FLG (DMA0 + ch) == 0) PC = (PC + 1) & VAMASK;
        break;

    case ioSFS:                                         /* skip flag set */
        if (FLG (DMA0 + ch) != 0) PC = (PC + 1) & VAMASK;
        break;

    case ioLIX:                                         /* load */
        dat = 0;

    case ioMIX:                                         /* merge */
        if (UNIT_CPU_TYPE == UNIT_TYPE_1000)
            dat = DMASK;
        break;

    case ioOTX:                                         /* output */
        if (UNIT_CPU_MODEL == UNIT_2114)                /* 12607? */
            dmac[ch].cw1 = (dat & 0137707) | 010;       /* mask SC, convert to 10-17 */
        else if (UNIT_CPU_TYPE == UNIT_TYPE_211X)       /* 12578? */
            dmac[ch].cw1 = dat;                         /* store full select code, flags */
        else                                            /* 12895, 12897 */
            dmac[ch].cw1 = dat & ~DMA1_PB;              /* clip byte-packing flag */
        break;

    case ioCRS:                                         /* control reset */
        clrCMD (DMA0 + ch);                             /* clear command flip-flop */

    case ioCTL:                                         /* control */
        if (IR & I_CTL) { clrCTL (DMA0 + ch); }         /* CLC: cmd unchgd */
        else {                                          /* STC */
            if (UNIT_CPU_TYPE == UNIT_TYPE_211X)        /* slow DMA card? */
                dmac[ch].latency = 1;                   /* needs startup latency */
            else
                dmac[ch].latency = 0;                   /* DCPC starts immediately */

            dmac[ch].packer = 0;                        /* clear packing register */
            setCTL (DMA0 + ch);                         /* set ctl, cmd */
            setCMD (DMA0 + ch);
            }
        break;

    default:
        break;
        }

if (IR & I_HC) { clrFLG (DMA0 + ch); }                  /* HC option */
return dat;
}

/* DMA cycle routine

   The 12578A card supports byte-packing.  If bit 14 in control word 1 is set,
   each transfer will involve one read/write from memory and two output/input
   operations in order to transfer sequential bytes to/from the device.

   The last cycle (word count reaches 0) logic is quite tricky.
   Input cases:
   - CLC requested: issue CLC
   Output cases:
   - neither STC nor CLC requested: issue CLF
   - STC requested but not CLC: issue STC,C
   - CLC requested but not STC: issue CLC,C
   - STC and CLC both requested: issue STC,C and CLC,C, in that order
   Either: issue EDT
*/

void dma_cycle (uint32 ch, uint32 map)
{
int32 temp, dev, MA;
int32 inp = dmac[ch].cw2 & DMA2_OI;                     /* input flag */
int32 byt = dmac[ch].cw1 & DMA1_PB;                     /* pack bytes flag */

if (dmac[ch].latency) {                                 /* start-up latency? */
    dmac[ch].latency = dmac[ch].latency - 1;            /* decrease it */
    return;                                             /* that's all this cycle */
    }

dev = dmac[ch].cw1 & I_DEVMASK;                         /* get device */
MA = dmac[ch].cw2 & VAMASK;                             /* get mem addr */

if (inp) {                                              /* input cycle? */
    temp = devdisp (dev, ioLIX, dev, 0);                /* do LIA dev */

    if (byt) {                                          /* byte packing? */
        if (dmac[ch].packer & DMA_OE) {                 /* second byte? */
            temp = (dmac[ch].packer << 8) |             /* merge stored byte */
                   (temp & DMASK8);
            WriteIO (MA, temp, map);                    /* store word data */
            }
        else                                            /* first byte */
            dmac[ch].packer = (temp & DMASK8);          /* save it */

        dmac[ch].packer = dmac[ch].packer ^ DMA_OE;     /* flip odd/even bit */
        }
    else                                                /* no byte packing */
        WriteIO (MA, temp, map);                        /* store word data */
    }
else {                                                  /* output cycle */
    if (byt) {                                          /* byte packing? */
        if (dmac[ch].packer & DMA_OE)                   /* second byte? */
            temp = dmac[ch].packer & DMASK8;            /* retrieve it */

        else {                                          /* first byte */
            dmac[ch].packer = ReadIO (MA, map);         /* read word data */
            temp = (dmac[ch].packer >> 8) & DMASK8;     /* get high byte */
            }

        dmac[ch].packer = dmac[ch].packer ^ DMA_OE;     /* flip odd/even bit */
        }
    else                                                /* no byte packing */
        temp = ReadIO (MA, map);                        /* read word data */

    devdisp (dev, ioOTX, dev, temp);                    /* do OTA dev */
    }

if ((dmac[ch].packer & DMA_OE) == 0) {                  /* new byte or no packing? */
    dmac[ch].cw2 = (dmac[ch].cw2 & DMA2_OI) |           /* increment address */
                   ((dmac[ch].cw2 + 1) & VAMASK);
    dmac[ch].cw3 = (dmac[ch].cw3 + 1) & DMASK;          /* increment word count */
    }

if (dmac[ch].cw3) {                                     /* more to do? */
    if (dmac[ch].cw1 & DMA1_STC)                        /* if STC flag, */
        devdisp (dev, ioCTL, I_HC + dev, 0);            /* do STC,C dev */
    else devdisp (dev, ioFLG, I_HC + dev, 0);           /* else CLF dev */
    }
else {
    if (inp) {                                          /* last cycle, input? */
        if (dmac[ch].cw1 & DMA1_CLC)                    /* CLC at end? */
            devdisp (dev, ioCTL, I_CTL + dev, 0);       /* yes */
        }                                               /* end input */
    else {                                              /* output */
        if ((dmac[ch].cw1 & (DMA1_STC | DMA1_CLC)) == 0)
            devdisp (dev, ioFLG, I_HC + dev, 0);        /* clear flag */
        if (dmac[ch].cw1 & DMA1_STC)                    /* if STC flag, */
            devdisp (dev, ioCTL, I_HC + dev, 0);        /* do STC,C dev */
        if (dmac[ch].cw1 & DMA1_CLC)                    /* CLC at end? */
            devdisp (dev, ioCTL, I_HC + I_CTL + dev, 0);        /* yes */
        }                                               /* end output */
    setFLG (DMA0 + ch);                                 /* set DMA flg */
    clrCMD (DMA0 + ch);                                 /* clr DMA cmd */
    devdisp (dev, ioEDT, dev, 0);                       /* do EDT */
    }
return;
}

/* Unimplemented device routine

   NOTE: For SC < 10, LIx/MIx reads floating S-bus (-1 on 21MX, 0 on 211x/2100).
         For SC >= 10, LIx/MIx reads floating I/O bus (0 on all machines).
*/

int32 nulio (int32 inst, int32 IR, int32 dat)
{
int32 devd;

devd = IR & I_DEVMASK;                                  /* get device no */
switch (inst) {                                         /* case on opcode */

    case ioSFC:                                         /* skip flag clear */
        PC = (PC + 1) & VAMASK;
        break;

    case ioLIX:                                         /* load */
        dat = 0;

    case ioMIX:                                         /* merge */
        if ((devd < VARDEV) && (UNIT_CPU_TYPE == UNIT_TYPE_1000))
            dat = DMASK;
        break;

    default:
        break;
        }

return (stop_dev << IOT_V_REASON) | dat;
}

/* Reset routines */

t_stat cpu_reset (DEVICE *dptr)
{
E = 0;
O = 0;
ion = ion_defer = 0;
clrCMD (PWR);
clrCTL (PWR);
clrFLG (PWR);
clrFBF (PWR);
dev_srq[0] = dev_srq[0] & ~M_FXDEV;
dms_enb = dms_ump = 0;                                  /* init DMS */
dms_sr = 0;
dms_vr = 0;
pcq_r = find_reg ("PCQ", NULL, dptr);
sim_brk_types = ALL_BKPTS;
sim_brk_dflt = SWMASK ('E');

if (M == NULL) {                                        /* initial call? */
    M = calloc (PASIZE, sizeof (uint16));               /* alloc mem */

    if (M == NULL)                                      /* alloc fail? */
        return SCPE_MEM;
    else {                                              /* do one-time init */
        MEMSIZE = 32768;                                /* set initial memory size */
        cpu_set_model (NULL, UNIT_2116, NULL, NULL);    /* set initial CPU model */
        SR = 001000;                                    /* select PTR boot ROM at SC 10 */
        cpu_boot (0, NULL);                             /* install loader for 2116 */
        cpu_set_ldr (NULL, FALSE, NULL, NULL);          /* disable loader (was enabled) */
        SR = 0;                                         /* clear S */
        sim_vm_post = &hp_post_cmd;                     /* set cmd post proc */
        }
}

if (pcq_r) pcq_r->qptr = 0;
else return SCPE_IERR;
return SCPE_OK;
}

t_stat mp_reset (DEVICE *dptr)
{
clrCTL (PRO);
clrFLG (PRO);
clrFBF (PRO);
mp_fence = 0;                                           /* init mprot */
mp_viol = 0;
mp_mevff = 0;
mp_evrff = 1;
return SCPE_OK;
}

t_stat dma0_reset (DEVICE *tptr)
{
if (UNIT_CPU_MODEL != UNIT_2114)                        /* 2114 has only one channel */
    hp_enbdis_pair (&dma0_dev, &dma1_dev);              /* make pair cons */
clrCMD (DMA0);
clrCTL (DMA0);
setFLG (DMA0);
clrSRQ (DMA0);
clrCTL (DMALT0);
dmac[0].latency = dmac[0].packer = 0;
if (sim_switches & SWMASK ('P'))                        /* power up? */
    dmac[0].cw1 = dmac[0].cw2 = dmac[0].cw3 = 0;
return SCPE_OK;
}

t_stat dma1_reset (DEVICE *tptr)
{
if (UNIT_CPU_MODEL != UNIT_2114)                        /* 2114 has only one channel */
    hp_enbdis_pair (&dma1_dev, &dma0_dev);              /* make pair cons */
clrCMD (DMA1);
clrCTL (DMA1);
setFLG (DMA1);
clrSRQ (DMA1);
clrCTL (DMALT1);
dmac[1].latency = dmac[1].packer = 0;
if (sim_switches & SWMASK ('P'))                        /* power up? */
    dmac[1].cw1 = dmac[1].cw2 = dmac[1].cw3 = 0;
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
int32 d;

addr = dms_cons (addr, sw);
if (addr >= MEMSIZE) return SCPE_NXM;
if (!(sw & SIM_SW_REST) && (addr == 0)) d = saved_AR;
else if (!(sw & SIM_SW_REST) && (addr == 1)) d = saved_BR;
else d = M[addr];
if (vptr != NULL) *vptr = d & DMASK;
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
addr = dms_cons (addr, sw);
if (addr >= MEMSIZE) return SCPE_NXM;
if (!(sw & SIM_SW_REST) && (addr == 0)) saved_AR = val & DMASK;
else if (!(sw & SIM_SW_REST) && (addr == 1)) saved_BR = val & DMASK;
else M[addr] = val & DMASK;
return SCPE_OK;
}

/* Set device number */

t_stat hp_setdev (UNIT *uptr, int32 num, char *cptr, void *desc)
{
DEVICE *dptr = (DEVICE *) desc;
DIB *dibp;
int32 i, newdev;
t_stat r;

if (cptr == NULL) return SCPE_ARG;
if ((desc == NULL) || (num > 1)) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
newdev = get_uint (cptr, 8, I_DEVMASK - num, &r);
if (r != SCPE_OK) return r;
if (newdev < VARDEV) return SCPE_ARG;
for (i = 0; i <= num; i++, dibp++) dibp->devno = newdev + i;
return SCPE_OK;
}

/* Show device number */

t_stat hp_showdev (FILE *st, UNIT *uptr, int32 num, void *desc)
{
DEVICE *dptr = (DEVICE *) desc;
DIB *dibp;
int32 i;

if ((desc == NULL) || (num > 1)) return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL) return SCPE_IERR;
fprintf (st, "devno=%o", dibp->devno);
for (i = 1; i <= num; i++) fprintf (st, "/%o", dibp->devno + i);
return SCPE_OK;
}

/* Make a pair of devices consistent */

void hp_enbdis_pair (DEVICE *ccp, DEVICE *dcp)
{
if (ccp->flags & DEV_DIS) dcp->flags = dcp->flags | DEV_DIS;
else dcp->flags = dcp->flags & ~DEV_DIS;
return;
}

/* VM command post-processor

   Update T register to contents of memory addressed by M register
   if M register has changed. */

void hp_post_cmd (t_bool from_scp)
{
if (MR != saved_MR) {                                   /* M changed since last update? */
    saved_MR = MR;
    TR = ReadTAB (MR);                                  /* sync T with new M */
    }
return;
}

/* Test for device conflict */

t_bool dev_conflict (void)
{
DEVICE *dptr;
DIB *dibp;
uint32 i, j, k;
t_bool is_conflict = FALSE;
uint32 conflicts[I_DEVMASK + 1] = { 0 };

for (i = 0; dptr = sim_devices[i]; i++) {
    dibp = (DIB *) dptr->ctxt;
    if (dibp && !(dptr->flags & DEV_DIS))
        if (++conflicts[dibp->devno] > 1)
            is_conflict = TRUE;
    }

if (is_conflict) {
    sim_ttcmd();
    for (i = 0; i <= I_DEVMASK; i++) {
        if (conflicts[i] > 1) {
            k = conflicts[i];
            printf ("Select code %o conflict:", i);
            if (sim_log) fprintf (sim_log, "Select code %o conflict:", i);
            for (j = 0; dptr = sim_devices[j]; j++) {
                dibp = (DIB *) dptr->ctxt;
                if (dibp && !(dptr->flags & DEV_DIS) && (i == dibp->devno)) {
                    if (k < conflicts[i]) {
                        printf (" and");
                        if (sim_log) fputs (" and", sim_log);
                        }
                    printf (" %s", sim_dname (dptr));
                    if (sim_log) fprintf (sim_log, " %s", sim_dname (dptr));
                    k = k - 1;
                    if (k == 0) {
                        putchar ('\n');
                        if (sim_log) fputc ('\n', sim_log);
                        break;
                        }
                    }
                }
            }
        }
    }
return is_conflict;
}

/* Change CPU memory size.

   On a 21xx, move the current loader to the top of the new memory size.  Then
   clear "non-existent memory" so that reads return zero, per spec.

   Validation:
   - New size <= maximum size for current CPU.
   - New size a positive multiple of 4K (progamming error if not).
   - If new size < old size, truncation accepted.
*/

t_stat cpu_set_size (UNIT *uptr, int32 new_size, char *cptr, void *desc)
{
int32 mc = 0;
uint32 i;
uint32 model = CPU_MODEL_INDEX;                         /* current CPU model index */
uint32 old_size = MEMSIZE;                              /* current memory size */

if ((uint32) new_size > cpu_features[model].maxmem)
    return SCPE_NOFNC;                                  /* mem size unsupported */

if ((new_size <= 0) || (new_size > PASIZE) || ((new_size & 07777) != 0))
    return SCPE_NXM;                                    /* invalid size (prog err) */

if (!(sim_switches & SWMASK ('F'))) {                   /* force truncation? */
    for (i = new_size; i < MEMSIZE; i++) mc = mc | M[i];
    if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
        return SCPE_INCOMP;
	}

if (UNIT_CPU_FAMILY == UNIT_FAMILY_21XX) {              /* 21xx CPU? */
    cpu_set_ldr (uptr, FALSE, NULL, NULL);              /* save loader to shadow RAM */
    MEMSIZE = new_size;                                 /* set new memory size */
    fwanxm = MEMSIZE - IBL_LNT;                         /* reserve memory for loader */
    }
else                                                    /* loader unsupported */
    fwanxm = MEMSIZE = new_size;                        /* set new memory size */

for (i = fwanxm; i < old_size; i++) M[i] = 0;           /* zero non-existent memory */
return SCPE_OK;
}

/* Change CPU models.

   For convenience, MP and DMA are typically enabled if available; they may be
   disabled subsequently if desired.  Note that the 2114 supports only one DMA
   channel (channel 0).  All other models support two channels.

   Validation:
   - Sets standard equipment and convenience features.
   - Changes DMA device name to DCPC if 1000 is selected.
   - Enforces maximum memory allowed (doesn't change otherwise).
   - Disables loader on 21xx machines.
*/

t_stat cpu_set_model (UNIT *uptr, int32 new_model, char *cptr, void *desc)
{
uint32 old_family = UNIT_CPU_FAMILY;                    /* current CPU type */
uint32 new_family = new_model & UNIT_FAMILY_MASK;       /* new CPU family */
uint32 new_index  = new_model >> UNIT_V_CPU;            /* new CPU model index */
uint32 new_memsize;
t_stat result;

cpu_unit.flags = cpu_unit.flags & ~UNIT_OPTS |          /* set typical features */
                 cpu_features[new_index].typ & UNIT_OPTS;   /* mask pseudo-opts */


if (cpu_features[new_index].typ & UNIT_MP)              /* MP in typ config? */
    mp_dev.flags = mp_dev.flags & ~DEV_DIS;             /* enable it */
else
    mp_dev.flags = mp_dev.flags |  DEV_DIS;             /* disable it */

if (cpu_features[new_index].opt & UNIT_MP)              /* MP an option? */
    mp_dev.flags = mp_dev.flags |  DEV_DISABLE;         /* make it alterable */
else
    mp_dev.flags = mp_dev.flags & ~DEV_DISABLE;         /* make it unalterable */


if (cpu_features[new_index].typ & UNIT_DMA) {           /* DMA in typ config? */
    dma0_dev.flags = dma0_dev.flags & ~DEV_DIS;         /* enable DMA channel 0 */

    if (new_model == UNIT_2114)                         /* 2114 has only one channel */
        dma1_dev.flags = dma1_dev.flags |  DEV_DIS;     /* disable channel 1 */
    else                                                /* all others have two channels */
        dma1_dev.flags = dma1_dev.flags & ~DEV_DIS;     /* enable it */
    }
else {
    dma0_dev.flags = dma0_dev.flags | DEV_DIS;          /* disable channel 0 */
    dma1_dev.flags = dma1_dev.flags | DEV_DIS;          /* disable channel 1 */
    }

if (cpu_features[new_index].opt & UNIT_DMA) {           /* DMA an option? */
    dma0_dev.flags = dma0_dev.flags |  DEV_DISABLE;     /* make it alterable */

    if (new_model == UNIT_2114)                         /* 2114 has only one channel */
        dma1_dev.flags = dma1_dev.flags & ~DEV_DISABLE; /* make it unalterable */
    else                                                /* all others have two channels */
        dma1_dev.flags = dma1_dev.flags |  DEV_DISABLE; /* make it alterable */
    }
else {
    dma0_dev.flags = dma0_dev.flags & ~DEV_DISABLE;     /* make it unalterable */
    dma1_dev.flags = dma1_dev.flags & ~DEV_DISABLE;     /* make it unalterable */
    }


if ((old_family == UNIT_FAMILY_1000) &&                 /* if current family is 1000 */
    (new_family == UNIT_FAMILY_21XX)) {                 /* and new family is 21xx */
    deassign_device (&dma0_dev);                        /* delete DCPC names */
    deassign_device (&dma1_dev);
    }
else if ((old_family == UNIT_FAMILY_21XX) &&            /* if current family is 21xx */
         (new_family == UNIT_FAMILY_1000)) {            /* and new family is 1000 */
    assign_device (&dma0_dev, "DCPC0");                 /* change DMA device name */
    assign_device (&dma1_dev, "DCPC1");                 /* to DCPC for familiarity */
    }

if ((MEMSIZE == 0) ||                                   /* current mem size not set? */
    (MEMSIZE > cpu_features[new_index].maxmem))         /* current mem size too large? */
    new_memsize = cpu_features[new_index].maxmem;       /* set it to max supported */
else
    new_memsize = MEMSIZE;                              /* or leave it unchanged */

result = cpu_set_size (uptr, new_memsize, NULL, NULL);  /* set memory size */

if (result == SCPE_OK)                                  /* memory change OK? */
    if (new_family == UNIT_FAMILY_21XX)                 /* 21xx CPU? */
        fwanxm = MEMSIZE - IBL_LNT;                     /* reserve memory for loader */
    else
        fwanxm = MEMSIZE;                               /* loader reserved only for 21xx */

return result;
}

/* Display the CPU model and optional loader status.

   Loader status is displayed for 21xx models and suppressed for 1000 models.
*/

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fputs ((char *) desc, st);                              /* write model name */

if (UNIT_CPU_FAMILY == UNIT_FAMILY_21XX)                /* valid only for 21xx */
    if (fwanxm < MEMSIZE)                               /* loader area non-existent? */
        fputs (", loader disabled", st);                /* yes, so access disabled */
    else
        fputs (", loader enabled", st);                 /* no, so access enabled */
return SCPE_OK;
}

/* Set a CPU option.

   Validation:
   - Checks that the current CPU model supports the option selected.
   - If CPU is 2100, ensures that FP/FFP and IOP are mutually exclusive.
   - If CPU is 2100, ensures that FP is enabled if FFP enabled
     (FP is required for FFP installation).
*/

t_stat cpu_set_opt (UNIT *uptr, int32 option, char *cptr, void *desc)
{
uint32 model = CPU_MODEL_INDEX;                         /* current CPU model index */

if ((cpu_features[model].opt & option) == 0)            /* option supported? */
    return SCPE_NOFNC;                                  /* no */

if (UNIT_CPU_TYPE == UNIT_TYPE_2100) {
    if ((option == UNIT_FP) || (option == UNIT_FFP))    /* 2100 IOP and FP/FFP options */
        uptr->flags = uptr->flags & ~UNIT_IOP;          /* are mutually exclusive */
    else if (option == UNIT_IOP)
        uptr->flags = uptr->flags & ~(UNIT_FP | UNIT_FFP);

    if (option == UNIT_FFP)                             /* 2100 FFP option requires FP */
        uptr->flags = uptr->flags | UNIT_FP;
    }

return SCPE_OK;
}

/* Clear a CPU option.

   Validation:
   - Checks that the current CPU model supports the option selected.
   - Clears flag from unit structure (we are processing MTAB_XTD entries).
   - If CPU is 2100, ensures that FFP is disabled if FP disabled
     (FP is required for FFP installation).
*/

t_bool cpu_clr_opt (UNIT *uptr, int32 option, char *cptr, void *desc)
{
uint32 model = CPU_MODEL_INDEX;                         /* current CPU model index */

if ((cpu_features[model].opt & option) == 0)            /* option supported? */
    return SCPE_NOFNC;                                  /* no */

uptr->flags = uptr->flags & ~option;                    /* disable option */

if ((UNIT_CPU_TYPE == UNIT_TYPE_2100) &&                /* disabling 2100 FP? */
    (option == UNIT_FP))
    uptr->flags = uptr->flags & ~UNIT_FFP;              /* yes, so disable FFP too */

return SCPE_OK;
}

/* 21xx loader enable/disable function.

   The 21xx CPUs store their initial binary loaders in the last 64 words of
   available memory.  This memory is protected by a LOADER ENABLE switch on the
   front panel.  When the switch is off (disabled), main memory effectively ends
   64 locations earlier, i.e., the loader area is treated as non-existent.
   Because these are core machines, the loader is retained when system power is
   off.

   1000 CPUs do not have a protected loader feature.  Instead, loaders are
   stored in PROMs and are copied into main memory for execution by the IBL
   switch.

   Under simulation, we keep both a total configured memory size (MEMSIZE) and a
   current configured memory size (fwanxm = "first word address of non-existent
   memory).  When the two are equal, the loader is enabled.  When the current
   size is less than the total size, the loader is disabled.

   Disabling the loader copies the last 64 words to a shadow array, zeros the
   corresponding memory, and decreases the last word of addressable memory by
   64.  Enabling the loader reverses this process.

   Disabling may be done manually by user command or automatically when a halt
   instruction is executed.  Enabling occurs only by user command.  This differs
   slightly from actual machine operation, which additionally disables the
   loader when a manual halt is performed.  We do not do this to allow
   breakpoints within and single-stepping through the loaders.
*/

t_stat cpu_set_ldr (UNIT *uptr, int32 enable, char *cptr, void *desc)
{
static uint16 loader[IBL_LNT];
int32 i;
t_bool is_enabled = (fwanxm == MEMSIZE);

if ((UNIT_CPU_FAMILY != UNIT_FAMILY_21XX) ||            /* valid only for 21xx */
    (MEMSIZE == 0))                                     /* and for initialized memory */
    return SCPE_NOFNC;

if (is_enabled && (enable == 0)) {                      /* disable loader? */
    fwanxm = MEMSIZE - IBL_LNT;                         /* decrease available memory */
    for (i = 0; i < IBL_LNT; i++) {                     /* copy loader */
        loader[i] = M[fwanxm + i];                      /* from memory */
        M[fwanxm + i] = 0;                              /* and zero location */
        }
    }

else if ((!is_enabled) && (enable == 1)) {              /* enable loader? */
    for (i = 0; i < IBL_LNT; i++)                       /* copy loader */
        M[fwanxm + i] = loader[i];                      /* to memory */
    fwanxm = MEMSIZE;                                   /* increase available memory */
    }

return SCPE_OK;
}

/* IBL routine (CPU boot) */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
extern const uint16 ptr_rom[IBL_LNT], dq_rom[IBL_LNT];
extern const uint16 ms_rom[IBL_LNT], ds_rom[IBL_LNT];
int32 dev = (SR >> IBL_V_DEV) & I_DEVMASK;
int32 sel = (SR >> IBL_V_SEL) & IBL_M_SEL;

if (dev < 010) return SCPE_NOFNC;
switch (sel) {

    case 0:                                             /* PTR boot */
        ibl_copy (ptr_rom, dev);
        break;

    case 1:                                             /* DP/DQ boot */
        ibl_copy (dq_rom, dev);
        break;

    case 2:                                             /* MS boot */
        ibl_copy (ms_rom, dev);
        break;

    case 3:                                             /* DS boot */
        ibl_copy (ds_rom,dev);
        break;
        }

return SCPE_OK;
}

/* IBL boot ROM copy

   - Use memory size to set the initial PC and base of the boot area
   - Copy boot ROM to memory, updating I/O instructions
   - Place 2's complement of boot base in last location

   Notes:
   - SR settings are done by the caller
   - Boot ROM's must be assembled with a device code of 10 (10 and 11 for
     devices requiring two codes)
*/

t_stat ibl_copy (const uint16 pboot[IBL_LNT], int32 dev)
{
int32 i;
uint16 wd;

cpu_set_ldr (NULL, TRUE, NULL, NULL);                   /* enable loader (ignore errors) */

if (dev < 010) return SCPE_ARG;                         /* valid device? */
PC = ((MEMSIZE - 1) & ~IBL_MASK) & VAMASK;              /* start at mem top */
for (i = 0; i < IBL_LNT; i++) {                         /* copy bootstrap */
    wd = pboot[i];                                      /* get word */
    if (((wd & I_NMRMASK) == I_IO) &&                   /* IO instruction? */
        ((wd & I_DEVMASK) >= 010) &&                    /* dev >= 10? */
        (I_GETIOOP (wd) != ioHLT))                      /* not a HALT? */
        M[PC + i] = (wd + (dev - 010)) & DMASK;         /* change dev code */
    else M[PC + i] = wd;                                /* leave unchanged */
    }
M[PC + IBL_DPC] = (M[PC + IBL_DPC] + (dev - 010)) & DMASK;      /* patch DMA ctrl */
M[PC + IBL_END] = (~PC + 1) & DMASK;                    /* fill in start of boot */
return SCPE_OK;
}
