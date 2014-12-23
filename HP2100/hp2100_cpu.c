/* hp2100_cpu.c: HP 21xx/1000 CPU simulator

   Copyright (c) 1993-2013, Robert M. Supnik

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
                12731A memory expansion module
   MP           12581A/12892B memory protect
   DMA1,DMA2    12607B/12578A/12895A direct memory access controller
   DCPC1,DCPC2  12897B dual channel port controller

   18-Mar-13    JDB     Removed redundant extern declarations
   05-Feb-13    JDB     HLT instruction handler now relies on sim_vm_fprint_stopped
   09-May-12    JDB     Separated assignments from conditional expressions
   13-Jan-12    JDB     Minor speedup in "is_mapped"
                        Added casts to cpu_mod, dmasio, dmapio, cpu_reset, dma_reset
   07-Apr-11    JDB     Fixed I/O return status bug for DMA cycles
                        Failed I/O cycles now stop on failing instruction
   28-Mar-11    JDB     Tidied up signal handling
   29-Oct-10    JDB     Revised DMA for new multi-card paradigm
                        Consolidated DMA reset routines
                        DMA channels renamed from 0,1 to 1,2 to match documentation
   27-Oct-10    JDB     Changed I/O instructions, handlers, and DMA for revised signal model
                        Changed I/O dispatch table to use DIB pointers
   19-Oct-10    JDB     Removed DMA latency counter
   13-Oct-10    JDB     Fixed DMA requests to enable stealing every cycle
                        Fixed DMA priority for channel 1 over channel 2
                        Corrected comments for "cpu_set_idle"
   30-Sep-08    JDB     Breakpoints on interrupt trap cells now work
   05-Sep-08    JDB     VIS and IOP are now mutually exclusive on 1000-F
   11-Aug-08    JDB     Removed A/B shadow register variables
   07-Aug-08    JDB     Moved hp_setdev, hp_showdev to hp2100_sys.c
                        Moved non-existent memory checks to WritePW
   05-Aug-08    JDB     Fixed mp_dms_jmp to accept lower bound, check write protection
   30-Jul-08    JDB     Corrected DMS violation register set conditions
                        Refefined ABORT to pass address, moved def to hp2100_cpu.h
                        Combined dms and dms_io routines
   29-Jul-08    JDB     JSB to 0/1 with W5 out and fence = 0 erroneously causes MP abort
   11-Jul-08    JDB     Unified I/O slot dispatch by adding DIBs for CPU, MP, and DMA
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
                        EDT no longer passes DMA channel
   30-Apr-08    JDB     Enabled SIGNAL instructions, SIG debug flag
   28-Apr-08    JDB     Added SET CPU IDLE/NOIDLE, idle detection for DOS/RTE
   24-Apr-08    JDB     Fixed single stepping through interrupts
   20-Apr-08    JDB     Enabled EMA and VIS, added EMA, VIS, and SIGNAL debug flags
   03-Dec-07    JDB     Memory ex/dep and bkpt type default to current map mode
   26-Nov-07    JDB     Added SET CPU DEBUG and OS/VMA flags, enabled OS/VMA
   15-Nov-07    JDB     Corrected MP W5 (JSB) jumper action, SET/SHOW reversal,
                        mp_mevff clear on interrupt with I/O instruction in trap cell
   04-Nov-07    JDB     Removed DBI support from 1000-M (was temporary for RTE-6/VM)
   28-Apr-07    RMS     Removed clock initialization
   02-Mar-07    JDB     EDT passes input flag and DMA channel in dat parameter
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
   - Model 2100A Computer Installation and Maintenance Manual
        (02100-90002, Aug-1972)
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
        (92851-90001, Mar-1981)
   - HP 1000 M/E/F-Series Computers I/O Interfacing Guide
        (02109-90006, Sep-1980)
   - 12607A Direct Memory Access Operating and Service Manual
        (12607-90002, Jan-1970)
   - 12578A/12578A-01 Direct Memory Access Operating and Service Manual
        (12578-9001, Mar-1972)
   - 12892B Memory Protect Installation Manual (12892-90007, Jun-1978)


   The register state for the HP 2116 CPU is:

   AR<15:0>             A register - addressable as location 0
   BR<15:0>             B register - addressable as location 1
   PC<14:0>             P register - program counter
   SR<15:0>             S register - switch register
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

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        infinite indirection loop
        unimplemented instruction and stop_inst flag set
        unknown I/O device and stop_dev flag set
        I/O error in I/O simulator

   2. Interrupts.  I/O devices are modelled by substituting software states for
      I/O backplane signals.  Signals generated by I/O instructions and DMA
      cycles are dispatched to the target device for action.  Backplane signals
      are processed sequentially, except for the "clear flag" signal, which may
      be generated in parallel with another signal.  For example, the "STC sc,C"
      instruction generates the "set control" and the "clear flag" signals
      concurrently.

      CPU interrupt signals are modelled as three parallel arrays:

        - device request priority as bit vector dev_prl [2] [31..0]
        - device interrupt requests as bit vector dev_irq [2] [31..0]
        - device service requests as bit vector dev_srq [2] [31..0]

      Each array forms a 64-bit vector, with bits 0-31 of the first element
      corresponding to select codes 00-37 octal, and bits 0-31 of the second
      element corresponding to select codes 40-77 octal.

      The HP 2100 interrupt structure is based on the PRH, PRL, IRQ, and IAK
      signals.  PRH indicates that no higher-priority device is interrupting.
      PRL indicates to lower-priority devices that a given device is not
      interrupting.  IRQ indicates that a given device is requesting an
      interrupt.  IAK indicates that the given device's interrupt request is
      being acknowledged.

      PRH and PRL form a hardware priority chain that extends from interface to
      interface on the backplane.  We model just PRL, as PRH is calculated from
      the PRLs of higher-priority devices.

      Typical I/O devices have a flag, flag buffer, and control flip-flop.  If a
      device's flag, flag buffer, and control bits are set, and the device is
      the highest priority on the interrupt chain, it requests an interrupt by
      asserting IRQ.  When the interrupt is acknowledged with IAK, the flag
      buffer is cleared, preventing further interrupt requests from that device.
      The combination of flag and control set blocks interrupts from lower
      priority devices.

      Service requests are used to trigger the DMA service logic.  Setting the
      device flag typically also sets SRQ, although SRQ may be calculated
      independently.

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
      request will be deferred if the previous instruction was a JMP indirect,
      JSB indirect, STC, CLC, STF, CLF, or was executing from an interrupt trap
      cell.  In addition, the following instructions will cause deferral on the
      1000 series: SFS, SFC, JRS, DJP, DJS, SJP, SJS, UJP, and UJS.

      On the HP 1000, the request is always deferred until after the current
      instruction completes.  On the 21xx, the request is deferred unless the
      current instruction is an MRG instruction other than JMP or JMP,I or
      JSB,I.  Note that for the 21xx, SFS and SFC are not included in the
      deferral criteria.

   7. Terminology.  The 1000 series of computers was originally called the 21MX
      at introduction.  The 21MX (occasionally, 21MXM) corresponds to the 1000
      M-Series, and the 21MXE (occasionally, 21XE) corresponds to the 1000
      E-Series.  The model numbers were changed before the introduction of the
      1000 F-Series, although some internal HP documentation refers to a 21MXF.

      The terms MEM (Memory Expansion Module), MEU (Memory Expansion Unit), DMI
      (Dynamic Mapping Instructions), and DMS (Dynamic Mapping System) are used
      somewhat interchangeably to refer to the logical-to-physical memory
      address translation option provided on the 1000-Series.  DMS consists of
      the MEM card (12731A) and the DMI firmware (13307A).  However, MEM and MEU
      have been used interchangeably to refer to the mapping card, as have DMI
      and DMS to refer to the firmware instructions.
*/


#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"


/* Memory protect constants */

#define UNIT_V_MP_JSB   (UNIT_V_UF + 0)                 /* MP jumper W5 */
#define UNIT_V_MP_INT   (UNIT_V_UF + 1)                 /* MP jumper W6 */
#define UNIT_V_MP_SEL1  (UNIT_V_UF + 2)                 /* MP jumper W7 */
#define UNIT_MP_JSB     (1 << UNIT_V_MP_JSB)            /* 1 = W5 is out */
#define UNIT_MP_INT     (1 << UNIT_V_MP_INT)            /* 1 = W6 is out */
#define UNIT_MP_SEL1    (1 << UNIT_V_MP_SEL1)           /* 1 = W7 is out */

/* DMA channels */

typedef enum { ch1, ch2 } CHANNEL;                      /* channel number */

#define DMA_CHAN_COUNT  2                               /* number of DMA channels */

#define DMA_OE          020000000000                    /* byte packing odd/even flag */
#define DMA1_STC        0100000                         /* DMA - issue STC */
#define DMA1_PB         0040000                         /* DMA - pack bytes */
#define DMA1_CLC        0020000                         /* DMA - issue CLC */
#define DMA2_OI         0100000                         /* DMA - output/input */

typedef struct {
    FLIP_FLOP control;                                  /* control flip-flop */
    FLIP_FLOP flag;                                     /* flag flip-flop */
    FLIP_FLOP flagbuf;                                  /* flag buffer flip-flop */
    FLIP_FLOP xferen;                                   /* transfer enable flip-flop */
    FLIP_FLOP select;                                   /* register select flip-flop */

    uint32    cw1;                                      /* device select */
    uint32    cw2;                                      /* direction, address */
    uint32    cw3;                                      /* word count */
    uint32    packer;                                   /* byte-packer holding reg */
    } DMA_STATE;

#define DMA_1_REQ       (1 << ch1)                      /* channel 1 request */
#define DMA_2_REQ       (1 << ch2)                      /* channel 2 request */


/* Command line switches */

#define ALL_BKPTS       (SWMASK('E')|SWMASK('N')|SWMASK('S')|SWMASK('U'))
#define ALL_MAPMODES    (SWMASK('S')|SWMASK('U')|SWMASK('P')|SWMASK('Q'))


/* RTE base-page addresses. */

static const uint32 xeqt = 0001717;                     /* XEQT address */
static const uint32 tbg  = 0001674;                     /* TBG address */

/* DOS base-page addresses. */

static const uint32 m64  = 0000040;                     /* constant -64 address */
static const uint32 p64  = 0000067;                     /* constant +64 address */

/* CPU local data */

static uint32 jsb_plb = 2;                              /* protected lower bound for JSB */
static uint32 saved_MR = 0;                             /* between executions */
static uint32 fwanxm = 0;                               /* first word addr of nx mem */

/* CPU global data */

uint16 *M = NULL;                                       /* memory */
uint16 ABREG[2];                                        /* A/B registers */
uint32 PC = 0;                                          /* P register */
uint32 SR = 0;                                          /* S register */
uint32 MR = 0;                                          /* M register */
uint32 TR = 0;                                          /* T register */
uint32 XR = 0;                                          /* X register */
uint32 YR = 0;                                          /* Y register */
uint32 E = 0;                                           /* E register */
uint32 O = 0;                                           /* O register */
FLIP_FLOP ion = CLEAR;                                  /* interrupt enable */
t_bool ion_defer = FALSE;                               /* interrupt defer */
uint32 intaddr = 0;                                     /* interrupt addr */
uint32 stop_inst = 1;                                   /* stop on ill inst */
uint32 stop_dev = 0;                                    /* stop on ill dev */
uint16 pcq[PCQ_SIZE] = { 0 };                           /* PC queue */
uint32 pcq_p = 0;                                       /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */

uint32 dev_prl [2] = { ~(uint32) 0, ~(uint32) 0 };      /* device priority low bit vector */
uint32 dev_irq [2] = { 0, 0 };                          /* device interrupt request bit vector */
uint32 dev_srq [2] = { 0, 0 };                          /* device service request bit vector */

/* Memory protect global data */

FLIP_FLOP mp_control = CLEAR;                           /* MP control flip-flop */
FLIP_FLOP mp_flag = CLEAR;                              /* MP flag flip-flop */
FLIP_FLOP mp_flagbuf = CLEAR;                           /* MP flag buffer flip-flop */
FLIP_FLOP mp_mevff = CLEAR;                             /* memory expansion violation flip-flop */
FLIP_FLOP mp_evrff = SET;                               /* enable violation register flip-flop */

uint32 mp_fence = 0;                                    /* MP fence register  */
uint32 mp_viol = 0;                                     /* MP violation register */

uint32 iop_sp = 0;                                      /* iop stack reg */
uint32 ind_max = 16;                                    /* iadr nest limit */
uint32 err_PC = 0;                                      /* error PC */
jmp_buf save_env;                                       /* MP abort handler */

/* DMA global data */

DMA_STATE dma [DMA_CHAN_COUNT];                         /* per-channel state */

/* Dynamic mapping system global data */

uint32 dms_enb = 0;                                     /* dms enable */
uint32 dms_ump = 0;                                     /* dms user map */
uint32 dms_sr = 0;                                      /* dms status reg */
uint32 dms_vr = 0;                                      /* dms violation reg */
uint16 dms_map[MAP_NUM * MAP_LNT] = { 0 };              /* dms maps */

/* External data */

extern DIB clk_dib;                                     /* CLK DIB for idle check */
extern const BOOT_ROM ptr_rom, dq_rom, ms_rom, ds_rom;  /* boot ROMs for cpu_boot routine */

/* CPU local routines */

static t_stat Ea (uint32 IR, uint32 *addr, uint32 irq);
static uint16 ReadTAB (uint32 va);
static uint32 dms (uint32 va, uint32 map, uint32 prot);
static uint32 shift (uint32 inval, uint32 flag, uint32 oper);
static t_stat dma_cycle (CHANNEL chan, uint32 map);
static uint32 calc_dma (void);
static t_bool dev_conflict (void);
static uint32 devdisp (uint32 select_code, IOCYCLE signal_set, uint16 data);

/* CPU global routines */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);
t_stat mp_reset (DEVICE *dptr);
t_stat dma_reset (DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 new_size, char *cptr, void *desc);
t_stat cpu_set_model (UNIT *uptr, int32 new_model, char *cptr, void *desc);
t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_set_opt (UNIT *uptr, int32 option, char *cptr, void *desc);
t_stat cpu_clr_opt (UNIT *uptr, int32 option, char *cptr, void *desc);
t_stat cpu_set_ldr (UNIT *uptr, int32 enable, char *cptr, void *desc);
t_stat cpu_set_idle  (UNIT *uptr, int32 option, char *cptr, void *desc);
t_stat cpu_show_idle (FILE *st, UNIT *uptr, int32 val, void *desc);
void hp_post_cmd (t_bool from_scp);

IOHANDLER cpuio;
IOHANDLER ovflio;
IOHANDLER pwrfio;
IOHANDLER protio;
IOHANDLER dmapio;
IOHANDLER dmasio;
IOHANDLER nullio;


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
    UNIT_PFAIL | UNIT_DMA | UNIT_MP | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DS,
    1048576 },
  { UNIT_MP | UNIT_DMA | UNIT_EAU | UNIT_FP | UNIT_DMS, /* UNIT_1000_E */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP  | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DBI | UNIT_DS  | UNIT_EMA_VMA,
    1048576 },
  { UNIT_MP  | UNIT_DMA | UNIT_EAU | UNIT_FP |          /* UNIT_1000_F */
    UNIT_FFP | UNIT_DBI | UNIT_DMS,
    UNIT_PFAIL | UNIT_DMA | UNIT_MP     | UNIT_VIS |
    UNIT_IOP   | UNIT_DS  | UNIT_SIGNAL | UNIT_EMA_VMA,
    1048576 }
  };


/* Null device information block */

DIB null_dib = { &nullio, 0 };

/* CPU data structures

   cpu_dib      CPU device information block
   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
   cpu_deb      CPU debug flags
*/

DIB cpu_dib = { &cpuio, CPU };

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, 0) };

REG cpu_reg[] = {
    { ORDATA (P, PC, 15) },
    { ORDATA (A, AR, 16), REG_FIT },
    { ORDATA (B, BR, 16), REG_FIT },
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
    { BRDATA (PCQ, pcq, 8, 15, PCQ_SIZE), REG_RO+REG_CIRC },
    { ORDATA (PCQP, pcq_p, 6), REG_HRO },
    { ORDATA (JSBPLB, jsb_plb, 32), REG_HRO },
    { ORDATA (SAVEDMR, saved_MR, 32), REG_HRO },
    { ORDATA (FWANXM, fwanxm, 32), REG_HRO },
    { ORDATA (WRU, sim_int_char, 8), REG_HRO },
    { ORDATA (BRK, sim_brk_char, 8), REG_HRO },
    { ORDATA (DEL, sim_del_char, 8), REG_HRO },
    { BRDATA (PRL, dev_prl, 8, 32, 2), REG_HRO },
    { BRDATA (IRQ, dev_irq, 8, 32, 2), REG_HRO },
    { BRDATA (SRQ, dev_srq, 8, 32, 2), REG_HRO },
    { NULL }
    };

/* CPU modifier table.

   The 21MX monikers are deprecated in favor of the 1000 designations.  See the
   "HP 1000 Series Naming History" on the back inside cover of the Technical
   Reference Handbook. */

MTAB cpu_mod[] = {
    { UNIT_MODEL_MASK, UNIT_2116,   "",   "2116",   &cpu_set_model, &cpu_show_model, (void *) "2116"   },
    { UNIT_MODEL_MASK, UNIT_2115,   "",   "2115",   &cpu_set_model, &cpu_show_model, (void *) "2115"   },
    { UNIT_MODEL_MASK, UNIT_2114,   "",   "2114",   &cpu_set_model, &cpu_show_model, (void *) "2114"   },
    { UNIT_MODEL_MASK, UNIT_2100,   "",   "2100",   &cpu_set_model, &cpu_show_model, (void *) "2100"   },
    { UNIT_MODEL_MASK, UNIT_1000_E, "",   "1000-E", &cpu_set_model, &cpu_show_model, (void *) "1000-E" },
    { UNIT_MODEL_MASK, UNIT_1000_E, NULL, "21MX-E", &cpu_set_model, &cpu_show_model, (void *) "1000-E" },
    { UNIT_MODEL_MASK, UNIT_1000_M, "",   "1000-M", &cpu_set_model, &cpu_show_model, (void *) "1000-M" },
    { UNIT_MODEL_MASK, UNIT_1000_M, NULL, "21MX-M", &cpu_set_model, &cpu_show_model, (void *) "1000-M" },

#if defined (HAVE_INT64)
    { UNIT_MODEL_MASK, UNIT_1000_F, "",   "1000-F", &cpu_set_model, &cpu_show_model, (void *) "1000-F" },
#endif

    { MTAB_XTD | MTAB_VDV, 1, "IDLE", "IDLE",   &cpu_set_idle, &cpu_show_idle, NULL },
    { MTAB_XTD | MTAB_VDV, 0, NULL,   "NOIDLE", &cpu_set_idle, NULL,           NULL },

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

    { UNIT_EMA_VMA, UNIT_EMA,   "EMA",        "EMA",      &cpu_set_opt, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_EMA,    NULL, "NOEMA",    &cpu_clr_opt, NULL, NULL },

    { UNIT_EMA_VMA, UNIT_VMAOS, "VMA",        "VMA",      &cpu_set_opt, NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_VMAOS,  NULL, "NOVMA",    &cpu_clr_opt, NULL, NULL },

    { UNIT_EMA_VMA, 0,          "no EMA/VMA", NULL,       &cpu_set_opt, NULL, NULL },

#if defined (HAVE_INT64)
    { UNIT_VIS,     UNIT_VIS,   "VIS",        "VIS",      &cpu_set_opt, NULL, NULL },
    { UNIT_VIS,     0,          "no VIS",     NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_VIS,    NULL, "NOVIS",    &cpu_clr_opt, NULL, NULL },

    { UNIT_SIGNAL,  UNIT_SIGNAL,"SIGNAL",     "SIGNAL",   &cpu_set_opt, NULL, NULL },
    { UNIT_SIGNAL,  0,          "no SIGNAL",  NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_SIGNAL, NULL, "NOSIGNAL", &cpu_clr_opt, NULL, NULL },
#endif

/* Future microcode support.
    { UNIT_DS,      UNIT_DS,    "DS",         "DS",       &cpu_set_opt, NULL, NULL },
    { UNIT_DS,      0,          "no DS",      NULL,       NULL,         NULL, NULL },
    { MTAB_XTD | MTAB_VDV, UNIT_DS,     NULL, "NODS",     &cpu_clr_opt, NULL, NULL },
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

DEBTAB cpu_deb[] = {
    { "OS",    DEB_OS },
    { "OSTBG", DEB_OSTBG },
    { "VMA",   DEB_VMA },
    { "EMA",   DEB_EMA },
    { "VIS",   DEB_VIS },
    { "SIG",   DEB_SIG },
    { NULL,    0 }
    };

DEVICE cpu_dev = {
    "CPU",                                  /* device name */
    &cpu_unit,                              /* unit array */
    cpu_reg,                                /* register array */
    cpu_mod,                                /* modifier array */
    1,                                      /* number of units */
    8,                                      /* address radix */
    PA_N_SIZE,                              /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    16,                                     /* data width */
    &cpu_ex,                                /* examine routine */
    &cpu_dep,                               /* deposit routine */
    &cpu_reset,                             /* reset routine */
    &cpu_boot,                              /* boot routine */
    NULL,                                   /* attach routine */
    NULL,                                   /* detach routine */
    &cpu_dib,                               /* device information block */
    DEV_DEBUG,                              /* device flags */
    0,                                      /* debug control flags */
    cpu_deb,                                /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL };                                 /* logical device name */

/* Overflow device information block */

DIB ovfl_dib = { &ovflio, OVF };

/* Powerfail device information block */

DIB pwrf_dib = { &pwrfio, PWR };

/* Memory protect data structures

   mp_dib       MP device information block
   mp_dev       MP device descriptor
   mp_unit      MP unit descriptor
   mp_reg       MP register list
   mp_mod       MP modifiers list
*/

DIB mp_dib = { &protio, PRO };

UNIT mp_unit = { UDATA (NULL, UNIT_MP_SEL1, 0) };       /* default is JSB in, INT in, SEL1 out */

REG mp_reg[] = {
    { FLDATA (CTL, mp_control, 0) },
    { FLDATA (FLG, mp_flag, 0) },
    { FLDATA (FBF, mp_flagbuf, 0) },
    { ORDATA (FR, mp_fence, 15) },
    { ORDATA (VR, mp_viol, 16) },
    { FLDATA (EVR, mp_evrff, 0) },
    { FLDATA (MEV, mp_mevff, 0) },
    { NULL }
    };

MTAB mp_mod[] = {
    { UNIT_MP_JSB, UNIT_MP_JSB, "JSB (W5) out", "JSBOUT", NULL },
    { UNIT_MP_JSB, 0, "JSB (W5) in", "JSBIN", NULL },
    { UNIT_MP_INT, UNIT_MP_INT, "INT (W6) out", "INTOUT", NULL },
    { UNIT_MP_INT, 0, "INT (W6) in", "INTIN", NULL },
    { UNIT_MP_SEL1, UNIT_MP_SEL1, "SEL1 (W7) out", "SEL1OUT", NULL },
    { UNIT_MP_SEL1, 0, "SEL1 (W7) in", "SEL1IN", NULL },
    { 0 }
    };

DEVICE mp_dev = {
    "MP",                                   /* device name */
    &mp_unit,                               /* unit array */
    mp_reg,                                 /* register array */
    mp_mod,                                 /* modifier array */
    1,                                      /* number of units */
    8,                                      /* address radix */
    1,                                      /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    16,                                     /* data width */
    NULL,                                   /* examine routine */
    NULL,                                   /* deposit routine */
    &mp_reset,                              /* reset routine */
    NULL,                                   /* boot routine */
    NULL,                                   /* attach routine */
    NULL,                                   /* detach routine */
    &mp_dib,                                /* device information block */
    DEV_DISABLE | DEV_DIS,                  /* device flags */
    0,                                      /* debug control flags */
    NULL,                                   /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL };                                 /* logical device name */

/* DMA controller data structures

   dmax_dib     DMAx device information block
   dmax_dev     DMAx device descriptor
   dmax_reg     DMAx register list
*/

DIB dmap1_dib = { &dmapio, DMA1,   ch1 };
DIB dmas1_dib = { &dmasio, DMALT1, ch1 };

UNIT dma1_unit = { UDATA (NULL, 0, 0) };

REG dma1_reg[] = {
    { FLDATA (XFR,    dma [ch1].xferen,   0) },
    { FLDATA (CTL,    dma [ch1].control,  0) },
    { FLDATA (FLG,    dma [ch1].flag,     0) },
    { FLDATA (FBF,    dma [ch1].flagbuf,  0) },
    { FLDATA (CTL2,   dma [ch1].select,   0) },
    { ORDATA (CW1,    dma [ch1].cw1,     16) },
    { ORDATA (CW2,    dma [ch1].cw2,     16) },
    { ORDATA (CW3,    dma [ch1].cw3,     16) },
    { FLDATA (BYTE,   dma [ch1].packer,  31) },
    { ORDATA (PACKER, dma [ch1].packer,   8) },
    { NULL }
    };

DEVICE dma1_dev = {
    "DMA1",                                 /* device name */
    &dma1_unit,                             /* unit array */
    dma1_reg,                               /* register array */
    NULL,                                   /* modifier array */
    1,                                      /* number of units */
    8,                                      /* address radix */
    1,                                      /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    16,                                     /* data width */
    NULL,                                   /* examine routine */
    NULL,                                   /* deposit routine */
    &dma_reset,                             /* reset routine */
    NULL,                                   /* boot routine */
    NULL,                                   /* attach routine */
    NULL,                                   /* detach routine */
    &dmap1_dib,                             /* device information block */
    DEV_DISABLE,                            /* device flags */
    0,                                      /* debug control flags */
    NULL,                                   /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL };                                 /* logical device name */

DIB dmap2_dib = { &dmapio, DMA2,   ch2 };
DIB dmas2_dib = { &dmasio, DMALT2, ch2 };

UNIT dma2_unit = { UDATA (NULL, 0, 0) };

REG dma2_reg[] = {
    { FLDATA (XFR,    dma [ch2].xferen,   0) },
    { FLDATA (CTL,    dma [ch2].control,  0) },
    { FLDATA (FLG,    dma [ch2].flag,     0) },
    { FLDATA (FBF,    dma [ch2].flagbuf,  0) },
    { FLDATA (CTL2,   dma [ch2].select,   0) },
    { ORDATA (CW1,    dma [ch2].cw1,     16) },
    { ORDATA (CW2,    dma [ch2].cw2,     16) },
    { ORDATA (CW3,    dma [ch2].cw3,     16) },
    { FLDATA (BYTE,   dma [ch2].packer,  31) },
    { ORDATA (PACKER, dma [ch2].packer,   8) },
    { NULL }
    };

DEVICE dma2_dev = {
    "DMA2",                                 /* device name */
    &dma2_unit,                             /* unit array */
    dma2_reg,                               /* register array */
    NULL,                                   /* modifier array */
    1,                                      /* number of units */
    8,                                      /* address radix */
    1,                                      /* address width */
    1,                                      /* address increment */
    8,                                      /* data radix */
    16,                                     /* data width */
    NULL,                                   /* examine routine */
    NULL,                                   /* deposit routine */
    &dma_reset,                             /* reset routine */
    NULL,                                   /* boot routine */
    NULL,                                   /* attach routine */
    NULL,                                   /* detach routine */
    &dmap2_dib,                             /* device information block */
    DEV_DISABLE,                            /* device flags */
    0,                                      /* debug control flags */
    NULL,                                   /* debug flag name table */
    NULL,                                   /* memory size change routine */
    NULL };                                 /* logical device name */

static DEVICE *dma_dptrs [] = { &dma1_dev, &dma2_dev };


/* Interrupt deferral table (1000 version) */
/* Deferral for I/O subops:    soHLT, soFLG, soSFC, soSFS, soMIX, soLIX, soOTX, soCTL */
static t_bool defer_tab [] = { FALSE,  TRUE,  TRUE,  TRUE, FALSE, FALSE, FALSE,  TRUE };

/* Device I/O dispatch table */

DIB *dtab [64] = { &cpu_dib, &ovfl_dib };               /* init with immutable devices */



/* Execute CPU instructions.

   This routine is the instruction decode routine for the HP 2100.  It is called
   from the simulator control program to execute instructions in simulated
   memory, starting at the simulated PC.  It runs until 'reason' is set to a
   status other than SCPE_OK.
*/

t_stat sim_instr (void)
{
uint32 intrq, dmarq;                                    /* set after setjmp */
uint32 iotrap = 0;                                      /* set after setjmp */
t_stat reason = SCPE_OK;                                /* set after setjmp */
int32 i;                                                /* temp */
DEVICE *dptr;                                           /* temp */
DIB *dibptr;                                            /* temp */
int abortval;

/* Restore register state */

if (dev_conflict ())                                    /* check device assignment consistency */
    return SCPE_STOP;                                   /* conflict; stop execution */

err_PC = PC = PC & VAMASK;                              /* load local PC */

/* Restore I/O state */

dev_prl [0] = dev_prl [1] = ~(uint32) 0;                /* set all priority lows */
dev_irq [0] = dev_irq [1] = 0;                          /* clear all interrupt requests */
dev_srq [0] = dev_srq [1] = 0;                          /* clear all service requests */

for (i = OPTDEV; i <= MAXDEV; i++)                      /* default optional devices */
    dtab [i] = &null_dib;

dtab [PWR] = &pwrf_dib;                                 /* for now, powerfail is always present */

for (i = 0; sim_devices [i] != NULL; i++) {             /* loop thru dev */
    dptr = sim_devices [i];
    dibptr = (DIB *) dptr->ctxt;                        /* get DIB */

    if (dibptr && !(dptr->flags & DEV_DIS)) {           /* handler exists and device is enabled? */
        dtab [dibptr->select_code] = dibptr;            /* set DIB pointer into dispatch table */
        dibptr->io_handler (dibptr, ioSIR, 0);          /* set interrupt request state */
        }
    }

if (dtab [DMA1] != &null_dib)                           /* first DMA channel enabled? */
    dtab [DMALT1] = &dmas1_dib;                         /* set up secondary device handler */

if (dtab [DMA2] != &null_dib)                           /* second DMA channel enabled? */
    dtab [DMALT2] = &dmas2_dib;                         /* set up secondary device handler */

/* Configure interrupt deferral table */

if (UNIT_CPU_FAMILY == UNIT_FAMILY_21XX)                /* 21xx series? */
    defer_tab [soSFC] = defer_tab [soSFS] = FALSE;      /* SFC/S doesn't defer */
else                                                    /* 1000 series */
    defer_tab [soSFC] = defer_tab [soSFS] = TRUE;       /* SFC/S does defer */


/* Set MP abort handling.

   If an abort occurs in memory protection, the relocation routine executes a
   longjmp to this area OUTSIDE the main simulation loop.  Memory protection
   errors are the only sources of aborts in the HP 2100.  All referenced
   variables must be globals, and all sim_instr scoped automatics must be set
   after the setjmp.

   To initiate an MP abort, use the MP_ABORT macro and pass the violation
   address.  MP_ABORT should only be called if "mp_control" is SET, as aborts do
   not occur if MP is turned off.

   An MP interrupt (SC 05) is qualified by "ion" but not by "ion_defer".  If the
   interrupt system is off when an MP violation is detected, the violating
   instruction will be aborted, even though no interrupt occurs.  In this case,
   neither the flag nor flag buffer are set, and EVR is not cleared.

   Implementation notes:

    1. The protected lower bound address for the JSB instruction depends on the
       W5 jumper setting.  If W5 is in, then the lower bound is 2, allowing JSBs
       to the A and B registers.  If W5 is out, then the lower bound is 0, just
       as with JMP.

    2. The violation address is passed to enable the MEM violation register to
       be updated.  The "longjmp" routine will not pass a value of 0; it is
       converted internally to 1.  This is OK, because only the page number
       of the address value is used, and locations 0 and 1 are both on page 0.

    3. This routine is used both for MP and MEM violations.  The MEV flip-flop
       will be clear for the former and set for the latter.  The MEV violation
       register will be updated by "dms_upd_vr" only if the call is NOT for an
       MEM violation; if it is, then the VR has already been set and should not
       be disturbed.
*/

jsb_plb = (mp_unit.flags & UNIT_MP_JSB) ? 0 : 2;        /* set protected lower bound for JSB */

abortval = setjmp (save_env);                           /* set abort hdlr */

if (abortval) {                                         /* memory protect abort? */
    dms_upd_vr (abortval);                              /* update violation register (if not MEV) */

    if (ion)                                            /* interrupt system on? */
        protio (dtab [PRO], ioENF, 0);                  /* set flag */
    }

dmarq = calc_dma ();                                    /* initial recalc of DMA masks */
intrq = calc_int ();                                    /* initial recalc of interrupts */


/* Main instruction fetch/decode loop */

while (reason == SCPE_OK) {                             /* loop until halted */
    uint32 IR, MA, absel, v1, t, skip;

    err_PC = PC;                                        /* save PC for error recovery */

    if (sim_interval <= 0) {                            /* event timeout? */
        reason = sim_process_event ();                  /* process event service */

        if (reason != SCPE_OK)                          /* service failed? */
            break;                                      /* stop execution */

        dmarq = calc_dma ();                            /* recalc DMA reqs */
        intrq = calc_int ();                            /* recalc interrupts */
        }

/* DMA cycles are requested by an I/O card asserting its SRQ signal.  If a DMA
   channel is programmed to respond to that card's select code, a DMA cycle will
   be initiated.  A DMA cycle consists of a memory cycle and an I/O cycle.
   These cycles are synchronized with the control processor on the 21xx CPUs.
   On the 1000s, memory cycles are asynchronous, while I/O cycles are
   synchronous.  Memory cycle time is about 40% of the I/O cycle time.

   With properly designed interface cards, DMA is capable of taking consecutive
   I/O cycles.  On all machines except the 1000 M-Series, a DMA cycle freezes
   the CPU for the duration of the cycle.  On the M-Series, a DMA cycle freezes
   the CPU if it attempts an I/O cycle (including IAK) or a directly-interfering
   memory cycle.  An interleaved memory cycle is allowed.  Otherwise, the
   control processor is allowed to run.  Therefore, during consecutive DMA
   cycles, the M-Series CPU will run until an IOG instruction is attempted,
   whereas the other CPUs will freeze completely.

   All DMA cards except the 12607B provide two independent channels.  If both
   channels are active simultaneously, channel 1 has priority for I/O cycles
   over channel 2.

   Most I/O cards assert SRQ no more than 50% of the time.  A few buffered
   cards, such as the 12821A and 13175A Disc Interfaces, are capable of
   asserting SRQ continuously while filling or emptying the buffer.  If SRQ for
   channel 1 is asserted continuously when both channels are active, then no
   channel 2 cycles will occur until channel 1 completes.

   Implementation notes:

    1. CPU freeze is simulated by skipping instruction execution during the
       current loop cycle.

    2. If both channels have SRQ asserted, DMA priority is simulated by skipping
       the channel 2 cycle if channel 1's SRQ is still asserted at the end of
       its cycle.  If it is not, then channel 2 steals the next cycle from the
       CPU.

    3. The 1000 M-Series allows some CPU processing concurrently with
       continuous DMA cycles, whereas all other CPUs freeze.  The processor
       freezes if an I/O cycle is attempted, including an interrupt
       acknowledgement.  Because some microcode extensions (e.g., Access IOP,
       RTE-6/VM OS) perform I/O cycles, advance detection of I/O cycles is
       difficult.  Therefore, we freeze all processing for the M-Series as well.
*/

    if (dmarq) {
        if (dmarq & DMA_1_REQ) {                                /* DMA channel 1 request? */
            reason = dma_cycle (ch1, PAMAP);                    /* do one DMA cycle using port A map */

            if (reason == SCPE_OK)                              /* cycle OK? */
                dmarq = calc_dma ();                            /* recalc DMA requests */
            else
                break;                                          /* cycle failed, so stop */
            }

        if ((dmarq & (DMA_1_REQ | DMA_2_REQ)) == DMA_2_REQ) {   /* DMA channel 1 idle and channel 2 request? */
            reason = dma_cycle (ch2, PBMAP);                    /* do one DMA cycle using port B map */

            if (reason == SCPE_OK)                              /* cycle OK? */
                dmarq = calc_dma ();                            /* recalc DMA requests */
            else
                break;                                          /* cycle failed, so stop */
            }

        if (dmarq)                                              /* DMA request still pending? */
            continue;                                           /* service it before instruction execution */

        intrq = calc_int ();                                    /* recalc interrupts */
        }

    if (intrq && ion_defer)                                     /* interrupt pending but deferred? */
        ion_defer = calc_defer ();                              /* confirm deferral */

/* Check for pending interrupt request.

   Interrupt recognition is controlled by three state variables: "ion",
   "ion_defer", and "intrq".  "ion" corresponds to the INTSYS flip-flop in the
   1000 CPU, "ion_defer" corresponds to the INTEN flip-flop, and "intrq"
   corresponds to the NRMINT flip-flop.  STF 00 and CLF 00 set and clear INTSYS,
   turning the interrupt system on and off.  Micro-orders ION and IOFF set and
   clear INTEN, deferring or allowing certain interrupts.  An IRQ signal from a
   device, qualified by the corresponding PRL signal, will set NRMINT to request
   a normal interrupt; an IOFF or IAK will clear it.

   Under simulation, "ion" is controlled by STF/CLF 00.  "ion_defer" is set or
   cleared as appropriate by the individual instruction simulators.  "intrq" is
   set to the successfully interrupting device's select code, or to zero if
   there is no qualifying interrupt request.

   Presuming PRL is set to allow priority to an interrupting device:

    1. Power fail (SC 04) may interrupt if "ion_defer" is clear; this is not
       conditional on "ion" being set.

    2. Memory protect (SC 05) may interrupt if "ion" is set; this is not
       conditional on "ion_defer" being clear.

    3. Parity error (SC 05) may interrupt always; this is not conditional on
       "ion" being set or "ion_defer" being clear.

    4. All other devices (SC 06 and up) may interrupt if "ion" is set and
       "ion_defer" is clear.

   Qualification with "ion" is performed by "calc_int", except for case 2, which
   is qualified by the MP abort handler above (because qualification occurs on
   the MP card, rather than in the CPU).  Therefore, we need only qualify by
   "ion_defer" here.
*/

    if (intrq && ((intrq == PRO) || !ion_defer)) {      /* interrupt request? */
        if (sim_brk_summ &&                             /* any breakpoints? */
            sim_brk_test (intrq, SWMASK ('E') |         /* unconditional or right type for DMS? */
              (dms_enb ? SWMASK ('S') : SWMASK ('N')))) {
            reason = STOP_IBKPT;                        /* stop simulation */
            break;
            }

        intaddr = intrq;                                /* save int addr in CIR */
        intrq = 0;                                      /* clear request */
        ion_defer = TRUE;                               /* defer interrupts */
        iotrap = 1;                                     /* mark as I/O trap cell instr */

        if (dms_enb)                                    /* dms enabled? */
            dms_sr = dms_sr | MST_ENBI;                 /* set in status */
        else                                            /* not enabled */
            dms_sr = dms_sr & ~MST_ENBI;                /* clear in status */

        if (dms_ump) {                                  /* user map enabled at interrupt? */
            dms_sr = dms_sr | MST_UMPI;                 /* set in status */
            dms_ump = SMAP;                             /* switch to system map */
            }
        else                                            /* system map enabled at interrupt */
            dms_sr = dms_sr & ~MST_UMPI;                /* clear in status */

        IR = ReadW (intaddr);                           /* get trap cell instruction */

        devdisp (intaddr, ioIAK, (uint16) IR);          /* acknowledge interrupt */

        if (intaddr != PRO)                             /* not MP interrupt? */
            protio (dtab [intaddr], ioIAK, IR);         /* send IAK for device to MP too */
        }

    else {                                              /* normal instruction */
        iotrap = 0;                                     /* not a trap cell instruction */

        if (sim_brk_summ &&                             /* any breakpoints? */
            sim_brk_test (PC, SWMASK ('E') |            /* unconditional or */
                              (dms_enb ?                /*   correct type for DMS state? */
                                (dms_ump ?
                                  SWMASK ('U') : SWMASK ('S')) :
                                SWMASK ('N')))) {
            reason = STOP_IBKPT;                        /* stop simulation */
            break;
            }

        if (mp_evrff)                                   /* violation register enabled */
            mp_viol = PC;                               /* update with current PC */

        IR = ReadW (PC);                                /* fetch instr */
        PC = (PC + 1) & VAMASK;
        ion_defer = FALSE;
        }

    sim_interval = sim_interval - 1;                    /* count instruction */

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

    absel = (IR & I_AB) ? 1 : 0;                        /* get A/B select */

    switch ((IR >> 8) & 0377) {                         /* decode IR<15:8> */

/* Memory reference instructions */

    case 0020:case 0021:case 0022:case 0023:
    case 0024:case 0025:case 0026:case 0027:
    case 0220:case 0221:case 0222:case 0223:
    case 0224:case 0225:case 0226:case 0227:
        reason = Ea (IR, &MA, intrq);                   /* AND */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        AR = AR & ReadW (MA);
        break;

/* JSB is a little tricky.  It is possible to generate both an MP and a DM
   violation simultaneously, as the MP and MEM cards validate in parallel.
   Consider a JSB to a location under the MP fence and on a write-protected
   page.  This situation must be reported as a DM violation, because it has
   priority (SFS 5 and SFC 5 check only the MEVFF, which sets independently of
   the MP fence violation).  Under simulation, this means that DM violations
   must be checked, and the MEVFF must be set, before an MP abort is taken.
   This is done by the "mp_dms_jmp" routine.
*/

    case 0230:case 0231:case 0232:case 0233:
    case 0234:case 0235:case 0236:case 0237:
        ion_defer = TRUE;                               /* defer if JSB,I */

    case 0030:case 0031:case 0032:case 0033:
    case 0034:case 0035:case 0036:case 0037:
        reason = Ea (IR, &MA, intrq);                   /* JSB */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        mp_dms_jmp (MA, jsb_plb);                       /* validate jump address */

        WriteW (MA, PC);                                /* store PC */
        PCQ_ENTRY;
        PC = (MA + 1) & VAMASK;                         /* jump */
        break;

    case 0040:case 0041:case 0042:case 0043:
    case 0044:case 0045:case 0046:case 0047:
    case 0240:case 0241:case 0242:case 0243:
    case 0244:case 0245:case 0246:case 0247:
        reason = Ea (IR, &MA, intrq);                   /* XOR */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        AR = AR ^ ReadW (MA);
        break;

/* CPU idle processing.

   The 21xx/1000 CPUs have no "wait for interrupt" instruction.  Idling in HP
   operating systems consists of sitting in "idle loops" that end with JMP
   instructions.  We test for certain known patterns when a JMP instruction is
   executed to decide if the simulator should idle.

   Idling must not occur if an interrupt is pending.  As mentioned in the
   "General Notes" above, HP CPUs will defer interrupts if certain instructions
   are executed.  OS interrupt handlers exit via such deferring instructions.
   If there is a pending interrupt when the OS is otherwise idle, the idle loop
   will execute one instruction before reentering the interrupt handler.  If we
   call sim_idle() in this case, we will lose interrupts.

   Consider the situation in RTE.  Under simulation, the TTY and CLK events are
   co-scheduled, with the CLK expiring one instruction after the TTY.  When the
   TTY interrupts, $CIC in RTE is entered.  One instruction later, the CLK
   expires and posts its interrupt, but it is not immediately handled, because
   the JSB $CIC,I / JMP $CIC0,I / SFS 0,C instruction entry sequence continually
   defers interrupts until the interrupt system is turned off.  When $CIC
   returns via $IRT, one instruction of the idle loop is executed, even though
   the CLK interrupt is still pending, because the UJP instruction used to
   return also defers interrupts.

   If sim_idle() is called at this point, the simulator will sleep when it
   should be handling the pending CLK interrupt.  When it awakes, TTY expiration
   will be moved forward to the next instruction.  The still-pending CLK
   interrupt will then be recognized, and $CIC will be entered.  But the TTY and
   then the CLK will then expire and attempt to interrupt again, although they
   are deferred by the $CIC entry sequence.  This causes the second CLK
   interrupt to be missed, as processing of the first one is just now being
   started.

   Similarly, at the end of the CLK handling, the TTY interrupt is still
   pending.  When $IRT returns to the idle loop, sim_idle() would be called
   again, so the TTY and then CLK interrupt a third time.  Because the second
   TTY interrupt is still pending, $CIC is entered, but the third TTY interrupt
   is lost.

   We solve this problem by testing for a pending interrupt before calling
   sim_idle().  The system isn't really quiescent if it is just about to handle
   an interrupt.
*/

    case 0250:case 0251:case 0252:case 0253:
    case 0254:case 0255:case 0256:case 0257:
        ion_defer = TRUE;                               /* defer if JMP,I */

    case 0050:case 0051:case 0052:case 0053:
    case 0054:case 0055:case 0056:case 0057:
        reason = Ea (IR, &MA, intrq);                   /* JMP */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        mp_dms_jmp (MA, 0);                             /* validate jump addr */
        PCQ_ENTRY;
        PC = MA;                                        /* jump */

/* Idle conditions by operating system:

   RTE-6/VM:
    - ISZ <n> / JMP *-1
    - mp_fence = 0
    - XEQT (address 1717B) = 0
    - DMS on with system map enabled
    - RTE verification: TBG (address 1674B) = CLK select code

   RTE though RTE-IVB:
    - JMP *
    - mp_fence = 0
    - XEQT (address 1717B) = 0
    - DMS on with user map enabled (RTE-III through RTE-IVB only)
    - RTE verification: TBG (address 1674B) = CLK select code

   DOS through DOS-III:
    - STF 0 / CCA / CCB / JMP *-3
    - DOS verification: A = B = -1, address 40B = -64, address 67B = +64
    - Note that in DOS, the TBG is set to 100 milliseconds
*/

        if ((sim_idle_enab) && (intrq == 0))                /* idle enabled w/o pending irq? */
            if (((PC == err_PC) ||                          /* RTE through RTE-IVB */
                 ((PC == (err_PC - 1)) &&                   /* RTE-6/VM */
                  ((ReadW (PC) & I_MRG) == I_ISZ))) &&      /* RTE jump target */
                (mp_fence == CLEAR) && (M [xeqt] == 0) &&   /* RTE idle indications */
                (M [tbg] == clk_dib.select_code) ||         /* RTE verification */

                (PC == (err_PC - 3)) &&                     /* DOS through DOS-III */
                (ReadW (PC) == I_STF) &&                    /* DOS jump target */
                (AR == 0177777) && (BR == 0177777) &&       /* DOS idle indication */
                (M [m64] == 0177700) &&                     /* DOS verification */
                (M [p64] == 0000100))                       /* DOS verification */

                sim_idle (TMR_POLL, FALSE);                 /* idle the simulator */
        break;

    case 0060:case 0061:case 0062:case 0063:
    case 0064:case 0065:case 0066:case 0067:
    case 0260:case 0261:case 0262:case 0263:
    case 0264:case 0265:case 0266:case 0267:
        reason = Ea (IR, &MA, intrq);                   /* IOR */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        AR = AR | ReadW (MA);
        break;

    case 0070:case 0071:case 0072:case 0073:
    case 0074:case 0075:case 0076:case 0077:
    case 0270:case 0271:case 0272:case 0273:
    case 0274:case 0275:case 0276:case 0277:
        reason = Ea (IR, &MA, intrq);                   /* ISZ */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        t = (ReadW (MA) + 1) & DMASK;
        WriteW (MA, t);

        if (t == 0)
            PC = (PC + 1) & VAMASK;
        break;

    case 0100:case 0101:case 0102:case 0103:
    case 0104:case 0105:case 0106:case 0107:
    case 0300:case 0301:case 0302:case 0303:
    case 0304:case 0305:case 0306:case 0307:
        reason = Ea (IR, &MA, intrq);                   /* ADA */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        v1 = ReadW (MA);
        t = AR + v1;

        if (t > DMASK)
            E = 1;

        if (((~AR ^ v1) & (AR ^ t)) & SIGN)
            O = 1;

        AR = t & DMASK;
        break;

    case 0110:case 0111:case 0112:case 0113:
    case 0114:case 0115:case 0116:case 0117:
    case 0310:case 0311:case 0312:case 0313:
    case 0314:case 0315:case 0316:case 0317:
        reason = Ea (IR, &MA, intrq);                   /* ADB */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        v1 = ReadW (MA);
        t = BR + v1;

        if (t > DMASK)
            E = 1;

        if (((~BR ^ v1) & (BR ^ t)) & SIGN)
            O = 1;

        BR = t & DMASK;
        break;

    case 0120:case 0121:case 0122:case 0123:
    case 0124:case 0125:case 0126:case 0127:
    case 0320:case 0321:case 0322:case 0323:
    case 0324:case 0325:case 0326:case 0327:
        reason = Ea (IR, &MA, intrq);                   /* CPA */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        if (AR != ReadW (MA))
            PC = (PC + 1) & VAMASK;
        break;

    case 0130:case 0131:case 0132:case 0133:
    case 0134:case 0135:case 0136:case 0137:
    case 0330:case 0331:case 0332:case 0333:
    case 0334:case 0335:case 0336:case 0337:
        reason = Ea (IR, &MA, intrq);                   /* CPB */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        if (BR != ReadW (MA))
            PC = (PC + 1) & VAMASK;
        break;

    case 0140:case 0141:case 0142:case 0143:
    case 0144:case 0145:case 0146:case 0147:
    case 0340:case 0341:case 0342:case 0343:
    case 0344:case 0345:case 0346:case 0347:
        reason = Ea (IR, &MA, intrq);                   /* LDA */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        AR = ReadW (MA);
        break;

    case 0150:case 0151:case 0152:case 0153:
    case 0154:case 0155:case 0156:case 0157:
    case 0350:case 0351:case 0352:case 0353:
    case 0354:case 0355:case 0356:case 0357:
        reason = Ea (IR, &MA, intrq);                   /* LDB */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        BR = ReadW (MA);
        break;

    case 0160:case 0161:case 0162:case 0163:
    case 0164:case 0165:case 0166:case 0167:
    case 0360:case 0361:case 0362:case 0363:
    case 0364:case 0365:case 0366:case 0367:
        reason = Ea (IR, &MA, intrq);                   /* STA */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        WriteW (MA, AR);
        break;

    case 0170:case 0171:case 0172:case 0173:
    case 0174:case 0175:case 0176:case 0177:
    case 0370:case 0371:case 0372:case 0373:
    case 0374:case 0375:case 0376:case 0377:
        reason = Ea (IR, &MA, intrq);                   /* STB */

        if (reason != SCPE_OK)                          /* address failed to resolve? */
            break;                                      /* stop execution */

        WriteW (MA, BR);
        break;

/* Alter/skip instructions */

    case 0004:case 0005:case 0006:case 0007:
    case 0014:case 0015:case 0016:case 0017:
        skip = 0;                                       /* no skip */

        if (IR & 000400)                                /* CLx */
            t = 0;
        else
            t = ABREG[absel];

        if (IR & 001000)                                /* CMx */
            t = t ^ DMASK;

        if (IR & 000001) {                              /* RSS? */
            if ((IR & 000040) && (E != 0))              /* SEZ,RSS */
                skip = 1;

            if (IR & 000100)                            /* CLE */
                E = 0;

            if (IR & 000200)                            /* CME */
                E = E ^ 1;

            if (((IR & 000030) == 000030) &&            /* SSx,SLx,RSS */
                ((t & 0100001) == 0100001))
                skip = 1;

            if (((IR & 000030) == 000020) &&            /* SSx,RSS */
                ((t & SIGN) != 0))
                skip = 1;

            if (((IR & 000030) == 000010) &&            /* SLx,RSS */
                ((t & 1) != 0))
                skip = 1;

            if (IR & 000004) {                          /* INx */
                t = (t + 1) & DMASK;

                if (t == 0)
                    E = 1;

                if (t == SIGN)
                    O = 1;
                }

            if ((IR & 000002) && (t != 0))              /* SZx,RSS */
                skip = 1;

            if ((IR & 000072) == 0)                     /* RSS */
                skip = 1;
            }                                           /* end if RSS */

        else {
            if ((IR & 000040) && (E == 0))              /* SEZ */
                skip = 1;

            if (IR & 000100)                            /* CLE */
                E = 0;

            if (IR & 000200)                            /* CME */
                E = E ^ 1;

            if ((IR & 000020) &&                        /* SSx */
                ((t & SIGN) == 0))
                skip = 1;

            if ((IR & 000010) &&                        /* SLx */
                 ((t & 1) == 0))
                 skip = 1;

            if (IR & 000004) {                          /* INx */
                t = (t + 1) & DMASK;

                if (t == 0)
                    E = 1;

                if (t == SIGN)
                    O = 1;
                }
            if ((IR & 000002) && (t == 0))              /* SZx */
                skip = 1;
            }                                           /* end if ~RSS */

        ABREG[absel] = t;                               /* store result */
        PC = (PC + skip) & VAMASK;                      /* add in skip */
        break;                                          /* end if alter/skip */

/* Shift instructions */

    case 0000:case 0001:case 0002:case 0003:
    case 0010:case 0011:case 0012:case 0013:
        t = shift (ABREG[absel], IR & 01000, IR >> 6);  /* do first shift */

        if (IR & 000040)                                /* CLE */
            E = 0;

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
        reason = SCPE_OK;                               /* continue */
        }

    else if (reason == NOTE_INDINT) {                   /* intr pend during indir? */
        PC = err_PC;                                    /* back out of inst */
        reason = SCPE_OK;                               /* continue */
        }
    }                                                   /* end while */

/* Simulation halted */

if (iotrap && (reason == STOP_HALT))                    /* HLT in trap cell? */
    MR = intaddr;                                       /* M = interrupt address */
else                                                    /* normal HLT */
    MR = (PC - 1) & VAMASK;                             /* M = P - 1 */

TR = ReadTAB (MR);                                      /* T = last word fetched */
saved_MR = MR;                                          /* save for T cmd update */

if (reason == STOP_HALT)                                /* programmed halt? */
    cpu_set_ldr (NULL, FALSE, NULL, NULL);              /* disable loader (after T is read) */
else                                                    /* simulation stop */
    PC = err_PC;                                        /* back out instruction */

dms_upd_sr ();                                          /* update dms_sr */
dms_upd_vr (MR);                                        /* update dms_vr */
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */

if (dms_enb)                                            /* DMS enabled? */
    if (dms_ump)                                        /* set default */
        sim_brk_dflt = SWMASK ('U');                    /*   breakpoint type */
    else                                                /*     to current */
        sim_brk_dflt = SWMASK ('S');                    /*       map mode */

else                                                    /* DMS disabled */
    sim_brk_dflt = SWMASK ('N');                        /* set breakpoint type to non-DMS */

return reason;                                          /* return status code */
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
t_bool int_enable = ((mp_unit.flags & UNIT_MP_INT) && mp_control);

for (i = 0; (i < ind_max) && (MA & I_IA); i++) {        /* resolve multilevel */
    if (pending) {                                      /* interrupt pending and MP enabled? */
        if ((i == 2) || int_enable)                     /* 3rd level indirect or INT out? */
            ion_defer = FALSE;                          /* reenable interrrupts */
        if ((i > 2) || int_enable)                      /* 4th or higher or INT out? */
            return NOTE_INDINT;                         /* break out now */
        }

    MA = ReadW (MA & VAMASK);                           /* follow address chain */
    }

if (MA & I_IA)                                          /* indirect loop? */
    return STOP_IND;                                    /* stop simulation */

*addr = MA;
return SCPE_OK;
}


/* Get effective address from IR */

static t_stat Ea (uint32 IR, uint32 *addr, uint32 irq)
{
uint32 MA;

MA = IR & (I_IA | I_DISP);                              /* ind + disp */

if (IR & I_CP)                                          /* current page? */
    MA = ((PC - 1) & I_PAGENO) | MA;                    /* merge in page from PC */

return resolve (MA, addr, irq);                         /* resolve indirects */
}


/* Shift micro operation */

static uint32 shift (uint32 t, uint32 flag, uint32 op)
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

if (op == 05)                                           /* disabled ext rgt rot */
    E = t & 1;

if (op == 06)                                           /* disabled ext lft rot */
    E = (t >> 15) & 1;

return t;                                               /* input unchanged */
}


/* I/O instruction decode.

   If memory protect is enabled, and the instruction is not in a trap cell, then
   HLT instructions are illegal and will cause a memory protect violation.  If
   jumper W7 (SEL1) is in, then all other I/O instructions are legal; if W7 is
   out, then only I/O instructions to select code 1 are legal, and I/O to other
   select codes will cause a violation.

   If the instruction is allowed, then the I/O signal corresponding to the
   instruction is determined, the state of the interrupt deferral flag is set.
   Then the signal is dispatched to the device simulator indicated by the target
   select code.  The return value is split into status and data values, with the
   latter containing the SKF signal state or data to be returned in the A or B
   registers.

   Implementation notes:

    1. If the H/C (hold/clear flag) bit is set, then the ioCLF signal is added
       (not ORed) to the base signal derived from the I/O instruction.

    2. ioNONE is dispatched for HLT instructions because although HLT does not
       assert any backplane signals, the H/C bit may be set.  If it is, then the
       result will be to dispatch ioCLF.

    3. Device simulators return either ioSKF or ioNONE in response to an SFC or
       SFS signal.  ioSKF means that the instruction should skip.  Because
       device simulators return the "data" parameter value by default, we
       initialize that parameter to ioNONE to ensure that a simulator that does
       not implement SFC or SFS does not skip, which is the correct action for
       an interface that does not drive the SKF signal.

    4. STF/CLF and STC/CLC share sub-opcode values and must be further decoded
       by the state of instruction register bits 9 and 11, respectively.

    5. We return NOTE_IOG for normal status instead of SCPE_OK to request that
       interrupts be recalculated at the end of the instruction (execution of
       the I/O group instructions can change the interrupt priority chain).
*/

t_stat iogrp (uint32 ir, uint32 iotrap)
{

/* Translation for I/O subopcodes:            soHLT, soFLG, soSFC, soSFS, soMIX, soLIX, soOTX, soCTL */
static const IOSIGNAL generate_signal [] = { ioNONE, ioSTF, ioSFC, ioSFS, ioIOI, ioIOI, ioIOO, ioSTC };

const uint32 dev = ir & I_DEVMASK;                      /* device select code */
const uint32 sop = I_GETIOOP (ir);                      /* I/O subopcode */
const uint32 ab  = (ir & I_AB) != 0;                    /* A/B register select */
const t_bool clf = (ir & I_HC) != 0;                    /* H/C flag select */
uint16 iodata = (uint16) ioNONE;                        /* initialize for SKF test */
uint32 ioreturn;
t_stat iostat;
IOCYCLE signal_set;

if (!iotrap && mp_control &&                                /* instr not in trap cell and MP on? */
    ((sop == soHLT) ||                                      /*   and is HLT? */
    ((dev != OVF) && (mp_unit.flags & UNIT_MP_SEL1)))) {    /*   or is not SC 01 and SEL1 out? */
        if (sop == soLIX)                                   /* MP violation; is LIA/B instruction? */
            ABREG [ab] = 0;                                 /* A/B writes anyway */

        MP_ABORT (err_PC);                                  /* MP abort */
        }

signal_set = generate_signal [sop];                     /* generate I/O signal from instruction */
ion_defer = defer_tab [sop];                            /* defer depending on instruction */

if (sop == soOTX)                                       /* OTA/B instruction? */
    iodata = ABREG [ab];                                /* pass A/B register value */

else if ((sop == soCTL) && (ir & I_CTL))                /* CLC instruction? */
    signal_set = ioCLC;                                 /* change STC to CLC signal */

if ((sop == soFLG) && clf)                              /* CLF instruction? */
    signal_set = ioCLF;                                 /* change STF to CLF signal */

else if (clf)                                           /* CLF with another instruction? */
    signal_set = signal_set | ioCLF;                    /* add CLF signal */

ioreturn = devdisp (dev, signal_set, IORETURN (SCPE_OK, iodata));   /* dispatch I/O signal */

iostat = IOSTATUS (ioreturn);                           /* extract status */
iodata = IODATA (ioreturn);                             /* extract return data value */

if (((sop == soSFC) || (sop == soSFS)) &&               /* testing flag state? */
    ((IOSIGNAL) iodata == ioSKF))                       /*   and SKF asserted? */
    PC = (PC + 1) & VAMASK;                             /* bump P to skip next instruction */

else if (sop == soLIX)                                  /* LIA/B instruction? */
    ABREG [ab] = iodata;                                /* load returned data */

else if (sop == soMIX)                                  /* MIA/B instruction? */
    ABREG [ab] = ABREG [ab] | iodata;                   /* merge returned data */

else if (sop == soHLT) {                                /* HLT instruction? */
    return STOP_HALT;                                   /* return halt status */
    }

if (iostat == SCPE_OK)                                  /* normal status? */
    return NOTE_IOG;                                    /* request interrupt recalc */
else                                                    /* abnormal status */
    return iostat;                                      /* return it */
}


/* Device I/O signal dispatcher */

static uint32 devdisp (uint32 select_code, IOCYCLE signal_set, uint16 data)
{
return dtab [select_code]->io_handler (dtab [select_code],
                                       signal_set,
                                       IORETURN (SCPE_OK, data));
}


/* Calculate DMA requests */

static uint32 calc_dma (void)
{
uint32 r = 0;

if (dma [ch1].xferen && SRQ (dma [ch1].cw1 & I_DEVMASK))    /* check DMA1 cycle */
    r = r | DMA_1_REQ;
if (dma [ch2].xferen && SRQ (dma [ch2].cw1 & I_DEVMASK))    /* check DMA2 cycle */
    r = r | DMA_2_REQ;
return r;
}


/* Determine whether a pending interrupt deferral should be inhibited.

   Execution of certain instructions generally cause a pending interrupt to be
   deferred until the succeeding instruction completes.  However, the interrupt
   deferral rules differ on the 21xx vs. the 1000.

   The 1000 always defers until the completion of the instruction following a
   deferring instruction.  The 21xx defers unless the following instruction is
   an MRG instruction other than JMP or JMP,I or JSB,I.  If it is, then the
   deferral is inhibited, i.e., the pending interrupt will be serviced.

   See the "Set Phase Logic Flowchart," transition from phase 1A to phase 1B,
   and the "Theory of Operation," "Control Section Detailed Theory," "Phase
   Control Logic," "Phase 1B" paragraph in the Model 2100A Computer Installation
   and Maintenance Manual for details.
*/

t_bool calc_defer (void)
{
uint16 IR;

if (UNIT_CPU_FAMILY == UNIT_FAMILY_21XX) {              /* 21xx series? */
    IR = ReadW (PC);                                    /* prefetch next instr */

    if (((IR & I_MRG & ~I_AB) != 0000000) &&            /* is MRG instruction? */
        ((IR & I_MRG_I)       != I_JSB_I) &&            /*   but not JSB,I? */
        ((IR & I_MRG)         != I_JMP))                /*   and not JMP or JMP,I? */
        return FALSE;                                   /* yes, so inhibit deferral */
    else
        return TRUE;                                    /* no, so allow deferral */
    }
else
    return TRUE;                                        /* 1000 always allows deferral */
}


/* Calculate interrupt requests.

   The interrupt request (IRQ) of the highest-priority device for which all
   higher-priority PRL bits are set is granted.  That is, there must be an
   unbroken chain of priority to a device requesting an interrupt for that
   request to be granted.

   A device sets its IRQ bit to request an interrupt, and it clears its PRL bit
   to prevent lower-priority devices from interrupting.  IRQ is cleared by an
   interrupt acknowledge (IAK) signal.  PRL generally remains low while a
   device's interrupt service routine is executing to prevent preemption.

   IRQ and PRL indicate one of four possible states for a device:

     IRQ  PRL  Device state
     ---  ---  ----------------------
      0    1   Not interrupting
      1    0   Interrupt requested
      0    0   Interrupt acknowledged
      1    1   (not allowed)

   Note that PRL must be dropped when requesting an interrupt (IRQ set).  This
   is a hardware requirement of the 1000 series.  The IRQ lines from the
   backplane are not priority encoded.  Instead, the PRL chain expresses the
   priority by allowing only one IRQ line to be active at a time.  This allows a
   simple pull-down encoding of the CIR inputs.

   The end of priority chain is marked by the highest-priority (lowest-order)
   bit that is clear.  The device corresponding to that bit is the only device
   that may interrupt (a higher priority device that had IRQ set would also have
   had PRL set, which is a state violation).  We calculate a priority mask by
   ANDing the complement of the PRL bits with an increment of the PRL bits.
   Only the lowest-order bit will differ.  For example:

     dev_prl     :  ...1 1 0 1 1 0 1 1 1 1 1 1   (PRL denied for SC 06 and 11)

     dev_prl + 1 :  ...1 1 0 1 1 1 0 0 0 0 0 0
    ~dev_prl     :  ...0 0 1 0 0 1 0 0 0 0 0 0
     ANDed value :  ...0 0 0 0 0 1 0 0 0 0 0 0   (break is at SC 06)

   The interrupt requests are then ANDed with the priority mask to determine if
   a request is pending:

     pri mask    :  ...0 0 0 0 0 1 0 0 0 0 0 0   (allowed interrupt source)
     dev_irq     :  ...0 0 1 0 0 1 0 0 0 0 0 0   (devices requesting interrupts)
     ANDed value :  ...0 0 0 0 0 1 0 0 0 0 0 0   (request to grant)

   The select code corresponding to the granted request is then returned to the
   caller.

   If ION is clear, only power fail (SC 04) and parity error (SC 05) are
   eligible to interrupt (memory protect shares SC 05, but qualification occurs
   in the MP abort handler, so if SC 05 is interrupting when ION is clear, it
   must be a parity error interrupt).
*/

uint32 calc_int (void)
{
uint32 sc, pri_mask [2], req_grant [2];

pri_mask  [0] = ~dev_prl [0] & (dev_prl [0] + 1);       /* calculate lower priority mask */
req_grant [0] = pri_mask [0] & dev_irq [0];             /* calculate lower request to grant */

if (ion)                                                    /* interrupt system on? */
    if ((req_grant [0] == 0) && (pri_mask [0] == 0)) {      /* no requests in lower set and PRL unbroken? */
        pri_mask  [1] = ~dev_prl [1] & (dev_prl [1] + 1);   /* calculate upper priority mask */
        req_grant [1] = pri_mask [1] & dev_irq [1];         /* calculate upper request to grant */
        }
    else                                                /* lower set has request */
        req_grant [1] = 0;                              /* no grants to upper set */

else {                                                  /* interrupt system off */
    req_grant [0] = req_grant [0] &                     /* only PF and PE can interrupt */
                    (BIT_M (PWR) | BIT_M (PRO));
    req_grant [1] = 0;
    }

if (req_grant [0])                                      /* device in lower half? */
    for (sc = 0; sc <= 31; sc++)                        /* determine interrupting select code */
        if (req_grant [0] & 1)                          /* grant this request? */
            return sc;                                  /* return this select code */
        else                                            /* not this one */
            req_grant [0] = req_grant [0] >> 1;         /* position next request */

else if (req_grant [1])                                 /* device in upper half */
    for (sc = 32; sc <= 63; sc++)                       /* determine interrupting select code */
        if (req_grant [1] & 1)                          /* grant this request? */
            return sc;                                  /* return this select code */
        else                                            /* not this one */
            req_grant [1] = req_grant [1] >> 1;         /* position next request */

return 0;                                               /* no interrupt granted */
}


/* Memory access routines.

   These routines access memory for reads and writes.  They validate the
   accesses for MP and MEM violations, if enabled.  The following routines are
   provided:

     - ReadPW  : Read a word using a physical address
     - ReadB   : Read a byte using the current map
     - ReadBA  : Read a byte using the alternate map
     - ReadW   : Read a word using the current map
     - ReadWA  : Read a word using the alternate map
     - ReadIO  : Read a word using the specified map without protection
     - ReadTAB : Read a word using the current map without protection

     - WritePW : Write a word using a physical address
     - WriteB  : Write a byte using the current map
     - WriteBA : Write a byte using the alternate map
     - WriteW  : Write a word using the current map
     - WriteWA : Write a word using the alternate map
     - WriteIO : Write a word using the specified map without protection

   The memory protect (MP) and memory expansion module (MEM) accessories provide
   a protected mode that guards against improper accesses by user programs.
   They may be enabled or disabled independently, although protection requires
   that both be enabled.  MP checks that memory writes do not fall below the
   Memory Protect Fence Register (MPFR) value, and MEM checks that read/write
   protection rules on the target page are compatible with the access desired.
   If either check fails, and MP is enabled, then the request is aborted.

   Each mapped routine calls "dms" if DMS is enabled to translate the logical
   address supplied to a physical address.  "dms" performs a protection check
   and aborts without returning if the check fails.  The write routines perform
   an additional memory-protect check and abort if a violation occurs (so, to
   pass, a page must be writable AND the target must be above the MP fence).

   Note that MP uses a lower bound of 2 for memory writes, allowing unrestricted
   access to the A and B registers (addressed as locations 0 and 1).
*/

#define MP_TEST(va)     (mp_control && ((va) >= 2) && ((va) < mp_fence))


/* Read a word using a physical address */

uint16 ReadPW (uint32 pa)
{
if (pa <= 1)                                            /* read locations 0 or 1? */
    return ABREG[pa];                                   /* return A/B register */
else                                                    /* location >= 2 */
    return M[pa];                                       /* return physical memory value */
}


/* Read a byte using the current map */

uint8 ReadB (uint32 va)
{
int32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va >> 1, dms_ump, RDPROT);                /* translate address */
else                                                    /* MEM disabled */
    pa = va >> 1;                                       /* use logical as physical address */

if (va & 1)                                             /* low byte addressed? */
    return (ReadPW (pa) & 0377);                        /* mask to lower byte */
else                                                    /* high byte addressed */
    return ((ReadPW (pa) >> 8) & 0377);                 /* position higher byte and mask */
}


/* Read a byte using the alternate map */

uint8 ReadBA (uint32 va)
{
uint32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va >> 1, dms_ump ^ MAP_LNT, RDPROT);      /* translate address using alternate map */
else                                                    /* MEM disabled */
    pa = va >> 1;                                       /* use logical as physical address */

if (va & 1)                                             /* low byte addressed? */
    return (ReadPW (pa) & 0377);                        /* mask to lower byte */
else                                                    /* high byte addressed */
    return ((ReadPW (pa) >> 8) & 0377);                 /* position higher byte and mask */
}


/* Read a word using the current map */

uint16 ReadW (uint32 va)
{
uint32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va, dms_ump, RDPROT);                     /* translate address */
else                                                    /* MEM disabled */
    pa = va;                                            /* use logical as physical address */

return ReadPW (pa);                                     /* return word */
}


/* Read a word using the alternate map */

uint16 ReadWA (uint32 va)
{
uint32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va, dms_ump ^ MAP_LNT, RDPROT);           /* translate address using alternate map */
else                                                    /* MEM disabled */
    pa = va;                                            /* use logical as physical address */

return ReadPW (pa);                                     /* return word */
}


/* Read a word using the specified map without protection */

uint16 ReadIO (uint32 va, uint32 map)
{
uint32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va, map, NOPROT);                         /* translate address with no protection */
else                                                    /* MEM disabled */
    pa = va;                                            /* use logical as physical address */

return M[pa];                                           /* return word without A/B interception */
}


/* Read a word using the current map without protection */

static uint16 ReadTAB (uint32 va)
{
uint32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va, dms_ump, NOPROT);                     /* translate address with no protection */
else                                                    /* MEM disabled */
    pa = va;                                            /* use logical as physical address */

return ReadPW (pa);                                     /* return word */
}


/* Write a word using a physical address */

void WritePW (uint32 pa, uint32 dat)
{
if (pa <= 1)                                            /* write locations 0 or 1? */
    ABREG[pa] = dat & DMASK;                            /* store A/B register */
else if (pa < fwanxm)                                   /* 2 <= location <= LWA memory? */
    M[pa] = dat & DMASK;                                /* store physical memory value */

return;
}


/* Write a byte using the current map */

void WriteB (uint32 va, uint32 dat)
{
uint32 pa, t;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va >> 1, dms_ump, WRPROT);                /* translate address */
else                                                    /* MEM disabled */
    pa = va >> 1;                                       /* use logical as physical address */

if (MP_TEST (va >> 1))                                  /* MPCK? */
    MP_ABORT (va >> 1);                                 /* MP violation */

t = ReadPW (pa);                                        /* get word */

if (va & 1)                                             /* low byte addressed? */
    t = (t & 0177400) | (dat & 0377);                   /* merge in lower byte */
else                                                    /* high byte addressed */
    t = (t & 0377) | ((dat & 0377) << 8);               /* position higher byte and merge */

WritePW (pa, t);                                        /* store word */
return;
}


/* Write a byte using the alternate map */

void WriteBA (uint32 va, uint32 dat)
{
uint32 pa, t;

if (dms_enb) {                                          /* MEM enabled? */
    dms_viol (va >> 1, MVI_WPR);                        /* always a violation if protected */
    pa = dms (va >> 1, dms_ump ^ MAP_LNT, WRPROT);      /* translate address using alternate map */
    }
else                                                    /* MEM disabled */
    pa = va >> 1;                                       /* use logical as physical address */

if (MP_TEST (va >> 1))                                  /* MPCK? */
    MP_ABORT (va >> 1);                                 /* MP violation */

t = ReadPW (pa);                                        /* get word */

if (va & 1)                                             /* low byte addressed? */
    t = (t & 0177400) | (dat & 0377);                   /* merge in lower byte */
else                                                    /* high byte addressed */
    t = (t & 0377) | ((dat & 0377) << 8);               /* position higher byte and merge */

WritePW (pa, t);                                        /* store word */
return;
}


/* Write a word using the current map */

void WriteW (uint32 va, uint32 dat)
{
uint32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va, dms_ump, WRPROT);                     /* translate address */
else                                                    /* MEM disabled */
    pa = va;                                            /* use logical as physical address */

if (MP_TEST (va))                                       /* MPCK? */
    MP_ABORT (va);                                      /* MP violation */

WritePW (pa, dat);                                      /* store word */
return;
}


/* Write a word using the alternate map */

void WriteWA (uint32 va, uint32 dat)
{
int32 pa;

if (dms_enb) {                                          /* MEM enabled? */
    dms_viol (va, MVI_WPR);                             /* always a violation if protected */
    pa = dms (va, dms_ump ^ MAP_LNT, WRPROT);           /* translate address using alternate map */
    }
else                                                    /* MEM disabled */
    pa = va;                                            /* use logical as physical address */

if (MP_TEST (va))                                       /* MPCK? */
    MP_ABORT (va);                                      /* MP violation */

WritePW (pa, dat);                                      /* store word */
return;
}


/* Write a word using the specified map without protection */

void WriteIO (uint32 va, uint32 dat, uint32 map)
{
uint32 pa;

if (dms_enb)                                            /* MEM enabled? */
    pa = dms (va, map, NOPROT);                         /* translate address with no protection */
else                                                    /* MEM disabled */
    pa = va;                                            /* use logical as physical address */

if (pa < fwanxm)
    M[pa] = dat & DMASK;                                /* store word without A/B interception */
return;
}


/* Mapped access check.

   Returns TRUE if the address will be mapped (presuming MEM is enabled).
*/

static t_bool is_mapped (uint32 va)
{
uint32 dms_fence;

if (va >= 02000)                                        /* above the base bage? */
    return TRUE;                                        /* always mapped */
else {
    dms_fence = dms_sr & MST_FENCE;                     /* get BP fence value */
    return (dms_sr & MST_FLT) ? (va < dms_fence) :      /* below BP fence and lower portion mapped? */
                                (va >= dms_fence);      /*   or above BP fence and upper portion mapped? */
    }
}


/* DMS relocation.

   This routine translates logical into physical addresses.  It must be called
   only when DMS is enabled, as that condition is not checked.  The logical
   address, desired map, and desired access type are supplied.  If the access is
   legal, the mapped physical address is returned; if it is not, then a MEM
   violation is indicated.

   The current map may be specified by passing "dms_ump" as the "map" parameter,
   or a specific map may be used.  Normally, read and write accesses pass RDPROT
   or WRPROT as the "prot" parameter to request access checking.  For DMA
   accesses, NOPROT must be passed to inhibit access checks.

   This routine checks for read, write, and base-page violations and will call
   "dms_viol" as appropriate.  The latter routine will abort if MP is enabled,
   or will return if protection is off.
*/

static uint32 dms (uint32 va, uint32 map, uint32 prot)
{
uint32 pgn, mpr;

if (va <= 1)                                            /* reference to A/B register? */
    return va;                                          /* use address */

if (!is_mapped (va)) {                                  /* unmapped? */
    if ((va >= 2) && (prot == WRPROT))                  /* base page write access? */
        dms_viol (va, MVI_BPG);                         /* signal a base page violation */
    return va;                                          /* use unmapped address */
    }

pgn = VA_GETPAG (va);                                   /* get page num */
mpr = dms_map[map + pgn];                               /* get map reg */

if (mpr & prot)                                         /* desired access disallowed? */
    dms_viol (va, prot);                                /* signal protection violation */

return (MAP_GETPAG (mpr) | VA_GETOFF (va));             /* return mapped address */
}


/* DMS relocation for console access.

   Console access allows the desired map to be specified by switches on the
   command line.  All protection checks are off for console access.

   This routine is called to restore a saved configuration, and mapping is not
   used for restoration.
*/

static uint32 dms_cons (uint32 va, int32 sw)
{
uint32 map_sel;

if ((dms_enb == 0) ||                                   /* DMS off? */
    (sw & (SWMASK ('N') | SIM_SW_REST)))                /* no mapping rqst or save/rest? */
    return va;                                          /* use physical address */

else if (sw & SWMASK ('S'))
    map_sel = SMAP;

else if (sw & SWMASK ('U'))
    map_sel = UMAP;

else if (sw & SWMASK ('P'))
    map_sel = PAMAP;

else if (sw & SWMASK ('Q'))
    map_sel = PBMAP;

else                                                    /* dflt to log addr, cur map */
    map_sel = dms_ump;

if (va >= VASIZE)                                       /* virtual, must be 15b */
    return MEMSIZE;

else if (dms_enb)                                       /* DMS on? go thru map */
    return dms (va, map_sel, NOPROT);

else                                                    /* else return virtual */
    return va;
}


/* Memory protect and DMS validation for jumps.

   Jumps are a special case of write validation.  The target address is treated
   as a write, even when no physical write takes place, so jumping to a
   write-protected page causes a MEM violation.  In addition, a MEM violation is
   indicated if the jump is to the unmapped portion of the base page.  Finally,
   jumping to a location under the memory-protect fence causes an MP violation.

   Because the MP and MEM hardware works in parallel, all three violations may
   exist concurrently.  For example, a JMP to the unmapped portion of the base
   page that is write protected and under the MP fence will indicate a
   base-page, write, and MP violation, whereas a JMP to the mapped portion will
   indicate a write and MP violation (BPV is inhibited by the MEBEN signal).  If
   MEM and MP violations occur concurrently, the MEM violation takes precedence,
   as the SFS and SFC instructions test the MEV flip-flop.

   The lower bound of protected memory is passed in the "plb" argument.  This
   must be either 0 or 2.  All violations are qualified by the MPCND signal,
   which responds to the lower bound.  Therefore, if the lower bound is 2, and
   if the part below the base-page fence is unmapped, or if the base page is
   write-protected, then a MEM violation will occur only if the access is not to
   locations 0 or 1.  The instruction set firmware uses a lower bound of 0 for
   JMP, JLY, and JPY (and for JSB with W5 out), and of 2 for DJP, SJP, UJP, JRS,
   and .GOTO (and JSB with W5 in).

   Finally, all violations are inhibited if MP is off (mp_control is CLEAR), and
   MEM violations are inhibited if the MEM is disabled.
*/

void mp_dms_jmp (uint32 va, uint32 plb)
{
uint32 violation = 0;
uint32 pgn = VA_GETPAG (va);                            /* get page number */

if (mp_control) {                                       /* MP on? */
    if (dms_enb) {                                      /* MEM on? */
        if (dms_map [dms_ump + pgn] & WRPROT)           /* page write protected? */
            violation = MVI_WPR;                        /* write violation occured */

        if (!is_mapped (va) && (va >= plb))             /* base page target? */
            violation = violation | MVI_BPG;            /* base page violation occured */

        if (violation)                                  /* any violation? */
            dms_viol (va, violation);                   /* signal MEM violation */
        }

    if ((va >= plb) && (va < mp_fence))                 /* jump under fence? */
        MP_ABORT (va);                                  /* signal MP violation */
    }

return;
}


/* DMS read and write map registers */

uint16 dms_rmap (uint32 mapi)
{
mapi = mapi & MAP_MASK;
return (dms_map[mapi] & ~MAP_RSVD);
}

void dms_wmap (uint32 mapi, uint32 dat)
{
mapi = mapi & MAP_MASK;
dms_map[mapi] = (uint16) (dat & ~MAP_RSVD);
return;
}


/* Process a MEM violation.

   A MEM violation will report the cause in the violation register.  This occurs
   even if the MEM is not in the protected mode (i.e., MP is not enabled).  If
   MP is enabled, an MP abort is taken with the MEV flip-flop set.  Otherwise,
   we return to the caller.
*/

void dms_viol (uint32 va, uint32 st)
{
dms_vr = st | dms_upd_vr (va);                          /* set violation cause in register */

if (mp_control) {                                       /* memory protect on? */
    mp_mevff = SET;                                     /* record memory expansion violation */
    MP_ABORT (va);                                      /* abort */
    }
return;
}


/* Update the MEM violation register.

   In hardware, the MEM violation register (VR) is clocked on every memory read,
   every memory write above the lower bound of protected memory, and every
   execution of a privileged DMS instruction.  The register is not clocked when
   MP is disabled by an MP or MEM error (i.e., when MEVFF sets or CTL5FF
   clears), in order to capture the state of the MEM.  In other words, the VR
   continually tracks the memory map register accessed plus the MEM state
   (MEBEN, MAPON, and USR) until a violation occurs, and then it's "frozen."

   Under simulation, we do not have to update the VR on every memory access,
   because the visible state is only available via a programmed RVA/B
   instruction or via the SCP interface.  Therefore, it is sufficient if the
   register is updated:

     - at a MEM violation (when freezing)
     - at an MP violation (when freezing)
     - during RVA/B execution (if not frozen)
     - before returning to SCP after a simulator stop (if not frozen)
*/

uint32 dms_upd_vr (uint32 va)
{
if (mp_control && (mp_mevff == CLEAR)) {                /* violation register unfrozen? */
    dms_vr = VA_GETPAG (va) |                           /* set map address */
             (dms_enb ? MVI_MEM : 0) |                  /*   and MEM enabled */
             (dms_ump ? MVI_UMP : 0);                   /*   and user map enabled */

    if (is_mapped (va))                                 /* is addressed mapped? */
        dms_vr = dms_vr | MVI_MEB;                      /* ME bus is enabled */
    }

return dms_vr;
}


/* Update the MEM status register */

uint32 dms_upd_sr (void)
{
dms_sr = dms_sr & ~(MST_ENB | MST_UMP | MST_PRO);

if (dms_enb)
    dms_sr = dms_sr | MST_ENB;

if (dms_ump)
    dms_sr = dms_sr | MST_UMP;

if (mp_control)
    dms_sr = dms_sr | MST_PRO;

return dms_sr;
}


/* CPU (SC 0) I/O signal handler.

   I/O instructions for select code 0 manipulate the interrupt system.  STF and
   CLF turn the interrupt system on and off, and SFS and SFC test the state of
   the interrupt system.  When the interrupt system is off, only power fail and
   parity error interrupts are allowed.

   A PON reset initializes certain CPU registers.  The 1000 series does a
   microcoded memory clear and leaves the T and P registers set as a result.

   Front-panel PRESET performs additional initialization.  We also handle MEM
   preset here.

   Implementation notes:

    1. An IOI signal reads the floating I/O bus (0 on all machines).

    2. A CLC 0 issues CRS to all devices, not CLC.  While most cards react
       identically to CRS and CLC, some do not, e.g., the 12566B when used as an
       I/O diagnostic target.

    3. RTE uses the undocumented SFS 0,C instruction to both test and turn off
       the interrupt system.  This is confirmed in the "RTE-6/VM Technical
       Specifications" manual (HP 92084-90015), section 2.3.1 "Process the
       Interrupt", subsection "A.1 $CIC":

        "Test to see if the interrupt system is on or off.  This is done with
         the SFS 0,C instruction.  In either case, turn it off (the ,C does
         it)."

       ...and in section 5.8, "Parity Error Detection":

        "Because parity error interrupts can occur even when the interrupt
         system is off, the code at $CIC must be able to save the complete
         system status. The major hole in being able to save the complete state
         is in saving the interrupt system state. In order to do this in both
         the 21MX and the 21XE the instruction 103300 was used to both test the
         interrupt system and turn it off."

    4. Select code 0 cannot interrupt, so there is no SIR handler.
*/

uint32 cpuio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint32 sc;
IOSIGNAL signal;
IOCYCLE  working_set = signal_set;                      /* no SIR handler needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            ion = CLEAR;                                /* turn interrupt system off */
            break;

        case ioSTF:                                     /* set flag flip-flop */
            ion = SET;                                  /* turn interrupt system on */
            break;

        case ioSFC:                                     /* skip if flag is clear */
            setSKF (!ion);                              /* skip if interrupt system is off */
            break;

        case ioSFS:                                     /* skip if flag is set */
            setSKF (ion);                               /* skip if interupt system is on */
            break;

        case ioIOI:                                     /* I/O input */
            stat_data = IORETURN (SCPE_OK, 0);          /* returns 0 */
            break;

        case ioPON:                                     /* power on normal */
            AR = 0;                                     /* clear A register */
            BR = 0;                                     /* clear B register */
            SR = 0;                                     /* clear S register */
            TR = 0;                                     /* clear T register */
            E = 1;                                      /* set E register */

            if (UNIT_CPU_FAMILY == UNIT_FAMILY_1000) {  /* 1000 series? */
                memset (M, 0, MEMSIZE * 2);             /* zero allocated memory */
                MR = 0077777;                           /* set M register */
                PC = 0100000;                           /* set P register */
                }

            else {                                      /* 21xx series */
                MR = 0;                                 /* clear M register */
                PC = 0;                                 /* clear P register */
                }
            break;

        case ioPOPIO:                                   /* power-on preset to I/O */
            O = 0;                                      /* clear O register */
            ion = CLEAR;                                /* turn off interrupt system */
            ion_defer = FALSE;                          /* clear interrupt deferral */

            dms_enb = 0;                                /* turn DMS off */
            dms_ump = 0;                                /* init to system map */
            dms_sr = 0;                                 /* clear status register and BP fence */
            dms_vr = 0;                                 /* clear violation register */
            break;

        case ioCLC:                                     /* clear control flip-flop */
            for (sc = CRSDEV; sc <= MAXDEV; sc++)       /* send CRS to devices */
                devdisp (sc, ioCRS, 0);                 /*   from select code 6 and up */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Overflow/S-register (SC 1) I/O signal handler.

   Flag instructions directed to select code 1 manipulate the overflow (O)
   register.  Input and output instructions access the switch (S) register.  On
   the 2115 and 2116, there is no S-register indicator, so it is effectively
   read-only.  On the other machines, a front-panel display of the S-register is
   provided.  On all machines, front-panel switches are provided to set the
   contents of the S register.

   Implementation notes:

    1. Select code 1 cannot interrupt, so there is no SIR handler.
*/

uint32 ovflio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = signal_set;                      /* no SIR handler needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            O = 0;                                      /* clear overflow */
            break;

        case ioSTF:                                     /* set flag flip-flop */
            O = 1;                                      /* set overflow */
            break;

        case ioSFC:                                     /* skip if flag is clear */
            setSKF (!O);                                /* skip if overflow is clear */
            break;

        case ioSFS:                                     /* skip if flag is set */
            setSKF (O);                                 /* skip if overflow is set */
            break;

        case ioIOI:                                     /* I/O input */
            stat_data = IORETURN (SCPE_OK, SR);         /* read switch register value */
            break;

        case ioIOO:                                     /* I/O output */
            if ((UNIT_CPU_MODEL != UNIT_2116) &&        /* no S register display on */
                (UNIT_CPU_MODEL != UNIT_2115))          /*   2116 and 2115 machines */
                SR = IODATA (stat_data);                /* write S register value */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Power fail (SC 4) I/O signal handler.

   Power fail detection is standard on 2100 and 1000 systems and is optional on
   21xx systems.  Power fail recovery is standard on the 2100 and optional on
   the others.  Power failure or restoration will cause an interrupt on select
   code 4.  The direction of power change (down or up) can be tested by SFC.

   We do not implement power fail under simulation.  However, the central
   interrupt register (CIR) is always read by an IOI directed to select code 4.
*/

uint32 pwrfio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioSTC:                                     /* set control flip-flop */
            break;                                      /* reinitializes power fail */

        case ioCLC:                                     /* clear control flip-flop */
            break;                                      /* reinitializes power fail */

        case ioSFC:                                     /* skip if flag is clear */
            break;                                      /* skips if power fail occurred */

        case ioIOI:                                     /* I/O input */
            stat_data = IORETURN (SCPE_OK, intaddr);    /* input CIR value */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Memory protect/parity error (SC 5) I/O signal handler.

   The memory protect card has a number of non-standard features:

    - CLF and STF affect the parity error enable flip-flop, not the flag
    - SFC and SFS test the memory expansion violation flip-flop, not the flag
    - POPIO clears control, flag, and flag buffer instead of setting the flags
    - CLC does not clear control (the only way to turn off MP is to cause a
      violation)
    - PRL and IRQ are a function of the flag only, not flag and control
    - IAK is used unqualified by IRQ

   The IAK backplane signal is asserted when any interrupt is acknowledged by
   the CPU.  Normally, an interface qualifies IAK with its own IRQ to ensure
   that it responds only to an acknowledgement of its own request.  The MP card
   does this to reset its flag buffer and flag flip-flops, and to reset the
   parity error indication.  However, it also responds to an unqualified IAK
   (i.e., for any interface) as follows:

    - clears the MPV flip-flop
    - clears the indirect counter
    - clears the control flip-flop
    - sets the INTPT flip-flop

   The INTPT flip-flop indicates an occurrence of an interrupt.  If the trap
   cell of the interrupting device contains an I/O instruction that is not a
   HLT, action equivalent to STC 05 is taken, i.e.:

    - sets the control flip-flop
    - set the EVR flip-flop
    - clears the MEV flip-flop
    - clears the PARERR flip-flop

   In other words, an interrupt for any device will disable MP unless the trap
   cell contains an I/O instruction other than a HLT.

   Implementation notes:

    1. Because the card uses IAK unqualified, this routine is called whenever
       any interrupt occurs.  If the MP card itself is not interrupting, the
       select code passed will not be SC 05.  In either case, the trap cell
       instruction is passed in the data portion of the "stat_data" parameter.

    2. The MEV flip-flop records memory expansion (a.k.a. dynamic mapping)
       violations.  It is set when an DM violation is encountered and can be
       tested via SFC/SFS.

    3. MP cannot be turned off in hardware, except by causing a violation.
       Microcode typically does this by executing an IOG micro-order with select
       code /= 1, followed by an IAK to clear the interrupt and a FTCH to clear
       the INTPT flip-flop.  Under simulation, mp_control may be set to CLEAR to
       produce the same effect.

    4. Parity error logic is not implemented.
*/

uint32 protio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16 data;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            break;                                      /* turns off PE interrupt */

        case ioSTF:                                     /* set flag flip-flop */
            break;                                      /* turns on PE interrupt */

        case ioENF:                                     /* enable flag */
            mp_flag = mp_flagbuf = SET;                 /* set flag buffer and flag flip-flops */
            mp_evrff = CLEAR;                           /* inhibit violation register updates */
            break;

        case ioSFC:                                     /* skip if flag is clear */
            setSKF (!mp_mevff);                         /* skip if MP interrupt */
            break;

        case ioSFS:                                     /* skip if flag is set */
            setSKF (mp_mevff);                          /* skip if DMS interrupt */
            break;

        case ioIOI:                                     /* I/O input */
            stat_data = IORETURN (SCPE_OK, mp_viol);    /* read MP violation register */
            break;

        case ioIOO:                                     /* I/O output */
            mp_fence = IODATA (stat_data) & VAMASK;     /* write to MP fence register */

            if (cpu_unit.flags & UNIT_2100)             /* 2100 IOP uses MP fence */
                iop_sp = mp_fence;                      /*   as a stack pointer */
            break;

        case ioPOPIO:                                   /* power-on preset to I/O */
            mp_control = CLEAR;                         /* clear control flip-flop */
            mp_flag = mp_flagbuf = CLEAR;               /* clear flag and flag buffer flip-flops */
            mp_mevff = CLEAR;                           /* clear memory expansion violation flip-flop */
            mp_evrff = SET;                             /* set enable violation register flip-flop */
            break;

        case ioSTC:                                     /* set control flip-flop */
            mp_control = SET;                           /* turn on MP */
            mp_mevff = CLEAR;                           /* clear memory expansion violation flip-flop */
            mp_evrff = SET;                             /* set enable violation register flip-flop */
            break;

        case ioSIR:                                     /* set interrupt request */
            setPRL (PRO, !mp_flag);                     /* set PRL signal */
            setIRQ (PRO, mp_flag);                      /* set IRQ signal */
            break;

        case ioIAK:                                     /* interrupt acknowledge */
            if (dibptr->select_code == PRO)             /* MP interrupt acknowledgement? */
                mp_flag = mp_flagbuf = CLEAR;           /* clear flag and flag buffer */

            data = IODATA (stat_data);                  /* get trap cell instruction */

            if (((data & I_NMRMASK) != I_IO) ||         /* trap cell instruction not I/O */
                (I_GETIOOP (data) == soHLT))            /*   or is halt? */
                mp_control = CLEAR;                     /* turn protection off */
            else {                                      /* non-HLT I/O instruction leaves MP on */
                mp_mevff = CLEAR;                       /*   but clears MEV flip-flop */
                mp_evrff = SET;                         /*   and reenables violation register flip-flop */
                }
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* DMA/DCPC secondary (SC 2/3) I/O signal handler.

   DMA consists of one (12607B) or two (12578A/12895A/12897B) channels.  Each
   channel uses two select codes: 2 and 6 for channel 1, and 3 and 7 for channel
   2.  The lower select codes are used to configure the memory address register
   (control word 2) and the word count register (control word 3).  The upper
   select codes are used to configure the service select register (control word
   1) and to activate and terminate the transfer.

   There are differences in the implementations of the memory address and word
   count registers among the various cards.  The 12607B (2114) supports 14-bit
   addresses and 13-bit word counts.  The 12578A (2115/6) supports 15-bit
   addresses and 14-bit word counts.  The 12895A (2100) and 12897B (1000)
   support 15-bit addresses and 16-bit word counts.

   Implementation notes:

    1. Because the I/O bus floats to zero on 211x computers, an IOI (read word
       count) returns zeros in the unused bit locations, even though the word
       count is a negative value.

    2. Select codes 2 and 3 cannot interrupt, so there is no SIR handler.
*/

uint32 dmasio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
const CHANNEL ch = (CHANNEL) dibptr->card_index;        /* DMA channel number */
uint16 data;
IOSIGNAL signal;
IOCYCLE  working_set = signal_set;                      /* no SIR handler needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioIOI:                                     /* I/O data input */
            if (UNIT_CPU_MODEL == UNIT_2114)            /* 2114? */
                data = dma [ch].cw3 & 0017777;          /* only 13-bit count */
            else if (UNIT_CPU_TYPE == UNIT_TYPE_211X)   /* 2115/2116? */
                data = dma [ch].cw3 & 0037777;          /* only 14-bit count */
            else                                        /* other models */
                data = dma [ch].cw3;                    /* rest use full value */

            stat_data = IORETURN (SCPE_OK, data);       /* merge status and remaining word count */
            break;

        case ioIOO:                                                 /* I/O data output */
            if (dma [ch].select)                                    /* word count selected? */
                dma [ch].cw3 = IODATA (stat_data);                  /* save count */
            else                                                    /* memory address selected */
                if (UNIT_CPU_MODEL == UNIT_2114)                    /* 2114? */
                    dma [ch].cw2 = IODATA (stat_data) & 0137777;    /* only 14-bit address */
                else                                                /* other models */
                    dma [ch].cw2 = IODATA (stat_data);              /* full address stored */
            break;

        case ioCLC:                                     /* clear control flip-flop */
            dma [ch].select = CLEAR;                    /* set for word count access */
            break;

        case ioSTC:                                     /* set control flip-flop */
            dma [ch].select = SET;                      /* set for memory address access */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* DMA/DCPC primary (SC 6/7) I/O signal handler.

   The primary DMA control interface and the service select register are
   manipulated through select codes 6 and 7.  Each channel has transfer enable,
   control, flag, and flag buffer flip-flops.  Transfer enable must be set via
   STC to start DMA.  Control is used only to enable the DMA completion
   interrupt; it is set by STC and cleared by CLC.  Flag and flag buffer are set
   at transfer completion to signal an interrupt.  STF may be issued to abort a
   transfer in progress.

   Again, there are hardware differences between the various DMA cards.  The
   12607B (2114) stores only bits 2-0 of the select code and interprets them as
   select codes 10-16 (SRQ17 is not decoded).  The 12578A (2115/16), 12895A
   (2100), and 12897B (1000) support the full range of select codes (10-77
   octal).

   Implementation notes:

     1. An IOI reads the floating S-bus (high on the 1000, low on the 21xx).

     2. The CRS signal on the DMA card resets the secondary (SC 2/3) select
        flip-flops.  Under simulation, ioCRS is dispatched to select codes 6 and
        up, so we reset the flip-flop in our handler.

     3. The 12578A supports byte-sized transfers by setting bit 14.  Bit 14 is
        ignored by all other DMA cards, which support word transfers only.
        Under simulation, we use a byte-packing/unpacking register to hold one
        byte while the other is read or written during the DMA cycle.
*/

uint32 dmapio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
const CHANNEL ch = (CHANNEL) dibptr->card_index;        /* DMA channel number */
uint16 data;
IOSIGNAL signal;
IOCYCLE  working_set = IOADDSIR (signal_set);           /* add ioSIR if needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioCLF:                                     /* clear flag flip-flop */
            dma [ch].flag = dma [ch].flagbuf = CLEAR;   /* clear flag and flag buffer */
            break;

        case ioSTF:                                     /* set flag flip-flop */
        case ioENF:                                     /* enable flag */
            dma [ch].flag = dma [ch].flagbuf = SET;     /* set flag and flag buffer */
            dma [ch].xferen = CLEAR;                    /* clear transfer enable to abort transfer */
            break;

        case ioSFC:                                     /* skip if flag is clear */
            setstdSKF (dma [ch]);                       /* skip if transfer in progress */
            break;

        case ioSFS:                                     /* skip if flag is set */
            setstdSKF (dma [ch]);                       /* skip if transfer is complete */
            break;

        case ioIOI:                                     /* I/O data input */
            if (UNIT_CPU_TYPE == UNIT_TYPE_1000)        /* 1000? */
                stat_data = IORETURN (SCPE_OK, DMASK);  /* return all ones */
            else                                        /* other models */
                stat_data = IORETURN (SCPE_OK, 0);      /* return all zeros */
            break;

        case ioIOO:                                     /* I/O data output */
            data = IODATA (stat_data);                  /* clear supplied status */

            if (UNIT_CPU_MODEL == UNIT_2114)            /* 12607? */
                dma [ch].cw1 = (data & 0137707) | 010;  /* mask SC, convert to 10-17 */
            else if (UNIT_CPU_TYPE == UNIT_TYPE_211X)   /* 12578? */
                dma [ch].cw1 = data;                    /* store full select code, flags */
            else                                        /* 12895, 12897 */
                dma [ch].cw1 = data & ~DMA1_PB;         /* clip byte-packing flag */
            break;

       case ioPOPIO:                                    /* power-on preset to I/O */
            dma [ch].flag = dma [ch].flagbuf = SET;     /* set flag and flag buffer */
            break;

        case ioCRS:                                     /* control reset */
            dma [ch].xferen = CLEAR;                    /* clear transfer enable */
            dma [ch].select = CLEAR;                    /* set secondary for word count access */
                                                        /* fall into CLC handler */

        case ioCLC:                                     /* clear control flip-flop */
            dma [ch].control = CLEAR;                   /* clear control */
            break;

        case ioSTC:                                     /* set control flip-flop */
            dma [ch].packer = 0;                        /* clear packing register */
            dma [ch].xferen = dma [ch].control = SET;   /* set transfer enable and control */
            break;

        case ioSIR:                                     /* set interrupt request */
            setstdPRL (dma [ch]);
            setstdIRQ (dma [ch]);
            break;

        case ioIAK:                                     /* interrupt acknowledge */
            dma [ch].flagbuf = CLEAR;                   /* clear flag buffer */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Unassigned select code I/O signal handler.

   The 21xx/1000 I/O structure requires that no empty slots exist between
   interface cards.  This is due to the hardware priority chaining (PRH/PRL).
   If it is necessary to leave unused I/O slots, HP 12777A Priority Jumper Cards
   must be installed in them to maintain priority continuity.

   Under simulation, every unassigned I/O slot behaves as though a 12777A were
   resident.

   Implementation notes:

     1. For select codes < 10 octal, an IOI reads the floating S-bus (high on
        the 1000, low on the 21xx).  For select codes >= 10 octal, an IOI reads
        the floating I/O bus (low on all machines).

     2. If "stop_dev" is TRUE, then the simulator will stop when an unassigned
        device is accessed.
*/

uint32 nullio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16 data = 0;
IOSIGNAL signal;
IOCYCLE  working_set = signal_set;                      /* no SIR handler needed */

while (working_set) {
    signal = IONEXT (working_set);                      /* isolate next signal */

    switch (signal) {                                   /* dispatch I/O signal */

        case ioIOI:                                     /* I/O data input */
            if ((dibptr->select_code < VARDEV) &&       /* internal device */
                (UNIT_CPU_TYPE == UNIT_TYPE_1000))      /*   and 1000? */
                data = DMASK;                           /* return all ones */
            else                                        /* external or other model */
                data = 0;                               /* return all zeros */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return IORETURN (stop_dev, data);                       /* flag missing device */
}


/* DMA cycle routine.

   This routine performs one DMA input or output cycle using the indicated DMA
   channel number and DMS map.  When the transfer word count reaches zero, the
   flag is set on the corresponding DMA channel to indicate completion.

   The 12578A card supports byte-packing.  If bit 14 in control word 1 is set,
   each transfer will involve one read/write from memory and two output/input
   operations in order to transfer sequential bytes to/from the device.

   DMA I/O cycles differ from programmed I/O cycles in that multiple I/O control
   backplane signals may be asserted simultaneously.  With programmed I/O, only
   CLF may be asserted with other signals, specifically with STC, CLC, SFS, SFC,
   IOI, or IOO.  With DMA, as many as five signals may be asserted concurrently.

   DMA I/O timing looks like this:

           ------------ Input ------------   ----------- Output ------------
     Sig    Normal Cycle      Last Cycle      Normal Cycle      Last Cycle
     ===   ==============   ==============   ==============   ==============
     IOI   T2-T3            T2-T3
     IOO                                        T3-T4            T3-T4
     STC *    T3                                T3               T3
     CLC *                     T3-T4                             T3-T4
     CLF      T3                                T3               T3
     EDT                          T4                                T4

      * if enabled by control word 1

   Under simulation, this routine dispatches one set of I/O signals per DMA
   cycle to the target device's I/O signal handler.  The signals correspond to
   the table above, except that all signals for a given cycle are concurrent
   (e.g., the last input cycle has IOI, EDT, and optionally CLC asserted, even
   though IOI and EDT are not coincident in hardware).  I/O signal handlers will
   process these signals sequentially, in the order listed above, before
   returning.

   Implementation notes:

    1. The address increment and word count decrement is done only after the I/O
       cycle has completed successfully.  This allows a failed transfer to be
       retried after correcting the I/O error.
*/

static t_stat dma_cycle (CHANNEL ch, uint32 map)
{
const uint32 dev   = dma [ch].cw1 & I_DEVMASK;          /* device select code */
const uint32 stc   = dma [ch].cw1 & DMA1_STC;           /* STC enable flag */
const uint32 bytes = dma [ch].cw1 & DMA1_PB;            /* pack bytes flag */
const uint32 clc   = dma [ch].cw1 & DMA1_CLC;           /* CLC enable flag */
const uint32 MA    = dma [ch].cw2 & VAMASK;             /* memory address */
const uint32 input = dma [ch].cw2 & DMA2_OI;            /* input flag */
const uint32 even  = dma [ch].packer & DMA_OE;          /* odd/even packed byte flag */
uint16 data;
t_stat status;
uint32 ioresult;
IOCYCLE signals;

if (bytes && !even || dma [ch].cw3 != DMASK) {          /* normal cycle? */
    if (input)                                          /* input cycle? */
        signals = ioIOI | ioCLF;                        /* assert IOI and CLF */
    else                                                /* output cycle */
        signals = ioIOO | ioCLF;                        /* assert IOO and CLF */

    if (stc)                                            /* STC wanted? */
        signals = signals | ioSTC;                      /* assert STC */
    }

else {                                                  /* last cycle */
    if (input)                                          /* input cycle? */
        signals = ioIOI | ioEDT;                        /* assert IOI and EDT */
    else {                                              /* output cycle */
        signals = ioIOO | ioCLF | ioEDT;                /* assert IOO and CLF and EDT */

        if (stc)                                        /* STC wanted? */
            signals = signals | ioSTC;                  /* assert STC */
        }

    if (clc)                                            /* CLC wanted? */
        signals = signals | ioCLC;                      /* assert CLC */
    }

if (input) {                                            /* input cycle? */
    ioresult = devdisp (dev, signals,                   /* do I/O input */
                        IORETURN (SCPE_OK, 0));

    status = IOSTATUS (ioresult);                       /* get cycle status */

    if (status == SCPE_OK) {                            /* good I/O cycle? */
        data = IODATA (ioresult);                       /* extract return data value */

        if (bytes) {                                    /* byte packing? */
            if (even) {                                 /* second byte? */
                data = (dma [ch].packer << 8) |         /* merge stored byte */
                         (data & DMASK8);
                WriteIO (MA, data, map);                /* store word data */
                }
            else                                        /* first byte */
                dma [ch].packer = (data & DMASK8);      /* save it */

            dma [ch].packer = dma [ch].packer ^ DMA_OE; /* flip odd/even bit */
            }
        else                                            /* no byte packing */
            WriteIO (MA, data, map);                    /* store word data */
        }
    }

else {                                                  /* output cycle */
    if (bytes) {                                        /* byte packing? */
        if (even)                                       /* second byte? */
            data = dma [ch].packer & DMASK8;            /* retrieve it */

        else {                                          /* first byte */
            dma [ch].packer = ReadIO (MA, map);         /* read word data */
            data = (dma [ch].packer >> 8) & DMASK8;     /* get high byte */
            }

        dma [ch].packer = dma [ch].packer ^ DMA_OE;     /* flip odd/even bit */
        }
    else                                                /* no byte packing */
        data = ReadIO (MA, map);                        /* read word data */

    ioresult = devdisp (dev, signals,                   /* do I/O output */
                        IORETURN (SCPE_OK, data));

    status = IOSTATUS (ioresult);                       /* get cycle status */
    }

if ((even || !bytes) && (status == SCPE_OK)) {          /* new byte or no packing and good xfer? */
    dma [ch].cw2 = input | (dma [ch].cw2 + 1) & VAMASK; /* increment address */
    dma [ch].cw3 = (dma [ch].cw3 + 1) & DMASK;          /* increment word count */

    if (dma [ch].cw3 == 0)                              /* end of transfer? */
        dmapio (dtab [DMA1 + ch], ioENF, 0);            /* set DMA channel flag */
    }

return status;                                          /* return I/O status */
}


/* Reset routines.

   The reset routines are called to simulate either an initial power on
   condition or a front-panel PRESET button press.  For initial power on
   (corresponds to PON, POPIO, and CRS signal assertion in the CPU), the "P"
   command switch will be set.  For PRESET (corresponds to POPIO and CRS
   assertion), the switch will be clear.

   SCP delivers a power-on reset to all devices when the simulator is started.
   A RUN, BOOT, RESET, or RESET ALL command delivers a PRESET to all devices.  A
   RESET <dev> delivers a PRESET to a specific device.
*/


/* CPU reset.

   If this is the first call after simulator startup, allocate the initial
   memory array, set the default CPU model, and install the default BBL.
*/

t_stat cpu_reset (DEVICE *dptr)
{
if (M == NULL) {                                        /* initial call after startup? */
    pcq_r = find_reg ("PCQ", NULL, dptr);               /* get PC queue pointer */

    if (pcq_r)                                          /* defined? */
        pcq_r->qptr = 0;                                /* initialize queue */
    else                                                /* not defined */
        return SCPE_IERR;                               /* internal error */

    M = (uint16 *) calloc (PASIZE, sizeof (uint16));    /* alloc mem */

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
        sim_vm_fprint_stopped = &hp_fprint_stopped;     /* set sim stop printer */
        sim_brk_types = ALL_BKPTS;                      /* register allowed breakpoint types */
        }
    }

if (sim_switches & SWMASK ('P'))                        /* PON reset? */
    IOPOWERON (&cpu_dib);
else                                                    /* PRESET */
    IOPRESET (&cpu_dib);

sim_brk_dflt = SWMASK ('N');                            /* type is nomap as DMS is off */

return SCPE_OK;
}


/* Memory protect reset */

t_stat mp_reset (DEVICE *dptr)
{
IOPRESET (&mp_dib);                                     /* PRESET device (does not use PON) */

mp_fence = 0;                                           /* clear fence register */
mp_viol = 0;                                            /* clear violation register */

return SCPE_OK;
}


/* DMA reset */

t_stat dma_reset (DEVICE *dptr)
{
DIB *dibptr = (DIB *) dptr->ctxt;                       /* DIB pointer */
const CHANNEL ch = (CHANNEL) dibptr->card_index;        /* DMA channel number */

if (UNIT_CPU_MODEL != UNIT_2114)                        /* 2114 has only one channel */
    hp_enbdis_pair (dma_dptrs [ch],                     /* make specified channel */
                    dma_dptrs [ch ^ 1]);                /*   consistent with other channel */

if (sim_switches & SWMASK ('P')) {                      /* power-on reset? */
    dma [ch].cw1 = 0;                                   /* clear control word registers */
    dma [ch].cw2 = 0;
    dma [ch].cw3 = 0;
    }

IOPRESET (dibptr);                                      /* PRESET device (does not use PON) */

dma [ch].packer = 0;                                    /* clear byte packer */

return SCPE_OK;
}


/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
uint16 d;

if ((sw & ALL_MAPMODES) && (dms_enb == 0))              /* req map with DMS off? */
    return SCPE_NOFNC;                                  /* command not allowed */

addr = dms_cons (addr, sw);                             /* translate address as indicated */

if (addr >= MEMSIZE)                                    /* beyond memory limits? */
    return SCPE_NXM;                                    /* non-existent memory */

if ((sw & SIM_SW_REST) || (addr >= 2))                  /* restoring or memory access? */
    d = M[addr];                                        /* return memory value */
else                                                    /* not restoring and A/B access */
    d = ABREG[addr];                                    /* return A/B register value */

if (vptr != NULL)
    *vptr = d & DMASK;                                  /* store return value */
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if ((sw & ALL_MAPMODES) && (dms_enb == 0))              /* req map with DMS off? */
    return SCPE_NOFNC;                                  /* command not allowed */

addr = dms_cons (addr, sw);                             /* translate address as indicated */

if (addr >= MEMSIZE)                                    /* beyond memory limits? */
    return SCPE_NXM;                                    /* non-existent memory */

if ((sw & SIM_SW_REST) || (addr >= 2))                  /* restoring or memory access? */
    M[addr] = val & DMASK;                              /* store memory value */
else                                                    /* not restoring and A/B access */
    ABREG[addr] = val & DMASK;                          /* store A/B register value */

return SCPE_OK;
}


/* Make a pair of devices consistent */

void hp_enbdis_pair (DEVICE *ccp, DEVICE *dcp)
{
if (ccp->flags & DEV_DIS)
    dcp->flags = dcp->flags | DEV_DIS;
else
    dcp->flags = dcp->flags & ~DEV_DIS;

return;
}


/* VM command post-processor

   Update T register to contents of memory addressed by M register
   if M register has changed.
*/

void hp_post_cmd (t_bool from_scp)
{
if (MR != saved_MR) {                                   /* M changed since last update? */
    saved_MR = MR;
    TR = ReadTAB (MR);                                  /* sync T with new M */
    }
return;
}


/* Test for device conflict */

static t_bool dev_conflict (void)
{
DEVICE *dptr;
DIB *dibptr;
uint32 i, j, k;
t_bool is_conflict = FALSE;
uint32 conflicts [MAXDEV + 1] = { 0 };

for (i = 0; sim_devices [i] != NULL; i++) {
    dptr = sim_devices [i];
    dibptr = (DIB *) dptr->ctxt;
    if (dibptr && !(dptr->flags & DEV_DIS))
        if (++conflicts [dibptr->select_code] > 1)
            is_conflict = TRUE;
    }

if (is_conflict) {
    sim_ttcmd();
    for (i = 0; i <= MAXDEV; i++) {
        if (conflicts [i] > 1) {
            k = conflicts [i];

            printf ("Select code %o conflict:", i);

            if (sim_log)
                fprintf (sim_log, "Select code %o conflict:", i);

            for (j = 0; sim_devices [j] != NULL; j++) {
                dptr = sim_devices [j];
                dibptr = (DIB *) dptr->ctxt;
                if (dibptr && !(dptr->flags & DEV_DIS) && i == dibptr->select_code) {
                    if (k < conflicts [i]) {
                        printf (" and");

                        if (sim_log)
                            fputs (" and", sim_log);
                        }

                    printf (" %s", sim_dname (dptr));

                    if (sim_log)
                        fprintf (sim_log, " %s", sim_dname (dptr));

                    k = k - 1;

                    if (k == 0) {
                        putchar ('\n');

                        if (sim_log)
                            fputc ('\n', sim_log);
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
    for (i = new_size; i < MEMSIZE; i++)                /* check truncated memory */
        mc = mc | M[i];                                 /*   for content */

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

for (i = fwanxm; i < old_size; i++)                     /* zero non-existent memory */
    M[i] = 0;

return SCPE_OK;
}


/* Change CPU models.

   For convenience, MP and DMA are typically enabled if available; they may be
   disabled subsequently if desired.  Note that the 2114 supports only one DMA
   channel (channel 1).  All other models support two channels.

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
    dma1_dev.flags = dma1_dev.flags & ~DEV_DIS;         /* enable DMA channel 1 */

    if (new_model == UNIT_2114)                         /* 2114 has only one channel */
        dma2_dev.flags = dma2_dev.flags |  DEV_DIS;     /* disable channel 2 */
    else                                                /* all others have two channels */
        dma2_dev.flags = dma2_dev.flags & ~DEV_DIS;     /* enable it */
    }
else {
    dma1_dev.flags = dma1_dev.flags | DEV_DIS;          /* disable channel 1 */
    dma2_dev.flags = dma2_dev.flags | DEV_DIS;          /* disable channel 2 */
    }

if (cpu_features[new_index].opt & UNIT_DMA) {           /* DMA an option? */
    dma1_dev.flags = dma1_dev.flags |  DEV_DISABLE;     /* make it alterable */

    if (new_model == UNIT_2114)                         /* 2114 has only one channel */
        dma2_dev.flags = dma2_dev.flags & ~DEV_DISABLE; /* make it unalterable */
    else                                                /* all others have two channels */
        dma2_dev.flags = dma2_dev.flags |  DEV_DISABLE; /* make it alterable */
    }
else {
    dma1_dev.flags = dma1_dev.flags & ~DEV_DISABLE;     /* make it unalterable */
    dma2_dev.flags = dma2_dev.flags & ~DEV_DISABLE;     /* make it unalterable */
    }


if ((old_family == UNIT_FAMILY_1000) &&                 /* if current family is 1000 */
    (new_family == UNIT_FAMILY_21XX)) {                 /* and new family is 21xx */
    deassign_device (&dma1_dev);                        /* delete DCPC names */
    deassign_device (&dma2_dev);
    }
else if ((old_family == UNIT_FAMILY_21XX) &&            /* if current family is 21xx */
         (new_family == UNIT_FAMILY_1000)) {            /* and new family is 1000 */
    assign_device (&dma1_dev, "DCPC1");                 /* change DMA device name */
    assign_device (&dma2_dev, "DCPC2");                 /* to DCPC for familiarity */
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
   - If CPU is 1000-F, ensures that VIS and IOP are mutually exclusive.
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

else if (UNIT_CPU_MODEL == UNIT_1000_F)
    if (option == UNIT_VIS)                             /* 1000-F IOP and VIS options */
        uptr->flags = uptr->flags & ~UNIT_IOP;          /* are mutually exclusive */
    else if (option == UNIT_IOP)
        uptr->flags = uptr->flags & ~UNIT_VIS;

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
   memory").  When the two are equal, the loader is enabled.  When the current
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
static BOOT_ROM loader;
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


/* Idle enable/disable */

t_stat cpu_set_idle (UNIT *uptr, int32 option, char *cptr, void *desc)
{
    if (option)
        return sim_set_idle (uptr, 10, NULL, NULL);
    else
        return sim_clr_idle (uptr, 0, NULL, NULL);
}


/* Idle display */

t_stat cpu_show_idle (FILE *st, UNIT *uptr, int32 val, void *desc)
{
    return sim_show_idle (st, uptr, val, desc);
}


/* IBL routine (CPU boot) */

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
int32 dev = (SR >> IBL_V_DEV) & I_DEVMASK;
int32 sel = (SR >> IBL_V_SEL) & IBL_M_SEL;

if (dev < 010)
    return SCPE_NOFNC;

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
        ibl_copy (ds_rom, dev);
        break;
        }

return SCPE_OK;
}


/* IBL boot ROM copy

   - Use memory size to set the initial PC and base of the boot area
   - Copy boot ROM to memory, updating I/O instructions
   - Place 2s complement of boot base in last location

   Notes:
   - SR settings are done by the caller
   - Boot ROMs must be assembled with a device code of 10 (10 and 11 for
     devices requiring two codes)
*/

t_stat ibl_copy (const BOOT_ROM rom, int32 dev)
{
int32 i;
uint16 wd;

cpu_set_ldr (NULL, TRUE, NULL, NULL);                   /* enable loader (ignore errors) */

if (dev < 010)                                          /* valid device? */
    return SCPE_ARG;

PC = ((MEMSIZE - 1) & ~IBL_MASK) & VAMASK;              /* start at mem top */

for (i = 0; i < IBL_LNT; i++) {                         /* copy bootstrap */
    wd = rom[i];                                        /* get word */

    if (((wd & I_NMRMASK) == I_IO) &&                   /* IO instruction? */
        ((wd & I_DEVMASK) >= 010) &&                    /* dev >= 10? */
        (I_GETIOOP (wd) != soHLT))                      /* not a HALT? */
        M[PC + i] = (wd + (dev - 010)) & DMASK;         /* change dev code */

    else                                                /* leave unchanged */
        M[PC + i] = wd;
    }

M[PC + IBL_DPC] = (M[PC + IBL_DPC] + (dev - 010)) & DMASK;  /* patch DMA ctrl */
M[PC + IBL_END] = (~PC + 1) & DMASK;                        /* fill in start of boot */
return SCPE_OK;
}
