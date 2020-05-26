/* hp2100_cpu.c: HP 21xx/1000 Central Processing Unit simulator

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2019, J. David Bryan

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
                I/O subsystem
                Power Fail Recovery System

   12-Feb-19    JDB     Worked around idle problem (SIMH issue 622)
   06-Feb-19    JDB     Corrected trace report for simulation stop
   05-Feb-19    JDB     sim_dname now takes a const pointer
   13-Aug-18    JDB     Renamed "ion_defer" to "cpu_interrupt_enable" and flipped sense
   24-Jul-18    JDB     Removed unneeded "iotrap" parameter from "cpu_iog" routine
   20-Jul-18    JDB     Moved memory/MEM/MP and DMA into separate source files
   29-Jun-18    JDB     Fixed "clear_option" return type definition
   14-Jun-18    JDB     Renamed PRO device to MPPE
   05-Jun-18    JDB     Revised I/O model
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
         (02100-90001, December 1971)
     - Model 2100A Computer Installation and Maintenance Manual
         (02100-90002, August 1972)
     - HP 1000 M/E/F-Series Computers Technical Reference Handbook
         (5955-0282, March 1980)
     - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
         (92851-90001, March 1981)
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
   maximum of 32K words, divided into 1K-word pages.  Memory-referencing
   instructions in the base set can directly address the 1024 words of the base
   page (page 0) or the 1024 words of the current page (the page containing the
   instruction).  The instructions in the extended set directly address the
   32768 words in the full logical address space.  The A and B accumulators may
   be addressed as logical memory addresses 0 and 1, respectively.

   Peripheral devices are connected to the CPU by interface cards installed in
   the I/O card cages present in the CPU and optional I/O extender chassis.
   Each slot in the card cage is assigned an address, called a select code, that
   may be referenced by I/O instructions in the base set.  Select codes range
   from 0 to 77 octal, with the first eight select codes reserved for the
   system, providing connections for 56 possible interfaces.

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
   floating-point microcode adds six two-word (single-precision) instructions.
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
   number of optional microcode extensions are available with the M/E/F-Series.

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
   attempt to write or jump below the fence separating the two parts is
   inhibited, and an interrupt to the operating system occurs, which aborts the
   offending user program.  If the DMS option is enabled as well, protection is
   enhanced by specifying read and write permissions on a page-by-page basis.

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
      A     16    Accumulator (addressable as memory location 0)
      B     16    Accumulator (addressable as memory location 1)
      P     15    Program counter
      S     16    Switch and display register
      M     15    Memory address register
      T     16    Memory data register
      E      1    Extend flag (arithmetic carry out)
      O      1    Overflow flag (arithmetic overflow)

   In addition, there are two internal registers that are not visible to the
   programmer but are used by the hardware:

     Name  Width  Description
     ----  -----  ----------------------------------------------
      IR    16    Instruction register
     CIR     6    Central interrupt register

   The Instruction Register holds the current instruction, while the Central
   Interrupt Register holds the select code identifying an interrupting device.

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

   The 2116 offers two hardware options that extended the instruction set.  The
   first is the 12579A Extended Arithmetic Unit.  The second is the 2152A
   Floating Point Processor, which is interfaced through, and therefore
   requires, the EAU.  The EAU adds 10 instructions including integer multiply
   and divide and double-word loads, stores, shifts, and rotates.  The FPP adds
   30 floating-point arithmetic, trigonometric, logarithmic, and exponential
   instructions.  The 2116 EAU is compatible with the 2100 and 1000 EAU
   implementations and is provided by the simulator.  The 2116 FPP is unique
   to that machine and is not simulated.

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

   The Memory-Reference Group instructions are encoded as follows:

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
     -----  --------  ---------------------------------------------
     0010     AND     A = A & M [MA]
     0011     JSB     M [MA] = P, P = MA + 1
     0100     XOR     A = A ^ M [MA]
     0101     JMP     P = MA
     0110     IOR     A = A | M [MA]
     0111     ISZ     M [MA] = M [MA] + 1, P = P + 2 if M [MA] == 0
     1000     ADA     A = A + M [MA]
     1001     ADB     B = B + M [MA]
     1010     CPA     P = P + 2 if A != M [MA]
     1011     CPB     P = P + 2 if B != M [MA]
     1100     LDA     A = M [MA]
     1101     LDB     B = M [MA]
     1110     STA     M [MA] = A
     1111     STB     M [MA] = B

   Bits 15 and 10 encode the type of access, as follows:

     15  10   Access Type            Action
     --- ---  ---------------------  -----------------------------
      0   0   base page direct       MA = IR <9:0>
      0   1   current page direct    MA = P <14:10> | IR <9:0>
      1   0   base page indirect     MA = M [IR <9:0>]
      1   1   current page indirect  MA = M [P <14:10> | IR <9:0>]


   The Shift-Rotate Group instructions are encoded as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | R | 0 | E |   op 1    | C | E | S |   op 2    |  SRG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = A/B register (0/1)
     E = disable/enable op
     C = CLE
     S = SL*

   Bits 8-6 and 2-0 each encode one of eight operations, as follows:

     Op N  Operation
     ----  ---------------------------
     000   Arithmetic left shift
     001   Arithmetic right shift
     010   Rotate left
     011   Rotate right
     100   Shift left and clear sign
     101   Rotate right through Extend
     110   Rotate left through Extend
     111   Rotate left four bits


   The Alter-Skip Group instructions are encoded as follows:

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

   Bits 9-8 and 7-6 each encode one of three operations, as follows:

     9-8  Operation
     ---  ------------------------
     01   Clear A/B
     10   Complement A/B
     11   Clear and complement A/B

     7-6  Operation
     ---  ---------------------------
     01   Clear Extend
     10   Complement Extend
     11   Clear and complement Extend


   The Input-Output Group instructions are encoded as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | R | 1 | H |  I/O op   |      select code      |  IOG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = A/B register (0/1)
     H = hold/clear flag (0/1)

   There are ten instructions.  Six are encoded directly by bits 8-6, with
   bits 11 and 9 assuming the definitions above.  The other four are encoded
   with bits 11 and 9 differentiating, as follows:

     11    9   8-6  Operation
     ---  ---  ---  ---------------------------
      x    H   000  Halt
      x    0   001  Set the flag flip-flop
      x    1   001  Clear the flag flip-flop
      x    H   010  Skip if the flag is clear
      x    H   011  Skip if the flag is set
      R    H   100  Merge input data into A/B
      R    H   101  Load input data into A/B
      R    H   110  Store output data from A/B
      0    H   111  Set the control flip-flop
      1    H   111  Clear the control flip-flop

   An I/O group instruction controls the device specified by the select code.
   Depending on the opcode, the instruction may set or clear the device flag,
   start or stop I/O, or read or write data.


   The Macroinstruction Group instructions are encoded with bits 15-12 and 10 as
   1 000 0.  Bits 11 and 9-0 determine the specific EAU or UIG instruction.

   The Extended Arithmetic Group instructions are encoded as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | op| 0 |   operation   | 0   0   0   0   0   0 |  EAG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Operations:

     11   9-6   Operation
     ---  ----  -----------------------------------------------------
      0   0010  Multiply 16 x 16 = 32-bit product
      0   0100  Divide 32 / 16 = 16-bit quotient and 16-bit remainder
      1   0010  Double load A and B registers from memory
      1   0100  Double store A and B registers to memory

   All other encodings are undefined.

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 0 | 0 |    shift/rotate op    |  shift count  |  EAG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Operations:

      9-4    Operation
     ------  --------------------------------------------------
     100001  Arithmetic shift right A and B registers 1-16 bits
     000001  Arithmetic shift left A and B registers 1-16 bits
     100010  Logical shift right A and B registers 1-16 bits
     000010  Logical shift left A and B registers 1-16 bits
     100100  Rotate right A and B registers 1-16 bits
     000100  Rotate left A and B registers 1-16 bits

   The shift count encodes the number of bits shifted, with a count of zero
   representing a shift of 16 bits.


   The User Instruction Group instructions are encoded as follows:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | R | 0   1 |      module       |   operation   |  UIG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = A/B register (0/1)

   Bits 8-4 encode the microcode module containing the instructions, and bits
   3-0 encode the specific instructions.  See the individual UIG instruction
   simulator source files for the specific encodings used.


   I/O device interfaces and their connected devices are simulated by
   substituting software states for I/O backplane signals.  The set of signals
   generated by I/O instructions and DMA cycles is dispatched to the target
   interface simulator for action, and the set of signals asserted or denied in
   response is returned from the call.  Backplane signals are processed
   sequentially.  For example, the "STC sc,C" instruction generates the "set
   control" and the "clear flag" signals that are processed in that order.

   The HP 21xx/1000 interrupt structure is based on the PRH, PRL, IEN, IRQ, and
   IAK signals.  PRH indicates that no higher-priority device is interrupting.
   PRL indicates to lower-priority devices that a given device is not
   interrupting.  IEN asserts when the interrupt system is enabled.  IRQ
   indicates that a given device is requesting an interrupt.  IAK indicates that
   the given device's interrupt request is being acknowledged.

   Typical I/O interfaces have a flag buffer, a flag, and a control flip-flop.
   If an interface's flag buffer, flag, and control flip-flops are all set, the
   interrupt system is enabled, and the interface has the highest priority on
   the interrupt chain, it requests an interrupt by asserting the IRQ signal.
   When the interrupt is acknowledged with the IAK signal, the flag buffer is
   cleared, preventing further interrupt requests from that interface.  The
   combination of flag set and control set blocks interrupts from lower priority
   devices.

   Service requests are used to trigger the DMA service logic.  Setting the
   interface flag flip-flop typically asserts SRQ, although SRQ may be
   calculated independently.

   Most of the I/O signals are generated by execution of IOG instructions or DMA
   cycles; the target interface simulator is called explicitly to respond to the
   signal assertions.  However, two hardware signals (ENF and SIR) are periodic,
   and a direct simulation would call each interface simulator with these
   signals after each machine instruction.  Instead, the interface simulator is
   called only when these signals would have an effect on the state of the
   interface.  Also, two signals (IEN and PRH) are combinatorial, in that
   changing either potentially affects all interfaces in the system.  These are
   handled efficiently by saving the states of each interface in bit vectors, as
   described below.

   PRH and PRL form a hardware priority chain that extends from interface to
   interface on the backplane.  For an interface to generate an interrupt, PRH
   must be asserted by the next-higher-priority interface.  When an interface
   generates an interrupt, it denies PRL to the next-lower-priority interface.
   When an interface is not interrupting, it passes PRH to PRL unaltered.  This
   means that a given interface can interrupt only if all higher-priority
   devices are receiving PRH asserted and are asserting PRL.  It also means that
   clearing the interrupt on a given interface, i.e., reasserting PRL, may
   affect all lower-priority interfaces.

   As an example, assume that the interface at select code 10 ("SC 10") has its
   flag buffer, flag, and control flip-flops set, that IEN and PRH are asserted,
   and that no other interfaces have their flag flip-flops set.  SC 10 will
   assert IRQ and deny PRL.  SC 11 sees its PRH low (because PRL 10 is connected
   to PRH 11) and so denies PRL, and this action ripples through all of the
   lower-priority interfaces.

   Then, while the interrupt for SC 10 is being serviced, the interface at
   select code 17 sets its flag buffer, flag, and control flip-flops.  SC 17 is
   inhibited from interrupting by PRH denied.

   When the interrupt service routine clears the interrupt by clearing the flag
   buffer and flag flip-flops, SC 10 reasserts PRL to SC 11, and that signal
   ripples through the interfaces at select codes 12-16, arriving at SC 17 as
   PRH assertion.  With PRH asserted, SC 17 generates an interrupt and denies
   PRL to SC 20 and above.

   A direct simulation of this hardware behavior would require calling all
   lower-priority interface simulators in ascending select code (and therefore
   priority) order with the PRH signal and checking for an asserted IRQ signal
   or a denied PRL signal.  This is inefficient.

   To avoid making a potentially long sequence of calls, each interface
   simulator returns a "conditional IRQ" signal and a "conditional PRL" signal
   in addition to the standard IRQ and PRL signals.  The conditional signals
   are those that would result if the higher-priority interface is asserting
   PRH and the interrupt system is on.  So, for instance, an interface simulator
   with its flag buffer, flag, and control flip-flops set will assert its
   conditional IRQ signal and deny its conditional PRL signal.  If PRH and IEN
   are asserted, then its "real" IRQ and PRL signals are also asserted and
   denied respectively.

   For fast assertion checking, the conditional IRQ and PRL signal states are
   kept in bit vectors, which are updated after each interface simulator call.
   Each vector is represented as a two-element array of 32-bit unsigned
   integers, forming a 64-bit vector in which bits 0-31 of the first element
   correspond to select codes 00-37 octal, and bits 0-31 of the second element
   correspond to select codes 40-77 octal.  The "interrupt_request_set" array
   holds conditional IRQ states, and the "priority_holdoff_set" array holds the
   complement of the conditional PRL states (the complement is used to simplify
   the priority calculation).  These vectors permit rapid determination of an
   interrupting interface when a higher-priority interface reasserts PRL or when
   the interrupt system is reenabled.


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

     >>CPU   reg: P **** 01535  040013    A 123003, B 001340, X 000000, Y 000000, e O I
                  ~ ~~~~ ~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |   |    |       |         |
                  |   |    |       |         +-- A, B, X, Y, E, O, interrupt system registers
                  |   |    |       |             (lower/upper case = 0/1 or off/on)
                  |   |    |       +------------ S register
                  |   |    +-------------------- MEM fence
                  |   +------------------------- (place holder)
                  +----------------------------- protection state (P/- protected/unprotected)

     >>CPU   reg: P **** *****  ******    MPF 00000, MPV 000000, MES 000000, MEV 000000
                  ~ ~~~~ ~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |   |    |       |         |
                  |   |    |       |         +-- memory protect fence and violation registers
                  |   |    |       |             memory expansion status and violation registers
                  |   |    |       +------------ (place holder)
                  |   |    +-------------------- (place holder)
                  |   +------------------------- (place holder)
                  +----------------------------- protection state (P/- protected/unprotected)



     >>CPU  opnd: . **** 36002  101475    return location is P+3 (error EM21)
     >>CPU  opnd: . **** 22067  105355    entry is for a dynamic mapping violation
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

    2. The Power Fail option is not currently implemented.

    3. Idling with 4.x simulator framework versions after June 14, 2018 (git
       commit ID d3986466) loses TBG ticks.  This causes the DOS/RTE/TSB system
       clock to run slowly, losing about one second per minute.  A workaround is
       to prevent "sim_interval" from going negative on return from "sim_idle".
       Issue 622 on the SIMH site describes the problem.
*/



#include <setjmp.h>

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"



/* Lost time workaround */

#if (SIM_MAJOR >= 4)
  #define sim_idle(timer,decrement) \
            if (sim_idle (timer, decrement) == TRUE     /* [workaround] idle the simulator; if idling occurred */ \
              && sim_interval < 0)                      /* [workaround]   and the time interval is negative */ \
                sim_interval = 0                        /* [workaround]     then reset it to zero */
#endif



/* CPU program constants */

/* Alter-Skip Group instruction register fields */

#define IR_CMx              0001000u            /* CMA/B */
#define IR_CLx              0000400u            /* CLA/B */
#define IR_CME              0000200u            /* CME */
#define IR_CLE              0000100u            /* CLE */
#define IR_SEZ              0000040u            /* SEZ */
#define IR_SSx              0000020u            /* SSA/B */
#define IR_SLx              0000010u            /* SLA/B */
#define IR_INx              0000004u            /* INA/B */
#define IR_SZx              0000002u            /* SZA/B */
#define IR_RSS              0000001u            /* RSS */

#define IR_SSx_SLx_RSS      (IR_SSx | IR_SLx | IR_RSS)          /* a special case */
#define IR_ALL_SKIPS        (IR_SEZ | IR_SZx | IR_SSx_SLx_RSS)  /* another special case */

/* Shift-Rotate Group instruction register micro-ops */

#define IR_xLS              0000000u            /* ALS/BLS */
#define IR_xRS              0000001u            /* ARS/BRS */
#define IR_RxL              0000002u            /* RAL/RBL */
#define IR_RxR              0000003u            /* RAR/RBR */
#define IR_xLR              0000004u            /* ALR/BLR */
#define IR_ERx              0000005u            /* ERA/ERB */
#define IR_ELx              0000006u            /* ELA/ELB */
#define IR_xLF              0000007u            /* ALF/BLF */

#define SRG_DIS             0000000u            /* micro-op disable */
#define SRG1_EN             0000010u            /* micro-op 1 enable */
#define SRG2_EN             0000020u            /* micro-op 2 enable */

/* Instruction register masks */

#define IR_MRG              (MRG | AB_MASK)     /* MRG instructions mask */
#define IR_MRG_I            (IR_MRG | IR_IND)   /* MRG indirect instructions mask */

#define IR_JSB              0014000u            /* JSB instruction */
#define IR_JSB_I            (IR_JSB | IR_IND)   /* JSB,I instruction */
#define IR_JMP              0024000u            /* JMP instruction */

#define IR_HLT_MASK         0172700u            /* I/O group mask for HLT[,C] instruction */
#define IR_CLC_MASK         0176700u            /* I/O group mask for a CLC[,C] instruction */

#define IR_HLT              0102000u            /* HLT instruction */
#define IR_CLC              0106700u            /* CLC instruction */


/* CPU unit flags and accessors.

      31  30  29  28  27  26  25  24  23  22  21  20  19  18  17  16
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | r | - | G | V | O | E | D | F | M | I | P | U |   CPU model   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     r = reserved
     G = SIGNAL/1000 firmware is present
     V = Vector Instruction Set firmware is present
     O = RTE-6/VM VMA and OS firmware is present
     E = RTE-IV EMA firmware is present
     D = Double Integer firmware is present
     F = Fast FORTRAN Processor firmware is present
     M = Dynamic Mapping System firmware is present
     I = 2000 I/O Processor firmware is present
     P = Floating Point hardware or firmware is present
     U = Extended Arithmetic Unit is present
*/

#define UNIT_MODEL_SHIFT    (UNIT_V_UF + 0)     /* bits 0- 3: CPU model (OPTION_ID value) */
#define UNIT_OPTION_SHIFT   (UNIT_V_UF + 4)     /* bits 4-15: CPU options installed */

#define UNIT_MODEL_MASK     0000017u            /* model ID mask */
#define UNIT_OPTION_MASK    0003777u            /* option ID mask */

#define UNIT_MODEL_FIELD    (UNIT_MODEL_MASK  << UNIT_MODEL_SHIFT)
#define UNIT_OPTION_FIELD   (UNIT_OPTION_MASK << UNIT_OPTION_SHIFT)

#define UNIT_MODEL(f)       ((f) >> UNIT_MODEL_SHIFT & UNIT_MODEL_MASK)
#define UNIT_OPTION(f)      ((f) >> UNIT_OPTION_SHIFT & UNIT_OPTION_MASK)

#define TO_UNIT_OPTION(id)  (1u << UNIT_OPTION_SHIFT + (id) - Option_BASE - 1)

/* Unit models */

#define UNIT_2116           (Option_2116   << UNIT_MODEL_SHIFT)
#define UNIT_2115           (Option_2115   << UNIT_MODEL_SHIFT)
#define UNIT_2114           (Option_2114   << UNIT_MODEL_SHIFT)
#define UNIT_2100           (Option_2100   << UNIT_MODEL_SHIFT)
#define UNIT_1000_M         (Option_1000_M << UNIT_MODEL_SHIFT)
#define UNIT_1000_E         (Option_1000_E << UNIT_MODEL_SHIFT)
#define UNIT_1000_F         (Option_1000_F << UNIT_MODEL_SHIFT)

/* Unit options */

#define UNIT_EAU            TO_UNIT_OPTION (Option_EAU)
#define UNIT_FP             TO_UNIT_OPTION (Option_FP)
#define UNIT_IOP            TO_UNIT_OPTION (Option_IOP)
#define UNIT_DMS            TO_UNIT_OPTION (Option_DMS)
#define UNIT_FFP            TO_UNIT_OPTION (Option_FFP)
#define UNIT_DBI            TO_UNIT_OPTION (Option_DBI)
#define UNIT_EMA            TO_UNIT_OPTION (Option_EMA)
#define UNIT_VMAOS          TO_UNIT_OPTION (Option_VMAOS)
#define UNIT_VIS            TO_UNIT_OPTION (Option_VIS)
#define UNIT_SIGNAL         TO_UNIT_OPTION (Option_SIGNAL)
#define UNIT_DS             TO_UNIT_OPTION (Option_DS)

#define UNIT_EMA_VMA        (UNIT_EMA | UNIT_VMAOS)

/* Unit conversions to CPU options */

#define TO_CPU_MODEL(f)     (CPU_OPTION) (1u << UNIT_MODEL (f))
#define TO_CPU_OPTION(f)    (CPU_OPTION) (UNIT_OPTION (f) << CPU_OPTION_SHIFT)

/* "Pseudo-option" flags used only for option testing; never set into the UNIT structure */

#define UNIT_V_PFAIL        (UNIT_V_UF - 1)                 /* Power fail is installed */
#define UNIT_V_DMA          (UNIT_V_UF - 2)                 /* DMA is installed */
#define UNIT_V_MP           (UNIT_V_UF - 3)                 /* Memory protect is installed */

#define UNIT_PFAIL          (1 << UNIT_V_PFAIL)
#define UNIT_DMA            (1 << UNIT_V_DMA)
#define UNIT_MP             (1 << UNIT_V_MP)


/* CPU global SCP data definitions */

REG *sim_PC = NULL;                             /* the pointer to the P register */


/* CPU global data structures */


/* CPU registers */

HP_WORD ABREG [2] = { 0, 0 };                   /* A and B registers */

HP_WORD PR  = 0;                                /* P register */
HP_WORD SR  = 0;                                /* S register */
HP_WORD MR  = 0;                                /* M register */
HP_WORD TR  = 0;                                /* T register */
HP_WORD XR  = 0;                                /* X register */
HP_WORD YR  = 0;                                /* Y register */

uint32  E   = 0;                                /* E register */
uint32  O   = 0;                                /* O register */

HP_WORD IR  = 0;                                /* Instruction Register */
HP_WORD CIR = 0;                                /* Central Interrupt Register */
HP_WORD SPR = 0;                                /* 1000 Stack Pointer Register / 2100 F Register */


/* CPU global state */

FLIP_FLOP      cpu_interrupt_enable  = SET;     /* interrupt enable flip-flop */
uint32         cpu_pending_interrupt = 0;       /* pending interrupt select code or zero if none */

t_stat         cpu_ss_unimpl   = SCPE_OK;       /* status return for unimplemented instruction execution */
t_stat         cpu_ss_undef    = SCPE_OK;       /* status return for undefined instruction execution */
t_stat         cpu_ss_unsc     = SCPE_OK;       /* status return for I/O to an unassigned select code */
t_stat         cpu_ss_ioerr    = SCPE_OK;       /* status return for an unreported I/O error */
t_stat         cpu_ss_inhibit  = SCPE_OK;       /* CPU stop inhibition mask */
UNIT           *cpu_ioerr_uptr = NULL;          /* pointer to a unit with an unreported I/O error */

HP_WORD        err_PR         = 0;              /* error PC */
uint16         pcq [PCQ_SIZE] = { 0 };          /* PC queue (must be 16-bits wide for REG array entry) */
uint32         pcq_p          = 0;              /* PC queue pointer */
REG            *pcq_r         = NULL;           /* PC queue register pointer */

CPU_OPTION_SET cpu_configuration;               /* the current CPU option set and model */
uint32         cpu_speed = 1;                   /* the CPU speed, expressed as a multiplier of a real machine */


/* CPU local state.


   Implementation notes:

    1. The "is_1000" variable is used to index into tables where the row
       selected depends on whether or not the CPU is a 1000 M/E/F-series model.
       For logical tests that depend on this, it is faster (by one x86 machine
       instruction) to test the "cpu_configuration" variable for the presence of
       one of the three 1000 model flags.
*/

static jmp_buf   abort_environment;             /* microcode abort environment */

static FLIP_FLOP interrupt_system  = CLEAR;     /* interrupt system */
static uint32    interrupt_request = 0;         /* the currently interrupting select code or zero if none */

static uint32    interrupt_request_set [2] = { 0, 0 };  /* device interrupt request bit vector */
static uint32    priority_holdoff_set  [2] = { 0, 0 };  /* device priority holdoff bit vector */

static uint32    exec_mask        = 0;          /* the current instruction execution trace mask */
static uint32    exec_match       = D16_UMAX;   /* the current instruction execution trace matching value */
static uint32    indirect_limit   = 16;         /* the indirect chain length limit */

static t_bool    is_1000          = FALSE;      /* TRUE if the CPU is a 1000 M/E/F-Series */
static t_bool    mp_is_present    = FALSE;      /* TRUE if Memory Protect is present */
static uint32    last_select_code = 0;          /* the last select code sent over the I/O backplane */
static HP_WORD   saved_MR         = 0;          /* the M-register value between SCP commands */

static DEVICE    *loader_rom [4]  = { NULL };   /* the four boot loader ROM sockets in a 1000 CPU */


/* CPU local data structures */


/* CPU features table.

   The feature table is used to validate CPU feature changes within the subset
   of features supported by a given CPU.  Features in the typical list are
   enabled when the CPU model is selected.  If a feature appears in the typical
   list but NOT in the optional list, then it is standard equipment and cannot
   be disabled.  If a feature appears in the optional list, then it may be
   enabled or disabled as desired by the user.
*/

typedef struct {                                /* CPU model feature table */
    uint32      typ;                            /*   standard features plus typically configured options */
    uint32      opt;                            /*   complete list of optional features */
    uint32      maxmem;                         /*   maximum configurable memory in 16-bit words */
    } FEATURE_TABLE;

static const FEATURE_TABLE cpu_features [] = {          /* CPU features indexed by OPTION_ID */
  { UNIT_DMA | UNIT_MP,                                 /*   Option_2116 */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP | UNIT_EAU,
    32 * 1024
    },

  { UNIT_DMA,                                           /*   Option_2115 */
    UNIT_PFAIL | UNIT_DMA | UNIT_EAU,
    8 * 1024
    },

  { UNIT_DMA,                                           /*   Option_2114 */
    UNIT_PFAIL | UNIT_DMA,
    16 * 1024
    },

  { UNIT_PFAIL | UNIT_MP | UNIT_DMA | UNIT_EAU,         /*   Option_2100 */
    UNIT_DMA   | UNIT_FP | UNIT_IOP | UNIT_FFP,
    32 * 1024
    },

  { UNIT_MP | UNIT_DMA | UNIT_EAU | UNIT_FP | UNIT_DMS, /*   Option_1000_M */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DS,
    1024 * 1024
    },

  { UNIT_MP | UNIT_DMA | UNIT_EAU | UNIT_FP | UNIT_DMS, /*   Option_1000_E */
    UNIT_PFAIL | UNIT_DMA | UNIT_MP  | UNIT_DMS |
    UNIT_IOP   | UNIT_FFP | UNIT_DBI | UNIT_DS  | UNIT_EMA_VMA,
    1024 * 1024
    },

  { UNIT_MP  | UNIT_DMA | UNIT_EAU | UNIT_FP |          /*   Option_1000_F */
    UNIT_FFP | UNIT_DBI | UNIT_DMS,
    UNIT_PFAIL | UNIT_DMA | UNIT_MP     | UNIT_DMS |
    UNIT_VIS   | UNIT_DS  | UNIT_SIGNAL | UNIT_EMA_VMA,
    1024 * 1024
    }
  };


/* CPU local SCP support routine declarations */

static INTERFACE cpu_interface;
static INTERFACE ovf_interface;
static INTERFACE pwr_interface;

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
static t_stat show_cage  (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_exec  (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_speed (FILE *st, UNIT *uptr, int32 val, CONST void *desc);


/* CPU local utility routine declarations */

static t_stat  mrg_address         (void);
static HP_WORD srg_uop             (HP_WORD value, HP_WORD operation);
static t_stat  machine_instruction (t_bool int_ack, uint32 *idle_save);
static t_bool  reenable_interrupts (void);


/* CPU SCP data structures */


/* Device information blocks */

static DIB cpu_dib = {                          /* CPU (select code 0) */
    &cpu_interface,                             /*   the device's I/O interface function pointer */
    CPU,                                        /*   the device's select code (02-77) */
    0,                                          /*   the card index */
    NULL,                                       /*   the card description */
    NULL                                        /*   the ROM description */
    };

static DIB ovfl_dib = {                         /* Overflow (select code 1) */
    &ovf_interface,                             /*   the device's I/O interface function pointer */
    OVF,                                        /*   the device's select code (02-77) */
    0,                                          /*   the card index */
    NULL,                                       /*   the card description */
    NULL                                        /*   the ROM description */
    };

static DIB pwrf_dib = {                         /* Power Fail (select code 4) */
    &pwr_interface,                             /*   the device's I/O interface function pointer */
    PWR,                                        /*   the device's select code (02-77) */
    0,                                          /*   the card index */
    NULL,                                       /*   the card description */
    NULL                                        /*   the ROM description */
    };


/* Unit list.

   The CPU unit "capac" field is set to the current main memory capacity in
   16-bit words by the "set_size" utility routine.  The CPU does not use a unit
   event service routine.
*/

static UNIT cpu_unit [] = {
    { UDATA (NULL, UNIT_FIX | UNIT_BINK, 0) }
    };


/* Register list.

   The CPU register list exposes the machine registers for user inspection and
   modification.


   Implementation notes:

    1. All registers that reference variables of type HP_WORD must have the
       REG_FIT flag for proper access if HP_WORD is a 16-bit type.

    2. The REG_X flag indicates that the register may be displayed in symbolic
       form.

    3. The T register cannot be modified.  To change a memory location value,
       the DEPOSIT CPU command must be used.
*/

static REG cpu_reg [] = {
/*    Macro   Name     Location            Radix  Width   Offset       Depth                Flags       */
/*    ------  -------  ------------------  -----  -----  --------  -----------------  ----------------- */
    { ORDATA (P,       PR,                         15)                                                  },
    { ORDATA (A,       AR,                         16),                               REG_X             },
    { ORDATA (B,       BR,                         16),                               REG_X             },
    { ORDATA (M,       MR,                         15)                                                  },
    { ORDATA (T,       TR,                         16),                               REG_RO | REG_X    },
    { ORDATA (X,       XR,                         16),                               REG_X             },
    { ORDATA (Y,       YR,                         16),                               REG_X             },
    { ORDATA (S,       SR,                         16),                               REG_X             },
    { FLDATA (E,       E,                                   0)                                          },
    { FLDATA (O,       O,                                   0)                                          },
    { ORDATA (CIR,     CIR,                         6)                                                  },

    { FLDATA (INTSYS,  interrupt_system,                    0)                                          },
    { FLDATA (INTEN,   cpu_interrupt_enable,                0)                                          },

    { ORDATA (IOPSP,   SPR,                        16)                                                  },
    { BRDATA (PCQ,     pcq,                  8,    15,             PCQ_SIZE),         REG_CIRC | REG_RO },

    { ORDATA (IR,      IR,                         16),                               REG_HRO           },
    { ORDATA (SAVEDMR, saved_MR,                   32),                               REG_HRO           },
    { ORDATA (PCQP,    pcq_p,                       6),                               REG_HRO           },

    { ORDATA (EMASK,   exec_mask,                  16),                               REG_HRO           },
    { ORDATA (EMATCH,  exec_match,                 16),                               REG_HRO           },
    { DRDATA (ILIMIT,  indirect_limit,             16),                               REG_HRO           },
    { ORDATA (FWANXM,  mem_end,                    32),                               REG_HRO           },
    { ORDATA (CONFIG,  cpu_configuration,          32),                               REG_HRO           },
    { BRDATA (ROMS,    loader_rom,           8,    32,                 4),            REG_HRO           },

    { FLDATA (IS1000,  is_1000,                             0),                       REG_HRO           },
    { ORDATA (INTREQ,  interrupt_request,           6),                               REG_HRO           },
    { ORDATA (LASTSC,  last_select_code,            6),                               REG_HRO           },

    { ORDATA (WRU,     sim_int_char,                8),                               REG_HRO           },
    { ORDATA (BRK,     sim_brk_char,                8),                               REG_HRO           },
    { ORDATA (DEL,     sim_del_char,                8),                               REG_HRO           },

    { NULL }
    };


/* Modifier list.


   Implementation notes:

    1. The 21MX monikers are deprecated in favor of the 1000 designations.  See
       the "HP 1000 Series Naming History" on the back inside cover of the
       Technical Reference Handbook.

    2. The string descriptors are used by the "show_model" routine to print the
       CPU model numbers prior to appending "loader enabled" or "loader
       disabled" to the report.

    3. Each CPU option requires three modifiers.  The two regular modifiers
       control the setting and printing of the option, while the extended
       modifier controls clearing the option.  The latter is necessary because
       the option must be checked before confirming the change, and so the
       option value must be passed to the validation routine.
*/

static MTAB cpu_mod [] = {
/*    Mask Value        Match Value  Print String  Match String  Validation     Display      Descriptor        */
/*    ----------------  -----------  ------------  ------------  -------------  -----------  ----------------- */
    { UNIT_MODEL_FIELD, UNIT_2116,   "",           "2116",       &set_model,    &show_model, (void *) "2116"   },
    { UNIT_MODEL_FIELD, UNIT_2115,   "",           "2115",       &set_model,    &show_model, (void *) "2115"   },
    { UNIT_MODEL_FIELD, UNIT_2114,   "",           "2114",       &set_model,    &show_model, (void *) "2114"   },
    { UNIT_MODEL_FIELD, UNIT_2100,   "",           "2100",       &set_model,    &show_model, (void *) "2100"   },
    { UNIT_MODEL_FIELD, UNIT_1000_E, "",           "1000-E",     &set_model,    &show_model, (void *) "1000-E" },
    { UNIT_MODEL_FIELD, UNIT_1000_M, "",           "1000-M",     &set_model,    &show_model, (void *) "1000-M" },

#if defined (HAVE_INT64)
    { UNIT_MODEL_FIELD, UNIT_1000_F, "",           "1000-F",     &set_model,    &show_model, (void *) "1000-F" },
#endif

    { UNIT_MODEL_FIELD, UNIT_1000_M, NULL,         "21MX-M",     &set_model,    &show_model, (void *) "1000-M" },
    { UNIT_MODEL_FIELD, UNIT_1000_E, NULL,         "21MX-E",     &set_model,    &show_model, (void *) "1000-E" },

    { UNIT_EAU,         UNIT_EAU,    "EAU",        "EAU",        &set_option,   NULL,        NULL              },
    { UNIT_EAU,         0,           "no EAU",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_EAU,     NULL,        "NOEAU",      &clear_option, NULL,        NULL              },

    { UNIT_FP,          UNIT_FP,     "FP",         "FP",         &set_option,   NULL,        NULL              },
    { UNIT_FP,          0,           "no FP",      NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_FP,      NULL,        "NOFP",       &clear_option, NULL,        NULL              },

    { UNIT_IOP,         UNIT_IOP,    "IOP",        "IOP",        &set_option,   NULL,        NULL              },
    { UNIT_IOP,         0,           "no IOP",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_IOP,     NULL,        "NOIOP",      &clear_option, NULL,        NULL              },

    { UNIT_DMS,         UNIT_DMS,    "DMS",        "DMS",        &set_option,   NULL,        NULL              },
    { UNIT_DMS,         0,           "no DMS",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_DMS,     NULL,        "NODMS",      &clear_option, NULL,        NULL              },

    { UNIT_FFP,         UNIT_FFP,    "FFP",        "FFP",        &set_option,   NULL,        NULL              },
    { UNIT_FFP,         0,           "no FFP",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_FFP,     NULL,        "NOFFP",      &clear_option, NULL,        NULL              },

    { UNIT_DBI,         UNIT_DBI,    "DBI",        "DBI",        &set_option,   NULL,        NULL              },
    { UNIT_DBI,         0,           "no DBI",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_DBI,     NULL,        "NODBI",      &clear_option, NULL,        NULL              },

    { UNIT_EMA_VMA,     UNIT_EMA,    "EMA",        "EMA",        &set_option,   NULL,        NULL              },
    { MTAB_XDV,         UNIT_EMA,     NULL,        "NOEMA",      &clear_option, NULL,        NULL              },

    { UNIT_EMA_VMA,     UNIT_VMAOS,  "VMA",        "VMA",        &set_option,   NULL,        NULL              },
    { MTAB_XDV,         UNIT_VMAOS,   NULL,        "NOVMA",      &clear_option, NULL,        NULL              },

    { UNIT_EMA_VMA,     0,           "no EMA/VMA", NULL,         &set_option,   NULL,        NULL              },

#if defined (HAVE_INT64)
    { UNIT_VIS,         UNIT_VIS,    "VIS",        "VIS",        &set_option,   NULL,        NULL              },
    { UNIT_VIS,         0,           "no VIS",     NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_VIS,     NULL,        "NOVIS",      &clear_option, NULL,        NULL              },

    { UNIT_SIGNAL,      UNIT_SIGNAL, "SIGNAL",     "SIGNAL",     &set_option,   NULL,        NULL              },
    { UNIT_SIGNAL,      0,           "no SIGNAL",  NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_SIGNAL,  NULL,        "NOSIGNAL",   &clear_option, NULL,        NULL              },
#endif

/* Future microcode support.
    { UNIT_DS,          UNIT_DS,     "DS",         "DS",         &set_option,   NULL,        NULL              },
    { UNIT_DS,          0,           "no DS",      NULL,         NULL,          NULL,        NULL              },
    { MTAB_XDV,         UNIT_DS,      NULL,        "NODS",       &clear_option, NULL,        NULL              },
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
    { MTAB_XDV | MTAB_NMO,      0,      "IOCAGE",     NULL,            NULL,          &show_cage,     NULL       },

    { MTAB_XDV | MTAB_NMO,      1,      "STOPS",      "STOP",          &set_stops,    &show_stops,    NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOSTOP",        &set_stops,    NULL,           NULL       },
    { MTAB_XDV | MTAB_NMO,      2,      "INDIR",      "INDIR",         &set_stops,    &show_stops,    NULL       },

    { MTAB_XDV | MTAB_NMO,      1,      "EXEC",       "EXEC",          &set_exec,     &show_exec,     NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOEXEC",        &set_exec,     NULL,           NULL       },

    { MTAB_XDV | MTAB_NMO,      0,      "SPEED",      NULL,            NULL,          &show_speed,    NULL       },

    { 0 }
    };


/* Trace list */

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
    cpu_unit,                                   /* unit array */
    cpu_reg,                                    /* register array */
    cpu_mod,                                    /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    PA_WIDTH,                                   /* address width */
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



/* I/O subsystem local data structures */


/* Signal names */

static const BITSET_NAME inbound_names [] = {   /* Inbound signal names, in INBOUND_SIGNAL order */
    "PON",                                      /*   000000000001 */
    "IOI",                                      /*   000000000002 */
    "IOO",                                      /*   000000000004 */
    "SFS",                                      /*   000000000010 */
    "SFC",                                      /*   000000000020 */
    "STC",                                      /*   000000000040 */
    "CLC",                                      /*   000000000100 */
    "STF",                                      /*   000000000200 */
    "CLF",                                      /*   000000000400 */
    "EDT",                                      /*   000000001000 */
    "CRS",                                      /*   000000002000 */
    "POPIO",                                    /*   000000004000 */
    "IAK",                                      /*   000000010000 */
    "ENF",                                      /*   000000020000 */
    "SIR",                                      /*   000000040000 */
    "IEN",                                      /*   000000100000 */
    "PRH"                                       /*   000000200000 */
    };

static const BITSET_FORMAT inbound_format =     /* names, offset, direction, alternates, bar */
    { FMT_INIT (inbound_names, 0, lsb_first, no_alt, no_bar) };


static const BITSET_NAME outbound_names [] = {  /* Outbound signal names, in OUTBOUND_SIGNAL order */
    "SKF",                                      /*   000000000001 */
    "PRL",                                      /*   000000000002 */
    "FLG",                                      /*   000000000004 */
    "IRQ",                                      /*   000000000010 */
    "SRQ"                                       /*   000000000020 */
    };

static const BITSET_FORMAT outbound_format =    /* names, offset, direction, alternates, bar */
    { FMT_INIT (outbound_names, 0, lsb_first, no_alt, no_bar) };


/* I/O signal tables.

   These tables contain the set of I/O signals that are appropriate for each I/O
   operation.  Two tables are defined.

   The first table defines the backplane signals generated by each I/O Group
   operation.  A signal set is sent to the device interface associated with the
   select code specified in an IOG instruction to direct the operation of the
   interface.  Backplane signals map closely to IOG instructions.  For example,
   the SFS instruction asserts the SFS backplane signal to the interface card.

   The hardware ENF and SIR signals are periodic.  In simulation, they are
   asserted only when the flag buffer or the flag and control flip-flops are
   changed, respectively.

   The second table defines the signals generated in addition to the explicitly
   asserted signal.  Specifically, asserting PON also asserts POPIO and CRS,
   and asserting POPIO also asserts CRS.  The ENF and SIR signals are asserted
   when necessary as described above.
*/

static const INBOUND_SET control_set [] = {     /* indexed by IO_GROUP_OP */
    ioNONE,                                     /*   iog_HLT */
    ioSTF  | ioENF | ioSIR,                     /*   iog_STF */
    ioSFC,                                      /*   iog_SFC */
    ioSFS,                                      /*   iog_SFS */
    ioIOI,                                      /*   iog_MIx */
    ioIOI,                                      /*   iog_LIx */
    ioIOO,                                      /*   iog_OTx */
    ioSTC          | ioSIR,                     /*   iog_STC */

    ioNONE | ioCLF | ioSIR,                     /*   iog_HLT_C */
    ioCLF          | ioSIR,                     /*   iog_CLF */
    ioSFC  | ioCLF | ioSIR,                     /*   iog_SFC_C */
    ioSFS  | ioCLF | ioSIR,                     /*   iog_SFS_C */
    ioIOI  | ioCLF | ioSIR,                     /*   iog_MIx_C */
    ioIOI  | ioCLF | ioSIR,                     /*   iog_LIx_C */
    ioIOO  | ioCLF | ioSIR,                     /*   iog_OTx_C */
    ioSTC  | ioCLF | ioSIR,                     /*   iog_STC_C */

    ioCLC          | ioSIR,                     /*   iog_CLC */
    ioCLC  | ioCLF | ioSIR                      /*   iog_CLC_C */
    };

static const INBOUND_SET assert_set [] = {      /* indexed by IO_ASSERTION */
    ioENF                             | ioSIR,  /*   ioa_ENF */
    ioSIR,                                      /*   ioa_SIR */
    ioPON   | ioPOPIO | ioCRS | ioENF | ioSIR,  /*   ioa_PON */
    ioPOPIO | ioCRS           | ioENF | ioSIR,  /*   ioa_POPIO */
    ioCRS                     | ioENF | ioSIR,  /*   ioa_CRS */
    ioIAK                             | ioSIR   /*   ioa_IAK */
    };


/* Interrupt enable table.

   I/O Group instructions that alter the interrupt priority chain must delay
   recognition of interrupts until the following instruction completes.  The HP
   1000 microcode does this by executing an IOFF micro-order to clear the
   interrupt enable (INTEN) flip-flop.  The table below gives the INTEN state
   for each I/O instruction; CLEAR corresponds to an IOFF micro-order, while SET
   corresponds to ION.  The table is also indexed by the "is_1000" selector, as
   the disable rules are different for the 21xx and 1000 machines.
*/

static const t_bool enable_map [2] [18] = {             /* interrupt enable table, indexed by is_1000 and IO_GROUP_OP */
/*    HLT    STF    SFC    SFS    MIx    LIx    OTx    STC   */
/*    HLT_C  CLF    SFC_C  SFS_C  MIx_C  LIx_C  OTx_C  STC_C */
/*    CLC    CLC_C                                           */
/*    -----  -----  -----  -----  -----  -----  -----  ----- */
    { CLEAR, CLEAR, SET,   SET,   SET,   SET,   SET,   CLEAR,   /* 21xx */
      CLEAR, CLEAR, SET,   SET,   SET,   SET,   SET,   CLEAR,   /* 21xx */
      CLEAR, CLEAR },

    { CLEAR, CLEAR, CLEAR, CLEAR, SET,   SET,   SET,   CLEAR,   /* 1000 */
      CLEAR, CLEAR, CLEAR, CLEAR, SET,   SET,   SET,   CLEAR,   /* 1000 */
      CLEAR, CLEAR }
    };


/* I/O access table.

   I/O signals are directed to specific interface cards by specifying their
   locations in the I/O card cage.  Each location is assigned a number, called a
   select code, that is specified by I/O Group instructions to indicate the
   interface card to activate.

   In simulation, the select code corresponding to each interface is stored in
   the corresponding Device Information Block (DIB).  To avoid having to scan
   the device list each time an I/O instruction is executed, an I/O access table
   is filled in as part of I/O initialization in the instruction execution
   prelude.  The table is indexed by select code (00-77 octal) and contains
   pointers to the device and DIB structures associated with each index.

   Initialization is performed during each "sim_instr" call, as the select code
   assignments may have been changed by the user at the SCP prompt.


   Implementation notes:

    1. The entries for select codes 0 and 1 (the CPU and Overflow Register,
       respectively) are initialized here, as they are always present (i.e.,
       cannot be disabled) and cannot change.

    2. The table contains constant pointers, but "const" cannot be used here, as
       "hp_trace" calls "sim_dname", which takes a variable device pointer even
       though it does not change anything.

    3. The "references" entries are used only during table initialization to
       ensure that each select code is referenced by only one device.
*/

typedef struct {                            /* I/O access table entry */
    DEVICE *devptr;                         /*   a pointer to the DEVICE structure */
    DIB    *dibptr;                         /*   a pointer to the DIB structure */
    uint32 references;                      /*   a count of the references to this select code */
    } IO_TABLE;

static IO_TABLE iot [SC_MAX + 1] = {         /* index by select code for I/O instruction dispatch */
    { &cpu_dev, &cpu_dib,  0 },              /*   select code 00 = interrupt system */
    { &cpu_dev, &ovfl_dib, 0 }               /*   select code 01 = overflow register */
    };


/* I/O subsystem local utility routine declarations */

static t_bool initialize_io (t_bool is_executing);



/* CPU global SCP support routines */


/* Execute CPU instructions.

   This is the instruction decode routine for the HP 21xx/1000 simulator.  It is
   called from the simulator control program (SCP) to execute instructions in
   simulated memory, starting at the simulated program counter.  It runs until
   the status to be returned is set to a value other than SCPE_OK.

   On entry, P points to the instruction to execute, and the "sim_switches"
   global contains any command-line switches included with the RUN command.  On
   exit, P points at the next instruction to execute.

   Execution is divided into four phases.

   First, the instruction prelude configures the simulation state to resume
   execution.  This involves verifying that there are no device conflicts (e.g.,
   two devices with the same select code) and initializing the I/O state.  These
   actions accommodate reconfiguration of the I/O device settings and program
   counter while the simulator was stopped.  The prelude also checks for one
   command-line switch: if "-B" is specified, the current set of simulation stop
   conditions is bypassed for the first instruction executed.

   Second, the memory protect option is initialized.  MP aborts utilize the
   "setjmp/longjmp" mechanism to transfer control out of the instruction
   executors without returning through the call stack.  This allows an
   instruction to be aborted part-way through execution when continuation is
   impossible due to a memory access violation.  An MP abort returns to the main
   instruction loop at the "setjmp" routine.

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


   The instruction execution loop starts by checking for event timer expiration.
   If one occurs, the associated device's service routine is called by the
   "sim_process_event" routine.  Then a check for DMA service requests is made.
   If a request is active, the "dma_service" routine is called to process it.

   DMA cycles are requested by an I/O card asserting its SRQ signal.  If a DMA
   channel is programmed to respond to that card's select code, the channel's
   service request flag is set in the "dma_request_set".  On each pass through
   the instruction execution loop, "dma_request_set" is checked; if it is
   non-zero, a DMA cycle will be initiated.  A DMA cycle consists of a memory
   cycle and an I/O cycle.  These cycles are synchronized with the control
   processor on the 21xx CPUs.  On the 1000s, memory cycles are asynchronous,
   while I/O cycles are synchronous.  Memory cycle time is about 40% of the I/O
   cycle time.

   With properly designed interface cards, DMA is capable of taking consecutive
   I/O cycles.  On all machines except the 1000 M-Series, a DMA cycle freezes
   the CPU for the duration of the cycle.  On the M-Series, a DMA cycle freezes
   the CPU if it attempts an I/O cycle (including IAK) or a directly-interfering
   memory cycle.  An interleaved memory cycle is allowed.  Otherwise, the
   control processor is allowed to run.  Therefore, during consecutive DMA
   cycles, the M-Series CPU will run until an IOG instruction is attempted,
   whereas the other CPUs will freeze completely.  This is simulated by skipping
   instruction execution if "dma_request_set" is still non-zero after servicing
   the current request, i.e., if the device asserted SRQ again as a result of
   the DMA cycle.

   All DMA cards except the 12607B provide two independent channels.  If both
   channels are active simultaneously, channel 1 has priority for I/O cycles
   over channel 2.

   Most I/O cards assert SRQ no more than 50% of the time.  A few buffered
   cards, such as the 12821A and 13175A Disc Interfaces, are capable of
   asserting SRQ continuously while filling or emptying the buffer.  If SRQ for
   channel 1 is asserted continuously when both channels are active, then no
   channel 2 cycles will occur until channel 1 completes.

   After DMA servicing, a check for pending interrupt requests is made.

   Interrupt recognition in the HP 1000 CPU is controlled by three state
   variables: "interrupt_system", "cpu_interrupt_enable", and
   "interrupt_request".  "interrupt_system" corresponds to the INTSYS flip-flop
   in the 1000 CPU, "cpu_interrupt_enable" corresponds to the INTEN flip-flop,
   and "interrupt_request" corresponds to the NRMINT flip-flop.  STF 00 and CLF
   00 set and clear INTSYS, turning the interrupt system on and off.  Microcode
   instructions ION and IOFF set and clear INTEN, enabling or disabling certain
   interrupts.  An IRQ signal from a device, qualified by the corresponding PRH
   and IEN signals, will set NRMINT to request a normal interrupt; an IOFF or
   IAK will clear it.

   Under simulation, "interrupt_system" is controlled by STF/CLF 00.
   "cpu_interrupt_enable" is set or cleared as appropriate by the individual
   instruction simulators.  "interrupt_request" is set to the successfully
   interrupting device's select code, or to zero if there is no qualifying
   interrupt request.

   The rules controlling interrupt recognition are:

    1. Power fail (SC 04) may interrupt if "cpu_interrupt_enable" is set; this
       is not conditional on "interrupt_system" being set.

    2. Memory protect (SC 05) may interrupt if "interrupt_system" is set; this
       is not conditional on "cpu_interrupt_enable" being set.

    3. Parity error (SC 05) may interrupt always; this is not conditional on
       either "interrupt_system" or "cpu_interrupt_enable" being set.

    4. All other devices (SC 06 and up) may interrupt only if both
      "interrupt_system" and "cpu_interrupt_enable" are set.

   Qualification with "interrupt_system" is performed by the I/O dispatcher,
   which asserts IEN to the device interface if the interrupt system is on.  All
   interfaces other than Power Fail or Parity Error assert IRQ only if IEN is
   asserted.  If IEN is denied, i.e., the interrupt system is off, then only
   Power Fail and Parity Error will assert IRQ and thereby set the
   "interrupt_request" value to their respective select codes.  Therefore, we
   need only qualify by "cpu_interrupt_enable" here.

   At instruction fetch time, a pending interrupt request will be deferred if
   the previous instruction was a JMP indirect, JSB indirect, STC, CLC, STF,
   CLF, or, for a 1000-series CPU, an SFS, SFC, JRS, DJP, DJS, SJP, SJS, UJP, or
   UJS.  The executors for these instructions clear the "cpu_interrupt_enable"
   flag, which is then set unilaterally when each instruction is dispatched.
   The flag is also cleared by an interrupt acknowledgement, deferring
   additional interrupts until after the instruction in the trap cell is
   executed.

   On the HP 1000, an interrupt request is always deferred until after the
   current instruction completes.  On the 21xx, the request is deferred unless
   the current instruction is an MRG instruction other than JMP or JMP,I or
   JSB,I.  Note that for the 21xx, SFS and SFC are not included in the deferral
   criteria.  In simulation, the "reenable_interrupts" routine is called to
   handle this case.


   When a status other than SCPE_OK is returned from an instruction executor or
   event service routine, the instruction execution loop exits into the
   instruction postlude.  The set of debug trace flags is restored if it had
   been changed by an active execution trace or idle trace suppression.  This
   ensures that the simulation stop does not exit with the flags set improperly.
   If the simulation stopped for a programmed halt, the 21xx binary loader area
   is protected in case it had been unprotected to run the loader.  The MEU
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

    2. The C standard requires that the "setjmp" call be made from a frame that
       is still active (i.e., still on the stack) when "longjmp" is called.
       Consequently, we must call "setjmp" from this routine rather than a
       subordinate routine that will have exited (to return to this routine)
       when the "longjmp" call is made.

    3. The -P switch is removed from the set of command line switches to ensure
       that internal calls to the device reset routines are not interpreted as
       "power-on" resets.

    4. A CPU freeze is simulated by skipping instruction execution during the
       current loop cycle.

    5. The 1000 M-Series allows some CPU processing concurrently with
       continuous DMA cycles, whereas all other CPUs freeze.  The processor
       freezes if an I/O cycle is attempted, including an interrupt
       acknowledgement.  Because some microcode extensions (e.g., Access IOP,
       RTE-6/VM OS) perform I/O cycles, advance detection of I/O cycles is
       difficult.  Therefore, we freeze all processing for the M-Series as well.

    6. EXEC tracing is active when exec_save is non-zero.  "exec_save" saves the
       current state of the trace flags when an EXEC trace match occurs.  For
       this to happen, at least TRACE_EXEC must be set, so "exec_save" will be
       set non-zero when a match is active.

    7. The execution trace (TRACE_EXEC) match test is performed in two parts to
       display the register values both before and after the instruction
       execution.  Consequently, the enable test is done before the register
       trace, and the disable test is done after.

    8. A simulation stop bypass is inactivated after the first instruction
       execution by the expedient of setting the stop inhibition mask to the
       execution status result.  This must be SCPE_OK (i.e., zero) for execution
       to continue, which removes the stop inhibition.  If a non-zero status
       value is returned, then the inhibition mask will be set improperly, but
       that is irrelevant, as execution will stop in this case.

    9. In hardware, the IAK signal is asserted to all devices in the I/O card
       cage, as well as to the Memory Protect card.  Only the interrupting
       device will respond to IAK.  In simulation, IAK is dispatched to the
       interrupting device and, if that device is not the MP card, then to the
       MP device as well.

   10. In hardware, execution of the instruction in the trap cell does not use
       the FTCH micro-order to avoid changing the MP violation register during
       an MP interrupt.  In simulation, we use a Fetch classification to label
       the access correctly in a trace listing.  This is OK because the Enable
       Violation Register flip-flop has already been reset if this is an MP
       interrupt, so the Fetch will not alter the MP VR.

   11. The "meu_assert_IAK" routine sets the "meu_indicator" and "meu_page"
       values for the P register before switching to the system map.  Therefore,
       on return, they indicate the prior map and page in use when the interrupt
       occurred.
*/

t_stat sim_instr (void)
{
static uint32 exec_save;                                /* the trace flag settings saved by an EXEC match */
static uint32 idle_save;                                /* the trace flag settings saved by an idle match */
MICRO_ABORT abort_reason;
t_bool      exec_test;                                  /* set after setjmp */
t_bool      interrupt_acknowledge;                      /* set after setjmp */
t_stat      status;                                     /* set after setjmp */


/* Instruction prelude */

if (sim_switches & SWMASK ('B'))                        /* if a simulation stop bypass was requested */
    cpu_ss_inhibit = SS_INHIBIT;                        /*   then inhibit stops for the first instruction only */
else                                                    /* otherwise */
    cpu_ss_inhibit = SCPE_OK;                           /*   clear the inhibition mask */

sim_switches &= ~SWMASK ('P');                          /* clear the power-on switch to prevent interference */

if (initialize_io (TRUE) == FALSE)                      /* set up the I/O table; if there's a select code conflict */
    return SCPE_STOP;                                   /*   then inhibit execution */

mp_is_present = mp_initialize ();                       /* set up memory protect */

exec_save = 0;                                          /* clear the EXEC match */
idle_save = 0;                                          /*   and idle match trace flags */

cpu_ioerr_uptr = NULL;                                  /* clear the I/O error unit pointer */


/* Microcode abort processor */

abort_reason = (MICRO_ABORT) setjmp (abort_environment);    /* set the microcode abort handler */

switch (abort_reason) {                                 /* dispatch on the abort reason */

    case Memory_Protect:                                /* a memory protect abort */
        status = SCPE_OK;                               /*   continues execution with FLG 5 asserted */
        break;


    case Interrupt:                                     /* an interrupt in an indirect chain */
        PR = err_PR;                                    /*   backs out of the instruction */
        status = SCPE_OK;                               /*     and then continues execution to service the interrupt */
        break;


    case Indirect_Loop:                                 /* an indirect chain length exceeding the limit */
        status = STOP_INDIR;                            /*   causes a simulator stop */
        break;


    default:                                            /* anything else */
        status = SCPE_OK;                               /*   continues execution */
        break;
    }

exec_test = FALSE;                                      /* clear the execution test flag */


/* Instruction execution loop */

while (status == SCPE_OK) {                             /* execute until simulator status prevents continuation */

    err_PR = PR;                                        /* save P for error recovery */

    if (sim_interval <= 0) {                            /* if an event is pending */
        status = sim_process_event ();                  /*   then call the event service */

        if (status != SCPE_OK)                          /* if the service failed */
            break;                                      /*   then stop execution */
        }

    if (dma_request_set) {                              /* if a DMA service request is pending */
        dma_service ();                                 /*   then service the active channel(s) */

        if (dma_request_set)                            /* if a DMA request is still pending */
            continue;                                   /*   then service it before instruction execution */
        }

    if (interrupt_request                                       /* if an interrupt request is pending */
      && (cpu_interrupt_enable || reenable_interrupts ())) {    /*   and is enabled or reenabled */
        if (sim_brk_summ                                        /*     then if any breakpoints are defined */
          && sim_brk_test (interrupt_request,                   /*       and an unconditional breakpoint */
                           SWMASK ('E')                         /*         or a breakpoint matching */
                             | meu_breakpoint_type (TRUE))) {   /*           the current MEM map is set */
            status = STOP_BRKPNT;                               /*             then stop simulation */
            break;
            }

        CIR = (HP_WORD) interrupt_request;              /* set the CIR to the select code of the interrupting device */
        interrupt_request = 0;                          /*   and then clear the request */

        cpu_interrupt_enable = CLEAR;                   /* inhibit interrupts */
        interrupt_acknowledge = TRUE;                   /*   while in an interrupt acknowledge cycle */

        if (idle_save != 0) {                           /* if idle loop tracing is suppressed */
            cpu_dev.dctrl = idle_save;                  /*   then restore the saved trace flag set */
            idle_save = 0;                              /*     and indicate that we are out of the idle loop */
            }

        meu_assert_IAK ();                              /* assert IAK to the MEM to switch to the system map */

        tprintf (cpu_dev, TRACE_INSTR, DMS_FORMAT "interrupt\n",
                 meu_indicator, meu_page, PR, CIR);

        io_assert (iot [CIR].devptr, ioa_IAK);          /* acknowledge the interrupt */

        if (CIR != MPPE)                                /* if the MP is not interrupting */
            io_assert (iot [MPPE].devptr, ioa_IAK);     /*   then notify MP of the IAK too */

        IR = ReadF (CIR);                               /* fetch the trap cell instruction */
        }

    else {                                              /* otherwise this is a normal instruction execution */
        interrupt_acknowledge = FALSE;                  /*   so clear the interrupt acknowledgement status */

        if (sim_brk_summ                                        /* if any breakpoints are defined */
          && sim_brk_test (PR, SWMASK ('E')                     /*   and an unconditional breakpoint or a */
                             | meu_breakpoint_type (FALSE))) {  /*     breakpoint matching the current map is set */
            status = STOP_BRKPNT;                               /*       then stop simulation */
            break;
            }

        IR = ReadF (PR);                                /* fetch the instruction */
        PR = PR + 1 & LA_MASK;                          /*   and point at the next memory location */

        cpu_interrupt_enable = SET;                     /* enable interrupts */
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

        if (cpu_dev.dctrl & TRACE_REG)                  /* if register tracing is enabled */
            mem_trace_registers (interrupt_system);     /*   then output the working and MP/MEM registers */

        if (cpu_dev.dctrl & TRACE_EXEC                  /* if execution tracing is enabled */
          && exec_save != 0                             /*   and is currently active */
          && ! exec_test) {                             /*     and the matching test fails */
            cpu_dev.dctrl = exec_save;                  /*       then restore the saved debug flag set */
            exec_save = 0;                              /*         and indicate that tracing is disabled */

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

    status = machine_instruction (interrupt_acknowledge,    /* execute one machine instruction */
                                  &idle_save);

    if (status == NOTE_INDINT) {                        /* if an interrupt was recognized while resolving indirects */
        PR = err_PR;                                    /*   then back out of the instruction */
        status = SCPE_OK;                               /*     to service the interrupt */
        }

    cpu_ss_inhibit = status;                            /* clear the simulation stop inhibition mask */
    }                                                   /*   and continue the instruction execution loop */


/* Instruction postlude */

if (interrupt_request                                   /* if an interrupt request is pending */
  && (cpu_interrupt_enable || reenable_interrupts ()))  /*   and is enabled or reenabled */
    cpu_pending_interrupt = interrupt_request;          /*     then report the pending interrupt select code */
else                                                    /*   otherwise */
    cpu_pending_interrupt = 0;                          /*     report that no interrupt is pending */

if (exec_save != 0) {                                   /* if EXEC tracing is active */
    cpu_dev.dctrl = exec_save;                          /*   then restore the saved trace flag set */
    hp_trace (&cpu_dev, TRACE_EXEC, EXEC_FORMAT "\n");  /*     and add a separator to the trace log */
    }

else if (idle_save != 0)                                /* otherwise if idle tracing is suppressed */
    cpu_dev.dctrl = idle_save;                          /*   then restore the saved trace flag set */

saved_MR = MR;                                          /* save the current M value to detect a user change */

if (status == STOP_HALT)                                /* if this is a programmed halt */
    set_loader (NULL, FALSE, NULL, NULL);               /*   then disable the 21xx loader */

else if (status <= STOP_RERUN)                          /* otherwise if this is a simulation stop */
    PR = err_PR;                                        /*   then restore P to reexecute the instruction */

meu_update_status ();                                   /* update the MEM status register */
meu_update_violation ();                                /*   and the violation register */

pcq_r->qptr = pcq_p;                                    /* update the PC queue pointer */

sim_brk_dflt = meu_breakpoint_type (FALSE);             /* base the default breakpoint type on the current MEM state */

tprintf (cpu_dev, cpu_dev.dctrl,
         DMS_FORMAT "simulation stop: %s\n",
         meu_indicator, meu_page, MR, TR,
         status >= SCPE_BASE ? sim_error_text (status)
                             : sim_stop_messages [status]);

return status;                                          /* return the status code */
}


/* VM command post-processor.

   This routine is called from SCP after every command before returning to the
   command prompt.  We use it to update the T (memory data) register whenever
   the M (memory address) register is changed to follow the 21xx/1000 CPU
   hardware action.


   Implementation notes:

    1. The T register must be changed only when M has changed.  Otherwise, if T
       is updated after every command, then T will be set to zero if M points
       into the protected loader area of the 21xx machines, e.g., after a HLT
       instruction in the loader reenables loader protection.
*/

void cpu_post_cmd (t_bool from_scp)
{
if (MR != saved_MR) {                                   /* if M has changed since the last update */
    saved_MR = MR;                                      /*   then save the new M value */
    TR = mem_fast_read (MR, Current_Map);               /*     and set T to the contents of the addressed location */
    }

return;
}



/* CPU global utility routines */


/* Execute an I/O instruction.

   This routine executes the I/O Group instruction that is passed in the
   "instruction" parameter.  The instruction is examined to obtain the I/O
   function desired.  A memory protect check is then made to determine if it is
   legal to execute the instruction.  If it is, the state of the interrupt
   enable flip-flop is set, the set of I/O backplane signals generated by the
   instruction are picked up from the "control_set" array, and the signals and
   inbound data value are dispatched to the device interface indicated by the
   select code contained in the instruction.  The data value and signals
   returned from the interface are used as directed by the I/O operation, and
   the status of the operation is returned.


   Implementation notes:

    1. The STC and CLC instructions share the same pattern in bits 9-6 that are
       used to decode the rest of the IOG instructions.  These instructions are
       differentiated by the A/B selector (bit 11).

    2. If a memory protect violation occurs, the IOG signal from the CPU to the
       I/O backplane is denied to disable all I/O signals.  For a LIA/B or MIA/B
       instruction, this will load or merge the value of the floating I/O bus
       into the A or B registers.  This value is zero on all machines.  Merging
       zero doesn't change the register value, so the only the LIA/B case must
       be explicitly checked.
*/

t_stat cpu_iog (HP_WORD instruction)
{
const uint32 select_code = instruction & SC_MASK;       /* device select code */
const uint32 ab_selector = AB_SELECT (instruction);     /* A/B register selector */
IO_GROUP_OP  micro_op;
HP_WORD      inbound_value;
INBOUND_SET  inbound_signals;
SKPF_DATA    outbound;

if ((instruction & IR_CLC_MASK) == IR_CLC)              /* if the instruction is CLC or CLC,C */
    if (instruction & IR_HCF)                           /*   then if the H/C flag bit is set */
        micro_op = iog_CLC_C;                           /*     then it's a CLC,C operation */
    else                                                /*   otherwise */
        micro_op = iog_CLC;                             /*     it's a CLC operation */
else                                                    /* otherwise */
    micro_op = IOG_OP (instruction);                    /*   the operation is decoded directly */

if (micro_op == iog_LIx || micro_op == iog_LIx_C)       /* if executing an LIA or LIB instruction */
    ABREG [ab_selector] = 0;                            /*   then clear the register value in case MP aborts */

mp_check_io (select_code, micro_op);                    /* check for a memory protect violation */

cpu_interrupt_enable = enable_map [is_1000] [micro_op]; /* disable interrupts depending on the instruction */

inbound_signals = control_set [micro_op];               /* get the set of signals to assert to the interface */

if (micro_op == iog_OTx || micro_op == iog_OTx_C)       /* if the instruction is OTA/B or OTA/B,C */
    inbound_value = ABREG [ab_selector];                /*   then send the register value to the interface */
else                                                    /* otherwise */
    inbound_value = 0;                                  /*   the interface won't use the inbound value */

outbound = io_dispatch (select_code, inbound_signals,   /* dispatch the I/O action to the interface */
                        inbound_value);

if (micro_op == iog_LIx || micro_op == iog_LIx_C)       /* if the instruction is LIA/B or LIA/B,C */
    ABREG [ab_selector] = outbound.data;                /*   then store the I/O bus data into the register */

else if (micro_op == iog_MIx || micro_op == iog_MIx_C)  /* otherwise if the instruction is MIA/B or MIA/B,C */
    ABREG [ab_selector] |= outbound.data;               /*   then merge the I/O bus data into the register */

else if (outbound.skip) {                               /* otherwise if the interface asserted SKF */
    PR = PR + 1 & R_MASK;                               /*   then bump P to skip then next instruction */
    return SCPE_OK;                                     /*     and return success */
    }

else if (micro_op == iog_HLT || micro_op == iog_HLT_C)  /* otherwise if the instruction is HLT or HLT,C */
    return STOP_HALT;                                   /*   then stop the simulator */

if (iot [select_code].devptr == NULL)                   /* if the I/O slot is empty */
    return STOP (cpu_ss_unsc);                          /*   then return stop status if enabled */
else                                                    /* otherwise */
    return SCPE_OK;                                     /*   the instruction executed successfully */
}


/* Resolve an indirect address.

   This routine resolves a possibly indirect memory address into a direct
   address by following an indirect chain, if any.  On entry, the M register
   contains the address to resolve, and the "interruptible" parameter is set to
   TRUE if the instruction is interruptible or FALSE if it is not.  On exit, the
   M register contains the direct address, and SCPE_OK is returned.  If an
   interrupt is pending and permitted, NOTE_INDINT is returned to abort the
   instruction.  If the indirect chain length is greater than the chain limit,
   STOP_INDIR is returned to abort execution.  In both abort cases, the M
   register content is undefined.

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

   Detection of interrupts is dependent on the Interrupt Enable flip-flop being
   set.  Certain instructions, such as JMP indirect, JSB indirect, and most IOG
   instructions, clear the enable flag to hold off interrupts until the current
   and following instructions complete, including complete resolution of the
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
       pending interrupts with the Interrupt Enable flip-flop clear are serviced
       immediately (jumper removed) or after three levels of indirection (jumper
       installed).  If the jumper is removed, MP must be enabled (control
       flip-flop set) for the interrupt disable to be overridden.

       The jumper state need not be checked here, however, because this routine
       can be entered with an interrupt pending, i.e., "interrupt_request" is
       non-zero, only if "cpu_interrupt_enable" and "mp_reenable_interrupts" are
       both false.  If either is true, the pending interrupt would have been
       serviced before calling the instruction executor that caleld this routine
       to resolve its address.  For "mp_reenable_interrupts" to return false,
       the INT jumper must be installed or the MP control flip-flop must be
       clear.

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

    6. In hardware, all instructions that resolve indirects are interruptible.
       In simulation, some instruction executors are not written to handle an
       instruction abort (e.g., the P register is not backed up properly to
       rerun the instruction); these will pass FALSE for the "interruptible"
       parameter.
*/

t_stat cpu_resolve_indirects (t_bool interruptible)
{
uint32 level;
t_bool pending;

if (MR & IR_IND) {                                      /* if the address is indirect */
    MR = ReadW (MR & LA_MASK);                          /*   then follow the chain (first level) */

    if (MR & IR_IND) {                                  /* if the address is still indirect */
        pending = interruptible && interrupt_request;   /*   then see if an interrupt request is pending */

        if (pending && cpu_interrupt_enable)            /* if it's pending and enabled */
            return NOTE_INDINT;                         /*   then service the interrupt now */
        else                                            /* otherwise */
            pending = pending && mp_is_present;         /*   a pending interrupt is recognized only if MP is present */

        for (level = 2; MR & IR_IND; level++) {         /* follow the chain from level 2 until the address is direct */
            if (level > indirect_limit)                 /* if the limit is exceeded */
                return STOP_INDIR;                      /*   then stop the simulator */

            else if (pending)                           /* otherwise if an interrupt is pending */
                if (level == 3)                         /*   then if this is the third level */
                    cpu_interrupt_enable = SET;         /*     then reenable interrupts */
                else if (level == 4)                    /*   otherwise if this is the fourth level */
                    return NOTE_INDINT;                 /*     then service the interrupt now */

            MR = ReadW (MR & LA_MASK);                  /* follow the address chain */
            }
        }
    }

return SCPE_OK;
}


/* Abort an instruction.

   This routine performs a microcode abort for the reason passed in the
   "abort_reason" parameter.  In hardware, microcode aborts are implemented by
   direct jumps to the FETCH label in the base set (microcode address 0).  This
   is typically done when an interrupt is detected while executing a lengthy
   instruction or during resolution of an indirect chain.  In simulation, this
   is performed by a "longjmp" back to the start of the instruction execution
   loop.  It is the caller's responsibility to restore the machine state, e.g.,
   to back up the P register to rerun the instruction.
*/

void cpu_microcode_abort (MICRO_ABORT abort_reason)
{
longjmp (abort_environment, (int) abort_reason);        /* jump back to the instruction execution loop */
}


/* Install a bootstrap loader into memory.

   This routine copies the bootstrap loader specified by "boot" into the last 64
   words of main memory, limited by a 32K memory size.  If "sc" contains the
   select code of an I/O interface (i.e., select code 10 or above), this routine
   will configure the I/O instructions in the loader to the supplied select
   code.  On exit, the P register will be set to point at the loader starting
   program address, and the S register will be altered as directed by the
   "sr_clear" and "sr_set" masks if the current CPU is a 1000.  The updated P
   register value is returned to the caller.

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
   the call by returning a starting address of zero to the caller.  In this
   case, neither P nor S are changed.

   If I/O configuration is requested, each instruction in the loader array is
   examined as it is copied.  If the instruction is a non-HLT I/O instruction
   referencing a select code >= 10, the select code will be reset by subtracting
   10 and adding the value of the select code supplied by the "sc" parameter (or
   the paper-tape reader select code, as above).  This permits configuration of
   loaders that address two- or three-card interfaces.  Passing an "sc" value of
   0 will inhibit configuration, and the loader array will be copied verbatim.

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

   The starting address for loader execution is derived from the "start_index"
   field and the starting memory address to which the loader is copied.

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

uint32 cpu_copy_loader (const LOADER_ARRAY boot, uint32 sc, HP_WORD sr_clear, HP_WORD sr_set)
{
uint32      index, loader_start, ptr_sc;
MEMORY_WORD loader [IBL_SIZE];
MEMORY_WORD word;
DEVICE      *ptr_dptr;

if (boot [is_1000].start_index == IBL_NA)               /* if the bootstrap is not defined for the current CPU */
    return 0;                                           /*   then reject the command */

else if (boot [is_1000].start_index > 0 && sc > 0) {    /* if this is a two-part loader with I/O reconfiguration */
    ptr_dptr = find_dev ("PTR");                        /*   then get a pointer to the paper tape reader device */

    if (ptr_dptr == NULL)                               /* if the PTR device is not present */
        return 0;                                       /*   then something is seriously wrong */
    else                                                /* otherwise */
        ptr_sc = ((DIB *) ptr_dptr->ctxt)->select_code; /*   get the select code from the device's DIB */
    }

else                                                    /* otherwise this is a single-part loader */
    ptr_sc = 0;                                         /*   or I/O reconfiguration is not requested */

loader_start = mem_size - 1 & ~IBL_MASK & LA_MASK;          /* get the base memory address of the loader */
PR = loader_start + boot [is_1000].start_index & R_MASK;    /*   and store the starting program address in P */

set_loader (NULL, TRUE, NULL, NULL);                    /* enable the loader (ignore errors if not 21xx) */

for (index = 0; index < IBL_SIZE; index++) {            /* copy the bootstrap loader to memory */
    word = boot [is_1000].loader [index];               /* get the next word */

    if (sc == 0)                                        /* if reconfiguration is not requested */
        loader [index] = word;                          /*   then copy the instruction verbatim */

    else if (IOGOP (word)                                           /* otherwise if this is an I/O instruction */
      && (word & SC_MASK) >= SC_VAR                                 /*   and the referenced select code is >= 10B */
      && (word & IR_HLT_MASK) != IR_HLT)                            /*   and it's not a halt instruction */
        if (index < boot [is_1000].start_index)                     /*   then if this is a split loader */
            loader [index] = word + (ptr_sc - SC_VAR) & DV_MASK;    /*     then reconfigure the paper tape reader */
        else                                                        /*   otherwise */
            loader [index] = word + (sc - SC_VAR) & DV_MASK;        /*     reconfigure the target device */

    else if (index == boot [is_1000].dma_index)             /* otherwise if this is the DMA configuration word */
        loader [index] = word + (sc - SC_VAR) & DV_MASK;    /*   then reconfigure the target device */

    else if (index == boot [is_1000].fwa_index)         /* otherwise if this is the starting address word */
        loader [index] = NEG16 (loader_start);          /*   then set the negative starting address of the bootstrap */

    else                                                /* otherwise the word is not a special one */
        loader [index] = word;                          /*   so simply copy it */
    }

mem_copy_loader (loader, loader_start, To_Memory);      /* copy the loader to memory */

if (cpu_configuration & CPU_1000)                       /* if the CPU is a 1000 */
    SR = SR & sr_clear | sr_set | IBL_TO_SC (sc);       /*   then modify the S register as indicated */

return PR;                                              /* return the starting execution address of the loader */
}


/* Check for an I/O stop condition.

   Entering the SET CPU STOP=IOERR command at the SCP command prompt stops the
   simulator if an I/O error occurs on a device that does not return error
   status to the CPU.  For example, while the paper tape punch returns low- or
   out-of-tape status, the paper tape reader gives no indication that a tape is
   loaded.  If the reader is commanded to read when no tape is mounted, the
   interface hangs while waiting for the handshake with the device to complete.

   In hardware, the CPU can detect this condition only by timing the operation
   and concluding that the tape is missing if the timeout is exceeded.  However,
   if an IOERR stop has been set, then the simulator will stop with an error
   message to permit the condition to be fixed.  For instance, attempting to
   read from the paper tape reader with no paper tape image file attached will
   print "No tape loaded in the PTR device" and will stop the simulator.

   Such devices will call this routine and pass a pointer to the unit that
   encountered the error condition.  The routine returns TRUE and saves the
   pointer to the failing unit if the IOERR stop is enabled, and FALSE
   otherwise.
*/

t_bool cpu_io_stop (UNIT *uptr)
{
if (cpu_ss_ioerr != SCPE_OK) {                          /* if the I/O error stop is enabled */
    cpu_ioerr_uptr = uptr;                              /*   then save the failing unit */
    return TRUE;                                        /*     and return TRUE to indicate that the stop is enabled */
    }

else                                                    /* otherwise */
    return FALSE;                                       /*   return FALSE to indicate that the stop is disabled */
}



/* I/O subsystem global utility routines */


/* Device I/O signal dispatcher.

   This routine calls the I/O interface handler of the device corresponding to
   the supplied "select_code" value, passing the "inbound_signals" and
   "inbound_value" to the interface.  The combined skip status and outbound data
   value from the handler is returned to the caller.

   The 21xx/1000 I/O structure requires that no empty slots exist between
   interface cards.  This is due to the hardware priority chaining (PRH/PRL)
   that is passed from card-to-card.  If it is necessary to leave unused I/O
   slots, HP 12777A Priority Jumper Cards must be installed in them to maintain
   priority continuity.

   Under simulation, every unassigned I/O slot behaves as though a 12777A were
   resident.  In this configuration, I/O instructions addressed to one of these
   slots read the floating bus for LIA/B and MIA/B instructions or do nothing
   for all other instructions.

   If the slot is occupied, then the routine first determines the rank (0 or 1)
   and bit (0 to 31) corresponding to the select code of the interface.  These
   will be used to access the interrupt, priority, and service request bit
   vectors.  Then it augments the supplied inbound signals set with IEN if the
   interrupt system flip-flop is set, and PRH if no higher priority interface is
   denying PRL.

   In hardware, PRH of each interface is connected to PRL of the next higher
   priority (lower select code) interface, so that a PRH-to-PRL chain is formed.
   An interface receives PRH if every higher-priority interface is asserting
   PRL.  When an interface denies PRL, each lower-priority interface has PRH
   denied and so denies PRL to the next interface in the chain.

   To avoid checking calling each device simulator's higher-priority interface
   routine to ascertain its PRL status, the priority holdoff bit vector is
   checked.  This vector has a bit set for each interface currently denying PRL.
   The check is made as in the following example:

     sc bit      :  ...0 0 1 0 0 0 0 0 0 0 0 0   (dispatching to SC 11)
     sc bit - 1  :  ...0 0 0 1 1 1 1 1 1 1 1 1   (PRH required thru SC 10)
     pri holdoff :  ...0 0 1 0 0 1 0 0 0 0 0 0   (PRL denied for SC 06 and 11)
     ANDed value :  ...0 0 0 0 0 1 0 0 0 0 0 0   (PRL blocked at SC 06)

   If the ANDed value is 0, then there are no higher priority devices in this
   rank that are denying PRL.  If the rank is 0, then PRH is asserted to the
   current select code.  If the rank is 1, then PRH is asserted only if all rank
   0 priority holdoff bits are zero, i.e., if no rank 0 interfaces are denying
   PRL.

   The device interface handler is obtained from the "dibs" array, and the
   interface is called to process the inbound signals.

   On return, the outbound signals from the interface are examined.  The
   interrupt vector bit is set to the state of the IRQ signal, and the priority
   vector bit is set to the state of the PRL signal.

   If the IRQ signal is present, then the interface is the highest priority
   device requesting an interrupt (otherwise the interface would not have
   received PRH, which is a term in asserting IRQ), so the select code of the
   interrupting device is set.  Otherwise, if the interface asserted its PRL
   condition from a prior denied state, then lower priority devices are checked
   to see if any now have a valid interrupt request.

   If the SRQ signal is asserted, a DMA request is made; if the device is under
   DMA control, a DMA cycle will be initiated at the start of the next pass
   through the instruction execution loop.  Finally, the skip condition is set
   if the SKF signal is asserted, and the skip state and outbound value from the
   interface are returned to the caller.


   Implementation notes:

    1. For select codes < 10 octal, an IOI signal reads the floating S-bus
       (high on the 1000, low on the 21xx).  For select codes >= 10 octal, an
       IOI reads the floating I/O bus (low on all machines).

    2. The last select code used is saved for use by the CPU I/O handler in
       detecting consecutive CLC 0 executions.

    3. The IRQ and FLG signals always assert together on HP 21xx/1000 machines.
       They are physically tied together on all interface cards, and the CPUs
       depend on their concurrent assertion for correct operation.  In
       simulation, we check only IRQ from the interface, although FLG will be
       asserted as well.

    4. The IRQ and SRQ signals usually assert together, which means that an
       interrupt is pending before the DMA request is serviced.  However, DMA
       cycles always assert CLF, which clears the interrupt request before
       interrupts are checked.  In hardware, an SRQ to an active channel asserts
       DMALO (DMA lock out), which inhibits IRQ from setting the interrupt
       request flip-flop until the DMA cycle completes.  In simulation, IRQ is
       never asserted because the CLF is processed by the interface simulator
       before IRQ is determined.

    5. An interface will assert "cnVALID" if the conditional PRL and IRQ were
       determined.  If "cnVALID" is not asserted by the interface, then the
       states of the "cnPRL" and "cnIRQ" signals cannot be inferred from their
       presence or absence in the outbound signal set.

    6. The "cnVALID" pseudo-signal is required because although most interfaces
       determine the PRL and IRQ states in response to an SIR assertion, not all
       do.  In particular, the 12936A Privileged Interrupt Fence determines PRL
       in response to an IOO signal.
*/

SKPF_DATA io_dispatch (uint32 select_code, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
SKPF_DATA     result;
SIGNALS_VALUE outbound;
uint32        sc_rank, sc_bit, previous_holdoff;

if (iot [select_code].dibptr == NULL) {                 /* if the I/O slot is empty */
    result.skip = FALSE;                                /*   then SKF cannot be asserted */

    if (inbound_signals & ioIOI && select_code < SC_VAR /* if this is an input request for an internal device */
      && cpu_configuration & CPU_1000)                  /*   of a 1000 CPU */
        result.data = D16_UMAX;                         /*     then the empty slot reads as all ones */
    else                                                /* otherwise */
        result.data = 0;                                /*   the empty slot reads as all zeros */
    }

else {                                                  /* otherwise the slot is occupied */
    sc_rank = select_code / 32;                         /*   so set the rank */
    sc_bit  = 1u << select_code % 32;                   /*     and bit of the interface */

    if (interrupt_system == SET) {                      /* if the interrupt system is on */
        inbound_signals |= ioIEN;                       /*   then assert IEN to the interface */

        if ((sc_bit - 1 & priority_holdoff_set [sc_rank]) == 0  /* if no higher priority device */
          && (sc_rank == 0 || priority_holdoff_set [0] == 0))   /*   is denying PRL */
            inbound_signals |= ioPRH;                           /*     then assert PRH to our interface */
        }

    tpprintf (iot [select_code].devptr, TRACE_IOBUS, "Received data %06o with signals %s\n",
              inbound_value, fmt_bitset (inbound_signals, inbound_format));

    outbound =                                                          /* call the device interface */
      iot [select_code].dibptr->io_interface (iot [select_code].dibptr, /*   with the device select code */
                                              inbound_signals,          /*     and inbound signal set */
                                              inbound_value);           /*       and data value */

    tpprintf (iot [select_code].devptr, TRACE_IOBUS, "Returned data %06o with signals %s\n",
              outbound.value, fmt_bitset (outbound.signals, outbound_format));

    last_select_code = select_code;                     /* save the select code for CLC 0 detection */
    previous_holdoff = priority_holdoff_set [sc_rank];  /*   and the current priority holdoff for this rank */

    if (outbound.signals & cnVALID) {                   /* if the IRQ and PRL signals are valid */
        if (outbound.signals & cnIRQ)                   /*   then if the conditional interrupt request signal is present */
            interrupt_request_set [sc_rank] |= sc_bit;  /*     then set the interrupt request bit */
        else                                            /*   otherwise */
            interrupt_request_set [sc_rank] &= ~sc_bit; /*     clear the request bit */

        if (outbound.signals & cnPRL)                   /* if the conditional priority low signal is present */
            priority_holdoff_set [sc_rank] &= ~sc_bit;  /*   then clear the priority inhibit bit */
        else                                            /* otherwise */
            priority_holdoff_set [sc_rank] |= sc_bit;   /*   set the inhibit bit */
        }

    if (outbound.signals & ioIRQ)                       /* if the interrupt request signal is present */
        interrupt_request = select_code;                /*   then indicate that the select code is interrupting */

    else if (previous_holdoff & ~priority_holdoff_set [sc_rank] & sc_bit)   /* otherwise if priority is newly asserted */
        interrupt_request = io_poll_interrupts (interrupt_system);          /*   then check interrupt requests */

    if (outbound.signals & ioSRQ)                       /* if SRQ is asserted */
        dma_assert_SRQ (select_code);                   /*   then check if DMA is controlling this interface */

    result.skip = (outbound.signals & ioSKF) != 0;      /* return TRUE if the skip-on-flag signal is present */
    result.data = outbound.value;                       /*   and return the outbound data from the interface */
    }

return result;                                          /* return the result of the I/O operation */
}


/* Execute an I/O control operation.

   This routine performs an I/O control operation on the interface specified by
   the "select_code" parameter.  I/O control operations are all those that do
   not pass data to or from the interface.  The routine returns TRUE if the
   interface asserted the SKF signal as a result of the operation and FALSE if
   it did not.

   Certain microcode extension instructions perform I/O operations as part of
   their execution.  In hardware, this is done by setting bits 11-6 of the
   Instruction Register to a code describing the operation and then issuing the
   IOG micro-order to generate the sppropriate backplane signals.  In
   simulation, this relatively lightweight routine performs the same action,
   avoiding much of the overhead of the "cpu_iog" routine.

   The routine performs a memory protect check and then dispatches the operation
   to the interface indicated by the supplied select code.
*/

t_bool io_control (uint32 select_code, IO_GROUP_OP micro_op)
{
SKPF_DATA result;

mp_check_io (select_code, micro_op);                    /* check that the I/O operation is permitted */

result = io_dispatch (select_code,                      /* send the signal set */
                      control_set [micro_op], 0);       /*   to the indicated interface */

return result.skip;                                     /* return TRUE if the interface asserted SKF */
}


/* Assert an I/O backplane signal.

   This routine is called by a device interface simulator to assert a specific
   I/O backplane signal.  The supported signal assertions are ENF, SIR, PON,
   POPIO, CRS, and IAK.  In hardware, these signals would be asserted by logic
   gates on the interface; in simulation, they are asserted by calls to this
   routine.

   The operation is dispatched via the I/O access table by the "io_dispatch"
   routine, so we first ensure that the table entry is set correctly.  During
   instruction execution, this is redundant, as the table has been set up during
   the execution prelude.  However, this routine is also called by each
   interface simulator's device reset routine in response to a RESET ALL or
   RESET <device> SCP command.  At that point, the table may not have been
   intitialized or may contain incorrect entries (if the device has been enabled
   or reassigned since the last time the table was set up).
*/

void io_assert (DEVICE *dptr, IO_ASSERTION assertion)
{
DIB    *dibptr;
uint32 select_code;

if (dptr != NULL && dptr->ctxt != NULL) {               /* if the device points to a valid DIB */
    dibptr = (DIB *) dptr->ctxt;                        /*   then get the DIB pointer */
    select_code = dibptr->select_code;                  /*     and associated select code */

    iot [select_code].devptr = dptr;                    /* set the current device and DIB pointer assignments */
    iot [select_code].dibptr = dibptr;                  /*   into the table */

    io_dispatch (select_code, assert_set [assertion], 0);   /* assert the signal set to the indicated interface */
    }

return;
}


/* Poll for a new interrupt request.

   This routine is called when an interface asserts a previously denied PRL
   signal, i.e., when PRL goes from low to high on the I/O backplane.  It
   determines if any interfaces lower in the priority chain are now ready to
   interrupt.  If so, the select code of the highest priority interrupting
   interface is returned; otherwise, 0 is returned.

   In hardware, PRH and PRL form a priority chain that extends from interface to
   interface on the backplane.  For an interface to generate an interrupt, PRH
   must be asserted by the next-higher-priority interface.  An interface
   receiving PRH asserts IRQ to request an interrupt, and it denies PRL to
   prevent lower-priority devices from interrupting.  IRQ is cleared by an
   interrupt acknowledge (IAK) signal.  PRL generally remains low while a
   device's interrupt service routine is executing to prevent preemption.

   IRQ and PRL indicate one of four possible states for a device:

     IRQ  PRL  Device state
     ---  ---  ----------------------
      0    1   Not interrupting
      1    0   Interrupt requested
      0    0   Interrupt acknowledged
      1    1   (not allowed)

   PRL must be denied when requesting an interrupt (IRQ asserted).  This is a
   hardware requirement of the 21xx/1000 series.  The IRQ lines from the
   backplane are not priority encoded.  Instead, the PRL chain expresses the
   priority by allowing only one IRQ line to be active at a time.  This allows a
   simple pull-down encoding of the CIR inputs.

   When a given interface denies PRL, the PRH-to-PRL chain through all
   lower-priority interfaces also denies.  When that interface reasserts PRL,
   the chain reasserts through intervening interfaces until it reaches one that
   is ready to request an interrupt or through all interfaces if none are ready.
   An interface that is ready to interrupt except for a denied PRH will assert
   IRQ when the higher-priority PRH-to-PRL chain reasserts.

   A direct simulation of this hardware behavior would require polling all
   lower-priority interfaces in order by calling their interface routines,
   asserting PRH to each, and checking each for an asserted IRQ or a denied PRL
   in response.  Two avoid this inefficiency, two bit vectors are kept that
   reflect the states of each interface's IRQ and PRL signals conditional on PRH
   being asserted.  The "interrupt_request_set" bit vector reflects each
   interface's readiness to interrupt if its associated PRH signal is asserted,
   and the "priority_holdoff_set" reflects the (inverted) state of each
   interface's PRL signal, i.e., a zero-bit indicates that the interface is
   ready to assert PRL if its associated PRH signal is asserted.  Each vector is
   represented as a two-element array of 32-bit unsigned integers, forming a
   64-bit vector in which bits 0-31 of the first element correspond to select
   codes 00-37 octal, and bits 0-31 of the second element correspond to select
   codes 40-77 octal.

   The routine begins by seeing if an interrupt is pending.  If not, it returns
   0.  Otherwise, the PRH-to-PRL chain is checked to see if PRL is denied
   anywhere upstream of the highest priority interrupting interface.  If it is,
   then no interface can interrupt.  If all higher-priority PRL signals are
   asserted, then the interrupt request of that interface is granted.

   Recognition of interrupts depends on the current state of the interrupt
   system and interrupt enable flip-flops, as follows:

     INTSYS  INTEN   Interrupts Recognized
     ------  ------  ----------------------------
     CLEAR   CLEAR   Parity error
     CLEAR   SET     Parity error, power fail
     SET     CLEAR   Parity error, memory protect
     SET     SET     All sources

   Memory protect and parity error share select code 05, but the interrupt
   sources are differentiated on the MP card -- setting the flag buffer for a
   memory protect violation is qualified by the IEN signal that reflects
   the INTSYS state, whereas no qualification is used for a parity error.
   Therefore, an interrupt on select code 05 with the interrupt system off must
   be a parity error interrupt.

   The end of the priority chain is marked by the highest-priority
   (lowest-order) bit that is set.  We calculate a priority mask by ANDing the
   the PRL bits with its two's complement using the IOPRIORITY macro.  Only the
   lowest-order bit will differ.  For example:

     pri holdoff :  ...0 0 1 0 0 1 0 0 0 0 0 0  (PRL denied for SC 06 and 11)
     one's compl :  ...1 1 0 1 1 0 1 1 1 1 1 1
     two's compl :  ...1 1 0 1 1 1 0 0 0 0 0 0
     ANDed value :  ...0 0 0 0 0 1 0 0 0 0 0 0  (chain is broken at SC 06)

   The interrupt requests are then ANDed with the priority masks to determine if
   a request is pending:

     pri mask    :  ...0 0 0 0 0 1 0 0 0 0 0 0  (allowed interrupt source)
     int request :  ...0 0 1 0 0 1 0 0 0 0 0 0  (interfaces asserting IRQ)
     ANDed value :  ...0 0 0 0 0 1 0 0 0 0 0 0  (request to grant)

   The select code corresponding to the granted request is then returned to the
   caller.


   Implementation notes:

    1. Recognition of an interrupt in the second half of the request set (i.e.,
       for select codes 40-77 octal) occurs only if the priority set for the
       first half is 0, i.e., if the PRH-to-PRL chain is unbroken through select
       code 37.

    2. If an interrupt for select codes 00-37 is pending but is inhibited, then
       all higher select code interrupts are inhibited as well, so we only need
       to check requests for the lower half or the upper half, but not both.

    3. Splitting the interrupt request and priority holdoff sets into two 32-bit
       parts is actually more efficient on 32-bit host CPUs than using a single
       64-bit set, especially when right-shifting to obtain the bit number.
       Efficiency is also aided by the fact that interface select codes are
       typically below 40 octal and therefore are represented in the first bit
       set(s).
*/

uint32 io_poll_interrupts (FLIP_FLOP interrupt_system)
{
const uint32 unmaskable = 1u << PWR | 1u << MPPE;       /* these interrupts are not inhibited by INTSYS clear */
uint32 sc, rank, priority_mask, request_granted;

if (interrupt_request_set [0]) {                                    /* if a lower pending interrupt exists */
    priority_mask = IOPRIORITY (priority_holdoff_set [0]);          /*   then calculate the first priority mask */
    request_granted = priority_mask & interrupt_request_set [0];    /*     and the request to grant */
    rank = 0;                                                       /*       for interrupt sources SC 00-37 */

    if (interrupt_system == CLEAR)                      /* if the interrupt system is off */
        request_granted &= unmaskable;                  /*   then only Power Fail and Parity Error may interrupt */
    }

else if (interrupt_request_set [1]                                  /* otherwise if an upper pending interrupt exists */
  && priority_holdoff_set [0] == 0) {                               /*   and the priority chain is intact */
    priority_mask = IOPRIORITY (priority_holdoff_set [1]);          /*     then calculate the second priority mask */
    request_granted = priority_mask & interrupt_request_set [1];    /*       and the request to grant */
    rank = 32;                                                      /*         for interrupt sources SC 40-77 */
    }

else                                                    /* otherwise no interrupts are pending */
    return 0;                                           /*   so return */

if (request_granted == 0)                               /* if no request was granted */
    return 0;                                           /*   then return */

else {                                                  /* otherwise */
    for (sc = rank; !(request_granted & 1); sc++)       /*   determine the interrupting select code */
        request_granted = request_granted >> 1;         /*     by counting the bits until the set bit is reached */

    return sc;                                          /* return the interrupting select code */
    }
}



/* CPU local SCP support routines */


/* CPU interface (select code 00).

   I/O operations directed to select code 0 manipulate the interrupt system.
   STF and CLF turn the interrupt system on and off, and SFS and SFC test the
   state of the interrupt system.  When the interrupt system is off, only Power
   Fail and Parity Error interrupts are allowed.  The PRL signal follows the
   state of the interrupt system.

   CLC asserts CRS to all interfaces from select code 6 upward.

   A PON reset initializes certain CPU registers.  The 1000 series does a
   microcoded memory clear and leaves the T and P registers set as a result.


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
         system status.  The major hole in being able to save the complete state
         is in saving the interrupt system state.  In order to do this in both
         the 21MX and the 21XE the instruction 103300 was used to both test the
         interrupt system and turn it off."

    4. Select code 0 cannot interrupt, so the SIR handler does not assert IRQ.

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

static SIGNALS_VALUE cpu_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
static INBOUND_SET last_signal_set = ioNONE;            /* the last set of I/O signals processed */
INBOUND_SIGNAL     signal;
INBOUND_SET        working_set = inbound_signals;
SIGNALS_VALUE      outbound    = { ioNONE, 0 };
uint32             sc;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            interrupt_system = CLEAR;                   /* turn the interrupt system off */

            if (interrupt_request > MPPE)               /* if any interrupt other than power fail or parity error */
                interrupt_request = 0;                  /*   is pending, then clear it */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            interrupt_system = SET;                     /* turn the interrupt system on */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (interrupt_system == CLEAR)              /* if the interrupt system is off */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (interrupt_system == SET)                /* if the interrupt system is on */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O input */
            outbound.value = 0;                         /* returns 0 */
            break;


        case ioPON:                                     /* Power On Normal */
            AR = 0;                                     /* clear A register */
            BR = 0;                                     /* clear B register */
            SR = 0;                                     /* clear S register */
            TR = 0;                                     /* clear T register */
            E = 1;                                      /* set E register */

            if (cpu_configuration & CPU_1000) {         /* if the CPU is a 1000-series machine */
                mem_zero (0, mem_size);                 /*   then power on clears memory */

                MR = LA_MAX;                            /* set the M register to the maximum logical address */
                PR = MR + 1;                            /*   and the P register to one more than that */
                }

            else {                                      /* otherwise is a 21xx-series machine */
                MR = 0;                                 /*   which clears the M register */
                PR = 0;                                 /*     and the P register */
                }
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            O = 0;                                      /* clear the overflow register */

            interrupt_system = CLEAR;                   /* turn off the interrupt system */
            cpu_interrupt_enable = SET;                 /*   and enable interrupts */
            break;


        case ioCLC:                                             /* Clear Control flip-flop */
            if (last_select_code != 0                           /* if the last I/O instruction */
              || (last_signal_set & ioCLC) == 0)                /*   was not a CLC 0 */
                for (sc = SC_CRS; sc <= SC_MAX; sc++)           /*     then assert the CRS signal */
                    if (iot [sc].devptr != NULL)                /*       to all occupied I/O slots  */
                        io_assert (iot [sc].devptr, ioa_CRS);   /*         from select code 6 and up */
            break;


        case ioSIR:                                             /* Set Interrupt Request */
            if (interrupt_system)                               /* if the interrupt system is on */
                outbound.signals |= ioPRL | cnPRL | cnVALID;    /*   then assert PRL */
            else                                                /* otherwise */
                outbound.signals |= cnVALID;                    /*   deny PRL */
            break;


        case ioIOO:                                     /* not used by this interface */
        case ioSTC:                                     /* not used by this interface */
        case ioEDT:                                     /* not used by this interface */
        case ioCRS:                                     /* not used by this interface */
        case ioIAK:                                     /* not used by this interface */
        case ioENF:                                     /* not used by this interface */
        case ioIEN:                                     /* not used by this interface */
        case ioPRH:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

last_signal_set = inbound_signals;                      /* save the current signal set for the next call */

return outbound;                                        /* return the outbound signals and value */
}


/* Overflow and S-register interface (select code 01).

   I/O operations directed to select code 1 manipulate the overflow (O) and
   switch (S) registers.  On the 2115 and 2116, there is no S-register
   indicator, so it is effectively read-only.  On the other machines, a
   front-panel display of the S-register is provided.  On all machines,
   front-panel switches are provided to set the contents of the S register.


   Implementation notes:

    1. Select code 1 cannot interrupt, so there is no SIR handler, and PRH is
       passed through to PRL.
*/

static SIGNALS_VALUE ovf_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            O = 0;                                      /* clear the overflow register */
            break;


        case ioSTF:                                     /* Set Flag flip-flop */
            O = 1;                                      /* set the overflow register */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (O == 0)                                 /* if the overflow register is clear */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (O != 0)                                 /* if the overflow register is set */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O data input */
            outbound.value = SR;                        /* read switch register value */
            break;


        case ioIOO:                                             /* I/O data output */
            if (!(cpu_configuration & (CPU_2115 | CPU_2116)))   /* on all machines except the 2115 and 2116 */
                SR = inbound_value & R_MASK;                    /*   write the value to the S-register display */
            break;


        case ioPRH:                                     /* Priority High */
            outbound.signals |= ioPRL;                  /* assert PRL */
            break;


        case ioSTC:                                     /* not used by this interface */
        case ioCLC:                                     /* not used by this interface */
        case ioEDT:                                     /* not used by this interface */
        case ioCRS:                                     /* not used by this interface */
        case ioPOPIO:                                   /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
        case ioIAK:                                     /* not used by this interface */
        case ioENF:                                     /* not used by this interface */
        case ioIEN:                                     /* not used by this interface */
        case ioSIR:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}


/* Power fail interface (select code 04).

   Power fail detection is standard on 2100 and 1000 systems and is optional on
   21xx systems.  Power fail recovery is standard on the 2100 and optional on
   the others.  Power failure or restoration will cause an interrupt on select
   code 4 and will deny PRL, preventing all other devices from interrupting
   while the power fail routine is executing.  Asserting either STC or CLC
   clears the interrupt and reasserts PRL.  The direction of power change (down
   or up) can be tested by SFC, which will skip if power is failing.

   Asserting IOI to select code 4 reads the Central Interrupt Register value.


   Implementation notes:

    1. Currently, power fail is not simulated, and the interface is provided
       solely to read the CIR.  As a result, PRL is always asserted.
*/

static SIGNALS_VALUE pwr_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set = inbound_signals;
SIGNALS_VALUE  outbound    = { ioNONE, 0 };

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioSTC:                                     /* Set Control flip-flop */
        case ioCLC:                                     /* Clear Control flip-flop */
            break;                                      /* reinitializes power fail */


        case ioSFC:                                     /* Skip if Flag is Clear */
            break;                                      /* skips if power is failing */


        case ioIOI:                                     /* I/O data input */
            outbound.value = CIR;                       /* return the select code of the interrupting interface */
            break;


        case ioPRH:                                     /* Priority High */
            outbound.signals |= ioPRL;                  /* assert PRL */
            break;


        case ioSTF:                                     /* not used by this interface */
        case ioCLF:                                     /* not used by this interface */
        case ioSFS:                                     /* not used by this interface */
        case ioIOO:                                     /* not used by this interface */
        case ioEDT:                                     /* not used by this interface */
        case ioCRS:                                     /* not used by this interface */
        case ioPOPIO:                                   /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
        case ioIAK:                                     /* not used by this interface */
        case ioENF:                                     /* not used by this interface */
        case ioIEN:                                     /* not used by this interface */
        case ioSIR:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}


/* Examine a CPU memory location.

   This routine is called by the SCP to examine memory.  The routine retrieves
   the memory location indicated by "address" as modified by any "switches" that
   were specified on the command line and returns the value in the first element
   of "eval_array".  The "uptr" parameter is not used.

   On entry, the "meu_map_address" routine is called to translate a logical
   address to a physical address.  If "switches" includes SIM_SW_REST or "-N",
   then the address is a physical address, and the routine returns the address
   unaltered.

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

index = meu_map_address ((HP_WORD) address, switches);  /* map the supplied address as directed by the switches */

if (index == D32_UMAX)                                  /* if the MEM is disabled but a mapping mode was given */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (index >= mem_size)                             /* otherwise if the address is beyond the memory limit */
    return SCPE_NXM;                                    /*   then return non-existent memory status */

else if (eval_array == NULL)                            /* otherwise if the value pointer was not supplied */
    return SCPE_IERR;                                   /*   then return internal error status */

else                                                    /* otherwise */
    *eval_array = (t_value) mem_examine (index);        /*   then return the memory or A/B register value */

return SCPE_OK;                                         /* return success status */
}


/* Deposit to a CPU memory location.

   This routine is called by the SCP to deposit to memory.  The routine stores
   the supplied "value" into memory at the "address" location as modified by any
   "switches" that were specified on the command line.

   On entry, the "meu_map_address" routine is called to translate a logical
   address to a physical address.  If "switches" includes SIM_SW_REST or "-N",
   then the address is a physical address, and the routine returns the address
   unaltered.

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

index = meu_map_address ((HP_WORD) address, switches);  /* map the supplied address as directed by the switches */

if (index == D32_UMAX)                                  /* if the MEM is disabled but a mapping mode was given */
    return SCPE_NOFNC;                                  /*   then the command is not allowed */

else if (index >= mem_size)                             /* otherwise if the address is beyond the memory limit */
    return SCPE_NXM;                                    /*   then return non-existent memory status */

else                                                    /* otherwise */
    mem_deposit (index, (HP_WORD) value);               /*   write the memory or A/B register value */

return SCPE_OK;                                         /* return success status */
}


/* Reset the CPU.

   This routine is called for a RESET, RESET CPU, RUN, or BOOT CPU command.  It
   is the simulation equivalent of an initial power-on condition (corresponding
   to PON, POPIO, and CRS signal assertion in the CPU) or a front-panel PRESET
   button press (corresponding to POPIO and CRS assertion).  SCP delivers a
   power-on reset to all devices when the simulator is started.

   If this is the first call after simulator startup, the initial memory array
   is allocated, the SCP-required program counter pointer is set to point to the
   REG array element corresponding to the P register, and the default CPU and
   memory size configuration is set.  In addition, the loader ROM sockets of the
   1000-series CPUs are populated with the initial ROM set, and the Basic Binary
   Loader (BBL) is installed in the protected memory (the upper 64 words of the
   defined memory size) of the 2116.


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
t_stat status;

if (sim_PC == NULL) {                                   /* if this is the first call after simulator start */

    hp_one_time_init();                                 /* perform one time initializations (previously defined as sim_vm_init() */

    status = mem_initialize (PA_MAX);                   /*   then allocate main memory */

    if (status == SCPE_OK) {                            /* if memory initialization succeeds */
        for (sim_PC = dptr->registers;                  /*   then find the P register entry */
             sim_PC->loc != &PR && sim_PC->loc != NULL; /*     in the register array */
             sim_PC++);                                 /*       for the SCP interface */

        if (sim_PC == NULL)                             /* if the P register entry is not present */
            return SCPE_NXREG;                          /*   then there is a serious problem! */

        pcq_r = find_reg ("PCQ", NULL, dptr);           /* get the PC queue pointer */

        if (pcq_r == NULL)                              /* if the PCQ register is not present */
            return SCPE_IERR;                           /*   then something is seriously wrong */
        else                                            /* otherwise */
            pcq_r->qptr = 0;                            /*   initialize the register's queue pointer */

        mem_size = 32768;                               /* set the initial memory size to 32K */
        set_model (NULL, UNIT_2116, NULL, NULL);        /*   and the initial CPU model */

        loader_rom [0] = find_dev ("PTR");              /* install the 12992K ROM in socket 0 */
        loader_rom [1] = find_dev ("DQC");              /*   and the 12992A ROM in socket 1 */
        loader_rom [2] = find_dev ("MSC");              /*   and the 12992D ROM in socket 2 */
        loader_rom [3] = find_dev ("DS");               /*   and the 12992B ROM in socket 3 */

        loader_rom [0]->boot (0, loader_rom [0]);       /* install the BBL via the paper tape reader boot routine */
        set_loader (NULL, FALSE, NULL, NULL);           /*   and then disable the loader, which had been enabled */
        }

    else                                                /* otherwise memory initialization failed */
        return status;                                  /*   so report the error and abort the simulator */
    }

if (sim_switches & SWMASK ('P'))                        /* if this is a power-on reset */
    io_assert (&cpu_dev, ioa_PON);                      /*   then issue the PON signal to the CPU */
else                                                    /* otherwise */
    io_assert (&cpu_dev, ioa_POPIO);                    /*   issue a PRESET */

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
       with "Non-existent device."
*/

static t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
const int32 select_code = IBL_SC  (SR);                 /* the select code from S register bits 11-6 */
const int32 rom_socket  = IBL_ROM (SR);                 /* the ROM socket number from S register bits 15-14 */

if (cpu_configuration & CPU_1000)                       /* if this is a 1000-series CPU */
    if (select_code < SC_VAR) {                         /*   then if the select code is invalid */
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
const uint32 model = UNIT_MODEL (cpu_unit [0].flags);   /* the current CPU model index */
int32 old_size = (int32) mem_size;                      /* current memory size */

if ((uint32) new_size > cpu_features [model].maxmem)    /* if the new memory size is not supported on current model */
    return SCPE_NOFNC;                                  /*   then report the error */

if (!(sim_switches & SWMASK ('F'))                      /* if truncation is not explicitly forced */
  && ! mem_is_empty (new_size)                          /*   and the truncated part is not empty */
  && get_yn (confirm, FALSE) == FALSE)                  /*     and the user denies confirmation */
    return SCPE_INCOMP;                                 /*       then abort the command */

if (cpu_configuration & CPU_1000)                       /* if the CPU is a 1000-series machine */
    cpu_unit [0].capac = mem_size = mem_end = new_size; /*   then memory is not reserved for the loader */

else {                                                  /* otherwise */
    set_loader (uptr, FALSE, NULL, NULL);               /* save loader to shadow RAM */
    cpu_unit [0].capac = mem_size = new_size;           /* set new memory size */
    mem_end = mem_size - IBL_SIZE;                      /* reserve memory for loader */
    }

if (old_size > new_size)                                /* if the new size is smaller than the prior size */
    mem_zero (mem_end, old_size - new_size);            /*   then zero the newly non-existent memory area */

return SCPE_OK;
}


/* Change the CPU model.

   This validation routine is called to configure the CPU model.  The
   "new_model" parameter is set to the unit flag corresponding to the model
   desired.  The "uptr" parameter points to the CPU unit.  The other parameters
   are not used.

   Validation starts by setting the new memory size.  If the current memory size
   is within the range of memory sizes permitted by the new CPU model, it is
   kept; otherwise, it is reduced to the maximum size permitted.  If memory is
   to be truncated, the "set_size" routine verifies either that it is blank
   (i.e., filled with zero values) or that the user confirms that truncation is
   allowable.

   If the new memory size is accepted, the CPU options are set to the typical
   configuration supported by the new model.  Memory Protect and DMA are then
   configured as specified by the CPU feature table.  Memory expansion is
   enabled if the DMS instruction set is present.  Finally, the "is_1000" flag
   and memory reserved for the binary loader are set as directed by the new
   model.

   On return, the "cpu_configuration" bit set is updated to indicate the new
   model configuration.


   Implementation notes:

    1. "cpu_configuration" is used by the symbolic examine and deposit routines
       and instruction tracing to determine whether the firmware implementing a
       given opcode is present.  It is a copy of the CPU unit option flags with
       the encoded CPU model decoded into model flag bits.  This allows a simple
       (and fast) AND operation with a firmware feature word to determine
       applicability, saving the multiple masks and comparisons that would
       otherwise be required.

    2. The 'is_1000" variable is used to index into tables where the row
       selected depends on whether or not the CPU is a 1000 M/E/F-series model.
       For logical tests that depend on this, it is faster (by one x86 machine
       instruction) to test the "cpu_configuration" variable for the presence of
       one of the three 1000 model flags.
*/

static t_stat set_model (UNIT *uptr, int32 new_model, CONST char *cptr, void *desc)
{
const FEATURE_TABLE new_cpu = cpu_features [UNIT_MODEL (new_model)];    /* get the features describing the new model */
uint32 new_memsize;
t_stat result;

if (mem_size > new_cpu.maxmem)                          /* if the current memory size is too large for the new model */
    new_memsize = new_cpu.maxmem;                       /*   then set it to the maximum size supported */
else                                                    /* otherwise */
    new_memsize = mem_size;                             /*   leave it unchanged */

result = set_size (uptr, new_memsize, NULL, NULL);      /* set the new memory size */

if (result == SCPE_OK) {                                /* if the change succeeded */
    cpu_configuration = TO_CPU_OPTION (new_cpu.typ)     /*   then set the typical options */
                          | CPU_BASE                    /*     and the base instruction set bit */
                          | TO_CPU_MODEL (new_model);   /*       and the new CPU model flag */

    cpu_unit [0].flags = cpu_unit [0].flags & ~UNIT_OPTION_FIELD    /* enable the typical features */
                          | new_cpu.typ & UNIT_OPTION_FIELD;        /*   for the new model */

    mp_configure ((new_cpu.typ & UNIT_MP) != 0,         /* configure MP, specifying whether */
                  (new_cpu.opt & UNIT_MP) != 0);        /*   it is enabled and optional */

    dma_configure ();                                   /* configure DMA for the new model */

    if (new_cpu.typ & UNIT_DMS)                         /* if DMS instructions are present */
        meu_configure (ME_Enabled);                     /*   then enable the MEM device */
    else                                                /* otherwise */
        meu_configure (ME_Disabled);                    /*   disable the MEM and mapping */

    if (cpu_configuration & CPU_1000) {                 /* if the CPU is a 1000-series machine */
        is_1000 = TRUE;                                 /*   then set the model index */
        mem_end = mem_size;                             /* memory is not reserved for the loader  */
        }

    else {                                              /* otherwise this is a 2100 or 211x */
        is_1000 = FALSE;                                /*   so set the model index */
        mem_end = mem_size - IBL_SIZE;                  /*     and reserve memory for the loader */
        }
    }

return result;
}


/* Set a CPU option.

   This validation routine is called to add an option to the current CPU
   configuration.  The "option" parameter is set to the unit flag corresponding
   to the option desired.  The "uptr" parameter points to the CPU unit and is
   used to obtain the CPU model.  The other parameters are not used.

   The routine processes commands of the form:

     SET CPU <option>[,<option>...]

   The option must be valid for the current CPU model, or the command will be
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
const uint32 model = UNIT_MODEL (uptr->flags);          /* the current CPU model index */

if ((cpu_features [model].opt & option) == 0)           /* if the option is not available for the current CPU */
    return SCPE_NOFNC;                                  /*   then reject the request */

if (option == UNIT_DMS)                                 /* if DMS instructions are being enabled */
    meu_configure (ME_Enabled);                         /*   then enable the MEM device */

if (cpu_configuration & CPU_2100) {                     /* if the current CPU is a 2100 */
    if ((option == UNIT_FP) || (option == UNIT_FFP))    /*   then the IOP option */
        uptr->flags &= ~UNIT_IOP;                       /*     and the FP and FFP options */
    else if (option == UNIT_IOP)                        /*       are */
        uptr->flags &= ~(UNIT_FP | UNIT_FFP);           /*         mutually exclusive */

    if (option == UNIT_FFP)                             /* the FFP option */
        uptr->flags |= UNIT_FP;                         /*   requires FP as well */
    }

cpu_configuration = cpu_configuration & ~CPU_OPTION_MASK    /* update the CPU configuration */
                      | TO_CPU_OPTION (uptr->flags)         /*   with any revised option settings */
                      | CPU_BASE;                           /*     and the base set bit */

if (option & UNIT_EMA_VMA)                              /* if EMA or VMA is being set */
    cpu_configuration &= ~UNIT_EMA_VMA;                 /*   then first remove both as they are mutually exclusive */

cpu_configuration |= TO_CPU_OPTION (option);            /* include the new setting in the configuration */

return SCPE_OK;
}


/* Clear a CPU option.

   This validation routine is called to remove an option from the current CPU
   configuration.  The "option" parameter is set to the unit flag corresponding
   to the option desired.  The "uptr" parameter points to the CPU unit and is
   used to obtain the CPU model.  The other parameters are not used.

   The routine processes commands of the form:

     SET CPU NO<option>[,NO<option>...]

   The option must be valid for the current CPU model, or the command will be
   rejected.
*/

static t_stat clear_option (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
const uint32 model = UNIT_MODEL (uptr->flags);          /* the current CPU model index */

if ((cpu_features [model].opt & option) == 0)           /* if the option is not available for the current CPU */
    return SCPE_NOFNC;                                  /*   then reject the request */

uptr->flags &= ~option;                                 /* disable the option */

if (option == UNIT_DMS)                                 /* if DMS instructions are being disabled */
    meu_configure (ME_Disabled);                        /*   then disable the MEM device */

if (cpu_configuration & CPU_2100 && option == UNIT_FP)  /* if the FP option on a 2100 is being disabled */
    uptr->flags &= ~UNIT_FFP;                           /*   then disable the FFP as well */

cpu_configuration = cpu_configuration & ~CPU_OPTION_MASK    /* update the CPU configuration */
                      | TO_CPU_OPTION (uptr->flags)         /*   with the revised option settings */
                      | CPU_BASE;                           /*     and the base set bit */

return SCPE_OK;
}


/* Enable or disable the 21xx binary loader.

   The 21xx CPUs store their initial binary loaders in the last 64 words of
   available memory.  This memory is protected by a LOADER ENABLE switch on the
   front panel.  When the switch is off (disabled), main memory effectively ends
   64 locations earlier, i.e., the loader area is treated as non-existent.
   Because these are core machines, the loader is retained when system power is
   off.

   1000 CPUs do not have a protected loader feature.  Instead, loaders are
   stored in PROMs and are copied into main memory for execution by the IBL
   switch.

   Under simulation, we keep both a total configured memory size (mem_size) and
   a current configured memory size (mem_end = "first word address of
   non-existent memory").  When the two are equal, the loader is enabled.  When
   the current size is less than the total size, the loader is disabled.

   Disabling the loader copies the last 64 words to a shadow array, zeros the
   corresponding memory, and decreases the last word of addressable memory by
   64.  Enabling the loader reverses this process.

   Disabling may be done manually by user command or automatically when a halt
   instruction is executed.  Enabling occurs only by user command.  This differs
   slightly from actual machine operation, which additionally disables the
   loader when a manual halt is performed.  We do not do this to allow
   breakpoints within and single-stepping through the loaders.


   Implementation notes:

    1. In hardware, reads from non-existent memory return zero.  In simulation,
       the largest possible memory is instantiated and initialized to zero, so
       that reads need not be checked against memory size.  To preserve this
       model for the protected loader, we save and then zero the memory area
       when the loader is disabled.
*/

static t_stat set_loader (UNIT *uptr, int32 enable, CONST char *cptr, void *desc)
{
static MEMORY_WORD loader [IBL_SIZE];                       /* the shadow memory for the currently disabled loader */
const  t_bool currently_enabled = (mem_end == mem_size);    /* TRUE if the loader is currently enabled */

if (cpu_configuration & CPU_1000)                       /* if the current CPU is a 1000-series */
    return SCPE_NOFNC;                                  /*   then the protected loader does not exist */

if (currently_enabled && enable == 0) {                 /* if the enabled loader is being disabled */
    mem_end = mem_size - IBL_SIZE;                      /*   then decrease available memory */
    mem_copy_loader (loader, mem_end, From_Memory);     /*     and shadow the loader and zero memory */
    }

else if (!currently_enabled && enable == 1) {           /* otherwise if the disabled loader is being enabled */
    mem_copy_loader (loader, mem_end, To_Memory);       /*   then copy the shadow loader to memory */
    mem_end = mem_size;                                 /*     and increase available memory */
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

if (!(cpu_configuration & CPU_1000))                    /* if the CPU is not a 1000-series unit */
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
fputs ((const char *) desc, st);                        /* output the CPU model name */

if (!(cpu_configuration & CPU_1000))                    /* if the CPU is a 2100 or 21xx */
    if (mem_end < mem_size)                             /*   then if the loader area is non-existent */
        fputs (", loader disabled", st);                /*     then report that the loader is disabled */
    else                                                /*   otherwise */
        fputs (", loader enabled", st);                 /*     report that it is enabled */

return SCPE_OK;
}


/* Show the set of installed loader ROMs.

   This display routine is called to show the set of installed loader ROMs in
   the four available sockets of a 1000-series CPU.  On entry, the "st"
   parameter is the open output stream.  The other parameters are not used.

   The routine prints a table of ROMs in this format:

     Socket  Device  ROM Description
     ------  ------  -----------------------------------------
       0      PTR    12992K Paper Tape Loader
       1      DA     12992H 7906H/7920H/7925H/9895 Disc Loader
       2      MSC    12992D 7970 Magnetic Tape Loader
       3     (none)  (empty socket)

   If a given socket contains a ROM, the associated device name, HP part number,
   and description of the loader ROM are printed.  Loader ROMs may be displayed
   only if the current CPU model is a 1000-series machine; if it is not, the
   command will be rejected.

   This routine services an extended modifier entry, so it must add the trailing
   newline to the output before returning.


   Implementation notes:

    1. SCP does not honor the status return from display routines, so we must
       explicitly print the rejection error message if the routine is called for
       a non-1000 CPU.

    2. sim_dname is called instead of using dptr->name directly to ensure that
       we pick up assigned logical device names.
*/

static t_stat show_roms (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const char *cname, *dname;
DIB    *dibptr;
uint32 socket;

if (!(cpu_configuration & CPU_1000)) {                  /* if the CPU is not a 1000-series unit */
    fputs (sim_error_text (SCPE_NOFNC), st);            /*   then print the rejection message */
    fputc ('\n', st);                                   /*     and add a line feed */

    return SCPE_NOFNC;                                  /* reject the command */
    }

fputc ('\n', st);                                           /* skip a line */
fputs ("Socket  Device  ROM Description\n", st);            /*   and print */
fputs ("------  ------  "                                   /*     the table header */
       "-----------------------------------------\n", st);

for (socket = 0; socket < 4; socket++)                      /* loop through the sockets */
    if (loader_rom [socket] == NULL)                        /* if the socket is empty */
        fprintf (st, "  %u     (none)  (empty socket)\n",   /*   then report it as such */
                 socket);

    else {                                              /* otherwise the socket is occupied */
        dname = sim_dname (loader_rom [socket]);        /*   so get the device name */
        dibptr = (DIB *) loader_rom [socket]->ctxt;     /*     and a pointer to that device's DIB */
        cname = dibptr->rom_description;                /*       to get the ROM description */

        if (cname == NULL)                              /* if there is no description */
            cname = "";                                 /*   then use a null string */

        fprintf (st, "  %u      %-4s   %s\n",           /* print the ROM information */
                 socket, dname, cname);
        }

return SCPE_OK;                                         /* return success status */
}


/* Show the currently configured I/O card cage.

   This display routine is called to show the set of interfaces installed in the
   I/O card cage.  On entry, the "st" parameter is the open output stream.  The
   other parameters are not used.

   The routine prints the installed I/O cards in this format:

     SC  Device  Interface Description
     --  ------  ---------------------------------------------------------------
     10   PTR    12597A-002 Tape Reader Interface
     11   TTY    12531C Buffered Teleprinter Interface
     12   PTP    12597A-005 Tape Punch Interface
     13   TBG    12539C Time Base Generator Interface
     14  (none)  12777A Priority Jumper Card
     15   LPT    12845B Line Printer Interface
     [...]

   If a given I/O slot contains an interface card, the associated device name,
   HP part number, and description of the interface are printed.  If the slot is
   empty, it is displayed as though a 12777A Priority Jumper Card is installed.
   The list terminates with the last occupied I/O slot.

   If select code conflicts exist, the invalid assignments are printed before
   the interface list, and the corresponding entries in the list are flagged.
   For example:

     Select code 13 conflict (TBG and LPS)

     SC  Device  Interface Description
     --  ------  ------------------------------------------------------------------
     10   PTR    12597A-002 Tape Reader Interface
     11   TTY    12531C Buffered Teleprinter Interface
     12   PTP    12597A-005 Tape Punch Interface
     13   ---    (multiple assignments)
     14  (none)  12777A Priority Jumper Card
     15   LPT    12845B Line Printer Interface
     [...]

   This routine services an extended modifier entry, so it must add the trailing
   newline to the output before returning.
*/

static t_stat show_cage (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
const char *cname, *dname;
uint32 sc, last_sc;

fputc ('\n', st);                                       /* skip a line */

if (initialize_io (FALSE) == FALSE)                     /* set up the I/O tables; if a conflict was reported */
    fputc ('\n', st);                                   /*   then separate it from the interface list */

fputs ("SC  Device  Interface Description\n", st);      /* print */
fputs ("--  ------  "                                   /*   the table header */
       "------------------------------------------------------------------\n", st);

for (last_sc = SC_MAX; last_sc > SC_VAR; last_sc--)     /* find the last occupied I/O slot */
    if (iot [last_sc].devptr != NULL                    /*   with an assigned device */
      && !(iot [last_sc].devptr->flags & DEV_DIS))      /*     that is currently enabled */
        break;

for (sc = SC_VAR; sc <= last_sc; sc++)                                      /* loop through the select codes */
    if (iot [sc].devptr == NULL || iot [sc].devptr->flags & DEV_DIS)        /* if the slot is unassigned or disabled */
        fprintf (st, "%02o  (none)  12777A Priority Jumper Card\n", sc);    /*   then report a jumper card */

    else if (iot [sc].references > 1)                               /* otherwise if a conflict exists */
        fprintf (st, "%02o   ---    (multiple assignments)\n", sc); /*   then report the multiple assignment */

    else {                                              /* otherwise the slot is valid */
        dname = sim_dname (iot [sc].devptr);            /*   so get the device name */
        cname = iot [sc].dibptr->card_description;      /*     and interface card description */

        if (cname == NULL)                              /* if there is no description */
            cname = "";                                 /*   then use a null string */

        fprintf (st, "%02o   %-4s   %s\n", sc, dname, cname);   /* report the interface in the slot */
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
   by the console poll service routine.  It is only representative when the CPU
   is not idling.
*/

static t_stat show_speed (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Simulation speed = %ux\n", cpu_speed);    /* display the current CPU speed */
return SCPE_OK;                                         /*   and report success */
}



/* CPU local utility routine declarations */


/* Execute one machine instruction.

   This routine executes the CPU instruction present in the IR.  The CPU state
   (registers, memory, interrupt status) is modified as necessary, and the
   routine return SCPE_OK if the instruction executed successfully.  Any other
   status indicates that execution should cease, and control should return to
   the simulator console.  For example, a programmed HALT instruction returns
   STOP_HALT status.

   This routine implements the main instruction dispatcher.  Instructions
   corresponding to the MRG, SRG, and ASG are executed inline.  IOG, EAG, and
   UIG instructions are executed by external handlers.

   The JMP instruction executor handles CPU idling.  The 21xx/1000 CPUs have no
   "wait for interrupt" instruction.  Idling in HP operating systems consists of
   sitting in "idle loops" that end with JMP instructions.  We test for certain
   known patterns when a JMP instruction is executed to decide if the simulator
   should idle.

   Idling must not occur if an interrupt is pending.  As mentioned before, the
   CPU will defer pending interrupts when certain instructions are executed.  OS
   interrupt handlers exit via such deferring instructions.  If there is a
   pending interrupt when the OS is otherwise idle, the idle loop will execute
   one instruction before reentering the interrupt handler.  If we call
   sim_idle() in this case, we will lose interrupts.

   Consider the situation in RTE.  Under simulation, the TTY and TBG events are
   co-scheduled, so their event routines are called sequentially during a single
   "sim_process_event" call.  If the TTY has received an input character from
   the console poll, then both devices are ready to interrupt.  Assume that the
   TTY has priority.  When the TTY interrupts, $CIC in RTE is entered.  The TBG
   interrupt is held off through the JSB $CIC,I / JMP $CIC0,I / SFS 0,C
   instruction entry sequence, which defers interrupts until the interrupt
   system is turned off.  When $CIC returns via $IRT, one instruction of the
   idle loop is executed, even though the TBG interrupt is still pending,
   because the UJP instruction used to return also defers interrupts.

   If "sim_idle" is called at this point, the simulator will sleep when it
   should be handling the pending TBG interrupt.  When it awakes, TTY expiration
   will be moved forward to the next instruction.  The still-pending TBG
   interrupt will then be recognized, and $CIC will be entered.  But the TTY and
   TBG will then expire and attempt to interrupt again, although they are
   deferred by the $CIC entry sequence.  This causes the second TBG interrupt to
   be missed, as processing of the first one is just now being started.

   Similarly, at the end of the TBG handling, the TTY interrupt is still
   pending.  When $IRT returns to the idle loop, "sim_idle" would be called
   again, so the TTY and then TBG interrupt a third time.  Because the second
   TTY interrupt is still pending, $CIC is entered, but the third TTY interrupt
   is lost.

   We solve this problem by testing for a pending interrupt before calling
   "sim_idle".  The system isn't really quiescent if it is just about to handle
   an interrupt.


   Implementation notes:

    1. Instruction decoding is based on the HP 1000, which does a 256-way branch
       on the upper eight bits of the instruction, as follows:

         15 14 13 12 11 10  9  8  Instruction Group
         -- -- -- -- -- -- -- --  -------------------------------------------
          x  n  n  n  x  x  x  x  Memory Reference (n n n not equal to 0 0 0)
          0  0  0  0  x  0  x  x  Shift-Rotate
          0  0  0  0  x  1  x  x  Alter-Skip
          1  0  0  0  x  1  x  x  I/O
          1  0  0  0  0  0  x  0  Extended Arithmetic
          1  0  0  0  0  0  0  1  divide (decoded as 100400)
          1  0  0  0  1  0  0  0  double load (decoded as 104000)
          1  0  0  0  1  0  0  1  double store (decoded as 104400)
          1  0  0  0  1  0  1  0  Extended Instruction 0 (A/B is set)
          1  0  0  0  x  0  1  1  Extended Instruction 1 (A/B is ignored)

    2. JSB is tricky.  It is possible to generate both an MP and a DM violation
       simultaneously, as the MP and MEM cards validate in parallel.  Consider a
       JSB to a location under the MP fence and on a write-protected page.  This
       situation must be reported as a DM violation, because it has priority
       (SFS 5 and SFC 5 check only the MEVFF, which sets independently of the MP
       fence violation).  Under simulation, this means that DM violations must
       be checked, and the MEVFF must be set, before an MP abort is taken.  This
       is done by the "mp_check_jmp" routine.

    3. Although MR (and TR) will be changed by reads of an indirect chain, the
       idle loop JMP will be direct, and so MR will contain the correct value
       for the "idle loop omitted" trace message.

    4. The Alter-Skip Group RSS micro-op reverses the skip sense of the SEZ,
       SSA/SSB, SLA/SLB, and SZA/SZB micro-op tests.  Normally, the instruction
       skips if any test is true.  However, the specific combination of SSA/SSB,
       SLA/SLB, and RSS micro-ops causes a skip if BOTH of the skip cases are
       true, i.e., if both the MSB and LSB of the register value are ones.  We
       handle this as a special case, because without RSS, the instruction skips
       if EITHER the MSB or LSB is zero.  The other reversed skip cases (SEZ,RSS
       and SZA,RSS/SZB,RSS) are independent.
*/

static t_stat machine_instruction (t_bool int_ack, uint32 *idle_save)
{
uint32  ab_selector, result, skip;
HP_WORD data;
t_bool  rss;
t_stat  status = SCPE_OK;

switch (UPPER_BYTE (IR)) {                              /* dispatch on bits 15-8 of the instruction */

/* Memory Reference Group */

    case 0020: case 0021: case 0022: case 0023:         /* AND */
    case 0024: case 0025: case 0026: case 0027:
    case 0220: case 0221: case 0222: case 0223:         /* AND,I */
    case 0224: case 0225: case 0226: case 0227:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = AR & ReadW (MR);                       /*   then AND the accumulator and memory */
        break;


    case 0230: case 0231: case 0232: case 0233:         /* JSB,I */
    case 0234: case 0235: case 0236: case 0237:
        cpu_interrupt_enable = CLEAR;                   /* defer interrupts */

    /* fall through into the JSB case */

    case 0030: case 0031: case 0032: case 0033:         /* JSB */
    case 0034: case 0035: case 0036: case 0037:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            mp_check_jsb (MR);                          /*   then validate the jump address */

            WriteW (MR, PR);                            /* store P into the target memory address */

            PCQ_ENTRY;                                  /* save P in the queue */
            PR = MR + 1 & LA_MASK;                      /*   and jump to the word after the target address */
            }
        break;


    case 0040: case 0041: case 0042: case 0043:         /* XOR */
    case 0044: case 0045: case 0046: case 0047:
    case 0240: case 0241: case 0242: case 0243:         /* XOR,I */
    case 0244: case 0245: case 0246: case 0247:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = AR ^ ReadW (MR);                       /*   then XOR the accumulator and memory */
        break;


    case 0250: case 0251: case 0252: case 0253:         /* JMP,I */
    case 0254: case 0255: case 0256: case 0257:
        cpu_interrupt_enable = CLEAR;                   /* defer interrupts */

    /* fall through into the JMP case */

    case 0050: case 0051: case 0052: case 0053:         /* JMP */
    case 0054: case 0055: case 0056: case 0057:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status != SCPE_OK)                          /* if the address failed to resolve */
            break;                                      /*   then abort execution */

        mp_check_jmp (MR, 0);                           /* validate the jump address */

        if (sim_idle_enab && interrupt_request == 0     /* if idle is enabled and no interrupt is pending */
          && mem_is_idle_loop ()) {                     /*   and execution is in the DOS or RTE idle loop */
            tprintf (cpu_dev, cpu_dev.dctrl,
                     DMS_FORMAT "idle loop execution omitted\n",
                     meu_indicator, meu_page, MR, IR);

            if (cpu_dev.dctrl != 0) {                   /*     then if tracing is enabled */
                *idle_save = cpu_dev.dctrl;             /*       then save the current trace flag set */
                cpu_dev.dctrl = 0;                      /*         and turn off tracing for the idle loop */
                }

            sim_idle (TMR_POLL, FALSE);                 /* idle the simulator */
            }

        PCQ_ENTRY;                                      /* save P in the queue */
        PR = MR;                                        /*   and jump to the target address */
        break;


    case 0060: case 0061: case 0062: case 0063:         /* IOR */
    case 0064: case 0065: case 0066: case 0067:
    case 0260: case 0261: case 0262: case 0263:         /* IOR,I */
    case 0264: case 0265: case 0266: case 0267:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = AR | ReadW (MR);                       /*   then OR the accumulator and memory */
        break;


    case 0070: case 0071: case 0072: case 0073:         /* ISZ */
    case 0074: case 0075: case 0076: case 0077:
    case 0270: case 0271: case 0272: case 0273:         /* ISZ,I */
    case 0274: case 0275: case 0276: case 0277:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            data = ReadW (MR) + 1 & D16_MASK;           /*   then increment the memory word */
            WriteW (MR, data);                          /*     and write it back */

            if (data == 0)                              /* if the value rolled over to zero */
                PR = PR + 1 & LA_MASK;                  /*   then increment P */
            }
        break;


    case 0100: case 0101: case 0102: case 0103:         /* ADA */
    case 0104: case 0105: case 0106: case 0107:
    case 0300: case 0301: case 0302: case 0303:         /* ADA,I */
    case 0304: case 0305: case 0306: case 0307:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            data = ReadW (MR);                          /*   then get the target word */
            result = AR + data;                         /*     and add the accumulator to memory */

            if (result > D16_UMAX)                      /* if the result overflowed */
                E = 1;                                  /*   then set the Extend register */

            if (~(AR ^ data) & (AR ^ result) & D16_SIGN)    /* if the sign of the result differs from the signs */
                O = 1;                                      /*   of the operands, then set the Overflow register */

            AR = result & R_MASK;                       /* store the sum into the accumulator */
            }
        break;


    case 0110: case 0111: case 0112: case 0113:         /* ADB */
    case 0114: case 0115: case 0116: case 0117:
    case 0310: case 0311: case 0312: case 0313:         /* ADB,I */
    case 0314: case 0315: case 0316: case 0317:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK) {                        /* if the address resolved */
            data = ReadW (MR);                          /*   then get the target word */
            result = BR + data;                         /*     and add the accumulator to memory */

            if (result > D16_UMAX)                      /* if the result overflowed */
                E = 1;                                  /*   then set the Extend register */

            if (~(BR ^ data) & (BR ^ result) & D16_SIGN)    /* if the sign of the result differs from the signs */
                O = 1;                                      /*   of the operands, then set the Overflow register */

            BR = result & R_MASK;                       /* store the sum into the accumulator */
            }
        break;


    case 0120: case 0121: case 0122: case 0123:         /* CPA */
    case 0124: case 0125: case 0126: case 0127:
    case 0320: case 0321: case 0322: case 0323:         /* CPA,I */
    case 0324: case 0325: case 0326: case 0327:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            if (AR != ReadW (MR))                       /*   then if the accumulator and memory differ */
                PR = PR + 1 & LA_MASK;                  /*     then increment P */
        break;


    case 0130: case 0131: case 0132: case 0133:         /* CPB */
    case 0134: case 0135: case 0136: case 0137:
    case 0330: case 0331: case 0332: case 0333:         /* CPB,I */
    case 0334: case 0335: case 0336: case 0337:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            if (BR != ReadW (MR))                       /*   then if the accumulator and memory differ */
                PR = PR + 1 & LA_MASK;                  /*     then increment P */
        break;


    case 0140: case 0141: case 0142: case 0143:         /* LDA */
    case 0144: case 0145: case 0146: case 0147:
    case 0340: case 0341: case 0342: case 0343:         /* LDA,I */
    case 0344: case 0345: case 0346: case 0347:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            AR = ReadW (MR);                            /*   then load the accumulator from memory */
        break;


    case 0150: case 0151: case 0152: case 0153:         /* LDB */
    case 0154: case 0155: case 0156: case 0157:
    case 0350: case 0351: case 0352: case 0353:         /* LDB,I */
    case 0354: case 0355: case 0356: case 0357:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            BR = ReadW (MR);                            /*   then load the accumulator from memory */
        break;


    case 0160: case 0161: case 0162: case 0163:         /* STA */
    case 0164: case 0165: case 0166: case 0167:
    case 0360: case 0361: case 0362: case 0363:         /* STA,I */
    case 0364: case 0365: case 0366: case 0367:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            WriteW (MR, AR);                            /*   then write the accumulator to memory */
        break;


    case 0170: case 0171: case 0172: case 0173:         /* STB */
    case 0174: case 0175: case 0176: case 0177:
    case 0370: case 0371: case 0372: case 0373:         /* STB,I */
    case 0374: case 0375: case 0376: case 0377:
        status = mrg_address ();                        /* get the memory referemce address */

        if (status == SCPE_OK)                          /* if the address resolved */
            WriteW (MR, BR);                            /*   then write the accumulator to memory */
        break;


/* Alter/Skip Group */

    case 0004: case 0005: case 0006: case 0007:         /* ASG */
    case 0014: case 0015: case 0016: case 0017:
        skip = 0;                                       /* assume that no skip is needed */

        rss = (IR & IR_RSS) != 0;                       /* get the Reverse Skip Sense flag */

        ab_selector = AB_SELECT (IR);                   /* get the A/B register selector */
        data = ABREG [ab_selector];                     /*   and the register data */

        if (IR & IR_CLx)                                /* if the CLA/CLB micro-op is enabled */
            data = 0;                                   /*   then clear the value */

        if (IR & IR_CMx)                                /* if the CMA/CMB micro-op is enabled */
            data = data ^ D16_MASK;                     /*   then complement the value */

        if (IR & IR_SEZ && (E == 0) ^ rss)              /* if SEZ[,RSS] is enabled and E is clear [set] */
            skip = 1;                                   /*   then skip the next instruction */

        if (IR & IR_CLE)                                /* if the CLE micro-op is enabled */
            E = 0;                                      /*   then clear E */

        if (IR & IR_CME)                                /* if the CME micro-op is enabled */
            E = E ^ LSB;                                /*   then complement E */

        if ((IR & IR_SSx_SLx_RSS) == IR_SSx_SLx_RSS) {  /* if the SSx, SLx, and RSS micro-ops are enabled together */
            if ((data & D16_SIGN_LSB) == D16_SIGN_LSB)  /*   then if both sign and least-significant bits are set */
                skip = 1;                               /*     then skip the next instruction */
            }

        else {                                              /* otherwise */
            if (IR & IR_SSx && !(data & D16_SIGN) ^ rss)    /*   if SSx[,RSS] is enabled and the MSB is clear [set] */
                skip = 1;                                   /*     then skip the next instruction */

            if (IR & IR_SLx && !(data & LSB) ^ rss)     /*   if SLx[,RSS] is enabled and the LSB is clear [set] */
                skip = 1;                               /*     then skip the next instruction */
            }

        if (IR & IR_INx) {                              /* if the INA/INB micro-op is enabled */
            data = data + 1 & D16_MASK;                 /*   then increment the value */

            if (data == 0)                              /* if the value wrapped around to zero */
                E = 1;                                  /*   then set the Extend register */

            else if (data == D16_SIGN)                  /* otherwise if the value overflowed into the sign bit */
                O = 1;                                  /*   then set the Overflow register */
            }

        if (IR & IR_SZx && (data == 0) ^ rss)           /* if SZx[,RSS] is enabled and the value is zero [non-zero] */
            skip = 1;                                   /*   then skip the next instruction */

        if ((IR & IR_ALL_SKIPS) == IR_RSS)              /* if RSS is present without any other skip micro-ops */
            skip = 1;                                   /*   then skip the next instruction unconditionally */

        ABREG [ab_selector] = data;                     /* store the result in the selected register */
        PR = PR + skip & LA_MASK;                       /*   and skip the next instruction if indicated */
        break;


/* Shift/Rotate Group */

    case 0000: case 0001: case 0002: case 0003:         /* SRG */
    case 0010: case 0011: case 0012: case 0013:
        ab_selector = AB_SELECT (IR);                   /* get the A/B register selector */
        data = ABREG [ab_selector];                     /*   and the register data */

        data = srg_uop (data, SRG1 (IR));               /* do the first shift */

        if (IR & SRG_CLE)                               /* if the CLE micro-op is enabled */
            E = 0;                                      /*   then clear E */

        if (IR & SRG_SLx && (data & LSB) == 0)          /* if SLx is enabled and the LSB is clear */
            PR = PR + 1 & LA_MASK;                      /*   then skip the next instruction */

        ABREG [ab_selector] = srg_uop (data, SRG2 (IR));    /* do the second shift and set the accumulator */
        break;


/* I/O Group */

    case 0204: case 0205: case 0206: case 0207:         /* IOG */
    case 0214: case 0215: case 0216: case 0217:
        status = cpu_iog (IR);                          /* execute the I/O instruction */
        break;


/* Extended Arithmetic Group */

    case 0200:                                          /* EAU group 0 */
    case 0201:                                          /* DIV */
    case 0202:                                          /* EAU group 2 */
    case 0210:                                          /* DLD */
    case 0211:                                          /* DST */
        status = cpu_eau ();                            /* execute the extended arithmetic instruction */
        break;


/* User Instruction Group */

    case 0212:                                              /* UIG 0 */
        status = cpu_uig_0 (interrupt_request, int_ack);    /* execute the user instruction opcode */
        break;


    case 0203:
    case 0213:                                          /* UIG 1 */
        status = cpu_uig_1 (interrupt_request);         /* execute the user instruction opcode */
        break;
    }                                                   /* all cases are handled */


return status;                                          /* return the execution status */
}


/* Get the effective address from an MRG instruction.

   Memory references are contained in bits 15, 10, and 9-0 of instructions in
   the Memory Reference Group.  Bits 9-0 specify one of 1024 locations within
   either the base page (if bit 10 is 0) or the current page (if bit 10 is 1).
   If bit 15 is 0, the address is direct.  If bit 15 is 1, then the address is
   indirect and specifies the location containing the target address.  That
   address itself may be direct or indirect as indicated by bit 15, with bits
   14-0 specifying a location within the 32K logical address space.

   On entry, the instruction register (IR) contains the MRG instruction to
   resolve, and the memory address register (MR) contains the address of the
   instruction.  This routine examines the instruction; if the address is
   direct, then the full 15-bit address is returned in the MR register.
   Otherwise, the indirect chain is followed until the address is direct, a
   pending interrupt is recognized, or the length of the indirect address chain
   exceeds the alllowable limit.  The resulting direct address is returned in
   the MR register.
*/

static t_stat mrg_address (void)
{
if (IR & IR_CP)                                         /* if the instruction specifies the current page */
    MR = IR & (IR_IND | IR_OFFSET) | MR & MR_PAGE;      /*   then merge the current page and the instruction offset */
else                                                    /* otherwise */
    MR = IR & (IR_IND | IR_OFFSET);                     /*   the offset is on the base page */

if (MR & IR_IND)                                        /* if the address is indirect */
    return cpu_resolve_indirects (TRUE);                /*   then resolve it to a direct address with interruptibility */
else                                                    /* otherwise */
    return SCPE_OK;                                     /*   the address in MR is already direct */
}


/* Execute a Shift-Rotate Group micro-operation.

   SRG instructions consist of two shift/rotate micro-operations plus a CLE and
   a SLA/SLB micro-op.  This routine implements the shift and rotate operation.

   Each of the two shift-rotate operations has an enable bit that must be set to
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
       the first shift-rotate micro-op, but it is spaced one bit away from the
       encoded operation for the second micro-op.  It is faster to decode
       separate values for each location rather than move the second enable bit
       adjacent to its encoded operation.  The former imposes no time penalty;
       the jump table for the "switch" statement is simply somewhat larger.
*/

static HP_WORD srg_uop (HP_WORD value, HP_WORD operation)
{
uint32 extend;

switch (operation) {                                        /* dispatch on the micro operation */

    case SRG1_EN | IR_xLS:
    case SRG2_EN | IR_xLS:                                  /* ALS/BLS */
        return value & D16_SIGN | value << 1 & D16_SMAX;    /* arithmetic left shift */


    case SRG1_EN | IR_xRS:
    case SRG2_EN | IR_xRS:                                  /* ARS/BRS */
        return value & D16_SIGN | value >> 1;               /* arithmetic right shift */


    case SRG1_EN | IR_RxL:
    case SRG2_EN | IR_RxL:                                  /* RAL/RBL */
        return (value << 1 | value >> 15) & D16_MASK;       /* rotate left */


    case SRG1_EN | IR_RxR:
    case SRG2_EN | IR_RxR:                                  /* RAR/RBR */
        return (value >> 1 | value << 15) & D16_MASK;       /* rotate right */


    case SRG1_EN | IR_xLR:
    case SRG2_EN | IR_xLR:                                  /* ALR/BLR */
        return value << 1 & D16_SMAX;                       /* arithmetic left shift, clear sign */


    case SRG_DIS | IR_ERx:                                  /* disabled ERA/ERB */
        E = value & LSB;                                    /* rotate the LSB right into E */
        return value;                                       /*   and return the original value */


    case SRG1_EN | IR_ERx:
    case SRG2_EN | IR_ERx:                                  /* ERA/ERB */
        extend = E;                                         /* save the original E value */
        E = value & LSB;                                    /* rotate the LSB right into E */
        return value >> 1 | (HP_WORD) extend << 15;         /*   and rotate right with E filling the MSB */


    case SRG_DIS | IR_ELx:                                  /* disabled ELA/ELB */
        E = value >> 15 & LSB;                              /* rotate the MSB left into E */
        return value;                                       /*   and return the original value */


    case SRG1_EN | IR_ELx:
    case SRG2_EN | IR_ELx:                                  /* ELA/ELB */
        extend = E;                                         /* save the original E value */
        E = value >> 15 & LSB;                              /* rotate the MSB left into E */
        return (value << 1 | (HP_WORD) extend) & D16_MASK;  /*   and rotate left with E filling the LSB */


    case SRG1_EN | IR_xLF:
    case SRG2_EN | IR_xLF:                                  /* ALF/BLF */
        return (value << 4 | value >> 12) & D16_MASK;       /* rotate left four */


    default:                                                /* all other (disabled) cases */
        return value;                                       /*   return the original value */
    }
}


/* Reenable interrupt recognition.

   Certain instructions clear the "cpu_interrupt_enable" flag to defer
   recognition of pending interrupts until the succeeding instruction completes.
   However, the interrupt deferral rules differ for the 21xx vs. the 1000.

   The 1000 always defers until the completion of the instruction following the
   deferring instruction.  The 21xx defers unless the following instruction is
   an MRG instruction other than JMP or JMP,I or JSB,I.  If it is, then the
   deferral is inhibited, i.e., the pending interrupt will be recognized.

   In either case, if the interrupting device is the memory protect card, or if
   the INT jumper is out on the 12892B MP card, then a pending interrupt is
   always recognized, regardless of the "cpu_interrupt_enable" flag setting.

   See the "Set Phase Logic Flowchart" for the transition from phase 1A to phase
   1B, and "Section III Theory of Operation," "Control Section Detailed Theory"
   division, "Phase Control Logic" subsection, "Phase 1B" paragraph (3-241) in
   the Model 2100A Computer Installation and Maintenance Manual for details.
*/

static t_bool reenable_interrupts (void)
{
HP_WORD next_instruction;

if (!(cpu_configuration & CPU_1000)) {                  /* if the CPU is a 21xx model */
    next_instruction = mem_fast_read (PR, Current_Map); /*   then prefetch the next instruction */

    if (MRGOP (next_instruction)                        /* if it is an MRG instruction */
      && (next_instruction & IR_MRG_I) != IR_JSB_I      /*   but not JSB,I */
      && (next_instruction & IR_MRG)   != IR_JMP)       /*   and not JMP or JMP,I */
        return TRUE;                                    /*     then reenable interrupts */
    }

if (interrupt_request == MPPE || mp_reenable_interrupts ()) /* if MP is interrupting or the INT jumper is out */
    return TRUE;                                            /*   then reenable interrupts */
else                                                        /* otherwise */
    return FALSE;                                           /*   interrupts remain disabled */
}



/* I/O subsystem local utility routine declarations */


/* Initialize the I/O system.

   This routine is called in the instruction prelude to set up the I/O access
   table prior to beginning execution.  The table is indexed by select code, and
   each entry records pointers to the device and DIB structures assoicated with
   that select code.  This allows fast access to the device trace flags and to
   the device interface routine by the I/O instruction executors, respectively.

   As part of the device scan, the sizes of the largest device name and active
   trace flag name among the devices enabled for tracing are accumulated for use
   in aligning the trace statements.

   The "is_executing" parameter indicates whether or not initialization is being
   performed from the instruction execution prelude.  If it is, the routine also
   sets the priority holdoff and interrupt request bit vectors by asserting the
   SIR signal to each enabled device interface routine.  Sending SIR to all
   devices will set the "interrupt_request" variable if an interrupt is pending,
   so no explicit poll is needed after initialization.

   After initializing the I/O access table, a check is made for device
   conflicts.  These occur if two or more devices are assigned to the same
   select code.

   Each select code must be unique among the enabled devices.  This requirement
   is checked as part of the instruction execution prelude; this allows the user
   to exchange two select codes simply by setting each device to the other's
   select code.  If conflicts were enforced instead at the time the codes were
   entered, the first device would have to be set to an unused select code
   before the second could be set to the first device's code.

   If any conflicts exist, the device table is scanned to find the DIBs whose
   select codes match the conflicting values, and the device names associated
   with the conflicts are printed.

   This routine returns the success or failure of I/O initialization; failure
   is reported if any select code conflicts exist.


   Implementation notes:

    1. If this routine is called from the instruction prelude, the console and
       optional log file have already been put into "raw" output mode.
       Therefore, newlines are not translated to the correct line ends on
       systems that require it.  Before reporting a conflict, "sim_ttcmd" is
       called to restore the console and log file translation.  This is OK
       because a conflict will abort the run and return to the command line
       anyway.

    2. sim_dname is called instead of using dptr->name directly to ensure that
       we pick up an assigned logical device name.

    3. Only the names of active trace (debug) options are accumulated to produce
       the most compact trace log.  However, if the CPU device's EXEC option is
       enabled, then all of the CPU option names are accumulated, as EXEC
       enables all trace options for a given instruction or instruction class.
*/

static t_bool initialize_io (t_bool is_executing)
{
DEVICE       *dptr;
DIB          *dibptr;
const DEBTAB *tptr;
uint32       dev, sc, count;
size_t       device_length, flag_length, device_size, flag_size;
t_bool       is_conflict = FALSE;

interrupt_request_set [0] = interrupt_request_set [1] = 0;  /* clear all interrupt requests */
priority_holdoff_set  [0] = priority_holdoff_set  [1] = 0;  /* clear all priority inhibits */

device_size = 0;                                        /* reset the device and flag name sizes */
flag_size = 0;                                          /*   to those of the devices actively tracing */

memset (&iot [2], 0, sizeof iot - 2 * sizeof iot [0]);  /* clear the I/O pointer table */

for (dev = 0; sim_devices [dev] != NULL; dev++) {       /* loop through all of the devices */
    dptr = sim_devices [dev];                           /* get a pointer to the device */
    dibptr = (DIB *) dptr->ctxt;                        /*   and to that device's DIB */

    if (dibptr != NULL && !(dptr->flags & DEV_DIS)) {   /* if the DIB exists and the device is enabled */
        sc = dibptr->select_code;
        iot [sc].devptr = dptr;                         /*   then set the device pointer into the device table */
        iot [sc].dibptr = dibptr;                       /*     and set the DIB pointer into the dispatch table */

        if (sc >= SC_VAR && ++iot [sc].references > 1)  /* increment the count of references; if more than one */
            is_conflict = TRUE;                         /*   then a conflict occurs */

        if (is_executing)                               /* if the CPU is executing instructions */
            io_assert (dptr, ioa_SIR);                  /*   then set the interrupt request state */
        }

    if (sim_deb && dptr->dctrl) {                       /* if tracing is active for this device */
        device_length = strlen (sim_dname (dptr));      /*   then get the length of the device name */

        if (device_length > device_size)                /* if it's greater than the current maximum */
            device_size = device_length;                /*   then reset the size */

        if (dptr->debflags)                             /* if the device has a trace flags table */
            for (tptr = dptr->debflags;                 /*   then scan the table */
                 tptr->name != NULL; tptr++)
                if (dev == 0 && dptr->dctrl & TRACE_EXEC    /* if the CPU device is tracing executions */
                  || tptr->mask & dptr->dctrl) {            /*   or this trace option is active */
                    flag_length = strlen (tptr->name);      /*     then get the flag name length */

                    if (flag_length > flag_size)            /* if it's greater than the current maximum */
                        flag_size = flag_length;            /*   then reset the size */
                    }
        }
    }


if (is_conflict) {                                      /* if a conflict exists */
    if (is_executing)                                   /*   then if execution has started */
        sim_ttcmd ();                                   /*     then restore the console and log I/O mode */

    for (sc = 0; sc <= SC_MAX; sc++)                    /* search the conflict table for the next conflict */
        if (iot [sc].references > 1) {                  /* if a conflict is present for this value */
            count = iot [sc].references;                /*   then get the number of conflicting devices */

            cprintf ("Select code %o conflict (", sc);  /* report the multiply-assigned select code */

            for (dev = 0; sim_devices [dev] != NULL; dev++) {   /* loop through all of the devices */
                dptr = sim_devices [dev];                       /* get a pointer to the device */
                dibptr = (DIB *) dptr->ctxt;                    /*   and to that device's DIB */

                if (dibptr != NULL && !(dptr->flags & DEV_DIS)  /* if the DIB exists and the device is enabled */
                  && dibptr->select_code == sc) {               /*   to find the conflicting entries */
                    if (count < iot [sc].references)            /*     and report them to the console */
                        cputs (" and ");

                    cputs (sim_dname (dptr));           /* report the conflicting device name */
                    count = count - 1;                  /*   and drop the count of remaining conflicts */

                    if (count == 0)                     /* if all devices have been reported */
                        break;                          /*   then there's no need to look farther */
                    }
                }                                       /* loop until all conflicting devices are reported */

            cputs (")\n");                              /* tie off the line */
            }                                           /*   and continue to look for other conflicting select codes */

    return FALSE;                                       /* report that initialization has failed */
    }

else {                                                  /* otherwise no conflicts were found */
    iot [PWR].devptr = &cpu_dev;                        /* for now, powerfail is always present */
    iot [PWR].dibptr = &pwrf_dib;                       /*   and is controlled by the CPU */

    if (iot [DMA1].devptr) {                            /* if the first DMA channel is enabled */
        iot [DMALT1] = iot [DMA1];                      /*   then set up  */
        iot [DMALT1].dibptr++;                          /*     the secondary device handler */
        }

    if (iot [DMA2].devptr) {                            /* if the second DMA channel is enabled */
        iot [DMALT2] = iot [DMA2];                      /*   then set up  */
        iot [DMALT2].dibptr++;                          /*     the secondary device handler */
        }

    hp_initialize_trace (device_size, flag_size);       /* initialize the trace routine */
    return TRUE;                                        /*   and report that initialization has succeeded */
    }
}
