/* hp2100_cpu.c: HP 21xx/1000 Central Processing Unit/MEM/MP/DCPC simulator

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2018, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   CPU          2114C/2115A/2116C/2100A/1000-M/E/F Central Processing Unit
                12731A Memory Expansion Module
   DMA1,DMA2    12607B/12578A/12895A Direct Memory Access
   DCPC1,DCPC2  12897B Dual Channel Port Controller
   MP           12581A/12892B Memory Protect

   21-May-18    JDB     Changed "access" to "mem_access" to avoid clashing
   07-May-18    JDB     Modified "io_dispatch" to display outbound signals
   01-May-18    JDB     Multiple consecutive CLC 0 operations are now omitted
   02-Apr-18    JDB     SET CPU 21MX now configures an M-Series model
   22-Feb-18    JDB     Reworked "cpu_ibl" into "cpu_copy_loader"
   11-Aug-17    JDB     MEM must be disabled when DMS is disabled
   01-Aug-17    JDB     Changed SET/SHOW CPU [NO]IDLE to use sim_*_idle routines
   22-Jul-17    JDB     Renamed "intaddr" to CIR; added IR
   18-Jul-17    JDB     Added CPU stops
   11-Jul-17    JDB     Moved "hp_enbdis_pair" to hp2100_sys.c
                        Renamed "ibl_copy" to "cpu_ibl"
   10-Jul-17    JDB     Renamed the global routine "iogrp" to "cpu_iog"
   07-Jul-17    JDB     Changed "iotrap" from uint32 to t_bool
   26-Jun-17    JDB     Moved I/O instruction subopcode constants from hp2100_defs.h
   16-May-17    JDB     Changed REG_A, REG_B to REG_X
   19-Apr-17    JDB     SET CPU IDLE now omits idle loop tracing
   04-Apr-17    JDB     Added "cpu_configuration" for symbolic ex/dep validation
                        Rejected model change no longer changes options
   21-Mar-17    JDB     IOP is now illegal on the 1000 F-Series
   27-Feb-17    JDB     Added BBL load for 21xx machines
                        ibl_copy no longer returns a status code
   22-Feb-17    JDB     Added DMA tracing
   21-Feb-17    JDB     Added bus tracing to the I/O dispatcher
   19-Jan-17    JDB     Added CPU tracing
                        Consolidated the memory read and write routines
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   13-May-16    JDB     Modified for revised SCP API function parameter types
   31-Dec-14    JDB     Corrected devdisp data parameters
   30-Dec-14    JDB     Added S-register parameters to ibl_copy
   24-Dec-14    JDB     Added casts for explicit downward conversions
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
     - 2100A Computer Reference Manual
         (02100-90001, Dec-1971)
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
     - 12892B Memory Protect Installation Manual
         (12892-90007, Jun-1978)
     - HP 1000 Computer Real-Time Systems
         (5091-4479, August 1992)


   Hewlett-Packard sold the HP 21xx/1000 family of real-time computers from 1966
   through 2000.  There are three major divisions within this family: the 21xx
   core-memory machines, the 1000 (originally 21MX) M/E/F-Series semiconductor-
   memory machines, and the 1000 L/A-Series machines.  All machines are 16-bit
   accumulator-oriented CISC machines running the same base instruction set.  A
   wide range of operating systems run on these machines, from a simple 4K word
   paper-tape-based monitor to a megaword multi-user, multiprogramming disc-
   based system and a multi-user time-shared BASIC system.

   This implementation is a simulator for the 2114, 2115, 2116, 2100, and 1000
   M/E/F-Series machines.  A large variety of CPU options, device interface
   cards, and peripherals are provided.  High-speed I/O transfers are performed
   by Direct Memory Access and Dual-Channel Port Controller options.  This
   simulator does not model the 1000 L/A-Series machines.

   All of the machines support a 15-bit logical address space, addressing a
   maximum of 32 K words, divided into 1K-word pages.  Memory-referencing
   instructions in the base set can directly address the 1024 words of the base
   page (page 0) or the 1024 words of the current page (the page containing the
   instruction).  The instructions in the extended set directly address the
   32768 words in the full logical address space.  The A and B accumulators may
   be addressed as logical addresses 0 and 1, respectively.

   Peripheral devices are connected to the CPU by interface cards installed in
   the I/O card cages present in the CPU and optional I/O extender chassis. Each
   slot in the card cage is assigned an address, called a select code, that may
   be referenced by I/O instructions in the base set.  Select codes range from 0
   to 77 octal, with the first eight select codes reserved for the system,
   providing connections for 56 possible interfaces.

   The 211x machines use a hardwired processor providing 70 basic instructions
   and up to 32K of core memory.  The base instruction set is divided into the
   Memory Reference Group, the Shift-Rotate Group, the Alter-Skip Group, and the
   I/O Group.  SRG instruction words may contain from one to four suboperation
   codes that are executed from left-to-right, and ASG instruction words may
   contain from one to eight suboperations.  An optional Extended Arithmetic
   Unit may be added to the 2115 and 2116 that provides hardware multiply and
   divide, double-load and -store, and double-word shift and rotate
   instructions.

   The 2100 machine uses a microprogrammed processor that provides the 80
   instructions of the base set and the EAU as standard equipment.  Optional
   floating-point microcode adds six two-word single-precision instructions.
   User microprogramming is also supported.  When used as part of an HP 2000
   Time-Shared BASIC system, the CPU designated as the I/O processor may be
   equipped with microcode implementing 18 additional OS accelerator
   instructions.

   The 1000 M/E-Series machines also use microprogrammed processors and extend
   the 2100 instruction set with two new index registers, X and Y, and a new
   Extended Instruction Group consisting of 32 index-register instructions and
   10 word-and-byte-manipulation instructions.  The six 2100 floating-point
   instructions are also standard.  The 1000 F-Series adds a hardware
   floating-point processor with 18 new triple- and quad-word instructions.  A
   number of new optional microcode extensions are available with the
   M/E/F-Series.

   1000 CPUs offer the optional Dynamic Mapping System, which provides memory
   mapping on a page-by-page basis.  The 5-bit page number of a logical memory
   address selects one of 32 ten-bit map registers containing physical page
   numbers.  The ten-bit page number combined with the ten-bit page offset
   yields a 20-bit physical address capable of accessing a location in a
   one-megaword memory.  DMS provides separate maps for system and user
   programs, as well as for the two DCPC channels, and includes microcode that
   implements the 38 Dynamic Mapping Instructions used to manipulate the mapping
   system.

   Optional memory protection is accomplished by dividing the logical address
   space into protected and unprotected parts.  When protection is enabled, any
   attempt to write below the fence separating the two parts is inhibited, and
   an interrupt to the operating system occurs, which aborts the offending user
   program.  If the DMS option is enabled as well, protection is enhanced by
   specifying read and write permissions on a page-by-page basis.

   A note on terminology: the 1000 series of computers was originally called the
   21MX at introduction.  The 21MX (occasionally, 21MXM) corresponds to the 1000
   M-Series, and the 21MXE (occasionally, 21XE) corresponds to the 1000
   E-Series.  The model numbers were changed before the introduction of the 1000
   F-Series, although some internal HP documentation refers to this machine as
   the 21MXF.

   The terms MEM (Memory Expansion Module), MEU (Memory Expansion Unit), DMI
   (Dynamic Mapping Instructions), and DMS (Dynamic Mapping System) are used
   somewhat interchangeably to refer to the logical-to-physical memory address
   translation option provided on the 1000-Series.  DMS consists of the MEM card
   (12731A) and the DMI firmware (13307A).  However, MEM and MEU have been used
   interchangeably to refer to the mapping card, as have DMI and DMS to refer to
   the firmware instructions.


   These CPU hardware registers are present in all machines:

     Name  Width  Description
     ----  -----  ----------------------------------------------
      A     16    accumulator (addressable as memory location 0)
      B     16    accumulator (addressable as memory location 1)
      P     15    program counter
      S     16    switch and display register
      M     15    memory address register
      T     16    memory data register
      E      1    extend flag (carry out)
      O      1    overflow flag

   The 1000 Series adds these CPU hardware registers:

     Name  Width  Description
     ----  -----  ----------------------------------------------
      X     16    index register
      Y     16    index register

   The data types supported by the base instruction set are:

     - 8-bit unsigned byte
     - 16-bit unsigned integer
     - 16-bit two's-complement integer
     - 32-bit two's-complement integer
     - 32-bit two's-complement floating point

   Multi-word values are stored in memory with the most-significant words in the
   lowest addresses.  Bytes are stored in memory with the most-significant byte
   in the upper half of the 16-bit word and the least-significant byte in the
   lower half.

   The instruction set is fairly irregular -- a legacy of its original
   implementation in hardware in the 2116 and the accretion of microprogrammed
   instructions in the 2100 and 1000 CPUs.  Initially, there were five base-set
   instruction groups:

     1. Memory-Reference Group (MRG)
     2. Shift-Rotate Group (SRG)
     3. Alter-Skip Group (ASG)
     4. I/O Group (IOG)
     5. Macroinstruction Group (MAC)

   All of the instructions added after the 2116 are in the Macroinstruction
   Group.

   The 2116 offered two hardware options that extended the instruction set.  The
   first is the 12579A Extended Arithmetic Unit.  The second is the 2152A
   Floating Point Processor, which is interfaced through, and therefore
   requires, the EAU.  The EAU adds 10 instructions including integer multiply
   and divide and double-word loads, stores, shifts, and rotates.  The FPP adds
   30 floating-point arithmetic, trigonometric, logarithmic, and exponential
   instructions.  (The 2116 FFP is not simulated.)

   The base set groups are decoded from bits 15-12 and 10, as follows:

     15  14-12  10  Group  Address Ranges
     --  -----  --  -----  -------------------------------
      x   nnn    x   MRG   010000-077777 and 110000-177777
      0   000    0   SRG   000000-001777 and 004000-005777
      0   000    1   ASG   002000-003777 and 006000-007777
      1   000    1   IOG   102000-103777 and 106000-107777
      1   000    0   MAC   100000-101777 and 104000-105777

   Where:

     x = don't care
     n = any combination other than all zeros

   The MAC group is subdivided into the Extended Arithmetic Group (EAG) and the
   User Instruction Group (UIG), based on bits 11, 9, and 8, as follows:

     11   9   8  Group  Address Range
     --  --  --  -----  -------------
      0   0   0  EAG    100000-100377
      0   0   1  EAG    100400-100777
      0   1   0  EAG    101000-101377
      0   1   1  UIG-1  101400-101777
      1   0   0  EAG    104000-104377
      1   0   1  EAG    104400-104777
      1   1   0  UIG-0  105000-105377
      1   1   1  UIG-1  105400-105777

   All of the 2116 FPP instructions are in the UIG sets: 3 use 10144x opcodes
   and the rest use 1050xx and 1054xx opcodes.  The 2100 decodes only UIG-0
   instructions, whereas the 1000s use both UIG sets.  In particular, the
   105740-105777 range is used by the 1000 Extended Instruction Group (EIG),
   which is part of the 1000-Series base set.

   The 21xx and 1000 M/E/F-Series machines do not trap unimplemented
   instructions.  In general, unimplemented EAG instructions cause erroneous
   execution, and unimplemented UIG instructions execute as NOP.  However, there
   are machine-to-machine variations, and some unimplemented instructions
   execute as other, defined instructions.

   The instruction set groups are encoded as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | I |    mem op     | P |            memory address             |  MRG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | I |  mem op   | R | P |            memory address             |  MRG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = direct/indirect (0/1)
     R = A/B register (0/1)
     P = base/current page (0/1)

   The "mem ops" are encoded as follows:

     14-11  Mnemonic  Action
     -----  --------  ----------------------------------------
     0010     AND     A = A & M [MA]
     0011     JSB     M [MA] = P, P = MA + 1
     0100     XOR     A = A ^ M [MA]
     0101     JMP     P = MA
     0110     IOR     A = A | M [MA]
     0111     ISZ     M [MA] = M [MA] + 1, skip if M [MA] == 0
     1000     ADA     A = A + M [MA]
     1001     ADB     B = B + M [MA]
     1010     CPA     skip if A != M [MA]
     1011     CPB     skip if B != M [MA]
     1100     LDA     A = M [MA]
     1101     LDB     B = M [MA]
     1110     STA     M [MA] = A
     1111     STB     M [MA] = B

   Bits 15 and 10 encode the type of access, as follows:

     15,10  Access Type            Action
     -----  ---------------------  --------------------------
      0,0   base page direct       MA = I <9:0>
      0,1   current page direct    MA = P <14:0>'I <9:0>
      1,0   base page indirect     MA = M [I <9:0>]
      1,1   current page indirect  MA = M [P <14:10>'I <9:0>]


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | R | 0 | E |   op 1    | C | E | S |   op 2    |  SRG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = A/B register (0/1)
     E = disable/enable op
     C = CLE
     S = SL*


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | R | 1 | r op  | e op  | E | S | L | I | Z | V |  ASG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = A/B register (0/1)
     E = SEZ
     S = SS*
     L = SL*
     I = IN*
     Z = SZ*
     V = RSS


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | R | 1 | H |  I/O op   |      select code      |  IOG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = A/B register (0/1)
     H = hold/clear flag (0/1)

   An I/O group instruction controls the device specified by the select code.
   Depending on the opcode, the instruction may set or clear the device flag,
   start or stop I/O, or read or write data.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 |   | 0 |    eau op     | 0   0   0   0   0   0 |  EAU
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 |   | 0 | eau shift/rotate op   |  shift count  |  EAU
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   MAC ops decode when bits 15-12 and 10 are 1 000 0.  Bits 11 and 9-0 determine
   the specific EAU instruction.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | R | 0   1 |      module       |   operation   |  UIG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = A/B register (0/1)


   In simulation, I/O devices are modelled by substituting software states for
   I/O backplane signals.  The set of signals generated by I/O instructions and
   DMA cycles is dispatched to the target device for action.  Backplane signals
   are processed sequentially.  For example, the "STC sc,C" instruction
   generates the "set control" and the "clear flag" signals that are processed
   in that order.

   CPU interrupt signals are modelled as three parallel arrays:

     - device request priority as bit vector dev_prl [2] [31..0]
     - device interrupt requests as bit vector dev_irq [2] [31..0]
     - device service requests as bit vector dev_srq [2] [31..0]

   Each array forms a 64-bit vector, with bits 0-31 of the first element
   corresponding to select codes 00-37 octal, and bits 0-31 of the second
   element corresponding to select codes 40-77 octal.

   The HP 21xx/1000 interrupt structure is based on the PRH, PRL, IRQ, and IAK
   signals.  PRH indicates that no higher-priority device is interrupting. PRL
   indicates to lower-priority devices that a given device is not interrupting.
   IRQ indicates that a given device is requesting an interrupt.  IAK indicates
   that the given device's interrupt request is being acknowledged.

   PRH and PRL form a hardware priority chain that extends from interface to
   interface on the backplane.  We model just PRL, as PRH is calculated from the
   PRLs of higher-priority devices.

   Typical I/O devices have a flag, flag buffer, and control flip-flops.  If a
   device's flag, flag buffer, and control bits are set, and the device is the
   highest priority on the interrupt chain, it requests an interrupt by
   asserting IRQ.  When the interrupt is acknowledged with IAK, the flag buffer
   is cleared, preventing further interrupt requests from that device. The
   combination of flag and control set blocks interrupts from lower priority
   devices.

   Service requests are used to trigger the DMA service logic.  Setting the
   device flag typically also sets SRQ, although SRQ may be calculated
   independently.


   The simulator provides three stop conditions related to instruction execution
   that may be enabled with a SET CPU STOP=<stop> command:

     <stop>  Action
     ------  ------------------------------------------
     UNIMPL  stop on an unimplemented instruction
     UNDEF   stop on an undefined instruction
     UNSC    stop on an access to an unused select code
     IOERR   stop on an unreported I/O error

   If an enabled stop condition is detected, execution ceases with the
   instruction pending, and control returns to the SCP prompt.  When simulation
   stops, execution may be resumed in two ways.  If the cause of the stop has
   not been remedied and the stop has not been disabled, resuming execution with
   CONTINUE, STEP, GO, or RUN will cause the stop to occur again.  Alternately,
   specifying the "-B" switch with any of the preceding commands will resume
   execution while bypassing the stop for the current instruction.

   The UNIMPL option stops the simulator if execution is attempted of an
   instruction provided by a firmware option that is not currently installed
   (e.g., a DAD instruction when the double-integer firmware is not installed)
   or of an opcode provided by an installed option but not assigned to an
   instruction (e.g., opcode 105335 from the double-integer firmware).
   Bypassing the stop will execute the instruction as a NOP (no-operation).

   The UNDEF option stops the simulator if execution is attempted of an
   instruction containing a decoded reserved bit pattern other than that defined
   in the Operating and Reference manual for the CPU.  For example, opcodes
   101700 and 105700 are not listed as DMS instructions, but they execute as
   XMM instructions, rather than as NOP.  The intent of this stop is to catch
   instructions containing reserved fields with values that change the meaning
   of those instructions.  Bypassing the stop will decode and execute the
   instruction in the same manner as the selected CPU.

   The UNSC option stops the simulator if an I/O instruction addresses a select
   code that is not assigned to an enabled device (equivalent to an empty
   hardware I/O backplane slot).  Bypassing the stop will read the floating
   S-bus or I/O-bus for LIA/B and MIA/B instructions or do nothing for all other
   instructions.

   The IOERR option stops the simulator if an I/O error condition exists for a
   device that does not report this status to the CPU.  For example, the paper
   tape reader device (PTR) does not report "no tape loaded" status, and the
   processor interconnect device (IPL) does not report "cable disconnected."  In
   both cases, I/O to the device will simply hang with no indication of the
   problem.  Enabling the IOERR option will stop the simulator with an error
   indication for these devices.

   In addition, a simulation stop will occur if an indirect addressing chain
   exceeds the maximum length specified by a SET CPU INDIR=<limit> command.
   Memory addresses may be indirect to indicate that the values point to the
   target addresses rather than contain the target addresses.  The target of an
   indirect address may itself be indirect, and the CPU follows this chain of
   addresses until it finds a direct address.  Indirect addressing is typically
   only one or two levels deep, but if the chain loops back on itself (e.g., if
   an indirect address points at itself), then instruction execution will hang.

   The limit may be set to any number of levels up to 32,768.  This is the
   absolute maximum number of levels that can be created without an infinite
   loop -- each location in memory points to the next one except for the last,
   which contains the target value.  In practice, anything over a few levels
   likely represents a programming error.  The default setting is 16 levels.


   In addition to the CPU, this module simulates the 12578A/12607B/12895A Direct
   Memory Access and 12897B Dual-Channel Port Controller devices (hereafter,
   "DMA").  These controllers permit the CPU to transfer data directly between
   an I/O device and memory on a cycle-stealing basis.  Depending on the CPU,
   the device interface, and main memory speed, DMA is capable of transferring
   data blocks from 1 to 32,768 words in length at rates between 500,000 and
   1,000,000 words per second.  The 2114 supports a single DMA channel.  All
   other CPUs support two DMA channels.

   DMA is programmed by setting three control words via two select codes: 2 and
   6 for channel 1, and 3 and 7 for channel 2.  During simultaneous transfers,
   channel 1 has priority over channel 2.  Otherwise, the channels are
   identical. Channel programming involves setting three control words, as
   follows:

   SC 06 Control Word 1 format:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | B | C | -   -   -   -   -   -  -  |  device select code   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = assert STC during each cycle
     B = enable byte packing and unpacking (12578A only)
     C = assert CLC at the end of the block transfer

   SC 02 Control Word 2/3 format:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | D |                  starting memory address                  | word 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      negative word count                      | word 3
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     D = transfer direction is out of/into memory (0/1)

   Control word 2 is stored if the control flip-flop of select code 2 is clear,
   i.e., if the OTA/B is preceded by CLC; control word 3 is stored if the
   flip-flop is set by a preceding STC.

   The 12607B supports 14-bit addresses and 13-bit word counts.  The 12578A
   supports 15-bit addresses and 14-bit word counts.  The 12895A and 12897B
   support 15-bit addresses and 16-bit word counts.

   DMA is started by setting the control flip-flop on select code 6.  DMA
   completion is indicated when the flag flip-flop sets on select code 8, which
   causes an interrupt if enabled.


   This module also simulates the 12581A/12892B Memory Protect devices for the
   2116 and 1000 M/E/F-Series, respectively, and the memory protect feature that
   is standard equipment for the 2100.  MP is addressed via select code 5 and
   provides a fence register that holds the address of the start of unprotected
   memory and a violation register that holds the address of the instruction
   that has caused a memory protect interrupt, as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 |          starting address of unprotected memory           | fence
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 |               violating instruction address               | violation
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   After setting the fence regiater with an OTA 5 or OTB 5 instruction, MP is
   enabled by an STC 5.


   This module also simulates the 12731A Memory Expansion Module for the 1000
   M/E/F-Series machines.  The MEM provides mapping of the 32 1024-word logical
   memory pages into a one-megaword physical memory.  Four separate maps are
   provided: system, user, DCPC port A, and DCPC port B.  The MEM is controlled
   by the associated Dynamic Mapping System instructions and contains status and
   violation registers, as follows:

   MEM Status Register:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | I | M | E | U | P | B |        base page fence address        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = MEM disabled/enabled (0/1) at last interrupt
     M = System/user map (0/1) selected at last interrupt
     E = MEM disabled/enabled (0/1) currently
     U = System/user map (0/1) selected currently
     P = Protected mode disabled/enabled (0/1) currently
     B = Base-page portion mapped (0/1 = above/below the fence)

   MEM Violation Register:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | R | W | B | P | -   -   -   - | S | E | M |    map address    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = Read violation
     W = Write violation
     B = Base-page violation
     P = Privileged instruction violation
     S = ME bus disabled/enabled (0/1) at violation
     E = MEM disabled/enabled (0/1) at violation
     M = System/user map (0/1) selected at violation


   The CPU simulator provides extensive tracing capabilities that may be enabled
   with the SET DEBUG <filename> and SET CPU DEBUG=<trace> commands.  The trace
   options that may be specified are:

     Trace  Action
     -----  -------------------------------------------
     INSTR  trace instructions executed
     DATA   trace memory data accesses
     FETCH  trace memory instruction fetches
     REG    trace registers
     OPND   trace instruction operands
     EXEC   trace matching instruction execution states

   A section of an example trace is:

     >>CPU instr: S 0002 05735  103101  CLO
     >>CPU fetch: S 0002 05736  000036    instruction fetch
     >>CPU   reg: - **** 01011  042200    A 177777, B 000000, X 177777, Y 000000, e o i
     >>CPU instr: S 0002 05736  000036  SLA,ELA
     >>CPU fetch: S 0002 05737  102101    instruction fetch
     >>CPU   reg: - **** 01011  042200    A 177776, B 000000, X 177777, Y 000000, E o i
     >>CPU instr: S 0002 05737  102101  STO
     >>CPU fetch: S 0002 05740  002400    instruction fetch
     >>CPU   reg: - **** 01011  042200    A 177776, B 000000, X 177777, Y 000000, E O i
     >>CPU instr: S 0002 05755  102100  STF 0
     >>CPU fetch: S 0002 05756  102705    instruction fetch
     >>CPU   reg: - **** 01011  042200    A 177777, B 177777, X 177777, Y 000000, E O I
     >>CPU instr: S 0002 05756  102705  STC 5
     >>CPU fetch: S 0002 05757  105736    instruction fetch
     >>CPU   reg: P **** 01011  042200    A 177777, B 177777, X 177777, Y 000000, E O I
     >>CPU instr: S 0002 05757  105736  UJP 2111
     >>CPU fetch: S 0002 05760  002111    instruction fetch
     >>CPU fetch: U 0001 02111  026111    instruction fetch
     >>CPU   reg: P **** 01011  042200    A 177777, B 177777, X 177777, Y 000000, E O I
     >>CPU instr: U 0001 02111  026111  JMP 2111
     >>CPU instr: U 0001 02111  000011  interrupt
     >>CPU fetch: S 0000 00011  115013    instruction fetch
     >>CPU   reg: - **** 01011  042200    A 177777, B 177777, X 177777, Y 000000, E O I
     >>CPU   reg: - **** *****  ******    MPF 000000, MPV 002111, MES 163011, MEV 030000
     >>CPU instr: S 0000 00011  115013  JSB 1013,I
     >>CPU  data: S 0000 01013  005557    data read
     >>CPU  data: S 0002 05557  002111    data write
     >>CPU fetch: S 0002 05560  103100    instruction fetch
     >>CPU   reg: - **** 01011  042200    A 177777, B 177777, X 177777, Y 000000, E O I
     >>CPU instr: S 0002 05560  103100  CLF 0
     >>CPU fetch: S 0002 05561  105714    instruction fetch
     >>CPU   reg: - **** 01011  042200    A 177777, B 177777, X 177777, Y 000000, E O i
     >>CPU  exec: ********************
     >>CPU   reg: P **** 01567  000000    A 100036, B 000100, X 000100, Y 074000, E o I
     >>CPU instr: U 0220 07063  105240  .PMAP
     >>CPU  data: U 0000 01776  000227    unprotected read
     >>CPU  data: U 0227 76100  000233    data read
     >>CPU  opnd: * **** 07065  105240    return location is P+2 (no error)
     >>CPU fetch: U 0220 07065  127055    instruction fetch
     >>CPU   reg: P **** 01567  000000    A 100037, B 000101, X 000100, Y 074000, e o I

   The INSTR option traces instruction executions and interrupts.  Each
   instruction is printed in symbolic form before it is executed.

   The DATA option traces reads from and writes to memory.  Each access is
   classified by its usage type as "data" (using the current or alternate map
   with access protection) or "unprotected" (using a specified map without
   protection).

   The FETCH option traces instruction fetches from memory.  Reads of the
   additional words in a multiword instruction, such as the target address of a
   DLD (double load) instruction, are also classified as fetches.

   The REG option traces register values.  Two sets of registers are printed.
   After executing each instruction, the working registers (A, B, E, O, S, and,
   for 1000 CPUs, X and Y) and the state of the interrupt system (on or off) are
   printed.  After executing an instruction that may alter the Memory Protect or
   Memory Expansion Module state, the MP fence and violation registers, the MEM
   status and violation registers, and the current protection state are printed.

   The OPND option traces operand values.  Some instructions that take memory
   and register operands that are difficult to decode from DATA or REG traces
   present the operand values in a higher-level format.  The operand data and
   value presented are specific to the instruction; see the instruction executor
   comments for details.

   The EXEC option traces the execution of instructions that match
   user-specified criteria.  When a match occurs, all CPU trace options are
   turned on for the duration of the execution of the matched instruction.  The
   prior trace settings are restored when a match fails.  This option allows
   detailed tracing of specified instructions while minimizing the log file size
   compared to a full instruction trace.

   The various trace formats are interpreted as follows:

     >>CPU instr: U 0045 10341  016200  LDA 11200
                  ~ ~~~~ ~~~~~  ~~~~~~  ~~~~~~~~~
                  |   |    |       |       |
                  |   |    |       |       +-- instruction mnemonic
                  |   |    |       +---------- octal data (instruction opcode)
                  |   |    +------------------ octal logical address (P register)
                  |   +----------------------- octal physical page number
                  +--------------------------- memory map (S/U/- system/user/disabled)

     >>CPU instr: U 0045 10341  000011  interrupt
                  ~ ~~~~ ~~~~~  ~~~~~~  ~~~~~~~~~
                  |   |    |       |       |
                  |   |    |       |       +-- interrupt classification
                  |   |    |       +---------- octal device number (CIR register)
                  |   |    +------------------ octal logical address at interrupt (P register)
                  |   +----------------------- octal physical page number at interrupt
                  +--------------------------- memory map (S/U/- system/user/disabled)

     >>CPU fetch: - 0000 10341  016200    instruction fetch
     >>CPU  data: U 0013 01200  123003    data read
     >>CPU  data: S 0013 01200  017200    unprotected write
                  ~ ~~~~ ~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~
                  |   |    |       |         |
                  |   |    |       |         +-- memory access classification
                  |   |    |       +------------ octal data (memory contents)
                  |   |    +-------------------- octal logical address (effective address)
                  |   +------------------------- octal physical page number
                  +----------------------------- memory map (S/U/A/B/- system/user/port A/port B/disabled)

     >>CPU   reg: P .... 01535  040013    A 123003, B 001340, X 000000, Y 000000, e O I
                  ~ ~~~~ ~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |   |    |       |         |
                  |   |    |       |         +-- A, B, X, Y, E, O, interrupt system registers
                  |   |    |       |             (lower/upper case = 0/1 or off/on)
                  |   |    |       +------------ S register
                  |   |    +-------------------- MEM fence
                  |   +-------------------------
                  +----------------------------- protection state (P/- protected/unprotected)

     >>CPU   reg: P .... .....  ......    MPF 00000, MPV 000000, MES 000000, MEV 000000
                  ~ ~~~~ ~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |   |    |       |         |
                  |   |    |       |         +-- memory protect fence and violation registers
                  |   |    |       |             memory expansion status and violation registers
                  |   |    |       +------------
                  |   |    +--------------------
                  |   +-------------------------
                  +----------------------------- protection state (P/- protected/unprotected)



     >>CPU  opnd: . .... 36002  101475    return location is P+3 (error EM21)
     >>CPU  opnd: . .... 22067  105355    entry is for a dynamic mapping violation
                         ~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                           |       |         |
                           |       |         +-- operand-specific value
                           |       +------------ operand-specific octal data
                           +-------------------- octal logical address (P register)


   Implementation notes:

    1. The simulator is fast enough, compared to the run-time of the longest
       instructions, for interruptibility not to matter.  However, the HP
       diagnostics explicitly test interruptibility in the EIS and DMS
       instructions and in long indirect address chains.  Accordingly, the
       simulator does "just enough" to pass these tests.  In particular, if an
       interrupt is pending but deferred at the beginning of an interruptible
       instruction, the interrupt is taken at the appropriate point; but there
       is no testing for new interrupts during execution (that is, the event
       timer is not called).
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"



/* CPU program constants */


/* Command line switches */

#define ALL_MAPMODES    (SWMASK ('S') | SWMASK ('U') | SWMASK ('P') | SWMASK ('Q'))


/* RTE base-page addresses */

static const uint32 xeqt = 0001717;             /* XEQT address */
static const uint32 tbg  = 0001674;             /* TBG address */

/* DOS base-page addresses */

static const uint32 m64  = 0000040;             /* constant -64 address */
static const uint32 p64  = 0000067;             /* constant +64 address */


/* CPU global SCP data definitions */

REG *sim_PC = NULL;                             /* the pointer to the P register */


/* CPU global data structures */


/* CPU registers */

HP_WORD ABREG [2] = { 0, 0};                    /* A and B registers */
HP_WORD PR = 0;                                 /* P register */
HP_WORD SR = 0;                                 /* S register */
HP_WORD MR = 0;                                 /* M register */
HP_WORD TR = 0;                                 /* T register */
HP_WORD XR = 0;                                 /* X register */
HP_WORD YR = 0;                                 /* Y register */
uint32  E  = 0;                                 /* E register */
uint32  O  = 0;                                 /* O register */

HP_WORD IR  = 0;                                /* Instruction Register */
HP_WORD CIR = 0;                                /* Central Interrupt Register */


/* CPU global state */

FLIP_FLOP ion = CLEAR;                          /* interrupt enable */
t_bool    ion_defer = FALSE;                    /* interrupt defer */

t_stat    cpu_ss_unimpl   = SCPE_OK;            /* status return for unimplemented instruction execution */
t_stat    cpu_ss_undef    = SCPE_OK;            /* status return for undefined instruction execution */
t_stat    cpu_ss_unsc     = SCPE_OK;            /* status return for I/O to an unassigned select code */
t_stat    cpu_ss_ioerr    = SCPE_OK;            /* status return for an unreported I/O error */
t_stat    cpu_ss_inhibit  = SCPE_OK;            /* CPU stop inhibition mask */
UNIT      *cpu_ioerr_uptr = NULL;               /* pointer to a unit with an unreported I/O error */

uint16    pcq [PCQ_SIZE] = { 0 };               /* PC queue (must be 16-bits wide for REG array entry) */
uint32    pcq_p = 0;                            /* PC queue ptr */
REG       *pcq_r = NULL;                        /* PC queue reg ptr */

uint32    cpu_configuration;                    /* the current CPU option set and model */
uint32    cpu_speed = 1;                        /* the CPU speed, expressed as a multiplier of a real machine */
t_bool    is_1000 = FALSE;                      /* TRUE if the CPU is a 1000 M/E/F-Series */

uint32 dev_prl [2] = { ~0u, ~0u };              /* device priority low bit vector */
uint32 dev_irq [2] = {  0u,  0u };              /* device interrupt request bit vector */
uint32 dev_srq [2] = {  0u,  0u };              /* device service request bit vector */


/* Main memory global state */

MEMORY_WORD *M = NULL;                          /* pointer to allocated memory */


/* Memory Expansion Unit global state */

uint32  dms_enb = 0;                            /* dms enable */
uint32  dms_ump = 0;                            /* dms user map */
HP_WORD dms_sr  = 0;                            /* dms status reg */


/* CPU local state */

static HP_WORD saved_MR = 0;                    /* M-register value between SCP commands */
static uint32  fwanxm   = 0;                    /* first word addr of nx mem */
static uint32  jsb_plb  = 2;                    /* protected lower bound for JSB */

static uint32  exec_mask        = 0;            /* the current instruction execution trace mask */
static uint32  exec_match       = D16_UMAX;     /* the current instruction execution trace matching value */
static uint32  indirect_limit   = 16;           /* the indirect chain length limit */
static uint32  last_select_code = 0;            /* the last select code sent over the I/O backplane */

static uint32  tbg_select_code = 0;             /* the time-base generator select code (for RTE idle check) */
static DEVICE  *loader_rom [4] = { NULL };      /* the four boot loader ROM sockets in a 1000 CPU */


/* Memory Expansion Unit local state */

static HP_WORD dms_vr = 0;                              /* dms violation reg */
static uint16  dms_map [MAP_NUM * MAP_LNT] = { 0 };     /* dms maps (must be 16-bits wide for REG array entry) */


/* CPU local data structures */


/* Interrupt deferral table (1000 version) */

static t_bool defer_tab [] = {                  /* deferral table, indexed by I/O sub-opcode */
    FALSE,                                      /*   soHLT */
    TRUE,                                       /*   soFLG */
    TRUE,                                       /*   soSFC */
    TRUE,                                       /*   soSFS */
    FALSE,                                      /*   soMIX */
    FALSE,                                      /*   soLIX */
    FALSE,                                      /*   soOTX */
    TRUE                                        /*   soCTL */
    };


/* CPU features table.

   The feature table is used to validate CPU feature changes within the subset
   of features supported by a given CPU.  Features in the typical list are
   enabled when the CPU model is selected.  If a feature appears in the typical
   list but NOT in the optional list, then it is standard equipment and cannot
   be disabled.  If a feature appears in the optional list, then it may be
   enabled or disabled as desired by the user.
*/

struct FEATURE_TABLE {                          /* CPU model feature table: */
    uint32      typ;                            /*  - typical features */
    uint32      opt;                            /*  - optional features */
    uint32      maxmem;                         /*  - maximum memory */
    };

static struct FEATURE_TABLE cpu_features [] = {         /* features indexed by CPU model */
  { UNIT_DMA | UNIT_MP,                                 /*   UNIT_2116 */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP | UNIT_EAU,
    32 * 1024
    },

  { UNIT_DMA,                                           /*   UNIT_2115 */
    UNIT_PFAIL | UNIT_DMA | UNIT_EAU,
    8 * 1024
    },

  { UNIT_DMA,                                           /*   UNIT_2114 */
    UNIT_PFAIL | UNIT_DMA,
    16 * 1024 },

  { 0, 0, 0                                             /*   unused model */
    },

  { UNIT_PFAIL | UNIT_MP | UNIT_DMA | UNIT_EAU,         /*   UNIT_2100 */
    UNIT_DMA   | UNIT_FP | UNIT_IOP | UNIT_FFP,
    32 * 1024
    },

  { 0, 0, 0                                             /*   unused model */
    },
  { 0, 0, 0                                             /*   unused model */
    },
  { 0, 0, 0                                             /*   unused model */
    },

  { UNIT_MP | UNIT_DMA | UNIT_EAU | UNIT_FP | UNIT_DMS, /*   UNIT_1000_M */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DS,
    1024 * 1024
    },

  { UNIT_MP | UNIT_DMA | UNIT_EAU | UNIT_FP | UNIT_DMS, /*   UNIT_1000_E */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP  | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DBI | UNIT_DS  | UNIT_EMA_VMA,
    1024 * 1024
    },

  { UNIT_MP  | UNIT_DMA | UNIT_EAU | UNIT_FP |          /*   UNIT_1000_F */
    UNIT_FFP | UNIT_DBI | UNIT_DMS,
    UNIT_PFAIL | UNIT_DMA | UNIT_MP     | UNIT_DMS |
    UNIT_VIS   | UNIT_DS  | UNIT_SIGNAL | UNIT_EMA_VMA,
    1024 * 1024
    }
  };


/* CPU local SCP support routine declarations */

static IOHANDLER cpuio;
static IOHANDLER ovflio;
static IOHANDLER pwrfio;

static t_stat cpu_examine (t_value *eval, t_addr address, UNIT *uptr, int32 switches);
static t_stat cpu_deposit (t_value value, t_addr address, UNIT *uptr, int32 switches);

static t_stat cpu_reset (DEVICE *dptr);
static t_stat cpu_boot  (int32  unitno, DEVICE *dptr);

static t_stat set_stops    (UNIT *uptr, int32 option,    CONST char *cptr, void *desc);
static t_stat set_size     (UNIT *uptr, int32 new_size,  CONST char *cptr, void *desc);
static t_stat set_model    (UNIT *uptr, int32 new_model, CONST char *cptr, void *desc);
static t_stat set_option   (UNIT *uptr, int32 option,    CONST char *cptr, void *desc);
static t_stat clear_option (UNIT *uptr, int32 option,    CONST char *cptr, void *desc);
static t_stat set_loader   (UNIT *uptr, int32 enable,    CONST char *cptr, void *desc);
static t_stat set_roms     (UNIT *uptr, int32 option,    CONST char *cptr, void *desc);
static t_stat set_exec     (UNIT *uptr, int32 option,    CONST char *cptr, void *desc);

static t_stat show_stops (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_roms  (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_exec  (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_speed (FILE *st, UNIT *uptr, int32 val, CONST void *desc);


/* CPU local utility routine declarations */

static t_stat  ea                  (HP_WORD IR, HP_WORD *address, uint32 irq);
static HP_WORD srg_uop             (HP_WORD value, HP_WORD operation);
static t_stat  machine_instruction (HP_WORD IR, t_bool iotrap, uint32 irq_pending, uint32 *idle_save);
static t_bool  check_deferral      (uint32 irq_sc);
static uint32  map_address         (HP_WORD logical, int32 switches);
static t_bool  mem_is_empty        (uint32 starting_address);


/* Memory Expansion Unit local utility routine declarations */

static t_bool is_mapped (uint32 address);
static uint32 meu_map   (HP_WORD address, uint32 map, HP_WORD prot);


/* CPU SCP data structures */


/* Device information blocks */

static DIB cpu_dib = {                          /* CPU select code 0 */
    &cpuio,                                     /*   device interface */
    CPU,                                        /*   select code */
    0                                           /*   card index */
    };

static DIB ovfl_dib = {                         /* Overflow select code 1 */
    &ovflio,                                    /*   device interface */
    OVF,                                        /*   select code */
    0                                           /*   card index */
    };

static DIB pwrf_dib = {                         /* Power Fail select code 4 */
    &pwrfio,                                    /*   device interface */
    PWR,                                        /*   select code */
    0                                           /*   card index */
    };


/* Unit list.

   The CPU unit holds the main memory capacity.


   Implementation notes:

    1. The unit structure must be global for other modules to access the unit
       flags, which describe the installed options, and to obtain the memory
       size via the MEMSIZE macro, which references the "capac" field.
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX | UNIT_BINK, 0) };


/* Register list.

   The CPU register list exposes the machine registers for user inspection and
   modification.


   Implementation notes:

    1. All registers that reference variables of type HP_WORD must have the
       REG_FIT flag for proper access if HP_WORD is a 16-bit type.

    2. The REG_X flag indicates that the register may be displayed in symbolic
       form.
*/

static REG cpu_reg [] = {
/*    Macro   Name       Location            Radix  Width   Offset       Depth                Flags       */
/*    ------  ---------  ------------------  -----  -----  --------  -----------------  ----------------- */
    { ORDATA (P,         PR,                         15)                                                  },
    { ORDATA (A,         AR,                         16),                               REG_X             },
    { ORDATA (B,         BR,                         16),                               REG_X             },
    { ORDATA (M,         MR,                         15)                                                  },
    { ORDATA (T,         TR,                         16),                               REG_RO | REG_X    },
    { ORDATA (X,         XR,                         16),                               REG_X             },
    { ORDATA (Y,         YR,                         16),                               REG_X             },
    { ORDATA (S,         SR,                         16),                               REG_X             },
    { FLDATA (E,         E,                                   0)                                          },
    { FLDATA (O,         O,                                   0)                                          },
    { ORDATA (CIR,       CIR,                         6)                                                  },

    { FLDATA (ION,       ion,                                 0)                                          },
    { FLDATA (ION_DEFER, ion_defer,                           0)                                          },
    { FLDATA (DMSENB,    dms_enb,                             0)                                          },
    { FLDATA (DMSCUR,    dms_ump,                          VA_N_PAG)                                      },

    { ORDATA (DMSSR,     dms_sr,                     16)                                                  },
    { ORDATA (DMSVR,     dms_vr,                     16)                                                  },
    { BRDATA (DMSMAP,    dms_map,              8,    16,             MAP_NUM * MAP_LNT)                   },

    { ORDATA (IOPSP,     iop_sp,                     16)                                                  },
    { BRDATA (PCQ,       pcq,                  8,    15,             PCQ_SIZE),         REG_CIRC | REG_RO },

    { ORDATA (IR,        IR,                         16),                               REG_HRO           },
    { ORDATA (PCQP,      pcq_p,                       6),                               REG_HRO           },
    { ORDATA (JSBPLB,    jsb_plb,                    32),                               REG_HRO           },
    { ORDATA (SAVEDMR,   saved_MR,                   32),                               REG_HRO           },
    { ORDATA (FWANXM,    fwanxm,                     32),                               REG_HRO           },
    { ORDATA (CONFIG,    cpu_configuration,          32),                               REG_HRO           },

    { ORDATA (WRU,       sim_int_char,                8),                               REG_HRO           },
    { ORDATA (BRK,       sim_brk_char,                8),                               REG_HRO           },
    { ORDATA (DEL,       sim_del_char,                8),                               REG_HRO           },

    { BRDATA (PRL,       dev_prl,              8,    32,                 2),            REG_HRO           },
    { BRDATA (IRQ,       dev_irq,              8,    32,                 2),            REG_HRO           },
    { BRDATA (SRQ,       dev_srq,              8,    32,                 2),            REG_HRO           },
    { NULL }
    };


/* Modifier list.


   Implementation notes:

    1. The 21MX monikers are deprecated in favor of the 1000 designations.  See
       the "HP 1000 Series Naming History" on the back inside cover of the
       Technical Reference Handbook.

    2. Each CPU option requires three modifiers.  The two regular modifiers
       control the setting and printing of the option, while the extended
       modifier controls clearing the option.  The latter is necessary because
       the option must be checked before confirming the change, and so the
       option value must be passed to the validation routine.
*/

static MTAB cpu_mod [] = {
/*    Mask Value       Match Value  Print String  Match String  Validation     Display      Descriptor        */
/*    ---------------  -----------  ------------  ------------  -------------  -----------  ----------------- */
    { UNIT_MODEL_MASK, UNIT_2116,   "",           "2116",       &set_model,    &show_model, (void *) "2116"   },
    { UNIT_MODEL_MASK, UNIT_2115,   "",           "2115",       &set_model,    &show_model, (void *) "2115"   },
    { UNIT_MODEL_MASK, UNIT_2114,   "",           "2114",       &set_model,    &show_model, (void *) "2114"   },
    { UNIT_MODEL_MASK, UNIT_2100,   "",           "2100",       &set_model,    &show_model, (void *) "2100"   },
    { UNIT_MODEL_MASK, UNIT_1000_E, "",           "1000-E",     &set_model,    &show_model, (void *) "1000-E" },
    { UNIT_MODEL_MASK, UNIT_1000_M, "",           "1000-M",     &set_model,    &show_model, (void *) "1000-M" },

#if defined (HAVE_INT64)
    { UNIT_MODEL_MASK, UNIT_1000_F, "",           "1000-F",     &set_model,    &show_model, (void *) "1000-F" },
#endif

    { UNIT_MODEL_MASK, UNIT_1000_M, NULL,         "21MX-M",     &set_model,    &show_model, (void *) "1000-M" },
    { UNIT_MODEL_MASK, UNIT_1000_E, NULL,         "21MX-E",     &set_model,    &show_model, (void *) "1000-E" },

    { UNIT_EAU,        UNIT_EAU,    "EAU",        "EAU",        &set_option,   NULL,        NULL              },
    { UNIT_EAU,        0,           "no EAU",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_EAU,     NULL,        "NOEAU",      &clear_option, NULL,        NULL              },

    { UNIT_FP,         UNIT_FP,     "FP",         "FP",         &set_option,   NULL,        NULL              },
    { UNIT_FP,         0,           "no FP",      NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_FP,      NULL,        "NOFP",       &clear_option, NULL,        NULL              },

    { UNIT_IOP,        UNIT_IOP,    "IOP",        "IOP",        &set_option,   NULL,        NULL              },
    { UNIT_IOP,        0,           "no IOP",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_IOP,     NULL,        "NOIOP",      &clear_option, NULL,        NULL              },

    { UNIT_DMS,        UNIT_DMS,    "DMS",        "DMS",        &set_option,   NULL,        NULL              },
    { UNIT_DMS,        0,           "no DMS",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_DMS,     NULL,        "NODMS",      &clear_option, NULL,        NULL              },

    { UNIT_FFP,        UNIT_FFP,    "FFP",        "FFP",        &set_option,   NULL,        NULL              },
    { UNIT_FFP,        0,           "no FFP",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_FFP,     NULL,        "NOFFP",      &clear_option, NULL,        NULL              },

    { UNIT_DBI,        UNIT_DBI,    "DBI",        "DBI",        &set_option,   NULL,        NULL              },
    { UNIT_DBI,        0,           "no DBI",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_DBI,     NULL,        "NODBI",      &clear_option, NULL,        NULL              },

    { UNIT_EMA_VMA,    UNIT_EMA,    "EMA",        "EMA",        &set_option,   NULL,        NULL              },
    { MTAB_XDV,        UNIT_EMA,     NULL,        "NOEMA",      &clear_option, NULL,        NULL              },

    { UNIT_EMA_VMA,    UNIT_VMAOS,  "VMA",        "VMA",        &set_option,   NULL,        NULL              },
    { MTAB_XDV,        UNIT_VMAOS,   NULL,        "NOVMA",      &clear_option, NULL,        NULL              },

    { UNIT_EMA_VMA,    0,           "no EMA/VMA", NULL,         &set_option,   NULL,        NULL              },

#if defined (HAVE_INT64)
    { UNIT_VIS,        UNIT_VIS,    "VIS",        "VIS",        &set_option,   NULL,        NULL              },
    { UNIT_VIS,        0,           "no VIS",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_VIS,     NULL,        "NOVIS",      &clear_option, NULL,        NULL              },

    { UNIT_SIGNAL,     UNIT_SIGNAL, "SIGNAL",     "SIGNAL",     &set_option,   NULL,        NULL              },
    { UNIT_SIGNAL,     0,           "no SIGNAL",  NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_SIGNAL,  NULL,        "NOSIGNAL",   &clear_option, NULL,        NULL              },
#endif

/* Future microcode support.
    { UNIT_DS,         UNIT_DS,     "DS",         "DS",         &set_option,   NULL,        NULL              },
    { UNIT_DS,         0,           "no DS",      NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,        UNIT_DS,      NULL,        "NODS",       &clear_option, NULL,        NULL              },
*/

/*    Entry Flags             Value     Print String  Match String     Validation     Display         Descriptor */
/*    -------------------  -----------  ------------  ---------------  -------------  --------------  ---------- */
    { MTAB_XDV,                 0,      "IDLE",       "IDLE",          &sim_set_idle, &sim_show_idle, NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOIDLE",        &sim_clr_idle, NULL,           NULL       },

    { MTAB_XDV,                 1,      NULL,         "LOADERENABLE",  &set_loader,   NULL,           NULL       },
    { MTAB_XDV,                 0,      NULL,         "LOADERDISABLE", &set_loader,   NULL,           NULL       },

    { MTAB_XDV,               4 * 1024, NULL,         "4K",            &set_size,     NULL,           NULL       },
    { MTAB_XDV,               8 * 1024, NULL,         "8K",            &set_size,     NULL,           NULL       },
    { MTAB_XDV,              12 * 1024, NULL,         "12K",           &set_size,     NULL,           NULL       },
    { MTAB_XDV,              16 * 1024, NULL,         "16K",           &set_size,     NULL,           NULL       },
    { MTAB_XDV,              24 * 1024, NULL,         "24K",           &set_size,     NULL,           NULL       },
    { MTAB_XDV,              32 * 1024, NULL,         "32K",           &set_size,     NULL,           NULL       },
    { MTAB_XDV,              64 * 1024, NULL,         "64K",           &set_size,     NULL,           NULL       },
    { MTAB_XDV,             128 * 1024, NULL,         "128K",          &set_size,     NULL,           NULL       },
    { MTAB_XDV,             256 * 1024, NULL,         "256K",          &set_size,     NULL,           NULL       },
    { MTAB_XDV,             512 * 1024, NULL,         "512K",          &set_size,     NULL,           NULL       },
    { MTAB_XDV,            1024 * 1024, NULL,         "1024K",         &set_size,     NULL,           NULL       },

    { MTAB_XDV | MTAB_NMO,      0,      "ROMS",       "ROMS",          &set_roms,     &show_roms,     NULL       },

    { MTAB_XDV | MTAB_NMO,      1,      "STOPS",      "STOP",          &set_stops,    &show_stops,    NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOSTOP",        &set_stops,    NULL,           NULL       },
    { MTAB_XDV | MTAB_NMO,      2,      "INDIR",      "INDIR",         &set_stops,    &show_stops,    NULL       },

    { MTAB_XDV | MTAB_NMO,      1,      "EXEC",       "EXEC",          &set_exec,     &show_exec,     NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOEXEC",        &set_exec,     NULL,           NULL       },

    { MTAB_XDV | MTAB_NMO,      0,      "SPEED",      NULL,            NULL,          &show_speed,    NULL       },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB cpu_deb [] = {
    { "INSTR", TRACE_INSTR },                   /* trace instruction executions */
    { "DATA",  TRACE_DATA  },                   /* trace memory data accesses */
    { "FETCH", TRACE_FETCH },                   /* trace memory instruction fetches */
    { "REG",   TRACE_REG   },                   /* trace register values */
    { "OPND",  TRACE_OPND  },                   /* trace instruction operands */
    { "EXEC",  TRACE_EXEC  },                   /* trace matching instruction execution states */
    { "NOOS",  DEBUG_NOOS  },                   /* RTE-6/VM will not use OS firmware */
    { NULL,    0 }
    };


/* Simulation stop list.

   The simulator can be configured to detect certain machine instruction
   conditions and stop execution when one of them occurs.  Stops may be enabled
   or disabled individually with these commands:

     SET CPU STOP=<option>[;<option]
     SET CPU NOSTOP=<option>[;<option]

   The CPU stop table is used to parse the commands and set the appropriate
   variables to enable or disable the stops.


   Implementation notes:

    1. To avoid the testing of stop conditions at run time, they are implemented
       by setting individual stop status variables either to the appropriate
       stop code (if enabled) or to SCPE_OK (if disabled).  This allows the
       affected routines to return the status value unconditionally and cause
       either a simulator stop or continued execution without a run-time test.

    2. SCPE_IOERR is not actually returned for unreported I/O errors.  Instead,
       it is simply a flag that a stop code specific to the detected error
       should be returned.

    3. To permit stops to be bypassed for one instruction execution, routines
       use the STOP macro to return the value of the applicable stop variable
       ANDed with the complement of the value of the "cpu_ss_inhibit" variable.
       The latter is set in the instruction prelude to SS_INHIBIT (i.e., all
       ones) if a bypass is requested or to SCPE_OK (i.e., all zeros) if not,
       and is reset to SCPE_OK after each instruction execution.  The effect is
       that SCPE_OK is returned instead of a simulator stop if a stop condition
       occurs when a bypass is specified.  This action depends on the value of
       SCPE_OK being zero (which is guaranteed).
*/

typedef struct {
    const char  *name;                          /* stop name */
    t_stat      *status;                        /* pointer to the stop status variable */
    t_stat      value;                          /* stop status return value */
    } STOPTAB;

static STOPTAB cpu_stop [] = {
    { "UNIMPL", &cpu_ss_unimpl, STOP_UNIMPL },  /* stop on an unimplemented instruction */
    { "UNDEF",  &cpu_ss_undef,  STOP_UNDEF  },  /* stop on an undefined instruction */
    { "UNSC",   &cpu_ss_unsc,   STOP_UNSC   },  /* stop on I/O to an unassigned select code */
    { "IOERR",  &cpu_ss_ioerr,  SCPE_IOERR  },  /* stop on an unreported I/O error */
    { NULL,     NULL,           0           }
    };


/* Device descriptor */

DEVICE cpu_dev = {
    "CPU",                                      /* device name */
    &cpu_unit,                                  /* unit array */
    cpu_reg,                                    /* register array */
    cpu_mod,                                    /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    PA_N_SIZE,                                  /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    &cpu_examine,                               /* examine routine */
    &cpu_deposit,                               /* deposit routine */
    &cpu_reset,                                 /* reset routine */
    &cpu_boot,                                  /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &cpu_dib,                                   /* device information block pointer */
    DEV_DEBUG,                                  /* device flags */
    0,                                          /* debug control flags */
    cpu_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Memory program constants */

static const char map_indicator [] = {          /* MEU map indicator, indexed by map type */
    'S',                                        /*   System */
    'U',                                        /*   User   */
    'A',                                        /*   Port_A */
    'B'                                         /*   Port_B */
    };


/* Memory global data structures */


/* Memory access classification table */

typedef struct {
    uint32      debug_flag;                     /* the debug flag for tracing */
    const char  *name;                          /* the classification name */
    } ACCESS_PROPERTIES;

static const ACCESS_PROPERTIES mem_access [] = {    /* indexed by ACCESS_CLASS */
/*    debug_flag    name                */
/*    ------------  ------------------- */
    { TRACE_FETCH,  "instruction fetch" },          /*   instruction fetch */
    { TRACE_DATA,   "data"              },          /*   data access */
    { TRACE_DATA,   "data"              },          /*   data access, alternate map */
    { TRACE_DATA,   "unprotected"       },          /*   data access, system map */
    { TRACE_DATA,   "unprotected"       },          /*   data access, user map */
    { TRACE_DATA,   "dma"               },          /*   DMA channel 1, port A map */
    { TRACE_DATA,   "dma"               }           /*   DMA channel 2, port B map */
    };



/* DMA program constants */

#define DMA_CHAN_COUNT  2                       /* number of DMA channels */

#define DMA_OE          020000000000u           /* byte packing odd/even flag */
#define DMA1_STC        0100000u                /* DMA - issue STC */
#define DMA1_PB         0040000u                /* DMA - pack bytes */
#define DMA1_CLC        0020000u                /* DMA - issue CLC */
#define DMA2_OI         0100000u                /* DMA - output/input */

typedef enum { ch1, ch2 } CHANNEL;              /* channel number */

#define DMA_1_REQ       (1 << ch1)              /* channel 1 request */
#define DMA_2_REQ       (1 << ch2)              /* channel 2 request */

typedef struct {
    FLIP_FLOP control;                          /* control flip-flop */
    FLIP_FLOP flag;                             /* flag flip-flop */
    FLIP_FLOP flagbuf;                          /* flag buffer flip-flop */
    FLIP_FLOP xferen;                           /* transfer enable flip-flop */
    FLIP_FLOP select;                           /* register select flip-flop */

    HP_WORD   cw1;                              /* device select */
    HP_WORD   cw2;                              /* direction, address */
    HP_WORD   cw3;                              /* word count */
    uint32    packer;                           /* byte-packer holding reg */
    } DMA_STATE;


/* DMA global state */

DMA_STATE dma [DMA_CHAN_COUNT];                 /* per-channel state */


/* DMA local data structures */

static const BITSET_NAME dma_cw1_names [] = {   /* DMA control word 1 names */
    "STC",                                      /*   bit 15 */
    "byte packing",                             /*   bit 14 */
    "CLC"                                       /*   bit 13 */
    };

static const BITSET_FORMAT dma_cw1_format =          /* names, offset, direction, alternates, bar */
    { FMT_INIT (dma_cw1_names, 13, msb_first, no_alt, append_bar) };


/* DMA local SCP support routine declarations */

static IOHANDLER dmapio;
static IOHANDLER dmasio;
static t_stat    dma_reset (DEVICE *dptr);


/* DMA local utility routine declarations */

static t_stat dma_cycle (CHANNEL chan, ACCESS_CLASS class);
static uint32 calc_dma  (void);


/* DMA SCP data structures */


/* Device information blocks */

static DIB dmap1_dib = {
    &dmapio,                                    /* device interface */
    DMA1,                                       /* select code */
    ch1                                         /* card index */
    };

static DIB dmas1_dib = {
    &dmasio,                                    /* device interface */
    DMALT1,                                     /* select code */
    ch1                                         /* card index */
    };

static DIB dmap2_dib = {
    &dmapio,                                    /* device interface */
    DMA2,                                       /* select code */
    ch2                                         /* card index */
    };

static DIB dmas2_dib = {
    &dmasio,                                    /* device interface */
    DMALT2,                                     /* select code */
    ch2                                         /* card index */
    };


/* Unit lists */

static UNIT dma1_unit = { UDATA (NULL, 0, 0) };

static UNIT dma2_unit = { UDATA (NULL, 0, 0) };


/* Register lists */

static REG dma1_reg [] = {
/*    Macro   Name     Location            Width  Flags */
/*    ------  -------  ------------------  -----  ----- */
    { FLDATA (XFR,     dma [ch1].xferen,     0)         },
    { FLDATA (CTL,     dma [ch1].control,    0)         },
    { FLDATA (FLG,     dma [ch1].flag,       0)         },
    { FLDATA (FBF,     dma [ch1].flagbuf,    0)         },
    { FLDATA (CTL2,    dma [ch1].select,     0)         },
    { ORDATA (CW1,     dma [ch1].cw1,       16)         },
    { ORDATA (CW2,     dma [ch1].cw2,       16)         },
    { ORDATA (CW3,     dma [ch1].cw3,       16)         },
    { FLDATA (BYTE,    dma [ch1].packer,    31)         },
    { ORDATA (PACKER,  dma [ch1].packer,     8),  REG_A },
    { NULL }
    };

static REG dma2_reg [] = {
/*    Macro   Name     Location            Width  Flags */
/*    ------  -------  ------------------  -----  ----- */
    { FLDATA (XFR,     dma [ch2].xferen,     0)         },
    { FLDATA (CTL,     dma [ch2].control,    0)         },
    { FLDATA (FLG,     dma [ch2].flag,       0)         },
    { FLDATA (FBF,     dma [ch2].flagbuf,    0)         },
    { FLDATA (CTL2,    dma [ch2].select,     0)         },
    { ORDATA (CW1,     dma [ch2].cw1,       16)         },
    { ORDATA (CW2,     dma [ch2].cw2,       16)         },
    { ORDATA (CW3,     dma [ch2].cw3,       16)         },
    { FLDATA (BYTE,    dma [ch2].packer,    31)         },
    { ORDATA (PACKER,  dma [ch2].packer,     8),  REG_A },
    { NULL }
    };


/* Debugging trace list */

static DEBTAB dma_deb [] = {
    { "CMD",   TRACE_CMD   },                   /* trace interface or controller commands */
    { "CSRW",  TRACE_CSRW  },                   /* trace interface control, status, read, and write actions */
    { "SR",    TRACE_SR    },                   /* trace service requests received */
    { "DATA",  TRACE_DATA  },                   /* trace memory data accesses */
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0 }
    };


/* Device descriptors */

DEVICE dma1_dev = {
    "DMA1",                                     /* device name */
    &dma1_unit,                                 /* unit array */
    dma1_reg,                                   /* register array */
    NULL,                                       /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    1,                                          /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &dma_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &dmap1_dib,                                 /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    dma_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

DEVICE dma2_dev = {
    "DMA2",                                     /* device name */
    &dma2_unit,                                 /* unit array */
    dma2_reg,                                   /* register array */
    NULL,                                       /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    1,                                          /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &dma_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &dmap2_dib,                                 /* device information block pointer */
    DEV_DISABLE | DEV_DEBUG,                    /* device flags */
    0,                                          /* debug control flags */
    dma_deb,                                    /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };

static DEVICE *dma_dptrs [] = {
    &dma1_dev,
    &dma2_dev
    };



/* Memory Protect program constants */

#define UNIT_V_MP_JSB   (UNIT_V_UF + 0)         /* MP jumper W5 */
#define UNIT_V_MP_INT   (UNIT_V_UF + 1)         /* MP jumper W6 */
#define UNIT_V_MP_SEL1  (UNIT_V_UF + 2)         /* MP jumper W7 */
#define UNIT_MP_JSB     (1 << UNIT_V_MP_JSB)    /* 1 = W5 is out */
#define UNIT_MP_INT     (1 << UNIT_V_MP_INT)    /* 1 = W6 is out */
#define UNIT_MP_SEL1    (1 << UNIT_V_MP_SEL1)   /* 1 = W7 is out */

#define MP_TEST(va)     (mp_control && ((va) >= 2) && ((va) < mp_fence))


/* Memory Protect global state */

FLIP_FLOP mp_control = CLEAR;                   /* MP control flip-flop */
FLIP_FLOP mp_mevff   = CLEAR;                   /* memory expansion violation flip-flop */
HP_WORD   mp_fence   = 0;                       /* MP fence register  */
HP_WORD   mp_viol    = 0;                       /* MP violation register */
HP_WORD   iop_sp     = 0;                       /* iop stack reg */
HP_WORD   err_PC     = 0;                       /* error PC */

jmp_buf   save_env;                             /* MP abort handler */
t_bool    mp_mem_changed;                       /* TRUE if the MP or MEM registers have been altered */


/* Memory Protect local state */

static FLIP_FLOP mp_flag        = CLEAR;        /* MP flag flip-flop */
static FLIP_FLOP mp_flagbuf     = CLEAR;        /* MP flag buffer flip-flop */
static FLIP_FLOP mp_evrff       = SET;          /* enable violation register flip-flop */
static char      meu_indicator;                 /* last map access indicator (S | U | A | B | -) */
static uint32    meu_page;                      /* last physical page number accessed */


/* Memory Protect local SCP support routine declarations */

static IOHANDLER protio;
static t_stat    mp_reset (DEVICE *dptr);


/* Memory Protect SCP data structures */


/* Device information block */

static DIB mp_dib = {
    &protio,                                    /*   device interface */
    PRO,                                        /*   select code */
    0                                           /*   card index */
    };


/* Unit list.


   Implementation notes:

    1. The default flags correspond to the following jumper settings: JSB in,
       INT in, SEL1 out.
*/

static UNIT mp_unit = { UDATA (NULL, UNIT_MP_SEL1, 0) };


/* Register list */

static REG mp_reg [] = {
/*    Macro   Name  Location     Width */
/*    ------  ----  -----------  ----- */
    { FLDATA (CTL,  mp_control,    0)  },
    { FLDATA (FLG,  mp_flag,       0)  },
    { FLDATA (FBF,  mp_flagbuf,    0)  },
    { ORDATA (FR,   mp_fence,     15)  },
    { ORDATA (VR,   mp_viol,      16)  },
    { FLDATA (EVR,  mp_evrff,      0)  },
    { FLDATA (MEV,  mp_mevff,      0)  },
    { NULL }
    };


/* Modifier list */

static MTAB mp_mod [] = {
/*    Mask Value     Match Value   Print String     Match String  Validation  Display  Descriptor */
/*    -------------  ------------  ---------------  ------------  ----------  -------  ---------- */
    { UNIT_MP_JSB,   UNIT_MP_JSB,  "JSB (W5) out",  "JSBOUT",     NULL,       NULL,    NULL       },
    { UNIT_MP_JSB,   0,            "JSB (W5) in",   "JSBIN",      NULL,       NULL,    NULL       },
    { UNIT_MP_INT,   UNIT_MP_INT,  "INT (W6) out",  "INTOUT",     NULL,       NULL,    NULL       },
    { UNIT_MP_INT,   0,            "INT (W6) in",   "INTIN",      NULL,       NULL,    NULL       },
    { UNIT_MP_SEL1,  UNIT_MP_SEL1, "SEL1 (W7) out", "SEL1OUT",    NULL,       NULL,    NULL       },
    { UNIT_MP_SEL1,  0,            "SEL1 (W7) in",  "SEL1IN",     NULL,       NULL,    NULL       },
    { 0 }
    };


/* Device descriptor */

DEVICE mp_dev = {
    "MP",                                       /* device name */
    &mp_unit,                                   /* unit array */
    mp_reg,                                     /* register array */
    mp_mod,                                     /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    1,                                          /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &mp_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &mp_dib,                                    /* device information block pointer */
    DEV_DISABLE | DEV_DIS,                      /* device flags */
    0,                                          /* debug control flags */
    NULL,                                       /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* I/O system program constants */

static const BITSET_NAME inbound_names [] = {   /* Inbound signal names, in IOSIGNAL order */
    "PON",                                      /*   000000000001 */
    "ENF",                                      /*   000000000002 */
    "IOI",                                      /*   000000000004 */
    "IOO",                                      /*   000000000010 */
    "SFS",                                      /*   000000000020 */
    "SFC",                                      /*   000000000040 */
    "STC",                                      /*   000000000100 */
    "CLC",                                      /*   000000000200 */
    "STF",                                      /*   000000000400 */
    "CLF",                                      /*   000000001000 */
    "EDT",                                      /*   000000002000 */
    "CRS",                                      /*   000000004000 */
    "POPIO",                                    /*   000000010000 */
    "IAK",                                      /*   000000020000 */
    "SIR"                                       /*   000000040000 */
    };

static const BITSET_FORMAT inbound_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (inbound_names, 0, lsb_first, no_alt, no_bar) };


static const BITSET_NAME outbound_names [] = {  /* Outbound signal names, in IOSIGNAL order */
    "SKF"                                       /*   000000200000 */
    };

static const BITSET_FORMAT outbound_format =    /* names, offset, direction, alternates, bar */
    { FMT_INIT (outbound_names, 16, lsb_first, no_alt, no_bar) };


/* I/O instruction sub-opcodes */

#define soHLT           0                       /* halt */
#define soFLG           1                       /* set/clear flag */
#define soSFC           2                       /* skip on flag clear */
#define soSFS           3                       /* skip on flag set */
#define soMIX           4                       /* merge into A/B */
#define soLIX           5                       /* load into A/B */
#define soOTX           6                       /* output from A/B */
#define soCTL           7                       /* set/clear control */


/* I/O system local data structures */

static DIB *dibs [MAXDEV + 1] = {               /* index by select code for I/O instruction dispatch */
    &cpu_dib,                                   /*   select code 00 = interrupt system */
    &ovfl_dib                                   /*   select code 01 = overflow register */
    };

static DEVICE *devs [MAXDEV + 1] = {            /* index by select code for I/O dispatch tracing */
    &cpu_dev,                                   /*   select code 00 = interrupt system */
    &cpu_dev                                    /*   select code 01 = overflow register */
    };


/* I/O system local utility routine declarations */

static void   io_initialize (void);
static uint32 io_dispatch   (uint32 select_code, IOCYCLE signal_set, HP_WORD data);



/* CPU global SCP support routines */


/* Execute CPU instructions.

   This is the instruction decode routine for the HP 21xx/1000 simulator.  It is
   called from the simulator control program (SCP) to execute instructions in
   simulated memory, starting at the simulated program counter.  It runs until
   the status to be returned is set to a value other than SCPE_OK.

   On entry, P points to the instruction to execute, and the "sim_switches"
   global contains any command-line switches included with the run command.  On
   exit, P points at the next instruction to execute.

   Execution is divided into four phases.

   First, the instruction prelude configures the simulation state to resume
   execution.  This involves verifying that there are no device conflicts (e.g.,
   two devices with the same select code) and initializing the I/O state.  These
   actions accommodate reconfiguration of the I/O device settings and program
   counter while the simulator was stopped.  The prelude also picks up the
   time-base generator's select code for use in idle testing, and it checks for
   one command-line switch: if "-B" is specified, the current set of simulation
   stop conditions is bypassed for the first instruction executed.

   Second, the memory protect abort mechanism is set up.  MP aborts utilize the
   "setjmp/longjmp" mechanism to transfer control out of the instruction
   executors without returning through the call stack.  This allows an
   instruction to be aborted part-way through execution when continuation is
   impossible due to a memory access violation.

   Third, the instruction execution loop decodes instructions and calls the
   individual executors in turn until a condition occurs that prevents further
   execution.  Examples of such conditions include execution of a HLT
   instruction, a user stop request (CTRL+E) from the simulation console, a
   recoverable device error (such as an improperly formatted tape image), a
   user-specified breakpoint, and a simulation stop condition (such as execution
   of an unimplemented instruction).  The execution loop also polls for I/O
   events and device interrupts, and runs DMA channel cycles.  During
   instruction execution, the IR register contains the currently executing
   instruction, and the P register points to the memory location containing the
   next instruction.

   Fourth, the instruction postlude updates the simulation state in preparation
   for returning to the SCP command prompt.  Devices that maintain an internal
   state different from their external state, such as the MEM status and
   violation registers, are updated so that their internal and external states
   are fully consistent.  This ensures that the state visible to the user during
   the simulation stop is correct.  It also ensures that the program counter
   points correctly at the next instruction to execute upon resumption.


   In hardware, if the Memory Protect accessory is installed and enabled, I/O
   operations to select codes other than 01 are prohibited.  Also, in
   combination with the MPCK micro-order, MP validates the M-register contents
   (memory address) against the memory protect fence.  If a violation occurs, an
   I/O instruction or memory write is inhibited, and a memory read returns
   invalid data.

   In simulation, an instruction executor that detects an MP violation calls the
   MP_ABORT macro, passing the violation address as the parameter.  This
   executes a "longjmp" to the abort handler, which is outside of and precedes
   the instruction execution loop.  The value passed to "longjmp" is a 32-bit
   integer containing the logical address of the instruction causing the
   violation.  MP_ABORT should only be called if "mp_control" is SET, as aborts
   do not occur if MP is turned off.

   An MP interrupt (SC 05) is qualified by "ion" but not by "ion_defer".  If the
   interrupt system is off when an MP violation is detected, the violating
   instruction will be aborted, even though no interrupt occurs.  In this case,
   neither the flag nor the flag buffer are set, and EVR is not cleared.


   The instruction execution loop starts by checking for event timer expiration.
   If one occurs, the associated event service routine is called, and if it was
   successful, the DMA service requests and interrupt requests are recalculated.

   DMA cycles are requested by an I/O card asserting its SRQ signal.  If a DMA
   channel is programmed to respond to that card's select code, a DMA cycle will
   be initiated.  A DMA cycle consists of a memory cycle and an I/O cycle.
   These cycles are synchronized with the control processor on the 21xx CPUs. On
   the 1000s, memory cycles are asynchronous, while I/O cycles are synchronous.
   Memory cycle time is about 40% of the I/O cycle time.

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

   At instruction fetch time, a pending interrupt request will be deferred if
   the previous instruction was a JMP indirect, JSB indirect, STC, CLC, STF,
   CLF, or was executing from an interrupt trap cell. In addition, the following
   instructions will cause deferral on the 1000 series: SFS, SFC, JRS, DJP, DJS,
   SJP, SJS, UJP, and UJS.

   On the HP 1000, the request is always deferred until after the current
   instruction completes.  On the 21xx, the request is deferred unless the
   current instruction is an MRG instruction other than JMP or JMP,I or JSB,I.
   Note that for the 21xx, SFS and SFC are not included in the deferral
   criteria.


   When a status other than SCPE_OK is returned from an instruction executor or
   event service routine, the instruction execution loop exits into the
   instruction postlude.  The set of debug trace flags is restored if it had
   been changed by an active execution trace or idle trace suppression.  This
   ensures that the simulation stop does not exit with the flags set improperly.
   If the simulation stopped for a programmed halt, the 21xx binary loader area
   is protected in case it had been unprotected to run the loader.  The DMS
   status and violation registers and the program counter queue pointer are
   updated to present the proper values to the user interface.  The default
   breakpoint type is updated to reflect the current MEU state (disabled, system
   map enabled, or user map enabled).  Finally, the P register is reset if the
   current instruction is to be reexecuted on reentry (for example, on an
   unimplemented instruction stop).


   Implementation notes:

    1. While the Microsoft VC++ "setjmp" documentation says, "All variables
       (except register variables) accessible to the routine receiving control
       contain the values they had when longjmp was called," the ISO C99
       standard says, "All accessible objects have values...as of the time the
       longjmp function was called, except that the values of objects of
       automatic storage duration that are local to the function containing the
       invocation of the corresponding setjmp macro that do not have
       volatile-qualified type and have been changed between the setjmp
       invocation and longjmp call are indeterminate."

       Therefore, the "exec_save" and "idle_save" variables are marked static
       to ensure that they are reloaded after a longjmp caused by a memory
       protect abort (they are not marked volatile to save redundant reloads
       within the instruction execution loop).  Also, "status" and "exec_test"
       are set before reentering the instruction loop after an abort.  This is
       done solely to reassure the compiler that the values are not clobbered,
       even though in both cases the values are reestablished after an abort
       before they are used.

    2. The protected lower bound address for the JSB instruction depends on the
       W5 jumper setting.  If W5 is in, then the lower bound is 2, allowing JSBs
       to the A and B registers.  If W5 is out, then the lower bound is 0, just
       as with JMP.  The protected lower bound is set during the instruction
       prelude and tested during JSB address validation.

    3. The -P switch is removed from the set of command line switches to ensure
       that internal calls to the device reset routines are not interpreted as
       "power-on" resets.

    4. The "longjmp" handler is used both for MP and MEM violations.  The MEV
       flip-flop will be clear for the former and set for the latter.  The MEV
       violation register will be updated by "dms_upd_vr" only if the call is
       NOT for an MEM violation; if it is, then the register has already been
       set and should not be disturbed.

    5. For an MP/MEM abort, the violation address is passed via "longjmp" to
       enable the MEM violation register to be updated.  The "longjmp" routine
       will not pass a value of 0; it is converted internally to 1.  This is OK,
       because only the page number of the address value is used, and locations
       0 and 1 are both on page 0.

    6. A CPU freeze is simulated by skipping instruction execution during the
       current loop cycle.

    7. If both DMA channels have SRQ asserted, priority is simulated by skipping
       the channel 2 cycle if channel 1's SRQ is still asserted at the end of
       its cycle.  If it is not, then channel 2 steals the next cycle from the
       CPU.

    8. The 1000 M-Series allows some CPU processing concurrently with
       continuous DMA cycles, whereas all other CPUs freeze.  The processor
       freezes if an I/O cycle is attempted, including an interrupt
       acknowledgement.  Because some microcode extensions (e.g., Access IOP,
       RTE-6/VM OS) perform I/O cycles, advance detection of I/O cycles is
       difficult.  Therefore, we freeze all processing for the M-Series as well.

    9. EXEC tracing is active when exec_save is non-zero.  "exec_save" saves the
       current state of the trace flags when an EXEC trace match occurs.  For
       this to happen, at least TRACE_EXEC must be set, so "exec_save" will be
       set non-zero when a match is active.

   10. The execution trace (TRACE_EXEC) match test is performed in two parts to
       display the register values both before and after the instruction
       execution.  Consequently, the enable test is done before the register
       trace, and the disable test is done after.

   11. A simulation stop bypass is inactivated after the first instruction
       execution by the expedient of setting the stop inhibition mask to the
       execution status result.  This must be SCPE_OK (i.e., zero) for execution
       to continue, which removes the stop inhibition.  If a non-zero status
       value is returned, then the inhibition mask will be set improperly, but
       that is irrelevant, as execution will stop in this case.
*/

t_stat sim_instr (void)
{
static const char *const register_values [] = {         /* register values, indexed by EOI concatenation */
    "e o i",
    "e o I",
    "e O i",
    "e O I",
    "E o i",
    "E o I",
    "E O i",
    "E O I"
    };

static const char mp_value [] = {                       /* memory protection value, indexed by mp_control */
    '-',
    'P'
    };

static const char *const register_formats [] = {        /* CPU register formats, indexed by is_1000 */
    REGA_FORMAT "  A %06o, B %06o, ",                   /*   is_1000 = FALSE format */
    REGA_FORMAT "  A %06o, B %06o, X %06o, Y %06o, "    /*   is_1000 = TRUE  format */
    };

static const char *const mp_mem_formats [] = {                  /* MP/MEM register formats, indexed by is_1000 */
    REGB_FORMAT "  MPF %06o, MPV %06o\n",                       /*   is_1000 = FALSE format */
    REGB_FORMAT "  MPF %06o, MPV %06o, MES %06o, MEV %06o\n"    /*   is_1000 = TRUE  format */
    };

static uint32 exec_save;                                /* the trace flag settings saved by an EXEC match */
static uint32 idle_save;                                /* the trace flag settings saved by an idle match */
DEVICE *tbg_dptr;
int    abortval;
uint32 intrq, dmarq;                                    /* set after setjmp */
t_bool exec_test;                                       /* set after setjmp */
t_bool iotrap;                                          /* set after setjmp */
t_stat status;                                          /* set after setjmp */


/* Instruction prelude */

if (sim_switches & SWMASK ('B'))                        /* if a simulation stop bypass was requested */
    cpu_ss_inhibit = SS_INHIBIT;                        /*   then inhibit stops for the first instruction */
else                                                    /* otherwise */
    cpu_ss_inhibit = SCPE_OK;                           /*   clear the inhibition mask */

sim_switches &= ~SWMASK ('P');                          /* clear the power-on switch to prevent interference */

if (hp_device_conflict ())                              /* if device assignment is inconsistent */
    return SCPE_STOP;                                   /*   then inhibit execution */

tbg_dptr = find_dev ("CLK");                            /* get a pointer to the time-base generator device */

if (tbg_dptr == NULL)                                           /* if the TBG device is not present */
    return SCPE_IERR;                                           /*   then something is seriously wrong */
else                                                            /* otherwise */
    tbg_select_code = ((DIB *) tbg_dptr->ctxt)->select_code;    /*   get the select code from the device's DIB */

io_initialize ();                                       /* set up the I/O data structures */
cpu_ioerr_uptr = NULL;                                  /*   and clear the I/O error unit pointer */

exec_save = 0;                                          /* clear the EXEC match */
idle_save = 0;                                          /*   and idle match trace flags */

jsb_plb = (mp_unit.flags & UNIT_MP_JSB) ? 0 : 2;        /* set the protected lower bound for JSB */

mp_mem_changed = TRUE;                                  /* request an initial MP/MEM trace */


/* Memory Protect abort processor */

abortval = setjmp (save_env);                           /* set abort hdlr */

if (abortval) {                                         /* memory protect abort? */
    dms_upd_vr (abortval);                              /* update violation register (if not MEV) */

    if (ion)                                            /* interrupt system on? */
        protio (dibs [PRO], ioENF, 0);                  /* set flag */
    }

dmarq = calc_dma ();                                    /* initial recalc of DMA masks */
intrq = calc_int ();                                    /* initial recalc of interrupts */

status = SCPE_OK;                                       /* clear the status */
exec_test = FALSE;                                      /*   and the execution test flag */


/* Instruction execution loop */

do {                                                    /* execute instructions until halted */
    err_PC = PR;                                        /* save P for error recovery */

    if (sim_interval <= 0) {                            /* event timeout? */
        status = sim_process_event ();                  /* process event service */

        if (status != SCPE_OK)                          /* service failed? */
            break;                                      /* stop execution */

        dmarq = calc_dma ();                            /* recalc DMA reqs */
        intrq = calc_int ();                            /* recalc interrupts */
        }


    if (dmarq) {                                        /* if a DMA service request is pending */
        if (dmarq & DMA_1_REQ) {                        /*   then if the request is for channel 1 */
            status = dma_cycle (ch1, DMA_Channel_1);    /*     then do one DMA cycle using the port A map */

            if (status == SCPE_OK)                      /* cycle OK? */
                dmarq = calc_dma ();                    /* recalc DMA requests */
            else
                break;                                  /* cycle failed, so stop */
            }

        if ((dmarq & (DMA_1_REQ | DMA_2_REQ)) == DMA_2_REQ) {   /* DMA channel 1 idle and channel 2 request? */
            status = dma_cycle (ch2, DMA_Channel_2);            /* do one DMA cycle using port B map */

            if (status == SCPE_OK)                      /* cycle OK? */
                dmarq = calc_dma ();                    /* recalc DMA requests */
            else
                break;                                  /* cycle failed, so stop */
            }

        if (dmarq)                                      /* DMA request still pending? */
            continue;                                   /* service it before instruction execution */

        intrq = calc_int ();                            /* recalc interrupts */
        }

    if (intrq && ion_defer)                             /* if an interrupt is pending but deferred */
        ion_defer = check_deferral (intrq);             /*   then check that the deferral is applicable */


    if (intrq && !ion_defer) {                          /* if an interrupt request is pending and not deferred */
        if (sim_brk_summ &&                             /* any breakpoints? */
            sim_brk_test (intrq, SWMASK ('E') |         /* unconditional or right type for DMS? */
              (dms_enb ? SWMASK ('S') : SWMASK ('N')))) {
            status = STOP_BRKPNT;                       /* stop simulation */
            break;
            }

        CIR = (HP_WORD) intrq;                          /* save int addr in CIR */
        intrq = 0;                                      /* clear request */
        ion_defer = TRUE;                               /* defer interrupts */
        iotrap = TRUE;                                  /* mark as I/O trap cell instr */

        if (idle_save != 0) {                           /* if idle loop tracing is suppressed */
            cpu_dev.dctrl = idle_save;                  /*   then restore the saved trace flag set */
            idle_save = 0;                              /*     and indicate that we are out of the idle loop */
            }

        if (TRACING (cpu_dev, TRACE_INSTR)) {
            meu_map (PR, dms_ump, NOPROT);              /* reset the indicator and page */

            tprintf (cpu_dev, cpu_dev.dctrl,
                     DMS_FORMAT "interrupt\n",
                     meu_indicator, meu_page,
                     PR, CIR);
            }

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

        mp_mem_changed = TRUE;                          /* set the MP/MEM registers changed flag */

        IR = ReadF (CIR);                               /* get trap cell instruction */

        io_dispatch (CIR, ioIAK, IR);                   /* acknowledge interrupt */

        if (CIR != PRO)                                 /* not MP interrupt? */
            protio (dibs [CIR], ioIAK, IR);             /* send IAK for device to MP too */
        }

    else {                                              /* normal instruction */
        iotrap = FALSE;                                 /* not a trap cell instruction */

        if (sim_brk_summ &&                             /* any breakpoints? */
            sim_brk_test (PR, SWMASK ('E') |            /* unconditional or */
                              (dms_enb ?                /*   correct type for DMS state? */
                                (dms_ump ?
                                  SWMASK ('U') : SWMASK ('S')) :
                                SWMASK ('N')))) {
            status = STOP_BRKPNT;                       /* stop simulation */
            break;
            }

        if (mp_evrff)                                   /* violation register enabled */
            mp_viol = PR;                               /* update with current P */

        IR = ReadF (PR);                                /* fetch instr */
        PR = (PR + 1) & VAMASK;
        ion_defer = FALSE;
        }


    if (TRACING (cpu_dev, TRACE_EXEC | TRACE_REG)) {    /* if execution or register tracing is enabled */
        if (cpu_dev.dctrl & TRACE_EXEC)                 /*   then if tracing execution */
            exec_test = (IR & exec_mask) == exec_match; /*     then the execution test succeeds if */
                                                        /*       the next instruction matches the test criteria */

        if (cpu_dev.dctrl & TRACE_EXEC                  /* if execution tracing is enabled */
          && exec_save == 0                             /*   and is currently inactive */
          && exec_test) {                               /*     and the matching test succeeds */
            exec_save = cpu_dev.dctrl;                  /*       then save the current trace flag set */
            cpu_dev.dctrl |= TRACE_ALL;                 /*         and turn on full tracing */
            }

        if (cpu_dev.dctrl & TRACE_REG) {                /* if register tracing is enabled */
            hp_trace (&cpu_dev, TRACE_REG,              /*   then output the working registers */
                      register_formats [is_1000],
                      mp_value [mp_control],
                      dms_sr & MST_FENCE,
                      SR, AR, BR, XR, YR);

            fputs (register_values [E << 2 | O << 1 | ion], sim_deb);
            fputc ('\n', sim_deb);

            if (mp_mem_changed) {                       /* if the MP/MEM registers have been altered */
                hp_trace (&cpu_dev, TRACE_REG,          /*   then output the register values */
                          mp_mem_formats [is_1000],
                          mp_value [mp_control],
                          mp_fence, mp_viol, dms_sr, dms_vr);

                mp_mem_changed = FALSE;                 /* clear the MP/MEM registers changed flag */
                }
            }

        if (cpu_dev.dctrl & TRACE_EXEC                          /* if execution tracing is enabled */
          && exec_save != 0                                     /*   and is currently active */
          && ! exec_test) {                                     /*     and the matching test fails */
            cpu_dev.dctrl = exec_save;                          /*       then restore the saved debug flag set */
            exec_save = 0;                                      /*         and indicate that tracing is disabled */

            hp_trace (&cpu_dev, TRACE_EXEC, EXEC_FORMAT "\n");  /* add a separator to the trace log */
            }
        }

    if (TRACING (cpu_dev, TRACE_INSTR)) {               /* if instruction tracing is enabled */
        hp_trace (&cpu_dev, TRACE_INSTR,                /*   then output the address and opcode */
                  DMS_FORMAT,
                  meu_indicator, meu_page,
                  MR, IR);

        sim_eval [0] = IR;                              /* save the (first) instruction word in the eval array */

        if (fprint_cpu (sim_deb, MR, sim_eval, 0, CPU_Trace) > SCPE_OK) /* print the mnemonic; if that fails */
            fprint_val (sim_deb, sim_eval [0], cpu_dev.dradix,          /*   then print the numeric */
                        cpu_dev.dwidth, PV_RZRO);                       /*     value again */

        fputc ('\n', sim_deb);                          /* end the trace with a newline */
        }


    sim_interval = sim_interval - 1;                    /* count the instruction */

    status = machine_instruction (IR, iotrap, intrq,    /* execute one machine instruction */
                                  &idle_save);

    if (status == NOTE_IOG) {                           /* I/O instr exec? */
        dmarq = calc_dma ();                            /* recalc DMA masks */
        intrq = calc_int ();                            /* recalc interrupts */
        status = SCPE_OK;                               /* continue */
        }

    else if (status == NOTE_INDINT) {                   /* intr pend during indir? */
        PR = err_PC;                                    /* back out of inst */
        status = SCPE_OK;                               /* continue */
        }

    cpu_ss_inhibit = status;                            /* clear the simulation stop inhibition mask */
    }
while (status == SCPE_OK);                              /* loop until halted */


/* Instruction postlude */

if (intrq && ion_defer)                                 /* if an interrupt is pending but deferred */
    ion_defer = check_deferral (intrq);                 /*   then check that the deferral is applicable */

if (exec_save != 0) {                                   /* if EXEC tracing is active */
    cpu_dev.dctrl = exec_save;                          /*   then restore the saved trace flag set */
    hp_trace (&cpu_dev, TRACE_EXEC, EXEC_FORMAT "\n");  /*     and add a separator to the trace log */
    }

else if (idle_save != 0)                                /* otherwise if idle tracing is suppressed */
    cpu_dev.dctrl = idle_save;                          /*   then restore the saved trace flag set */

saved_MR = MR;                                          /* save for T cmd update */

if (status == STOP_HALT)                                /* programmed halt? */
    set_loader (NULL, FALSE, NULL, NULL);               /* disable loader (after T is read) */
else if (status <= STOP_RERUN)                          /* simulation stop */
    PR = err_PC;                                        /* back out instruction */

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

tprintf (cpu_dev, cpu_dev.dctrl,
         DMS_FORMAT "simulation stop: %s\n",
         meu_indicator, meu_page,
         MR, TR, sim_error_text (status));

return status;                                          /* return status code */
}


/* VM command post-processor

   Update T register to contents of memory addressed by M register.


   Implementation notes:

    1. The T register must be changed only when M has changed.  Otherwise, if T
       is updated after every command, then T will be set to zero if M points
       into the protected loader area of the 21xx machines, e.g., after a HLT
       instruction in the loader reenables loader protection.
*/

void cpu_post_cmd (t_bool from_scp)
{
if (MR != saved_MR) {                                   /* M changed since last update? */
    saved_MR = MR;
    TR = mem_fast_read (MR, dms_ump);                   /* sync T with new M */
    }
return;
}



/* CPU global utility routines */


/* Install a bootstrap loader into memory.

   This routine copies the bootstrap loader specified by "boot" into the last 64
   words of main memory, limited by a 32K memory size.  If "sc" contains the
   select code of an I/O interface (i.e., select code 10 or above), this routine
   will configure the I/O instructions in the loader to the supplied select
   code.  On exit, P will be set to point at the loader starting program
   address, and S will be altered as directed by the "sr_clear" and "sr_set"
   masks if the current CPU is a 1000.

   The currently configured CPU family (21xx or 1000) determines which of two
   BOOT_LOADER structures is accessed from the "boot" array.  Each structure
   contains the 64-word loader array and three indicies into the loader
   array that specify the start of program execution, the element containing the
   DMA control word, and the element containing the (negative) address of the
   first loader word in memory.

   21xx-series loaders consist of subsections handling one or two devices.  A
   two-part loader is indicated by a starting program index other than 0, i.e.,
   other than the beginning of the loader.  An example is the Basic Moving-Head
   Disc Loader (BMDL), which consists of a paper tape loader section starting at
   index 0 and a disc loader section starting at index 50 octal.  For these
   loaders, I/O configuration depends on the "start_index" field of the selected
   BOOTSTRAP structure: I/O instructions before the starting index are
   configured to the current paper-tape reader select code, and instructions at
   or after the starting index are configured to the device select code
   specified by "sc".  Single-part loaders specify a starting index of 0, and
   all I/O instructions are configured to the "sc" select code.

   1000-series loaders are always single part and always start at index 0, so
   they are always configured to use the "sc" select code.

   If a given device does not have both a 21xx-series and a 1000-series loader,
   the "start_index" field of the undefined loader will be set to the "IBL_NA"
   value.  If this routine is called to copy an undefined loader, it will reject
   the call with a "Command not allowed" error.

   If I/O configuration is requested, each instruction in the loader array is
   examined as it is copied to memory.  If the instruction is a non-HLT I/O
   instruction referencing a select code >= 10, the select code will be reset by
   subtracting 10 and adding the value of the select code supplied by the "sc"
   parameter (or the paper-tape reader select code, as above).  This permits
   configuration of loaders that address two- or three-card interfaces.  Passing
   an "sc" value of 0 will inhibit configuration, and the loader array will be
   copied verbatim.

   As an example, passing an "sc" value of 24 octal will alter these I/O-group
   instructions as follows:

        Loader    Configured
     Instruction  Instruction  Note
     -----------  -----------  ------------------------------
       OTA 10       OTA 24     Normal configuration
       LIA 11       LIA 25     Second card configuration
       STC  6       STC  6     DCPC configuration not changed
       HLT 11       HLT 11     Halt instruction not changed

   If configuration is performed, two additional operations may be performed.
   First, the routine will alter the word at the index specified by the
   "dma_index" field of the selected BOOTSTRAP structure unconditionally as
   above.  This word is assumed to contain a DMA control word; it is configured
   to reference the supplied select code.  Second, it will set the word at the
   index specified by the "fwa_index" field to the two's-complement of the
   starting address of the loader in memory.  This value may be used by the
   loader to check that it will not be overwritten by loaded data.

   If either field is set to the IBL_NA value, then the corresponding
   modification is not made.  For example, the 21xx Basic Binary Loader (BBL)
   does not use DMA, so its "dma_index" field is set to IBL_NA, and so no DMA
   control word modification is done.

   This routine also unconditionally sets the P register to the starting
   address for loader execution.  This is derived from the "start_index" field
   and the starting memory address to which the loader is copied.

   Finally, if the current CPU is a 1000-series machine, the S register bits
   corresponding to those set in the "sr_clear" value are masked off, and the
   bits corresponding to those in the "sr_set" value are set.  In addition, the
   select code from the "sc" value is shifted left and ORed into the value.
   This action presets the S-register to the correct value for the selected
   loader.


   Implementation notes:

    1. The paper-tape reader's select code is determined on each entry to the
       routine to accommodate select code reassignment by the user.
*/

t_stat cpu_copy_loader (const LOADER_ARRAY boot, uint32 sc, HP_WORD sr_clear, HP_WORD sr_set)
{
uint32      index, base, ptr_sc;
MEMORY_WORD word;
DEVICE      *ptr_dptr;

if (boot [is_1000].start_index == IBL_NA)               /* if the bootstrap is not defined for the current CPU */
    return SCPE_NOFNC;                                  /*   then reject the command */

else if (boot [is_1000].start_index > 0 && sc > 0) {    /* if this is a two-part loader with I/O reconfiguration */
    ptr_dptr = find_dev ("PTR");                        /*   then get a pointer to the paper tape reader device */

    if (ptr_dptr == NULL)                               /* if the PTR device is not present */
        return SCPE_IERR;                               /*   then something is seriously wrong */
    else                                                /* otherwise */
        ptr_sc = ((DIB *) ptr_dptr->ctxt)->select_code; /*   get the select code from the device's DIB */
    }

else                                                    /* otherwise this is a single-part loader */
    ptr_sc = 0;                                         /*   or I/O reconfiguration is not requested */

base = MEMSIZE - 1 & ~IBL_MASK & LA_MASK;               /* get the base memory address of the loader */
PR = base + boot [is_1000].start_index & R_MASK;        /*   and store the starting program address in P */

set_loader (NULL, TRUE, NULL, NULL);                    /* enable the loader (ignore errors if not 21xx) */

for (index = 0; index < IBL_SIZE; index++) {            /* copy the bootstrap loader to memory */
    word = boot [is_1000].loader [index];               /* get the next word */

    if (sc == 0)                                        /* if reconfiguration is not requested */
        M [base + index] = word;                        /*   then copy the instruction verbatim */

    else if ((word & I_NMRMASK) == I_IO                             /* otherwise if this is an I/O instruction */
      && (word & I_DEVMASK) >= VARDEV                               /*   and the referenced select code is >= 10B */
      && I_GETIOOP (word) != soHLT)                                 /*   and it's not a halt instruction */
        if (index < boot [is_1000].start_index)                     /*   then if this is a split loader */
            M [base + index] = word + (ptr_sc - VARDEV) & DV_MASK;  /*     then reconfigure the paper tape reader */
        else                                                        /*   otherwise */
            M [base + index] = word + (sc - VARDEV) & DV_MASK;      /*     reconfigure the target device */

    else if (index == boot [is_1000].dma_index)             /* otherwise if this is the DMA configuration word */
        M [base + index] = word + (sc - VARDEV) & DV_MASK;  /*   then reconfigure the target device */

    else if (index == boot [is_1000].fwa_index)         /* otherwise if this is the starting address word */
        M [base + index] = NEG16 (base);                /*   then set the negative starting address of the bootstrap */

    else                                                /* otherwise the word is not a special one */
        M [base + index] = word;                        /*   so simply copy it */
    }

if (is_1000)                                            /* if the CPU is a 1000 */
    SR = SR & sr_clear | sr_set | IBL_TO_SC (sc);       /*   then modify the S register as indicated */

return SCPE_OK;                                         /* return success with the loader copied to memory */
}


/* Execute an I/O instruction.

   If memory protect is enabled, and the instruction is not in a trap cell, then
   HLT instructions are illegal and will cause a memory protect violation.  If
   jumper W7 (SEL1) is in, then all other I/O instructions are legal; if W7 is
   out, then only I/O instructions to select code 1 are legal, and I/O to other
   select codes will cause a violation.

   If the instruction is allowed, then the I/O signal corresponding to the
   instruction is determined, and the state of the interrupt deferral flag is
   set.  The signal is then dispatched to the device simulator indicated by the
   target select code.  The return value is split into status and data values,
   with the latter containing the SKF signal state or data to be returned in the
   A or B registers.


   Implementation notes:

    1. If the H/C (hold/clear flag) bit is set, then the ioCLF signal is added
       to the base signal set derived from the I/O instruction.

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
       the I/O group instructions can change the interrupt priority chain).  We
       do this in preference to calling the recalculation routines directly, as
       some extended firmware instructions call this routine multiple times, and
       there is no point in recalculating until all calls are complete.

    6. The I/O dispatcher returns NOTE_SKIP if the interface asserted the SKF
       signal.  We must recalculate interrupts if the originating SFS or SFC
       instruction included the CLF signal (e.g., SFS 0,C).
*/

t_stat cpu_iog (HP_WORD IR, t_bool iotrap)
{
/* Translation for I/O subopcodes:            soHLT, soFLG, soSFC, soSFS, soMIX, soLIX, soOTX, soCTL */
static const IOSIGNAL generate_signal [] = { ioNONE, ioSTF, ioSFC, ioSFS, ioIOI, ioIOI, ioIOO, ioSTC };

const uint32 dev = IR & I_DEVMASK;                      /* device select code */
const uint32 sop = I_GETIOOP (IR);                      /* I/O subopcode */
const uint32 ab  = (IR & I_AB ? 1 : 0);                 /* A/B register selector */
uint32  ioreturn;
t_stat  iostat;
IOCYCLE signal_set;
HP_WORD iodata = (HP_WORD) ioNONE;                      /* initialize for SKF test */

if (mp_control && !iotrap                               /* if MP is enabled and the instruction is not in trap cell */
  && (sop == soHLT                                      /*   and it is a HLT */
  || dev != OVF && (mp_unit.flags & UNIT_MP_SEL1))) {   /*   or does not address SC 01 and SEL1 is out */
        if (sop == soLIX)                               /*     then an MP violation occurs; if it is an LIA/B */
            ABREG [ab] = 0;                             /*       then the register is written before the abort */

        MP_ABORT (err_PC);                              /* MP abort */
        }

signal_set = generate_signal [sop];                     /* generate I/O signal from instruction */
ion_defer = defer_tab [sop];                            /* defer depending on instruction */

if (sop == soOTX)                                       /* OTA/B instruction? */
    iodata = ABREG [ab];                                /* pass A/B register value */

else if (sop == soCTL && IR & I_CTL)                    /* CLC instruction? */
    signal_set = ioCLC;                                 /* change STC to CLC signal */

if (IR & I_HC)                                          /* if the H/C bit is set */
    if (sop == soFLG)                                   /*   then if the instruction is STF or CLF */
        signal_set = ioCLF;                             /*     then change the ioSTF signal to ioCLF */
    else                                                /*   otherwise it's a non-flag instruction */
        signal_set |= ioCLF;                            /*     so add ioCLF to the instruction-specific signal */

ioreturn = io_dispatch (dev, signal_set, iodata);       /* dispatch the I/O signals */

iostat = IOSTATUS (ioreturn);                           /* extract status */
iodata = IODATA (ioreturn);                             /* extract return data value */

if (iostat == NOTE_SKIP) {                              /* if the interface asserted SKF */
    PR = PR + 1 & LA_MASK;                              /*   then bump P to skip then next instruction */
    return (IR & I_HC ? NOTE_IOG : SCPE_OK);            /*     and request recalculation of interrupts if needed */
    }

else if (iostat == SCPE_OK) {                           /* otherwise if instruction execution succeeded */
    if (sop == soLIX)                                   /*   then if is it an LIA or LIB */
        ABREG [ab] = iodata;                            /*     then load the returned data */

    else if (sop == soMIX)                              /*   otherwise if it is an MIA or MIB */
        ABREG [ab] = ABREG [ab] | iodata;               /*     then merge the returned data */

    else if (sop == soHLT)                              /*   otherwise if it is a HLT */
        return STOP_HALT;                               /*     then stop the simulator */

    return NOTE_IOG;                                    /* request recalculation of interrupts */
    }

else                                                    /* otherwise the execution failed */
    return iostat;                                      /*   so return the failure status */
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
        if (req_grant [0] & LSB)                        /* grant this request? */
            return sc;                                  /* return this select code */
        else                                            /* not this one */
            req_grant [0] = req_grant [0] >> 1;         /* position next request */

else if (req_grant [1])                                 /* device in upper half */
    for (sc = 32; sc <= 63; sc++)                       /* determine interrupting select code */
        if (req_grant [1] & LSB)                        /* grant this request? */
            return sc;                                  /* return this select code */
        else                                            /* not this one */
            req_grant [1] = req_grant [1] >> 1;         /* position next request */

return 0;                                               /* no interrupt granted */
}


/* Resolve a indirect address.

   This routine resolves a supplied memory address into a direct address by
   following an indirect chain, if any.  On entry, "MA" contains the address to
   resolve, and "irq" is non-zero if an interrupt is currently pending.  On
   exit, the variable pointed to by "addr" is set to the direct address, and
   SCPE_OK is returned.  If an interrupt is pending and permitted, NOTE_INDINT
   is returned to abort the instruction, and the variable indicated by "addr" is
   unchanged.

   Logical memory addresses are 15 bits wide, providing direct access to a 32K
   logical address space.  Addresses may also be indirect, with bit 15 (the MSB)
   serving as the direct/indirect indicator.  An indirect address may point at
   either a direct or indirect address.  In the latter case, the chain is
   followed until a direct address is obtained.

   Indirect addressing has implications for interrupt handling.  Normally,
   interrupts are checked at each level of indirection, and if one is pending,
   the CPU will abort execution of the instruction and then service the
   interrupt.  On return from the interrupt handler, the instruction will be
   restarted.

   However, the JMP indirect and JSB indirect instructions hold off interrupts
   until completion of the instruction, including complete resolution of the
   indirect chain.  If the chain is unresolvable (i.e., it points to itself, as
   in the instruction sequence JMP *+1,I and DEF *,I), then interrupts are held
   off forever.

   To prevent a user program from freezing a protected OS with an infinite
   indirect chain, and to permit real-time interrupts to be handled while
   resolving a long indirect chain, the Memory Protect accessory counts indirect
   levels during address resolution and will reenable interrupt recognition
   after the third level.  Operating systems that run without MP installed are
   subject to freezing as above, but those employing MP will be able to regain
   control from an infinite indirect chain.

   In simulation, the SET CPU INDIR=<limit> command sets the maximum number of
   levels; the default is 16.  If the level is exceeded during address
   resolution, the simulator will stop.  The maximum limit is 32768, which is
   the maximum possible address chain without an infinite loop, but an indirect
   chain over a few levels deep almost certainly represents a programming error.


   Implementation notes:

    1. Virtually all valid indirect references are one level deep, so we
       optimize for this case.  Also, we protect against entry with a direct
       address by simply returning the address, but the overhead can be saved by
       calling this routine only for indirect addresses.

    2. The 12892B Memory Protect accessory jumper W6 ("INT") controls whether
       held off pending interrupts are serviced immediately (jumper removed) or
       after three levels of indirection (jumper installed).  If the jumper is
       removed, MP must be enabled (control flip-flop set) for the interrupt
       hold off to be overridden.

       The jumper state need not be checked here, however, because this routine
       can be entered with an interrupt pending ("irq" non-zero) only if
       "ion_defer" and "check_deferral" are both true.  If either is false, the
       pending interrupt would have been serviced before calling the instruction
       executor that is calling this routine to resolve its address.  For
       "check_deferral" to return TRUE, then the INT jumper must be installed or
       the MP control flip-flop must be clear.

    3. When employing the indirect counter, the hardware clears a pending
       interrupt deferral after the third level of indirection and aborts the
       instruction after the fourth.

    4. The JRS, DJP, DJS, SJP, SJS, UJP, and UJS instructions also hold off
       interrupts for three indirect levels, but they count levels internally
       and do not depend on the presence of the MP accessory to reenable
       interrupt recognition.  However, DMS requires MP, so simulation uses the
       MP indirect counter for these instructions as well.

    5. In hardware, it is possible to execute an instruction with an infinite
       indirect loop (e.g., JMP *+1,I and DEF *,I).  If MP is not installed,
       this freezes the CPU with interrupts disabled until HALT is pressed.  In
       simulation, the instruction executes until the indirect limit is reached,
       whereupon the simulator stops with "Indirect address loop" status.
       Modelling the hardware CPU freeze would be difficult, as the simulation
       console would have to be polled locally to watch for CTRL+E (the
       simulation equivalent of the CPU front panel HALT button).
*/

t_stat resolve (HP_WORD MA, HP_WORD *address, uint32 irq)
{
uint32 level;
t_bool pending;

if (MA & I_IA) {                                        /* if the address is indirect */
    MA = ReadW (MA & LA_MASK);                          /*   then follow the chain (first level) */

    if (MA & I_IA) {                                    /* if the address is still indirect */
        pending = (irq && !(mp_unit.flags & DEV_DIS));  /*   then permit a pending interrupt if MP is enabled */

        for (level = 2; MA & I_IA; level++) {           /* follow the chain from level 2 until the address resolves */
            if (level > indirect_limit)                 /* if the limit is exceeded */
                return STOP_INDIR;                      /*   then stop the simulator */

            else if (pending)                           /* otherwise if an interrupt is pending */
                if (level == 3)                         /*   then if this is the third level */
                    ion_defer = FALSE;                  /*     then reenable interrupts */
                else if (level == 4)                    /*   otherwise if this is the fourth level */
                    return NOTE_INDINT;                 /*     then service the interrupt now */

            MA = ReadW (MA & LA_MASK);                  /* follow the address chain */
            }
        }
    }

*address = MA;                                          /* return the direct address */
return SCPE_OK;                                         /*   and success status */
}



/* Memory global utility routines */


/* Read a word from memory.

   Read and return a word from memory at the indicated logical address.  On
   entry, "dptr" points to the DEVICE structure of the device requesting access,
   "classification" is the type of access requested, and "address" is the offset
   into the 32K logical address space implied by the classification.

   If memory expansion is enabled, the logical address is mapped into a physical
   memory location; the map used is determined by the access classification.
   The current map (user or system), alternate map (the map not currently
   selected), or an explicit map (system, user, DCPC port A, or port B) may be
   requested.  Read protection is enabled for current or alternate map access
   and disabled for the others.  If memory expansion is disabled or not present,
   the logical address directly accesses the first 32K of memory.

   The memory protect (MP) and memory expansion module (MEM) accessories provide
   a protected mode that guards against improper accesses by user programs.
   They may be enabled or disabled independently, although protection requires
   that both be enabled.  MEM checks that read protection rules on the target
   page are compatible with the access desired.  If the check fails, and MP is
   enabled, then the request is aborted.

   The 1000 family maps memory location 0 to the A-register and location 1 to
   the B-register.  CPU reads of these locations return the A- or B-register
   values, while DCPC reads access physical memory locations 0 and 1 instead.


   Implementation notes:

    1. A read beyond the limit of physical memory returns 0.  This is handled by
       allocating the maximum memory array and initializing memory beyond the
       defined limit to zero, so no special handling is needed here..

    2. A MEM read protection violation with MP enabled causes an MP abort
       instead of a normal return.
*/

HP_WORD mem_read (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address)
{
uint32  index, map;
HP_WORD protection;

switch (classification) {                               /* dispatch on the access classification */

    case Fetch:
    case Data:
    default:                                            /* needed to quiet the compiler's anxiety */
        map = dms_ump;                                  /* use the currently selected map (user or system) */
        protection = RDPROT;                            /*   and enable read protection */
        break;

    case Data_Alternate:
        map = dms_ump ^ MAP_LNT;                        /* use the alternate map (user or system) */
        protection = RDPROT;                            /*   and enable read protection */
        break;

    case Data_System:
        map = SMAP;                                     /* use the system map explicitly */
        protection = NOPROT;                            /*   without protection */
        break;

    case Data_User:
        map = UMAP;                                     /* use the user map explicitly */
        protection = NOPROT;                            /*   without protection */
        break;

    case DMA_Channel_1:
        map = PAMAP;                                    /* use the DCPC port A map */
        protection = NOPROT;                            /*   without protection */
        break;

    case DMA_Channel_2:
        map = PBMAP;                                    /* use the DCPC port B map */
        protection = NOPROT;                            /*   without protection */
        break;
    }                                                   /* all cases are handled */

MR = address;                                           /* save the logical memory address */
index = meu_map (address, map, protection);             /*   and translate to a physical address */

if (index <= 1 && map < PAMAP)                          /* if the A/B register is referenced */
    TR = ABREG [index];                                 /*   then return the selected register value */
else                                                    /* otherwise */
    TR = (HP_WORD) M [index];                           /*   return the physical memory value */

tpprintf (dptr, mem_access [classification].debug_flag,
          DMS_FORMAT "  %s%s\n",
          meu_indicator, meu_page, MR, TR,
          mem_access [classification].name,
          mem_access [classification].debug_flag == TRACE_FETCH ? "" : " read");

return TR;
}


/* Write a word to memory.

   Write a word to memory at the indicated logical address.  On entry, "dptr"
   points to the DEVICE structure of the device requesting access,
   "classification" is the type of access requested, "address" is the offset
   into the 32K logical address space implied by the classification, and
   "value" is the value to write.

   If memory expansion is enabled, the logical address is mapped into a physical
   memory location; the map used is determined by the access classification.
   The current map (user or system), alternate map (the map not currently
   selected), or an explicit map (system, user, DCPC port A, or port B) may be
   requested.  Write protection is enabled for current or alternate map access
   and disabled for the others.  If memory expansion is disabled or not present,
   the logical address directly accesses the first 32K of memory.

   The memory protect (MP) and memory expansion module (MEM) accessories provide
   a protected mode that guards against improper accesses by user programs.
   They may be enabled or disabled independently, although protection requires
   that both be enabled.  MP checks that memory writes do not fall below the
   Memory Protect Fence Register (MPFR) value, and MEM checks that write
   protection rules on the target page are compatible with the access desired.
   If either check fails, and MP is enabled, then the request is aborted (so, to
   pass, a page must be writable AND the target must be above the MP fence).  In
   addition, a MEM write violation will occur if MP is enabled and the alternate
   map is selected, regardless of the page protection.

   The 1000 family maps memory location 0 to the A-register and location 1 to
   the B-register.  CPU writes to these locations store the values into the A or
   B register, while DCPC writes access physical memory locations 0 and 1
   instead.  MP uses a lower bound of 2 for memory writes, allowing unrestricted
   access to the A and B registers.


   Implementation notes:

    1. A write beyond the limit of physical memory is a no-operation.

    2. When the alternate map is enabled, writes are permitted only in the
       unprotected mode, regardless of page protections or the MP fence setting.
       This behavior is not mentioned in the MEM documentation, but it is tested by
       the MEM diagnostic and is evident from the MEM schematic.  Referring to
       Sheet 2 in the ERD, gates U125 and U127 provide this logic:

         WTV = MPCNDB * MAPON * (WPRO + ALTMAP)

       The ALTMAP signal is generated by the not-Q output of flip-flop U117,
       which toggles on control signal -CL3 assertion (generated by the MESP
       microorder) to select the alternate map.  Therefore, a write violation is
       indicated whenever a memory protect check occurs while the MEM is enabled
       and either the page is write-protected or the alternate map is selected.

       The hardware reference manuals that contain descriptions of those DMS
       instructions that write to the alternate map (e.g., MBI) say, "This
       instruction will always cause a MEM violation when executed in the
       protected mode and no bytes [or words] will be transferred."  However,
       they do not state that a write violation will be indicated, nor does the
       description of the write violation state that this is a potential cause.
*/

void mem_write (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address, HP_WORD value)
{
uint32  index, map;
HP_WORD protection;

switch (classification) {                               /* dispatch on the access classification */

    case Data:
    default:                                            /* needed to quiet the compiler's anxiety */
        map = dms_ump;                                  /* use the currently selected map (user or system) */
        protection = WRPROT;                            /*   and enable write protection */
        break;

    case Data_Alternate:
        map = dms_ump ^ MAP_LNT;                        /* use the alternate map (user or system) */
        protection = WRPROT;                            /*   and enable write protection */

        if (dms_enb)                                    /* if the MEM is enabled */
            dms_viol (address, MVI_WPR);                /*   then a violation always occurs if in protected mode */
        break;

    case Data_System:
        map = SMAP;                                     /* use the system map explicitly */
        protection = NOPROT;                            /*   without protection */
        break;

    case Data_User:
        map = UMAP;                                     /* use the user map explicitly */
        protection = NOPROT;                            /*   without protection */
        break;

    case DMA_Channel_1:
        map = PAMAP;                                    /* use the DCPC port A map */
        protection = NOPROT;                            /*   without protection */
        break;

    case DMA_Channel_2:
        map = PBMAP;                                    /* use the DCPC port B map */
        protection = NOPROT;                            /*   without protection */
        break;

    case Fetch:                                         /* instruction fetches */
        return;                                         /*   do not cause writes */

    }                                                   /* all cases are handled */

MR = address;                                           /* save the logical memory address */
index = meu_map (address, map, protection);             /*   and translate to a physical address */

if (protection != NOPROT && MP_TEST (address))          /* if protected and the MP check fails */
    MP_ABORT (address);                                 /*   then abort with an MP violation */

if (index <= 1 && map < PAMAP)                          /* if the A/B register is referenced */
    ABREG [index] = value;                              /*   then write the value to the selected register */

else if (index < fwanxm)                                /* otherwise if the location is within defined memory */
    M [index] = (MEMORY_WORD) value;                    /*   then write the value to memory */

TR = value;                                             /* save the value */

tpprintf (dptr, mem_access [classification].debug_flag,
          DMS_FORMAT "  %s write\n",
          meu_indicator, meu_page, MR, TR,
          mem_access [classification].name);

return;
}


/* Read a byte from memory.

   Read and return a byte from memory at the indicated logical address.  On
   entry, "dptr" points to the DEVICE structure of the device requesting access,
   "classification" is the type of access requested, and "byte_address" is the
   byte offset into the 32K logical address space implied by the classification.

   The 1000 is a word-oriented machine.  To permit byte accesses, a logical byte
   address is defined as two times the associated word address.  The LSB of the
   byte address designates the byte to access: 0 for the upper byte, and 1 for
   the lower byte.  As all 16 bits are used, byte addresses cannot be indirect.


   Implementation notes:

    1. Word buffering is not used to minimize memory reads, as the HP 1000
       microcode does a full word read for each byte accessed.
*/

uint8 mem_read_byte (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address)
{
const HP_WORD word_address = byte_address >> 1;         /* the address of the word containing the byte */
HP_WORD word;

word = mem_read (dptr, classification, word_address);   /* read the addressed word */

if (byte_address & LSB)                                 /* if the byte address is odd */
    return LOWER_BYTE (word);                           /*   then return the right-hand byte */
else                                                    /* otherwise */
    return UPPER_BYTE (word);                           /*   return the left-hand byte */
}


/* Write a byte to memory.

   Write a byte to memory at the indicated logical address.  On entry, "dptr"
   points to the DEVICE structure of the device requesting access,
   "classification" is the type of access requested, "byte_address" is the
   byte offset into the 32K logical address space implied by the classification,
   and "value" is the value to write.

   The 1000 is a word-oriented machine.  To permit byte accesses, a logical byte
   address is defined as two times the associated word address.  The LSB of the
   byte address designates the byte to access: 0 for the upper byte, and 1 for
   the lower byte.  As all 16 bits are used, byte addresses cannot be indirect.


   Implementation notes:

    1. Word buffering is not used to minimize memory writes, as the HP 1000
       base-set microcode does a full word write for each byte accessed.  (The
       DMS byte instructions, e.g., MBI, do full-word accesses for each pair of
       bytes, but that is to minimize the number of map switches.)
*/

void mem_write_byte (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address, uint8 value)
{
const HP_WORD word_address = byte_address >> 1;         /* the address of the word containing the byte */
HP_WORD word;

word = mem_read (dptr, classification, word_address);   /* read the addressed word */

if (byte_address & LSB)                                 /* if the byte address is odd */
    word = REPLACE_LOWER (word, value);                 /*   then replace the right-hand byte */
else                                                    /* otherwise */
    word = REPLACE_UPPER (word, value);                 /*   replace the left-hand byte */

mem_write (dptr, classification, word_address, word);   /* write the updated word back */

return;
}


/* Fast read from memory.

   This routine reads and returns a word from memory at the indicated logical
   address using the specified map.  Memory protection is not used, and tracing
   is not available.

   This routine is used when fast, unchecked access to mapped memory is
   required.
*/

HP_WORD mem_fast_read (HP_WORD address, uint32 map)
{
return mem_examine (meu_map (address, map, NOPROT));    /* return the value at the translated address */
}


/* Examine a physical memory address.

   This routine reads and returns a word from memory at the indicated physical
   address.  If the address lies outside of allocated memory, a zero value is
   returned.  There are no protections or error indications.
*/

HP_WORD mem_examine (uint32 address)
{
if (address <= 1)                                       /* if the address is 0 or 1 */
    return ABREG [address];                             /*   then return the A or B register value */

else if (address < PASIZE)                              /* otherwise if the address is within allocated memory */
    return (HP_WORD) M [address];                       /*   then return the memory value */

else                                                    /* otherwise the access is outside of memory */
    return 0;                                           /*   which reads as zero */
}


/* Deposit into a physical memory address.

   This routine writes a word into memory at the indicated physical address.  If
   the address lies outside of defined memory, the write is ignored.  There are
   no protections or error indications.
*/

void mem_deposit (uint32 address, HP_WORD value)
{
if (address <= 1)                                       /* if the address is 0 or 1 */
    ABREG [address] = value & DV_MASK;                  /*   then store into the A or B register */

else if (address < fwanxm)                              /* otherwise if the address is within defined memory */
    M [address] = (MEMORY_WORD) value & DV_MASK;        /*   then store the value */

return;
}



/* Memory Expansion Unit global utility routines */


/* DMS read and write map registers */

uint16 dms_rmap (uint32 mapi)
{
return dms_map [mapi & MAP_MASK] & ~MAP_RSVD;
}

void dms_wmap (uint32 mapi, uint32 dat)
{
dms_map [mapi & MAP_MASK] = (uint16) (dat & ~MAP_RSVD);
return;
}


/* Process a MEM violation.

   A MEM violation will report the cause in the violation register.  This occurs
   even if the MEM is not in the protected mode (i.e., MP is not enabled).  If
   MP is enabled, an MP abort is taken with the MEV flip-flop set.  Otherwise,
   we return to the caller.
*/

void dms_viol (uint32 va, HP_WORD st)
{
dms_vr = st | dms_upd_vr (va);                          /* set violation cause in register */

if (mp_control) {                                       /* memory protect on? */
    mp_mem_changed = TRUE;                              /* set the MP/MEM registers changed flag */

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

HP_WORD dms_upd_vr (uint32 va)
{
if (mp_control && (mp_mevff == CLEAR)) {                /* violation register unfrozen? */
    dms_vr = VA_GETPAG (va) |                           /* set map address */
             (dms_enb ? MVI_MEM : 0) |                  /*   and MEM enabled */
             (dms_ump ? MVI_UMP : 0);                   /*   and user map enabled */

    if (is_mapped (va))                                 /* is addressed mapped? */
        dms_vr = dms_vr | MVI_MEB;                      /* ME bus is enabled */

    mp_mem_changed = TRUE;                              /* set the MP/MEM registers changed flag */
    }

return dms_vr;
}


/* Update the MEM status register */

HP_WORD dms_upd_sr (void)
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



/* Memory Protect global utility routines */


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
HP_WORD violation = 0;
uint32  pgn = VA_GETPAG (va);                           /* get page number */

if (mp_control) {                                       /* MP on? */
    if (dms_enb) {                                      /* MEM on? */
        if (dms_map [dms_ump + pgn] & WRPROT)           /* page write protected? */
            violation = MVI_WPR;                        /* write violation occurred */

        if (!is_mapped (va) && (va >= plb))             /* base page target? */
            violation = violation | MVI_BPG;            /* base page violation occurred */

        if (violation)                                  /* any violation? */
            dms_viol (va, violation);                   /* signal MEM violation */
        }

    if ((va >= plb) && (va < mp_fence))                 /* jump under fence? */
        MP_ABORT (va);                                  /* signal MP violation */
    }

return;
}



/* CPU local SCP support routine declarations */


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

    5. To guarantee proper initialization, the 12920A terminal multiplexer
       requires that the Control Reset (CRS) I/O signal be asserted for a
       minimum of 100 milliseconds.  In practice, this is achieved by executing
       131,072 (128K) CLC 0 instructions in a tight loop.  This is not necessary
       in simulation, and in fact is detrimental, as 262,000+ trace lines will
       be written for each device that enables IOBUS tracing.  To avoid this,
       consecutive CLC 0 operations after the first are omitted.  This is
       detected by checking the select code and signal set of the last I/O
       operation.
*/

static uint32 cpuio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
static IOCYCLE last_signal_set = ioNONE;                /* the last set of I/O signals processed */
uint32   sc;
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
            setSKF (ion);                               /* skip if interrupt system is on */
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

            if (is_1000) {                              /* 1000 series? */
                memset (M, 0, (uint32) MEMSIZE * 2);    /* zero allocated memory */
                MR = 0077777;                           /* set M register */
                PR = 0100000;                           /* set P register */
                }

            else {                                      /* 21xx series */
                MR = 0;                                 /* clear M register */
                PR = 0;                                 /* clear P register */
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

            mp_mem_changed = TRUE;                      /* set the MP/MEM registers changed flag */
            break;

        case ioCLC:                                     /* clear control flip-flop */
            if (last_select_code != 0                   /* if the last I/O instruction */
              || (last_signal_set & ioCLC) == 0)        /*   was not a CLC 0 */
                for (sc = CRSDEV; sc <= MAXDEV; sc++)   /*     then assert the CRS signal */
                    if (devs [sc] != NULL)              /*       to all occupied I/O slots  */
                        io_dispatch (sc, ioCRS, 0);     /*         from select code 6 and up */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

last_signal_set = signal_set;                           /* save the current signal set for the next call */

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

static uint32 ovflio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
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

static uint32 pwrfio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
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
            stat_data = IORETURN (SCPE_OK, CIR);        /* input CIR value */
            break;

        default:                                        /* all other signals */
            break;                                      /*   are ignored */
        }

    working_set = working_set & ~signal;                /* remove current signal from set */
    }

return stat_data;
}


/* Examine a CPU memory location.

   This routine is called by the SCP to examine memory.  The routine retrieves
   the memory location indicated by "address" as modified by any "switches" that
   were specified on the command line and returns the value in the first element
   of "eval_array".

   On entry, the "map_address" routine is called to translate a logical address
   to a physical address.  If "switches" includes SIM_SW_REST or "-N", then the
   address is a physical address, and the routine returns the address unaltered.

   Otherwise, the address is a logical address interpreted in the context of the
   translation map implied by the specified switch and is mapped to a physical
   address.  If memory expansion is disabled but a map is specified, then the
   command is rejected.  Otherwise if the resulting address is beyond the
   current memory limit, or if mapping is implied or explicit but the address
   specified is outside of the logical address space, "address space exceeded"
   status is returned.

   Otherwise, the value is obtained from memory or the A/B register and returned
   in the first word of "eval_array."
*/

static t_stat cpu_examine (t_value *eval_array, t_addr address, UNIT *uptr, int32 switches)
{
uint32 index;

index = map_address ((HP_WORD) address, switches);      /* map the supplied address as directed by the switches */

if (dms_enb == 0 && switches & ALL_MAPMODES)            /* if the MEM is disabled but a mapping mode was given */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (index >= MEMSIZE)                              /* otherwise if the address is beyond the memory limit */
    return SCPE_NXM;                                    /*   then return non-existent memory status */

else if (eval_array == NULL)                            /* otherwise if the value pointer was not supplied */
    return SCPE_IERR;                                   /*   then return internal error status */

else if (switches & SIM_SW_REST || index >= 2)          /* otherwise if restoring or memory is being accessed */
    *eval_array = (t_value) M [index];                  /*   then return the memory value */
else                                                    /* otherwise */
    *eval_array = (t_value) ABREG [index];              /*   return the A or B register value */

return SCPE_OK;                                         /* return success status */
}


/* Deposit to a CPU memory location.

   This routine is called by the SCP to deposit to memory.  The routine stores
   the supplied "value" into memory at the "address" location as modified by any
   "switches" that were specified on the command line.

   On entry, the "map_address" routine is called to translate a logical address
   to a physical address.  If "switches" includes SIM_SW_REST or "-N", then the
   address is a physical address, and the routine returns the address unaltered.

   Otherwise, the address is a logical address interpreted in the context of the
   translation map implied by the specified switch and is mapped to a physical
   address.  If memory expansion is disabled but a map is specified, then the
   command is rejected.  Otherwise if the resulting address is beyond the
   current memory limit, or if mapping is implied or explicit but the address
   specified is outside of the logical address space, "address space exceeded"
   status is returned.

   Otherwise, the value is stored into memory or the A/B register.
*/

static t_stat cpu_deposit (t_value value, t_addr address, UNIT *uptr, int32 switches)
{
uint32 index;

index = map_address ((HP_WORD) address, switches);      /* map the supplied address as directed by the switches */

if (dms_enb == 0 && switches & ALL_MAPMODES)            /* if the MEM is disabled but a mapping mode was given */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (index >= MEMSIZE)                              /* otherwise if the address is beyond the memory limit */
    return SCPE_NXM;                                    /*   then return non-existent memory status */

else if (switches & SIM_SW_REST || index >= 2)          /* otherwise if restoring or memory is being accessed */
    M [index] = (MEMORY_WORD) value & DV_MASK;          /*   then write the memory value */

else                                                    /* otherwise */
    ABREG [index] = (HP_WORD) value & DV_MASK;          /*   write the A or B register value */

return SCPE_OK;                                         /* return success status */
}


/* Reset the CPU.

   This routine is called for a RESET, RESET CPU, RUN, or BOOT CPU command.  It
   is the simulation equivalent of an initial power-on condition (corresponding
   to PON, POPIO, and CRS signal assertion in the CPU) or a front-panel PRESET
   button press (corresponding to POPIO and CRS assertion).  SCP delivers a
   power-on reset to all devices when the simulator is started.

   If this is the first call after simulator startup, the initial memory array
   is allocated, the default CPU and memory size configuration is set, and the
   SCP-required program counter pointer is set to point to the REG array element
   corresponding to the P register.  In addition, the loader ROM sockets of the
   1000-series CPUs are populated with the initial ROM set, and the Basic Binary
   Loader (BBL) is installed in protected memory (the upper 64 words of the
   defined memory size).


   Implementation notes:

    1. Setting the sim_PC value at run time accommodates changes in the register
       order automatically.  A fixed setting runs the risk of it not being
       updated if a change in the register order is made.

    2. The initial set of installed HP 1000 boot loader ROMs is:

         Socket   ROM    Boot Device
         ------  ------  ------------------------
           0     12992K  2748 Paper Tape Reader
           1     12992A  7900 or 2883 Disc Drive
           2     12992D  7970 Magnetic Tape Drive
           3     12992B  7905/06/20/25 Disc Drive
*/

static t_stat cpu_reset (DEVICE *dptr)
{
if (M == NULL) {                                        /* if this is the initial call after simulator startup */
    pcq_r = find_reg ("PCQ", NULL, dptr);               /*   then get the PC queue pointer */

    if (pcq_r == NULL)                                  /* if the PCQ register is not present */
        return SCPE_IERR;                               /*   then something is seriously wrong */
    else                                                /* otherwise */
        pcq_r->qptr = 0;                                /*   initialize the register's queue pointer */

    M = (MEMORY_WORD *) calloc (PASIZE,                 /* allocate and zero the main memory array */
                                sizeof (MEMORY_WORD));  /*   to the maximum configurable size */

    if (M == NULL)                                      /* if the allocation failed */
        return SCPE_MEM;                                /*   then report a "Memory exhausted" error */

    else {                                              /* otherwise perform one-time initialization */
        for (sim_PC = dptr->registers;                  /* find the P register entry */
             sim_PC->loc != &PR && sim_PC->loc != NULL; /*   in the register array */
             sim_PC++);                                 /*     for the SCP interface */

        if (sim_PC == NULL)                             /* if the P register entry is not present */
            return SCPE_NXREG;                          /*   then there is a serious problem! */

        MEMSIZE = 32768;                                /* set the initial memory size */
        set_model (NULL, UNIT_2116, NULL, NULL);        /*   and the initial CPU model */

        loader_rom [0] = find_dev ("PTR");              /* install the 12992K ROM in socket 0 */
        loader_rom [1] = find_dev ("DQC");              /*   and the 12992A ROM in socket 1 */
        loader_rom [2] = find_dev ("MSC");              /*   and the 12992D ROM in socket 2 */
        loader_rom [3] = find_dev ("DS");               /*   and the 12992B ROM in socket 3 */

        loader_rom [0]->boot (0, loader_rom [0]);       /* install the BBL via the paper tape reader boot routine */
        set_loader (NULL, FALSE, NULL, NULL);           /*   and then disable the loader, which was enabled */
        }
    }


if (sim_switches & SWMASK ('P'))                        /* if this is a power-on reset */
    IOPOWERON (&cpu_dib);                               /*   then issue the PON signal to the CPU */
else                                                    /* otherwise */
    IOPRESET (&cpu_dib);                                /*   issue a PRESET */

sim_brk_dflt = SWMASK ('N');                            /* the default breakpoint type is "nomap" as MEM is disabled */

return SCPE_OK;
}


/* Device boot routine.

   This routine is called by the BOOT CPU and LOAD CPU commands to copy the
   specified boot loader ROM program into the upper 64 words of the logical
   address space.  It is equivalent to pressing the IBL (Initial Binary Loader)
   button on the front panel of a 1000 M/E/F-Series CPU.

   On entry, the S register must be set to indicate the specific boot loader ROM
   and the associated device select code to be copied, as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | -   - |      select code      | -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Bits 15-14 select one of four loader ROM sockets on the CPU board that may
   contain ROMs.  If the specified socket does, the contents of the ROM are
   copied into the upper 64 words of memory and configured to use the specified
   select code.  The unspecified bits of the S register are available for use by
   the bootstrap program.

   If the select code is less than 10 octal, the loader is not copied, and the
   O (overflow) register is set to 1.  A successful copy and configuration
   clears the O register.

   The 21xx-series CPUs do not provide the IBL function.  If this routine is
   invoked while the CPU is configured as one of these machines, the command is
   rejected.


   Implementation notes:

    1. In hardware, a non-existent ROM (i.e., an empty socket) reads as though
       all words contain 177777 octal.  This would result in the loader area of
       memory containing 62 all-ones words, followed by a word set to 177777 +
       SC - 000010, where SC is the configured select code, followed by a word
       set to the negative starting address of the loader.  This is not
       simulated; instead, an attempt to boot from an empty socket is rejected
       with "Command not allowed."
*/

static t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
const int32 select_code = IBL_SC  (SR);                 /* the select code from S register bits 11-6 */
const int32 rom_socket  = IBL_ROM (SR);                 /* the ROM socket number from S register bits 15-14 */

if (is_1000)                                            /* if this is a 1000-series CPU */
    if (select_code < VARDEV) {                         /*   then if the select code is invalid */
        O = 1;                                          /*     then set the overflow register */
        return SCPE_ARG;                                /*       and reject the IBL with "Invalid argument" */
        }

    else if (loader_rom [rom_socket] == NULL)           /*   otherwise if the ROM socket is empty */
        return SCPE_NXDEV;                              /*     then reject with "Non-existent device" */

    else {                                                          /*   otherwise */
        O = 0;                                                      /*     clear overflow to indicate a good IBL */
        return loader_rom [rom_socket]->boot (select_code, NULL);   /*       and copy the ROM into memory */
        }

else                                                    /* otherwise this is a 21xx machine */
    return SCPE_NOFNC;                                  /*   and IBL isn't supported */
}


/* Set the CPU simulation stop conditions.

   This validation routine is called to configure the set of CPU stop
   conditions.  The "option" parameter is 0 to clear the stops, 1 to set the
   stops, and 2 to set the indirect chain length limit.  "cptr" points to the
   first character of the name of the stop to be cleared or set.  The unit and
   description pointers are not used.

   The routine processes commands of the form:

     SET CPU STOP
     SET CPU STOP=<stopname>[;<stopname>...]
     SET CPU NOSTOP
     SET CPU NOSTOP=<stopname>[;<stopname>...]
     SET CPU INDIR=<limit>

   The valid <stopname>s are contained in the "cpu_stop" table.  If names are
   not specified, all stop conditions are enabled or disabled.


   Implementation notes:

    1. The maximum indirect limit value is 32K, as an indirect chain cannot
       exceed the logical memory size without being in a loop.
*/

static t_stat set_stops (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
char gbuf [CBUFSIZE];
t_stat status;
uint32 stop;

if (cptr == NULL)                                               /* if there are no arguments */
    if (option == 0)                                            /*   then if we're clearing the stops */
        for (stop = 0; cpu_stop [stop].name != NULL; stop++)    /*     then loop through the flags */
            *cpu_stop [stop].status = SCPE_OK;                  /*       and clear each stop status */

    else if (option == 1)                                       /* otherwise if we're setting the stops */
        for (stop = 0; cpu_stop [stop].name != NULL; stop++)    /*   then loop through the flags */
            *cpu_stop [stop].status = cpu_stop [stop].value;    /*     and set each stop status */

    else                                                        /* otherwise */
        return SCPE_MISVAL;                                     /*   report the missing indirect limit value */

else if (*cptr == '\0')                                 /* otherwise if the argument is empty */
    return SCPE_MISVAL;                                 /*   then report the missing value */

else if (option == 2) {                                         /* otherwise if we're setting the indirect limit */
    stop = (uint32) get_uint (cptr, 10, LA_MAX + 1, &status);   /*   then parse the limit value */

    if (status != SCPE_OK)                              /* if a parsing error occurred */
        return status;                                  /*   then return the error status */
    else                                                /* otherwise */
        indirect_limit = stop;                          /*   set the indirect limit */
    }

else                                                    /* otherwise at least one stop argument is present */
    while (*cptr) {                                     /* loop through the arguments */
        cptr = get_glyph (cptr, gbuf, ';');             /* get the next argument */

        for (stop = 0; cpu_stop [stop].name != NULL; stop++)            /* loop through the flags */
            if (strcmp (cpu_stop [stop].name, gbuf) == 0) {             /*   and if the argument matches */
                if (option == 1)                                        /*     then if it's a STOP argument */
                    *cpu_stop [stop].status = cpu_stop [stop].value;    /*       then set the stop status */
                else                                                    /*     otherwise it's a NOSTOP argument */
                    *cpu_stop [stop].status = SCPE_OK;                  /*       so clear the stop status */

                break;                                  /* this argument has been processed */
                }

        if (cpu_stop [stop].name == NULL)               /* if the argument was not found */
            return SCPE_ARG;                            /*   then report it */
        }

return SCPE_OK;                                         /* the stops were successfully processed */
}


/* Change the CPU memory size.

   This validation routine is called to configure the CPU memory size.  The
   "new_size" parameter is set to the size desired and will be one of the
   discrete sizes supported by the simulator.  The "uptr" parameter points to
   the CPU unit and is used to obtain the CPU model.  The other parameters are
   not used.

   The routine processes commands of the form:

     SET [-F] CPU <memsize>

   If the new memory size is larger than the supported size for the CPU model
   currently selected, the routine returns an error.  If the new size is smaller
   than the previous size, and if the area that would be lost contains non-zero
   data, the user is prompted to confirm that memory should be truncated.  If
   the user denies the request, the change is rejected.  Otherwise, the new size
   is set.  The user may omit the confirmation request and force truncation by
   specifying the "-F" switch on the command line.

   On a 21xx CPU, the last 64 words in memory are reserved for the binary
   loader.  Before changing the memory size, the current loader is copied to the
   shadow RAM to preserve any manual changes that were made.  Then the new
   memory size is set, with the beginning of the loader area set as the first
   word of non-existent memory.

   Finally, non-existent memory is zeroed, so that the mem_read routine does not
   need any special handling for addresses beyond the end of defined memory.


   Implementation notes:

    1. In hardware, reads from non-existent memory return zero, and writes are
       ignored.  In simulation, the largest possible memory is instantiated and
       initialized to zero.  Therefore, only writes need to be checked against
       memory size.

    2. On the 21xx machines, doing SET CPU LOADERDISABLE decreases available
       memory size by 64 words.
*/

static t_stat set_size (UNIT *uptr, int32 new_size, CONST char *cptr, void *desc)
{
static CONST char confirm [] = "Really truncate memory [N]?";
uint32 i;
uint32 old_size = (uint32) MEMSIZE;                     /* current memory size */

const uint32 model = CPU_MODEL_INDEX;                   /* the current CPU model index */

if ((uint32) new_size > cpu_features [model].maxmem)    /* if the new memory size is not supported on current model */
    return SCPE_NOFNC;                                  /*   then report the error */

if (!(sim_switches & SWMASK ('F'))                      /* if truncation is not explicitly forced */
  && ! mem_is_empty (new_size)                          /*   and the truncated part is not empty */
  && get_yn (confirm, FALSE) == FALSE)                  /*     and the user denies confirmation */
    return SCPE_INCOMP;                                 /*       then abort the command */

if (is_1000)                                            /* loader unsupported */
    MEMSIZE = fwanxm = new_size;                        /* set new memory size */

else {                                                  /* 21xx CPU? */
    set_loader (uptr, FALSE, NULL, NULL);               /* save loader to shadow RAM */
    MEMSIZE = new_size;                                 /* set new memory size */
    fwanxm = (uint32) MEMSIZE - IBL_SIZE;               /* reserve memory for loader */
    }

for (i = fwanxm; i < old_size; i++)                     /* zero non-existent memory */
    M [i] = 0;

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


   Implementation notes:

    1. "cpu_configuration" is used by the symbolic examine and deposit routines
       and instruction tracing to determine whether the firmware implementing a
       given opcode is present.  It is a copy of the CPU unit option flags with
       the encoded CPU model decoded into model flag bits.  This allows a simple
       (and fast) AND operation with a firmware feature word to determine
       applicability, saving the multiple masks and comparisons that would
       otherwise be required.

       Additionally, the configuration word has the unit CPU model bits set on
       permanently to permit a base-set feature test for those CPUs that have no
       options currently enabled (at least one non-option bit must be on for the
       test to succeed, and the model bits are not otherwise used).
*/

static t_stat set_model (UNIT *uptr, int32 new_model, CONST char *cptr, void *desc)
{
const uint32 old_family = UNIT_CPU_FAMILY;              /* current CPU type */
const uint32 new_family = new_model & UNIT_FAMILY_MASK; /* new CPU family */
const uint32 new_index  = new_model >> UNIT_V_CPU;      /* new CPU model index */
uint32 new_memsize;
t_stat result;

if (MEMSIZE > cpu_features [new_index].maxmem)          /* if the current memory size is too large for the new model */
    new_memsize = cpu_features [new_index].maxmem;      /*   then set it to the maximum size supported */
else                                                    /* otherwise */
    new_memsize = (uint32) MEMSIZE;                     /*   leave it unchanged */

result = set_size (uptr, new_memsize, NULL, NULL);      /* set the new memory size */

if (result == SCPE_OK) {                                            /* if the change succeeded */
    cpu_configuration = cpu_features [new_index].typ & UNIT_OPTS    /*   then set the typical options */
                          | UNIT_MODEL_MASK                         /*     and the base model bits */
                          | 1u << new_index;                        /*       and the new CPU model flag */

    cpu_unit.flags = cpu_unit.flags & ~UNIT_OPTS                    /* enable the typical features */
                       | cpu_features [new_index].typ & UNIT_OPTS;  /*   for the new model */

    if (cpu_features [new_index].typ & UNIT_MP)         /* MP in typ config? */
        mp_dev.flags &= ~DEV_DIS;                       /* enable it */
    else
        mp_dev.flags |= DEV_DIS;                        /* disable it */

    if (cpu_features[new_index].opt & UNIT_MP)          /* MP an option? */
        mp_dev.flags |= DEV_DISABLE;                    /* make it alterable */
    else
        mp_dev.flags &= ~DEV_DISABLE;                   /* make it unalterable */


    if (cpu_features [new_index].typ & UNIT_DMA) {      /* DMA in typ config? */
        dma1_dev.flags &= ~DEV_DIS;                     /* enable DMA channel 1 */

        if (new_model == UNIT_2114)                     /* 2114 has only one channel */
            dma2_dev.flags |= DEV_DIS;                  /* disable channel 2 */
        else                                            /* all others have two channels */
            dma2_dev.flags &= ~DEV_DIS;                 /* enable it */
        }

    else {
        dma1_dev.flags |= DEV_DIS;                      /* disable channel 1 */
        dma2_dev.flags |= DEV_DIS;                      /* disable channel 2 */
        }

    if (cpu_features [new_index].opt & UNIT_DMA) {      /* DMA an option? */
        dma1_dev.flags |= DEV_DISABLE;                  /* make it alterable */

        if (new_model == UNIT_2114)                     /* 2114 has only one channel */
            dma2_dev.flags &= ~DEV_DISABLE;             /* make it unalterable */
        else                                            /* all others have two channels */
            dma2_dev.flags |= DEV_DISABLE;              /* make it alterable */
        }

    else {                                              /* otherwise DMA is not available */
        dma1_dev.flags &= ~DEV_DISABLE;                 /* make it unalterable */
        dma2_dev.flags &= ~DEV_DISABLE;                 /* make it unalterable */
        }

    if ((old_family == UNIT_FAMILY_1000) &&             /* if current family is 1000 */
        (new_family == UNIT_FAMILY_21XX)) {             /* and new family is 21xx */
        deassign_device (&dma1_dev);                    /* delete DCPC names */
        deassign_device (&dma2_dev);
        }

    else if ((old_family == UNIT_FAMILY_21XX) &&        /* otherwise if current family is 21xx */
             (new_family == UNIT_FAMILY_1000)) {        /* and new family is 1000 */
        assign_device (&dma1_dev, "DCPC1");             /* change DMA device name */
        assign_device (&dma2_dev, "DCPC2");             /* to DCPC for familiarity */
        }


    if (!(cpu_features [new_index].typ & UNIT_DMS))     /* if DMS is not being enabled */
        dms_enb = 0;                                    /*   then disable MEM mapping */

    is_1000 = (new_family == UNIT_FAMILY_1000);         /* set model */

    if (is_1000)
        fwanxm = (uint32) MEMSIZE;                      /* loader reserved only for 21xx */
    else                                                /* 2100 or 211x */
        fwanxm = (uint32) MEMSIZE - IBL_SIZE;           /* reserve memory for loader */
    }

return result;
}


/* Change a CPU option.

   This validation routine is called to configure the option set for the current
   CPU model.  The "option" parameter is set to the option desired and will be
   one of the unit option flags.  The "uptr" parameter points to the CPU unit
   and is used to obtain the CPU model.  The other parameters are not used.

   The routine processes commands of the form:

     SET CPU <option>[,<option>...]

   The option must be valid for the current CPU model, or the command is
   rejected.


   Implementation notes:

    1. "cpu_configuration" is used by the symbolic examine and deposit routines
       and instruction tracing to determine whether the firmware implementing a
       given opcode is present.  It is a copy of the CPU unit option flags with
       the encoded CPU model decoded into model flag bits.  This allows a simple
       (and fast) AND operation with a firmware feature word to determine
       applicability, saving the multiple masks and comparisons that would
       otherwise be required.
*/

static t_stat set_option (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
uint32 model = CPU_MODEL_INDEX;                         /* current CPU model index */

if ((cpu_features [model].opt & option) == 0)           /* option supported? */
    return SCPE_NOFNC;                                  /* no */

if (UNIT_CPU_TYPE == UNIT_TYPE_2100) {
    if ((option == UNIT_FP) || (option == UNIT_FFP))    /* 2100 IOP and FP/FFP options */
        uptr->flags &= ~UNIT_IOP;                       /*   are mutually exclusive */
    else if (option == UNIT_IOP)
        uptr->flags &= ~(UNIT_FP | UNIT_FFP);

    if (option == UNIT_FFP)                             /* 2100 FFP option requires FP */
        uptr->flags |= UNIT_FP;
    }

cpu_configuration = cpu_configuration & ~UNIT_OPTS      /* update the CPU configuration */
                      | uptr->flags & UNIT_OPTS;        /*   with the revised option settings */

if (option & UNIT_EMA_VMA)                              /* if EMA or VMA is being set */
    cpu_configuration &= ~UNIT_EMA_VMA;                 /*   then remove both as they are mutually exclusive */

cpu_configuration |= option;                            /* include the new setting */

return SCPE_OK;
}


/* Clear a CPU option.

   Validation:
   - Checks that the current CPU model supports the option selected.
   - Clears flag from unit structure (we are processing MTAB_XTD entries).
   - If CPU is 2100, ensures that FFP is disabled if FP disabled
     (FP is required for FFP installation).


   Implementation notes:

    1. "cpu_configuration" is used by the symbolic examine and deposit routines
       and instruction tracing to determine whether the firmware implementing a
       given opcode is present.  It is a copy of the CPU unit option flags with
       the encoded CPU model decoded into model flag bits.  This allows a simple
       (and fast) AND operation with a firmware feature word to determine
       applicability, saving the multiple masks and comparisons that would
       otherwise be required.
*/

t_bool clear_option (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
uint32 model = CPU_MODEL_INDEX;                         /* current CPU model index */

if ((cpu_features[model].opt & option) == 0)            /* option supported? */
    return SCPE_NOFNC;                                  /* no */

uptr->flags = uptr->flags & ~option;                    /* disable option */

if (option == UNIT_DMS)                                 /* if DMS is being disabled */
    dms_enb = 0;                                        /*   then disable MEM mapping */

if ((UNIT_CPU_TYPE == UNIT_TYPE_2100) &&                /* disabling 2100 FP? */
    (option == UNIT_FP))
    uptr->flags = uptr->flags & ~UNIT_FFP;              /* yes, so disable FFP too */

cpu_configuration = cpu_configuration & ~UNIT_OPTS      /* update the CPU configuration */
                      | uptr->flags & UNIT_OPTS;        /*   with the revised option settings */

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

static t_stat set_loader (UNIT *uptr, int32 enable, CONST char *cptr, void *desc)
{
static MEMORY_WORD loader [IBL_SIZE];
uint32 i;
t_bool is_enabled = (fwanxm == MEMSIZE);

if (is_1000 || MEMSIZE == 0)                            /* valid only for 21xx and for initialized memory */
    return SCPE_NOFNC;

if (is_enabled && (enable == 0)) {                      /* disable loader? */
    fwanxm = (uint32) MEMSIZE - IBL_SIZE;               /* decrease available memory */
    for (i = 0; i < IBL_SIZE; i++) {                    /* copy loader */
        loader [i] = M [fwanxm + i];                    /* from memory */
        M [fwanxm + i] = 0;                             /* and zero location */
        }
    }

else if ((!is_enabled) && (enable == 1)) {              /* enable loader? */
    for (i = 0; i < IBL_SIZE; i++)                      /* copy loader */
        M [fwanxm + i] = loader [i];                    /* to memory */
    fwanxm = (uint32) MEMSIZE;                          /* increase available memory */
    }

return SCPE_OK;
}


/* Change the set of installed loader ROMs.

   This validation routine is called to install loader ROMs in the four
   available sockets of a 1000-series CPU.  The routine processes commands of
   the form:

     SET CPU ROMS=[<dev0>][;[<dev1>][;[<dev2>][;[<dev3>]]]]

   On entry, "cptr" points at the the first character of the ROM list.  The
   option value and the unit and description pointers are not used.

   All four ROM sockets are set for each command.  If no devices are specified,
   then all sockets are emptied.  Otherwise, specifying a valid device name
   installs the device loader ROM into the socket corresponding to the position
   of the device name in the list.  Sockets may be left empty by omitting the
   corresponding device name or by supplying fewer than four device names.

   Loader ROMs may only be altered if the current CPU model is a 1000-series
   machine, and a device must be bootable and have a loader ROM assigned, or the
   command will be rejected.  A rejected command does not alter any of the ROM
   assignments.

   Example commands and their effects on the installed ROM sockets follow:

     Command                Action
     ---------------------  -------------------------------------------------
     SET CPU ROMS=          Remove ROMs from sockets 0-3
     SET CPU ROMS=PTR       Install PTR in 0; leave 1-3 empty
     SET CPU ROMS=DS;MS     Install DS in 0 and MS in 1; leave 2 and 3 empty
     SET CPU ROMS=;;DPC     Install DPC in 2; leave 0, 1, and 3 empty
     SET CPU ROMS=DQC;;;DA  Install DQC in 0 and DA in 3; leave 1 and 2 empty


   Implementation notes:

    1. Entering "SET CPU ROMS" without an equals sign or list is rejected with a
       "Missing value" error.  This is to prevent accidental socket clearing
       when "SHOW CPU ROMS" was intended.
*/

static t_stat set_roms (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
DEVICE *dptr;
char   gbuf [CBUFSIZE];
uint32 socket = 0;
DEVICE *rom [4] = { NULL };

if (is_1000 == FALSE)                                   /* if the CPU is not a 1000-series unit */
    return SCPE_NOFNC;                                  /*   then reject the command */

else if (cptr == NULL)                                  /* otherwise if the list is not specified */
    return SCPE_MISVAL;                                 /*   then report that the list is missing */

else if (*cptr == '\0') {                               /* otherwise if the list is null */
    loader_rom [0] = NULL;                              /*   then empty */
    loader_rom [1] = NULL;                              /*     all of the */
    loader_rom [2] = NULL;                              /*       ROM sockets */
    loader_rom [3] = NULL;
    }

else {                                                  /* otherwise */
    while (*cptr) {                                     /*   loop through the arguments */
        cptr = get_glyph (cptr, gbuf, ';');             /* get the next argument */

        if (socket == 4)                                /* if all four sockets have been set */
            return SCPE_2MARG;                          /*   then reject the command */

        else if (gbuf [0] == '\0')                      /* otherwise if the device name is omitted */
            rom [socket++] = NULL;                      /*   then empty the corresponding socket */

        else {                                          /* otherwise we have a device name */
            dptr = find_dev (gbuf);                     /*   so find the associated DEVICE pointer */

            if (dptr == NULL)                           /* if the device name is not valid */
                return SCPE_NXDEV;                      /*   then reject the command */

            else if (dptr->boot == NULL)                /* otherwise if it's valid but not bootable */
                return SCPE_NOFNC;                      /*   then reject the command */

            else                                        /* otherwise */
                rom [socket++] = dptr;                  /*   install the boot loader ROM */
            }
        }

    loader_rom [0] = rom [0];                           /* install the ROM set */
    loader_rom [1] = rom [1];                           /*   now that we have */
    loader_rom [2] = rom [2];                           /*     a valid */
    loader_rom [3] = rom [3];                           /*       device list */
    }

return SCPE_OK;                                         /* report that the command succeeded */
}


/* Change the instruction execution trace criteria.

   This validation routine is called to configure the criteria that select
   instruction execution tracing.  The "option" parameter is 0 to clear and 1 to
   set the criteria, and "cptr" points to the first character of the match value
   to be set.  The unit and description pointers are not used.

   The routine processes commands of the form:

     SET CPU EXEC=<match>[;<mask>]
     SET CPU NOEXEC

   If the <mask> value is not supplied, a mask of 177777 octal is used.  The
   values are entered in the current CPU data radix, which defaults to octal,
   unless an override switch is present on the command line.
*/

static t_stat set_exec (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
char   gbuf [CBUFSIZE];
uint32 match, mask, radix;
t_stat status;

if (option == 0)                                        /* if this is a NOEXEC request */
    if (cptr == NULL) {                                 /*   then if there are no arguments */
        exec_match = D16_UMAX;                          /*     then set the match and mask values */
        exec_mask  = 0;                                 /*       to prevent matching */
        return SCPE_OK;                                 /*         and return success */
        }

    else                                                /*   otherwise there are extraneous characters */
        return SCPE_2MARG;                              /*     so report that there are too many arguments */

else if (cptr == NULL || *cptr == '\0')                 /* otherwise if the EXEC request supplies no arguments */
    return SCPE_MISVAL;                                 /*   then report a missing value */

else {                                                  /* otherwise at least one argument is present */
    cptr = get_glyph (cptr, gbuf, ';');                 /*   so get the match argument */

    if (sim_switches & SWMASK ('O'))                    /* if an octal override is present */
        radix = 8;                                      /*   then parse the value in base 8 */
    else if (sim_switches & SWMASK ('D'))               /* otherwise if a decimal override is present */
        radix = 10;                                     /*   then parse the value in base 10 */
    else if (sim_switches & SWMASK ('H'))               /* otherwise if a hex override is present */
        radix = 16;                                     /*   then parse the value in base 16 */
    else                                                /* otherwise */
        radix = cpu_dev.dradix;                         /*   use the current CPU data radix */

    match = (uint32) get_uint (gbuf, radix, D16_UMAX, &status); /* parse the match value */

    if (status != SCPE_OK)                              /* if a parsing error occurred */
        return status;                                  /*   then return the error status */

    else if (*cptr == '\0') {                           /* otherwise if no more characters are present */
        exec_match = match;                             /*   then set the match value */
        exec_mask  = D16_MASK;                          /*     and default the mask value */
        return SCPE_OK;                                 /*       and return success */
        }

    else {                                              /* otherwise another argument is present */
        cptr = get_glyph (cptr, gbuf, ';');             /*   so get the mask argument */

        mask = (uint32) get_uint (gbuf, radix, D16_UMAX, &status);  /* parse the mask value */

        if (status != SCPE_OK)                          /* if a parsing error occurred */
            return status;                              /*   then return the error status */

        else if (*cptr == '\0')                         /* if no more characters are present */
            if (mask == 0)                              /*   then if the mask value is zero */
                return SCPE_ARG;                        /*     then the match will never succeed */

            else {                                      /*   otherwise */
                exec_match = match;                     /*     set the match value */
                exec_mask  = mask;                      /*       and the mask value */
                return SCPE_OK;                         /*         and return success */
                }

        else                                            /* otherwise extraneous characters are present */
            return SCPE_2MARG;                          /*   so report that there are too many arguments */
        }
    }
}


/* Show the CPU simulation stop conditions.

   This display routine is called to show the set of CPU stop conditions or the
   indirect chain length limit.  The "st" parameter is the open output stream,
   and "val" is 1 to show the stops and 2 to show the indirect limit.  The other
   parameters are not used.

   To show stops, the routine searches through the stop table for status
   variables that are set to values other than SCPE_OK.  For each one it finds,
   the routine prints the corresponding stop name.  If none are found, it
   reports that all stops are disabled.

   This routine services an extended modifier entry, so it must add the trailing
   newline to the output before returning.
*/

static t_stat show_stops (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 stop;
t_bool need_spacer = FALSE;

if (val == 2)                                           /* if the indirect limit is requested */
    fprintf (st, "Limit=%d\n", indirect_limit);         /*   then show it */

else {                                                      /* otherwise show the enabled stops */
    for (stop = 0; cpu_stop [stop].name != NULL; stop++)    /* loop through the set of stops in the table */
        if (*cpu_stop [stop].status != SCPE_OK) {           /* if the current stop is enabled */
            if (need_spacer)                                /*   then if a spacer is needed */
                fputc (';', st);                            /*     then add it first */
            else                                            /* otherwise this is the first one reported */
                fputs ("Stop=", st);                        /*   so print the report label */

            fputs (cpu_stop [stop].name, st);               /* report the stop name */

            need_spacer = TRUE;                             /* a spacer will be needed next time */
            }

    if (need_spacer)                                    /* if at least one simulation stop was enabled */
        fputc ('\n', st);                               /*   then add the required trailing newline */
    else                                                /* otherwise no enabled stops were found */
        fputs ("Stops disabled\n", st);                 /*   so report that all are disabled */
    }

return SCPE_OK;                                         /* report the success of the display */
}


/* Display the CPU model and optional loader status.

   Loader status is displayed for 21xx models and suppressed for 1000 models.
*/

static t_stat show_model (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fputs ((const char *) desc, st);                        /* write model name */

if (! is_1000)                                          /* valid only for 21xx */
    if (fwanxm < MEMSIZE)                               /* loader area non-existent? */
        fputs (", loader disabled", st);                /* yes, so access disabled */
    else
        fputs (", loader enabled", st);                 /* no, so access enabled */

return SCPE_OK;
}


/* Show the set of installed loader ROMs.

   This display routine is called to show the set of installed loader ROMs in
   the four available sockets of a 1000-series CPU.  On entry, the "st"
   parameter is the open output stream.  The other parameters are not used.

   The routine prints a table of ROMs in this format:

     Socket  Device    ROM
     ------  -------  ------
       0       PTR    12992K
       1       DQC    12992A
       2       DS     12992B
       3     <empty>

   If a given socket contains a ROM, the associated device name and HP part
   number for the loader ROM are printed.

   This routine services an extended modifier entry, so it must add the trailing
   newline to the output before returning.
*/

static t_stat show_roms (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
struct LOOKUP_TABLE {
    char    *name;                              /* device name */
    char     suffix;                            /* ROM part number suffix */
    };

static const struct LOOKUP_TABLE lookup [] = {  /* table of device names and ROM part numbers */
    { "DQC",  'A' },                            /*   12992A 7900/7901/2883 Disc Loader */
    { "DS",   'B' },                            /*   12992B 7905/7906/7920/7925 Disc Loader */
    { "MSC",  'D' },                            /*   12992D 7970 Magnetic Tape Loader */
    { "DPC",  'F' },                            /*   12992F 7900/7901 Disc Loader */
    { "DA",   'H' },                            /*   12992H 7906H/7920H/7925H/9885 Disc Loader */
    { "IPLI", 'K' },                            /*   12992K Paper Tape Loader */
    { "PTR",  'K' },                            /*   12992K Paper Tape Loader */
    { NULL,   '?' }
    };

CONST char *dname;
uint32 socket, index;
char   letter = '?';

fputc ('\n', st);                                       /* skip a line */
fputs ("Socket  Device    ROM\n", st);                  /*   and print */
fputs ("------  -------  ------\n", st);                /*     the table header */

for (socket = 0; socket < 4; socket++)                  /* loop through the sockets */
    if (loader_rom [socket] == NULL)                    /* if the socket is empty */
        fprintf (st, "  %u     <empty>\n", socket);     /*   then report it as such */

    else {                                              /* otherwise the socket is occupied */
        dname = loader_rom [socket]->name;              /*   so get the device name */

        for (index = 0; lookup [index].name; index++)       /* search the lookup table */
            if (strcmp (lookup [index].name, dname) == 0) { /*   for a match to the device name */
                letter = lookup [index].suffix;             /*     and get the part number suffix */
                break;
                }

        fprintf (st, "  %u       %-4s   12992%c\n",     /* print the ROM information */
                 socket, dname, letter);
        }

return SCPE_OK;                                         /* return success status */
}


/* Show the instruction execution trace criteria.

   This display routine is called to show the criteria that select instruction
   execution tracing.  The "st" parameter is the open output stream.  The other
   parameters are not used.

   This routine services an extended modifier entry, so it must add the trailing
   newline to the output before returning.
*/

static t_stat show_exec (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 radix;

if (exec_mask == 0)                                     /* if the instruction is entirely masked */
    fputs ("Execution trace disabled\n", st);           /*   then report that matching is disabled */

else {                                                  /* otherwise */
    if (sim_switches & SWMASK ('O'))                    /*   if an octal override is present */
        radix = 8;                                      /*     then print the value in base 8 */
    else if (sim_switches & SWMASK ('D'))               /*   otherwise if a decimal override is present */
        radix = 10;                                     /*     then print the value in base 10 */
    else if (sim_switches & SWMASK ('H'))               /*   otherwise if a hex override is present */
        radix = 16;                                     /*     then print the value in base 16 */
    else                                                /*   otherwise */
        radix = cpu_dev.dradix;                         /*     use the current CPU data radix */

    fputs ("Execution trace match = ", st);                         /* print the label */
    fprint_val (st, exec_match, radix, cpu_dev.dwidth, PV_RZRO);    /*   and the match value */

    fputs (", mask = ", st);                                        /* print a separator */
    fprint_val (st, exec_mask, radix, cpu_dev.dwidth, PV_RZRO);     /*   and the mask value */

    fputc ('\n', st);                                               /* tie off the line */
    }

return SCPE_OK;                                         /* report the success of the display */
}


/* Show the current CPU simulation speed.

   This display routine is called to show the current simulation speed.  The
   "st" parameter is the open output stream.  The other parameters are not used.

   The CPU speed, expressed as a multiple of the hardware speed, is calculated
   by the time-base generator service routine.  It is only representative when
   the TBG is calibrated, and the CPU is not idling.
*/

static t_stat show_speed (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Simulation speed = %ux\n", cpu_speed);    /* display the current CPU speed */
return SCPE_OK;                                         /*   and report success */
}



/* CPU local utility routine declarations */


/* Get effective address from IR */

static t_stat ea (HP_WORD IR, HP_WORD *address, uint32 irq)
{
HP_WORD MA;

MA = IR & (I_IA | I_DISP);                              /* ind + disp */

if (IR & I_CP)                                          /* current page? */
    MA = ((PR - 1) & I_PAGENO) | MA;                    /* merge in page from P */

if (IR & I_IA)                                          /* if the address is indirect */
    return resolve (MA, address, irq);                  /*   then resolve it to a direct address */

else {                                                  /* otherwise the address is direct */
    *address = MA;                                      /*   so use it as is */
    return SCPE_OK;                                     /*     and return success */
    }
}


/* Execute a Shift/Rotate Group micro-operation.

   SRG instructions consist of two shift/rotate micro-operations plus a CLE and
   a SLA/SLB micro-op.  This routine implements the shift and rotate operation.

   Each of the two shift/rotate operations has an enable bit that must be set to
   enable the operation.  If the bit is not set, the operation is a NOP, with
   the exception that an ELA/ELB or ERA/ERB operation alters the E register (but
   not the A/B register).  We accommodate this by including the enable/disable
   bit with the three-bit operation code and decode the disabled operations of
   ELA/ELB and ERA/ERB separately from their enabled operations.

   On entry, "value" is the value of the selected accumulator (A/B), and
   "operation" is the micro-op and enable bit.  The routine returns the updated
   accumulator value and modifies the E register as indicated.


   Implementation notes:

    1. The enable bit is located adjacent to the three-bit encoded operation for
       the first shift/rotate micro-op, but it is spaced one bit away from the
       encoded operation for the second micro-op.  It is faster to decode
       separate values for each location rather than move the second enable bit
       adjacent to its encoded operation.  The former imposes no time penalty;
       the jump table for the "switch" statement is simply somewhat larger.
*/

static HP_WORD srg_uop (HP_WORD value, HP_WORD operation)
{
uint32 extend;

switch (operation) {                                       /* dispatch on the micro operation */

    case SRG1_EN | I_xLS:
    case SRG2_EN | I_xLS:                                   /* ALS/BLS */
        return value & D16_SIGN | value << 1 & D16_SMAX;    /* arithmetic left shift */

    case SRG1_EN | I_xRS:
    case SRG2_EN | I_xRS:                                   /* ARS/BRS */
        return value & D16_SIGN | value >> 1;               /* arithmetic right shift */

    case SRG1_EN | I_RxL:
    case SRG2_EN | I_RxL:                                   /* RAL/RBL */
        return (value << 1 | value >> 15) & D16_MASK;       /* rotate left */

    case SRG1_EN | I_RxR:
    case SRG2_EN | I_RxR:                                   /* RAR/RBR */
        return (value >> 1 | value << 15) & D16_MASK;       /* rotate right */

    case SRG1_EN | I_xLR:
    case SRG2_EN | I_xLR:                                   /* ALR/BLR */
        return value << 1 & D16_SMAX;                       /* arithmetic left shift, clear sign */

    case SRG_DIS | I_ERx:                                   /* disabled ERA/ERB */
        E = value & LSB;                                    /* rotate the LSB right into E */
        return value;                                       /*   and return the original value */

    case SRG1_EN | I_ERx:
    case SRG2_EN | I_ERx:                                   /* ERA/ERB */
        extend = E;                                         /* save the original E value */
        E = value & LSB;                                    /* rotate the LSB right into E */
        return value >> 1 | (HP_WORD) extend << 15;         /*   and rotate right with E filling the MSB */

    case SRG_DIS | I_ELx:                                   /* disabled ELA/ELB */
        E = value >> 15 & LSB;                              /* rotate the MSB left into E */
        return value;                                       /*   and return the original value */

    case SRG1_EN | I_ELx:
    case SRG2_EN | I_ELx:                                   /* ELA/ELB */
        extend = E;                                         /* save the original E value */
        E = value >> 15 & LSB;                              /* rotate the MSB left into E */
        return (value << 1 | (HP_WORD) extend) & D16_MASK;  /*   and rotate left with E filling the LSB */

    case SRG1_EN | I_xLF:
    case SRG2_EN | I_xLF:                                   /* ALF/BLF */
        return (value << 4 | value >> 12) & D16_MASK;       /* rotate left four */

    default:                                                /* all other (disabled) cases */
        return value;                                       /*   return the original value */
    }
}


/* Execute one machine instruction.

   This routine executes the CPU instruction present in the IR.  The CPU state
   (registers, memory, interrupt status) is modified as necessary, and the
   routine return SCPE_OK if the instruction executed successfully.  Any other
   status indicates that execution should cease, and control should return to
   the simulator console.  For example, a programmed HALT instruction returns
   STOP_HALT status.

   This routine implements the main instruction dispatcher.  Instructions
   corresponding to the MRG, SRG, and ASG are executed inline.  IOG, EAG, and
   UIG instructions are executed in external handlers.

   The JMP instruction executor handles CPU idling.  The 21xx/1000 CPUs have no
   "wait for interrupt" instruction.  Idling in HP operating systems consists of
   sitting in "idle loops" that end with JMP instructions.  We test for certain
   known patterns when a JMP instruction is executed to decide if the simulator
   should idle.  The recognized patterns are:

     for RTE-6/VM:
      - ISZ <n> / JMP *-1
      - mp_fence = 0
      - XEQT (address 1717B) = 0
      - DMS on with system map enabled
      - RTE verification: TBG (address 1674B) = CLK select code

     for RTE though RTE-IVB:
      - JMP *
      - mp_fence = 0
      - XEQT (address 1717B) = 0
      - DMS on with user map enabled (RTE-III through RTE-IVB only)
      - RTE verification: TBG (address 1674B) = CLK select code

     for DOS through DOS-III:
      - STF 0 / CCA / CCB / JMP *-3
      - DOS verification: A = B = -1, address 40B = -64, address 67B = +64
      - Note that in DOS, the TBG is set to 100 milliseconds

   Idling must not occur if an interrupt is pending.  As mentioned before, the
   CPU will defer pending interrupts when certain instructions are executed.  OS
   interrupt handlers exit via such deferring instructions.  If there is a
   pending interrupt when the OS is otherwise idle, the idle loop will execute
   one instruction before reentering the interrupt handler.  If we call
   sim_idle() in this case, we will lose interrupts.

   Consider the situation in RTE.  Under simulation, the TTY and CLK events are
   co-scheduled, with the CLK expiring one instruction after the TTY.  When the
   TTY interrupts, $CIC in RTE is entered.  One instruction later, the CLK
   expires and posts its interrupt, but it is not immediately handled, because
   the JSB $CIC,I / JMP $CIC0,I / SFS 0,C instruction entry sequence continually
   defers interrupts until the interrupt system is turned off.  When $CIC
   returns via $IRT, one instruction of the idle loop is executed, even though
   the CLK interrupt is still pending, because the UJP instruction used to
   return also defers interrupts.

   If "sim_idle" is called at this point, the simulator will sleep when it
   should be handling the pending CLK interrupt.  When it awakes, TTY expiration
   will be moved forward to the next instruction.  The still-pending CLK
   interrupt will then be recognized, and $CIC will be entered.  But the TTY and
   then the CLK will then expire and attempt to interrupt again, although they
   are deferred by the $CIC entry sequence.  This causes the second CLK
   interrupt to be missed, as processing of the first one is just now being
   started.

   Similarly, at the end of the CLK handling, the TTY interrupt is still
   pending.  When $IRT returns to the idle loop, "sim_idle" would be called
   again, so the TTY and then CLK interrupt a third time.  Because the second
   TTY interrupt is still pending, $CIC is entered, but the third TTY interrupt
   is lost.

   We solve this problem by testing for a pending interrupt before calling
   "sim_idle".  The system isn't really quiescent if it is just about to handle
   an interrupt.


   Implementation notes:

    1. Instruction decoding is based on the HP 1000, which does a 256-way branch
       on the upper eight bits of the instruction, as follows:

         15 14 13 12 11 10  9  8  Instruction Group
         -- -- -- -- -- -- -- --  ---------------------------------------
          x <-!= 0->  x  x  x  x  memory reference
          0  0  0  0  x  0  x  x  shift/rotate
          0  0  0  0  x  1  x  x  alter/skip
          1  0  0  0  x  1  x  x  I/O
          1  0  0  0  0  0  x  0  extended arithmetic
          1  0  0  0  0  0  0  1  divide (decoded as 100400)
          1  0  0  0  1  0  0  0  double load (decoded as 104000)
          1  0  0  0  1  0  0  1  double store (decoded as 104400)
          1  0  0  0  1  0  1  0  extended instr group 0 (A/B is set)
          1  0  0  0  x  0  1  1  extended instr group 1 (A/B is ignored)

    2. JSB is tricky.  It is possible to generate both an MP and a DM violation
       simultaneously, as the MP and MEM cards validate in parallel.  Consider a
       JSB to a location under the MP fence and on a write-protected page.  This
       situation must be reported as a DM violation, because it has priority
       (SFS 5 and SFC 5 check only the MEVFF, which sets independently of the MP
       fence violation).  Under simulation, this means that DM violations must
       be checked, and the MEVFF must be set, before an MP abort is taken.  This
       is done by the "mp_dms_jmp" routine.

    3. Although MR (and TR) will be changed by reads of an indirect chain, the
       idle loop JMP will be direct, and so MR will contain the correct value
       for the "idle loop omitted" trace message.

    4. The Alter/Skip Group RSS micro-op reverses the skip sense of the SEZ,
       SSA/SSB, SLA/SLB, and SZA/SZB micro-op tests.  Normally, the instruction
       skips if any test is true.  However, the specific combination of SSA/SSB,
       SLA/SLB, and RSS micro-ops causes a skip if BOTH of the skip cases are
       true, i.e., if both the MSB and LSB of the register value are ones.  We
       handle this as a special case, because without RSS, the instruction skips
       if EITHER the MSB or LSB is zero.  The other reversed skip cases (SEZ,RSS
       and SZA,RSS/SZB,RSS) are independent.
*/

static t_stat machine_instruction (HP_WORD IR, t_bool iotrap, uint32 irq_pending, uint32 *idle_save)
{
uint32  ab_selector, result, skip;
HP_WORD data, MA;
t_bool  rss;
t_stat  status = SCPE_OK;

switch (UPPER_BYTE (IR)) {                              /* dispatch on bits 15-8 of the instruction */

/* Memory Reference Group */

    case 0020: case 0021: case 0022: case 0023:
    case 0024: case 0025: case 0026: case 0027:         /* AND */
    case 0220: case 0221: case 0222: case 0223:
    case 0224: case 0225: case 0226: case 0227:         /* AND,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = AR & ReadW (MA);                       /*   then AND the accumulator and memory */
        break;


    case 0230: case 0231: case 0232: case 0233:
    case 0234: case 0235: case 0236: case 0237:         /* JSB,I */
        ion_defer = TRUE;                               /* defer interrupts */

    /* fall into the JSB case */

    case 0030: case 0031: case 0032: case 0033:
    case 0034: case 0035: case 0036: case 0037:         /* JSB */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            mp_dms_jmp (MA, jsb_plb);                   /*   then validate the jump address */

            WriteW (MA, PR);                            /* store P into the target memory address */

            PCQ_ENTRY;                                  /* save P in the queue */
            PR = MA + 1 & LA_MASK;                      /*   and jump to the word after the target address */
            }
        break;


    case 0040: case 0041: case 0042: case 0043:
    case 0044: case 0045: case 0046: case 0047:         /* XOR */
    case 0240: case 0241: case 0242: case 0243:
    case 0244: case 0245: case 0246: case 0247:         /* XOR,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = AR ^ ReadW (MA);                       /*   then XOR the accumulator and memory */
        break;


    case 0250: case 0251: case 0252: case 0253:
    case 0254: case 0255: case 0256: case 0257:         /* JMP,I */
        ion_defer = TRUE;                               /* defer interrupts */

    /* fall into the JMP case */

    case 0050: case 0051: case 0052: case 0053:
    case 0054: case 0055: case 0056: case 0057:         /* JMP */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status != SCPE_OK)                          /* if the address failed to resolve */
            break;                                      /*   then abort execution */

        mp_dms_jmp (MA, 0);                             /* validate the jump address */

        PCQ_ENTRY;                                      /* save P in the queue */
        PR = MA;                                        /*   and jump to the target address */

        if (sim_idle_enab && irq_pending == 0                   /* if idle is enabled and no interrupt is pending */
          && ((PR == err_PC                                     /*   and the jump target is * (RTE through RTE-IVB) */
            || PR == err_PC - 1                                 /*   or the target is *-1 (RTE-6/VM) */
            && (mem_fast_read (PR, dms_ump) & I_MRG) == I_ISZ)  /*     and *-1 is ISZ <n> */
          && mp_fence == 0                                      /*   and the MP fence is zero */
          && M [xeqt] == 0                                      /*   and no program is executing */
          && M [tbg] == tbg_select_code)                        /*   and the TBG select code is set */

          || PR == err_PC - 3                                   /*   or the jump target is *-3 (DOS through DOS-III) */
          && M [PR] == I_STF                                    /*   and *-3 is STF 0 */
          && AR == 0177777                                      /*   and the A and B registers */
          && BR == 0177777                                      /*     are both set to -1 */
          && M [m64] == 0177700                                 /*   and the -64 and +64 base-page constants */
          && M [p64] == 0000100) {                              /*     are set as expected */
            tprintf (cpu_dev, cpu_dev.dctrl,
                     DMS_FORMAT "idle loop execution omitted\n",
                     meu_indicator, meu_page, MR, IR);

            if (cpu_dev.dctrl != 0) {                   /* if tracing is enabled */
                *idle_save = cpu_dev.dctrl;             /*   then save the current trace flag set */
                cpu_dev.dctrl = 0;                      /*     and turn off tracing for the idle loop */
                }

            sim_idle (TMR_POLL, FALSE);                 /* idle the simulator */
            }
        break;


    case 0060: case 0061: case 0062: case 0063:
    case 0064: case 0065: case 0066: case 0067:         /* IOR */
    case 0260: case 0261: case 0262: case 0263:
    case 0264: case 0265: case 0266: case 0267:         /* IOR,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = AR | ReadW (MA);                       /*   then OR the accumulator and memory */
        break;


    case 0070: case 0071: case 0072: case 0073:
    case 0074: case 0075: case 0076: case 0077:         /* ISZ */
    case 0270: case 0271: case 0272: case 0273:
    case 0274: case 0275: case 0276: case 0277:         /* ISZ,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            data = ReadW (MA) + 1 & D16_MASK;           /*   then increment the memory word */
            WriteW (MA, data);                          /*     and write it back */

            if (data == 0)                              /* if the value rolled over to zero */
                PR = PR + 1 & LA_MASK;                  /*   then increment P */
            }
        break;


    case 0100: case 0101: case 0102: case 0103:
    case 0104: case 0105: case 0106: case 0107:         /* ADA */
    case 0300: case 0301: case 0302: case 0303:
    case 0304: case 0305: case 0306: case 0307:         /* ADA,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            data = ReadW (MA);                          /*   then get the target word */
            result = AR + data;                         /*     and add the accumulator to memory */

            if (result > D16_UMAX)                      /* if the result overflowed */
                E = 1;                                  /*   then set the Extend register */

            if (~(AR ^ data) & (AR ^ result) & D16_SIGN)    /* if the sign of the result differs from the signs */
                O = 1;                                      /*   of the operands, then set the Overflow register */

            AR = result & R_MASK;                       /* store the sum into the accumulator */
            }
        break;


    case 0110: case 0111: case 0112: case 0113:
    case 0114: case 0115: case 0116: case 0117:         /* ADB */
    case 0310: case 0311: case 0312: case 0313:
    case 0314: case 0315: case 0316: case 0317:         /* ADB,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            data = ReadW (MA);                          /*   then get the target word */
            result = BR + data;                         /*     and add the accumulator to memory */

            if (result > D16_UMAX)                      /* if the result overflowed */
                E = 1;                                  /*   then set the Extend register */

            if (~(BR ^ data) & (BR ^ result) & D16_SIGN)    /* if the sign of the result differs from the signs */
                O = 1;                                      /*   of the operands, then set the Overflow register */

            BR = result & R_MASK;                       /* store the sum into the accumulator */
            }
        break;


    case 0120: case 0121: case 0122: case 0123:
    case 0124: case 0125: case 0126: case 0127:         /* CPA */
    case 0320: case 0321: case 0322: case 0323:
    case 0324: case 0325: case 0326: case 0327:         /* CPA,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            if (AR != ReadW (MA))                       /*   then if the accumulator and memory differ */
                PR = PR + 1 & LA_MASK;                  /*     then increment P */
        break;


    case 0130: case 0131: case 0132: case 0133:
    case 0134: case 0135: case 0136: case 0137:         /* CPB */
    case 0330: case 0331: case 0332: case 0333:
    case 0334: case 0335: case 0336: case 0337:         /* CPB,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            if (BR != ReadW (MA))                       /*   then if the accumulator and memory differ */
                PR = PR + 1 & LA_MASK;                  /*     then increment P */
        break;


    case 0140: case 0141: case 0142: case 0143:
    case 0144: case 0145: case 0146: case 0147:         /* LDA */
    case 0340: case 0341: case 0342: case 0343:
    case 0344: case 0345: case 0346: case 0347:         /* LDA,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = ReadW (MA);                            /*   then load the accumulator from memory */
        break;


    case 0150: case 0151: case 0152: case 0153:
    case 0154: case 0155: case 0156: case 0157:         /* LDB */
    case 0350: case 0351: case 0352: case 0353:
    case 0354: case 0355: case 0356: case 0357:         /* LDB,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            BR = ReadW (MA);                            /*   then load the accumulator from memory */
        break;


    case 0160: case 0161: case 0162: case 0163:
    case 0164: case 0165: case 0166: case 0167:         /* STA */
    case 0360: case 0361: case 0362: case 0363:
    case 0364: case 0365: case 0366: case 0367:         /* STA,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            WriteW (MA, AR);                            /*   then write the accumulator to memory */
        break;


    case 0170: case 0171: case 0172: case 0173:
    case 0174: case 0175: case 0176: case 0177:         /* STB */
    case 0370: case 0371: case 0372: case 0373:
    case 0374: case 0375: case 0376: case 0377:         /* STB,I */
        status = ea (IR, &MA, irq_pending);             /* get the effective address */

        if (status == SCPE_OK)                          /* if the address resolved */
            WriteW (MA, BR);                            /*   then write the accumulator to memory */
        break;


/* Alter/Skip Group */

    case 0004: case 0005: case 0006: case 0007:
    case 0014: case 0015: case 0016: case 0017:         /* ASG */
        skip = 0;                                       /* assume that no skip is needed */

        rss = (IR & I_RSS) != 0;                        /* get the Reverse Skip Sense flag */

        ab_selector = (IR & I_AB ? 1 : 0);              /* get the A/B register selector */
        data = ABREG [ab_selector];                     /*   and the register data */

        if (IR & I_CLx)                                 /* if the CLA/CLB micro-op is enabled */
            data = 0;                                   /*   then clear the value */

        if (IR & I_CMx)                                 /* if the CMA/CMB micro-op is enabled */
            data = data ^ D16_MASK;                     /*   then complement the value */

        if (IR & I_SEZ && (E == 0) ^ rss)               /* if SEZ[,RSS] is enabled and E is clear [set] */
            skip = 1;                                   /*   then skip the next instruction */

        if (IR & I_CLE)                                 /* if the CLE micro-op is enabled */
            E = 0;                                      /*   then clear E */

        if (IR & I_CME)                                 /* if the CME micro-op is enabled */
            E = E ^ LSB;                                /*   then complement E */

        if ((IR & I_SSx_SLx_RSS) == I_SSx_SLx_RSS) {    /* if the SSx, SLx, and RSS micro-ops are enabled together */
            if ((data & D16_SIGN_LSB) == D16_SIGN_LSB)  /*   then if both sign and least-significant bits are set */
                skip = 1;                               /*     then skip the next instruction */
            }

        else {                                          /* otherwise */
            if (IR & I_SSx && !(data & D16_SIGN) ^ rss) /*   if SSx[,RSS] is enabled and the MSB is clear [set] */
                skip = 1;                               /*     then skip the next instruction */

            if (IR & I_SLx && !(data & LSB) ^ rss)      /*   if SLx[,RSS] is enabled and the LSB is clear [set] */
                skip = 1;                               /*     then skip the next instruction */
            }

        if (IR & I_INx) {                               /* if the INA/INB micro-op is enabled */
            data = data + 1 & D16_MASK;                 /*   then increment the value */

            if (data == 0)                              /* if the value wrapped around to zero */
                E = 1;                                  /*   then set the Extend register */

            else if (data == D16_SIGN)                  /* otherwise if the value overflowed into the sign bit */
                O = 1;                                  /*   then set the Overflow register */
            }

        if (IR & I_SZx && (data == 0) ^ rss)            /* if SZx[,RSS] is enabled and the value is zero [non-zero] */
            skip = 1;                                   /*   then skip the next instruction */

        if ((IR & I_ALL_SKIPS) == I_RSS)                /* if RSS is present without any other skip micro-ops */
            skip = 1;                                   /*   then skip the next instruction unconditionally */

        ABREG [ab_selector] = data;                     /* store the result in the selected register */
        PR = PR + skip & LA_MASK;                       /*   and skip the next instruction if indicated */
        break;


/* Shift/Rotate Group */

    case 0000: case 0001: case 0002: case 0003:
    case 0010: case 0011: case 0012: case 0013:         /* SRG */
        ab_selector = (IR & I_AB ? 1 : 0);              /* get the A/B register selector */
        data = ABREG [ab_selector];                     /*   and the register data */

        data = srg_uop (data, SRG1 (IR));               /* do the first shift */

        if (IR & SRG_CLE)                               /* if the CLE micro-op is enabled */
            E = 0;                                      /*   then clear E */

        if (IR & SRG_SLx && (data & LSB) == 0)          /* if SLx is enabled and the LSB is clear */
            PR = PR + 1 & LA_MASK;                      /*   then skip the next instruction */

        ABREG [ab_selector] = srg_uop (data, SRG2 (IR));    /* do the second shift and set the accumulator */
        break;


/* I/O Group */

    case 0204: case 0205: case 0206: case 0207:
    case 0214: case 0215: case 0216: case 0217:         /* IOG */
        status = cpu_iog (IR, iotrap);                  /* execute the I/O instruction */
        break;


/* Extended Arithmetic Group */

    case 0200:                                          /* EAU group 0 */
    case 0201:                                          /* DIV */
    case 0202:                                          /* EAU group 2 */
    case 0210:                                          /* DLD */
    case 0211:                                          /* DST */
        status = cpu_eau (IR, irq_pending);             /* execute the extended arithmetic instruction */
        break;


/* User Instruction Group */

    case 0212:                                          /* UIG 0 */
        status = cpu_uig_0 (IR, irq_pending, iotrap);   /* execute the user instruction opcode */
        break;

    case 0203:
    case 0213:                                          /* UIG 1 */
        status = cpu_uig_1 (IR, irq_pending, iotrap);   /* execute the user instruction opcode */
        break;

    }                                                   /* all cases are handled */


return status;                                          /* return the execution status */
}


/* Determine whether a pending interrupt deferral should be inhibited.

   Execution of certain instructions generally cause a pending interrupt to be
   deferred until the succeeding instruction completes.  However, the interrupt
   deferral rules differ for the 21xx vs. the 1000.

   The 1000 always defers until the completion of the instruction following a
   deferring instruction.  The 21xx defers unless the following instruction is
   an MRG instruction other than JMP or JMP,I or JSB,I.  If it is, then the
   deferral is inhibited, i.e., the pending interrupt will be serviced.

   In either case, if the interrupting device is the memory protect card, or if
   the INT jumper is out on the 12892B MP card, then interrupts are not
   deferred.

   See the "Set Phase Logic Flowchart" for the transition from phase 1A to phase
   1B, and "Section III Theory of Operation," "Control Section Detailed Theory"
   division, "Phase Control Logic" subsection, "Phase 1B" paragraph (3-241) in
   the Model 2100A Computer Installation and Maintenance Manual for details.
*/

static t_bool check_deferral (uint32 irq_sc)
{
HP_WORD next_instruction;

if (! is_1000) {                                        /* if the CPU is a 21xx model */
    next_instruction = mem_fast_read (PR, dms_ump);     /*   then prefetch the next instruction */

    if (MRGOP (next_instruction)                        /* if it is an MRG instruction */
      && (next_instruction & I_MRG_I) != I_JSB_I        /*   but not JSB,I? */
      && (next_instruction & I_MRG)   != I_JMP)         /*   and not JMP or JMP,I */
        return FALSE;                                   /*     then inhibit deferral */
    }

if (irq_sc == PRO                                       /* if memory protect is interrupting */
  || mp_unit.flags & UNIT_MP_INT && mp_control)         /*   or the INT jumper is out for the 12892B card */
    return FALSE;                                       /*     then inhibit deferral */
else                                                    /* otherwise */
    return TRUE;                                        /*   deferral is permitted */
}


/* Logical-to-physical address translation for console access.

   This routine translates a logical address interpreted in the context of the
   translation map implied by the specified switch to a physical address.  It is
   called to map addresses when the user is examining or depositing memory.  It
   is also called to restore a saved configuration, although mapping is not used
   for restoration.  All memory protection checks are off for console access.

   Command line switches modify the interpretation of logical addresses as
   follows:

     Switch  Meaning
     ------  --------------------------------------------------
       -N    Use the address directly with no mapping
       -S    If memory expansion is enabled, use the system map
       -U    If memory expansion is enabled, use the user map
       -P    If memory expansion is enabled, use the port A map
       -Q    If memory expansion is enabled, use the port B map

   If no switch is specified, the address is interpreted using the current map
   if memory expansion is enabled; otherwise, the address is not mapped.  If the
   current or specified map is used, then the address must lie within the 32K
   logical address space; if not, then an address larger than the current memory
   size is returned.
*/

static uint32 map_address (HP_WORD logical, int32 switches)
{
uint32 map;

if (switches & (SWMASK ('N') | SIM_SW_REST))            /* if no mapping is requested */
    return logical;                                     /*   then the address is already a physical address */

else if ((dms_enb || switches & ALL_MAPMODES)           /* otherwise if mapping is enabled or requested */
  && logical > LA_MAX)                                  /*   and the address is not a logical address */
    return (uint32) MEMSIZE;                            /*     then report a memory overflow */

else if (switches & SWMASK ('S'))                       /* otherwise if the -S switch is specified */
    map = SMAP;                                         /*   then use the system map */

else if (switches & SWMASK ('U'))                       /* otherwise if the -U switch is specified */
    map = UMAP;                                         /*   then use the user map */

else if (switches & SWMASK ('P'))                       /* otherwise if the -P switch is specified */
    map = PAMAP;                                        /*   then use the DCPC port A map */

else if (switches & SWMASK ('Q'))                       /* otherwise if the -Q switch is specified */
    map = PBMAP;                                        /*   then use the DCPC port B map */

else                                                    /* otherwise */
    map = dms_ump;                                      /*   use the current map (system or user) */

return meu_map (logical, map, NOPROT);                  /* translate the address without protection */
}


/* Check for non-zero value in a memory address range.

   A range of memory locations is checked for the presence of a non-zero value.
   The starting address of the range is supplied, and the check continues
   through the end of defined memory.  The routine returns TRUE if the memory
   range was empty (i.e., contained only zero values) and FALSE otherwise.
*/

static t_bool mem_is_empty (uint32 starting_address)
{
uint32 address;

for (address = starting_address; address < MEMSIZE; address++)  /* loop through the specified address range */
    if (M [address] != 0)                                       /* if this location is non-zero */
        return FALSE;                                           /*   then indicate that memory is not empty */

return TRUE;                                            /* return TRUE if all locations contain zero values */
}



/* Memory Expansion Unit local utility routine declarations */


/* Mapped access check.

   Return TRUE if the address will be mapped (presuming MEM is enabled).
*/

static t_bool is_mapped (uint32 address)
{
uint32 dms_fence;

if (address >= 02000u)                                  /* if the address is not on the base page */
    return TRUE;                                        /*   then it is always mapped */

else {                                                  /* otherwise */
    dms_fence = dms_sr & MST_FENCE;                     /*   get the base-page fence value */

    if (dms_sr & MST_FLT)                               /* if the lower portion is mapped */
        return (address < dms_fence);                   /*   then return TRUE if the address is below the fence */
    else                                                /* otherwise the upper portion is mapped */
        return (address >= dms_fence);                  /*   so return TRUE if the address is at or above the fence */
    }
}


/* Map a logical address to a physical address..

   This routine translates logical into physical addresses.  The logical
   address, desired map, and desired access protection are supplied.  If the
   access is legal, the mapped physical address is returned; if it is not, then
   a MEM violation is indicated.

   The current map may be specified by passing "dms_ump" as the "map" parameter,
   or a specific map may be used.  Normally, read and write accesses pass RDPROT
   or WRPROT as the "prot" parameter to request access checking.  For DMA
   accesses, NOPROT must be passed to inhibit access checks.

   This routine checks for read, write, and base-page violations and will call
   "dms_viol" as appropriate.  The latter routine will abort if MP is enabled,
   or will return if protection is off.
*/

static uint32 meu_map (HP_WORD address, uint32 map, HP_WORD prot)
{
uint32 map_register;

if (dms_enb) {                                          /* if the Memory Expansion Unit is enabled */
    if (address <= 1 && map < PAMAP) {                  /*   then if the reference is to the A or B register */
        meu_page = 0;                                   /*     then the physical page is page 0 */
        return address;                                 /*       and the address is already physical */
        }

    else if (is_mapped (address) == FALSE) {            /* otherwise if a base-page address is not mapped */
        meu_page = 0;                                   /*   then the physical page is page 0 */

        if (address > 1 && prot == WRPROT)              /* a write to the unmapped part of the base page */
            dms_viol (address, MVI_BPG);                /*   causes a base-page violation if protection is enabled */

        return address;                                 /* the address is already physical */
        }

    else {                                                  /* otherwise the address is mapped */
        map_register = dms_map [map + VA_GETPAG (address)]; /*   so get the map register for the logical page */

        meu_page = MAP_PAGE (map_register);                 /* save the physical page number */
        meu_indicator = map_indicator [map / MAP_LNT];      /*   and set the map indicator the the applied map */

        if (map_register & prot)                            /* if the desired access is not allowed */
            dms_viol (address, prot);                       /*   then a read or write protection violation occurs */

        return TO_PAGE (meu_page) | VA_GETOFF (address);    /* form the physical address from the mapped page and offset */
        }
    }

else {                                                  /* otherwise the MEU is disabled */
    meu_page = VA_GETPAG (address);                     /*   so the physical page is the logical page */
    meu_indicator = '-';                                /* set the map indicator to indicate no mapping */

    return address;                                     /* the physical address is the logical address */
    }
}



/* DMA local SCP support routine declarations */


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

static uint32 dmapio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
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
            if (dma [ch].xferen == SET)
                tpprintf (dma_dptrs [ch], TRACE_CMD, "Channel transfer %s\n",
                          (dma [ch].cw3 == 0 ? "completed" : "aborted"));

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
            if (is_1000)                                /* 1000? */
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

            tpprintf (dma_dptrs [ch], TRACE_CSRW, "Control word 1 is %sselect code %02o\n",
                      fmt_bitset (data, dma_cw1_format), data & I_DEVMASK);
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

            if (dma [ch].xferen == SET)
                tpprintf (dma_dptrs [ch], TRACE_CMD, "Channel completion interrupt is inhibited\n");
            break;

        case ioSTC:                                     /* set control flip-flop */
            dma [ch].packer = 0;                        /* clear packing register */
            dma [ch].xferen = dma [ch].control = SET;   /* set transfer enable and control */

            if (dma [ch].cw2 & DMA2_OI)
                tpprintf (dma_dptrs [ch], TRACE_CMD,
                          "Channel transfer of %u words from select code %02o to address %05o started\n",
                          NEG16 (dma [ch].cw3), dma [ch].cw1 & I_DEVMASK, dma [ch].cw2 & VAMASK);
            else
                tpprintf (dma_dptrs [ch], TRACE_CMD,
                          "Channel transfer of %u words from address %05o to select code %02o started\n",
                          NEG16 (dma [ch].cw3), dma [ch].cw2 & VAMASK, dma [ch].cw1 & I_DEVMASK);
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

static uint32 dmasio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
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
                data = (uint16) dma [ch].cw3;           /* rest use full value */

            stat_data = IORETURN (SCPE_OK, data);       /* merge status and remaining word count */

            tpprintf (dma_dptrs [ch], TRACE_CSRW, "Remaining word count is %u\n",
                      NEG16 (dma [ch].cw3));
            break;

        case ioIOO:                                                 /* I/O data output */
            if (dma [ch].select) {                                  /* word count selected? */
                dma [ch].cw3 = IODATA (stat_data);                  /* save count */

                tpprintf (dma_dptrs [ch], TRACE_CSRW, "Control word 3 is word count %u\n",
                          NEG16 (dma [ch].cw3));
                }

            else {                                                  /* memory address selected */
                if (UNIT_CPU_MODEL == UNIT_2114)                    /* 2114? */
                    dma [ch].cw2 = IODATA (stat_data) & 0137777;    /* only 14-bit address */
                else                                                /* other models */
                    dma [ch].cw2 = IODATA (stat_data);              /* full address stored */

                tpprintf (dma_dptrs [ch], TRACE_CSRW, "Control word 2 is %s address %05o\n",
                          (dma [ch].cw2 & DMA2_OI ? "input to" : "output from"),
                          dma [ch].cw2 & VAMASK);
                }
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


/* DMA reset */

static t_stat dma_reset (DEVICE *dptr)
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



/* DMA local utility routine declarations */


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

static t_stat dma_cycle (CHANNEL ch, ACCESS_CLASS class)
{
const uint32  dev   = dma [ch].cw1 & I_DEVMASK;          /* device select code */
const uint32  stc   = dma [ch].cw1 & DMA1_STC;           /* STC enable flag */
const uint32  bytes = dma [ch].cw1 & DMA1_PB;            /* pack bytes flag */
const uint32  clc   = dma [ch].cw1 & DMA1_CLC;           /* CLC enable flag */
const HP_WORD MA    = dma [ch].cw2 & VAMASK;             /* memory address */
const HP_WORD input = dma [ch].cw2 & DMA2_OI;            /* input flag */
const uint32  even  = dma [ch].packer & DMA_OE;          /* odd/even packed byte flag */
HP_WORD data;
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
    ioresult = io_dispatch (dev, signals, 0);           /* do I/O input */

    status = IOSTATUS (ioresult);                       /* get cycle status */

    if (status == SCPE_OK) {                            /* good I/O cycle? */
        data = IODATA (ioresult);                       /* extract return data value */

        if (bytes) {                                    /* byte packing? */
            if (even) {                                 /* second byte? */
                data = (uint16) (dma [ch].packer << 8)  /* merge stored byte */
                         | (data & DMASK8);
                mem_write (dma_dptrs [ch], class, MA, data);    /* store word data */
                }
            else                                        /* first byte */
                dma [ch].packer = (data & DMASK8);      /* save it */

            dma [ch].packer = dma [ch].packer ^ DMA_OE; /* flip odd/even bit */
            }
        else                                            /* no byte packing */
            mem_write (dma_dptrs [ch], class, MA, data);    /* store word data */
        }
    }

else {                                                  /* output cycle */
    if (bytes) {                                        /* byte packing? */
        if (even)                                       /* second byte? */
            data = dma [ch].packer & DMASK8;            /* retrieve it */

        else {                                          /* first byte */
            dma [ch].packer = mem_read (dma_dptrs [ch], class, MA);         /* read word data */
            data = (dma [ch].packer >> 8) & DMASK8;     /* get high byte */
            }

        dma [ch].packer = dma [ch].packer ^ DMA_OE;     /* flip odd/even bit */
        }
    else                                                /* no byte packing */
        data = mem_read (dma_dptrs [ch], class, MA);    /* read word data */

    ioresult = io_dispatch (dev, signals, data);        /* do I/O output */

    status = IOSTATUS (ioresult);                       /* get cycle status */
    }

if ((even || !bytes) && (status == SCPE_OK)) {          /* new byte or no packing and good xfer? */
    dma [ch].cw2 = input | (dma [ch].cw2 + 1) & VAMASK; /* increment address */
    dma [ch].cw3 = (dma [ch].cw3 + 1) & DMASK;          /* increment word count */

    if (dma [ch].cw3 == 0)                              /* end of transfer? */
        dmapio (dibs [DMA1 + ch], ioENF, 0);            /* set DMA channel flag */
    }

return status;                                          /* return I/O status */
}


/* Calculate DMA requests */

static uint32 calc_dma (void)
{
uint32 r = 0;

if (dma [ch1].xferen && SRQ (dma [ch1].cw1 & I_DEVMASK)) {  /* check DMA1 cycle */
    r = r | DMA_1_REQ;

    tprintf (dma1_dev, TRACE_SR, "Select code %02o asserted SRQ\n",
             dma [ch1].cw1 & I_DEVMASK);
    }

if (dma [ch2].xferen && SRQ (dma [ch2].cw1 & I_DEVMASK)) {  /* check DMA2 cycle */
    r = r | DMA_2_REQ;

    tprintf (dma2_dev, TRACE_SR, "Select code %02o asserted SRQ\n",
             dma [ch2].cw1 & I_DEVMASK);
    }

return r;
}



/* Memory Protect local SCP support routine declarations */


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

static uint32 protio (DIB *dibptr, IOCYCLE signal_set, uint32 stat_data)
{
uint16   data;
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

            mp_mem_changed = TRUE;                      /* set the MP/MEM registers changed flag */
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


/* Memory protect reset */

static t_stat mp_reset (DEVICE *dptr)
{
IOPRESET (&mp_dib);                                     /* PRESET device (does not use PON) */

mp_fence = 0;                                           /* clear fence register */
mp_viol = 0;                                            /* clear violation register */

mp_mem_changed = TRUE;                                  /* set the MP/MEM registers changed flag */

return SCPE_OK;
}



/* I/O system local utility routine declarations */


/* Initialize the I/O system.

   This routine is called in the instruction prelude to set up the I/O data
   structures prior to beginning execution.  It sets up two tables indexed by
   select code: one of DIB pointers, and the other of DEVICE pointers.  This
   allows fast access to the device interface routine by the I/O instruction
   executors and to the device trace flags, respectively.

   It also sets the interface priority, interrupt request, and service request
   bit vectors from the interface flip-flop values by calling the device
   interface routines.

   Finally, it sets the interrupt deferral table entries for the SFC and SFS
   signals.  These depend on the current CPU model, which may have been changed
   while the simulation was stopped.
*/

static void io_initialize (void)
{
DEVICE *dptr;
DIB    *dibptr;
uint32 i;

dev_prl [0] = dev_prl [1] = ~0u;                        /* set all priority lows */
dev_irq [0] = dev_irq [1] = 0;                          /* clear all interrupt requests */
dev_srq [0] = dev_srq [1] = 0;                          /* clear all service requests */

memset (&dibs [2], 0, sizeof dibs - 2 * sizeof dibs [0]);   /* clear the DIB pointer table */
memset (&devs [2], 0, sizeof devs - 2 * sizeof devs [0]);   /*   and the device table */

for (i = 0; sim_devices [i] != NULL; i++) {             /* loop through all of the devices */
    dptr = sim_devices [i];                             /* get a pointer to the device */
    dibptr = (DIB *) dptr->ctxt;                        /*   and to that device's DIB */

    if (dibptr && !(dptr->flags & DEV_DIS)) {           /* if the DIB exists and the device is enabled */
        devs [dibptr->select_code] = dptr;              /*   then set the device pointer into the device table */
        dibs [dibptr->select_code] = dibptr;            /*     and set the DIB pointer into the dispatch table */

        if (dibptr->select_code >= SIRDEV)              /* if this device receives SIR */
            dibptr->io_handler (dibptr, ioSIR, 0);      /*   then set the interrupt request state */
        }
    }

dibs [PWR] = &pwrf_dib;                                 /* for now, powerfail is always present */
devs [PWR] = &cpu_dev;                                  /*   and is controlled by the CPU */

if (dibs [DMA1]) {                                      /* if the first DMA channel is enabled */
    dibs [DMALT1] = &dmas1_dib;                         /*   then set up  */
    devs [DMALT1] = &dma1_dev;                          /*     the secondary device handler */
    }

if (dibs [DMA2]) {                                      /* if the second DMA channel is enabled */
    dibs [DMALT2] = &dmas2_dib;                         /*   then set up  */
    devs [DMALT2] = &dma2_dev;                          /*     the secondary device handler */
    }

defer_tab [soSFC] = is_1000;                            /* SFC and SFS defer */
defer_tab [soSFS] = is_1000;                            /*   for 1000-Series CPUs only */

return;
}


/* Device I/O signal dispatcher.

   This routine calls the I/O signal handler of the device corresponding to the
   supplied "select_code" value, passing the "signal_set" and inbound "data"
   values.  The combined status and outbound data value from the handler is
   returned to the caller.

   The 21xx/1000 I/O structure requires that no empty slots exist between
   interface cards.  This is due to the hardware priority chaining (PRH/PRL)
   that is passed from card-to-card.  If it is necessary to leave unused I/O
   slots, HP 12777A Priority Jumper Cards must be installed in them to maintain
   priority continuity.

   Under simulation, every unassigned I/O slot behaves as though a 12777A were
   resident.  In this configuration, I/O instructions addressed to one of these
   slots read the floating bus for LIA/B and MIA/B instructions or do nothing
   for all other instructions.


   Implementation notes:

    1. For select codes < 10 octal, an IOI signal reads the floating S-bus
       (high on the 1000, low on the 21xx).  For select codes >= 10 octal, an
       IOI reads the floating I/O bus (low on all machines).

    2. The last select code used is saved for use by the CPU I/O handler in
       detecting consecutive CLC 0 executions.
*/

static uint32 io_dispatch (uint32 select_code, IOCYCLE signal_set, HP_WORD data)
{
uint32 stat_data;

if (dibs [select_code] != NULL) {                           /* if the I/O slot is occupied */
    tpprintf (devs [select_code], TRACE_IOBUS, "Received data %06o with signals %s\n",
              data, fmt_bitset (signal_set, inbound_format));

    stat_data =                                             /*   then call the device interface */
      dibs [select_code]->io_handler (dibs [select_code],   /*     with the indicated signals and write value */
                                      signal_set,
                                      IORETURN (SCPE_OK, data));

    tpprintf (devs [select_code], TRACE_IOBUS, "Returned data %06o with signals %s\n",
              IODATA (stat_data), fmt_bitset (stat_data, outbound_format));

    last_select_code = select_code;                         /* save the select code for CLC 0 detection */

    if (stat_data & ioSKF)                                  /* if the interface asserted SKF */
        stat_data = IORETURN (NOTE_SKIP, 0);                /*   then notify the caller to increment P */
    }

else if (signal_set & ioIOI)                                /* otherwise if it is an input request */
    if (select_code < VARDEV && is_1000)                    /*   then if it is an internal device of a 1000 CPU */
        stat_data = IORETURN (STOP (cpu_ss_unsc), DMASK);   /*     then the empty slot reads as all ones */
    else                                                    /*   otherwise */
        stat_data = IORETURN (STOP (cpu_ss_unsc), 0);       /*     the empty slot reads as all zeros */

else                                                        /* otherwise */
    stat_data = IORETURN (STOP (cpu_ss_unsc), 0);           /*   the signal is ignored */

return stat_data;
}
