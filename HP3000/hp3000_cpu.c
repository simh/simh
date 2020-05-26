/* hp3000_cpu.c: HP 3000 Central Processing Unit simulator

   Copyright (c) 2016-2019, J. David Bryan

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
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   CPU          HP 3000 Series III Central Processing Unit

   21-Feb-19    JDB     Resuming into PAUS with pending IRQ now sets P correctly
                        Moved "debug_save" from global to "sim_instr" local
   12-Feb-19    JDB     Worked around idle problem (SIMH issue 622)
   06-Feb-19    JDB     Corrected trace report for simulation stop
   27-Dec-18    JDB     Revised fall through comments to comply with gcc 7
   05-Sep-17    JDB     Removed the -B (binary display) option; use -2 instead
                        Changed REG_A (permit any symbolic override) to REG_X
   19-Jan-17    JDB     Added comments describing the OPND and EXEC trace options
   29_Dec-16    JDB     Changed the status mnemonic flag from REG_S to REG_T
   07-Nov-16    JDB     Renamed cpu_byte_to_word_ea to cpu_byte_ea
   03-Nov-16    JDB     Added zero offsets to the cpu_call_procedure calls
   01-Nov-16    JDB     Added per-instruction trace capability
   24-Oct-16    JDB     Renamed SEXT macro to SEXT16
   22-Sep-16    JDB     Moved byte_to_word_address from hp3000_cpu_base.c
   21-Sep-16    JDB     Added CIS/NOCIS option
   01-Sep-16    JDB     Add power fail/power restore support
   23-Aug-16    JDB     Add module interrupt support
   14-Jul-16    JDB     Implemented the cold dump process
                        Changed "loading" EXEC_STATE to "waiting"
   11-Jul-16    JDB     Change "cpu_unit" from a UNIT to an array of one UNIT
   08-Jun-16    JDB     Corrected %d format to %u for unsigned values
   16-May-16    JDB     ACCESS_PROPERTIES.name is now a pointer-to-constant
   13-May-16    JDB     Modified for revised SCP API function parameter types
   24-Mar-16    JDB     Changed memory word type from uint16 to MEMORY_WORD
   21-Mar-16    JDB     Changed cpu_ccb_table type from uint16 to HP_WORD
   11-Mar-16    JDB     Fixed byte EA calculations with negative indexes
   22-Dec-15    JDB     First release version
   01-Apr-15    JDB     First successful run of MPE-V/R through account login
   11-Dec-12    JDB     Created

   References:
     - HP 3000 Series II System Microprogram Listing
         (30000-90023, August 1976)
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - Machine Instruction Set Reference Manual
         (30000-90022, June 1984)


   The HP 3000 is a family of general-purpose business computers that were sold
   by Hewlett-Packard from 1972 through 2001.  There are two major divisions
   within this family: the "classic" 16-bit, stack-oriented CISC machines, and
   the "Precision Architecture" 32-bit, register-oriented RISC machines that
   succeeded them.  All machines run versions of MPE, the "Multiprogramming
   Executive" operating system.

   Within the "classic" division, there are two additional subdivisions, based
   on the method used for peripheral connections: the original "SIO" machines,
   and the later "HP-IB" machines.  The I/O interfacing hardware differs between
   the two types of machines, as do the privileged I/O machine instructions.
   The user instruction sets are identical, as are the register sets visible to
   the programmer.  The I/O drivers are different to account for the hardware
   differences, and therefore they run slightly different versions of MPE.

   This implementation is a simulator for the classic SIO machines.  This group
   consists of the 3000 CX, the Series I, Series II, and Series III; the last is
   simulated here.  The CX and Series I, which is a repackaged CX, are
   essentially subsets of the Series II/III -- a smaller instruction set,
   limited memory size, and lower-precision floating-point instructions.
   Simulation of these machines may be added in the future.  Future simulation
   of the HP-IB machines (the Series 30 through 70) is desirable, as the latest
   MPE versions run only on these machines, but documentation on the internals
   of the HP-IB hardware controllers is nonexistent.

   The CX and Series I support 64K 16-bit words of memory.  The Series II
   supports up to 256K, divided into four banks of 64K words each, and the
   Series III extends this to 1024K words using 16 banks.  Memory is divided
   into variable-length code and data segments, with the latter containing a
   program's global data area and stack.

   Memory protection is accomplished by checking program, data, and stack
   accesses against segment base and limit registers, which can be set only by
   MPE.  Bounds violations cause automatic hardware traps to a handler routine
   within MPE.  Some violations may be permitted; for example, a Stack Overflow
   trap may cause MPE to allocate a larger stack and then restart the
   interrupted instruction.  Almost all memory references are position-
   independent, so moving segments to accommodate expansion requires only
   resetting of the segment registers to point at the new locations.  Code
   segments are fully reentrant and shareable, and both code and data are
   virtual, as the hardware supports absent code and data segment traps.

   The classic 3000s are stack machines.  Most of the instructions operate on
   the value on the top of the stack (TOS) or on the TOS and the next-to-the-top
   of the stack (NOS).  To improve execution speed, the 3000 has a set of
   hardware registers that are accessed as the first four locations at the top
   of the stack, while the remainder of the stack locations reside in main
   memory.  A hardware register renamer provides fast stack pushes and pops
   without physically copying values between registers.

   In hardware, the stack registers are referenced internally as TR0-TR3 and
   externally as RA-RD.  An access to the RA (TOS) register is translated by the
   renamer to access TR0, TR1, etc. depending on which internal register is
   designated as the current top of the stack.  For example, assume that RA
   corresponds to TR0.  To push a new value onto the top of the stack, the
   renamer is adjusted so that RA corresponds to TR1, and the new value is
   stored there.  In this state, RB corresponds to TR0, the previous TOS (and
   current NOS) value.  Additional pushes rename RA as TR2 and then TR3, with RB
   being renamed to TR1 and then TR2, and similarly for RC and RD.  The number
   of valid TOS registers is given by the value in the SR register, and the
   first stack location in memory is given by the value in the SM register.
   Pops reverse the sequence: a pop with RA corresponding to TR3 renames RA to
   TR2, RB to TR1, etc.  When all four stack registers are in use, a push will
   copy the value in RD to the top of the memory stack (SM + 1) before the
   registers are renamed and the new value stored into RA.  Similarly, when all
   four stack registers are empty, a pop simply decrements the SM register,
   effectively deleting the top of the memory stack.

   Because of the renamer, the microcode can always use RA to refer to the top
   of the stack, regardless of which internal TR register is being used, and
   similarly for RB-RD.  Execution of each stack instruction begins with a
   preadjustment, if needed, that loads the TOS registers that will be used by
   the instruction from the top of the memory stack.  For example, if only RA
   contains a value (i.e., the SR register value is 1), the ADD instruction,
   which adds the values in RA and RB, will load RB with the value on the top of
   the memory stack, the SR count will be incremented, and the SM register will
   be decremented.  On the other hand, if both RA and RB contained values (SR >=
   2), then no preadjustment would be required before the ADD instruction
   microcode manipulated RA and RB.

   In simulation, the renamer is implemented by physically copying the values
   between registers, as this is much faster than mapping via array index
   values, as the hardware does.  A set of functions provides the support to
   implement the hardware stack operations:

     cpu_push       - empty the TOS register (SR++, caller stores into RA)
     cpu_pop        - delete the TOS register (SR--)
     cpu_queue_up   - move from memory to the lowest TOS register (SR++, SM--)
     cpu_queue_down - move from the lowest TOS register to memory (SR--, SM++)
     cpu_flush      - move all stack registers to memory (SR = 0 on return)
     cpu_adjust_sr  - queue up until the required SR count is reached
     cpu_mark_stack - write a stack marker to memory

   The renamer is described in US Patent 3,737,871 (Katzman, "Stack Register
   Renamer", June 1973).

   The MPE operating system is supported by special microcode features that
   perform code and data segment mapping, segment access bounds checking,
   privilege checking, etc.  The layout of certain in-memory tables is known to
   both the OS and the microcode and is used to validate execution of
   instructions.  For instance, every stack instruction is checked for a valid
   access within the stack segment boundaries, which are set up by the OS
   before program dispatch.  For this reason, the 3000 cannot be operated as a
   "bare" machine, because these tables would not have been initialized.
   Similarly, the "cold load" process by which the OS is loaded from storage
   media into memory is entirely in microcode, as machine instructions cannot be
   executed until the required tables are loaded into memory.

   This OS/microcode integration means that the microcode may detect conditions
   that make continued execution impossible.  An example would be a not-present
   segment fault for the segment containing the disc I/O driver.  If such a
   condition is detected, the CPU does a "system halt."  This fatal microcode
   error, distinct from a regular programmed halt, causes operation to cease
   until the CPU is reset.

   The CPU hardware includes a free-running 16-bit process clock that increments
   once per millisecond whenever a user program is executing.  MPE uses the
   process clock to charge CPU usage to programs, and thereby to users, groups,
   and accounts.  Instructions are provided to read (RCLK) and set (SCLK) the
   process clock.


   The data types supported by the instruction set are:

     - 8-bit unsigned byte
     - 16-bit unsigned integer ("logical" format)
     - 16-bit two's-complement integer
     - 32-bit two's-complement integer
     - 32-bit sign-magnitude floating point
     - 64-bit sign-magnitude floating point

   Multi-word values are stored in memory with the most-significant words in the
   lowest addresses.  The stack is organized in memory from lower to higher
   addresses, so a value on the stack has its least-significant word on the top
   of the stack.

   Machine instructions are initially decoded by a sub-opcode contained in the
   four highest bits, as follows (note that the HP 3000 numbers bits from
   left-to-right, i.e., bit 0 is the MSB and bit 15 is the LSB):

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0 |   1st stack opcode    |   2nd stack opcode    |  Stack
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | X |   shift opcode    |      shift count      |  Shift
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | I |   branch opcode   |+/-|  P displacement   |  Branch
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | X |  bit test opcode  |     bit position      |  Bit Test
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  move op  | opts/S decrement  |  Move
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  special op   | 0   0 | sp op |  Special
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 |  option set   | option subset |  Firmware
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 |  imm opcode   |       immediate operand       |  Immediate
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | field opcode  |    J field    |    K field    |  Field
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 |  register op  | SK| DB| DL| Z |STA| X | Q | S |  Register
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | 0   0   0   0 |  I/O opcode   |    K field    |  I/O
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | 0   0   0   0 |  cntl opcode  | 0   0 | cn op |  Control
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 |  program op   |            N field            |  Program
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | immediate op  |       immediate operand       |  Immediate
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 |   memory op   |        P displacement         |  Memory
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     The memory, loop, and branch instructions occupy the remainder of the
     sub-opcodes (04-17):

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   memory op   | X | I |         mode and displacement         |  Memory
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             | 0 | 0 |     P+ displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 0 | 1 |     P- displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 0 |    DB+ displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 0 |   Q+ displacement 0-127   |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 1 | 0 | Q- displacement 0-63  |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 1 | 1 | S- displacement 0-63  |
                             +---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   memory op   | X | I | s |     mode and displacement         |   Memory
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                 | 0 |    DB+ displacement 0-255     |
                                 +---+---+---+---+---+---+---+---+---+
                                 | 1 | 0 |   Q+ displacement 0-127   |
                                 +---+---+---+---+---+---+---+---+---+
                                 | 1 | 1 | 0 | Q- displacement 0-63  |
                                 +---+---+---+---+---+---+---+---+---+
                                 | 1 | 1 | 1 | S- displacement 0-63  |
                                 +---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   1   0   1 |loop op| 0 |+/-|    P-relative displacement    |  Loop
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   0   0 | I | 0   1 | > | = | < | P+- displacement 0-31 |  Branch
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Optional firmware extension instruction sets occupy instruction codes
   020400-020777, except for the DMUL (020570) and DDIV (020571) instructions
   that are part of the base set, as follows:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   1   1   1 | 1   0   0 | x |  DMUL/DDIV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   0 | 1 | ext fp op |  Extended FP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   0   1 |    APL op     |  APL
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 |   COBOL op    |  COBOL
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 1 |  options  |  decimal op   |  Decimal
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Most instructions are defined by unique 16-bit codes that specify the
   operations, operands, addressing modes, shift counts, etc.  For each of these
   instructions, there is only one canonical form that encodes a given
   instruction (or instruction pair, in the case of stack instructions).  For
   example, the octal value 140003 is the only value that encodes the "BR P+3"
   instruction.

   There are also instruction codes that contain one or more bits designated as
   "reserved" in the Machine Instruction Set Reference Manual.  For some of
   these instructions, the reserved bits do not affect instruction decoding.
   Each instruction of this type has a defined canonical form -- typically with
   the reserved bits set to zero -- but will execute identically if one of the
   undefined forms is used.  For example, the "MOVE" instructions define bits
   12-13 as 00, but the bits are not decoded, so values of 01, 10, and 11 for
   these bits will result in identical execution.  Specifically, "MOVE 0" is
   encoded canonically as octal value 020020, but the undefined codes 020024,
   020030, and 020034 execute "MOVE 0" as well.

   For the rest of these instructions, the reserved bits are decoded and will
   affect the instruction interpretation.  An example of this is the "IXIT"
   instruction.  It also defines bits 12-13 as 00 (canonical encoding 020360),
   but in this case the bits must be 00 for the instruction to execute; any
   other value (e.g., undefined codes 020364, 020370, or 020374) executes as a
   "PCN" instruction, whose canonical encoding is 020362.

   Finally, some codes are not assigned to any instructions, or they are
   assigned to instructions that are supplied by optional firmware that is not
   present in the machine.  These instruction codes are designated as
   "unimplemented" and will cause Unimplemented Instruction traps if they are
   executed.  Examples are the stack operation code 072 in either the left-hand
   or right-hand position (i.e., instruction codes 0072xx and 00xx72), codes
   020410-020415 if the Extended Instruction Set firmware option is not
   installed, and codes 036000-036777.

   When the simulator examines the bit patterns of instructions to execute, each
   will fall into one of four categories:

    1. Defined (canonical) instruction encodings.

    2. Undefined (non-canonical) instruction encodings, where reserved fields
       are "don't care" bits (e.g., MOVE).

    3. Undefined (non-canonical) instruction encodings, where reserved fields
       are decoded (e.g., IXIT).

    4. Unimplemented instruction encodings (e.g., stack opcode 072 or EADD with
       no EIS installed).

   When examining memory or register values in instruction-mnemonic form, the
   names of the canonical instructions in category 1 are displayed in uppercase,
   as are the names of the non-canonical instructions in category 2.  The
   non-canonical instruction names in category 3 are displayed in lowercase.
   This is to indicate to the user that the instructions that will be executed
   may not be the ones expected from the decoding.  Instruction names in
   category 4 that correspond to supported firmware options are displayed in
   uppercase, regardless of whether or not the option is enabled.  Category 4
   encodings that do not correspond to instructions are displayed in octal.

   All of the base set instructions are one word in length.  Most of the
   firmware extension instructions occupy one word as well, although some are
   two words long.  If the first word of a two-word instruction is valid but the
   second is not, an Unimplemented Instruction trap will occur, with P pointing
   to the location after the second word.


   The simulator provides four stop conditions related to instruction execution
   that may be enabled with a SET CPU STOP=<stop> command:

     <stop>  Action
     ------  ------------------------------------
     LOOP    stop on an infinite loop
     PAUSE   stop on a PAUS instruction
     UNDEF   stop on an undefined instruction
     UNIMPL  stop on an unimplemented instruction

   If an enabled stop condition is detected, execution ceases with the
   instruction pending, and control returns to the SCP prompt.  When simulation
   stops, execution may be resumed in two ways.  If the cause of the stop has
   not been remedied and the stop has not been disabled, resuming execution with
   CONTINUE, STEP, GO, or RUN will cause the stop to occur again.  Alternately,
   specifying the "-B" switch with any of the preceding commands will resume
   execution while bypassing the stop for the current instruction.

   The LOOP option stops the simulator if it attempts to execute an instruction
   that enters an infinite loop (e.g., BR P+0).  The branch instructions TBA,
   TBX, BCC, BR, BCY, BNCY, BOV, and BNOV result in an infinite loop if the
   branch displacement is zero and the branch condition is true.  The remaining
   branch instructions cannot result in an infinite loop, as they all modify the
   CPU state and so eventually reach a point where they drop out of the loop.

   The PAUSE option stops the simulator if execution of a PAUS instruction is
   attempted.  This instruction normally suspends instruction fetching until an
   interrupt occurs.  Clearing the stop and resuming execution suspends the
   fetch/execute process until an external interrupt occurs.  Resuming with the
   stop bypassed continues execution with the instruction following the PAUS;
   this is the same action that occurs when pressing the HALT button and then
   the RUN button in hardware.

   The UNDEF option stops the simulator if execution of a non-canonical
   instruction from decoding category 3 (i.e., an instruction containing a
   decoded reserved bit pattern other than that defined in the Machine
   Instruction Set manual) is attempted.  The intent is to catch instructions
   containing reserved fields with values that change the meaning of those
   instructions.  Bypassing the stop will decode and execute the instruction in
   the same manner as the HP 3000 microcode.

   The UNIMPL option stops the simulator if execution of an instruction from
   decoding category 4 is attempted.  Bypassing the stop will cause an
   Unimplemented Instruction trap.  Instructions that depend on the presence of
   firmware options are implemented if the option is present and unimplemented
   if the option is absent.  For example, instruction code 020410 executes as
   the EADD instruction if the Extended Instruction Set is enabled but is
   unimplemented if the EIS is disabled.  It is displayed as EADD in either
   case.

   The instructions in category 2 whose non-canonical forms do not cause an
   UNDEF stop are:

           Canonical  Reserved
     Inst  Encoding     Bits    Defined As    Decoded As
     ----  ---------  --------  -----------   -----------
     SCAN   010600     10-15    0 0 0 0 0 0   x x x x x x
     TNSL   011600     10-15    0 0 0 0 0 0   x x x x x x
     MOVE   020000     12-13        0 0           x x
     MVB    020040     12-13        0 0           x x
     MVBL   020100        13          0             x
     SCW    020120        13          0             x
     MVLB   020140        13          0             x
     SCU    020160        13          0             x
     CMPB   020240     12-13        0 0           x x
     RSW    020300     12-14        0 0 0         x x x
     LLSH   020301     12-14        0 0 0         x x x
     PLDA   020320     12-14        0 0 0         x x x
     PSTA   020321     12-14        0 0 0         x x x
     LSEA   020340     12-13        0 0           x x
     SSEA   020341     12-13        0 0           x x
     LDEA   020342     12-13        0 0           x x
     SDEA   020343     12-13        0 0           x x
     PAUS   030020     12-15        0 0 0 0       x x x x

   The instructions in category 3 whose non-canonical forms cause an UNDEF stop
   are:

           Canonical  Reserved
     Inst  Encoding     Bits    Defined As  Decoded As
     ----  ---------  --------  ----------  ----------
     IXIT   020360     12-15     0 0 0 0     0 0 0 0
     LOCK   020361     12-15     0 0 0 1     n n 0 1
     PCN    020362     12-15     0 0 1 0     n n n 0
     UNLK   020363     12-15     0 0 1 1     n n 1 1

     SED    030040     12-15     0 0 0 x     n n n x

     XCHD   030060     12-15     0 0 0 0     0 0 0 0
     PSDB   030061     12-15     0 0 0 1     n n 0 1
     DISP   030062     12-15     0 0 1 0     n n n 0
     PSEB   030063     12-15     0 0 1 1     n n 1 1

     SMSK   030100     12-15     0 0 0 0     0 0 0 0
     SCLK   030101     12-15     0 0 0 1     n n n n
     RMSK   030120     12-15     0 0 0 0     0 0 0 0
     RCLK   030121     12-15     0 0 0 1     n n n n

   Where:

     x = 0 or 1
     n = any collective value other than 0

   In hardware, the SED instruction works correctly only if opcodes 030040 and
   030041 are used.  Opcodes 030042-030057 also decode as SED, but the status
   register is set improperly (the I bit is cleared, bits 12-15 are rotated
   right twice and then ORed into the status register).  In simulation, opcodes
   030042-030057 work correctly but will cause an UNDEF simulation stop if
   enabled.


   The CPU simulator provides extensive tracing capabilities that may be enabled
   with the SET CONSOLE DEBUG=<filename> and SET CPU DEBUG=<trace> commands.
   The trace options that may be specified are:

     Trace  Action
     -----  -------------------------------------------
     INSTR  trace instructions executed
     DATA   trace memory data accesses
     FETCH  trace memory instruction fetches
     REG    trace registers
     OPND   trace memory operands
     EXEC   trace matching instruction execution states
     PSERV  trace process clock service events

   A section of an example trace is:

     >>CPU fetch: 00.010342  020320    instruction fetch
     >>CPU instr: 00.010341  000300  ZROX,NOP
     >>CPU   reg: 00.006500  000000    X 000000, M i t r o c CCG
     >>CPU fetch: 00.010343  041100    instruction fetch
     >>CPU instr: 00.010342  020320  PLDA
     >>CPU  data: 00.000000  001340    absolute read
     >>CPU   reg: 00.006500  000001    A 001340, X 000000, M i t r o c CCG
     >>CPU fetch: 00.010344  037777    instruction fetch
     >>CPU instr: 00.010343  041100  LOAD DB+100
     >>CPU  data: 00.002100  123003    data read
     >>CPU   reg: 00.006500  000002    A 123003, B 001340, X 000000, M i t r o c CCL
     >>CPU fetch: 00.010345  023404    instruction fetch
     >>CPU instr: 00.010344  037777  ANDI 377
     >>CPU   reg: 00.006500  000002    A 000003, B 001340, X 000000, M i t r o c CCG
     >>CPU fetch: 00.010346  002043    instruction fetch
     >>CPU instr: 00.010345  023404  MPYI 4
     >>CPU   reg: 00.006500  000002    A 000014, B 001340, X 000000, M i t r o c CCG
     >>CPU fetch: 00.010347  020320    instruction fetch

   The INSTR option traces instruction executions.  Each instruction is printed
   before it is executed.  The two opcodes of a stack instruction are printed
   together before the left-hand opcode is executed.  If the right-hand opcode
   is not NOP, it is reprinted before execution, with dashes replacing the
   just-executed left-hand opcode.

   The DATA option traces reads from and writes to memory.  Each access is
   classified by the memory bank register that is paired with the specified
   offset; they are: dma, absolute, program, data, and stack.  DMA accesses
   derive their bank addresses from the banks specified in Set Bank I/O program
   orders.  Absolute accesses always use bank 0.  Program, data, and stack
   accesses use the bank addresses in the PBANK, DBANK, and SBANK registers,
   respectively.

   The FETCH option traces instruction fetches from memory.  These accesses are
   separated from those traced by the DATA option because fetches usually are of
   little interest except when debugging the fetch/execute sequence.  Because
   the HP 3000 has a two-stage pipeline, fetches load the NIR (Next Instruction
   Register) with the instruction after the instruction to be executed from the
   CIR (Current Instruction Register).

   The REG option traces register values.  Two sets of registers are printed.
   After executing each instruction, the currently active TOS registers, the
   index register, and the status register are printed.  After executing an
   instruction that may alter the base registers, the program, data, and stack
   segment base registers are printed.

   The OPND option traces memory byte operand values.  Some instructions take
   memory and register operands that are difficult to decode from DATA or REG
   traces.  This option presents these operands in a higher-level format.  The
   memory bank and address values are always those of the operands.  The operand
   data and values printed are specific to the instruction.  For example, the
   ALGN instruction prints its source and target operands, digit counts, and
   fraction counts, and the EDIT instruction displays its subprogram operations.

   The EXEC option traces the execution of instructions that match
   user-specified criteria.  When a match occurs, all CPU trace options are
   turned on for the duration of the execution of the matched instruction.  The
   prior trace settings are restored when a match fails.  This option allows
   detailed tracing of specified instructions while minimizing the log file size
   compared to a full instruction trace.

   The PSERV option traces process clock event service entries.  Each trace
   reports whether or not the CPU was executing on the Interrupt Control Stack
   when the process clock ticked.  Execution on the ICS implies that the
   operating system is executing.  As the process clock ticks every millisecond,
   enabling PSERV tracing can quickly produce a large number of trace lines.

   The various trace formats are interpreted as follows:

     >>CPU instr: 00.010341  000300  ZROX,NOP
                  ~~ ~~~~~~  ~~~~~~  ~~~~~~~~
                  |    |       |       |
                  |    |       |       +-- instruction mnemonic(s)
                  |    |       +---------- octal data (instruction opcode)
                  |    +------------------ octal address (P)
                  +----------------------- octal bank (PBANK)

     >>CPU instr: 00.001240  000006  external interrupt
     >>CPU instr: 00.023736  000000  unimplemented instruction trap
                  ~~ ~~~~~~  ~~~~~~  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |    |       |       |
                  |    |       |       +-- interrupt classification
                  |    |       +---------- parameter
                  |    +------------------ octal address (P) at interrupt
                  +----------------------- octal bank (PBANK) at interrupt


     >>CPU  data: 00.000000  001340    absolute read
     >>CPU  data: 00.002100  123003    data read
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~~~~~~~~
                  |    |       |         |
                  |    |       |         +-- memory access classification
                  |    |       +------------ octal data (memory contents)
                  |    +-------------------- octal address (effective address)
                  +------------------------- octal bank (PBANK, DBANK, or SBANK)


     >>CPU fetch: 00.010342  020320    instruction fetch
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~
                  |    |       |         |
                  |    |       |         +-- memory access classification
                  |    |       +------------ octal data (instruction opcode)
                  |    +-------------------- octal address (P + 1)
                  +------------------------- octal bank (PBANK)


     >>CPU   reg: 00.006500  000002    A 123003, B 001340, X 000000, M i t r o c CCL
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |    |       |         |
                  |    |       |         +-- register values (from 0-4 TOS registers, X, STA)
                  |    |       +------------ octal stack register count (SR)
                  |    +-------------------- octal stack memory address (SM)
                  +------------------------- octal bank (SBANK)

     >>CPU   reg: 00.000000  000001    PB 010000, PL 025227, DL 001770, DB 002000, Q 006510, Z 007000
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |    |       |         |
                  |    |       |         +-- base register values
                  |    |       +------------ current code segment number (from STA)
                  |    +-------------------- zero
                  +------------------------- octal bank (DBANK)

     >>CPU  opnd: 00.135771  000000    DFLC '+','!'
     >>CPU  opnd: 00.045071  000252    target fraction 3 length 6,"002222"
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                  |    |       |         |
                  |    |       |         +-- operand-specific value
                  |    |       +------------ operand-specific data
                  |    +-------------------- octal address (effective address)
                  +------------------------- octal bank (PBANK, DBANK, or SBANK)

     >>CPU pserv: Process clock service entered not on the ICS


   The process clock offers a user-selectable choice of calibrated or realistic
   timing.  Calibrated timing adjusts the clock to match actual elapsed time
   (i.e., wall-clock time).  Realistic timing bases the process-clock interval
   on machine instructions executed, using a mean instruction time of 2.5
   microseconds.  Running on a typical host platform, the simulator is one or
   two orders of magnitude faster than a real HP 3000, so the number of machine
   instructions executed for a given calibrated time interval will be
   correspondingly greater.  When the process clock is calibrated, the current
   simulation speed, expressed as a multiple of the speed of a real HP 3000
   Series III, may be obtained with the SHOW CPU SPEED command.  The speed
   reported will not be representative if the machine was executing a PAUS
   instruction when the simulator was stopped.

   When enabled by a SET CPU IDLE command, execution of a PAUS instruction will
   idle the simulator.  While idle, the simulator does not use any host system
   processor time.  Idle detection requires that the process clock and system
   clock be set to calibrated timing.  Idling is disabled by default.


   Implementation notes:

    1. Three timing sources in the simulator may be calibrated to wall-clock
       time.  These are the process clock, the system clock, and the ATC poll
       timer.  The process clock is always enabled and running, although the
       PCLK register only increments if the CPU is not executing on the ICS.
       The system clock and poll timer run continuously if their respective
       devices are enabled.  If the ATC is disabled, then the process clock
       takes over polling for the simulation console.

       The three sources must be synchronized to allow efficient simulator
       idling.  This is accomplished by designating the process clock as the
       master device, which calls the SCP timer calibration routines, and
       setting the system clock and ATC poll timer to the process clock wait.
       All three devices will then enter their respective service routines
       concurrently.

    2. In hardware, the process clock period is fixed at one millisecond, and
       the system clock period, while potentially variable, is set by MPE to one
       millisecond with an interrupt every 100 ticks.  These periods are too
       short to allow the simulator to idle, as the host OS clock resolution is
       typically also one millisecond.

       Therefore, the process and system clock simulators are scheduled with
       ten-millisecond service times, and the PCLK and counter registers are
       incremented by ten for each event service.  To present the correct values
       when the registers are read, the counts are incremented by amounts
       proportional to the fractions of the service intervals that have elapsed
       when the reads occur.

    3. In simulation, the TOS renamer is implemented by permanently assigning
       the register names RA-RD to TR [0]-TR [3], respectively, and copying the
       contents between registers to pop and push values.  An alternate
       implementation approach is to use a renaming register, RN, that tracks
       the correspondence between registers and array entries, and to assign the
       register names dynamically using modular indices, e.g., RA is TR [RN], RB
       is TR [(RN + 1) & 3], etc.  In lieu of copying, incrementing and
       decrementing RN is done to pop and push values.

       Both implementations were mocked up and timing measurements made.  The
       results were that copying values is much faster than mapping via array
       index values, so this is the implementation chosen.

    4. Idling with 4.x simulator framework versions after June 14, 2018 (git
       commit ID d3986466) loses system clock ticks.  This causes the MPE clock
       to run slowly, losing about one second per minute.  A workaround is to
       prevent "sim_interval" from going negative on return from "sim_idle".
       Issue 622 on the SIMH site describes the same problem with the HP 2100
       simulator.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_cpu_ims.h"
#include "hp3000_mem.h"
#include "hp3000_io.h"



/* Lost time workaround */

#if (SIM_MAJOR >= 4)
  #define sim_idle(timer,decrement) \
            if (sim_idle (timer, decrement) == TRUE     /* [workaround] idle the simulator; if idling occurred */ \
              && sim_interval < 0)                      /* [workaround]   and the time interval is negative */ \
                sim_interval = 0                        /* [workaround]     then reset it to zero */
#endif



/* Program constants */

#define PCLK_PERIOD         mS (1)                      /* 1 millisecond process clock period */

#define PCLK_MULTIPLIER     10                          /* number of hardware process clock ticks per service */
#define PCLK_RATE           (1000 / PCLK_MULTIPLIER)    /* process clock rate in ticks per second */

#define UNIT_OPTS           (UNIT_EIS)                  /* the standard equipment feature set */

#define CPU_IO_RESET        0                           /* reset CPU and all I/O devices */
#define IO_RESET            1                           /* reset just the I/O devices */

#define CNTL_BASE           8                           /* the radix for the cold dump control byte */
#define CNTL_MAX            0377                        /* the maximum cold dump control byte value */

#define SIO_JUMP            0000000u                    /* Jump unconditionally */
#define SIO_SBANK           0014000u                    /* Set bank */
#define SIO_ENDIN           0034000u                    /* End with interrupt */
#define SIO_CNTL            0040000u                    /* Control */
#define SIO_WRITE           0060000u                    /* Write 4096 words */
#define SIO_READ            0077760u                    /* Read 16 words */

#define MS_CN_GAP           0000005u                    /* Write Gap */
#define MS_CN_WRR           0000004u                    /* Write Record */
#define MS_CN_RST           0000011u                    /* Rewind and Reset */
#define MS_CN_BSR           0000012u                    /* Backspace Record */
#define MS_CN_WFM           0000015u                    /* Write File Mark */

#define MS_ST_PROTECTED     0001000u                    /* write protected */
#define MS_ST_READY         0000400u                    /* unit ready */

#define MS_ST_MASK          (MS_ST_PROTECTED | MS_ST_READY)


/* CPU global SCP data definitions */

DEVICE cpu_dev;                                 /* incomplete device structure */

REG *sim_PC = NULL;                             /* the pointer to the P register */


/* CPU global data structures */


/* CPU registers */

HP_WORD CIR    = 0;                             /* current instruction register */
HP_WORD NIR    = 0;                             /* next instruction register */
HP_WORD PB     = 0;                             /* program base register */
HP_WORD P      = 0;                             /* program counter register */
HP_WORD PL     = 0;                             /* program limit register */
HP_WORD PBANK  = 0;                             /* program segment bank register */
HP_WORD DL     = 0;                             /* data limit register */
HP_WORD DB     = 0;                             /* data base register */
HP_WORD DBANK  = 0;                             /* data segment bank register */
HP_WORD Q      = 0;                             /* stack marker register */
HP_WORD SM     = 0;                             /* stack memory register */
HP_WORD SR     = 0;                             /* stack register counter */
HP_WORD Z      = 0;                             /* stack limit register */
HP_WORD SBANK  = 0;                             /* stack segment bank register */
HP_WORD TR [4] = { 0, 0, 0, 0 };                /* top of stack registers */
HP_WORD X      = 0;                             /* index register */
HP_WORD STA    = 0;                             /* status register */
HP_WORD SWCH   = 0;                             /* switch register */
HP_WORD CPX1   = 0;                             /* run-mode interrupt flags register */
HP_WORD CPX2   = 0;                             /* halt-mode interrupt flags register */
HP_WORD MOD    = 0;                             /* module control register */
HP_WORD PCLK   = 0;                             /* process clock register */
HP_WORD CNTR   = 0;                             /* microcode counter */


/* Condition Code B lookup table.

   Byte-oriented instructions set the condition code in the status word using
   Pattern B (CCB).  For this encoding:

     CCG = ASCII numeric character
     CCE = ASCII alphabetic character
     CCL = ASCII special character

   The simplest implementation of this pattern is a 256-way lookup table using
   disjoint condition code flags.  The SET_CCB macro uses this table to set the
   condition code appropriate for the supplied operand into the status word.
*/

const HP_WORD cpu_ccb_table [256] = {
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* NUL SOH STX ETX EOT ENQ ACK BEL */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* BS  HT  LF  VT  FF  CR  SO  SI  */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* DLE DC1 DC2 DC3 DC4 NAK SYN ETB */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* CAN EM  SUB ESC FS  GS  RS  US  */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* spa  !   "   #   $   %   &   '  */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /*  (   )   *   +   ,   -   .   /  */
    CFG, CFG, CFG, CFG, CFG, CFG, CFG, CFG,     /*  0   1   2   3   4   5   6   7  */
    CFG, CFG, CFL, CFL, CFL, CFL, CFL, CFL,     /*  8   9   :   ;   <   =   >   ?  */
    CFL, CFE, CFE, CFE, CFE, CFE, CFE, CFE,     /*  @   A   B   C   D   E   F   G  */
    CFE, CFE, CFE, CFE, CFE, CFE, CFE, CFE,     /*  H   I   J   K   L   M   N   O  */
    CFE, CFE, CFE, CFE, CFE, CFE, CFE, CFE,     /*  P   Q   R   S   T   U   V   W  */
    CFE, CFE, CFE, CFL, CFL, CFL, CFL, CFL,     /*  X   Y   Z   [   \   ]   ^   _  */
    CFL, CFE, CFE, CFE, CFE, CFE, CFE, CFE,     /*  `   a   b   c   d   e   f   g  */
    CFE, CFE, CFE, CFE, CFE, CFE, CFE, CFE,     /*  h   i   j   k   l   m   n   o  */
    CFE, CFE, CFE, CFE, CFE, CFE, CFE, CFE,     /*  p   q   r   s   t   u   v   w  */
    CFE, CFE, CFE, CFL, CFL, CFL, CFL, CFL,     /*  x   y   z   {   |   }   ~  DEL */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 200 - 207 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 210 - 217 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 220 - 227 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 230 - 237 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 240 - 247 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 250 - 257 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 260 - 267 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 270 - 277 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 300 - 307 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 310 - 317 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 320 - 327 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 330 - 337 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 340 - 347 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 350 - 357 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL,     /* 360 - 367 */
    CFL, CFL, CFL, CFL, CFL, CFL, CFL, CFL      /* 370 - 377 */
    };


/* CPU global state */

jmp_buf     cpu_save_env;                       /* the saved environment for microcode aborts */
uint32      cpu_stop_flags;                     /* the simulation stop flag set */

POWER_STATE cpu_power_state   = power_on;       /* the power supply state */
EXEC_STATE  cpu_micro_state   = halted;         /* the microcode execution state */
t_bool      cpu_base_changed  = FALSE;          /* TRUE if any base register is changed */
t_bool      cpu_is_calibrated = TRUE;           /* TRUE if the process clock is calibrated */
UNIT       *cpu_pclk_uptr     = &cpu_unit [0];  /* a (constant) pointer to the process clock unit */


/* CPU local state */

static uint32  sim_stops      = 0;              /* the current simulation stop flag settings */
static uint32  cpu_speed      = 1;              /* the CPU speed, expressed as a multiplier of a real machine */
static uint32  pclk_increment = 1;              /* the process clock increment per event service */
static uint32  dump_control   = 0002006u;       /* the cold dump control word (default CNTL = 4, DEVNO = 6 */
static HP_WORD exec_mask      = 0;              /* the current instruction execution trace mask */
static HP_WORD exec_match     = D16_UMAX;       /* the current instruction execution trace matching value */


/* CPU local data structures */


/* Interrupt classification names */

static const char *const interrupt_name [] = {  /* class names, indexed by IRQ_CLASS */
    "integer overflow",                         /*   000 irq_Integer_Overflow */
    "bounds violation",                         /*   001 irq_Bounds_Violation */
    "illegal memory address error",             /*   002 irq_Illegal_Address  */
    "non-responding module error",              /*   003 irq_Timeout          */
    "system parity error",                      /*   004 irq_System_Parity    */
    "address parity error",                     /*   005 irq_Address_Parity   */
    "data parity error",                        /*   006 irq_Data_Parity      */
    "module",                                   /*   007 irq_Module           */
    "external",                                 /*   010 irq_External         */
    "power fail",                               /*   011 irq_Power_Fail       */
    "ICS trap",                                 /*   012 irq_Trap             */
    "dispatch",                                 /*   013 irq_Dispatch         */
    "exit"                                      /*   014 irq_IXIT             */
    };


/* Trap classification names */

static const char *const trap_name [] = {       /* trap names, indexed by TRAP_CLASS */
    "no",                                       /*   000 trap_None                */
    "bounds violation",                         /*   001 trap_Bounds_Violation    */
    NULL,                                       /*   002 (unused)                 */
    NULL,                                       /*   003 (unused)                 */
    NULL,                                       /*   004 (unused)                 */
    NULL,                                       /*   005 (unused)                 */
    NULL,                                       /*   006 (unused)                 */
    NULL,                                       /*   007 (unused)                 */
    NULL,                                       /*   010 (unused)                 */
    NULL,                                       /*   011 (unused)                 */
    NULL,                                       /*   012 (unused)                 */
    NULL,                                       /*   013 (unused)                 */
    NULL,                                       /*   014 (unused)                 */
    NULL,                                       /*   015 (unused)                 */
    NULL,                                       /*   016 (unused)                 */
    NULL,                                       /*   017 (unused)                 */
    "unimplemented instruction",                /*   020 trap_Unimplemented       */
    "STT violation",                            /*   021 trap_STT_Violation       */
    "CST violation",                            /*   022 trap_CST_Violation       */
    "DST violation",                            /*   023 trap_DST_Violation       */
    "stack underflow",                          /*   024 trap_Stack_Underflow     */
    "privileged mode violation",                /*   025 trap_Privilege_Violation */
    NULL,                                       /*   026 (unused)                 */
    NULL,                                       /*   027 (unused)                 */
    "stack overflow",                           /*   030 trap_Stack_Overflow      */
    "user",                                     /*   031 trap_User                */
    NULL,                                       /*   032 (unused)                 */
    NULL,                                       /*   033 (unused)                 */
    NULL,                                       /*   034 (unused)                 */
    NULL,                                       /*   035 (unused)                 */
    NULL,                                       /*   036 (unused)                 */
    "absent code segment",                      /*   037 trap_CS_Absent           */
    "trace",                                    /*   040 trap_Trace               */
    "STT entry uncallable",                     /*   041 trap_Uncallable          */
    "absent data segment",                      /*   042 trap_DS_Absent           */
    "power on",                                 /*   043 trap_Power_On            */
    "cold load",                                /*   044 trap_Cold_Load           */
    "system halt"                               /*   045 trap_System_Halt         */
    };


/* CPU features table.

   The feature table is used to validate CPU feature changes within the subset
   of features supported by a given CPU.  Features in the typical list are
   enabled when the CPU model is selected.  If a feature appears in the typical
   list but NOT in the optional list, then it is standard equipment and cannot
   be disabled.  If a feature appears in the optional list, then it may be
   enabled or disabled as desired by the user.


   Implementation notes:

    1. The EIS was standard equipment for the Series II and III, so UNIT_EIS
       should appear in their "typ" fields.  However, the EIS instructions are
       not currently implemented, so the value is omitted below.
*/

struct FEATURE_TABLE {
    uint32      typ;                            /* standard features plus typically configured options */
    uint32      opt;                            /* complete list of optional features */
    uint32      maxmem;                         /* maximum configurable memory in 16-bit words */
    };

static const struct FEATURE_TABLE cpu_features [] = {   /* features indexed by CPU_MODEL */
  { 0,                                                  /*   UNIT_SERIES_III */
    UNIT_CIS,
    1024 * 1024 },
  { 0,                                                  /*   UNIT_SERIES_II */
    0,
    256 * 1024 }
  };


/* CPU local SCP support routine declarations */

static t_stat cpu_service (UNIT    *uptr);
static t_stat cpu_reset   (DEVICE  *dptr);

static t_stat set_stops  (UNIT *uptr, int32 option,     CONST char *cptr, void *desc);
static t_stat set_exec   (UNIT *uptr, int32 option,     CONST char *cptr, void *desc);
static t_stat set_dump   (UNIT *uptr, int32 option,     CONST char *cptr, void *desc);
static t_stat set_size   (UNIT *uptr, int32 new_size,   CONST char *cptr, void *desc);
static t_stat set_model  (UNIT *uptr, int32 new_model,  CONST char *cptr, void *desc);
static t_stat set_option (UNIT *uptr, int32 new_option, CONST char *cptr, void *desc);
static t_stat set_pfars  (UNIT *uptr, int32 setting,    CONST char *cptr, void *desc);

static t_stat show_stops (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_exec  (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_dump  (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat show_speed (FILE *st, UNIT *uptr, int32 val, CONST void *desc);


/* CPU local utility routine declarations */

static t_stat halt_mode_interrupt (HP_WORD device_number);
static t_stat machine_instruction (void);


/* CPU SCP data structures */


/* Unit list.

   The CPU unit holds the main memory capacity and is used to schedule the
   process clock events.


   Implementation notes:

    1. The unit structure must be global for other modules to obtain the memory
       size via the MEMSIZE macro, which references the "capac" field.
*/

#define UNIT_FLAGS          (UNIT_PFARS | UNIT_CALTIME)

UNIT cpu_unit [] = {
    { UDATA (&cpu_service, UNIT_FLAGS | UNIT_FIX | UNIT_BINK | UNIT_IDLE, 0), PCLK_PERIOD * PCLK_MULTIPLIER }
    };


/* Register list.

   The CPU register list exposes the machine registers for user inspection and
   modification.  User flags describe the permitted and default display formats,
   as follows:

     - REG_X  permits any symbolic display
     - REG_M  defaults to CPU instruction mnemonic display
     - REG_T  defaults to CPU status mnemonic display


 Implementation notes:

    1. All registers that reference variables of type HP_WORD must have the
       REG_FIT flag for proper access if HP_WORD is a 16-bit type.

    2. The CNTR register is set to the value of the SR register when the
       micromachine halts or pauses.  This allows the SR value to be accessed by
       the diagnostics.  The top-of-stack registers are flushed to main memory
       when the machine halts or pauses, which alters SR.
*/

static REG cpu_reg [] = {
/*    Macro   Name     Location      Radix  Width  Offset           Flags           */
/*    ------  -------  ------------  -----  -----  ------  ------------------------ */
    { ORDATA (CIR,     CIR,                  16),          REG_M | REG_RO | REG_FIT },  /* current instruction register */
    { ORDATA (NIR,     NIR,                  16),          REG_M | REG_RO | REG_FIT },  /* next instruction register */
    { ORDATA (PB,      PB,                   16),                           REG_FIT },  /* program base register */
    { ORDATA (P,       P,                    16),                           REG_FIT },  /* program counter register */
    { ORDATA (PL,      PL,                   16),                           REG_FIT },  /* program limit register */
    { ORDATA (PBANK,   PBANK,                 4),                           REG_FIT },  /* program segment bank register */
    { ORDATA (DL,      DL,                   16),                           REG_FIT },  /* data limit register */
    { ORDATA (DB,      DB,                   16),                           REG_FIT },  /* data base register */
    { ORDATA (DBANK,   DBANK,                 4),                           REG_FIT },  /* data segment bank register */
    { ORDATA (Q,       Q,                    16),                           REG_FIT },  /* stack marker register */
    { ORDATA (SM,      SM,                   16),                           REG_FIT },  /* stack memory register */
    { ORDATA (SR,      SR,                    3),                           REG_FIT },  /* stack register counter */
    { ORDATA (Z,       Z,                    16),                           REG_FIT },  /* stack limit register */
    { ORDATA (SBANK,   SBANK,                 4),                           REG_FIT },  /* stack segment bank register */
    { ORDATA (RA,      TR [0],               16),          REG_X          | REG_FIT },  /* top of stack register */
    { ORDATA (RB,      TR [1],               16),          REG_X          | REG_FIT },  /* top of stack - 1 register */
    { ORDATA (RC,      TR [2],               16),          REG_X          | REG_FIT },  /* top of stack - 2 register */
    { ORDATA (RD,      TR [3],               16),          REG_X          | REG_FIT },  /* top of stack - 3 register */
    { ORDATA (X,       X,                    16),          REG_X          | REG_FIT },  /* index register */
    { ORDATA (STA,     STA,                  16),          REG_T          | REG_FIT },  /* status register */
    { ORDATA (SWCH,    SWCH,                 16),          REG_X          | REG_FIT },  /* switch register */
    { ORDATA (CPX1,    CPX1,                 16),                           REG_FIT },  /* run-mode interrupt flags */
    { ORDATA (CPX2,    CPX2,                 16),                           REG_FIT },  /* halt-mode interrupt flags */
    { ORDATA (PCLK,    PCLK,                 16),                           REG_FIT },  /* process clock register */
    { ORDATA (CNTR,    CNTR,                  6),                 REG_HRO | REG_FIT },  /* microcode counter */
    { ORDATA (MOD,     MOD,                  16),                 REG_HRO | REG_FIT },  /* module control register */

    { DRDATA (POWER,   cpu_power_state,       2),                 REG_HRO           },  /* system power supply state */
    { ORDATA (WRU,     sim_int_char,          8),                 REG_HRO           },  /* SCP interrupt character */
    { ORDATA (BRK,     sim_brk_char,          8),                 REG_HRO           },  /* SCP break character */
    { ORDATA (DEL,     sim_del_char,          8),                 REG_HRO           },  /* SCP delete character */

    { NULL }
    };


/* Modifier list */

static MTAB cpu_mod [] = {
/*    Mask Value    Match Value      Print String         Match String  Validation   Display  Descriptor */
/*    ------------  ---------------  -------------------  ------------  -----------  -------  ---------- */
    { UNIT_MODEL,   UNIT_SERIES_II,  "Series II",         NULL,         &set_model,  NULL,    NULL       },
    { UNIT_MODEL,   UNIT_SERIES_III, "Series III",        "III",        &set_model,  NULL,    NULL       },

    { UNIT_EIS,     UNIT_EIS,        "EIS",               NULL,         &set_option, NULL,    NULL       },
    { UNIT_EIS,     0,               "no EIS",            "NOEIS",      NULL,        NULL,    NULL       },

    { UNIT_CIS,     UNIT_CIS,        "CIS",               "CIS",        &set_option, NULL,    NULL       },
    { UNIT_CIS,     0,               "no CIS",            "NOCIS",      NULL,        NULL,    NULL       },

    { UNIT_PFARS,   UNIT_PFARS,      "auto-restart",      "ARS",        &set_pfars,  NULL,    NULL       },
    { UNIT_PFARS,   0,               "no auto-restart",   "NOARS",      &set_pfars,  NULL,    NULL       },

    { UNIT_CALTIME, UNIT_CALTIME,    "calibrated timing", "CALTIME",    NULL,        NULL,    NULL       },
    { UNIT_CALTIME, 0,               "realistic timing",  "REALTIME",   NULL,        NULL,    NULL       },

/*    Entry Flags             Value     Print String  Match String  Validation     Display         Descriptor */
/*    -------------------  -----------  ------------  ------------  -------------  --------------  ---------- */
    { MTAB_XDV,             128 * 1024, NULL,         "128K",       &set_size,     NULL,           NULL       },
    { MTAB_XDV,             256 * 1024, NULL,         "256K",       &set_size,     NULL,           NULL       },
    { MTAB_XDV,             384 * 1024, NULL,         "384K",       &set_size,     NULL,           NULL       },
    { MTAB_XDV,             512 * 1024, NULL,         "512K",       &set_size,     NULL,           NULL       },
    { MTAB_XDV,             768 * 1024, NULL,         "768K",       &set_size,     NULL,           NULL       },
    { MTAB_XDV,            1024 * 1024, NULL,         "1024K",      &set_size,     NULL,           NULL       },

    { MTAB_XDV,                 0,      "IDLE",       "IDLE",       &sim_set_idle, &sim_show_idle, NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOIDLE",     &sim_clr_idle, NULL,           NULL       },

    { MTAB_XDV | MTAB_NMO,      0,      "DUMP",       "DUMPDEV",    &set_dump,     &show_dump,     NULL       },
    { MTAB_XDV,                 1,      NULL,         "DUMPCTL",    &set_dump,     NULL,           NULL       },

    { MTAB_XDV | MTAB_NMO,      1,      "STOPS",      "STOP",       &set_stops,    &show_stops,    NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOSTOP",     &set_stops,    NULL,           NULL       },

    { MTAB_XDV | MTAB_NMO,      1,      "EXEC",       "EXEC",       &set_exec,     &show_exec,     NULL       },
    { MTAB_XDV,                 0,      NULL,         "NOEXEC",     &set_exec,     NULL,           NULL       },

    { MTAB_XDV | MTAB_NMO,      0,      "SPEED",      NULL,         NULL,          &show_speed,    NULL       },

    { 0 }
    };


/* Debugging trace list */

static DEBTAB cpu_deb [] = {
    { "INSTR", DEB_INSTR  },                    /* instructions */
    { "DATA",  DEB_MDATA  },                    /* memory data accesses */
    { "FETCH", DEB_MFETCH },                    /* memory instruction fetches */
    { "REG",   DEB_REG    },                    /* register values */
    { "OPND",  DEB_MOPND  },                    /* memory operand values */
    { "EXEC",  DEB_EXEC   },                    /* instruction execution states */
    { "PSERV", DEB_PSERV  },                    /* process clock service events */
    { NULL,    0          }
    };

/* Debugging stop list */

static DEBTAB cpu_stop [] = {
    { "LOOP",   SS_LOOP   },                    /* stop on an infinite loop */
    { "PAUSE",  SS_PAUSE  },                    /* stop on a PAUS instruction */
    { "UNDEF",  SS_UNDEF  },                    /* stop on an undefined instruction */
    { "UNIMPL", SS_UNIMPL },                    /* stop on an unimplemented instruction */
    { NULL,     0         }
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
    &mem_examine,                               /* examine routine */
    &mem_deposit,                               /* deposit routine */
    &cpu_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    NULL,                                       /* device information block pointer */
    DEV_DEBUG,                                  /* device flags */
    0,                                          /* debug control flags */
    cpu_deb,                                    /* debug flag name array */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* CPU global SCP support routines */


/* Execute CPU instructions.

   This is the instruction decode routine for the HP 3000.  It is called from
   the simulator control program to execute instructions in simulated memory,
   starting at the simulated program counter.  It runs until the status to be
   returned is set to a value other than SCPE_OK.

   On entry, P points to the instruction to execute, and the "sim_switches"
   global contains any command-line switches included with the run command.  On
   exit, P points at the next instruction to execute (or the current
   instruction, in the case of a simulator stop during a PAUS instruction or
   after the first of two stack operations).

   Execution is divided into four phases.

   First, the instruction prelude configures the simulation state to resume
   execution.  This involves verifying that system power is on and there are no
   device conflicts (e.g., two devices with the same device number),
   initializing the I/O processor and channels, and setting the RUN switch if no
   other front panel switches are pressed.  These actions accommodate
   reconfiguration of the I/O device settings and program counter while the
   simulator was stopped.  The prelude also responds to one command-line switch:
   if "-B" is specified, the current set of simulation stop conditions is
   bypassed for the first instruction executed.  This allows, e.g., a PAUS
   instruction to be bypassed or an unimplemented instruction trap to be taken.

   Second, the microcode abort mechanism is set up.  Microcode aborts utilize
   the "setjmp/longjmp" mechanism to transfer control out of the instruction
   executors without returning through the call stack.  This allows an
   instruction to be aborted part-way through execution when continuation is
   impossible, e.g., due to a memory access violation.  It corresponds to direct
   microcode jumps out of the execution sequence and to the appropriate trap
   handlers.

   Third, the instruction execution loop decodes instructions and calls the
   individual executors in turn until a condition occurs that prevents further
   execution.  Examples of such conditions includes execution of a HALT
   instruction, a user stop request (CTRL+E) from the simulation console, a
   recoverable device error (such as an improperly formatted tape image), a
   user-specified breakpoint, and a simulation stop condition (such as execution
   of an unimplemented instruction).  The execution loop also polls for I/O
   events and device interrupts, and runs I/O channel cycles.  During
   instruction execution, the CIR register contains the currently executing
   instruction, the NIR register contains the next instruction to execute, and
   the P register points to the memory location two instructions ahead of the
   current instruction.

   Fourth, the instruction postlude updates the simulation state in preparation
   for returning to the SCP command prompt.  Devices that maintain an internal
   state different from their external state, such as the CPU process clock, are
   updated so that their internal and external states are fully consistent.
   This ensures that the state visible to the user during the simulation stop is
   correct.  It also ensures that the program counter points correctly at the
   next instruction to execute upon resumption.

   If enabled, the simulator is idled when a PAUS instruction has been executed
   and no service requests for the multiplexer or selector channels are active.
   Execution of a PAUS instruction suspends the fetch-and-execute process until
   an interrupt occurs or the simulator is stopped and then resumed with a GO -B
   or RUN -B command.

   The HP 3000 is a microcoded machine.  In hardware, the micromachine is always
   executing microinstructions, even when the CPU is "halted."  The halt/run
   state is simply a flip-flop setting, reflected in bit 15 of the CPX2
   register and the RUN light on the front panel, that determines whether the
   "halt-mode" or "run-mode" microprogram is currently executing.

   In simulation, the "cpu_micro_state" variable indicates the state of the
   micromachine, i.e., which section of the microcode it is executing, while
   CPX2 bit 15 indicates whether the macromachine is halted or running.  The
   micromachine may be in one of four states:

     - running : the run-mode fetch-and-execute microcode is executing
     - paused  : the run-mode PAUS instruction microcode is executing
     - waiting : the halt-mode cold load or dump microcode is executing
     - halted  : the halt-mode front panel microcode is executing

   Simulation provides a variety of stop conditions that break instruction
   execution and return to the SCP prompt with the CPU still in run mode.  These
   have no analog in hardware; the only way to stop the CPU is to press the HALT
   button on the front panel, which shifts the micromachine into halt-mode
   microcode execution.  When any of these conditions occur, the micromachine
   state is set to "halted," but the CPX2 run flag is remains set unless the
   stop was caused by execution of a HALT instruction.  Resuming execution with
   a STEP, CONT, GO, or RUN command proceeds as though the hardware RUN switch
   was pressed after a programmed halt.  This provides the proper semantics for
   seamlessly stopping and restarting instruction execution.

   A microcode abort is performed by executing a "longjmp" to the abort handler,
   which is outside of and precedes the instruction execution loop.  The value
   passed to "longjmp" is a 32-bit integer containing the Segment Transfer Table
   index of the trap handler in the lower word and an optional parameter in the
   upper word.  Aborts are invoked by the MICRO_ABORT macro, which takes as its
   parameter a trap classification value, e.g.:

     MICRO_ABORT (trap_Privilege_Violation);
     MICRO_ABORT (trap_Integer_Zero_Divide);

   Some aborts require an additional parameter and must be invoked by the
   MICRO_ABORTP macro, which takes a trap classification value and a
   trap-specific value as parameters.  The traps that require additional
   parameters are:

     MICRO_ABORTP (trap_CST_Violation, segment_number);
     MICRO_ABORTP (trap_STT_Violation, segment_number);
     MICRO_ABORTP (trap_CS_Absent,     label/n/0);
     MICRO_ABORTP (trap_DS_Absent,     DST_number);
     MICRO_ABORTP (trap_Uncallable,    label);
     MICRO_ABORTP (trap_Trace,         label/n/0);
     MICRO_ABORTP (trap_User,          trap_number);

   trap_User is not usually called explicitly via MICRO_ABORTP; rather,
   MICRO_ABORT is used with one of the specific user-trap identifiers, e.g.,
   trap_Integer_Overflow, trap_Float_Overflow, trap_Decimal_Overflow, etc., that
   supplies both the trap classification and the trap parameter value.

   In addition, user traps must be enabled by setting the T-bit in the status
   word.  If the T bit is not set, a user trap sets the O-bit (overflow) in the
   status word and resumes execution with the next instruction instead of
   invoking the user trap handler.

   When an abort occurs, an equivalent PCAL to the appropriate STT entry is set
   up.  Then execution drops into the instruction loop to execute the first
   instruction of the trap handler.

   When the instruction loop is exited, the CPU process clock and system clock
   registers are updated, the micromachine is halted, and control returns to
   SCP.  Upon return, P points at the next instruction to execute, i.e., the
   instruction that will execute when the instruction loop is reentered.

   If the micromachine is paused, then P is reset to point to the PAUS
   instruction, which will be reexecuted when the routine is reentered.  If it
   is running, then P is reset to point to the current instruction if the stop
   allows it to be rerun, or at the next instruction.


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

       Therefore, the "device" and "status" variables are marked volatile to
       ensure that they are reloaded after a longjmp caused by a micrcode abort.

    2. In hardware, the NEXT microcode order present at the end of each
       instruction transfers the NIR content to the CIR, reads the memory word
       at P into the NIR, and increments P.  However, if an interrupt is
       present, then this action is omitted, and a microcode jump is performed
       to control store location 3, which then jumps to the microcoded interrupt
       handler.  In simulation, the CIR/NIR/P update is performed before the
       next instruction is executed, rather than after the last instruction
       completes, so that interrupts are handled before updating.

       In addition, the NEXT action is modified in hardware if the NIR contains
       a stack instruction with a non-NOP B stackop.  In this case, NEXT
       transfers the NIR content to the CIR, reads the memory word at P into the
       NIR, but does not increment P.  Instead, the R bit of the status register
       is set to indicate that a B stackop is pending.  When the NEXT at the
       completion of the A stackop is executed, the NIR and CIR are untouched,
       but P is incremented, and the R bit is cleared.  This ensures that if an
       interrupt or trap occurs between the stackops, P will point correctly at
       the next instruction to be executed.

       In simulation, following the hardware would require testing the NIR for a
       non-NOP B stackop at every pass through the instruction execution loop.
       To avoid this, the NEXT simulation unilaterally increments P, rather than
       only when a B stackop is not present, and the stack instruction executor
       tests for the B stackop and sets the R bit there.  However, by that time,
       P has already been incremented, so we decrement it there to return it to
       the correct value.

    3. The System Halt trap has no handler.  Instead, the simulator is halted,
       and control returns to the SCP prompt.

    4. The trace display for a trap reports the parameter value supplied with
       the microcode abort.  This is not necessarily the same as the parameter
       that is pushed on the stack for the trap handler.  As some traps, e.g.,
       trap_CST_Violation, can cause a System Halt, the placement of the trace
       call is dictated by the desire to report both the original trap and the
       System Halt trap, even though the placement results in the display of the
       incoming parameter value, rather than the stacked parameter value.

    5. The execution trace (DEB_EXEC) match test is performed in two parts to
       display the register values both before and after the instruction
       execution.  Consequently, the enable test is done before the register
       trace, and the disable test is done after.

    6. The execution test (exec_test) is set FALSE even though execution tracing
       is not specified.  This is done solely to reassure the compiler that the
       value is not clobbered by a longjmp call.

    7. The set of debug trace flags is restored in the postlude (and therefore
       also saved in the instruction prelude) to ensure that a simulation stop
       that occurs while an execution trace is in progress does not exit with
       the flags set improperly.
*/

t_stat sim_instr (void)
{
static const char *const stack_formats [] = {           /* stack register display formats, indexed by SR */
    BOV_FORMAT "  ",                                    /*   SR = 0 format */
    BOV_FORMAT "  A %06o, ",                            /*   SR = 1 format */
    BOV_FORMAT "  A %06o, B %06o, ",                    /*   SR = 2 format */
    BOV_FORMAT "  A %06o, B %06o, C %06o, ",            /*   SR = 3 format */
    BOV_FORMAT "  A %06o, B %06o, C %06o, D %06o, "     /*   SR = 4 format */
    };

int        abortval;
HP_WORD    label, parameter;
TRAP_CLASS trap;
t_bool     exec_test;
volatile   uint32  debug_save;
volatile   HP_WORD device;
volatile   t_stat  status = SCPE_OK;


/* Instruction prelude */

debug_save = cpu_dev.dctrl;                             /* save the current set of debug flags for later restoration */

if (sim_switches & SWMASK ('B'))                        /* if a simulation stop bypass was requested */
    cpu_stop_flags = SS_BYPASSED;                       /*   then clear the stop flags for the first instruction */
else                                                    /* otherwise */
    cpu_stop_flags = sim_stops;                         /*   set the stops as indicated */

if (cpu_power_state == power_off)                       /* if system power is off */
    status = STOP_POWER;                                /*   then execution is not possible until restoration */

else if (hp_device_conflict ())                         /* otherwise if device assignment is inconsistent */
    status = SCPE_STOP;                                 /*   then inhibit execution */

else {                                                  /* otherwise */
    device = iop_initialize ();                         /*   initialize the IOP */
    mpx_initialize ();                                  /*     and the multiplexer channel */
    sel_initialize ();                                  /*       and the selector channel */

    if ((CPX2 & CPX2_IRQ_SET) == 0)                     /* if no halt-mode interrupt is present */
        CPX2 |= cpx2_RUNSWCH;                           /*   then assume a RUN command via STEP/CONT */
    }


/* Microcode abort processor */

abortval = setjmp (cpu_save_env);                       /* set the microcode abort handler */

if (abortval) {                                         /* if a microcode abort occurred */
    trap = TRAP (abortval);                             /*   then get the trap classification */
    parameter = PARAM (abortval);                       /*     and the optional parameter */

    label = TO_LABEL (LABEL_IRQ, trap);                 /* form the label from the STT number */

    dprintf (cpu_dev, DEB_INSTR, BOV_FORMAT "%s trap%s\n",
             PBANK, P - 1 & R_MASK, parameter, trap_name [trap],
             (trap == trap_User && !(STA & STATUS_T) ? " (disabled)" : ""));

    switch (trap) {                                     /* dispatch on the trap classification */

        case trap_None:                                 /* trap_None should never occur */
        case trap_System_Halt:
            CNTR = SR;                                  /* copy the stack register to the counter */
            cpu_flush ();                               /*   and flush the TOS registers to memory */

            RA = parameter;                             /* set RA to the parameter (system halt condition) */

            CPX2 = CPX2 & ~cpx2_RUN | cpx2_SYSHALT;     /* halt the CPU and set the system halt flag */
            status = STOP_SYSHALT;                      /*   and report the system halt condition */

            label = 0;                                  /* there is no trap handler for a system halt */
            break;


        case trap_CST_Violation:
            if (STT_SEGMENT (parameter) <= ISR_SEGMENT) /* if the trap occurred in segment 1 */
                MICRO_ABORT (trap_SysHalt_CSTV_1);      /*   then the failure is fatal */

        /* fall through into the next trap handler */

        case trap_STT_Violation:
            if (STT_SEGMENT (parameter) <= ISR_SEGMENT) /* if the trap occurred in segment 1 */
                MICRO_ABORT (trap_SysHalt_STTV_1);      /*   then the failure is fatal */

        /* fall through into the next trap handler */

        case trap_Unimplemented:
        case trap_DST_Violation:
        case trap_Stack_Underflow:
        case trap_Privilege_Violation:
        case trap_Bounds_Violation:
            parameter = label;                          /* the label is the parameter for these traps */

        /* fall through into the next trap handler */

        case trap_DS_Absent:
            cpu_flush ();                               /* flush the TOS registers to memory */
            cpu_mark_stack ();                          /*   and then write a stack marker */

        /* fall through into the next trap handler */

        case trap_Uncallable:
            break;                                      /* set up the trap handler */


        case trap_User:
            if (STA & STATUS_T) {                       /* if user traps are enabled */
                STA &= ~STATUS_O;                       /*   then clear overflow status */
                cpu_flush ();                           /*     and flush the TOS registers to memory */
                cpu_mark_stack ();                      /*       and write a stack marker */
                }                                       /*         and set up the trap handler */

            else {                                      /* otherwise in lieu of trapping */
                STA |= STATUS_O;                        /*   set overflow status */
                label = 0;                              /*     and continue execution with the next instruction */
                }
            break;


        case trap_CS_Absent:
            if (CPX1 & cpx1_ICSFLAG)                    /* if the trap occurred while on the ICS */
                MICRO_ABORT (trap_SysHalt_Absent_ICS);  /*   then the failure is fatal */

            else if (STT_SEGMENT (STA) <= ISR_SEGMENT)  /* otherwise if the trap occurred in segment 1 */
                MICRO_ABORT (trap_SysHalt_Absent_1);    /*   then the failure is fatal */
            break;                                      /* otherwise set up the trap handler */


        case trap_Trace:
            if (STT_SEGMENT (STA) <= ISR_SEGMENT)       /* if the trap occurred in segment 1 */
                MICRO_ABORT (trap_SysHalt_Trace_1);     /*   then the failure is fatal */
            break;                                      /* otherwise set up the trap handler */


        case trap_Cold_Load:                            /* this trap executes on the ICS */
            status = STOP_CLOAD;                        /* report that the cold load is complete */

        /* fall through into trap_Stack_Overflow */

        case trap_Stack_Overflow:                           /* this trap executes on the ICS */
            if (CPX1 & cpx1_ICSFLAG)                        /*   so if the trap occurred while on the ICS */
                MICRO_ABORT (trap_SysHalt_Overflow_ICS);    /*     then the failure is fatal */

            cpu_setup_ics_irq (irq_Trap, trap);             /* otherwise, set up the ICS */
            break;                                          /*   and then the trap handler */


        case trap_Power_On:                             /* this trap executes on the ICS */
            cpu_setup_ics_irq (irq_Trap, trap);         /*   so set it up */
            cpu_power_state = power_on;                 /*     and return to the power-on state */

            if (CPX2 & cpx2_INHPFARS)                   /* if auto-restart is inhibited */
                status = STOP_ARSINH;                   /*   then exit and wait for a manual restart */
            break;
        }                                               /* all cases are handled */


    if (label != 0) {                                   /* if the trap handler is to be called */
        STA = STATUS_M;                                 /*   then clear the status and enter privileged mode */

        SM = SM + 1 & R_MASK;                           /* increment the stack pointer */
        cpu_write_memory (stack, SM, parameter);        /*   and push the parameter on the stack */

        X = CIR;                                        /* save the current instruction for restarting */

        cpu_call_procedure (label, 0);                  /* set up PB, P, PL, and STA to call the procedure */

        cpu_base_changed = TRUE;                        /* one or more base registers have changed */
        }

    sim_interval = sim_interval - 1;                    /* count the execution cycle that aborted */
    }


/* Instruction loop */

while (status == SCPE_OK) {                             /* execute until simulator status prevents continuation */

    if (sim_interval <= 0) {                            /* if an event timeout has expired */
        status = sim_process_event ();                  /*   then call the event service */

        if (status != SCPE_OK)                          /* if the service failed */
            break;                                      /*   then abort execution and report the failure */
        }

    if (sel_request)                                    /* if a selector channel request is pending */
        sel_service (1);                                /*   then service it */

    if (mpx_request_set)                                /* if a multiplexer channel request is pending */
        mpx_service (1);                                /*   then service it */

    if (iop_interrupt_request_set                       /* if a hardware interrupt request is pending */
      && STA & STATUS_I                                 /*   and interrupts are enabled */
      && CIR != SED_1)                                  /*     and not deferred by a SED 1 instruction */
        device = iop_poll ();                           /*       then poll to acknowledge the request */

    if (cpu_micro_state == running)                     /* if the micromachine is running */
        if (CPX1 & CPX1_IRQ_SET                         /*   then if a run-mode interrupt is pending */
          && cpu_power_state != power_failing)          /*     and power is not currently failing */
            cpu_run_mode_interrupt (device);            /*       then service it */

        else if (sim_brk_summ                               /* otherwise if a breakpoint exists */
          && sim_brk_test (TO_PA (PBANK, P - 1 & LA_MASK),  /*   at the next location */
                           BP_EXEC)) {                      /*     to execute */
            status = STOP_BRKPNT;                           /*       then stop the simulation */
            sim_interval = sim_interval + 1;                /*         and don't count the cycle */
            }

        else {                                              /* otherwise execute the next instruction */
            if (DPRINTING (cpu_dev, DEB_EXEC | DEB_REG)) {  /* if execution or register tracing is enabled */
                if (cpu_dev.dctrl & DEB_EXEC)               /*   then if tracing execution */
                    if (STA & STATUS_R)                     /*     then if the right-hand stack op is pending */
                        exec_test =                         /*       then the execution test succeeds if */
                           (CIR << STACKOP_A_SHIFT          /*         the right-hand stack op */
                             & STACKOP_A_MASK               /*           matches the test criteria */
                             & exec_mask) == exec_match;    /*             for the left-hand stack op value */
                    else                                    /*     otherwise */
                        exec_test =                         /*       then the execution test succeeds if */
                           (NIR & exec_mask) == exec_match; /*         the next instruction matches the test criteria */
                else                                        /*   otherwise */
                    exec_test = FALSE;                      /*     there is no execution test */

                if (cpu_dev.dctrl & DEB_EXEC                /* if execution tracing is enabled */
                  && cpu_dev.dctrl != DEB_ALL               /*   and is currently inactive */
                  && exec_test) {                           /*     and the matching test succeeds */
                    debug_save = cpu_dev.dctrl;             /*       then save the current trace flag set */
                    cpu_dev.dctrl = DEB_ALL;                /*         and turn on full tracing */
                    }

                if (cpu_dev.dctrl & DEB_REG) {              /* if register tracing is enabled */
                    hp_debug (&cpu_dev, DEB_REG,            /*   then output the active TOS registers */
                              stack_formats [SR],
                              SBANK, SM, SR, RA, RB, RC, RD);

                    fprintf (sim_deb, "X %06o, %s\n",       /* output the index and status registers */
                             X, fmt_status (STA));

                    if (cpu_base_changed) {                 /* if the base registers have been altered */
                        hp_debug (&cpu_dev, DEB_REG,        /*   then output the base register values */
                                  BOV_FORMAT "  PB %06o, PL %06o, DL %06o, DB %06o, Q %06o, Z %06o\n",
                                  DBANK, 0, STATUS_CS (STA),
                                  PB, PL, DL, DB, Q, Z);

                        cpu_base_changed = FALSE;           /* clear the base registers changed flag */
                        }
                    }

                if (cpu_dev.dctrl & DEB_EXEC                /* if execution tracing is enabled */
                  && cpu_dev.dctrl == DEB_ALL               /*   and is currently active */
                  && ! exec_test) {                         /*     and the matching test fails */
                    cpu_dev.dctrl = debug_save;             /*       then restore the saved debug flag set */
                    hp_debug (&cpu_dev, DEB_EXEC,           /*         and add a separator to the trace log */
                              "*****************\n");
                    }
                }

            if (!(STA & STATUS_R)) {                    /* (NEXT) if the right-hand stack op is not pending */
                CIR = NIR;                              /*   then update the current instruction */
                cpu_read_memory (fetch, P, &NIR);       /*     and load the next instruction */
                }

            P = P + 1 & R_MASK;                         /* point to the following instruction */

            if (DPRINTING (cpu_dev, DEB_INSTR)) {           /* if instruction tracing is enabled */
                sim_eval [0] = CIR;                         /*   then save the instruction that will be executed */
                sim_eval [1] = NIR;                         /*     and the following word for evaluation */

                hp_debug (&cpu_dev, DEB_INSTR, BOV_FORMAT,  /* print the address and the instruction opcode */
                          PBANK, P - 2 & R_MASK, CIR);      /*   as an octal value */

                if (fprint_cpu (sim_deb, sim_eval, 0, SIM_SW_STOP) == SCPE_ARG) /* print the mnemonic; if that fails */
                    fprint_val (sim_deb, sim_eval [0], cpu_dev.dradix,          /*   then print the numeric */
                                cpu_dev.dwidth, PV_RZRO);                       /*     value again */

                fputc ('\n', sim_deb);                      /* end the trace with a newline */
                }

            status = machine_instruction ();            /* execute one machine instruction */

            cpu_stop_flags = sim_stops;                 /* reset the stop flags as indicated */
            }

    else if (cpu_micro_state == paused) {               /* otherwise if the micromachine is paused */
        if (CPX1 & CPX1_IRQ_SET)                        /*   then if a run-mode interrupt is pending */
            cpu_run_mode_interrupt (device);            /*     then service it */

        else if (sim_idle_enab                          /*   otherwise if idling is enabled */
          && ! sel_request && mpx_request_set == 0)     /*     and there are no channel requests pending */
            sim_idle (TMR_PCLK, FALSE);                 /*       then idle the simulator */
        }

    else if (CPX2 & CPX2_IRQ_SET)                       /* otherwise if a halt-mode interrupt is pending */
        status = halt_mode_interrupt (device);          /*   then service it */

    sim_interval = sim_interval - 1;                    /* count the execution cycle */
    }                                                   /*   and continue with the instruction loop */


/* Instruction postlude */

cpu_dev.dctrl = debug_save;                             /* restore the flag set */

cpu_update_pclk ();                                     /* update the process clock */
clk_update_counter ();                                  /*   and system clock counters */

if (cpu_micro_state == paused)                          /* if the micromachine is paused */
    P = P - 2 & R_MASK;                                 /*   then set P to point to the PAUS instruction */

else if (cpu_micro_state == running)                    /* otherwise if it is running */
    if (status <= STOP_RERUN)                           /*   then if the instruction will be rerun when resumed */
        P = P - 2 & R_MASK;                             /*     then set P to point to it */
    else                                                /*   otherwise */
        P = P - 1 & R_MASK;                             /*     set P to point to the next instruction */

cpu_micro_state = halted;                               /* halt the micromachine */

if (cpu_power_state == power_failing                    /* if power is failing */
  && status == STOP_HALT)                               /*   and we executed a HALT instruction */
    cpu_power_state = power_off;                        /*     then power will be off when we return */

dprintf (cpu_dev, cpu_dev.dctrl, BOV_FORMAT "simulation stop: %s\n",
         PBANK, P, STA,
         status >= SCPE_BASE ? sim_error_text (status)
                             : sim_stop_messages [status]);

return status;                                          /* return the reason for the stop */
}


/* Execute the LOAD and DUMP commands.

   This command processing routine implements the cold load and cold dump
   commands.  The syntax is:

     LOAD { <control/devno> }
     DUMP { <control/devno> }

   The <control/devno> is a number that is deposited into the SWCH register
   before invoking the CPU cold load or cold dump facility.  The CPU radix is
   used to interpret the number; it defaults to octal.  If the number is
   omitted, the SWCH register value is not altered before loading or dumping.

   On entry, the "arg" parameter is "Cold_Load" for a LOAD command and
   "Cold_Dump" for a DUMP command, and "buf" points at the remainder of the
   command line.  If characters exist on the command line, they are parsed,
   converted to a numeric value, and stored in the SWCH register.  Then the
   CPU's cold load/dump routine is called to set up the CPU state.  Finally, the
   CPU is started to begin the requested action.


   Implementation notes:

    1. The RUN command uses the RU_CONT argument instead of RU_RUN so that the
       run_cmd SCP routine will not reset all devices before entering the
       instruction executor.  The halt mode interrupt handlers for cold load and
       cold dump reset the simulator as appropriate for their commands (i.e.,
       the CPU and all I/O devices, or just the I/O devices, respectively).
*/

t_stat cpu_cold_cmd (int32 arg, CONST char *buf)
{
const char *cptr;
char       gbuf [CBUFSIZE];
t_stat     status;
HP_WORD    value;

if (*buf != '\0') {                                     /* if more characters exist on the command line */
    cptr = get_glyph (buf, gbuf, 0);                    /*   then get the next glyph */

    if (*cptr != '\0')                                  /* if that does not exhaust the input */
        return SCPE_2MARG;                              /*   then report that there are too many arguments */

    value = (HP_WORD) get_uint (gbuf, cpu_dev.dradix,   /* get the parameter value */
                                D16_UMAX, &status);

    if (status == SCPE_OK)                              /* if a valid number was present */
        SWCH = value;                                   /*   then set it into the switch register */
    else                                                /* otherwise */
        return status;                                  /*   return the error status */
    }

else if (arg == Cold_Dump)                              /* otherwise if no dump value was given */
    SWCH = dump_control;                                /*   then use the system control panel presets */

cpu_front_panel (SWCH, (PANEL_TYPE) arg);               /* set up the cold load or dump microcode */

return run_cmd (RU_CONT, buf);                          /* execute the halt-mode routine */
}


/* Execute the POWER commands.

   This command processing routine is called to initiate a power failure or
   power restoration.  The "cptr" parameter points to the power option keyword;
   the "arg" parameter is not used.

   The routine processes commands of the form:

     POWER { FAIL | OFF | DOWN }
     POWER { RESTORE | ON | UP }

   In simulation, the "cpu_power_state" global variable indicates the current
   state of system power.  The simulator starts in the power_on state.  The
   POWER FAIL command moves from the power_on to the power_failing state if the
   CPU is running, or to the power_off state if it is not.  Execution of a HALT
   in the power_failing state moves to the power_off state.  The POWER RESTORE
   command moves from the power_off to the power_returning state if the CPU is
   running, or to the power_on state if it is not.  Execution of the power-on
   trap moves from the power_returning to the power_on state.

   The POWER FAIL and POWER RESTORE commands are only valid in the power_on and
   power_off states, respectively; otherwise, they print "Command not allowed."

   The four enumeration values model the states of the PON (power on) and PFW
   (power-fail warning) hardware signals, as follows:

     PON  PFW  State            Simulator Action
     ---  ---  ---------------  ----------------------------
      1    0   power on         executing normally
      1    1   power failing    executing with cpx1_PFINTR
      0    1   power off        will not execute
      0    0   power returning  executing with trap_Power_On

   In microcode, the power-fail routine writes the current value of the CPX2
   register to the word following the last word of the ICS.  This value is used
   by the power-on routine to decide if the CPU was running (cpx2_RUN bit is
   set) or halted at the time of the power failure.  A power failure is
   indicated by setting the cpx1_PFINTR bit in the CPX1 register; this causes an
   interrupt to the power-failure routine in the operating system, which
   performs an orderly shutdown followed by a programmed HALT to wait for power
   to die.  When power is restored, the power-on trap (trap_Power_On) is set up,
   and then, if the PF/ARS switch is in the "enable" position, the trap is
   taken, which restarts the operating system and any I/O that was in progress
   when power failed.  If the switch is in the "disable" position, the CPU
   remains halted, and the trap is taken when the RUN button is pressed.

   The POWER commands are entered at the SCP prompt.  If the machine was running
   at the time of power failure, execution is resumed automatically to execute
   the power-fail or power-restore OS routines.  If the machine was halted when
   the POWER commands were entered, the machine remains halted -- just the power
   state changes.


   Implementation notes:

    1. In hardware, when the power fail interrupt is serviced, the microcode
       sets the PWR INH flip-flop, which locks out the RUN switch and inhibits
       all other halt-mode (CPX2) and run-mode (CPX1) interrupts until the CPU
       is reset.  This ensures that the software power-fail interrupt handler
       executes unimpeded.  In simulation, a POWER FAIL command with the CPU
       running sets the power-fail interrupt bit in the CPX1 register and
       resumes execution in the power_on state.  When the interrupt is detected
       by the "cpu_run_mode_interrupt" routine, the state is changed to
       power_failing.  In this state, run-mode interrupts are not recognized.
       Halt-mode interrupts need no special handling, as the power state is
       changed to power_off when the CPU halts.  Therefore, the CPU is never in
       a "halted-and-waiting-for-power-to-fade-away" state.

    2. The RUN command uses the RU_CONT argument instead of RU_RUN so that the
       run_cmd SCP routine will not reset all devices before entering the
       instruction executor.  The halt-mode interrupt handlers for cold load and
       cold dump reset the simulator as appropriate for their commands (i.e.,
       the CPU and all I/O devices, or just the I/O devices, respectively).

    3. In order to set up the power-on trap, this routine presses the RUN button
       and continues the simulation.  After the trap is set up in the
       "sim_instr" routine, the halt-mode interrupt handler checks the PF/ARS
       switch and stops simulation if auto-restart is disabled.
*/

t_stat cpu_power_cmd (int32 arg, CONST char *cptr)
{
static CTAB options [] = {
    { "FAIL",    NULL, power_failing   },
    { "RESTORE", NULL, power_returning },
    { "OFF",     NULL, power_failing   },
    { "ON",      NULL, power_returning },
    { "DOWN",    NULL, power_failing   },
    { "UP",      NULL, power_returning },
    { NULL }
    };

char    gbuf [CBUFSIZE];
CTAB    *ctptr;
HP_WORD zi, failure_cpx2;
t_stat  status;

if (cptr == NULL || *cptr == '\0')                      /* if there is no option word */
    return SCPE_2FARG;                                  /*   then report a missing argument */

cptr  = get_glyph (cptr, gbuf, 0);                      /* parse (and upshift) the option specified */
ctptr = find_ctab (options, gbuf);                      /*   and look it up in the option table */

if (ctptr == NULL)                                      /* if the option is not valid */
    status = SCPE_ARG;                                  /*   then report a bad argument */

else if (*cptr != '\0')                                 /* otherwise if something follows the option */
    return SCPE_2MARG;                                  /*   then report too many arguments */

else if (ctptr->arg == power_failing)                   /* otherwise if a power-fail option was given */
    if (cpu_power_state != power_on)                    /*   but the CPU power is not on */
        status = SCPE_NOFNC;                            /*     then the command is not allowed */

    else {                                              /* otherwise the failure is valid */
        iop_assert_PFWARN ();                           /*   so send a power-fail warning to all devices */

        cpu_read_memory (absolute, ICS_Z, &zi);         /* get the ICS stack limit */
        cpu_write_memory (absolute, zi + 1, CPX2);      /*   and save the CPX2 value in the following word */

        if (CPX2 & cpx2_RUN) {                          /* if the CPU is currently running */
            CPX1 |= cpx1_PFINTR;                        /*   then set the power-fail interrupt */
            CPX2 |= cpx2_RUNSWCH;                       /*     and assume a RUN command */
            status = run_cmd (RU_CONT, cptr);           /*       and continue execution */
            }

        else {                                          /* otherwise the CPU is currently halted */
            cpu_power_state = power_off;                /*   so remain halted in the "power is off" state */
            status = SCPE_OK;                           /*     and return command success */
            }
        }

else if (ctptr->arg == power_returning)                 /* otherwise if a power-restoration option was given */
    if (cpu_power_state != power_off)                   /*   but the CPU power is not off */
        status = SCPE_NOFNC;                            /*     then the command is not allowed */

    else {                                              /* otherwise the restoration is valid */
        reset_all_p (0);                                /*   so reset all devices to their power on states */

        cpu_read_memory (absolute, ICS_Z, &zi);             /* get the ICS stack limit */
        cpu_read_memory (absolute, zi + 1, &failure_cpx2);  /*   and get the value of CPX2 at the time of failure */
        cpu_write_memory (absolute, zi + 1, CPX2);          /*     and replace it with the current CPX2 value */

        if (failure_cpx2 & cpx2_RUN) {                  /* if the CPU was running at the time of power failure */
            cpu_power_state = power_returning;          /*   then move to the "power is returning" state */
            CPX2 |= cpx2_RUNSWCH;                       /*     and assume a RUN command */
            status = run_cmd (RU_CONT, cptr);           /*       and continue execution */
            }

        else {                                          /* otherwise the CPU was halted when power failed */
            cpu_power_state = power_on;                 /*   so remain halted in the "power is on" state */
            status = SCPE_OK;                           /*     and return command success */
            }
        }

else                                                    /* otherwise a valid option has no handler */
    status = SCPE_IERR;                                 /*   so report an internal error */

return status;                                          /* return the operation status */
}



/* CPU global utility routines */


/* Process a run-mode interrupt.

   This routine is called when one or more of the interrupt request bits are set
   in the CPX1 register.  The highest-priority request is identified and
   cleared, the interrupt classification and parameter are established, and the
   associated interrupt handler is set up for execution.  On return, the CPU has
   been configured and is ready to execute the first instruction in the handler.

   On entry, the routine first checks for an external interrupt alone; this is
   done to improve performance, as this is the most common case.  If some other
   interrupt is requested, or if multiple interrupts are requested, the CPX1
   register is scanned from MSB to LSB to identify the request.


   Implementation notes:

    1. In hardware, halting the CPU while a PAUS instruction is executing leaves
       P pointing to the following instruction, which is executed when the RUN
       button is pressed.  In simulation, this action occurs only when execution
       is resumed with the "-B" option to bypass the pause.  Otherwise, resuming
       (or stepping) continues with the PAUS instruction.

       If an interrupt occurs while PAUS is executing, the interrupt handler
       will return to the instruction following the PAUS, as though HALT and RUN
       had been pressed.  This occurs because the P register normally points two
       instructions past the instruction currently executing.  When an interrupt
       occurs, P is decremented to point at the instruction after the current
       instruction, which is the correct point of return after the interrupt
       service routine completes.

       When the simulator is stopped, P is backed up to point at the next
       instruction to execute.  In the case of a PAUS instruction, the "next
       instruction" is the same PAUS instruction.  When simulation resumes, the
       PAUS instruction is fetched into the NIR, and P is incremented.  If no
       interrupt is pending, the main instruction execution loop copies the NIR
       into the CIR, prefetches the instruction following the PAUS into the NIR,
       and increments P again, so that it points two instructions beyond the
       current instruction.  At this point, everything is set up properly as
       before the simulation stop.

       However, if an interrupt is pending when simulation resumes, this routine
       is called before the NIR-to-CIR operation is performed, so P still points
       one instruction beyond the PAUS.  Stacking the usual P - 1 value would
       cause the interrupt handler to return to the PAUS instruction instead of
       to the instruction following, as would have occurred had a simulation
       stop not been involved.

       Therefore, when resuming from a simulator stop, the SS_PAUSE_RESUMED flag
       is set in the "cpu_stop_flags" variable by the "halt_mode_interrupt"
       routine if a PAUS instruction is in the NIR after reloading.  If an
       interrupt is pending, this routine will be entered with the flag set, and
       we then increment P so that it is correctly set to point two instructions
       beyond the PAUS before handling the interrupt.  If an interrupt is not
       pending on resumption, the P adjustment is performed as described above,
       and P will be set properly when the next interrupt occurs.

       A special case occurs when resuming into a PAUS from a simulator stop if
       a higher-priority interrupt occurs immediately after handling a pending
       lower-priority interrupt.  In this case, this routine will be entered a
       second time before the instruction execution loop performs the NIR-to-CIR
       operation.  Although the PAUS is still in the CIR, we must not increment
       P a second time, because the instruction now being interrupted is not the
       PAUS but is the first instruction of the lower-priority interrupt routine
       that never had a chance to execute.  Wo do this by clearing the
       SS_PAUSE_RESUMED flag after the first increment.
*/

void cpu_run_mode_interrupt (HP_WORD device_number)
{
HP_WORD   request_set, request_count, parameter;
IRQ_CLASS class;

if (cpu_stop_flags & SS_PAUSE_RESUMED) {                /* if we are resuming into a PAUS instruction */
    P = P + 1 & R_MASK;                                 /*   then set the return to the instruction following */
    cpu_stop_flags &= ~SS_PAUSE_RESUMED;                /*     and clear the flag in case of back-to-back interrupts */
    }

cpu_micro_state = running;                              /* the micromachine may be paused but is no longer */

request_set = CPX1 & CPX1_IRQ_SET;                      /* get the set of active interrupt requests */

if (request_set == cpx1_EXTINTR) {                      /* if only an external request is present */
    class = irq_External;                               /*   (the most common case) then set the class */
    parameter = device_number;                          /*     and set the parameter to the device number */
    }

else {                                                  /* otherwise scan for the class */
    request_count = 0;                                  /*   where CPX1.1 through CPX1.9 */
    request_set = D16_SIGN;                             /*     correspond to IRQ classes 1-9 */

    while ((CPX1 & request_set) == 0) {                 /* scan from left to right for the first request */
        request_count = request_count + 1;              /*   while incrementing the request number */
        request_set = request_set >> 1;                 /*     and shifting the current request bit */
        }

    class = (IRQ_CLASS) request_count;                  /* set the class from the request count */

    if (class == irq_Integer_Overflow) {                /* if an integer overflow occurred */
        parameter = 1;                                  /*   then set the parameter to 1 */
        STA &= ~STATUS_O;                               /*     and clear the overflow flag */
        }

    else if (class == irq_External)                     /* otherwise if an external interrupt occurred */
        parameter = device_number;                      /*   then set the parameter to the device number */

    else if (class == irq_Module) {                     /* otherwise if the class is a module interrupt */
        parameter = UPPER_BYTE (MOD);                   /*   then the parameter is the module number */
        MOD = 0;                                        /* clear the register to prevent a second interrupt */
        }

    else if (class == irq_Power_Fail) {                 /* otherwise if a power fail interrupt occurred */
        parameter = TO_LABEL (LABEL_IRQ, class);        /*   then the parameter is the label */
        cpu_power_state = power_failing;                /*     and system power is now failing */
        }

    else                                                /* otherwise the parameter */
        parameter = TO_LABEL (LABEL_IRQ, class);        /*   is the label */
    }

CPX1 &= ~request_set;                                   /* clear the associated CPX request bit */

dprintf (cpu_dev, DEB_INSTR, BOV_FORMAT "%s interrupt\n",
         PBANK, P - 1 & R_MASK, parameter, interrupt_name [class]);

cpu_setup_irq_handler (class, parameter);               /* set up the entry into the interrupt handler */

return;
}


/* Set up a front panel operation.

   This routine sets the SWCH register to the supplied value and then sets the
   appropriate bit in the CPX2 register.  This will cause a halt-mode interrupt
   when simulated execution is resumed.


   Implementation notes:

    1. We do this here to avoid having to export the registers and CPX values
       globally.
*/

void cpu_front_panel (HP_WORD switch_reg, PANEL_TYPE request)
{
SWCH = switch_reg;                                      /* set the SWCH register value */

switch (request) {                                      /* dispatch on the request type */

    case Run:                                           /* a run request */
        CPX2 |= cpx2_RUNSWCH;                           /* set the RUN switch */
        break;

    case Cold_Load:                                     /* a cold load request */
        CPX2 |= cpx2_LOADSWCH;                          /* set the LOAD switch */
        break;

    case Cold_Dump:                                     /* a cold dump request */
        CPX2 |= cpx2_DUMPSWCH;                          /* set the DUMP switch */
        break;
    }                                                   /* all cases are handled */

return;
}


/* Update the process clock.

   If the process clock is currently calibrated, then the service interval is
   actually ten times the hardware period of 1 millisecond.  This provides
   sufficient event service call spacing to allow idling to work.

   To present the correct value when the process clock is read, this routine is
   called to increment the count by an amount proportional to the fraction of
   the service interval that has elapsed.  In addition, it is called by the CPU
   instruction postlude, so that the PCLK register will have the correct value
   if it is examined from the SCP command prompt.
*/

void cpu_update_pclk (void)
{
int32 elapsed, ticks;

if (cpu_is_calibrated) {                                /* if the process clock is calibrated */
    elapsed = cpu_unit [0].wait                         /*   then the elapsed time is the original wait time */
                - sim_activate_time (&cpu_unit [0]);    /*     less the time remaining before the next service */

    ticks =                                             /* the adjustment is */
       (elapsed * PCLK_MULTIPLIER) / cpu_unit [0].wait  /*   the elapsed fraction of the multiplier */
         - (PCLK_MULTIPLIER - pclk_increment);          /*     less the amount of any adjustment already made */

    PCLK = PCLK + ticks & R_MASK;                       /* update the process clock counter with rollover */
    pclk_increment = pclk_increment - ticks;            /*   and reduce the amount remaining to add at service */
    }

return;
}



/* CPU global instruction execution routines */


/* Push the stack down.

   This routine implements the PUSH micro-order to create space on the stack for
   a new value.  On return, the new value may be stored in the RA register.

   If the SR register indicates that all of the TOS registers are in use, then
   the RD register is freed by performing a queue down.  Then the values in the
   TOS registers are shifted down, freeing the RA register.  Finally, SR is
   incremented in preparation for the store.
*/

void cpu_push (void)
{
if (SR == 4)                                            /* if all TOS registers are full */
    cpu_queue_down ();                                  /*   then move the RD value to memory */

RD = RC;                                                /* shift */
RC = RB;                                                /*   the register */
RB = RA;                                                /*     values down */

SR = SR + 1;                                            /* increment the register-in-use count */

return;
}


/* Pop the stack up.

   This routine implements the POP micro-order to delete the top-of-stack value.

   On entry, if the SR register indicates that all of the TOS registers are
   empty, then if decrementing the SM register would move it below the DB
   register value, and the CPU is not in privileged mode, then a Stack Underflow
   trap is taken.  Otherwise, the stack memory pointer is decremented.

   If one or more values exist in the TOS registers, the values are shifted up,
   deleting the previous value in the RA register, and SR is decremented.
*/

void cpu_pop (void)
{
if (SR == 0) {                                          /* if the TOS registers are empty */
    if (SM <= DB && NPRV)                               /*   then if SM isn't above DB and the mode is non-privileged */
        MICRO_ABORT (trap_Stack_Underflow);             /*     then trap with a Stack Underflow */

    SM = SM - 1 & R_MASK;                               /* decrement the stack memory register */
    }

else {                                                  /* otherwise at least one TOS register is occupied */
    RA = RB;                                            /*   so shift */
    RB = RC;                                            /*     the register */
    RC = RD;                                            /*       values up */

    SR = SR - 1;                                        /* decrement the register-in-use count */
    }

return;
}


/* Queue a value from memory up to the register file.

   This routine implements the QUP micro-order to move the value at the top of
   the memory stack into the bottom of the TOS register file.  There must be a
   free TOS register when this routine is called.

   On entry, if decrementing the SM register would move it below the DB register
   value, and the CPU is not in privileged mode, then a Stack Underflow trap is
   taken.  Otherwise, the value pointed to by SM is read into the first unused
   TOS register, SM is decremented to account for the removed value, and SR is
   incremented to account for the new TOS register in use.


   Implementation notes:

    1. SR must be less than 4 on entry, so that TR [SR] is the first unused TOS
       register.

    2. SM and SR must not be modified within the call to cpu_read_memory.  For
       example, SR++ cannot be passed as a parameter.
*/

void cpu_queue_up (void)
{
if (SM <= DB && NPRV)                                   /* if SM isn't above DB and the mode is non-privileged */
    MICRO_ABORT (trap_Stack_Underflow);                 /*   then trap with a Stack Underflow */

else {                                                  /* otherwise */
    cpu_read_memory (stack, SM, &TR [SR]);              /*   read the value from memory into a TOS register */

    SM = SM - 1 & R_MASK;                               /* decrement the stack memory register */
    SR = SR + 1;                                        /*   and increment the register-in-use count */
    }

return;
}


/* Queue a value from the register file down to memory.

   This routine implements the QDWN micro-order to move the value at the bottom
   of the TOS register file into the top of the memory stack.  There must be a
   TOS register in use when this routine is called.

   On entry, if incrementing the SM register would move it above the Z register
   value, then a Stack Overflow trap is taken.  Otherwise, SM is incremented to
   account for the new value written, SR is decremented to account for the TOS
   register removed from use, and the value in that TOS register is written into
   memory at the top of the memory stack.


   Implementation notes:

    1. SR must be greater than 0 on entry, so that TR [SR - 1] is the last TOS
       register in use.

    2. SM and SR must not be modified within the call to cpu_write_memory.  For
       example, SR-- cannot be passed as a parameter.
*/

void cpu_queue_down (void)
{
if (SM >= Z)                                            /* if SM isn't below Z */
    MICRO_ABORT (trap_Stack_Overflow);                  /*   then trap with a Stack Overflow */

SM = SM + 1 & R_MASK;                                   /* increment the stack memory register */
SR = SR - 1;                                            /*   and decrement the register-in-use count */

cpu_write_memory (stack, SM, TR [SR]);                  /* write the value from a TOS register to memory */

return;
}


/* Flush the register file.

   This routine implements the PSHA microcode subroutine that writes the values
   of all TOS registers in use to the memory stack.  As each value is written,
   the SM register is incremented and the SR register is decremented.  On
   return, the SR register value will be zero.

   The routine does not check for stack overflow.
*/

void cpu_flush (void)
{
while (SR > 0) {                                        /* while one or more registers are in use */
    SM = SM + 1 & R_MASK;                               /*   increment the stack memory register */
    SR = SR - 1;                                        /*     and decrement the register-in-use count */

    cpu_write_memory (stack, SM, TR [SR]);              /* write the value from a TOS register to memory */
    }

return;
}


/* Adjust SR until it reaches a specified value.

   This routine implements the SRP1-SRP4 microcode subroutines that adjust the
   stack until the prescribed number (1-4) of TOS registers are occupied.  It
   performs queue ups, i.e., moves values from the top of the memory stack to
   the bottom of the register file, until the specified SR value is reached.
   Stack underflow is checked after the all of the values have been moved.  The
   routine assumes that at least one value must be moved.


   Implementation notes:

    1. SR must be greater than 0 on entry, so that TR [SR - 1] is the last TOS
       register in use.

    2. SM and SR must not be modified within the call to cpu_read_memory.  For
       example, SR++ cannot be passed as a parameter.

    3. The cpu_queue_up routine isn't used, as that routine checks for a stack
       underflow after each word is moved rather than only after the last word.
*/

void cpu_adjust_sr (uint32 target)
{
do {
    cpu_read_memory (stack, SM, &TR [SR]);              /* read the value from memory into a TOS register */

    SM = SM - 1 & R_MASK;                               /* decrement the stack memory register */
    SR = SR + 1;                                        /*   and increment the register-in-use count */
    }
while (SR < target);                                    /* queue up until the requested number of registers are in use */

if (SM <= DB && NPRV)                                   /* if SM isn't above DB, or the mode is non-privileged */
    MICRO_ABORT (trap_Stack_Underflow);                 /*   then trap with a Stack Underflow */

return;
}


/* Write a stack marker to memory.

   This routine implements the STMK microcode subroutine that writes a four-word
   marker to the stack.  The format of the marker is as follows:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       X register value                        |  [Q - 3]  X
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  PB-relative return address                   |  [Q - 2]  P + 1 - PB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     Status register value                     |  [Q - 1]  STA
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         Delta Q value                         |  [Q - 0]  S - Q
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   After the values are written, the Q register is set to point to the marker.

   This routine is always entered with SR = 0, i.e., with the TOS registers
   flushed to the memory stack.  It does not check for stack overflow.


   Implementation notes:

    1. The PB-relative return address points to the instruction after the point
       of the call.  Conceptually, this is location P + 1, but because the CPU
       uses a two-instruction prefetch, the location is actually P - 1.
*/

void cpu_mark_stack (void)
{
SM = SM + 4 & R_MASK;                                   /* adjust the stack pointer */

cpu_write_memory (stack, SM - 3, X);                    /* push the index register */
cpu_write_memory (stack, SM - 2, P - 1 - PB & LA_MASK); /*   and delta P */
cpu_write_memory (stack, SM - 1, STA);                  /*     and the status register */
cpu_write_memory (stack, SM - 0, SM - Q & LA_MASK);     /*       and delta Q */

Q = SM;                                                 /* set Q to point to the new stack marker */

return;
}


/* Calculate an effective memory address.

   This routine calculates the effective address for a memory reference or
   branch instruction.  On entry, "mode_disp" contains the mode, displacement,
   index, and indirect fields of the instruction, "classification" and "offset"
   point to variables to receive the corresponding values, and "selector" points
   to a variable to receive the byte selection ("upper" or "lower") for byte-
   addressable instructions or is NULL for word-addressable instructions.  On
   exit, "classification" is set to the memory access classification, "offset"
   is set to the address offset within the memory bank implied by the
   classification, and "selector" is set to indicate the byte referenced if the
   pointer is non-NULL.

   The mode and displacement fields of the instruction encode an address
   relative to one of the base registers P, DB, Q, or S, as follows:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   memory op   | X | I |         mode and displacement         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             | 0 | 0 |     P+ displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 0 | 1 |     P- displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 0 |    DB+ displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 0 |   Q+ displacement 0-127   |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 1 | 0 | Q- displacement 0-63  |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 1 | 1 | S- displacement 0-63  |
                             +---+---+---+---+---+---+---+---+---+---+

   The displacement encoded in the instruction is an unsigned value that is
   added to or subtracted from the indicated base register.

   If the X and I fields are both 0, the addressing is direct.  If the X field
   is 1, the addressing is indexed.  If the I field is 1, the addressing is
   indirect.  If both fields are 1, the addressing is indirect indexed, with
   indirection performed before indexing.

   To improve execution speed in hardware, a preadder is implemented that sums
   the offset contained in certain bits of the CIR with the index register (if
   enabled).  The primary use is to save a microinstruction cycle during memory
   reference instructions, which must add a base register, the offset in the
   CIR, and potentially the X register (either directly or shifted left or right
   by one place for LDD/STD or LDB/STB, respectively).  The preadder also serves
   to hold other counts obtained from the CIR, e.g., shift counts, although in
   these cases, the addition function is not used.

   This routine simulates the preadder as part of the effective address
   calculation.  The calculations employed for word addressing are:

     Direct word addressing:
       ea = PBANK.(P + displacement)
       ea = DBANK.(DB + displacement)
       ea = SBANK.(Q,S + displacement)

     Direct indexed word addressing:
       ea = PBANK.(P + displacement + X)
       ea = DBANK.(DB + displacement + X)
       ea = SBANK.(Q,S + displacement + X)

     Indirect word addressing:
       ea = PBANK.(P + displacement + M [PBANK.(P + displacement)])
       ea = DBANK.(DB + M [DBANK.(DB + displacement)])
       ea = DBANK.(DB + M [SBANK.(Q,S + displacement)])

     Indirect indexed word addressing:
       ea = PBANK.(P + displacement + M [PBANK.(P + displacement)] + X)
       ea = DBANK.(DB + M [DBANK.(DB + displacement)] + X)
       ea = DBANK.(DB + M [SBANK.(Q,S + displacement)] + X)

   The indirect cell contains either a self-relative, P-relative address or a
   DB-relative address, even for S or Q-relative modes.  Indirect branches with
   DB/Q/S-relative mode are offsets from PB, not DB.

   The effective address calculations employed for byte addressing are:

     Direct byte addressing:
       ea = DBANK.(DB + displacement).byte [0]
       ea = SBANK.(Q,S + displacement).byte [0]

     Direct indexed byte addressing:
       ea = DBANK.(DB + displacement + X / 2).byte [X & 1]
       ea = SBANK.(Q,S + displacement + X / 2).byte [X & 1]

     Indirect byte addressing:
       ea,I = DBANK.(DB + M [DBANK.(DB + displacement)] / 2).byte [cell & 1]
       ea,I = DBANK.(DB + M [SBANK.(Q,S + displacement)] / 2).byte [cell & 1]

     Indirect indexed byte addressing:
       ea,I = DBANK.(DB + (M [DBANK.(DB + displacement)] + X) / 2).byte [cell + X & 1]
       ea,I = DBANK.(DB + (M [SBANK.(Q,S + displacement)] + X) / 2).byte [cell + X & 1]

   For all modes, the displacement is a word address, whereas the indirect cell
   and index register contain byte offsets.  For direct addressing, the byte
   selected is byte 0.  For all other modes, the byte selected is the byte at
   (offset & 1), where the offset is the index register value, the indirect cell
   value, or the sum of the two.

   Byte offsets into data segments present problems, in that negative offsets
   are permitted (to access the DL-to-DB area), but there are not enough bits to
   represent all locations unambiguously in the potential -32K to +32K word
   offset range.  Therefore, a byte offset with bit 0 = 1 can represent either a
   positive or negative word offset from DB, depending on the interpretation.
   The HP 3000 adopts the convention that if the address resulting from a
   positive-offset interpretation does not fall within the DL-to-S range, then
   32K is added to the address, effectively changing the interpretation from a
   positive to a negative offset.  If this new address does not fall within the
   DL-to-S range, a Bounds Violation trap occurs if the mode is non-privileged.

   The reinterpretation as a negative offset is performed only if the CPU is not
   in split-stack mode (where either DBANK is different from SBANK, or DB does
   not lie between DL and Z), as extra data segments do not permit negative-DB
   addressing.  Reinterpretation is also not used for code segments, as negative
   offsets from PB are not permitted.


   Implementation notes:

    1. On entry, the program counter points to the instruction following the
       next instruction (i.e., the NIR location + 1).  However, P-relative
       offsets are calculated from the current instruction (CIR location).
       Therefore, we decrement P by two before adding the offset.

       In hardware, P-relative addresses obtained from the preadder are offset
       from P + 1, whereas P-relative addresses obtained by summing with
       P-register values obtained directly from the S-Bus are offset from P + 2.
       This is because the P-register increment that occurs as part of a NEXT
       micro-order is coincident with the R-Bus and S-Bus register loads; both
       operations occur when the NXT+1 signal asserts.  Therefore, the microcode
       handling P-relative memory reference address resolution subtracts one to
       get the P value corresponding to the CIR, whereas branches on overflow,
       carry, etc. subtract two.

    2. If the mode is indirect, this routine handles bounds checks and TOS
       register accesses on the initial address.

    3. The System Reference Manual states that byte offsets are interpreted as
       negative if the effective address does not lie between DL and Z.
       However, the Series II microcode actually uses DL and S for the limits.

    4. This routine calculates the effective address for the memory address
       instructions.  These instructions always bounds-check their accesses, and
       data and stack accesses are mapped to the TOS registers if the effective
       address lies between SM and SM + SR within the stack bank.
*/

void cpu_ea (HP_WORD mode_disp, ACCESS_CLASS *classification, HP_WORD *offset, BYTE_SELECTOR *selector)
{
HP_WORD      base, displacement;
ACCESS_CLASS class;

switch ((mode_disp & MODE_MASK) >> MODE_SHIFT) {        /* dispatch on the addressing mode */

    case 000:
    case 001:
    case 002:
    case 003:                                           /* positive P-relative displacement */
        base = P - 2 + (mode_disp & DISPL_255_MASK);    /* add the displacement to the base */
        class = program_checked;                        /*   and classify as a program reference */
        break;

    case 004:
    case 005:
    case 006:
    case 007:                                           /* negative P-relative displacement */
        base = P - 2 - (mode_disp & DISPL_255_MASK);    /* subtract the displacement from the base */
        class = program_checked;                        /*   and classify as a program reference */
        break;

    case 010:
    case 011:
    case 012:
    case 013:                                           /* positive DB-relative displacement */
        base = DB + (mode_disp & DISPL_255_MASK);       /* add the displacement to the base */
        class = data_mapped_checked;                    /*   and classify as a data reference */
        break;

    case 014:
    case 015:                                           /* positive Q-relative displacement */
        base = Q + (mode_disp & DISPL_127_MASK);        /* add the displacement to the base */
        class = stack_checked;                          /*   and classify as a stack reference */
        break;

    case 016:                                           /* negative Q-relative displacement */
        base = Q - (mode_disp & DISPL_63_MASK);         /* subtract the displacement from the base */
        class = stack_checked;                          /*   and classify as a stack reference */
        break;

    case 017:                                           /* negative S-relative displacement */
        base = SM + SR - (mode_disp & DISPL_63_MASK);   /* subtract the displacement from the base */
        class = stack_checked;                          /*   and classify as a stack reference */
        break;
    }                                                   /* all cases are handled */


if (!(mode_disp & I_FLAG_BIT_5))                            /* if the mode is direct */
    displacement = 0;                                       /*   then there's no displacement */

else {                                                      /* otherwise the mode is indirect */
    cpu_read_memory (class, base & LA_MASK, &displacement); /*   so get the displacement value */

    if ((CIR & BR_MASK) == BR_DBQS_I) {                     /* if this a DB/Q/S-relative indirect BR instruction */
        base = PB;                                          /*   then PB is the base for the displacement */
        class = program_checked;                            /* reclassify as a program reference */
        }

    else if (class != program_checked) {                    /* otherwise if it is a data or stack reference */
        base = DB;                                          /*   then DB is the base for the displacement */
        class = data_mapped_checked;                        /* reclassify as a data reference */
        }
                                                        /* otherwise, this is a program reference */
    }                                                   /*   which is self-referential */

if ((CIR & LSDX_MASK) == LDD_X                          /* if the mode */
  || (CIR & LSDX_MASK) == STD_X)                        /*   is double-word indexed */
    displacement = displacement + X * 2 & DV_MASK;      /*     then add the doubled index to the displacement */

else if (mode_disp & X_FLAG)                            /* otherwise if the mode is indexed */
    displacement = displacement + X & DV_MASK;          /*   then add the index to the displacement */

if (selector == NULL)                                   /* if a word address is requested */
    base = base + displacement;                         /*   then add in the word displacement */

else if ((mode_disp & (X_FLAG | I_FLAG_BIT_5)) == 0)    /* otherwise if a direct byte address is requested */
    *selector = upper;                                  /*   then it references the upper byte */

else {                                                  /* otherwise an indexed or indirect byte address is requested */
    if (displacement & 1)                               /*   so if the byte displacement is odd */
        *selector = lower;                              /*     then the lower byte was requested */
    else                                                /*   otherwise it is even */
        *selector = upper;                              /*     and the upper byte was requested */

    base = base + (displacement >> 1) & LA_MASK;        /* convert the displacement from byte to word and add */

    if (DBANK == SBANK && DL <= DB && DB <= Z           /* if not in split-stack mode */
     && (base < DL || base > SM + SR))                  /*   and the word address is out of range */
        base = base ^ D16_SIGN;                         /*     then add 32K to swap the offset polarity */
    }

*offset = base & LA_MASK;                               /* set the */
*classification = class;                                /*   return values */

return;
}


/* Convert a data- or program-relative byte address to a word effective address.

   The supplied byte offset from DB or PB is converted to a memory address,
   bounds-checked if requested, and returned.  If the supplied block length is
   not zero, the converted address is assumed to be the starting address of a
   block, and an ending address, based on the block length, is calculated and
   bounds-checked if requested.  If either address lies outside of the segment
   associated with the access class, a Bounds Violation trap occurs, unless a
   privileged data access is requested.

   The "class" parameter indicates whether the offset is from DB or PB and
   must be "data", "data_checked", "program", or "program_checked".  No other
   access classes are supported.  In particular, "data_mapped" and
   "data_mapped_checked" are not allowed, as callers of this routine never map
   accesses to the TOS registers.

   Byte offsets into data segments present problems, in that negative offsets
   are permitted (to access the DL-to-DB area), but there are not enough bits to
   represent all locations unambiguously in the potential -32K to +32K word
   offset range.  Therefore, a byte offset with bit 0 = 1 can represent either a
   positive or negative word offset from DB, depending on the interpretation.
   The HP 3000 adopts the convention that if the address resulting from a
   positive-offset interpretation does not fall within the DL-to-S range, then
   32K is added to the address, effectively changing the interpretation from a
   positive to a negative offset.  If this new address does not fall within the
   DL-to-S range, a Bounds Violation trap occurs if the mode is non-privileged.

   The reinterpretation as a negative offset is performed only if the CPU is not
   in split-stack mode (where either DBANK is different from SBANK, or DB does
   not lie between DL and Z), as extra data segments do not permit negative-DB
   addressing.  Reinterpretation is also not used for code segments, as negative
   offsets from PB are not permitted.


   Implementation notes:

    1. This routine implements the DBBC microcode subroutine.
*/

uint32 cpu_byte_ea (ACCESS_CLASS class, uint32 byte_offset, uint32 block_length)
{
uint32 starting_word, ending_word, increment;

if (block_length & D16_SIGN)                            /* if the block length is negative */
    increment = 0177777;                                /*   then the memory increment is negative also */
else                                                    /* otherwise */
    increment = 1;                                      /*   the increment is positive */

if (class == program || class == program_checked) {     /* if this is a program access */
    starting_word = PB + (byte_offset >> 1) & LA_MASK;  /*   then determine the starting word address */

    if (class == program_checked                        /* if checking is requested */
      && starting_word < PB || starting_word > PL)      /*   and the starting address is out of range */
        MICRO_ABORT (trap_Bounds_Violation);            /*     then trap for a bounds violation */

    if (block_length != 0) {                            /* if a block length was supplied */
        ending_word =                                   /*   then determine the ending address */
           starting_word + ((block_length - increment + (byte_offset & 1)) >> 1) & LA_MASK;

        if (class == program_checked                    /* if checking is requested */
          && ending_word < PB || ending_word > PL)      /*   and the ending address is out of range */
            MICRO_ABORT (trap_Bounds_Violation);        /*     then trap for a bounds violation */
        }
    }

else {                                                  /* otherwise this is a data address */
    starting_word = DB + (byte_offset >> 1) & LA_MASK;  /*   so determine the starting word address */

    if (DBANK == SBANK && DL <= DB && DB <= Z           /* if not in split-stack mode */
      && (starting_word < DL || starting_word > SM)) {  /*   and the word address is out of range */
        starting_word = starting_word ^ D16_SIGN;       /*     then add 32K and try again */

        if (class == data_checked && NPRV                   /* if checking is requested and non-privileged */
          && (starting_word < DL || starting_word > SM))    /*   and still out of range */
            MICRO_ABORT (trap_Bounds_Violation);            /*     then trap for a bounds violation */
        }

    if (block_length != 0) {                            /* if a block length was supplied */
        ending_word =                                   /*   then determine the ending word address */
           starting_word + ((block_length - increment + (byte_offset & 1)) >> 1) & LA_MASK;

        if (class == data_checked && NPRV               /* if checking is requested and non-privileged */
          && (ending_word < DL || ending_word > SM))    /*   and the address is out of range */
            MICRO_ABORT (trap_Bounds_Violation);        /*     then trap for a bounds violation */
        }
    }


return starting_word;                                   /* return the starting word address */
}


/* Set up the entry into an interrupt handler.

   This routine prepares the CPU state to execute an interrupt handling
   procedure.  On entry, "class" is the classification of the current interrupt,
   and "parameter" is the parameter associated with the interrupt.  On exit, the
   stack has been set up correctly, and the PB, P, PL, and status registers have
   been set up for entry into the interrupt procedure.

   Run-mode interrupts are classified as external or internal and ICS or
   non-ICS.  External interrupts are those originating with the device
   controllers, and internal interrupts are conditions detected by the microcode
   (e.g., a bounds violation or arithmetic overflow).  ICS interrupts execute
   their handlers on the system's Interrupt Control Stack.  Non-ICS interrupts
   execute on the user's stack.

   Of the run-mode interrupts, the External, System Parity Error, Address
   Parity Error, Data Parity Error, and Module interrupts execute on the ICS.
   All other interrupts execute on the user's stack.  The routine begins by
   determining whether an ICS or non-ICS interrupt is indicated.  The
   appropriate stack is established, and the stack marker is written to preserve
   the state of the interrupted routine.  The label of the handler procedure is
   obtained, and then the procedure designated by the label is set up.  On
   return, the first instruction of the handler is ready to execute.


   Implementation notes:

    1. This routine implements various execution paths through the microcode
       labeled as INT0 through INT7.

    2. This routine is also called directly by the IXIT instruction executor if
       an external interrupt is pending.  This is handled as an external
       interrupt but is classified differently so that the teardown and rebuild
       of the stack may be avoided to improve performance.
*/

void cpu_setup_irq_handler (IRQ_CLASS class, HP_WORD parameter)
{
HP_WORD label;

if (class == irq_External || class == irq_IXIT) {       /* if entry is for an external interrupt */
    if (class == irq_External)                          /*   then if it was detected during normal execution */
        cpu_setup_ics_irq (class, 0);                   /*     then set it up on the ICS */
    else                                                /*   otherwise it was detected during IXIT */
        SM = Q + 2 & R_MASK;                            /*     so the ICS is already set up */

    DBANK = 0;                                          /* all handlers are in bank 0 */
    STA = STATUS_M | STATUS_I;                          /* enter privileged mode with interrupts enabled */

    cpu_read_memory (stack, parameter * 4 + 2, &DB);    /* read the DB value */
    cpu_read_memory (stack, parameter * 4 + 1, &label); /*   and the procedure label from the DRT */
    }

else if (class >= irq_System_Parity                     /* otherwise if entry is for */
  && class <= irq_Power_Fail) {                         /*   another ICS interrupt */
    cpu_setup_ics_irq (class, 0);                       /*     then set it up on the ICS */

    label = TO_LABEL (LABEL_IRQ, class);                /* form the label for the specified classification */

    STA = STATUS_M;                                     /* clear status and enter privileged mode */
    }

else {                                                  /* otherwise entry is for a non-ICS interrupt */
    if (class == irq_Integer_Overflow)                  /* if this is an integer overflow interrupt */
        label = TO_LABEL (LABEL_IRQ, trap_User);        /*   then form the label for a user trap */
    else                                                /* otherwise form the label */
        label = TO_LABEL (LABEL_IRQ, class);            /*   for the specified classification */

    cpu_flush ();                                       /* flush the TOS registers to memory */
    cpu_mark_stack ();                                  /*   and write a stack marker */

    STA = STATUS_M;                                     /* clear status and enter privileged mode */
    }

SM = SM + 1 & R_MASK;                                   /* increment the stack pointer */
cpu_write_memory (stack, SM, parameter);                /*   and push the parameter on the stack */

X = CIR;                                                /* save the CIR in the index register */

cpu_call_procedure (label, 0);                          /* set up to call the interrupt handling procedure */

return;
}


/* Set up an interrupt on the Interrupt Control Stack.

   This routine prepares the Interrupt Control Stack (ICS) to support interrupt
   processing.  It is called from the run-time interrupt routine for ICS
   interrupts, the microcode abort routine for ICS traps, and from the DISP and
   PSEB instruction executors before entering the dispatcher.  On entry, "class"
   is the interrupt classification, and, if the class is "irq_Trap", then "trap"
   is the trap classification.  The trap classification is ignored for
   interrupts, including the dispatcher start interrupt.

   Unless entry is for a Cold Load trap, the routine begins by writing a
   six-word stack marker.  This special ICS marker extends the standard marker
   by adding the DBANK and DB values as follows:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       X register value                        |  [Q - 3]  X
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  PB-relative return address                   |  [Q - 2]  P + 1 - PB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     Status register value                     |  [Q - 1]  STA
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | D |                     Delta Q value                         |  [Q - 0]  S - Q
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         DB-Bank value                         |  [Q + 1]  DBANK
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                           DB value                            |  [Q + 2]  DB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     D = the dispatcher was interrupted

   After the values are written, the Q register is set to point to the marker.
   The stack bank register is then cleared, as the ICS is always located in
   memory bank 0.

   If the interrupt or trap occurred while executing on the ICS, and the
   dispatcher was running at the time, the "dispatcher running" bit in the CPX1
   register is cleared, and the D-bit is set in the delta-Q value word of the
   stack marker.  This bit will be used during interrupt exit to decide whether
   to restart the dispatcher.

   If the CPU was executing on the user's stack, the "ICS flag" bit in CPX1 is
   set, the Q register is reset to point at the permanent dispatcher stack
   marker established by the operating system, and the Z register is reset to
   the stack limit established by the OS for the ICS; the values are obtained
   from reserved memory locations 5 and 6, respectively.  The ICS DB value is
   read from the ICS global area that precedes the dispatcher stack marker and
   is used to write the stack-DB-relative S value back to the global area.

   Finally, the stack pointer is set to point just above the stack marker.


   Implementation notes:

    1. This routine implements various execution paths through the microcode
       labeled as INT1 through INT5.
*/

void cpu_setup_ics_irq (IRQ_CLASS class, TRAP_CLASS trap)
{
HP_WORD delta_q, stack_db;

if (class != irq_Trap                                   /* if this is not */
  || trap != trap_Cold_Load && trap != trap_Power_On) { /*   a cold load or power on trap entry */
    cpu_flush ();                                       /*     then flush the TOS registers to memory */
    cpu_mark_stack ();                                  /*       and write a four-word stack marker */

    cpu_write_memory (stack, SM + 1 & LA_MASK, DBANK);  /* add DBANK and DB to the stack */
    cpu_write_memory (stack, SM + 2 & LA_MASK, DB);     /*   to form a six-word ICS marker */
    }

SBANK = 0;                                              /* the ICS is always located in bank 0 */

if (CPX1 & cpx1_ICSFLAG) {                              /* if execution is currently on the ICS */
    if (CPX1 & cpx1_DISPFLAG) {                         /*   then if the dispatcher was interrupted */
        CPX1 &= ~cpx1_DISPFLAG;                         /*     then clear the dispatcher flag */

        cpu_read_memory (stack, Q, &delta_q);           /* get the delta Q value from the stack marker */
        cpu_write_memory (stack, Q, delta_q | STMK_D);  /*   and set the dispatcher-interrupted flag */
        }
    }

else {                                                  /* otherwise execution is on the user's stack */
    CPX1 |= cpx1_ICSFLAG;                               /*   so set the ICS flag */

    cpu_read_memory (stack, ICS_Q, &Q);                 /* set Q = QI */
    cpu_read_memory (stack, ICS_Z, &Z);                 /* set Z = ZI */

    cpu_read_memory (stack, Q - 4 & LA_MASK,            /* read the stack DB value */
                     &stack_db);

    cpu_write_memory (stack, Q - 6 & LA_MASK,           /* write the stack-DB-relative S value */
                      SM + 2 - stack_db & DV_MASK);     /*   which is meaningless for a cold load or power on */

    SR = 0;                                             /* invalidate the stack registers for a cold load */
    DL = D16_UMAX;                                      /*   and set the data limit */
    }

SM = Q + 2 & R_MASK;                                    /* set S above the stack marker */

return;
}


/* Set up a code segment.

   This routine is called to set up a code segment in preparation for calling or
   exiting a procedure located in a segment different from the currently
   executing segment.  On entry, "label" indicates the segment number containing
   the procedure.  On exit, the new status register value and the first word of
   the Code Segment Table entry are returned to the variables pointed to by
   "status" and "entry_0", respectively.

   The routine begins by reading the CST pointer.  The CST is split into two
   parts: a base table, and an extension table.  The table to use is determined
   by the requested segment number.  Segment numbers 0 and 192, corresponding to
   the first entries of the two tables, are reserved and cause a CST Violation
   trap if specified.

   The CST entry corresponding to the segment number is examined to set the
   program bank, base, and limit registers (the segment length stored in the
   table is number of quad-words, which must be multiplied by four to get the
   size in words).  The new status register value is set up and returned, along
   with the first word of the CST entry.


   Implementation notes:

    1. This routine implements the microcode SSEG subroutine.

    2. Passing -1 as a parameter to trap_CST_Violation ensures that the segment
       number >= 2 check will pass and the trap handler will be invoked.

    3. The Series II microcode sets PBANK and PB unilaterally but sets PL only
       if the code segment is not absent.  An absent segment entry contains the
       disc address in words 3 and 4 instead of the bank address and base
       address, so PBANK and PB will contain invalid values in this case.  It is
       not clear why the microcode avoids setting PL; the microinstruction in
       question also sets Flag 2, so conditioning PL may be just a side effect.
       In any case, we duplicate the firmware behavior here.

    4. This routine is only used locally, but we leave it as a global entry to
       support future firmware extensions that may need to call it.
*/

void cpu_setup_code_segment (HP_WORD label, HP_WORD *status, HP_WORD *entry_0)
{
HP_WORD cst_pointer, cst_size, cst_entry, cst_bank, segment_number, entry_number;

segment_number = STT_SEGMENT (label);                       /* isolate the segment number from the label */

if (segment_number < CST_RESERVED) {                        /* if the target segment is in the base table */
    cpu_read_memory (absolute, CSTB_POINTER, &cst_pointer); /*   then read the CST base pointer */
    entry_number = segment_number;                          /*     and set the entry number */
    }

else {                                                      /* otherwise it is in the extension table */
    cpu_read_memory (absolute, CSTX_POINTER, &cst_pointer); /*   so read the CST extension pointer */
    entry_number = segment_number - CST_RESERVED;           /*     and set the entry number */
    }

if (entry_number == 0)                                  /* segment numbers 0 and 192 do not exist */
    MICRO_ABORTP (trap_CST_Violation, -1);              /*   so trap for a violation if either is specified */

cpu_read_memory (absolute, cst_pointer, &cst_size);     /* read the table size */

if (entry_number > cst_size)                            /* if the entry is outside of the table */
    MICRO_ABORTP (trap_CST_Violation, entry_number);    /*   then trap for a violation */

cst_entry = cst_pointer + entry_number * 4;             /* get the address of the target CST entry */

cpu_read_memory  (absolute, cst_entry, entry_0);                /* get the first word of the entry */
cpu_write_memory (absolute, cst_entry, *entry_0 | CST_R_BIT);   /*   and set the segment reference bit */

cpu_read_memory (absolute, cst_entry + 2, &cst_bank);   /* read the bank address word */
PBANK = cst_bank & CST_BANK_MASK;                       /*   and mask to just the bank number */

cpu_read_memory (absolute, cst_entry + 3, &PB);         /* read the segment's base address */

PL = (*entry_0 & CST_SEGLEN_MASK) * 4 - 1;              /* set PL to the segment length - 1 */

*status = STA & ~LABEL_SEGMENT_MASK | segment_number;   /* set the segment number in the new status word */

if (*entry_0 & CST_M_BIT)                               /* if the segment executes in privileged mode */
    *status |= STATUS_M;                                /*   then set up to enter privileged mode */

if (! (*entry_0 & CST_A_BIT))                           /* if the segment is not absent */
    PL = PL + PB;                                       /*   then set the segment limit */

return;
}


/* Set up a data segment.

   This routine is called to set up a data segment for access.  It is called by
   the MDS, MFDS, and MTDS instruction executors to obtain the bank and offset
   of specified segments from the Data Segment Table.  On entry,
   "segment_number" indicates the number of the desired data segment.  On exit,
   the memory bank number and offset of the data segment base are returned to
   the variables pointed to by "bank" and "address", respectively.

   The routine begins by reading the DST pointer.  Segment number 0,
   corresponding to the first entry of the table, is reserved and causes a DST
   Violation trap if specified.

   The DST entry corresponding to the segment number is examined to obtain the
   bank and base address.  If the segment is absent, a Data Segment Absent trap
   is taken.  Otherwise, the bank and address values are returned.


   Implementation notes:

    1. This routine implements the microcode DSEG subroutine.
*/

void cpu_setup_data_segment (HP_WORD segment_number, HP_WORD *bank, HP_WORD *address)
{
HP_WORD dst_pointer, dst_size, dst_entry, entry_0;

cpu_read_memory (absolute, DST_POINTER, &dst_pointer);  /* read the DST base pointer */

if (segment_number == 0)                                /* segment number 0 does not exist */
    MICRO_ABORT (trap_DST_Violation);                   /*   so trap for a violation if it is specified */

cpu_read_memory (absolute, dst_pointer, &dst_size);     /* read the table size */

if (segment_number > dst_size)                          /* if the entry is outside of the table */
    MICRO_ABORT (trap_DST_Violation);                   /*   then trap for a violation */

dst_entry = dst_pointer + segment_number * 4;           /* get the address of the target DST entry */

cpu_read_memory (absolute, dst_entry, &entry_0);                /* get the first word of the entry */
cpu_write_memory (absolute, dst_entry, entry_0 | DST_R_BIT);    /*   and set the segment reference bit */

if (entry_0 & DST_A_BIT)                                /* if the segment is absent */
    MICRO_ABORTP (trap_DS_Absent, segment_number);      /*   then trap for an absentee violation */

cpu_read_memory (absolute, dst_entry + 2, bank);        /* read the segment bank number */
cpu_read_memory (absolute, dst_entry + 3, address);     /*   and base address */

*bank = *bank & DST_BANK_MASK;                          /* mask off the reserved bits */

return;                                                 /*   before returning to the caller */
}


/* Call a procedure.

   This routine sets up the PB, P, PL, and status registers to enter a
   procedure.  It is called by the PCAL instruction executor and by the
   interrupt and trap routines to set up the handler procedures.  On entry,
   "label" contains an external program label indicating the segment number and
   Segment Transfer Table entry number describing the procedure, or a local
   program label indicating the starting address of the procedure, and "offset"
   contains an offset to be added to the starting address.  On exit, the
   registers are set up for execution to resume with the first instruction of
   the procedure.

   If the label is a local label, the PB-relative address is obtained from the
   label and stored in the P register, and the Next Instruction Register is
   loaded with the first instruction of the procedure.

   If the label is external, the code segment referenced by the label is set up.
   If the "trace" or "absent" bits are set, the corresponding trap is taken.
   Otherwise, the Segment Transfer Table length is read, and the STT entry
   number is validated; if it references a location outside of the table, a STT
   violation trap is taken.

   Otherwise, the valid STT entry is examined.  If the target procedure is not
   in the designated code segment or is uncallable if not in privileged mode,
   the appropriate traps are taken.  If the STT entry contains a local label, it
   is used to set up the P register and NIR as above.

   The "offset" parameter is used only by the XBR instruction executor.  The
   PCAL executor and the interrupt handlers pass an offset of zero to begin
   execution at the first instruction of the designated procedure.


   Implementation notes:

    1. This routine implements the microcode PCL3 and PCL5 subroutines.
*/

void cpu_call_procedure (HP_WORD label, HP_WORD offset)
{
HP_WORD new_status, new_label, new_p, cst_entry, stt_size, stt_entry;

new_status = STA;                                       /* save the status for a local label */

if (label & LABEL_EXTERNAL) {                                   /* if the label is non-local */
    cpu_setup_code_segment (label, &new_status, &cst_entry);    /*   then set up the corresponding code segment */

    stt_entry = STT_NUMBER (label);                     /* get the STT entry number from the label */

    if (cst_entry & (CST_A_BIT | CST_T_BIT)) {          /* if the code segment is absent or being traced */
        STA = new_status;                               /*   then set the new status before trapping */
        cpu_mark_stack ();                              /*     and write a stack marker to memory */

        if (cst_entry & CST_A_BIT)                      /* if the code segment is absent */
            MICRO_ABORTP (trap_CS_Absent, label);       /*   then trap to load it */
        else                                            /* otherwise */
            MICRO_ABORTP (trap_Trace, label);           /*   trap to trace it */
        }

    cpu_read_memory (program_checked, PL, &stt_size);   /* read the table size */

    if (stt_entry > STT_LENGTH (stt_size))              /* if the entry is outside of the table */
        MICRO_ABORTP (trap_STT_Violation, new_status);  /*   then trap for a violation */

    cpu_read_memory (program_checked, PL - stt_entry, &new_label);  /* read the label from the STT */

    if (new_label & LABEL_EXTERNAL)                     /* if the procedure is not in the target segment */
        MICRO_ABORTP (trap_STT_Violation, new_status);  /*   then trap for a violation */

    if ((new_label & LABEL_UNCALLABLE) && NPRV)         /* if the procedure is uncallable in the current mode */
        MICRO_ABORTP (trap_Uncallable, label);          /*   then trap for a violation */

    if (stt_entry == 0)                                 /* if the STT number is zero in an external label */
        label = 0;                                      /*   then the starting address is PB */
    else                                                /* otherwise */
        label = new_label;                              /*   the PB offset is contained in the new label */

    cpu_base_changed = TRUE;                            /* the program base registers have changed for tracing */
    }

new_p = PB + (label + offset & LABEL_ADDRESS_MASK);     /* get the procedure starting address */

cpu_read_memory (fetch_checked, new_p, &NIR);           /* check the bounds and get the next instruction */
P = new_p + 1 & R_MASK;                                 /* the bounds are valid, so set the new P value */

STA = new_status;                                       /* set the new status value */

return;
}


/* Return from a procedure.

   This routine sets up the P, Q, SM, and status registers to return from a
   procedure.  It is called by the EXIT and IXIT instruction executors and by
   the cpu_start_dispatcher routine to enter the dispatcher.  On entry, "new_q"
   and "new_sm" contain the new values for the Q and SM registers that unwind
   the stack.  The "parameter" value is used only if a Trace or Code Segment
   Absent trap is taken.  For EXIT, the parameter is the stack adjustment value
   (the N field).  For IXIT, the parameter is zero.  On exit, the registers are
   set up for execution to resume with the first instruction after the procedure
   call or interrupt.

   The routine begins by reloading register values from the stack marker.  The
   stack marker format is:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       X register value                        |  [Q - 3]  X
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | T | M |          PB-relative return address                   |  [Q - 2]  P + 1 - PB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     Status register value                     |  [Q - 1]  STA
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         Delta Q value                         |  [Q - 0]  S - Q
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     T = a trace or control-Y interrupt is pending
     M = the code segment is physically mapped

   The T and M bits are set by the operating system, if applicable, after the
   stack marker was originally written.

   Stack underflow and overflow are checked, and privilege changes are
   validated.  If the return will be to a different code segment, it is set up.
   Finally, the new P, Q, SM, and status register values are loaded, and NIR is
   loaded with the first instruction after the return.


   Implementation notes:

    1. This routine implements the microcode EXI1 subroutine.

    2. We pass a temporary status to cpu_setup_code_segment because it forms the
       returned new status from the current STA register value.  But for EXIT
       and IXIT, the new status comes from the stack marker, which already has
       the segment number to which we're returning, and which must not be
       altered.

    3. The NEXT action is modified when the R-bit is set in the status word
       being restored.  This occurs when an interrupt occurred between the two
       stackops of a stack instruction.  The main instruction loop does not
       alter CIR in this case, so we must set it up here.
*/

void cpu_exit_procedure (HP_WORD new_q, HP_WORD new_sm, HP_WORD parameter)
{
HP_WORD temp_status, new_status, new_p, cst_entry;

SM = Q;                                                 /* delete any local values from the stack */

if (new_q > Z || new_sm > Z)                            /* if either the new Q or SM exceed the stack limit */
    MICRO_ABORT (trap_Stack_Overflow);                  /*   then trap with a stack overflow */

cpu_read_memory (stack, Q - 1, &new_status);            /* read the new status value from the stack marker */

if ((CIR & EXIT_MASK) == EXIT                           /* if an EXIT instruction is executing */
  && (new_q < DB || new_sm < DB)                        /*   and either new Q or new S are below the data base */
  && (new_status & STATUS_M) == 0)                      /*     and the new mode is non-privileged */
    MICRO_ABORT (trap_Stack_Underflow);                 /*       then trap with a stack underflow */

cpu_read_memory (stack, Q - 2, &new_p);                 /* read the PB-relative return value from the stack marker */

if (NPRV                                                /* if currently in user mode */
  && ((new_status & STATUS_M)                           /*   and returning to privileged mode */
  || (new_status & STATUS_I) != (STA & STATUS_I)))      /*   or attempting to change interrupt state */
    MICRO_ABORT (trap_Privilege_Violation);             /*     then trap with a privilege violation */

STA &= ~STATUS_I;                                       /* turn off external interrupts */

cpu_read_memory (stack, Q - 3, &X);                     /* read the new X value from the stack marker */

if (STATUS_CS (new_status) != STATUS_CS (STA)) {                    /* if returning to a different segment */
    cpu_setup_code_segment (new_status, &temp_status, &cst_entry);  /*   then set up the new segment */

    if (NPRV && (temp_status & STATUS_M))               /* if in user mode now and returning to a privileged segment */
        MICRO_ABORT (trap_Privilege_Violation);         /*   then trap with a privilege violation */

    if (new_p & STMK_T)                                 /* if the new code segment is being traced */
        MICRO_ABORTP (trap_Trace, parameter);           /*   then trap to trace it */

    if (cst_entry & CST_A_BIT)                          /* if the code segment is absent */
        MICRO_ABORTP (trap_CS_Absent, parameter);       /*   then trap to load it */
    }

new_p = PB + (new_p & STMK_RTN_ADDR);                   /* convert the relative address to absolute */

cpu_read_memory (fetch_checked, new_p, &NIR);           /* check the bounds and get the next instruction */
P = new_p + 1 & R_MASK;                                 /* the bounds are valid, so set the new P value */

STA = new_status;                                       /* set the new status value */
Q   = new_q;                                            /*   and the stack marker */
SM  = new_sm;                                           /*     and the stack pointer */

if (STA & STATUS_R) {                                   /* if a right-hand stack op is pending */
    CIR = NIR;                                          /*   then set the current instruction */
    cpu_read_memory (fetch, P, &NIR);                   /*     and load the next instruction */
    }

cpu_base_changed = TRUE;                                /* one or more base registers have changed for tracing */

return;
}


/* Start the dispatcher.

   This routine is called by the DISP and PSEB instruction executors to start
   the dispatcher and by the IXIT executor to restart the dispatcher if it was
   interrupted.

   On entry, the ICS has been set up.  The "dispatcher running" bit in the CPX1
   register is set, Q is set to point at the permanent dispatcher stack marker
   on the ICS, the dispatcher's DBANK and DB registers are loaded, and an "exit
   procedure" is performed to return to the dispatcher.
*/

void cpu_start_dispatcher (void)
{
dprintf (cpu_dev, DEB_INSTR, BOV_FORMAT "%s interrupt\n",
         PBANK, P - 1 & R_MASK, 0, interrupt_name [irq_Dispatch]);

CPX1 |= cpx1_DISPFLAG;                                  /* set the "dispatcher is running" flag */

cpu_read_memory (absolute, ICS_Q, &Q);                  /* set Q to point to the dispatcher's stack marker */
cpu_write_memory (absolute, Q, 0);                      /*   and clear the stack marker delta Q value */

cpu_read_memory (stack, Q + 1 & LA_MASK, &DBANK);       /* load the dispatcher's data bank */
cpu_read_memory (stack, Q + 2 & LA_MASK, &DB);          /*   and data base registers */

cpu_exit_procedure (Q, Q + 2, 0);                       /* return to the dispatcher */

return;
}



/* CPU local SCP support routines */


/* Service the CPU process clock.

   The process clock is used by the operating system to time per-process CPU
   usage.  It is always enabled and running, although the PCLK register only
   increments if the CPU is not executing on the ICS.

   The process clock may be calibrated to wall-clock time or set to real time.
   In hardware, the process clock has a one-millisecond period.  Setting the
   mode to real time schedules clock events based on the number of event ticks
   equivalent to one millisecond.  Because the simulator is an order of
   magnitude faster than the hardware, this short period precludes idling.

   In the calibrated mode, the short period would still preclude idling.
   Therefore, in this mode, the clock is scheduled with a ten-millisecond
   service time, and the PCLK register is incremented by ten for each event
   service.  To present the correct value when PCLK is read, the
   "cpu_update_pclk" routine is called by the RCLK instruction executor to
   increment the count by an amount proportional to the fraction of the service
   interval that has elapsed.  In addition, that routine is called by the CPU
   instruction postlude, so that PCLK will have the correct value if it is
   examined from the SCP command prompt.

   The simulation console is normally hosted by, and therefore polled by, the
   ATC on channel 0.  If the console is not hosted by the ATC, due to a SET ATC
   DISABLED command, the process clock assumes polling control over the console.


   Implementation notes:

    1. If the process clock is calibrated, the system clock and ATC poll
       services are synchronized with the process clock service to improve
       idling.

    2. The current CPU speed, expressed as a multiple of the hardware speed, is
       calculated for each service entry.  It may be displayed at the SCP prompt
       with the SHOW CPU SPEED command.  The speed is only representative when
       the process clock is calibrated, and the CPU is not executing a PAUS
       instruction (which suspends the normal fetch/execute instruction cycle).
*/

static t_stat cpu_service (UNIT *uptr)
{
const t_bool ics_exec = (CPX1 & cpx1_ICSFLAG) != 0;     /* TRUE if the CPU is executing on the ICS */
t_stat status;

dprintf (cpu_dev, DEB_PSERV, "Process clock service entered on the %s\n",
         (ics_exec ? "ICS" : "user stack"));

if (!ics_exec)                                          /* if the CPU is not executing on the ICS */
    PCLK = PCLK + pclk_increment & R_MASK;              /*   then increment the process clock */

cpu_is_calibrated = (uptr->flags & UNIT_CALTIME) != 0;  /* TRUE if the process clock is calibrated */

if (cpu_is_calibrated) {                                /* if the process clock is tracking wall-clock time */
    uptr->wait = sim_rtcn_calb (PCLK_RATE, TMR_PCLK);   /*   then calibrate it */
    pclk_increment = PCLK_MULTIPLIER;                   /*     and set the increment to the multiplier */
    }

else {                                                  /* otherwise */
    uptr->wait = PCLK_PERIOD;                           /*   set the delay as an event tick count */
    pclk_increment = 1;                                 /*     and set the increment without multiplying */
    }

sim_activate (uptr, uptr->wait);                        /* reschedule the timer */

cpu_speed = uptr->wait / (PCLK_PERIOD * pclk_increment);    /* calculate the current CPU speed multiplier */

if (atc_is_polling == FALSE) {                          /* if the ATC is not polling for the simulation console */
    status = sim_poll_kbd ();                           /*   then we must poll for a console interrupt */

    if (status < SCPE_KFLAG)                            /* if the result is not a character */
        return status;                                  /*   then return the resulting status */
    }

return SCPE_OK;                                         /* return the success of the service */
}


/* Reset the CPU.

   This routine is called for a RESET, RESET CPU, or BOOT CPU command.  It is the
   simulation equivalent of the CPURESET signal, which is asserted by the front
   panel LOAD switch.  In hardware, this causes a microcode restart in addition
   to clearing certain registers.

   If this is the first call after simulator startup, the initial memory array
   is allocated, the default CPU and memory size configuration is set, and the
   SCP-required program counter pointer is set to point to the REG array element
   corresponding to the P register.

   If this is a power-on reset ("RESET -P"), the process clock calibrated timer
   is initialized, and any LOAD or DUMP request in progress is cleared.

   The micromachine is halted, the process clock is scheduled, and several
   registers are cleared.


   Implementation notes:

    1. Setting the sim_PC value at run time accommodates changes in the register
       order automatically.  A fixed setting runs the risk of it not being
       updated if a change in the register order is made.
*/

static t_stat cpu_reset (DEVICE *dptr)
{
if (sim_PC == NULL) {                                   /* if this is the first call after simulator start */

    hp_one_time_init();                                 /* perform one time initializations (previously defined as sim_vm_init() */

    if (mem_initialize (PA_MAX)) {
        set_model (&cpu_unit [0], UNIT_SERIES_III,      /*   so establish the initial CPU model */
                   NULL, NULL);

        for (sim_PC = dptr->registers;                      /* find the P register entry */
             sim_PC->loc != &P && sim_PC->loc != NULL;      /*   in the register array */
             sim_PC++);                                     /*     for the SCP interface */

        if (sim_PC == NULL)                                 /* if the P register entry is not present */
            return SCPE_NXREG;                              /*   then there is a serious problem! */
        }

    else                                                /* otherwise memory initialization failed */
        return SCPE_MEM;                                /*   so report the error and abort the simulation */
    }

if (sim_switches & SWMASK ('P')) {                      /* if this is a power-on reset */
    sim_rtcn_init (cpu_unit [0].wait, TMR_PCLK);        /*   then initialize the process clock timer */
    CPX2 &= ~(cpx2_LOADSWCH | cpx2_DUMPSWCH);           /*     and clear any cold load or dump request */
    }

cpu_micro_state = halted;                               /* halt the micromachine */
sim_activate_abs (&cpu_unit [0], cpu_unit [0].wait);    /*   and schedule the process clock */

PCLK = 0;                                               /* clear the process clock counter */
CPX1 = 0;                                               /*   and all run-mode signals */
CPX2 &= ~(cpx2_RUN | cpx2_SYSHALT);                     /*     and the run and system halt flip-flops */

if (cpu_unit [0].flags & UNIT_PFARS)                    /* if the PF/ARS switch position is ENBL */
    CPX2 &= ~cpx2_INHPFARS;                             /*   then clear the auto-restart inhibit flag */
else                                                    /* otherwise the position is DSBL */
    CPX2 |= cpx2_INHPFARS;                              /*   so set the auto-restart inhibit flag */

CNTR = SR;                                              /* copy the stack register to the counter */
cpu_flush ();                                           /*   and flush the TOS registers to memory */

return SCPE_OK;                                         /* indicate that the reset succeeded */
}


/* Set the CPU simulation stop conditions.

   This validation routine is called to configure the set of CPU stop
   conditions.  The "option" parameter is 0 to clear the stops and 1 to set
   them, and "cptr" points to the first character of the name of the stop to be
   cleared or set.  The unit and description pointers are not used.

   The routine processes commands of the form:

     SET CPU STOP
     SET CPU STOP=<stopname>[;<stopname>...]
     SET CPU NOSTOP
     SET CPU NOSTOP=<stopname>[;<stopname>...]

   The valid <stopname>s are contained in the debug table "cpu_stop".  If names
   are not specified, all stop conditions are enabled or disabled.


   Implementation notes:

    1. The CPU simulator maintains a private and a public set of simulator
       stops.  This routine sets the private set.  The private set is copied to
       the public set as part of the instruction execution prelude, unless the
       "-B" ("bypass") command-line switch is used with the run command.  This
       allows the stops to be bypassed conveniently for the first instruction
       execution only.
*/

static t_stat set_stops (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
char gbuf [CBUFSIZE];
uint32 stop;

if (cptr == NULL) {                                     /* if there are no arguments */
    sim_stops = 0;                                      /*   then clear all of the stop flags */

    if (option == 1)                                            /* if we're setting the stops */
        for (stop = 0; cpu_stop [stop].name != NULL; stop++)    /*   then loop through the flags */
            sim_stops |= cpu_stop [stop].mask;                  /*     and add each one to the set */
    }

else if (*cptr == '\0')                                 /* otherwise if the argument is empty */
    return SCPE_MISVAL;                                 /*   then report the missing value */

else                                                    /* otherwise at least one argument is present */
    while (*cptr) {                                     /* loop through the arguments */
        cptr = get_glyph (cptr, gbuf, ';');             /* get the next argument */

        for (stop = 0; cpu_stop [stop].name != NULL; stop++)    /* loop through the flags */
            if (strcmp (cpu_stop [stop].name, gbuf) == 0) {     /*   and if the argument matches */
                if (option == 1)                                /*     then if it's a STOP argument */
                    sim_stops |= cpu_stop [stop].mask;          /*       then add the stop flag */
                else                                            /*     otherwise it's a NOSTOP argument */
                    sim_stops &= ~cpu_stop [stop].mask;         /*       so remove the flag */

                break;                                          /* this argument has been processed */
                }

        if (cpu_stop [stop].name == NULL)               /* if the argument was not found */
            return SCPE_ARG;                            /*   then report it */
        }

return SCPE_OK;                                         /* the stops were successfully processed */
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


/* Set the CPU cold dump configuration jumpers.

   This validation routine is called to configure the set of jumpers on the
   system control panel that preset the device number and control value for the
   cold dump process.  The "option" parameter is 0 to set the device number
   and 1 to set the control value.  The "cptr" parameter points to the first
   character of the value to be set.  The unit and description pointers are not
   used.

   The routine processes commands of the form:

     SET CPU DUMPDEV=<devno>
     SET CPU DUMPCTL=<cntlval>

   The device number is a decimal value between 3 and 127, and the control value
   is an octal number between 0 and 377.  Values outside of these ranges are
   rejected.
*/

static t_stat set_dump (UNIT *uptr, int32 option, CONST char *cptr, void *desc)
{
t_value value;
t_stat  status = SCPE_OK;

if (cptr == NULL || *cptr == '\0')                      /* if the expected value is missing */
    status = SCPE_MISVAL;                               /*   then report the error */

else if (option == 0) {                                 /* otherwise if a device number is present */
    value = get_uint (cptr, DEVNO_BASE,                 /*   then parse the supplied value */
                      DEVNO_MAX, &status);

    if (status == SCPE_OK)                                  /* if it is valid */
        if (value >= 3)                                     /*   and in the proper range */
            dump_control = REPLACE_LOWER (dump_control,     /*     then set the new device number */
                                          (uint32) value);  /*       into the dump control word */
        else                                                /*   otherwise the device number */
            status = SCPE_ARG;                              /*     is invalid */
    }

else {                                                  /* otherwise a control byte is present */
    value = get_uint (cptr, CNTL_BASE,                  /*   so parse the supplied value */
                      CNTL_MAX, &status);

    if (status == SCPE_OK)                              /* if it is valid */
        dump_control = REPLACE_UPPER (dump_control,     /*   then set the new control value */
                                      (uint32) value);  /*     into the dump control word */
    }

return status;                                          /* return the operation status */
}


/* Change the CPU memory size.

   This validation routine is called to configure the CPU memory size.  The
   "new_size" parameter is set to the size desired and will be one of the
   discrete sizes supported by the machine.  The "uptr" parameter points to the
   CPU unit and is used to obtain the CPU model.  The other parameters are not
   used.

   The routine processes commands of the form:

     SET [-F] CPU <memsize>

   If the new memory size is larger than the supported size for the CPU model
   currently selected, the routine returns an error.  If the new size is smaller
   than the previous size, and if the area that would be lost contains non-zero
   data, the user is prompted to confirm that memory should be truncated.  If
   the user denies the request, the change is rejected.  Otherwise, the new size
   is set.  The user may omit the confirmation request and force truncation by
   specifying the "-F" switch on the command line.


   Implementation notes:

    1. The memory access routines return a zero value for locations beyond the
       currently defined memory size.  Therefore, the unused area need not be
       explicitly zeroed.
*/

static t_stat set_size (UNIT *uptr, int32 new_size, CONST char *cptr, void *desc)
{
static CONST char confirm [] = "Really truncate memory [N]?";

const uint32 model = CPU_MODEL (uptr->flags);           /* the current CPU model index */

if ((uint32) new_size > cpu_features [model].maxmem)    /* if the new memory size is not supported on current model */
    return SCPE_NOFNC;                                  /*   then report the error */

if (!(sim_switches & SWMASK ('F'))                      /* if truncation is not explicitly forced */
  && ! mem_is_empty (new_size)                          /*   and the truncated part is not empty */
  && get_yn (confirm, FALSE) == FALSE)                  /*     and the user denies confirmation */
    return SCPE_INCOMP;                                 /*       then abort the command */

MEMSIZE = new_size;                                     /* set the new memory size */

return SCPE_OK;                                         /* confirm that the change is OK */
}


/* Change the CPU model.

   This validation routine is called to configure the CPU model.  The
   "new_model" parameter is set to the model desired and will be one of the unit
   model flags.  The other parameters are not used.

   The routine processes commands of the form:

     SET [-F] CPU <model>

   Setting the model establishes a set of typical hardware features.  It also
   verifies that the current memory size is supported by the new model.  If it
   is not, the size is reduced to the maximum supported memory configuration.
   If the area that would be lost contains non-zero data, the user is prompted
   to confirm that memory should be truncated.  If the user denies the request,
   the change is rejected.  Otherwise, the new size is set.  The user may omit
   the confirmation request and force truncation by specifying the "-F" switch
   on the command line.

   This routine is also called once from the CPU reset routine to establish the
   initial CPU model.  The current memory size will be 0 when this call is made.
*/

static t_stat set_model (UNIT *uptr, int32 new_model, CONST char *cptr, void *desc)
{
const uint32 new_index = CPU_MODEL (new_model);         /* the new index into the CPU features table */
uint32 new_memsize;
t_stat status;

if (MEMSIZE == 0                                        /* if this is the initial establishing call */
  || MEMSIZE > cpu_features [new_index].maxmem)         /*   or if the current memory size is unsupported */
    new_memsize = cpu_features [new_index].maxmem;      /*     then set the new size to the maximum supported size */
else                                                    /* otherwise the current size is valid for the new model */
    new_memsize = (uint32) MEMSIZE;                     /*   so leave it unchanged */

status = set_size (uptr, new_memsize, NULL, NULL);      /* set the new memory size */

if (status == SCPE_OK)                                  /* if the change succeeded */
    uptr->flags = uptr->flags & ~UNIT_OPTS              /*   then set the typical features */
                    | cpu_features [new_index].typ;     /*     for the new model */

return status;                                          /* return the validation result */
}


/* Change a CPU option.

   This validation routine is called to configure the option set for the current
   CPU model.  The "new_option" parameter is set to the option desired and will
   be one of the unit option flags.  The "uptr" parameter points to the CPU unit
   and is used to obtain the CPU model.  The other parameters are not used.

   The routine processes commands of the form:

     SET CPU <option>[,<option>...]

   The option must be valid for the current CPU model, or the command is
   rejected.
*/

static t_stat set_option (UNIT *uptr, int32 new_option, CONST char *cptr, void *desc)
{
const uint32 model = CPU_MODEL (uptr->flags);           /* the current CPU model index */

if ((cpu_features [model].opt & new_option) != 0)       /* if the option is supported on the current model */
    return SCPE_OK;                                     /*   then confirm the change */
else                                                    /* otherwise */
    return SCPE_NOFNC;                                  /*   reject the change */
}


/* Change the power-fail auto-restart switch setting.

   This validation routine is called to configure the PF/ARS switch that is
   located behind the system control panel.  If set to the ENBL (enable)
   position, the CPU will perform an auto-restart when power is restored after a
   failure.  In the DSBL (disable) position, the CPU will remain halted after
   power restoration; execution may be continued by pressing the RUN button.

   In simulation, a SET CPU ARS command enables auto-restart, and SET CPU NOARS
   disables auto-restart.  The "setting" parameter is set to the UNIT_ARS flag
   in the former cast and to zero in the latter case.  The other parameters are
   not used.  The routine reflects the ARS setting in "inhibit auto-restart" bit
   of the CPX2 register.
*/

static t_stat set_pfars (UNIT *uptr, int32 setting, CONST char *cptr, void *desc)
{
if (setting == UNIT_PFARS)                              /* if the option is ARS */
    CPX2 &= ~cpx2_INHPFARS;                             /*   then clear the auto-restart inhibit flag */
else                                                    /* otherwise the option is NOARS */
    CPX2 |= cpx2_INHPFARS;                              /*   so set the auto-restart inhibit flag */

return SCPE_OK;                                         /* confirm the change */
}


/* Show the CPU simulation stop conditions.

   This display routine is called to show the set of CPU stop conditions.  The
   "st" parameter is the open output stream.  The other parameters are not used.

   If at least one stop condition is enabled, the routine searches through the
   stop table for flag bits that are set in the stop set.  For each one it
   finds, the routine prints the corresponding stop name.

   This routine services an extended modifier entry, so it must add the trailing
   newline to the output before returning.
*/

static t_stat show_stops (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
uint32 stop;
t_bool need_spacer = FALSE;

if (sim_stops == 0)                                     /* if no simulation stops are set */
    fputs ("Stops disabled", st);                       /*   then report that all are disabled */

else {                                                  /* otherwise at least one stop is valid */
    fputs ("Stop=", st);                                /*   so prepare to report the list of conditions */

    for (stop = 0; cpu_stop [stop].name != NULL; stop++)    /* loop through the set of stops in the table */
        if (cpu_stop [stop].mask & sim_stops) {             /* if the current stop is enabled */
            if (need_spacer)                                /*   then if a spacer is needed */
                fputc (';', st);                            /*     then add it first */

            fputs (cpu_stop [stop].name, st);               /* report the stop name */

            need_spacer = TRUE;                             /* a spacer will be needed next time */
            }
        }

fputc ('\n', st);                                       /* add the trailing newline */

return SCPE_OK;                                         /* report the success of the display */
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


/* Show the CPU cold dump configuration jumpers.

   This display routine is called to show the device number and control byte
   that are preset on the rear of the system control panel for the cold dump
   process.  The "st" parameter is the open output stream.  The other parameters
   are not used.
*/

static t_stat show_dump (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Dump device = %u, dump control = %03o\n",
         LOWER_BYTE (dump_control), UPPER_BYTE (dump_control));

return SCPE_OK;
}


/* Show the current CPU simulation speed.

   This display routine is called to show the current simulation speed.  The
   "st" parameter is the open output stream.  The other parameters are not used.

   The CPU speed, expressed as a multiple of the hardware speed, is calculated
   by the process clock service routine.  It is only representative when the
   process clock is calibrated, and the CPU is not executing a PAUS instruction
   (which suspends the normal fetch/execute instruction cycle).
*/

static t_stat show_speed (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, "Simulation speed = %ux\n", cpu_speed);    /* display the current CPU speed */
return SCPE_OK;                                         /*   and report success */
}



/* CPU local utility routines */


/* Process a halt-mode interrupt.

   This routine is called when one or more of the interrupt request bits are set
   in the CPX2 register.  These bits represent switch closures on the CPU front
   panel or on the optional maintenance display panel.  The specific switch
   closure is identified, and the corresponding microcode routine is executed.
   If multiple bits are set in CPX2, the microcode recognizes them in order from
   MSB to LSB.

   If the RUN switch is set, a test is made for a System Halt, which inhibits
   recognition of the switch.  If the System Halt flag is set, the CPU must be
   reset before execution may be resumed.  Otherwise, the NIR is reloaded in
   case P was changed during the simulation stop.  If the R-bit (right stack op
   pending) flag in the status register is set, but the NIR no longer contains a
   stack instruction, R is cleared.  If a stack instruction is present, and the
   R-bit is set, then the CIR is set, and the following instruction is fetched.
   The micromachine state is set to "running", and one event tick is added back
   to the accumulator to ensure that a single step won't complete without
   executing an instruction.

   If the DUMP switch is pressed, an I/O Reset is performed on all devices
   except the CPU, which is skipped to preserve the register state.  The dump
   device number is obtained from the SWCH register and tested to ensure that a
   tape is mounted with a write ring and the unit is online.  Then the contents
   of the CPU registers are written to the reserved memory area starting at
   address 1400 octal in bank 0.  This is followed by two SIO programs.  The
   main program at addresses 1430-1437 write 4K-word blocks of memory to the
   dump device.  The error recovery program at addresses 1422-1427 is invoked
   when a write fails.  It does a Backspace Record followed by a Write Gap to
   skip the bad spot on the tape, and then the write is retried.

   The DUMP switch remains set, so after each pass through the main execution
   loop to run a channel cycle, this routine is reentered.  The "waiting" state
   causes the second part of the process to check for device completion.  When
   it occurs, the expected external interrupt is cleared, and if the dump is
   complete, the DUMP switch is reset, the original SIO pointer in the DRT is
   restored, and the micromachine is halted.  Otherwise, the SIO pointer is
   read.  If it points at the end of the program, then the operation completed
   normally.  In this case, the dump address is advanced, and, if all of memory
   has been dumped, the SIO pointer is reset to the recovery program, and that
   program is changed to finish up with Write File Mark and Rewind/Offline
   commands.  Otherwise, the pointer is reset to the main program in preparation
   for the next 4K write.  The SIO program is then restarted.

   If SIO pointer failed to complete, the pointer is reset to point at the error
   recovery program, and the memory address is unchanged; the same 4K write will
   be performed once the recovery program runs.  If the recovery program fails,
   the dump is terminated at that point with a failure indication.

   During the dump operation, the CIR register is continually updated with the
   current memory bank number.  If the dump runs to completion, CIR will contain
   the number of 64K memory banks installed in the machine.  A value less than
   the installed memory value indicates a dump failure.  Except for the CIR, the
   machine state is restored, so that another dump may be attempted.

   If the LOAD switch is pressed, the cold load process begins by filling memory
   with HALT 10 instructions if SWCH register bit 8 is clear.  The cold load
   device number is obtained from the lower byte of the SWCH register.

   The first part of the cold load process clears the TOS and STA registers,
   stores the initial channel program in memory, and executes an SIO instruction
   to start the channel.  Once the device starts, interrupts are enabled, and
   the micromachine state is set to "waiting" in preparation for executing the
   second part of the cold load process once the channel program ends.  The
   routine then exits to begin channel execution.

   The LOAD switch remains set, so after each pass through the main execution
   loop to run a channel cycle, this routine is reentered.  The "waiting" state
   causes the second part of the process to check for device completion.  The
   expected external interrupt is cleared, the LOAD switch is cleared, the
   micromachine state is set to "running", and the Cold Load trap is taken to
   complete the process.


   Implementation notes:

    1. After processing the RUN switch and returning to the instruction loop, if
       no interrupt is present, and the R-bit in the status register is clear,
       then CIR will be set from NIR, and NIR will be reloaded.  If the R-bit is
       set, then CIR and NIR will not be changed, i.e., the NEXT action will be
       skipped, so we perform it here.  If an interrupt is pending, then the
       interrupt will be processed using the old value of the CIR, and the
       instruction in the NIR will become the first instruction executed after
       the interrupt handler completes.

    2. The cold load microcode is shared with the cold dump process.  The Series
       II dump process saves memory locations DRT + 0 through DRT + 3 in the TOS
       registers.  The load process uses the same microcode but does not perform
       the memory read, so the TOS registers are loaded with the previous
       contents of the OPND register, which is effectively a random value.  In
       simulation, the TOS registers are cleared.

    3. The cold load and dump microcode waits forever for an interrupt from the
       cold load device.  If it doesn't occur, the microcode hangs until a
       system reset is performed (it tests CPX1 bit 8 and does a JMP *-1 if the
       bit is not set).  The simulation follows the microcode behavior.

    4. Front panel diagnostics and direct I/O cold loading is not implemented.
*/

static t_stat halt_mode_interrupt (HP_WORD device_number)
{
static HP_WORD cold_device, sio_pointer, status, offset, pointer;
static uint32 address;
static t_bool error_recovery;

if (CPX2 & cpx2_RUNSWCH) {                              /* if the RUN switch is pressed */
    if (CPX2 & cpx2_SYSHALT) {                          /*   then if the System Halt flip-flop is set */
        CPX2 &= ~CPX2_IRQ_SET;                          /*     then clear all switches */
        return STOP_SYSHALT;                            /*       as the CPU cannot run until it is reset */
        }

    else                                                /*   otherwise */
        CPX2 = CPX2 & ~cpx2_RUNSWCH | cpx2_RUN;         /*     clear the switch and set the Run flip-flop */

    cpu_read_memory (fetch, P, &NIR);                   /* load the next instruction to execute */
    P = P + 1 & R_MASK;                                 /*   and point to the following instruction */

    if ((NIR & PAUS_MASK) == PAUS)                      /* if resuming into a PAUS instruction */
        cpu_stop_flags |= SS_PAUSE_RESUMED;             /*   then defer any interrupt until after PAUS is executed */

    if ((NIR & SUBOP_MASK) != 0)                        /* if the instruction is not a stack instruction */
        STA &= ~STATUS_R;                               /*   then clear the R-bit in case it had been set */

    else if (STA & STATUS_R) {                          /* otherwise if a right-hand stack op is pending */
        CIR = NIR;                                      /*   then set the current instruction */
        cpu_read_memory (fetch, P, &NIR);               /*     and load the next instruction */
        }

    cpu_micro_state = running;                          /* start the micromachine */
    sim_interval = sim_interval + 1;                    /* don't count this cycle against a STEP count */

    if (cpu_power_state == power_returning) {           /* if power is returning after a failure */
        if (CPX2 & cpx2_INHPFARS)                       /*   then if auto-restart is inhibited */
            CPX2 &= ~cpx2_RUN;                          /*     then clear the Run flip-flop */

        MICRO_ABORT (trap_Power_On);                    /* set up the trap to the power-on routine */
        }
    }


else if (CPX2 & cpx2_DUMPSWCH) {                        /* otherwise if the DUMP switch is pressed */
    if (cpu_micro_state != waiting) {                   /*   then if the dump is not in progress */
        reset_all (IO_RESET);                           /*     then reset all I/O devices */

        cold_device = LOWER_BYTE (SWCH) & DEVNO_MASK;   /* get the device number from the lower SWCH byte */

        status = iop_direct_io (cold_device, ioTIO, 0); /* get the device status */

        if ((status & MS_ST_MASK) != MS_ST_READY) {     /* if the tape is not ready and unprotected */
            CPX2 &= ~cpx2_DUMPSWCH;                     /*   then clear the dump switch */

            CIR = 0;                                    /* clear CIR to indicate a failure */
            return STOP_CDUMP;                          /*   and terminate the dump */
            }

        cpu_read_memory (absolute, cold_device * 4,     /* get the original DRT pointer */
                         &sio_pointer);

        cpu_write_memory (absolute, 01400, 1);              /* set the machine ID to 1 for the Series III */
        cpu_write_memory (absolute, 01401, sio_pointer);    /* store the original DRT pointer */
        cpu_write_memory (absolute, 01402, SM);             /* store the stack pointer */
        cpu_write_memory (absolute, 01403, 0);              /* store zeros for the scratch pad 1 */
        cpu_write_memory (absolute, 01404, 0);              /*   and scratch pad 2 register values */
        cpu_write_memory (absolute, 01405, DB);             /* store the data base */
        cpu_write_memory (absolute, 01406, DBANK << 12      /* store DBANK in 0:4 */
                                             | PBANK << 8   /*   and PBANK in 4:4 */
                                             | SBANK);      /*     and SBANK in 12:4 */
        cpu_write_memory (absolute, 01407, Z);              /* store the stack limit */
        cpu_write_memory (absolute, 01410, DL);             /*   and the data limit */
        cpu_write_memory (absolute, 01411, X);              /*   and the index register */
        cpu_write_memory (absolute, 01412, Q);              /*   and the frame pointer */
        cpu_write_memory (absolute, 01413, CIR);            /*   and the current instruction */
        cpu_write_memory (absolute, 01414, PB);             /*   and the program base */
        cpu_write_memory (absolute, 01415, PL);             /*   and the program limit */
        cpu_write_memory (absolute, 01416, P);              /*   and the program counter */
        cpu_write_memory (absolute, 01417, CPX1);           /* store the CPX1 register */
        cpu_write_memory (absolute, 01420, STA);            /*   and the status register */
        cpu_write_memory (absolute, 01421,                  /* store the lower byte of the CPX2 register */
                          LOWER_WORD (CPX2 << 8             /*   in the upper byte of memory */
                            | MEMSIZE / 65536));            /*     and the memory bank count in the lower byte */

        cpu_write_memory (absolute, 01422, SIO_CNTL);           /* CONTRL 0,BSR */
        cpu_write_memory (absolute, 01423, MS_CN_BSR);
        cpu_write_memory (absolute, 01424, SIO_CNTL);           /* CONTRL 0,GAP */
        cpu_write_memory (absolute, 01425, MS_CN_GAP);
        cpu_write_memory (absolute, 01426, SIO_JUMP);           /* JUMP   001436 */
        cpu_write_memory (absolute, 01427, 001436);

        cpu_write_memory (absolute, 01430, SIO_SBANK);          /* SETBNK 0 */
        cpu_write_memory (absolute, 01431, 000000);
        cpu_write_memory (absolute, 01432, SIO_CNTL);           /* CONTRL 0,<SWCH-upper> */
        cpu_write_memory (absolute, 01433, UPPER_BYTE (SWCH));
        cpu_write_memory (absolute, 01434, SIO_WRITE);          /* WRITE  #4096,000000 */
        cpu_write_memory (absolute, 01435, 000000);
        cpu_write_memory (absolute, 01436, SIO_ENDIN);          /* ENDINT */
        cpu_write_memory (absolute, 01437, 000000);

        address = 0;                                    /* clear the address */
        offset = 0;                                     /*   and memory offset counters */

        CIR = 0;                                        /* clear the memory bank counter */

        cpu_write_memory (absolute, cold_device * 4, 01430);    /* point the DRT at the cold dump program */
        error_recovery = FALSE;

        iop_direct_io (cold_device, ioSIO, 0);          /* start the device */

        if (CPX1 & cpx1_IOTIMER)                        /* if the device did not respond */
            MICRO_ABORT (trap_SysHalt_IO_Timeout);      /*   then a System Halt occurs */

        else {                                          /* otherwise the device has started */
            status = STA;                               /*   so save the original status register value */

            STA = STATUS_I | STATUS_O;                  /* enable interrupts and set overflow */
            cpu_micro_state = waiting;                  /*   and set the load-in-progress state */
            }
        }

    else if (CPX1 & cpx1_EXTINTR) {                     /* otherwise if an external interrupt is pending */
        CPX1 &= ~cpx1_EXTINTR;                          /*   then clear it */

        iop_direct_io (device_number, ioRIN, 0);        /* reset the device interrupt */

        if (device_number == cold_device)               /* if the expected device interrupted */
            if (address >= MEMSIZE) {                   /*   then if all of memory has been dumped */
                CPX2 &= ~cpx2_DUMPSWCH;                 /*   then reset the DUMP switch */

                STA = status;                           /* restore the original status register value */

                cpu_write_memory (absolute,             /* restore the */
                                  cold_device * 4,      /*   original SIO pointer */
                                  sio_pointer);         /*     to the DRT */

                cpu_micro_state = halted;               /* clear the dump-in-progress state */
                return STOP_CDUMP;                      /*   and report dump completion */
                }

            else {                                      /* otherwise the dump continues */
                cpu_read_memory (absolute,              /* read the */
                                 cold_device * 4,       /*   current SIO pointer address */
                                 &pointer);             /*     from the DRT */

                if (pointer == 01440) {                 /* if the SIO program completed normally */
                    cpu_write_memory (absolute,         /*   then reset the pointer */
                                      cold_device * 4,  /*     to the start */
                                      001430);          /*       of the program */

                    if (error_recovery)                 /* if this was a successful error recovery */
                        error_recovery = FALSE;         /*   then clear the flag and keep the current address */

                    else {                                  /* otherwise this was a successful write */
                        address = address + 4096;           /*   so bump the memory address */
                        offset = offset + 4096 & LA_MASK;   /*     and offset to the next 4K block */

                        cpu_write_memory (absolute,         /* store the new write buffer address */
                                          001435, offset);

                        if (offset == 0) {                  /* if the offset wrapped around */
                            CIR = CIR + 1;                  /*   then increment the bank number */
                            cpu_write_memory (absolute,     /*     and store it as the SET BANK target */
                                              001431, CIR);

                            if (address >= MEMSIZE) {               /* if all of memory has been dumped */
                                cpu_write_memory (absolute, 001423, /*   then change the error recovery program */
                                                  MS_CN_WFM);       /*     to write a file mark */
                                cpu_write_memory (absolute, 001425, /*       followed by */
                                                  MS_CN_RST);       /*         a rewind/offline request */

                                cpu_write_memory (absolute,         /* point at the recovery program */
                                                  cold_device * 4,
                                                  001422);
                                }
                            }
                        }
                    }

                else if (error_recovery) {              /* otherwise if the recover program failed */
                    CPX2 &= ~cpx2_DUMPSWCH;             /*   then reset the DUMP switch */

                    STA = status;                       /* restore the original status register value */

                    cpu_write_memory (absolute,         /* restore the */
                                      cold_device * 4,  /*   original SIO pointer */
                                      sio_pointer);     /*     to the DRT */

                    cpu_micro_state = halted;           /* clear the dump-in-progress state */
                    return STOP_CDUMP;                  /*   and report dump failure */
                    }

                else {                                  /* otherwise attempt error recovery */
                    cpu_write_memory (absolute,         /*   by setting the SIO pointer */
                                      cold_device * 4,  /*     to the backspace/write gap */
                                      001422);          /*       program */

                    error_recovery = TRUE;              /* indicate that recovery is in progress */
                    }

                iop_direct_io (cold_device, ioSIO, 0);  /* start the device */
                }
        }                                               /* otherwise wait for the cold dump device to interrupt */
    }

else if (CPX2 & cpx2_LOADSWCH)                          /* otherwise if the LOAD switch is pressed */
    if (cpu_micro_state != waiting) {                   /*   then if the load is not in progress */
        reset_all (CPU_IO_RESET);                       /*     then reset the CPU and all I/O devices */

        if ((SWCH & 000200) == 0)                       /* if switch register bit 8 is clear */
            mem_fill (0, HALT_10);                      /*   then fill all of memory with HALT 10 instructions */

        SBANK = 0;                                      /* set the stack bank to bank 0 */

        cold_device = LOWER_BYTE (SWCH) & DEVNO_MASK;   /* get the device number from the lower SWCH byte */

        if (cold_device < 3) {                          /* if the device number is between 0 and 2 */
            CPX2 &= ~cpx2_LOADSWCH;                     /*   then reset the LOAD switch */
            return SCPE_INCOMP;                         /*     and execute a front panel diagnostic */
            }

        else if (cold_device > 63) {                    /* otherwise if the device number is > 63 */
            CPX2 &= ~cpx2_LOADSWCH;                     /*   then reset the LOAD switch */
            return SCPE_INCOMP;                         /*     and execute a direct I/O cold load */
            }

        else {                                          /* otherwise the device number is in the channel I/O range */
            RA = 0;                                     /* set the */
            RB = 0;                                     /*   TOS registers */
            RC = 0;                                     /*     to the same */
            RD = 0;                                     /*       (random) value */

            SR = 4;                                     /* mark the TOS registers as valid */
            STA = 0;                                    /*   and clear the status register */

            cpu_write_memory (absolute, 01430, SIO_SBANK);          /* SETBNK 0 */
            cpu_write_memory (absolute, 01431, 000000);
            cpu_write_memory (absolute, 01432, SIO_CNTL);           /* CONTRL 0,<SWCH-upper> */
            cpu_write_memory (absolute, 01433, UPPER_BYTE (SWCH));
            cpu_write_memory (absolute, 01434, SIO_READ);           /* READ   #16,001400 */
            cpu_write_memory (absolute, 01435, 001400);
            cpu_write_memory (absolute, 01436, SIO_JUMP);           /* JUMP   001400 */
            cpu_write_memory (absolute, 01437, 001400);

            cpu_write_memory (absolute, cold_device * 4, 01430);    /* point the DRT to the cold load program */

            iop_direct_io (cold_device, ioSIO, 0);      /* start the device */

            if (CPX1 & cpx1_IOTIMER)                    /* if the device did not respond */
                MICRO_ABORT (trap_SysHalt_IO_Timeout);  /*   then a System Halt occurs */

            else {                                      /* otherwise the device has started */
                STA = STATUS_I | STATUS_O;              /*   so enable interrupts and set overflow */
                cpu_micro_state = waiting;              /*     and set the load-in-progress state */
                }
            }
        }

    else                                                /* otherwise the load is in progress */
        if (CPX1 & cpx1_EXTINTR) {                      /* if an external interrupt is pending */
            CPX1 &= ~cpx1_EXTINTR;                      /*   then clear it */

            iop_direct_io (device_number, ioRIN, 0);    /* reset the device interrupt */

            if (device_number == cold_device) {         /* if the expected device interrupted */
                CPX2 &= ~cpx2_LOADSWCH;                 /*   then reset the LOAD switch */

                cpu_micro_state = running;              /* clear the load-in-progress state */
                MICRO_ABORT (trap_Cold_Load);           /*   and execute the cold load trap handler */
                }
            }                                           /* otherwise wait for the cold load device to interrupt */

return SCPE_OK;
}


/* Execute one machine instruction.

   This routine executes the CPU instruction present in the CIR.  The CPU state
   (registers, memory, interrupt status) is modified as necessary, and the
   routine return SCPE_OK if the instruction executed successfully.  Any other
   status indicates that execution should cease, and control should return to
   the simulator console.  For example, a programmed HALT instruction returns
   STOP_HALT status.

   Unimplemented instructions are detected by those decoding branches that
   result from bit patterns not corresponding to legal instructions or
   corresponding to optional instructions not currently enabled.  Normally,
   execution of an unimplemented instruction would result in an Unimplemented
   Instruction trap.  However, for debugging purposes, a simulator stop may be
   requested instead by returning STOP_UNIMPL status if the SS_UNIMPL simulation
   stop flag is set.

   This routine implements the main instruction dispatcher, as well as memory
   address instructions (subopcodes 04-17).  Instructions corresponding to
   subopcodes 00-03 are executed by routines in the base instruction set module.


   Implementation notes:

    1. Each instruction executor begins with a comment listing the instruction
       mnemonic and, following in parentheses, the condition code setting, or
       "none" if the condition code is not altered, and a list of any traps that
       might be generated.

    2. Stack preadjusts are simpler if performed explicitly due to overlapping
       requirements that reduce the number of static preadjusts to 4 of the 16
       entries.

    3. The order of operations for each instruction follows the microcode.  For
       example, the LOAD instruction performs the bounds check on the effective
       address and reads the operand before pushing the stack down and storing
       it in the RA registrer.  Pushing the stack down first and then reading
       the value directly into the RA register would leave the stack in a
       different state if the memory access caused a Bounds Violation trap.

    4. The TBA, MTBA, TBX, and MTBX instructions take longer to execute than the
       nominal 2.5 microseconds assumed for the average instruction execution
       time.  Consequently, the 7905 disc diagnostic fails Step 66 (the retry
       counter test) if the DS device is set for REALTIME operation.  The
       diagnostic uses the MTBA P+0 instruction in a timing loop, which expires
       before the disc operations complete.

       A workaround for this diagnostic is to decrement sim_interval twice for
       these instructions.  However, doing so causes the 7970 tape diagnostic to
       fail Step 532 (the timing error test) for the opposite reason: a wait
       loop of MTBA P+0 instructions causes the tape data transfer service event
       time to count down twice as fast, while the multiplexer channel data
       transfer polls occur at the usual one per instruction.  This could be
       remedied by having the channel polls execute twice as many I/O cycles for
       these instructions, although the general solution would be to recast
       sim_intervals as microseconds and to decrement sim_interval by differing
       amounts appropriate for each instruction.
*/

static t_stat machine_instruction (void)
{
HP_WORD       displacement, opcode, offset, operand, operand_1, operand_2, result;
int32         control, limit;
ACCESS_CLASS  class;
BYTE_SELECTOR selector;
t_bool        branch;
t_stat        status = SCPE_OK;

switch (SUBOP (CIR)) {                                  /* dispatch on bits 0-3 of the instruction */

    case 000:                                           /* stack operations */
        status = cpu_stack_op ();                       /* set the status from the instruction executor */
        break;


    case 001:                                           /* shift, branch, and bit test operations */
        status = cpu_shift_branch_bit_op ();            /* set the status from the instruction executor */
        break;


    case 002:                                           /* move, special, firmware, immediate, field, and register operations */
        status = cpu_move_spec_fw_imm_field_reg_op ();  /* set the status from the instruction executor */
        break;


    case 003:                                           /* I/O, control, program, immediate, and memory operations */
        status = cpu_io_cntl_prog_imm_mem_op ();        /* set the status from the instruction executor */
        break;


    case 004:                                           /* LOAD (CCA; STOV, BNDV) */
        cpu_ea (CIR, &class, &offset, NULL);            /* get the effective address */
        cpu_read_memory (class, offset, &operand);      /*   and read the operand */

        cpu_push ();                                    /* push the operand */
        RA = operand;                                   /*   onto the stack */

        SET_CCA (RA, 0);                                /* set the condition code */
        break;


    case 005:                                           /* TBA, MTBA, TBX, MTBX, and STOR */
        if (CIR & M_FLAG) {                             /* STOR (none; STUN, BNDV) */
            cpu_ea (CIR, &class, &offset, NULL);        /* get the effective address */

            PREADJUST_SR (1);                           /* ensure that at least one TOS register is loaded */

            cpu_write_memory (class, offset, RA);       /* write the TOS to memory */
            cpu_pop ();                                 /*   and pop the stack */
            }

        else {                                          /* TBA, MTBA, TBX, or MTBX */
            opcode = CIR & TBR_MASK;                    /* get the test and branch operation */

            if (opcode == TBA || opcode == MTBA) {      /* TBA or MTBA (none; STUN, STOV, BNDV) */
                PREADJUST_SR (3);                       /* ensure that at least three TOS registers are loaded */

                while (SR > 3)                          /* if more than three TOS register are loaded */
                    cpu_queue_down ();                  /*   queue them down until exactly three are left */

                offset = DB + RC & LA_MASK;             /* get the address of the control value */

                if (DL <= offset && offset <= SM || PRIV)       /* if the address is within the segment */
                    cpu_read_memory (data, offset, &operand);   /*   then read the value */

                else                                            /* otherwise */
                    MICRO_ABORT (trap_Bounds_Violation);        /*   trap with a bounds violation if not privileged */

                if (opcode == MTBA) {                           /* if the instruction is MTBA */
                    operand = operand + RB & DV_MASK;           /*   then add the step size */
                    cpu_write_memory (data, offset, operand);   /*     to the control variable */
                    }

                control = SEXT16 (operand);             /* sign-extend the control value */
                }

            else {                                      /* TBX or MTBX (none; STUN, BNDV) */
                PREADJUST_SR (2);                       /* ensure that at least two TOS registers are loaded */

                if (opcode == MTBX)                     /* if the instruction is MTBX */
                    X = X + RB & R_MASK;                /*   then add the step size to the control variable */

                control = SEXT16 (X);                   /* sign-extend the control value */
                }

            limit = SEXT16 (RA);                        /* sign-extend the limit value */

            if (RB & D16_SIGN)                          /* if the step size is negative */
                branch = control >= limit;              /*   then branch if the value is not below the limit */
            else                                        /* otherwise */
                branch = control <= limit;              /*   branch if the value is not above the limit */

            if (branch) {                                       /* if the test succeeded */
                displacement = CIR & DISPL_255_MASK;            /*   then get the branch displacement */

                if (CIR & DISPL_255_SIGN)                       /* if the displacement is negative */
                    offset = P - 2 - displacement & LA_MASK;    /*   then subtract the displacement from the base */
                else                                            /* otherwise */
                    offset = P - 2 + displacement & LA_MASK;    /*   add the displacement to the base */

                if (cpu_stop_flags & SS_LOOP                    /* if the infinite loop stop is active */
                  && displacement == 0                          /*   and the target is the current instruction */
                  && (opcode == TBA || opcode == TBX))          /*     and the instruction must be checked */
                    status = STOP_INFLOOP;                      /*       then stop the simulator */
                else                                            /* otherwise */
                    status = SCPE_OK;                           /*   continue */

                cpu_read_memory (fetch_checked, offset, &NIR);  /* load the next instruction register */
                P = offset + 1 & R_MASK;                        /*   and increment the program counter */
                }

            else {                                      /* otherwise the test failed */
                cpu_pop ();                             /*   so pop the limit */
                cpu_pop ();                             /*     and the step size from the stack */

                if (opcode == TBA || opcode == MTBA)    /* if the instruction is TBA or MTBA */
                    cpu_pop ();                         /*   then pop the variable address too */
                }                                       /*     and continue execution at P + 1 */
            }
        break;


    case 006:                                           /* CMPM (CCC; STUN) */
        cpu_ea (CIR, &class, &offset, NULL);            /* get the effective address */
        cpu_read_memory (class, offset, &operand);      /*   and read the operand */

        PREADJUST_SR (1);                               /* ensure that at least one TOS register is loaded */

        SET_CCC (RA, 0, operand, 0);                    /* set the condition code from the TOS value */
        cpu_pop ();                                     /*   and then pop the value from the stack */
        break;


    case 007:                                           /* ADDM (CCA, C, O; STUN, BNDV, ARITH) */
        cpu_ea (CIR, &class, &offset, NULL);            /* get the effective address */
        cpu_read_memory (class, offset, &operand);      /*   and read the operand */

        PREADJUST_SR (1);                               /* ensure that at least one TOS register is loaded */

        RA = cpu_add_16 (RA, operand);                  /* add the operands */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 010:                                           /* SUBM (CCA, C, O; STUN, BNDV, ARITH) */
        cpu_ea (CIR, &class, &offset, NULL);            /* get the effective address */
        cpu_read_memory (class, offset, &operand);      /*   and read the operand */

        PREADJUST_SR (1);                               /* ensure that at least one TOS register is loaded */

        RA = cpu_sub_16 (RA, operand);                  /* subtract the operands */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 011:                                           /* MPYM (CCA, O; STUN, STOV, BNDV, ARITH) */
        cpu_ea (CIR, &class, &offset, NULL);            /* get the effective address */
        cpu_read_memory (class, offset, &operand);      /*   and read the operand */

        PREADJUST_SR (1);                               /* ensure that at least one TOS register is loaded */

        RA = cpu_mpy_16 (RA, operand);                  /* multiply the operands */
        SET_CCA (RA, 0);                                /*   and set the condition code */
        break;


    case 012:                                           /* INCM, DECM */
        cpu_ea (CIR | M_FLAG, &class, &offset, NULL);   /* get the effective address (forced to data-relative) */
        cpu_read_memory (class, offset, &operand);      /*   and read the operand */

        if (CIR & M_FLAG)                               /* DECM (CCA, C, O; BNDV, ARITH) */
            result = cpu_sub_16 (operand, 1);           /* decrement the operand and set C and O as necessary */
        else                                            /* INCM (CCA, C, O; BNDV, ARITH) */
            result = cpu_add_16 (operand, 1);           /* increment the operand and set C and O as necessary */

        cpu_write_memory (class, offset, result);       /* write the operand back to memory */
        SET_CCA (result, 0);                            /*   and set the condition code */
        break;


    case 013:                                           /* LDX (CCA; BNDV) */
        cpu_ea (CIR, &class, &offset, NULL);            /* get the effective address */
        cpu_read_memory (class, offset, &X);            /*   and read the operand into the X register */

        SET_CCA (X, 0);                                 /* set the condition code */
        break;


    case 014:                                           /* BR (none; BNDV), BCC (none; BNDV) */
        if ((CIR & BR_MASK) != BCC) {                   /* if the instruction is BR */
            cpu_ea (CIR, &class, &offset, NULL);        /*   then get the effective address of the branch */

            if (cpu_stop_flags & SS_LOOP                /* if the infinite loop stop is active */
              && offset == (P - 2 & LA_MASK))           /*   and the target is the current instruction */
                status = STOP_INFLOOP;                  /*     then stop the simulator */
            else                                        /* otherwise */
                status = SCPE_OK;                       /*   continue */

            cpu_read_memory (fetch_checked, offset, &NIR);  /* load the next instruction register */
            P = offset + 1 & R_MASK;                        /*   and increment the program counter */
            }

        else if (TO_CCF (STA) & CIR << BCC_CCF_SHIFT)   /* otherwise if the BCC test succeeds */
            status = cpu_branch_short (TRUE);           /*   then branch to the target address */
        break;                                          /* otherwise continue execution at P + 1 */


    case 015:                                           /* LDD (CCA; STOV, BNDV), LDB (CCB; STOV, BNDV) */
        if (CIR & M_FLAG) {                             /* if the instruction is LDD */
            cpu_ea (CIR, &class, &offset, NULL);        /*   then get the effective address of the double-word */

            cpu_read_memory (class, offset, &operand_1);                /* read the MSW */
            cpu_read_memory (class, offset + 1 & LA_MASK, &operand_2);  /*   and the LSW of the operand */

            cpu_push ();                                /* push the MSW  */
            cpu_push ();                                /*   and the LSW  */
            RB = operand_1;                             /*     of the operand */
            RA = operand_2;                             /*       onto the stack */

            SET_CCA (RB, RA);                           /* set the condition code */
            }

        else {                                                  /* otherwise the instruction is LDB */
            cpu_ea (CIR | M_FLAG, &class,  &offset, &selector); /*   so get the effective word address of the byte */
            cpu_read_memory (class, offset, &operand);          /*     and read the operand */

            cpu_push ();                                /* push the stack down */

            if (selector == upper)                      /* if the upper byte is selected */
                RA = UPPER_BYTE (operand);              /*   then store it in the TOS */
            else                                        /* otherwise */
                RA = LOWER_BYTE (operand);              /*   store the lower byte in the TOS */

            SET_CCB (RA);                               /* set the condition code */
            }
        break;


    case 016:                                           /* STD (none; STUN, BNDV), STB (none; STUN, BNDV) */
        if (CIR & M_FLAG) {                             /* if the instruction is STD */
            cpu_ea (CIR, &class, &offset, NULL);        /*   then get the effective address of the double-word */

            PREADJUST_SR (2);                           /* ensure that at least two TOS registers are loaded */

            cpu_write_memory (class, offset + 1 & LA_MASK, RA); /* write the LSW first to follow the microcode */
            cpu_write_memory (class, offset, RB);               /*   and then write the MSW */

            cpu_pop ();                                 /* pop the TOS */
            cpu_pop ();                                 /*   and the NOS */
            }

        else {                                                  /* otherwise the instruction is STB */
            cpu_ea (CIR | M_FLAG, &class, &offset, &selector);  /*   so get the effective word address of the byte */

            PREADJUST_SR (1);                           /* ensure that at least one TOS register is loaded */

            cpu_read_memory (class, offset, &operand);  /* read the word containing the target byte */

            if (selector == upper)                      /* if the upper byte is targeted */
                operand = REPLACE_UPPER (operand, RA);  /*   then store the TOS into it */
            else                                        /* otherwise */
                operand = REPLACE_LOWER (operand, RA);  /*   store the TOS into the lower byte */

            cpu_write_memory (class, offset, operand);  /* write the word back */
            cpu_pop ();                                 /*   and pop the TOS */
            }
        break;


    case 017:                                           /* LRA (none; STOV, BNDV) */
        cpu_ea (CIR, &class, &offset, NULL);            /* get the effective address */

        if (class == program_checked)                   /* if this a program reference */
            offset = offset - PB;                       /*   then subtract PB to get the relative address */
        else                                            /* otherwise it is a data or stack reference */
            offset = offset - DB;                       /*   so subtract DB to get the address */

        cpu_push ();                                    /* push the relative address */
        RA = offset & R_MASK;                           /*   onto the stack */
        break;

    }                                                   /* all cases are handled */

if (status == STOP_UNIMPL                               /* if the instruction is unimplemented */
  && (cpu_stop_flags & SS_UNIMPL) == 0)                 /*   and the unimplemented instruction stop is inactive */
    MICRO_ABORT (trap_Unimplemented);                   /*     then trap to handle it */

return status;
}
