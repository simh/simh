/* hp2100_cpu5.c: HP 1000 EMA, VIS, and SIGNAL microcode simulator

   Copyright (c) 2007-2008, Holger Veit
   Copyright (c) 2006-2019, J. David Bryan

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   CPU5         Extended Memory Array, Vector Instruction Set, and SIGNAL/1000
                instructions

   23-Jan-19    JDB     Moved fmt_ab from hp2100_cpu1.c
   02-Aug-18    JDB     Moved VMA dispatcher to hp2100_cpu7.c
                        Moved VIS dispatcher from hp2100_cpu7.c
   30-Jul-18    JDB     Renamed "dms_[rw]map" to "meu_read_map", "meu_write_map"
   07-Sep-17    JDB     Replaced "uint16" casts with "HP_WORD" for A/B assignments
   15-Jul-17    JDB     Replaced "vma_resolve" with "resolve"
   26-Jun-17    JDB     Replaced SEXT with SEXT16
   06-Jun-17    HV      Fixed bug in cpu_vma_lbp "last suit + 1" handler
   31-Jan-17    JDB     Revised to use tprintf and TRACE_OPND for debugging
   26-Jan-17    JDB     Removed debug parameters from ema_* routines
   24-Jan-17    JDB     Replaced ReadIO, WriteIO with ReadS/U, WriteS/U
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   17-Dec-12    JDB     Fixed cpu_vma_mapte to return FALSE if not a VMA program
   09-May-12    JDB     Separated assignments from conditional expressions
   23-Mar-12    JDB     Added sign extension for dim count in "ema_resolve"
   28-Dec-11    JDB     Eliminated unused variable in "vis_vset"
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   30-Jul-08    JDB     Redefined ABORT to pass address, moved def to hp2100_cpu.h
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   01-May-08    HV      Fixed mapping bug in "ema_emap"
   21-Apr-08    JDB     Added EMA support from Holger
   25-Nov-07    JDB     Added TF fix from Holger
   07-Nov-07    HV      VMACK diagnostic tests 1...32 passed
   19-Oct-07    JDB     Corrected $LOC operand profile to OP_CCCACC
   03-Oct-07    HV      Moved RTE-6/VM instrs from hp2100_cpu0.c
   26-Sep-06    JDB     Created

   Primary references:
     - HP 1000 M/E/F-Series Computers Technical Reference Handbook
         (5955-0282, March 1980)
     - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
         (92851-90001, March 1981)
     - Macro/1000 Reference Manual
         (92059-90001, December 1992)


   The RTE-IV and RTE-IVB Extended Memory Array instructions and the RTE-6/VM
   Virtual Memory Area instructions were added to accelerate the logical-to-
   physical address translations and array subscript calculations of programs
   running under the RTE-IV (HP product number 92067A), RTE-IVB (92068A), and
   RTE-6/VM (92084A) operating systems.  Microcode was available for the E- and
   F-Series; the M-Series used software equivalents.

   Both EMA and VMA opcodes reside in the range 105240-105257, so only one or
   the other could be installed in a given system.  This did not present a
   difficulty, as VMA was a superset of EMA.  The EMA encodings are:

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 0   0   0 |  .EMIO
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      buffer size address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      array table address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    last subscript address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                              ...                              :
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    first subscript address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                   return location if error                    :  P+n
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :              return location if buffer is mapped              :  P+n+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .EMIO instruction maps a buffer of the indicated size and starting at the
   indicated array location into memory.  It ensures that the buffer is entirely
   within the logical address space in preparation for an I/O operation.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 0   0   1 |  MMAP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |  relative page count from EMA start to segment start address  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      page count address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The MMAP instruction maps a sequence of physical memory pages into the
   mapping segment area of a program's logical address space.  The A-register
   value on return indicates the success or failure of the request.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   0 | 0   1   0 |  emtst
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The emtst instruction is used to determine if the EMA firmware has been
   installed.  If it is executed in single-step mode, it sets S to 102077 (HLT
   77B).  It executes as NOP from a running program.


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   0 | 1   0   1 | 1   1   1 |  .EMAP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         array address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      array table address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    last subscript address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                              ...                              :
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    first subscript address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                   return location if error                    :  P+n
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if page is mapped               :  P+n+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .EMAP instruction resolves an array access into the memory address of the
   referenced element.  If the array is in EMA, it also maps the element into
   the mapping segment.



   The Vector Instruction Set (VIS) provides instructions that operate on
   one-dimensional arrays of floating-point values.  Both single- and
   double-precision operations are supported.  VIS uses the F-Series
   floating-point processor to handle the floating-point math, so the firmware
   is supported only on that machine.

   Instructions use IR bit 11 to select single- or double-precision format.  The
   double-precision instruction names begin with "D" (e.g., DVADD vs. VADD).
   Most VIS instructions are two words in length, with a sub-opcode immediately
   following the primary opcode.

   The two-word instructions are interruptible.  the firmware sets bit 15 of the
   second word to 1 to indicate that the instruction has been interrupted.  This
   allows the instruction to resume at the correct point in the vector
   operation.  Bit 15 is set to 0 before exiting for instruction completion.

   The .ESEG instruction behaves slightly differently when invoked with the
   105475 opcode.  The microcode source calls it a .VPRG instruction, but the
   only difference is that it sets the MSEG start and size to 0 and 32,
   respectively, instead of obtaining them from the ID extension.  In all other
   respects, the instructions are identical.

   The .ERES, .VSET, and test instructions do not test bit 11, so they will be
   invoked with either the 101xxx or 105xxx forms.  The 101xxx forms are
   canonical for the first two, while the 105xxx form is canonical for the
   self-test instruction.

   The VIS encodings are:

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VADD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 0   0   0 | 0   0   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 3 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 3 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VSUB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 0   0   0 | 0   1   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 3 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 3 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VMPY
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 0   0   0 | 1   0   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 3 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 3 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VDIV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 0   0   0 | 1   1   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 3 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 3 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VSAD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 1   0   0 | 0   0   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VSSB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 1   0   0 | 0   1   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VSMY
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 1   0   0 | 1   0   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   0 |  (D)VSDV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | P | 0   0 | 1   0   0 | 1   1   0 | 0 | P | 0 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   0   1 |  (D)VPIV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 3 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 3 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   1   0 |  (D)VABS
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 0   1   1 |  (D)VSUM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 1   0   0 |  (D)VNRM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 1   0   1 |  (D)VDOT
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        scalar address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 1   1   0 |  (D)VMAX
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   0 | 1   1   1 |  (D)VMAB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   1 | 0   0   0 |  (D)VMIN
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   1 | 0   0   1 |  (D)VMIB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   1 | 0   1   0 |  (D)VMOV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | P | 0   1 | 1   0   0 | 1   1   1 | 0   1   1 |  (D)VSWP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | -   -   -   -   -   -   -   -   -   -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 1 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       vector 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      increment 2 address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     element count address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 0   0   1 | 1   0   0 | 1   1   1 | 1   0   0 |  .ERES
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         array address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      array table address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    last subscript address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                              ...                              :
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    first subscript address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                   return location if error                    :  P+n
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if page is mapped               :  P+n+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 0   0   1 | 1   0   0 | 1   1   1 | 1   0   1 |  .ESEG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      array table address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                   return location if error                    :  P+3
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if page is mapped               :  P+4
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 0   0   1 | 1   0   0 | 1   1   1 | 1   1   0 |  .VSET
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     input vector address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     output vector address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       map table address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         scalar count                          |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         vector count                          |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    elements per page count                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :                   return location if error                    :  P+8
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if setup is hard                :  P+9
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if setup is easy                :  P+10
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   0   0 | 1   1   1 | 1   1   1 |  test
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :       return location if the firmware is not installed        :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :         return location if the firmware is installed          :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The test instruction is used to determine if the VIS firmware has been
   installed.  It sets X to the firmware revision code, S to 102077 (HLT 77B)
   and skips the next instruction if the microcode is present.



   The SIGNAL/1000 instructions provide fast Fourier transforms and complex
   arithmetic.  They utilize the F-Series floating-point processor and the
   Vector Instruction Set, so the firmware is supported only on the F-Series
   CPU.

   The SIGNAL encodings are:

      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 0   0   0 |  BITRV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      array base address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     index bitmap address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  count of index bits address                  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 0   0   1 |  BTRFY
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    complex vector address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       real part address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    imaginary part address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         node address                          |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    maximum length address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 0   1   0 |  UNSCR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        vector address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       real part address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    imaginary part address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        index 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        index 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 0   1   1 |  PRSCR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        vector address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       real part address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    imaginary part address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        index 1 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        index 2 address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 1   0   0 |  BITR1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    real array base address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 imaginary array base address                  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     index bitmap address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  count of index bits address                  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 1   0   1 |  BTRF1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                   real vector part address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                 imaginary vector part address                 |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       real part address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    imaginary part address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                         node address                          |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    maximum length address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 1   1   0 |  .CADD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        augend address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        addend address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   0 | 1   1   1 |  .CSUB
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        minuend address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      subtrahend address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   1 | 0   0   0 |  .CMPY
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                     multiplicand address                      |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                      multiplier address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   1 | 0   0   1 |  .CDIV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       dividend address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        divisor address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   1 | 0   1   0 |  CONJG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       argument address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   1 | 0   1   1 |  ..CCM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       argument address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   1 | 1   0   0 |  AIMAG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        operand address                        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   1 | 1   0   1 |  CMPLX
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        result address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       real part address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    imaginary part address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


      15 |14  13  12 |11  10   9 | 8   7   6 | 5   4   3 | 2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 1   1   0 | 0   0   1 | 1   1   1 |  test
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :       return location if the firmware is not installed        :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :         return location if the firmware is installed          :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The test instruction is used to determine if the SIGNAL firmware has been
   installed.  It sets X to the firmware revision code, S to 102077 (HLT 77B)
   and skips the next instruction if the microcode is present.


   Implementation notes:

    1. As the VIS and SIGNAL firmware uses the F-Series Floating-Point
       Processor, and the FPP simulator requires 64-bit integer support, VIS and
       SIGNAL also require 64-bit support.
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"
#include "hp2100_cpu_fp.h"



/* Paging constants */

#define MSEG_MASK           0076000u


/* RTE base page addresses */

static const HP_WORD idx   = 0001645u;
static const HP_WORD xeqt  = 0001717u;
static const HP_WORD umaps = 0003740u;


/* VIS operand accessors */

#define GET_MSIGN(op)       ((op)->fpk [0] & D16_SIGN)


/* SIGNAL operand address accessors */

#define RE(x)               (x + 0)
#define IM(x)               (x + 2)


/* EMA utility structure declarations */

typedef struct ema4 {
    uint32 mseg;                                /* logical start of MSEG */
    uint32 msegsz;                              /* size of std mseg in pgs */
    uint32 pgoff;                               /* pg # in EMA containing element */
    uint32 offs;                                /* offset into page of element */
    uint32 msoff;                               /* total offset to element in MSEG */
    uint32 emasz;                               /* size of ema in pgs */
    uint32 msegno;                              /* # of std mseg */
    uint32 ipgs;                                /* # of pgs to start of MSEG */
    uint32 npgs;                                /* # of pgs needed */
    uint32 spmseg;                              /* first phys pg of MSEG */
    } EMA4;


/* EMA local utility routine declarations */

static t_stat ema_emap (uint32* rtn, uint32 abase, uint32 dtbl, uint32 atbl);
static t_stat ema_emio (uint32* rtn, uint32 bufl, uint32 dtbl, uint32 atbl);
static t_stat ema_mmap (uint32 ipage, uint32 npgs);

static t_bool ema_resolve (uint32 dtbl, uint32 atbl, uint32* sum);
static t_bool ema_emas    (uint32 dtbl, uint32 atbl, EMA4* e);
static t_bool ema_emat    (EMA4* e);
static t_bool ema_mmap01  (EMA4* e);
static t_bool ema_mmap02  (EMA4* e);

static const char *fmt_ab (t_bool success);


#if defined (HAVE_INT64)                                /* int64 support available */

/* VIS local utility routine declarations */

static void vis_svop   (uint32 subcode, OPS op, OPSIZE opsize);
static void vis_vvop   (uint32 subcode, OPS op, OPSIZE opsize);
static void vis_abs    (OP* in, OPSIZE opsize);
static void vis_minmax (OPS op, OPSIZE opsize, t_bool domax, t_bool doabs);
static void vis_vpiv   (OPS op, OPSIZE opsize);
static void vis_vabs   (OPS op, OPSIZE opsize);
static void vis_trunc  (OP* out, OP in);
static void vis_vsmnm  (OPS op, OPSIZE opsize, t_bool doabs);
static void vis_vdot   (OPS op, OPSIZE opsize);
static void vis_movswp (OPS op, OPSIZE opsize, t_bool doswp);

static t_stat vis_eres (HP_WORD *rtn, uint32 dtbl, uint32 atbl);
static t_stat vis_eseg (HP_WORD *rtn, uint32 tbl);
static t_stat vis_vset (HP_WORD *rtn, OPS op);


/* SIGNAL local utility routine declarations */

static void sig_caddsub (uint16 addsub, OPS op);
static void sig_btrfy   (uint32 re, uint32 im, OP wr, OP wi, uint32 k, uint32 n2);
static void sig_bitrev  (uint32 re, uint32 im, uint32 idx, uint32 log2n, int sz);
static OP   sig_scadd   (uint16 oper, t_bool addh, OP a, OP b);
static void sig_cmul    (OP *r, OP *i, OP a, OP b, OP c, OP d);

#endif                                                  /* int64 conditional */



/* Global instruction executors */


/* RTE-IV Extended Memory Array instructions.

   The RTE-IV operating system (HP product number 92067A) introduced the
   Extended Memory Area (EMA) instructions.  EMA provided a mappable data area
   up to one megaword in size.  These three instructions accelerated data
   accesses to variables stored in EMA partitions.  Support was limited to
   E/F-Series machines; M-Series machines used software equivalents.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    92067A  92067A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     .EMIO    105240   EMA I/O
     MMAP     105241   Map physical to logical memory
     emtst    105242   [self test]
     .EMAP    105257   Resolve array element address

   Additional references:
    - RTE-IVB Programmer's Reference Manual (92068-90004, December 1983).
    - RTE-IVB Technical Specifications (92068-90013, January 1980).


   Implemenation notes:

     1. RTE-IV EMA and RTE-6 VMA instructions share the same address space, so a
        given machine can run one or the other, but not both.

     2. The EMA diagnostic (92067-16013) reports bogus MMAP failures if it is
        not loaded at the start of its partition (e.g., because of a LOADR "LO"
        command).  The "ICMPS" map comparison check in the diagnostic assumes
        that the starting page of the program's partition contains the first
        instruction of the program and prints "MMAP ERROR" if it does not.
*/

static const OP_PAT op_ema[16] = {
  OP_AKA,  OP_AKK,    OP_N,    OP_N,            /* .EMIO  MMAP   [test]  ---   */
  OP_N,    OP_N,      OP_N,    OP_N,            /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_N,            /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_AAA           /*  ---    ---    ---   .EMAP  */
  };

t_stat cpu_rte_ema (void)
{
t_stat reason = SCPE_OK;
OPS op;
OP_PAT pattern;
uint32 entry, rtn;

entry = IR & 017;                                       /* mask to entry point */
pattern = op_ema[entry];                                /* get operand pattern */

if (pattern != OP_N) {
    reason = cpu_ops (pattern, op);                     /* get instruction operands */
    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */
    case 000:                                           /* .EMIO 105240 (OP_A) */
        rtn = op[0].word;
        reason = ema_emio(&rtn, op[1].word,
                          op[2].word, PR);              /* handle the EMIO instruction */
        PR = rtn;

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%s)\n",
                 PR, IR, PR - err_PR, fmt_ab (PR - op[0].word));
        break;

    case 001:                                           /* .MMAP  105241 (OP_AKK) */
        reason = ema_mmap(op[1].word, op[2].word);      /* handle the MMAP instruction */
        break;

    case 002:                                           /* emtst 105242 (OP_N) */
        /* effectively, this code just returns without error:
         * real microcode will set S register to 102077B when in single step mode */
        if (sim_step == 1)
            SR = 0102077;
        break;

    case 017:                                           /* .EMAP  105247 (OP_A) */
        rtn = op[0].word;                               /* error return */
        reason = ema_emap(&rtn, op[1].word,
                          op[2].word, PR);              /* handle the EMAP instruction */
        PR = rtn;

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%s)\n",
                 PR, IR, PR - err_PR, fmt_ab (PR - op[0].word));
        break;

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
    }

return reason;
}


#if defined (HAVE_INT64)                                /* int64 support available */

/* Vector Instruction Set.

   The VIS provides instructions that operate on one-dimensional arrays of
   floating-point values.  Both single- and double-precision operations are
   supported.  VIS uses the F-Series floating-point processor to handle the
   floating-point math.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A    12824A

   The routines are mapped to instruction codes as follows:

        Single-Precision        Double-Precision
     Instr.  Opcode  Subcod  Instr.  Opcode  Subcod  Description
     ------  ------  ------  ------  ------  ------  -----------------------------
     VADD    101460  000000  DVADD   105460  004002  Vector add
     VSUB    101460  000020  DVSUB   105460  004022  Vector subtract
     VMPY    101460  000040  DVMPY   105460  004042  Vector multiply
     VDIV    101460  000060  DVDIV   105460  004062  Vector divide
     VSAD    101460  000400  DVSAD   105460  004402  Scalar-vector add
     VSSB    101460  000420  DVSSB   105460  004422  Scalar-vector subtract
     VSMY    101460  000440  DVSMY   105460  004442  Scalar-vector multiply
     VSDV    101460  000460  DVSDV   105460  004462  Scalar-vector divide
     VPIV    101461  0xxxxx  DVPIV   105461  0xxxxx  Vector pivot
     VABS    101462  0xxxxx  DVABS   105462  0xxxxx  Vector absolute value
     VSUM    101463  0xxxxx  DVSUM   105463  0xxxxx  Vector sum
     VNRM    101464  0xxxxx  DVNRM   105464  0xxxxx  Vector norm
     VDOT    101465  0xxxxx  DVDOT   105465  0xxxxx  Vector dot product
     VMAX    101466  0xxxxx  DVMAX   105466  0xxxxx  Vector maximum value
     VMAB    101467  0xxxxx  DVMAB   105467  0xxxxx  Vector maximum absolute value
     VMIN    101470  0xxxxx  DVMIN   105470  0xxxxx  Vector minimum value
     VMIB    101471  0xxxxx  DVMIB   105471  0xxxxx  Vector minimum absolute value
     VMOV    101472  0xxxxx  DVMOV   105472  0xxxxx  Vector move
     VSWP    101473  0xxxxx  DVSWP   105473  0xxxxx  Vector swap
     .ERES   101474    --     --       --      --    Resolve array element address
     .ESEG   101475    --    .VPRG   105475    --    Load MSEG maps
     .VSET   101476    --     --       --      --    Vector setup
      --       --      --    [test]  105477    --    [self test]

   Instructions use IR bit 11 to select single- or double-precision format.  The
   double-precision instruction names begin with "D" (e.g., DVADD vs. VADD).
   Most VIS instructions are two words in length, with a sub-opcode immediately
   following the primary opcode.

   Additional references:
    - 12824A Vector Instruction Set User's Manual (12824-90001, June 1979).
    - VIS Microcode Source (12824-18059, revision 3).


   Implementation notes:

     1. The .VECT (101460) and .DVCT (105460) opcodes preface a single- or
        double-precision arithmetic operation that is determined by the
        sub-opcode value.  The remainder of the dual-precision sub-opcode values
        are "don't care," except for requiring a zero in bit 15.

     2. The VIS uses the hardware FPP of the F-Series.  FPP malfunctions are
        detected by the VIS firmware and are indicated by a memory-protect
        violation and setting the overflow flag.  Under simulation,
        malfunctions cannot occur.
*/

static const OP_PAT op_vis [16] = {
  OP_N,    OP_AAKAKAKK, OP_AKAKK,  OP_AAKK,     /*  .VECT  VPIV   VABS   VSUM   */
  OP_AAKK, OP_AAKAKK,   OP_AAKK,   OP_AAKK,     /*  VNRM   VDOT   VMAX   VMAB   */
  OP_AAKK, OP_AAKK,     OP_AKAKK,  OP_AKAKK,    /*  VMIN   VMIB   VMOV   VSWP   */
  OP_AA,   OP_A,        OP_AAACCC, OP_N         /*  .ERES  .ESEG  .VSET  [test] */
  };

static const t_bool op_ftnret [16] = {
  FALSE, TRUE,  TRUE,  TRUE,
  TRUE,  TRUE,  TRUE,  TRUE,
  TRUE,  TRUE,  TRUE,  TRUE,
  FALSE, TRUE,  TRUE, FALSE,
  };

t_stat cpu_vis (void)
{
static const char * const difficulty [2] = { "hard", "easy" };

t_stat reason = SCPE_OK;
OPS op;
OP_PAT pattern;
OPSIZE opsize;
uint32 entry, subcode;
HP_WORD rtn = 0;

opsize = (IR & 004000) ? fp_t : fp_f;                   /* double or single precision */
entry = IR & 017;                                       /* mask to entry point */
pattern = op_vis [entry];

if (entry == 0) {                                       /* retrieve sub opcode */
    subcode = ReadW (PR);                               /* get it */

    if (subcode & 0100000)                              /* special property of ucode */
        subcode = AR;                                   /*   for reentry */

    PR = (PR + 1) & LA_MASK;                                /* bump to real argument list */
    pattern = (subcode & 0400) ? OP_AAKAKK : OP_AKAKAKK;    /* scalar or vector operation */
    }

if (pattern != OP_N) {
    if (op_ftnret [entry]) {                            /* most VIS instrs ignore RTN addr */
        rtn = ReadW (PR);                               /* get it */
        PR = (PR + 1) & LA_MASK;                        /* move to next argument */
        }

    reason = cpu_ops (pattern, op);                     /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

   case 000:                                            /* .VECT (OP_special) */
       if (subcode & 0400)
           vis_svop(subcode,op,opsize);                 /* scalar/vector op */
       else
           vis_vvop(subcode,op,opsize);                 /* vector/vector op */
       break;

   case 001:                                            /* VPIV (OP_(A)AAKAKAKK) */
       vis_vpiv(op,opsize);
       break;

   case 002:                                            /* VABS (OP_(A)AKAKK) */
       vis_vabs(op,opsize);
       break;

   case 003:                                            /* VSUM (OP_(A)AAKK) */
       vis_vsmnm(op,opsize,FALSE);
       break;

   case 004:                                            /* VNRM (OP_(A)AAKK) */
       vis_vsmnm(op,opsize,TRUE);
       break;

   case 005:                                            /* VDOT (OP_(A)AAKAKK) */
       vis_vdot(op,opsize);
       break;

   case 006:                                            /* VMAX (OP_(A)AAKK) */
       vis_minmax(op,opsize,TRUE,FALSE);
       break;

   case 007:                                            /* VMAB (OP_(A)AAKK) */
       vis_minmax(op,opsize,TRUE,TRUE);
       break;

   case 010:                                            /* VMIN (OP_(A)AAKK) */
       vis_minmax(op,opsize,FALSE,FALSE);
       break;

   case 011:                                            /* VMIB (OP_(A)AAKK) */
       vis_minmax(op,opsize,FALSE,TRUE);
       break;

   case 012:                                            /* VMOV (OP_(A)AKAKK) */
       vis_movswp(op,opsize,FALSE);
       break;

   case 013:                                            /* VSWP (OP_(A)AKAKK) */
       vis_movswp(op,opsize,TRUE);
       break;

   case 014:                                            /* .ERES (OP_(A)AA) */
       PR = rtn;
       reason = vis_eres(&PR,op[2].word,PR);            /* handle the ERES instruction */

       tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%s)\n",
                PR, IR, PR - err_PR, fmt_ab (PR - rtn));
       break;

   case 015:                                            /* .ESEG (OP_(A)A) */
       PR = rtn;
       reason = vis_eseg(&PR, op[0].word);              /* handle the ESEG instruction */

       tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%s)\n",
                PR, IR, PR - err_PR, fmt_ab (PR - rtn));
       break;

   case 016:                                            /* .VSET (OP_(A)AAACCC) */
       PR = rtn;
       reason = vis_vset(&PR,op);

       tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%s)\n",
                PR, IR, PR - err_PR,
                (PR == rtn
                  ? fmt_ab (0)
                  : difficulty [PR - rtn - 1]));
       break;

   case 017:                                            /* [test] (OP_N) */
       XR = 3;                                          /* firmware revision */
       SR = 0102077;                                    /* test passed code */
       PR = (PR + 1) & LA_MASK;                         /* P+2 return for firmware w/VIS */
       break;

   default:                                             /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
   }

return reason;
}


/* SIGNAL/1000 Instructions

   The SIGNAL/1000 instructions provide fast Fourier transforms and complex
   arithmetic.  They utilize the F-Series floating-point processor and the
   Vector Instruction Set.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A    92835A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-F  Description
     ------  ------  ----------------------------------------------
     BITRV   105600  Bit reversal
     BTRFY   105601  Butterfly algorithm
     UNSCR   105602  Unscramble for phasor MPY
     PRSCR   105603  Unscramble for phasor MPY
     BITR1   105604  Swap two elements in array (alternate format)
     BTRF1   105605  Butterfly algorithm (alternate format)
     .CADD   105606  Complex number addition
     .CSUB   105607  Complex number subtraction
     .CMPY   105610  Complex number multiplication
     .CDIV   105611  Complex number division
     CONJG   105612  Complex conjugate
     ..CCM   105613  Complex complement
     AIMAG   105614  Return imaginary part
     CMPLX   105615  Form complex number
     [nop]   105616  [no operation]
     [test]  105617  [self test]

   Notes:

     1. SIGNAL/1000 ROM data are available from Bitsavers.

   Additional references (documents unavailable):
    - HP Signal/1000 User Reference and Installation Manual (92835-90002).
    - SIGNAL/1000 Microcode Source (92835-18075, revision 2).
*/

static const OP_PAT op_signal [16] = {
  OP_AAKK,  OP_AAFFKK,  OP_AAFFKK, OP_AAFFKK,   /*  BITRV  BTRFY  UNSCR  PRSCR */
  OP_AAAKK, OP_AAAFFKK, OP_AAA,    OP_AAA,      /*  BITR1  BTRF1  .CADD  .CSUB */
  OP_AAA,   OP_AAA,     OP_AAA,    OP_A,        /*  .CMPY  .CDIV  CONJG  ..CCM */
  OP_AA,    OP_AAFF,    OP_N,      OP_N         /*  AIMAG  CMPLX  ---    [test]*/
  };

t_stat cpu_signal (void)
{
t_stat reason = SCPE_OK;
OPS op;
OP a,b,c,d,p1,p2,p3,p4,m1,m2,wr,wi;
uint32 entry, v, idx1, idx2;
int32 exc, exd;

entry = IR & 017;                                       /* mask to entry point */

if (op_signal [entry] != OP_N) {
    reason = cpu_ops (op_signal [entry], op);           /* get instruction operands */
    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */
    case 000:                                           /* BITRV (OP_AAKK) */
        /* BITRV
         * bit reversal for FFT
         *   JSB BITRV
         *   DEF ret(,I)   return address
         *   DEF vect,I    base address of array
         *   DEF idx,I     index bitmap to be reversed (one-based)
         *   DEF nbits,I   number of bits of index
         *
         * Given a complex*8 vector of nbits (power of 2), this calculates:
         * swap( vect[idx], vect[rev(idx)]) where rev(i) is the bitreversed value of i */
        sig_bitrev(op[1].word, op[1].word+2, op[2].word-1, op[3].word, 4);
        PR = op[0].word & LA_MASK;
        break;

    case 001:                                           /* BTRFY (OP_AAFFKK) */
        /* BTRFY - butterfly operation
         *   JSB BTRFY
         *   DEF ret(,I)   return address
         *   DEF vect(,I)  complex*8 vector
         *   DEF wr,I      real part of W
         *   DEF wi,I      imag part of W
         *   DEF node,I    index of 1st op (1 based)
         *   DEF lmax,I    offset to 2nd op (0 based) */
        sig_btrfy(op[1].word, op[1].word+2,
                  op[2], op[3],
                  2*(op[4].word-1), 2*op[5].word);
        PR = op[0].word & LA_MASK;
        break;

    case 002:                                           /* UNSCR (OP_AAFFKK) */
        /* UNSCR unscramble for phasor MPY
         *   JSB UNSCR
         *   DEF ret(,I)
         *   DEF vector,I
         *   DEF WR
         *   DEF WI
         *   DEF idx1,I
         *   DEF idx2,I */
        v = op[1].word;
        idx1 = 2 * (op[4].word - 1);
        idx2 = 2 * (op[5].word - 1);
        wr = op[2];                                     /* read WR */
        wi = op[3];                                     /* read WI */
        p1 = ReadOp(RE(v + idx1), fp_f);                /* S1 VR[idx1] */
        p2 = ReadOp(RE(v + idx2), fp_f);                /* S2 VR[idx2] */
        p3 = ReadOp(IM(v + idx1), fp_f);                /* S9 VI[idx1] */
        p4 = ReadOp(IM(v + idx2), fp_f);                /* S10 VI[idx2] */
        c = sig_scadd(000, TRUE, p3, p4);               /* S5,6 0.5*(p3+p4) */
        d = sig_scadd(020, TRUE, p2, p1);               /* S7,8 0.5*(p2-p1) */
        sig_cmul(&m1, &m2, wr, wi, c, d);               /* (WR,WI) * (c,d) */
        c = sig_scadd(000, TRUE, p1, p2);               /* 0.5*(p1+p2) */
        d = sig_scadd(020, TRUE, p3, p4);               /* 0.5*(p3-p4) */
        (void)fp_exec(000, &p1, c, m1);                 /* VR[idx1] := 0.5*(p1+p2) + real(W*(c,d)) */
        WriteOp(RE(v + idx1), p1, fp_f);
        (void)fp_exec(000, &p2, d, m2);                 /* VI[idx1] := 0.5*(p3-p4) + imag(W*(c,d)) */
        WriteOp(IM(v + idx1), p2, fp_f);
        (void)fp_exec(020, &p1, c, m1);                 /* VR[idx2] := 0.5*(p1+p2) - imag(W*(c,d)) */
        WriteOp(RE(v + idx2), p1, fp_f);
        (void)fp_exec(020, &p2, d, m2);                 /* VI[idx2] := 0.5*(p3-p4) - imag(W*(c,d)) */
        WriteOp(IM(v + idx2), p2, fp_f);
        PR = op[0].word & LA_MASK;
        break;

    case 003:                                           /* PRSCR (OP_AAFFKK) */
        /* PRSCR unscramble for phasor MPY
         *   JSB PRSCR
         *   DEF ret(,I)
         *   DEF vector,I
         *   DEF WR
         *   DEF WI
         *   DEF idx1,I
         *   DEF idx2,I */
        v = op[1].word;
        idx1 = 2 * (op[4].word - 1);
        idx2 = 2 * (op[5].word - 1);
        wr = op[2];                                     /* read WR */
        wi = op[3];                                     /* read WI */
        p1 = ReadOp(RE(v + idx1), fp_f);                /* VR[idx1] */
        p2 = ReadOp(RE(v + idx2), fp_f);                /* VR[idx2] */
        p3 = ReadOp(IM(v + idx1), fp_f);                /* VI[idx1] */
        p4 = ReadOp(IM(v + idx2), fp_f);                /* VI[idx2] */
        c = sig_scadd(020, FALSE, p1, p2);              /* p1-p2 */
        d = sig_scadd(000, FALSE, p3, p4);              /* p3+p4 */
        sig_cmul(&m1,&m2, wr, wi, c, d);                /* (WR,WI) * (c,d) */
        c = sig_scadd(000, FALSE, p1, p2);              /* p1+p2 */
        d = sig_scadd(020, FALSE, p3,p4);               /* p3-p4 */
        (void)fp_exec(020, &p1, c, m2);                 /* VR[idx1] := (p1-p2) - imag(W*(c,d)) */
        WriteOp(RE(v + idx1), p1, fp_f);
        (void)fp_exec(000, &p2, d, m1);                 /* VI[idx1] := (p3-p4) + real(W*(c,d)) */
        WriteOp(IM(v + idx1), p2, fp_f);
        (void)fp_exec(000, &p1, c, m2);                 /* VR[idx2] := (p1+p2) + imag(W*(c,d)) */
        WriteOp(RE(v + idx2), p1, fp_f);
        (void)fp_exec(020, &p2, m1, d);                 /* VI[idx2] := imag(W*(c,d)) - (p3-p4) */
        WriteOp(IM(v + idx2), p2, fp_f);
        PR = op[0].word & LA_MASK;
        break;

    case 004:                                           /* BITR1 (OP_AAAKK) */
        /* BITR1
         * bit reversal for FFT, alternative version
         *   JSB BITR1
         *   DEF ret(,I)   return address if already swapped
         *   DEF revect,I  base address of real vect
         *   DEF imvect,I  base address of imag vect
         *   DEF idx,I     index bitmap to be reversed (one-based)
         *   DEF nbits,I   number of bits of index
         *
         * Given a complex*8 vector of nbits (power of 2), this calculates:
         * swap( vect[idx], vect[rev(idx)]) where rev(i) is the bitreversed value of i
         *
         * difference to BITRV is that BITRV uses complex*8, and BITR1 uses separate real*4
         * vectors for Real and Imag parts */
        sig_bitrev(op[1].word, op[2].word, op[3].word-1, op[4].word, 2);
        PR = op[0].word & LA_MASK;
        break;


    case 005:                                           /* BTRF1 (OP_AAAFFKK) */
        /* BTRF1 - butterfly operation with real*4 vectors
         *   JSB BTRF1
         *   DEF ret(,I)   return address
         *   DEF rvect,I   real part of vector
         *   DEF ivect,I   imag part of vector
         *   DEF wr,I      real part of W
         *   DEF wi,I      imag part of W
         *   DEF node,I    index (1 based)
         *   DEF lmax,I    index (0 based) */
        sig_btrfy(op[1].word, op[2].word,
                  op[3], op[4],
                  op[5].word-1, op[6].word);
        PR = op[0].word & LA_MASK;
        break;

    case 006:                                           /* .CADD (OP_AAA) */
        /* .CADD Complex addition
         *   JSB .CADD
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex addition is: (a+bi) + (c+di) => (a+c) + (b+d)i */
        sig_caddsub(000,op);
        break;

    case 007:                                           /* .CSUB (OP_AAA) */
        /* .CSUB Complex subtraction
         *   JSB .CSUB
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex subtraction is: (a+bi) - (c+di) => (a - c) + (b - d)i */
        sig_caddsub(020,op);
        break;

    case 010:                                           /* .CMUL (OP_AAA) */
        /* .CMPY Complex multiplication
         * call:
         *   JSB .CMPY
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex multiply is: (a+bi)*(c+di) => (ac-bd) + (ad+bc)i */
        a = ReadOp(RE(op[1].word), fp_f);               /* read 1st op */
        b = ReadOp(IM(op[1].word), fp_f);
        c = ReadOp(RE(op[2].word), fp_f);               /* read 2nd op */
        d = ReadOp(IM(op[2].word), fp_f);
        sig_cmul(&p1, &p2, a, b, c, d);
        WriteOp(RE(op[0].word), p1, fp_f);              /* write real result */
        WriteOp(IM(op[0].word), p2, fp_f);              /* write imag result */
        break;

    case 011:                                           /* .CDIV (OP_AAA) */
        /* .CDIV Complex division
         * call:
         *   JSB .CDIV
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex division is: (a+bi)/(c+di) => ((ac+bd) + (bc-ad)i)/(c^2+d^2) */
        a = ReadOp(RE(op[1].word), fp_f);               /* read 1st op */
        b = ReadOp(IM(op[1].word), fp_f);
        c = ReadOp(RE(op[2].word), fp_f);               /* read 2nd op */
        d = ReadOp(IM(op[2].word), fp_f);
        (void)fp_unpack (NULL, &exc, c, fp_f);          /* get exponents */
        (void)fp_unpack (NULL, &exd, d, fp_f);
        if (exc < exd) {                                /* ensure c/d < 1 */
            p1 = a; a = c; c = p1;                      /* swap dividend and divisor */
            p1 = b; b = d; d = p1;
        }
        (void)fp_exec(060, &p1, d, c);                  /* p1,accu := d/c */
        (void)fp_exec(044, ACCUM, d, NOP);              /* ACCUM := dd/c */
        (void)fp_exec(004, &p2, c, NOP);                /* p2 := c + dd/c */
        (void)fp_exec(040, ACCUM, b, p1);               /* ACCUM := bd/c */
        (void)fp_exec(004, ACCUM, a, NOP);              /* ACCUM := a + bd/c */
        (void)fp_exec(070, &p3, NOP, p2);               /* p3 := (a+bd/c)/(c+dd/c) == (ac+bd)/(cc+dd) */
        WriteOp(RE(op[0].word), p3, fp_f);              /* Write real result */
        (void)fp_exec(040, ACCUM, a, p1);               /* ACCUM := ad/c */
        (void)fp_exec(030, ACCUM, NOP, b);              /* ACCUM := ad/c - b */
        if (exd < exc) {                                /* was not swapped? */
            (void)fp_exec(024, ACCUM, ZERO, NOP);       /* ACCUM := -ACCUM */
        }
        (void)fp_exec(070, &p3, NOP, p2);               /* p3 := (b-ad/c)/(c+dd/c) == (bc-ad)/cc+dd) */
        WriteOp(IM(op[0].word), p3, fp_f);              /* Write imag result */
        break;

    case 012:                                           /* CONJG (OP_AAA) */
        /* CONJG build A-Bi from A+Bi
         * call:
         *   JSB CONJG
         *   DEF RTN
         *   DEF res,I    result
         *   DEF arg,I    input argument */
        a = ReadOp(RE(op[2].word), fp_f);               /* read real */
        b = ReadOp(IM(op[2].word), fp_f);               /* read imag */
        (void)fp_pcom(&b, fp_f);                        /* negate imag */
        WriteOp(RE(op[1].word), a, fp_f);               /* write real */
        WriteOp(IM(op[1].word), b, fp_f);               /* write imag */
        break;

    case 013:                                           /* ..CCM (OP_A) */
        /* ..CCM complement complex
         * call
         *   JSB ..CCM
         *   DEF arg
         * build (-RE,-IM)
         */
        v = op[0].word;
        a = ReadOp(RE(v), fp_f);                        /* read real */
        b = ReadOp(IM(v), fp_f);                        /* read imag */
        (void)fp_pcom(&a, fp_f);                        /* negate real */
        (void)fp_pcom(&b, fp_f);                        /* negate imag */
        WriteOp(RE(v), a, fp_f);                        /* write real */
        WriteOp(IM(v), b, fp_f);                        /* write imag */
        break;

    case 014:                                           /* AIMAG (OP_AA) */
        /* AIMAG return the imaginary part in AB
         *   JSB AIMAG
         *   DEF *+2
         *   DEF cplx(,I)
         * returns: AB imaginary part of complex number */
        a = ReadOp(IM(op[1].word), fp_f);               /* read imag */
        AR = a.fpk[0];                                  /* move MSB to A */
        BR = a.fpk[1];                                  /* move LSB to B */
        break;

    case 015:                                           /* CMPLX (OP_AFF) */
        /* CMPLX form a complex number
         *   JSB CMPLX
         *   DEF *+4
         *   DEF result,I  complex number
         *   DEF repart,I  real value
         *   DEF impart,I  imaginary value */
        WriteOp(RE(op[1].word), op[2], fp_f);           /* write real part */
        WriteOp(IM(op[1].word), op[3], fp_f);           /* write imag part */
        break;

    case 017:                                           /* [slftst] (OP_N) */
        XR = 2;                                         /* firmware revision */
        SR = 0102077;                                   /* test passed code */
        PR = (PR + 1) & LA_MASK;                        /* P+2 return for firmware w/SIGNAL1000 */
        break;

    case 016:                                           /* invalid */
    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
        }

return reason;
}

#endif                                                  /* int64 conditional */



/* EMA local utility routines */


/* .EMAP microcode routine, resolves both EMA/non-EMA calls
 *  Call:
 *    OCT 105257B
 *    DEF RTN          error return (rtn), good return is rtn+1
 *    DEF ARRAY[,I]    array base (abase)
 *    DEF TABLE[,I]    array declaration (dtbl)
 *    DEF A(N)[,I]     actual subscripts (atbl)
 *    DEF A(N-1)[,I]
 *    ...
 *    DEF A(2)[,I]
 *    DEF A(1)[,I]
 *  RTN EQU *          error return A="15", B="EM"
 *  RTN+1 EQU *+1      good return B=logical address
 *
 *  TABLE DEC #        # dimensions
 *        DEC -L(N)
 *        DEC D(N-1)
 *        DEC -L(N-1)  lower bound (n-1)st dim
 *        DEC D(N-2)   (n-2)st dim
 *        ...
 *        DEC D(1)     1st dim
 *        DEC -L(1)    lower bound 1st dim
 *        DEC #        # words/element
 *        OFFSET 1     EMA Low
 *        OFFSET 2     EMA High
 */

static t_stat ema_emap(uint32* rtn,uint32 abase,uint32 dtbl,uint32 atbl)
{
uint32 xidex, eqt, idext0, idext1;
int32 sub, ndim, sz;
uint32 offs, pgoff, emasz, phys, msgn, mseg, sum, pg0, pg1, act, low, usz;

xidex = ReadU (idx);                                    /* read ID Extension */
if (xidex) {                                            /* is EMA declared? */
    idext1 = ReadWA(xidex+1);                           /* get word 1 of idext */
    mseg = (idext1 >> 1) & MSEG_MASK;                   /* get logical start MSEG */
    if (abase >= mseg) {                                /* EMA reference? */
        if (!ema_resolve(dtbl,atbl,&sum))               /* calculate subscript */
            goto em15;
        offs = sum & 01777;                             /* address offset within page */
        pgoff = sum >> 10;                              /* ema offset in pages */
        if (pgoff > 1023) goto em15;                    /* overflow? */
        eqt = ReadU (xeqt);
        emasz = ReadWA(eqt+28) & 01777;                 /* EMA size in pages */
        phys = idext1 & 01777;                          /* physical start pg of EMA */
        if (pgoff > emasz) goto em15;                   /* outside EMA range? */

        msgn = mseg >> 10;                              /* get # of 1st MSEG reg */
        phys += pgoff;

        pg0 = meu_read_map(User_Map,0);                 /* read base page map# */
        pg1 = meu_read_map(User_Map,1);                 /* save map# 1 */
        meu_write_map(User_Map,1,pg0);                  /* map #0 into reg #1 */

        WriteU (umaps + msgn, phys);                    /* store 1st mapped pg in user map */
        meu_write_map(User_Map, msgn, phys);            /* and set the map register */
        phys = (pgoff+1)==emasz ? 0140000 : phys+1;     /* protect 2nd map if end of EMA */
        WriteU (umaps + msgn + 1, phys);                /* store 2nd mapped pg in user map */
        meu_write_map(User_Map, msgn+1, phys);          /* and set the map register */

        meu_write_map(User_Map,1,pg1);                  /* restore map #1 */

        idext0 = ReadWA(xidex+0) | 0100000;             /* set NS flag in id extension */
        WriteS (xidex + 0, idext0);                     /* save back value */
        AR = 0;                                         /* was successful */
        BR = (HP_WORD) (mseg + offs);                   /* calculate log address */
        (*rtn)++;                                       /* return via good exit */
        return SCPE_OK;
    }
}                                                       /* not EMA reference */
ndim = ReadW(dtbl++);
if (ndim<0) goto em15;                                  /* negative ´dimensions */
sum = 0;                                                /* accu for index calc */
while (ndim > 0) {
    MR = ReadW (atbl++);                                /* fetch address of A(N) */
    cpu_resolve_indirects (FALSE);                      /* resolve indirects (uninterruptible) */
    act = ReadW(MR);                                    /* A(N) */
    low = ReadW(dtbl++);                                /* -L(N) */
    sub = SEXT16(act) + SEXT16(low);                    /* subscript */
    if (sub & 0xffff8000) goto em15;                    /* overflow? */
    sum += sub;                                         /* accumulate */
    usz = ReadW(dtbl++);
    sz = SEXT16(usz);
    if (sz < 0) goto em15;
    sum *= sz;                                          /* and multiply with sz of dimension */
    if (sum & 0xffff8000) goto em15;                    /* overflow? */
    ndim--;
}
BR = (HP_WORD) (abase + sum);                           /* add displacement */
(*rtn)++;                                               /* return via good exit */
return SCPE_OK;

em15:                                                   /* error condition */
  AR=0x3135;                                            /* AR = '15' */
  BR=0x454d;                                            /* BR = 'EM' */
  return SCPE_OK;                                       /* return via unmodified e->rtn */
}


/* .EMIO microcode routine, resolves element addr for EMA array
 * and maps the appropriate map segment
 *
 *  Call:
 *    OCT 105250B
 *    DEF RTN          error return (rtn), good return is rtn+1
 *    DEF BUFLEN       length of buffer in words (bufl)
 *    DEF TABLE[,I]    array declaration (dtbl)
 *    DEF A(N)[,I]     actual subscripts (atbl)
 *    DEF A(N-1)[,I]
 *    ...
 *    DEF A(2)[,I]
 *    DEF A(1)[,I]
 *  RTN EQU *          error return A="15", B="EM"
 *  RTN+1 EQU *+1      good return B=logical address
 *
 *  TABLE DEC #        # dimensions
 *        DEC -L(N)
 *        DEC D(N-1)
 *        DEC -L(N-1)  lower bound (n-1)st dim
 *        DEC D(N-2)   (n-2)st dim
 *        ...
 *        DEC D(1)     1st dim
 *        DEC -L(1)    lower bound 1st dim
 *        DEC #        # words/element
 *        OFFSET 1     EMA Low
 *        OFFSET 2     EMA High
 */

static t_stat ema_emio(uint32* rtn,uint32 bufl,uint32 dtbl,uint32 atbl)
{
uint32 xidex, idext1;
uint32 mseg, bufpgs, npgs;
EMA4 ema4, *e = &ema4;

xidex = ReadU (idx);                                    /* read ID extension */
if (bufl & D16_SIGN ||                                  /* buffer length negative? */
    xidex==0) goto em16;                                /* no EMA declared? */

idext1 = ReadWA(xidex+1);                               /* |logstrt mseg|d|physstrt ema| */
mseg = (idext1 >> 1) & MSEG_MASK;                       /* get logical start MSEG */
if (!ema_emas(dtbl,atbl,e)) goto em16;                  /* resolve address */
bufpgs = (bufl + e->offs) >> 10;                        /* # of pgs reqd for buffer */
if ((bufl + e->offs) & 01777) bufpgs++;                 /* S11 add 1 if not at pg boundary */
if ((bufpgs + e->pgoff) > e->emasz) goto em16;          /* exceeds EMA limit? */
npgs = (e->msoff + bufl) >> 10;                         /* # of pgs reqd for MSEG */
if ((e->msoff + bufl) & 01777) npgs++;                  /* add 1 if not at pg boundary */
if (npgs < e->msegsz) {
    e->mseg = mseg;                                     /* logical stat of MSEG */
    if (!ema_emat(e)) goto em16;                        /* do a std mapping */
} else {
    BR = (HP_WORD) (mseg + e->offs);                    /* logical start of buffer */
    e->npgs = bufpgs;                                   /* S5 # pgs required */
    e->ipgs = e->pgoff;                                 /* S6 page offset to reqd pg */
    if (!ema_mmap02(e)) goto em16;                      /* do nonstd mapping */
}
(*rtn)++;                                               /* return via good exit */
return SCPE_OK;

em16:                                                   /* error condition */
AR=0x3136;                                              /* AR = '16' */
BR=0x454d;                                              /* BR = 'EM' */
return SCPE_OK;                                         /* return via unmodified rtn */
}


/* Map a sequence of physical memory pages into the mapping segment */

static t_stat ema_mmap(uint32 ipage,uint32 npgs)
{
uint32 xidex;
EMA4 ema4, *e = &ema4;

e->ipgs = ipage;                                        /* S6 set the arguments */
e->npgs = npgs;                                         /* S5 */

AR = 0;
xidex = ReadU (idx);
if ((ipage & D16_SIGN) ||                               /* negative page displacement? */
    (npgs & D16_SIGN) ||                                /* negative # of pages? */
    xidex == 0 ||                                       /* no EMA? */
    !ema_mmap02(e))                                     /* mapping failed? */
    AR = 0177777;                                       /* return with error */
return SCPE_OK;                                         /* leave */
}


/* calculate the 32 bit EMA subscript for an array */

static t_bool ema_resolve(uint32 dtbl,uint32 atbl,uint32* sum)
{
int32 sub, sz, ndim;
uint32 base, udim, usz, act, low;

udim = ReadW(dtbl++);                                   /* # dimensions */
ndim = SEXT16(udim);                                    /* sign extend */
if (ndim < 0) return FALSE;                             /* invalid? */

*sum = 0;                                               /* accu for index calc */
while (ndim > 0) {
    MR = ReadW (atbl++);                                /* fetch address of A(N) */
    cpu_resolve_indirects (FALSE);                      /* resolve indirects (uninterruptible) */
    act = ReadW(MR);                                    /* A(N) */
    low = ReadW(dtbl++);                                /* -L(N) */
    sub = SEXT16(act) + SEXT16(low);                    /* subscript */
    if (sub & 0xffff8000) return FALSE;                 /* overflow? */
    *sum += sub;                                        /* accumulate */
    usz = ReadW(dtbl++);
    sz = SEXT16(usz);
    if (sz < 0) return FALSE;
    *sum *= sz;
    if (*sum > (512*1024)) return FALSE;                /* overflow? */
    ndim--;
}
base = (ReadW(dtbl+1)<<16) | (ReadW(dtbl) & 0xffff);    /* base of array in EMA */
if (base & 0x8000000) return FALSE;
*sum += base;                                           /* calculate address into EMA */
if (*sum & 0xf8000000) return FALSE;                    /* overflow? */
return TRUE;
}


static t_bool ema_emas(uint32 dtbl,uint32 atbl,EMA4* e)
{
uint32 xidex, eqt;
uint32 sum, msegsz,pgoff,offs,emasz,msegno,msoff,ipgs;

if (!ema_resolve(dtbl,atbl,&sum)) return FALSE;         /* calculate 32 bit index */

xidex = ReadU (idx);                                    /* read ID extension */
msegsz = ReadWA(xidex+0) & 037;                         /* S5 # pgs for std MSEG */
pgoff = sum >> 10;                                      /* S2 page containing element */
offs = sum & 01777;                                     /* S6 offset in page to element */
if (pgoff > 1023) return FALSE;                         /* overflow? */
eqt = ReadU (xeqt);
emasz = ReadWA(eqt+28) & 01777;                         /* S EMA size in pages */
if (pgoff > emasz) return FALSE;                        /* outside EMA? */
msegno = pgoff / msegsz;                                /* S4 # of MSEG */
msoff = pgoff % msegsz;                                 /* offset within MSEG in pgs */
ipgs = pgoff - msoff;                                   /* S7 # pgs to start of MSEG */
msoff = msoff << 10;                                    /* offset within MSEG in words */
msoff += offs;                                          /* S1 offset to element in words */

e->msegsz = msegsz;                                     /* return calculated data */
e->pgoff = pgoff;
e->offs = offs;
e->emasz = emasz;
e->msegno = msegno;
e->ipgs = ipgs;
e->msoff = msoff;
return TRUE;
}


static t_bool ema_emat(EMA4* e)
{
uint32 xidex,idext0;
uint32 curmseg,phys,msnum,lastpgs;

xidex = ReadU (idx);                                    /* read ID extension */
idext0 = ReadWA(xidex+0);                               /* get current segment */
curmseg = idext0 >> 5;
if ((idext0 & 0100000) ||                               /* was nonstd MSEG? */
    curmseg != e->msegno) {                             /* or different MSEG last time? */
    phys = ReadWA(xidex+1) & 01777;                     /* physical start pg of EMA */
    e->spmseg = phys + e->ipgs;                         /* physical start pg of MSEG */
    msnum = e->emasz / e->msegsz;                       /* find last MSEG# */
    lastpgs = e->emasz % e->msegsz;                     /* #pgs in last MSEG */
    if (lastpgs==0) msnum--;                            /* adjust # of last MSEG */
    e->npgs = msnum==e->msegno ? lastpgs : e->msegsz;   /* for last MSEG, only map available pgs */
    if (!ema_mmap01(e)) return FALSE;                   /* map npgs pages at ipgs */
}
BR = (HP_WORD) (e->mseg + e->msoff);                    /* return address of element */
return TRUE;                                            /* and everything done */
}


static t_bool ema_mmap01(EMA4* e)
{
uint32 xidex,idext0, pg, pg0, pg1, i;

uint32 base = e->mseg >> 10;                            /* get the # of first MSEG DMS reg */
xidex = ReadU (idx);                                    /* get ID extension */
idext0 = ReadWA(xidex+1);

if (e->npgs==0) return FALSE;                           /* no pages to map? */
if ((e->npgs+1+e->ipgs) <= e->emasz) e->npgs++;         /* actually map npgs+1 pgs */

/* locations 1740...1777 of user base page contain the map entries we need.
 * They are normally hidden by BP fence, therefore they have to be accessed by
 * another fence-less map register.  uCode uses #1, macro code uses $DVCT (==2)
 */
pg0 = meu_read_map(User_Map,0);                         /* read base page map# */
pg1 = meu_read_map(User_Map,1);                         /* save map# 1 */
meu_write_map(User_Map,1,pg0);                          /* map #0 into reg #1 */
for (i=0; (base+i)<32; i++) {
    pg = i<e->npgs ? e->spmseg : 0140000;               /* write protect if outside */
    WriteU (umaps + base + i, pg);                      /* copy pg to user map */
/* printf("MAP val %d to reg %d (addr=%o)\n",pg,base+i,umaps+base+i); */
    meu_write_map(User_Map, base+i, pg);                /* set DMS reg */
    e->spmseg++;
}
meu_write_map(User_Map,1,pg1);                          /* restore map #1 */

xidex = ReadU (idx);                                    /* get ID extension */
idext0 = ReadWA(xidex+0);
if (e->msegno == 0xffff)                                /* non std mseg */
    idext0 |= 0x8000;                                   /* set nonstd marker */
else
    idext0 = (idext0 & 037) | (e->msegno<<5);           /* set new current mseg# */
WriteS (xidex, idext0);                                 /* save back value */
AR = 0;                                                 /* was successful */
return TRUE;
}


static t_bool ema_mmap02(EMA4* e)
{
uint32 xidex, eqt, idext1;
uint32 mseg,phys,spmseg,emasz,msegsz,msegno;

xidex = ReadU (idx);                                    /* get ID extension */
msegsz = ReadWA(xidex+0) & 037;                         /* P size of std MSEG */
idext1 = ReadWA(xidex+1);
mseg = (idext1 >> 1) & MSEG_MASK;                       /* S9 get logical start MSEG */
phys = idext1 & 01777;                                  /* S phys start of EMA */
spmseg = phys + e->ipgs;                                /* S7 phys pg# of MSEG */
msegno = e->ipgs / msegsz;
if ((e->ipgs % msegsz) != 0)                            /* non std MSEG? */
    msegno = 0xffff;                                    /* S4 yes, set marker */
if (e->npgs > msegsz) return FALSE;                     /* map more pages than MSEG sz? */
eqt = ReadU (xeqt);
emasz = ReadWA(eqt+28) & 01777;                         /* B EMA size in pages */
if ((e->ipgs+e->npgs) > emasz) return FALSE;            /* outside EMA? */
if ((e->ipgs+msegsz) > emasz)                           /* if MSEG overlaps end of EMA */
    e->npgs = emasz - e->ipgs;                          /* only map until end of EMA */

e->emasz = emasz;                                       /* copy arguments */
e->msegsz = msegsz;
e->msegno = msegno;
e->spmseg = spmseg;
e->mseg = mseg;
return ema_mmap01(e);
}


/* Format an error code in the A and B registers.

   This routine conditionally formats the contents of the A and B registers into
   an error message.  If the supplied "success" flag is 0, the A and B registers
   contain a four-character error code (e.g., "EM82"), with the leading
   characters in the B register.  The characters are moved into the error
   message, and a pointer to the message is returned.  If "success" is non-zero,
   then a pointer to the message reporting normal execution is returned.

   The routine is typically called from an instructio executor during operand
   tracing.
*/

static const char *fmt_ab (t_bool success)
{
static const char good  [] = "normal";
static       char error [] = "error ....";

if (success)                                            /* if the instruction succeeded */
    return good;                                        /*   then report a normal completion */

else {                                                  /* otherwise */
    error [6] = UPPER_BYTE (BR);                        /*   format the */
    error [7] = LOWER_BYTE (BR);                        /*     error code */
    error [8] = UPPER_BYTE (AR);                        /*       into the */
    error [9] = LOWER_BYTE (AR);                        /*         error message */

    return error;                                       /* report an abnormal completion */
    }
}



#if defined (HAVE_INT64)                                /* int64 support available */

/* VIS local utility routines */


/* handle the scalar/vector base ops */

static void vis_svop(uint32 subcode, OPS op, OPSIZE opsize)
{
OP v1,v2;
int16 delta = opsize==fp_f ? 2 : 4;
OP s = ReadOp(op[0].word,opsize);
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
uint32 v2addr = op[3].word;
int16 ix2 = INT16(op[4].word) * delta;
int16 i, n = INT16(op[5].word);
uint16 fpuop = (uint16) (subcode & 060) | (opsize==fp_f ? 0 : 2);

if (n <= 0) return;
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    (void)fp_exec(fpuop, &v2, s ,v1);
    WriteOp(v2addr, v2, opsize);
    v1addr += ix1;
    v2addr += ix2;
    }
}


/* handle the vector/vector base ops */

static void vis_vvop(uint32 subcode, OPS op,OPSIZE opsize)
{
OP v1,v2,v3;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 v1addr = op[0].word;
int32 ix1 = INT16(op[1].word) * delta;
uint32 v2addr = op[2].word;
int32 ix2 = INT16(op[3].word) * delta;
uint32 v3addr = op[4].word;
int32 ix3 = INT16(op[5].word) * delta;
int16 i, n = INT16(op[6].word);
uint16 fpuop = (uint16) (subcode & 060) | (opsize==fp_f ? 0 : 2);

if (n <= 0) return;
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    v2 = ReadOp(v2addr, opsize);
    (void)fp_exec(fpuop, &v3, v1 ,v2);
    WriteOp(v3addr, v3, opsize);
    v1addr += ix1;
    v2addr += ix2;
    v3addr += ix3;
    }
}


static void vis_abs(OP* in, OPSIZE opsize)
{
uint32 sign = GET_MSIGN(in);                            /* get sign */
if (sign) (void)fp_pcom(in, opsize);                    /* if negative, make positive */
}


static void vis_minmax(OPS op,OPSIZE opsize,t_bool domax,t_bool doabs)
{
OP v1,vmxmn,res;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 mxmnaddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
int16 n = INT16(op[3].word);
int16 i,mxmn,sign;
uint16 subop = 020 | (opsize==fp_f ? 0 : 2);

if (n <= 0) return;
mxmn = 0;                                               /* index of maxmin element */
vmxmn = ReadOp(v1addr,opsize);                          /* initialize with first element */
if (doabs) vis_abs(&vmxmn,opsize);                      /* ABS(v[1]) if requested */

for (i = 0; i<n; i++) {
    v1 = ReadOp(v1addr,opsize);                         /* get v[i] */
    if (doabs) vis_abs(&v1,opsize);                     /* build ABS(v[i]) if requested */
    (void)fp_exec(subop,&res,vmxmn,v1);                 /* subtract vmxmn - v1[i] */
    sign = GET_MSIGN(&res);                             /* !=0 if vmxmn < v1[i] */
    if ((domax && sign) ||                              /* new max value found */
        (!domax && !sign)) {                            /* new min value found */
        mxmn = i;
       vmxmn = v1;                                      /* save the new max/min value */
       }
    v1addr += ix1;                                      /* point to next element */
    }
res.word = mxmn+1;                                      /* adjust to one-based FTN array */
WriteOp(mxmnaddr, res, in_s);                           /* save result */
}


static void vis_vpiv(OPS op, OPSIZE opsize)
{
OP s,v1,v2,v3;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 saddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
uint32 v2addr = op[3].word;
int16 ix2 = INT16(op[4].word) * delta;
uint32 v3addr = op[5].word;
int16 ix3 = INT16(op[6].word) * delta;
int16 i, n = INT16(op[7].word);
int16 oplen = opsize==fp_f ? 0 : 2;

if (n <= 0) return;
s = ReadOp(saddr,opsize);
/* calculates v3[k] = s * v1[i] + v2[j] for incrementing i,j,k */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    (void)fp_exec(040+oplen, ACCUM, s ,v1);             /* ACCU := s*v1 */
    v2 = ReadOp(v2addr, opsize);
    (void)fp_exec(004+oplen,&v3,v2,NOP);                /* v3 := v2 + s*v1 */
    WriteOp(v3addr, v3, opsize);                        /* write result */
    v1addr += ix1;                                      /* forward to next array elements */
    v2addr += ix2;
    v3addr += ix3;
    }
}


static void vis_vabs(OPS op, OPSIZE opsize)
{
OP v1;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 v1addr = op[0].word;
int16 ix1 = INT16(op[1].word) * delta;
uint32 v2addr = op[2].word;
int32 ix2 = INT16(op[3].word) * delta;
int16 i,n = INT16(op[4].word);

if (n <= 0) return;
/* calculates v2[j] = ABS(v1[i]) for incrementing i,j */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    vis_abs(&v1,opsize);                                /* make absolute value */
    WriteOp(v2addr, v1, opsize);                        /* write result */
    v1addr += ix1;                                      /* forward to next array elements */
    v2addr += ix2;
    }
}


static void vis_trunc(OP* out, OP in)
{
/* Note there is fp_trun(), but this doesn't seem to do the same conversion
 * as the original code does */
out->fpk[0] = in.fpk[0];
out->fpk[1] = (in.fpk[1] & 0177400) | (in.fpk[3] & 0377);
}


static void vis_vsmnm(OPS op,OPSIZE opsize,t_bool doabs)
{
uint16 fpuop;
OP v1,sumnrm = ZERO;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 saddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
int16 i,n = INT16(op[3].word);

if (n <= 0) return;
/* calculates sumnrm = sumnrm + DBLE(v1[i]) resp DBLE(ABS(v1[i])) for incrementing i */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    if (opsize==fp_f) (void)fp_cvt(&v1,fp_f,fp_t);      /* cvt to DBLE(v1) */
    fpuop = (doabs && GET_MSIGN(&v1)) ? 022 : 002;      /* use subtract for NRM && V1<0 */
    (void)fp_exec(fpuop,&sumnrm, sumnrm, v1);           /* accumulate */
    v1addr += ix1;                                      /* forward to next array elements */
    }
if (opsize==fp_f)
    (void)vis_trunc(&sumnrm,sumnrm);                    /* truncate to SNGL(sumnrm) */
WriteOp(saddr, sumnrm, opsize);                         /* write result */
}


static void vis_vdot(OPS op,OPSIZE opsize)
{
OP v1,v2,dot = ZERO;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 daddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
uint32 v2addr = op[3].word;
int16 ix2 = INT16(op[4].word) * delta;
int16 i,n = INT16(op[5].word);

if (n <= 0) return;
/* calculates dot = dot + v1[i]*v2[j] for incrementing i,j */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    if (opsize==fp_f) (void)fp_cvt(&v1,fp_f,fp_t);      /* cvt to DBLE(v1) */
    v2 = ReadOp(v2addr, opsize);
    if (opsize==fp_f) (void)fp_cvt(&v2,fp_f,fp_t);      /* cvt to DBLE(v2) */
    (void)fp_exec(042,ACCUM, v1, v2);                   /* ACCU := v1 * v2 */
    (void)fp_exec(006,&dot,dot,NOP);                    /* dot := dot + v1*v2 */
    v1addr += ix1;                                      /* forward to next array elements */
    v2addr += ix2;
    }
if (opsize==fp_f)
    (void)vis_trunc(&dot,dot);                          /* truncate to SNGL(sumnrm) */
WriteOp(daddr, dot, opsize);                            /* write result */
}


static void vis_movswp(OPS op, OPSIZE opsize, t_bool doswp)
{
OP v1,v2;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 v1addr = op[0].word;
int16 ix1 = INT16(op[1].word) * delta;
uint32 v2addr = op[2].word;
int16 ix2 = INT16(op[3].word) * delta;
int16 i,n = INT16(op[4].word);

if (n <= 0) return;
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    v2 = ReadOp(v2addr, opsize);
    WriteOp(v2addr, v1, opsize);                        /* v2 := v1 */
    if (doswp) WriteOp(v1addr, v2, opsize);             /* v1 := v2 */
    v1addr += ix1;                                      /* forward to next array elements */
    v2addr += ix2;
    }
}


/* implementation of VIS RTE-IVB EMA support
 * .ERES microcode routine, resolves only EMA addresses
 *  Call:
 *    .OCT 101474B
 *    DEF RTN          error return (rtn), good return is rtn+1
 *    DEF DUMMY        dummy argument for compatibility with .EMAP
 *    DEF TABLE[,I]    array declaration (dtbl)
 *    DEF A(N)[,I]     actual subscripts (atbl)
 *    DEF A(N-1)[,I]
 *    ...
 *    DEF A(2)[,I]
 *    DEF A(1)[,I]
 *  RTN EQU *          error return A="20", B="EM"
 *  RTN+1 EQU *+1      good return B=logical address
 *
 *  TABLE DEC #        # dimensions
 *        DEC -L(N)
 *        DEC D(N-1)
 *        DEC -L(N-1)  lower bound (n-1)st dim
 *        DEC D(N-2)   (n-2)st dim
 *        ...
 *        DEC D(1)     1st dim
 *        DEC -L(1)    lower bound 1st dim
 *        DEC #        # words/element
 *        OFFSET 1     EMA Low
 *        OFFSET 2     EMA High
 */

static t_stat vis_eres(HP_WORD *rtn,uint32 dtbl,uint32 atbl)
{
uint32 sum;
if (ema_resolve(dtbl,atbl,&sum)) {                      /* calculate subscript */
    AR = sum & 0xffff;
    BR = sum >> 16;
    if (!(BR & D16_SIGN)) {                             /* no overflow? */
        (*rtn)++;                                       /* return via good exit */
        return SCPE_OK;
    }
}
AR = 0x3230;                                            /* error condition: */
BR = 0x454d;                                            /* AR = '20', BR = 'EM' */
return SCPE_OK;                                         /* return via unmodified rtn */
}


/* implementation of VIS RTE-IVB EMA support
 * .ESEG microcode routine
 *  Call:
 *    LDA FIRST        first map to set
 *    LDB N            # of maps to set
 *    .OCT 101475B/105475B
 *    DEF RTN          ptr to return
 *    DEF TABLE        map table
 *    RTN EQU *        error return A="21", B="EM"
 *    RTN+1 EQU *+1    good return B=logical address
 *
 * load maps FIRST to FIRST+N from TABLE, with FIRST = FIRST + LOG_START MSEG
 * update map table in base page.  Set LOG_START MSEG=0 if opcode==105475
 */

static t_stat vis_eseg(HP_WORD* rtn, uint32 tbl)
{
uint32 xidex,eqt,idext0,idext1;
uint32 msegsz,phys,msegn,last,emasz,pg0,pg1,pg,i,lp;

if ((BR & D16_SIGN) || BR==0) goto em21;                /* #maps not positive? */
xidex = ReadU (idx);                                    /* read ID extension */
if (xidex==0) goto em21;
idext0 = ReadWA(xidex+0);                               /* get 1st word idext */
msegsz = idext0 & 037;                                  /* S7 MSEG size */
WriteS (xidex + 0, idext0 | 0100000);                   /* enforce nonstd MSEG */
idext1 = ReadWA(xidex+1);                               /* get 2nd word idext */
phys = idext1 & 01777;                                  /* S5 phys start of EMA */
msegn = (idext1 >> 11) & 037;                           /* S9 get logical start MSEG# */
if (IR & 04000) {                                       /* opcode == 105475? (.VPRG) */
    msegn = 0;                                          /* log start = 0 */
    msegsz = 32;                                        /* size = full range */
}
last = AR-1 + BR;                                       /* last page */
if (last > msegsz) goto em21;                           /* too many? error */
eqt = ReadU (xeqt);
emasz = (ReadWA(eqt+28) & 01777) - 1;                   /* S6 EMA size in pages */

/* locations 1740...1777 of user base page contain the map entries we need.
 * They are normally hidden by BP fence, therefore they have to be accessed by
 * another fence-less map register.  uCode uses #1 temporarily */
pg0 = meu_read_map(User_Map,0);                         /* read map #0 */
pg1 = meu_read_map(User_Map,1);                         /* save map #1 */
meu_write_map(User_Map,1,pg0);                          /* copy #0 into reg #1 */
lp = AR + msegn;                                        /* first */
for (i=0; i<BR; i++) {                                  /* loop over N entries */
    pg = ReadW(tbl++);                                  /* get value from table */
    if ((pg & D16_SIGN) || pg > emasz) pg |= 0140000;   /* write protect if outside */
    pg += phys;                                         /* adjust into EMA page range */
    WriteU (umaps + lp + i, pg);                        /* copy pg to user map */
/* printf("MAP val %oB to reg %d (addr=%oB)\n",pg,lp+i,umaps+lp+i); */
    meu_write_map(User_Map, lp+i, pg);                  /* set DMS reg */
}
meu_write_map(User_Map,1,pg1);                          /* restore map #1 */
O = 0;                                                  /* clear overflow */
(*rtn)++;                                               /* return via good exit */
return SCPE_OK;

em21:
AR = 0x3231;                                            /* error condition: */
BR = 0x454d;                                            /* AR = '21', BR = 'EM' */
return SCPE_OK;                                         /* return via unmodified rtn */
}


/* implementation of VIS RTE-IVB EMA support
 * .VSET microcode routine
 *  Call:
 *    .OCT 101476B
 *    DEF RTN          return address
 *    DEF VIN          input vector
 *    DEF VOUT         output vector
 *    DEF MAPS
 *    OCT #SCALARS
 *    OCT #VECTORS
 *    OCT K            1024/(#words/element)
 *    RTN EQU *        error return  (B,A) = "VI22"
 *    RTN+1 EQU *+1    hard return, A = K/IMAX
 *    RTN+2 EQU *+2    easy return, A = 0, B = 2* #VCTRS
 */

static t_stat vis_vset(HP_WORD* rtn, OPS op)
{
HP_WORD vin     = op[0].word;                            /* S1 */
HP_WORD vout    = op[1].word;                            /* S2 */
HP_WORD maps    = op[2].word;                            /* S3 */
HP_WORD scalars = op[3].word;                            /* S4 */
HP_WORD vectors = op[4].word;                            /* S5 */
HP_WORD k       = op[5].word;                            /* S6 */
uint32  imax    = 0;                                     /* imax S11*/
uint32  xidex, idext1, mseg, addr, i;
t_bool negflag = FALSE;

for (i=0; i<scalars; i++) {                             /* copy scalars */
    XR = ReadW(vin++);
    WriteW(vout++, XR);
}
xidex = ReadU (idx);                                    /* get ID extension */
if (xidex==0) goto vi22;                                /* NO EMA? error */
idext1 = ReadWA(xidex+1);
mseg = (idext1 >> 1) & MSEG_MASK;                       /* S9 get logical start MSEG */

for (i=0; i<vectors; i++) {                             /* copy vector addresses */
    MR = ReadW(vin++);
    cpu_resolve_indirects (FALSE);                      /* resolve indirects (uninterruptible) */
    addr = ReadW(MR) & 0177777;                         /* LSB */
    addr |= (ReadW(MR+1)<<16);                          /* MSB, build address */
    WriteW(vout++, mseg + (addr & 01777));              /* build and write log addr of vector */
    addr = (addr >> 10) & 0xffff;                       /* get page */
    WriteW(maps++, addr);                               /* save page# */
    WriteW(maps++, addr+1);                             /* save next page# as well */

    MR = ReadW(vin++);                                  /* get index into Y */
    cpu_resolve_indirects (FALSE);                      /* resolve indirects (uninterruptible) */
    YR = ReadW(MR);                                     /* get index value */
    WriteW(vout++, MR);                                 /* copy address of index */
    if (YR & D16_SIGN) {                                /* index is negative */
         negflag = TRUE;                                /* mark a negative index (HARD) */
         YR = NEG16 (YR);                               /* make index positive */
    }
    if (imax < YR) imax = YR;                           /* set maximum index */
    mseg += 04000;                                      /* incr mseg address by 2 more pages */
}
MR = ReadW(vin);                                        /* get N index into Y */
cpu_resolve_indirects (FALSE);                          /* resolve indirects (uninterruptible) */
YR = ReadW(MR);
WriteW(vout++, MR); vin++;                              /* copy address of N */

if (imax==0) goto easy;                                 /* easy case */
AR = (HP_WORD) (k / imax); AR++;                        /* calculate K/IMAX */
if (negflag) goto hard;                                 /* had a negative index? */
if (YR > AR) goto hard;

easy:
(*rtn)++;                                               /* return via exit 2 */
AR = 0;

hard:
(*rtn)++;                                               /* return via exit 1 */
BR = 2 * op[4].word;                                    /* B = 2* vectors */
return SCPE_OK;

vi22:                                                   /* error condition */
  AR=0x3232;                                            /* AR = '22' */
  BR=0x5649;                                            /* BR = 'VI' */
  return SCPE_OK;                                       /* return via unmodified e->rtn */
}



/* SIGNAL local utility routines */


/* complex addition helper */

static void sig_caddsub(uint16 addsub,OPS op)
{
OP a,b,c,d,p1,p2;

a = ReadOp(RE(op[1].word), fp_f);                       /* read 1st op */
b = ReadOp(IM(op[1].word), fp_f);
c = ReadOp(RE(op[2].word), fp_f);                       /* read 2nd op */
d = ReadOp(IM(op[2].word), fp_f);
(void)fp_exec(addsub,&p1, a, c);                        /* add real */
(void)fp_exec(addsub,&p2, b, d);                        /* add imag */
WriteOp(RE(op[0].word), p1, fp_f);                      /* write result */
WriteOp(IM(op[0].word), p2, fp_f);                      /* write result */
}


/* butterfly operation helper */
/*
 * v(k)-------->o-->o----> v(k)
 *               \ /
 *                x
 *               / \
 * v(k+N/2)---->o-->o----> v(k+N/2)
 *           Wn   -1
 *
 */

static void sig_btrfy(uint32 re,uint32 im,OP wr,OP wi,uint32 k, uint32 n2)
{
OP p1,p2,p3,p4;
OP v1r = ReadOp(re+k, fp_f);                            /* read v1 */
OP v1i = ReadOp(im+k, fp_f);
OP v2r = ReadOp(re+k+n2, fp_f);                         /* read v2 */
OP v2i = ReadOp(im+k+n2, fp_f);

/* (p1,p2) := cmul(w,v2) */
(void)fp_exec(040, &p1, wr, v2r);                       /* S7,8 p1 := wr*v2r */
(void)fp_exec(040, ACCUM, wi, v2i);                     /* ACCUM := wi*v2i */
(void)fp_exec(024, &p1, p1, NOP);                       /* S7,S8 p1 := wr*v2r-wi*v2i ==real(w*v2) */
(void)fp_exec(040, &p2, wi, v2r);                       /* S9,10 p2 := wi*v2r */
(void)fp_exec(040, ACCUM, wr, v2i);                     /* ACCUM := wr*v2i */
(void)fp_exec(004, &p2, p2, NOP);                       /* S9,10 p2 := wi*v2r+wr*v2i ==imag(w*v2) */
/* v2 := v1 - (p1,p2) */
(void)fp_exec(020, &p3, v1r, p1);                       /* v2r := v1r-real(w*v2) */
(void)fp_exec(020, &p4, v1i, p2);                       /* v2i := v1i-imag(w*v2) */
WriteOp(re+k+n2, p3, fp_f);                             /* write v2r */
WriteOp(im+k+n2, p4, fp_f);                             /* write v2i */
/* v1 := v1 + (p1,p2) */
(void)fp_exec(0, &p3, v1r, p1);                         /* v1r := v1r+real(w*v2) */
(void)fp_exec(0, &p4, v1i, p2);                         /* v1i := v1i+imag(w*v2) */
WriteOp(re+k, p3, fp_f);                                /* write v1r */
WriteOp(im+k, p4, fp_f);                                /* write v1i */
O = 0;
}


/* helper for bit reversal
 * idx is 0-based already */

static void sig_bitrev(uint32 re,uint32 im, uint32 idx, uint32 log2n, int sz)
{
uint32 i, org=idx, rev = 0;
OP v1r,v1i,v2r,v2i;

for (i=0; i<log2n; i++) {                               /* swap bits of idx */
    rev = (rev<<1) | (org & 1);                         /* into rev */
    org >>= 1;
}

if (rev < idx) return;                                  /* avoid swapping same pair twice in loop */

idx *= sz;                                              /* adjust for element size */
rev *= sz;                                              /* (REAL*4 vs COMPLEX*8) */

v1r = ReadOp(re+idx, fp_f);                             /* read 1st element */
v1i = ReadOp(im+idx, fp_f);
v2r = ReadOp(re+rev, fp_f);                             /* read 2nd element */
v2i = ReadOp(im+rev, fp_f);
WriteOp(re+idx, v2r, fp_f);                             /* swap elements */
WriteOp(im+idx, v2i, fp_f);
WriteOp(re+rev, v1r, fp_f);
WriteOp(im+rev, v1i, fp_f);
}


/* helper for PRSCR/UNSCR */

static OP sig_scadd(uint16 oper,t_bool addh, OP a, OP b)
{
OP r;
static const OP plus_half = { { 0040000, 0000000 } };   /* DEC +0.5 */

(void)fp_exec(oper,&r,a,b);                             /* calculate r := a +/- b */
if (addh) (void)fp_exec(044,&r,plus_half,NOP);          /* if addh set, multiply by 0.5 */
return r;
}


/* complex multiply helper */

static void sig_cmul(OP *r, OP *i, OP a, OP b, OP c, OP d)
{
OP p;
(void)fp_exec(040, &p , a, c);                          /* p := ac */
(void)fp_exec(040, ACCUM, b, d);                        /* ACCUM := bd */
(void)fp_exec(024, r, p , NOP);                         /* real := ac-bd */
(void)fp_exec(040, &p, a, d);                           /* p := ad */
(void)fp_exec(040, ACCUM, b, c);                        /* ACCUM := bc */
(void)fp_exec(004, i, p, NOP);                          /* imag := ad+bc */
}

#endif                                                  /* int64 conditional */
