/* hp3000_cpu_cis.c: HP 32234A COBOL II Instruction Set simulator

   Copyright (c) 2016-2017, J. David Bryan

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

   22-Apr-17    JDB     Corrected the significance flag setting in "edit"
   29-Dec-16    JDB     Disabled interrupt checks pending test generation
   19-Oct-16    JDB     Passes the COBOL-II firmware "A" diagnostic (D441A)
   06-Oct-16    JDB     Passes the COBOL-II firmware "B" diagnostic (D442A)
   21-Sep-16    JDB     Created

   References:
     - Machine Instruction Set Reference Manual
         (30000-90022, June 1984)
     - HP 3000 Series 64/68/70 Computer Systems Microcode Manual
         (30140-90045, October 1986)


   This module implements the HP 32234A COBOL II Extended Instruction
   Set firmware, also known as the Language Extension Instructions.  The set
   contains these instructions in the firmware extension range 020460-020477:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 0   0   0 | S |  ALGN
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 0   0   1 | S |  ABSN
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   0   0 | B |  EDIT
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   0   1 | B |  CMPS
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   0   0 |  XBR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   0   1 |  PARC
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   0 |  ENDP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  CMPT
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 0   0   0   1   1 | B |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  TCCS
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 0   0   1 | > | = | < |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  CVND
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 0   1 |  sign op  | S |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  LDW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 1   0   0   0   0 | S |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  LDDW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 1   0   0   0   1 | S |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  TR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 1   0   0   1   0 | B |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  ABSD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 1   0   0   1   1 | S |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 | 1   1   1   1 |  NEGD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0   0   0   0   0   0   0 | 1   0   1   0   0 | S |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = S-decrement
     B = Bank select (0/1 = PB-relative/DB-relative)

   The PARC, ENDP, and XBR instructions implement the COBOL "PERFORM" statement.
   ABSD and NEGD manipulate packed decimal numbers.  ALGN, ABSN, EDIT, and CVND
   manipulate external decimal numbers.  The LDW and LDDW instructions load
   single and double-words, respectively, from byte-aligned addresses.  TCCS
   tests the status register for a specific condition code and loads the logical
   result; operation is similar to the Bcc instruction, except that the result
   is explicit rather than implied by the branch.  TR translates a string using
   a mapping table.  CMPS and CMPT compare two strings (with translation for
   CMPT) and set the condition code accordingly.  CMPS is similar to CMPB,
   except that a shorter string is blank-padded for comparison.


   Packed decimal (also known as COMPUTATIONAL-3, BCD, and binary-coded decimal)
   numbers contain from 1 to 28 digits that are stored in pairs in successive
   memory bytes in this format:

       0   1   2   3   4   5   6   7
     +---+---+---+---+---+---+---+---+
     | unused/digit  |     digit     |
     +---+---+---+---+---+---+---+---+
     |     digit     |     digit     |
     +---+---+---+---+---+---+---+---+

     [...]

     +---+---+---+---+---+---+---+---+
     |     digit     |     digit     |
     +---+---+---+---+---+---+---+---+
     |     digit     |     sign      |
     +---+---+---+---+---+---+---+---+

   The sign is always located in the lower four bits of the final byte, so
   numbers with an even number of digits will not use the upper four bits of the
   first byte.  Digits are represented by four-bit values from 0-9 (i.e., in
   Binary-Coded Decimal or BCD), with the most-significant digit first and the
   least-significant digit last.  The sign is given by one of these encodings:

     1100 - the number is positive
     1101 - the number is negative
     1111 - the number is unsigned

   All other values are interpreted as meaning the number is positive; however,
   only one of the three values above is generated.

   Numbers may begin at an even or odd byte address, and the size of the number
   (in digits) may be even or odd, so there are four possible cases of packing
   into 16-bit words:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | unused/digit  |     digit     |     digit     |     digit     |  addr even
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     digit     |     digit     |     digit     |     sign      |  size even
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | unused/digit  |     digit     |     digit     |     digit     |  addr even
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     digit     |     sign      |              ...              |  size odd
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |              ...              | unused/digit  |     digit     |  addr odd
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     digit     |     digit     |     digit     |     sign      |  size ?...
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |              ...              | unused/digit  |     digit     |  addr odd
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |     digit     |     sign      |              ...              |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   External decimal (also known as DISPLAY, numeric display, and ASCII) values
   contain contain from 1 to 28 digits that are stored as ASCII characters in
   successive memory bytes in this format:

       0   1   2   3   4   5   6   7
     +---+---+---+---+---+---+---+---+
     |             digit             |
     +---+---+---+---+---+---+---+---+
     |             digit             |
     +---+---+---+---+---+---+---+---+

     [...]

     +---+---+---+---+---+---+---+---+
     |             digit             |
     +---+---+---+---+---+---+---+---+
     |         digit and sign        |
     +---+---+---+---+---+---+---+---+

   The number begins with the most-significant digit.  The sign is combined with
   the least-significant digit in the final byte.  Each digit except the LSD
   must be in the ASCII range "0" through "9".  Leading blanks are allowed, and
   the entire number may be blank, but blanks within a number are not.  The
   least-signifiant digit and sign are represented by either:

     "0" and "1" through "9" for an unsigned number
     "{" and "A" through "I" for a positive number
     "}" and "J" through "R" for a negative number

   Numbers may begin at an even or odd byte address, and the size of the number
   (in digits) may be even or odd, so there are four possible cases of packing
   into 16-bit words:

     - the number completely fills the words
     - the number has an unused leading byte in the first word
     - the number has an unused trailing byte in the last word
     - the number has an unused byte at each end

   Any unused bytes are not part of the number and are not disturbed.

   The EDIT instruction moves bytes from a source string to a target string
   under the control of a subprogram indicated by a PB- or DB-relative address.
   The subprogram consists of 8-bit instructions, each followed by zero or more
   8-bit operands.  The subprogram ends with a TE (Terminate Edit) instruction.

   The supported instructions are:

       0   1   2   3   4   5   6   7
     +---+---+---+---+---+---+---+---+
     | 0   0   0   0 | imm. operand  |  MC - move characters
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 0   0   0   1 | imm. operand  |  MA - move alphabetics
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 0   0   1   0 | imm. operand  |  MN - move numerics
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 0   0   1   1 | imm. operand  |  MNS - move numerics suppressed
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 0   1   0   0 | imm. operand  |  MFL - move numerics with floating insertion
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 0   1   0   1 | imm. operand  |  IC - insert character
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+
     |      character to insert      |
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 0   1   1   0 | imm. operand  |  ICS - insert character suppressed
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+
     |      character to insert      |
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 0   1   1   1 | imm. operand  |  ICI - insert characters immediate
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+
     |     character 1 to insert     |
     +---+---+---+---+---+---+---+---+
                    ...
     +---+---+---+---+---+---+---+---+
   { |     character n to insert     | }  (present if operand > 1)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   0   0   0 | imm. operand  |  ICSI - insert characters suppressed immediate
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+
     |     character 1 to insert     |
     +---+---+---+---+---+---+---+---+
                    ...
     +---+---+---+---+---+---+---+---+
   { |     character n to insert     | }  (present if operand > 1)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   0   0   1 | imm. operand  |  BRIS - branch if significance
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   0   1   0 | imm. operand  |  SUFT - subtract from target
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   0   1   1 | imm. operand  |  SUFS - subtract from source
     +---+---+---+---+---+---+---+---+
   { |       extended operand        | }  (present if imm. operand = 0)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   0   1   1 | imm. operand  |  ICP - insert character punctuation
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   0   0 | imm. operand  |  ICPS - insert character punctuation suppressed
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   0 | imm. operand  |  IS - insert character on sign
     +---+---+---+---+---+---+---+---+
     |   positive sign character 1   |
     +---+---+---+---+---+---+---+---+
                    ...
     +---+---+---+---+---+---+---+---+
   { |     character n to insert     | }  (present if operand > 1)
     +---+---+---+---+---+---+---+---+
     |   negative sign character 1   |
     +---+---+---+---+---+---+---+---+
                    ...
     +---+---+---+---+---+---+---+---+
   { |     character n to insert     | }  (present if operand > 1)
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   0   0   0 |  TE - terminate edit
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   0   0   1 |  ENDF - end floating point insertion
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   0   1   0 |  SST1 - set significance to 1
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   0   1   1 |  SST0 - set significance to 0
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   1   0   0 |  MDWO - move digit with overpunch
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   1   0   1 |  SFC - set fill character
     +---+---+---+---+---+---+---+---+
     |        fill character         |
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   1   1   0 |  SFLC - set float character
     +---+---+---+---+---+---+---+---+
     | + float char  | - float char  |
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 0   1   1   1 |  DFLC - define float character
     +---+---+---+---+---+---+---+---+
     |   positive float character    |
     +---+---+---+---+---+---+---+---+
     |   negative float character    |
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 1   0   0   0 |  SETC - set loop count
     +---+---+---+---+---+---+---+---+
     |           loop count          |
     +---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+
     | 1   1   1   1 | 1   0   0   1 |  DBNZ - decrement loop count and branch
     +---+---+---+---+---+---+---+---+
     |      branch displacement      |
     +---+---+---+---+---+---+---+---+

   The EDIT instruction is interruptible after each subprogram command.  The TR,
   CMPS, and CMPT instructions are interruptible after each byte processed.  The
   remaining instructions execute to completion.

   Two user traps may be taken by these instructions if the T bit is on in the
   status register:

     - Word Count Overflow (parameter 17)
     - Invalid ASCII Digit (parameter 14)

   The Word Count Overflow trap is taken when an instruction is given an
   external decimal number with more than 28 digits.  The Invalid ASCII Digit
   trap occurs when an external decimal number contains characters other than
   "0"-"9", a "+" or "-" sign in any position other than the first or last
   position, blanks other than leading blanks, an invalid overpunch character,
   or an overpunch character in any position other than the last digit.

   Enabling the OPND debug flag traces the instruction operands, including the
   subprogram operations of the EDIT instruction.


   Implementation notes:

    1. In several cases noted below, the hardware microcode implementations
       differ from the descriptions in the Machine Instruction Set manual.
       Also, the comments in the microcode source sometimes do not correctly
       described the microcode actions.  In all cases of conflict, the simulator
       follows the microcode implementation.

    2. The Machine Instruction Set manual references trap conditions (Invalid
       Alphabetic Character, Invalid Operand Length, Invalid Source Character
       Count, and Invalid Digit Count) that are not defined in the Series II/III
       System Reference Manual.  Examination of the microcode indicates that
       only the Invalid ASCII Digit and Word Count Overflow traps are taken.

    3. Target operand tracing is not done if a trap occurred, as the result will
       be invalid.

    4. The calls to "cpu_interrupt_pending" are currently stubbed out pending
       testing of interrupted instruction exits and reentries.  The COBOL II
       firmware diagnostics do not test interruptibility, so tests have to be
       written before correct action is assured.  This will be addressed in a
       future release.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_mem.h"



/* Disable intra-instruction interrupt checking pending testing */

#define cpu_interrupt_pending(x)    FALSE       /* TEMPORARY ONLY */


/* Program constants */

#define MAX_DIGITS          28                  /* maximum number of decimal digits accepted */


/* Packed decimal constants */

#define SIGN_MASK           0360u               /* 8-bit numeric sign mask */
#define SIGN_TEST_MASK      0017u               /* 8-bit numeric sign test mask */

#define SIGN_PLUS           0014u               /* 1100 -> the number is positive */
#define SIGN_MINUS          0015u               /* 1101 -> the number is negative */
#define SIGN_UNSIGNED       0017u               /* 1111 -> the number is unsigned */

#define IS_NEG(v)           (((v) & SIGN_TEST_MASK) == SIGN_MINUS)


/* External decimal constants */

typedef enum {                                  /* location and type of the numeric sign character */
    Leading_Separate   = 0,                     /*   plus or minus prefix */
    Trailing_Separate  = 1,                     /*   plus or minus suffix */
    Leading_Overpunch  = 2,                     /*   overpunched first character */
    Trailing_Overpunch = 3,                     /*   overpunched last character */
    Absolute           = 4                      /*   no sign character */
    } DISPLAY_MODE;


typedef enum {                                  /* numeric sign values */
    Negative,
    Unsigned,
    Positive
    } NUMERIC_SIGN;

static uint8 overpunch [3] [10] = {                         /* sign overpunches, indexed by NUMERIC_SIGN */
    { '}', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R' },   /*   Negative */
    { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' },   /*   Unsigned */
    { '{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I' }    /*   Positive */
    };


/* Operand printer function */

typedef char * (*OP_PRINT) (uint32 byte_address, uint32 byte_length);


/* CIS local utility routine declarations */

static void   branch_external            (HP_WORD segment, HP_WORD offset);
static uint32 strip_overpunch            (uint8 *byte, NUMERIC_SIGN *sign);
static uint32 convert                    (HP_WORD sba, HP_WORD tba, HP_WORD count, DISPLAY_MODE mode);
static t_bool edit                       (t_stat *status, uint32 *trap);
static t_bool compare                    (BYTE_ACCESS *source, BYTE_ACCESS *target, BYTE_ACCESS *table, t_stat *status);
static void   fprint_operands            (BYTE_ACCESS *source, BYTE_ACCESS *target, uint32 trap);
static void   fprint_translated_operands (BYTE_ACCESS *source, BYTE_ACCESS *target, BYTE_ACCESS *table);
static void   fprint_operand             (BYTE_ACCESS *op, char *label, OP_PRINT operand_printer);



/* CIS global routines */


/* Execute a CIS operation.

   This routine is called to execute the COBOL II instruction currently in the
   CIR.  The instruction format is:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 | 0   0   1   1 |  CIS opcode   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Instructions 020460 - 020476 are decoded directly.  Code 020477 introduces a
   set of two-word instructions; the second word decodes the operation.  Codes
   020464 through 020467 and code 020477 with the second word less than 000006
   or greater than 000051 are unimplemented.

   Entry is with four TOS registers preloaded (this is done for all firmware
   extension instructions).  Therefore, no SR preload is needed here.
   Instructions that provide option bits to leave addresses on the stack do not
   modify those addresses during instruction execution.

   If an invalid digit or digit count is detected, a microcode abort occurs.  If
   the T (trap) bit is not set in the status register, the O (overflow) bit is
   set and execution continues.  If the T bit is set, the O bit is not set and
   the trap is taken.  In either case, the stack is popped according to the
   instruction.


   Implementation notes:

    1. If a two-word instruction has a valid first word (020477) but an invalid
       second word, the CIR must retain the first-word value and P must be
       incremented over the second word when the Unimplemented Instruction trap
       is taken.

    2. The MICRO_ABORT macro does a longjmp to the microcode abort handler in
       the sim_instr routine.  If the user trap bit (T-bit) in the status word
       is set, the routine clears the overflow bit (O-bit) and invokes the trap
       handler.  If the T-bit is clear, the routine sets the O-bit and continues
       with the next instruction.  Therefore, we do not need to check the T-bit
       here and can simply do an unconditional MICRO_ABORT if a trap is
       indicated.

    3. Regarding error traps, the "Instruction Commentary" for the Language
       Extension Instructions section of the Machine Instruction Set manual
       says, in part:

         If the User Traps bit (STA.2) is not set, the Overflow bit (STA.4) is
         set, the stack is popped in accordance with the instruction, and
         execution continues with the following instruction.  If the Traps bit
         is set, the Overflow bit is not set, the stack is not popped, and a
         trap to the Traps segment, segment 1, is taken.

       However, the Series 6x microcode for, e.g., the ALGN instruction does a
       "JSZ TRPR" after unconditionally popping the stack (page 334).  TRPR
       checks STA.2 and either starts trap processing if it is set or sets
       Overflow and proceeds to the next instruction if it is clear.  So the
       stack is popped in either case.  This implementation follows this
       behavior.

    4. In the comments for the ABSD/NEGD instructions, the Series 6x microcode
       manual says, "Extract the last two bits of the sign..." and then "If the
       sign bits = 01 (the sign was negative => D), then...", implying that only
       the lower two bits are tested.  The microcode itself, however, tests all
       four bits and assumes a negative value only if they equal 1101.  This
       behavior agrees with the EIS commentary in the Machine Instruction Set
       manual, which says that all sign values other than those defined as plus,
       minus, and unsigned, are treated as plus.  This implementation follows
       this behavior.
*/

t_stat cpu_cis_op (void)
{
BYTE_ACCESS  source, target, table;
NUMERIC_SIGN sign;
HP_WORD      segment, offset, sign_cntl;
HP_WORD      source_rba, table_rba;
char         label [64];
uint8        byte;
uint8        source_length, source_lead, source_fraction;
uint8        target_length, target_lead, target_fraction;
uint32       opcode;
t_bool       store, zero_fill;
uint32       trap   = trap_None;
t_stat       status = SCPE_OK;

opcode = FMEXSUBOP (CIR);                               /* get the opcode from the instruction */

switch (opcode) {                                       /* dispatch the opcode */

    case 000:                                           /* ALGN (O; INV DIG, WC OVF, STOV, STUN, BNDV) */
    case 001:
        source_length   = LOWER_BYTE (RA);              /* get the source digit count */
        source_fraction = UPPER_BYTE (RA);              /*   and the source fraction count */

        target_length   = LOWER_BYTE (RC);              /* get the target digit count */
        target_fraction = UPPER_BYTE (RC);              /*   and the target fraction count */

        if (source_fraction > source_length             /* if the source fraction count is invalid */
          || source_length > MAX_DIGITS                 /*   or the source digit count is too large */
          || target_fraction > target_length            /*     or the target fraction count is invalid */
          || target_length > MAX_DIGITS)                /*       or the target digit count is too large */
            trap = trap_Word_Count_Overflow;            /*         then trap for a count overflow */

        else if (source_length > 0 && target_length > 0) {  /* otherwise if there is an alignment to do */
            sign = Unsigned;                                /*   then assume the source is unsigned */
            store = TRUE;                                   /* enable storing */
            zero_fill = TRUE;                               /*   and zero fill of the target */

            source_lead = source_length - source_fraction;  /* get the counts of the leading digits */
            target_lead = target_length - target_fraction;  /*   of the source and target numbers */

            mem_init_byte (&source, data_checked, &RB, source_length);  /* set up byte accessors */
            mem_init_byte (&target, data_checked, &RD, target_length);  /*   for the source and target strings */

            while (source_length > 0 || target_length > 0) {    /* while digits remain to be aligned */
                if (source_lead < target_lead                   /* if the target has more leading digits */
                  || source_length == 0)                        /*   or there are no more source digits */
                    byte = '0';                                 /*     then transfer a zero */

                else {                                      /* otherwise */
                    store = (source_lead == target_lead);   /*   don't store if still more leading source digits */

                    byte = mem_read_byte (&source);         /* get a source digit */
                    source_length = source_length - 1;      /*   and count it */

                    if (source_length == 0) {                   /* if this is the last source digit */
                        trap = strip_overpunch (&byte, &sign);  /*   then strip the sign from the digit */

                        if (trap != trap_None)              /* if the overpunch was invalid */
                            break;                          /*   then abandon the transfer */
                        }

                    if (source_lead > 0)                    /* if this is a leading digit */
                        source_lead = source_lead - 1;      /*   then count it */

                    if (byte >= '0' && byte <= '9')         /* if it is numeric */
                        zero_fill = FALSE;                  /*   then turn zero-filling off */

                    else if (byte == ' ' && zero_fill)      /* otherwise if it's a blank and zero-filling is on */
                        byte = '0';                         /*   then fill */

                    else {                                  /* otherwise */
                        trap = trap_Invalid_ASCII_Digit;    /*   the digit is invalid */
                        break;                              /*     so abandon the transfer */
                        }
                    }

                if (store && target_length > 0) {               /* if storing and target space is available */
                    if (target_length == 1)                     /*   then if this is the last byte to store */
                        byte = overpunch [sign] [byte - '0'];   /*     then overpunch with the source sign */

                    mem_write_byte (&target, byte);         /* store the target digit */
                    target_length = target_length - 1;      /*   and count it */

                    if (target_lead > 0)                    /* if this is a leading digit */
                        target_lead = target_lead - 1;      /*   then count it */
                    }
                }                                           /* continue the alignment loop */


            mem_update_byte (&target);                      /* update the final target byte */

            if (DPRINTING (cpu_dev, DEB_MOPND)) {
                sprintf (label, "source fraction %d length", source_fraction);
                fprint_operand (&source, label, &fmt_byte_operand);

                if (trap == trap_None) {
                    sprintf (label, "target fraction %d length", target_fraction);
                    fprint_operand (&target, label, &fmt_byte_operand);
                    }
                }
            }

        if (CIR & CIS_SDEC_MASK)                        /* if the S-decrement bit is set in the instruction */
            SR = 0;                                     /*   then pop all four operands */

        else {                                          /* otherwise */
            cpu_pop ();                                 /*   pop all */
            cpu_pop ();                                 /*     of the operands */
            cpu_pop ();                                 /*       except the target address */
            }

        if (trap == trap_None)                          /* if the instruction succeeded */
            STA &= ~STATUS_O;                           /*   then clear overflow status */
        else                                            /* otherwise */
            MICRO_ABORT (trap);                         /*   abort with the indicated trap */
        break;


    case 002:                                           /* ABSN (CCA, O; INV DIG, WC OVF, STOV, STUN, BNDV) */
    case 003:
        if (RA > MAX_DIGITS)                            /* if the digit count is too large */
            trap = trap_Word_Count_Overflow;            /*   then trap for a count overflow */

        else if (RA > 0) {                              /* otherwise if there are digits to process */
            source_rba = RB;                            /*   then use a working source byte address pointer */
            mem_init_byte (&source, data_checked,       /*     and set up a byte accessor for the string */
                           &source_rba, RA);

            if (DPRINTING (cpu_dev, DEB_MOPND))
                fprint_operand (&source, "source", &fmt_byte_operand);

            byte = mem_read_byte (&source);             /* get the first source digit */

            while (byte == ' ' && RA > 0) {             /* while there are leading blanks */
                mem_modify_byte (&source, '0');         /*   replace each blank with a zero digit */
                RA = RA - 1;                            /*     and count it */

                if (RA > 0)                             /* if there are more digits */
                    byte = mem_read_byte (&source);     /*   then get the next one */
                }

            while (RA > 1 && byte >= '0' && byte <= '9') {  /* validate the digits */
                byte = mem_read_byte (&source);             /*   by getting and checking */
                RA = RA - 1;                                /*     each digit until the last */
                }

            if (RA == 1) {                              /* if this is the last digit */
                trap = strip_overpunch (&byte, &sign);  /*   then strip the sign from the digit */

                if (trap == trap_None) {                /* if the overpunch was valid */
                    if (sign == Negative)               /*   then if the number was negative */
                        SET_CCL;                        /*     then set the less-than condition code */
                    else                                /*   otherwise a positive or zero number */
                        SET_CCG;                        /*     sets the greater-than condition code */

                    mem_modify_byte (&source, byte);    /* rewrite the digit without the sign overpunch */
                    }
                }

            else if (RA > 0)                            /* otherwise if we've abandoned the validation */
                trap = trap_Invalid_ASCII_Digit;        /*   then trap for an invalid digit */

            mem_post_byte (&source);                    /* post the last byte written */

            if (DPRINTING (cpu_dev, DEB_MOPND) && trap == trap_None)
                fprint_operand (&source, "target", &fmt_byte_operand);
            }

        cpu_pop ();                                     /* pop the digit count */

        if (CIR & CIS_SDEC_MASK)                        /* if the S-decrement bit is set in the instruction */
            cpu_pop ();                                 /*   then pop the address as well */

        if (trap == trap_None)                          /* if the instruction succeeded */
            STA &= ~STATUS_O;                           /*   then clear overflow status */
        else                                            /* otherwise */
            MICRO_ABORT (trap);                         /*   abort with the indicated trap */
        break;


    case 010:                                           /* EDIT (O; INV DIG, WC OVF, STUN, BNDV) */
    case 011:
        if (edit (&status, &trap)) {                    /* process the edit subprogram; if no interrupt intervened */
            SR = 0;                                     /*   then pop all four values from the stack */

            if (trap != trap_None)                      /* if a trap occurred */
                MICRO_ABORT (trap);                     /*   then take it now */
            }
        break;


    case 012:                                           /* CMPS (CCx; STOV, STUN, BNDV) */
    case 013:
        mem_init_byte (&source, data_checked, &RB, RA); /* set up a byte accessor for the first operand */

        if (CIR & CIS_DB_FLAG)                                  /* if the second operand is in the data segment */
            mem_init_byte (&target, data_checked, &RD, RC);     /*   then set up a DB byte accessor */
        else                                                    /* otherwise */
            mem_init_byte (&target, program_checked, &RD, RC);  /*   set up a PB byte accessor */

        if (compare (&source, &target, NULL, &status)) {    /* compare the strings; if no interrupt intervened */
            SR = 0;                                         /*   then pop all four values from the stack */

            if (DPRINTING (cpu_dev, DEB_MOPND))
                fprint_operands (&source, &target, trap);
            }
        break;


    case 014:                                           /* XBR (none; STUN, BNDV, CSTV, MODE, ABS CST, TRACE) */
        segment = RA;                                   /* get the segment number */
        offset = RB;                                    /*   and PB-relative offset of the target */

        cpu_pop ();                                     /* pop the operands */
        cpu_pop ();

        branch_external (segment, offset);              /* branch to the target location */
        break;


    case 015:                                           /* PARC (none; STOV, STUN, BNDV, CSTV, MODE, ABS CST, TRACE) */
        segment = RB;                                   /* get the segment number */
        offset = RC;                                    /*   and the PB-relative offset of the target */

        RB = STA;                                       /* replace the segment number with the current status */
        RC = P - 1 - PB & R_MASK;                       /*   and the target with the PB-relative return address */

        branch_external (segment, offset);              /* branch to the target location */
        break;


    case 016:                                           /* ENDP (none; STUN, BNDV, CSTV, MODE, ABS CST, TRACE) */
        if (RA == RB) {                                 /* if the paragraph numbers are equal */
            SR = 0;                                     /*   then pop all of the parameters */
            branch_external (RC, RD);                   /*     and return to the caller */
            }

        else                                            /* otherwise the paragraph numbers are unequal */
            cpu_pop ();                                 /*   so pop the current paragraph number and continue */
        break;


    case 017:                                           /* double-word instructions */
        opcode = NIR;                                   /* get the operation code from the second word */

        cpu_read_memory (fetch, P, &NIR);               /* load the next instruction */
        P = P + 1 & R_MASK;                             /*   and point to the following instruction */

        switch (opcode) {                               /* dispatch the second instruction word */

            case 006:                                   /* CMPT (CCx; STOV, STUN, BNDV) */
            case 007:
                cpu_read_memory (stack, SM, &table_rba);    /* get the byte address of the translation table */

                mem_init_byte (&source, data_checked, &RB, RA); /* set up a byte accessor for the first operand */

                if (opcode & CIS_DB_FLAG)                               /* if the second operand is in the data segment */
                    mem_init_byte (&target, data_checked, &RD, RC);     /*   then set up a DB byte accessor */
                else                                                    /* otherwise */
                    mem_init_byte (&target, program_checked, &RD, RC);  /*   set up a PB byte accessor */

                mem_init_byte (&table, data, &table_rba, 0);    /* set up a byte accessor for the translation table */

                if (compare (&source, &target, &table, &status)) {  /* compare the strings; if no interrupt intervened */
                    SR = 0;                                         /*   then pop the first four parameters */
                    cpu_pop ();                                     /*     and then the fifth parameter */

                    if (DPRINTING (cpu_dev, DEB_MOPND))
                        fprint_translated_operands (&source, &target, &table);
                    }
                break;


            case 010:                                   /* TCCS (Test condition code and set, bits 13-15 options) */
            case 011:
            case 012:
            case 013:
            case 014:
            case 015:
            case 016:
            case 017:
                cpu_push ();                            /* push the stack down */

                if ((STA & STATUS_CC_MASK) != STATUS_CCI        /* if the condition code is not invalid */
                  && TO_CCF (STA) & opcode << TCCS_CCF_SHIFT)   /*   and the test succeeds */
                    RA = D16_UMAX;                              /*     then set the TOS to TRUE */
                else                                            /* otherwise */
                    RA = 0;                                     /*   set the TOS to FALSE */
                break;


            case 020:                                   /* CVND (O; INV DIG, WC OVF, STOV, STUN, BNDV) */
            case 021:
            case 022:
            case 023:
            case 024:
            case 025:
            case 026:
            case 027:
            case 030:
            case 031:
            case 032:
            case 033:
            case 034:
            case 035:
            case 036:
            case 037:
                if (RA > MAX_DIGITS)                    /* if the digit count is too large */
                    trap = trap_Word_Count_Overflow;    /*   then trap for a count overflow */

                else if (RA > 0) {                      /* otherwise if there are digits to convert */
                    sign_cntl = (opcode & CVND_SC_MASK) /*   then get the sign control code */
                                   >> CVND_SC_SHIFT;

                    if (sign_cntl > Absolute)           /* if the code is one of the unnormalized values */
                        sign_cntl = Absolute;           /*   then normalize it  */

                    trap = convert (RB, RC, RA, sign_cntl); /* convert the number as directed */
                    }

                cpu_pop ();                             /* pop the source character count */
                cpu_pop ();                             /*   and source byte address */

                if (opcode & CIS_SDEC_MASK)             /* if the S-decrement bit is set in the instruction */
                    cpu_pop ();                         /*   then pop the target byte address also */

                if (trap == trap_None)                  /* if the instruction succeeded */
                    STA &= ~STATUS_O;                   /*   then clear overflow status */
                else                                    /* otherwise */
                    MICRO_ABORT (trap);                 /*   abort with the indicated trap */
                break;


            case 040:                                   /* LDW (none; STOV, STUN, BNDV) */
            case 041:
                source_rba = RA;                        /* use a working source byte address pointer */

                if ((opcode & CIS_SDEC_MASK) == 0)      /* if the S-decrement bit is clear */
                    cpu_push ();                        /*   then push the stack down for the return value */

                mem_init_byte (&source, data_checked, &source_rba, 2);  /* set up a byte accessor for the characters */

                byte = mem_read_byte (&source);                 /* get the first byte */
                RA = TO_WORD (byte, mem_read_byte (&source));   /*   and the second byte and merge them */

                if (DPRINTING (cpu_dev, DEB_MOPND))
                    fprint_operand (&source, "source", &fmt_byte_operand);
                break;


            case 042:                                   /* LDDW (none; STOV, STUN, BNDV) */
            case 043:
                source_rba = RA;                        /* use a working source byte address pointer */

                if ((opcode & CIS_SDEC_MASK) == 0)      /* if the S-decrement bit is clear */
                    cpu_push ();                        /*   then push the stack down for the return value */

                cpu_push ();                            /* push again for the two-word return value */

                mem_init_byte (&source, data_checked, &source_rba, 4);  /* set up a byte accessor for the characters */

                byte = mem_read_byte (&source);                 /* get the first byte */
                RA = TO_WORD (byte, mem_read_byte (&source));   /*   and the second byte and merge them */

                byte = mem_read_byte (&source);                 /* get the third byte */
                RB = TO_WORD (byte, mem_read_byte (&source));   /*   and the fourth byte and merge them */

                if (DPRINTING (cpu_dev, DEB_MOPND))
                    fprint_operand (&source, "source", &fmt_byte_operand);
                break;


            case 044:                                   /* TR (none; STUN, STOV, BNDV) */
            case 045:
                if (RA > 0) {                                       /* if there are bytes to translate */
                    mem_init_byte (&source, data_checked, &RC, RA); /*   then set up byte accessors */
                    mem_init_byte (&target, data_checked, &RB, RA); /*     for the source and target strings */

                    if (opcode & CIS_DB_FLAG)                       /* if the table is in the data segment */
                        mem_init_byte (&table, data, &RD, 0);       /*   then set up a DB byte accessor */
                    else                                            /* otherwise */
                        mem_init_byte (&table, program, &RD, 0);    /*   set up a PB byte accessor */

                    while (RA > 0) {                            /* while there are bytes to translate */
                        byte = mem_read_byte (&source);         /* get the next byte */
                        byte = mem_lookup_byte (&table, byte);  /* look up the translated value */
                        mem_write_byte (&target, byte);         /*   and write it to the target */

                        RA = RA - 1;                            /* update the byte count (cannot underflow) */

                        if (cpu_interrupt_pending (&status)) {  /* if an interrupt is pending */
                            mem_update_byte (&target);          /*   then update the last word written */
                            return status;                      /*     and return with an interrupt set up or an error */
                            }
                        }

                    mem_update_byte (&target);                  /* update the final target byte */

                    if (DPRINTING (cpu_dev, DEB_MOPND))
                        fprint_operands (&source, &target, trap);
                    }

                SR = 0;                                         /* pop all four values from the stack */
                break;


            case 046:                                   /* ABSD (CCA, O; WC OVF, STOV, STUN, BNDV) */
            case 047:
            case 050:                                   /* NEGD (CCA, O; WC OVF, STOV, STUN, BNDV) */
            case 051:
                if (RA > MAX_DIGITS)                    /* if the digit count is too large */
                    trap = trap_Word_Count_Overflow;    /*   then trap for a count overflow */

                else {                                      /* otherwise */
                    mem_init_byte (&source, data, &RB, RA); /*   set up a byte accessor for the operand */

                    source_rba = RA / 2 + RB;                       /* index to the trailing sign digit */
                    mem_init_byte (&target, data, &source_rba, 0);  /*   and set up a byte accessor for the sign */

                    if (DPRINTING (cpu_dev, DEB_MOPND))
                        fprint_operand (&source, "source", &fmt_bcd_operand);

                    byte = mem_read_byte (&target);     /* get the sign byte */

                    if (opcode < 050) {                 /* if this is an ABSD instruction */
                        if (IS_NEG (byte))              /*   then if the number is negative */
                            SET_CCL;                    /*     then set the less-than condition code */
                        else                            /*   otherwise a positive or zero number */
                            SET_CCG;                    /*     sets the greater-than condition code */

                        byte |= SIGN_UNSIGNED;          /* change the number to unsigned */
                        }

                    else                                            /* otherwise this is a NEGD instruction */
                        if (IS_NEG (byte)) {                        /*   so if the number is negative */
                            byte = byte & SIGN_MASK | SIGN_PLUS;    /*     then make the number positive */
                            SET_CCG;                                /*       and set the greater-than condition code */
                            }

                        else {                                      /*   otherwise the number is positive */
                            byte = byte & SIGN_MASK | SIGN_MINUS;   /*     so make it negative */
                            SET_CCL;                                /*       and set the less-than condition code */
                            }

                    mem_modify_byte (&target, byte);    /* rewrite the digit */
                    mem_post_byte (&target);            /*   and post it */

                    if (DPRINTING (cpu_dev, DEB_MOPND))
                        fprint_operand (&source, "target", &fmt_bcd_operand);
                    }

                cpu_pop ();                             /* pop the digit count */

                if (opcode & CIS_SDEC_MASK)             /* if the S-decrement bit is set in the instruction */
                    cpu_pop ();                         /*   then pop the address as well */

                if (trap == trap_None)                  /* if the instruction succeeded */
                    STA &= ~STATUS_O;                   /*   then clear overflow status */
                else                                    /* otherwise */
                    MICRO_ABORT (trap);                 /*   abort with the indicated trap */
                break;


            default:
                status = STOP_UNIMPL;                   /* the second instruction word is unimplemented */
            }
        break;                                          /* end of the double-word instructions */


    default:
        status = STOP_UNIMPL;                           /* the firmware extension instruction is unimplemented */
    }

return status;                                          /* return the execution status */
}



/* CIS local utility routine declarations */


/* Execute a branch to a location in a specified segment.

   If the target segment is the same as the current segment, as indicated in the
   status register, then a local label is used.  Otherwise, an external label is
   used that specifies the target segment entry 0 of the STT, which specifies
   the start of the segment.  The target is then set up in the same manner as a
   procedure call, with the program counter adjusted by the target offset.

   The procedure setup may abort, rather than returning, if a trap prevents the
   setup from succeeding.
*/

static void branch_external (HP_WORD segment, HP_WORD offset)
{
HP_WORD label;

if (STATUS_CS (segment) == STATUS_CS (STA))             /* if the target segment is current */
    label = LABEL_LOCAL;                                /*   then use a local label */
else                                                    /* otherwise */
    label = LABEL_EXTERNAL                              /*   use an external label */
              | TO_LABEL (STATUS_CS (segment), 0);      /*     that specifies the target segment and STT 0 */

cpu_call_procedure (label, offset);                     /* set up the segment as for a procedure call */

return;                                                 /* return with the branch completed */
}


/* Strip the sign from an overpunched digit.

   If the supplied digit includes an overpunched sign, it is stripped and
   returned separately, along with the stripped digit; trap_None is returned to
   indicate success.  If the digit is not a valid overpunch character, then
   trap_Invalid_ASCII_Digit is returned to indicate failure.


   Implementation notes:

    1. A set of direct tests is faster than looping through the overpunch table
       to search for the matching digit.  Faster still would be a direct 256-way
       reverse overpunch lookup, but the gain is not significant.
*/

static uint32 strip_overpunch (uint8 *byte, NUMERIC_SIGN *sign)
{
if (*byte == '{') {                                     /* if the digit is a zero with positive overpunch */
    *byte = '0';                                        /*   then strip the overpunch */
    *sign = Positive;                                   /*     and set the returned sign positive */
    }

else if (*byte >= 'A' && *byte <= 'I') {                /* otherwise if the digit is a positive overpunch */
    *byte = *byte - 'A' + '1';                          /*   then strip the overpunch to yield a 1-9 value */
    *sign = Positive;                                   /*     and set the returned sign positive */
    }

else if (*byte == '}') {                                /* otherwise if the digit is a zero with negative overpunch */
    *byte = '0';                                        /*   then strip the overpunch */
    *sign = Negative;                                   /*     and set the returned sign negative */
    }

else if (*byte >= 'J' && *byte <= 'R') {                /* otherwise if the digit is a negative overpunch */
    *byte = *byte - 'J' + '1';                          /*   then strip the overpunch to yield a 1-9 value */
    *sign = Negative;                                   /*     and set the returned sign negative */
    }

else if (*byte >= '0' && *byte <= '9')                  /* otherwise if the digit is not overpunched */
    *sign = Unsigned;                                   /*   then simply set the return as unsigned */

else                                                    /* otherwise the digit is not a valid overpunch */
    return trap_Invalid_ASCII_Digit;                    /*   so return an Invalid Digit trap as failure */

return trap_None;                                       /* return no trap for successful stripping */
}


/* Convert a numeric display string.

   This routine converts a numeric display string to an external decimal number.
   A display string consists of the ASCII characters "0" to "9" and optional
   leading spaces.  The sign may be omitted (unsigned), a separate leading or
   trailing "+" or "-" sign, or an integral leading or trailing overpunch.  The
   result is an external decimal number, consisting of "0" to "9" digits with a
   trailing sign overpunch.  The routine implements the CVND instruction.

   The "sba" parameter is the DB-relative byte address of the source string,
   "tba" is the DB-relative byte address of the target string, "count" is the
   number of source characters (including the sign character, if separate), and
   "mode" is the sign display mode of the source (leading separate, trailing
   separate, leading overpunch, trailing overpunch, or unsigned).  The count is
   always non-zero on entry.

   The routine validates all of the source characters, even if no conversion is
   needed, and returns trap_Invalid_ASCII_Digit if validation fails.  trap_None
   is returned if the conversion succeeds.

   The implementation starts by setting indices for the location of the sign.
   Separate indices for separate and overpunched signs are kept, with the unused
   index set to a value beyond the string so that it never matches.  If the
   input has separate sign mode and only one character, then that character is
   the sign, and a zero value is implied.  Leading blanks are zero-filled until
   the first numeric digit (if the mode is leading overpunch, then the first
   digit is numeric, so leading blanks are not permitted).


   Implementation notes:

    1. The "last_digit" variable is only used in Trailing_Separate mode when
       there is at least one character other than the sign.  Consequently, it
       will never be used before it is set.  However, the compiler cannot detect
       this and will warn erroneously that it could be used uninitialized.
       Therefore, it is set (redundantly) before the conversion loop entry.
*/

static uint32 convert (HP_WORD sba, HP_WORD tba, HP_WORD count, DISPLAY_MODE mode)
{
BYTE_ACCESS  source, target;
NUMERIC_SIGN sign;
HP_WORD      separate_index, overpunch_index;
uint8        byte, last_digit;
uint32       trap = trap_None;
t_bool       zero_fill = TRUE;
t_bool       bare_sign = FALSE;

switch (mode) {                                         /* set up the sign flag and indices */

    case Leading_Separate:
        separate_index = count;                         /* the first character is the separate sign */
        overpunch_index = MAX_DIGITS + 1;               /*   and no character is the overpunched sign */

        bare_sign = (count == 1);                       /* only one character implies a bare sign */
        break;

    case Trailing_Separate:
        separate_index = 1;                             /* the last character is the separate sign */
        overpunch_index = MAX_DIGITS + 1;               /*   and no character is the overpunched sign */

        bare_sign = (count == 1);                       /* only one character implies a bare sign */
        break;

    case Leading_Overpunch:
        overpunch_index = count;                        /* the first character is the overpunched sign */
        separate_index = MAX_DIGITS + 1;                /*   and no character is the separate sign */
        break;

    case Trailing_Overpunch:
        overpunch_index = 1;                            /* the last character is the overpunched sign */
        separate_index = MAX_DIGITS + 1;                /*   and no character is the separate sign */
        break;

    case Absolute:
    default:                                            /* quiet "potentially uninitialized local variable" warnings */
        separate_index = MAX_DIGITS + 1;                /* no character is the overpunched sign */
        overpunch_index = MAX_DIGITS + 1;               /*   and no character is the separate sign */
        break;
    }

mem_init_byte (&source, data_checked, &sba, count);     /* set up byte accessors */
mem_init_byte (&target, data_checked, &tba, count);     /*   for the source and target strings */

sign = Unsigned;                                        /* assume that the source is unsigned */
byte = 0;                                               /* shut up incorrect "may be used uninitialized" warning */

while (count > 0) {                                     /* while there are characters to convert */
    last_digit = byte;                                  /*   then save any previous character */
    byte = mem_read_byte (&source);                     /*     and get the next source character */

    if (count == separate_index) {                      /* If this is the separate sign character */
        if (byte == '+' || byte == ' ')                 /*   then a plus or blank */
            sign = Positive;                            /*     indicates a positive number */

        else if (byte == '-')                           /*   otherwise a minus */
            sign = Negative;                            /*     indicates a negative number */

        else {                                          /*   otherwise */
            trap = trap_Invalid_ASCII_Digit;            /*     the character is not a valid sign */
            break;                                      /*       so abandon the conversion */
            }

        if (bare_sign)                                      /* if this is the only character */
            byte = '0';                                     /*   then supply a zero for overpunching */

        else {                                              /* otherwise */
            if (mode == Trailing_Separate) {                /*   if this is the trailing sign */
                byte = overpunch [sign] [last_digit - '0']; /*     then overpunch the last numeric digit with the sign */
                mem_modify_byte (&target, byte);            /*       and update it */
                }

            count = count - 1;                              /* count the separate sign character */
            continue;                                       /*   and continue with the next character */
            }
        }

    else if (count == overpunch_index && byte != ' ') { /* otherwise if this is the non-blank overpunched sign */
        trap = strip_overpunch (&byte, &sign);          /*   then strip the overpunch and set the sign */

        if (trap == trap_None)                          /* if the overpunch was valid */
            zero_fill = FALSE;                          /*   then turn zero-filling off */
        else                                            /* otherwise the overpunch was not valid */
            break;                                      /*   so abandon the conversion */
        }

    if (byte >= '0' && byte <= '9')                     /* if the character is numeric */
        zero_fill = FALSE;                              /*   then turn zero-filling off */

    else if (byte == ' ' && zero_fill)                  /* otherwise if it's a blank and zero-filling is on */
        byte = '0';                                     /*   then fill */

    else {                                              /* otherwise */
        trap = trap_Invalid_ASCII_Digit;                /*   the digit is invalid */
        break;                                          /*     so abandon the conversion */
        }

    if (count == 1 && mode != Absolute)                 /* if this is the last character and the value is signed */
        byte = overpunch [sign] [byte - '0'];           /*   then overpunch with the sign */

    mem_write_byte (&target, byte);                     /* store it in the target string */

    count = count - 1;                                  /* count the character */
    }                                                   /*   and continue */

mem_update_byte (&target);                              /* update the final target byte */

if (DPRINTING (cpu_dev, DEB_MOPND))
    fprint_operands (&source, &target, trap);

return trap;                                            /* return the trap condition */
}


/* Edit a number into a formatted picture.

   This routine moves an external decimal number to a target buffer under the
   control of an editing subprogram.  The subprogram consists of one or more
   editing operations.  The routine is interruptible between operations.

   On entry, the TOS registers and the condition code in the STA register are
   set as follows:

     RA = 0 on initial entry, or 177777 on reentry after an interrupt
     RB = the source byte address (DB-relative)
     RC = the target byte address (DB-relative)
     RD = the subprogram byte address (PB or DB-relative)
     CC = the sign of the source number

   On return, the "status" parameter is set to the SCP status returned by the
   interrupt test, and "trap" is set to trap_Invalid_ASCII_Digit if an operation
   encountered an invalid digit, or trap_None if the edit succeeded.  The
   routine returns TRUE if the operation ran to completion, or FALSE if the
   operation was interrupted and should be resumed.

   If an interrupt is detected between operations, two words are pushed onto the
   stack before the interrupt handler is called.  These words hold the current
   significance trigger (a flag indicating that a significant digit has been
   seen or that zero filling should occur), loop count, float character, and
   fill character, in this format:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   1   1   1   1   1   1   1   1   1   1   1   1   1   1 |  TOS
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S | 0   0   0   0   0   0   0 |          loop count           |  TOS - 1
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |        fill character         |        float character        |  TOS - 2
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     S = significance trigger (0/1)

   The loop count/significance trigger word is written over the zero that
   resides at the TOS on instruction entry, and then the fill/float character
   word and a word of all ones are pushed.  The source, target, and subprogram
   byte addresses are updated during execution, so they retain their modified
   values on reentry.  When reentry is detected, the fill, float, and count
   values are reestablished, and two words are popped off the stack to restore
   the initial entry conditions.

   If operand tracing is enabled, each subprogram operation is printed in this
   format:

     >>CPU  opnd: 00.136024  000000    IS 6,"AAAAAA","BBBBBB"
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~~~~~~~~~~~~~~~~~
                  |    |       |         |
                  |    |       |         +-- subprogram operation and operands (if any)
                  |    |       +------------ octal loop counter value
                  |    +-------------------- octal subprogram address (effective address)
                  +------------------------- octal bank (PBANK, DBANK, or SBANK)

   ...and source and target operands are printed in this format:

     >>CPU  opnd: 00.045177  000467    source 3,"ABC"
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~~~~~~~~~
                  |    |       |         |
                  |    |       |         +-- byte operand label, length, and value
                  |    |       +------------ octal relative byte offset from base register
                  |    +-------------------- octal address (effective address)
                  +------------------------- octal bank (PBANK, DBANK, or SBANK)


   Implementation notes:

    1. The Machine Instruction Set manual says that the DBNZ ("decrement loop
       count and branch") operation adds the displacement operand.  However,
       the microcode manual shows that the displacement is actually subtracted.
       The simulator follows the microcode implementation.

    2. The BRIS ("branch if significance") operation adds the displacement to
       the address of the displacement byte.  After reading the displacement
       byte, the address is incremented to the next location, so the operation
       subtracts 1 before adding the displacement value.

    3. The significance trigger is represented by the "filling" flag; its value
       is the opposite of the trigger, i.e., FALSE if a significant digit has
       been seen, TRUE if all leading digits have been zeros, because that more
       clearly indicates that it controls leading zero suppression and
       replacement.
*/

static t_bool edit (t_stat *status, uint32 *trap)
{
BYTE_ACCESS  source, target, prog;
ACCESS_CLASS class;
HP_WORD      bank;
char         fill_char, float_char;
uint8        byte, opcode, operand, count;
uint32       loop_count;
t_bool       filling = TRUE;                            /* TRUE if zero-filling is enabled */
t_bool       terminate = FALSE;                         /* TRUE if the operation loop is ending */

*status = SCPE_OK;                                      /* initialize the return status */
*trap   = trap_None;                                    /*   and trap condition */

if (RA != 0) {                                          /* if this is a reentry after an interrupt */
    filling    = ((RB & D16_SIGN) == 0);                /*   then reset the zero-filling flag */
    loop_count = LOWER_BYTE (RB);                       /*     and the loop counter */
    fill_char  = UPPER_BYTE (RC);                       /* reset the fill */
    float_char = LOWER_BYTE (RC);                       /*   and float characters */

    cpu_pop ();                                         /* pop the extra words */
    cpu_pop ();                                         /*   added to save the context */
    }

else {                                                  /* otherwise this is an initial entry */
    filling    = TRUE;                                  /*   so set the zero-filling flag */
    loop_count = 0;                                     /*     and clear the loop counter */
    fill_char  = ' ';                                   /* set the fill */
    float_char = '$';                                   /*   and float character defaults */
    }

RA = D16_UMAX;                                          /* set the in-process flag in case of microcode abort */
STA &= ~STATUS_O;                                       /* clear overflow status */

if (CIR & CIS_DB_FLAG) {                                /* if the subprogram is in the data segment */
    class = data;                                       /*   then set up for data reads */
    bank = DBANK;                                       /*     and use DBANK for traces */
    }

else {                                                  /* otherwise it's in the program segment */
    class = program;                                    /*   so set up for program reads */
    bank = PBANK;                                       /*     and use PBANK for traces */
    }

mem_init_byte (&source, data,  &RB, 0);                 /* set up byte accessors */
mem_init_byte (&target, data,  &RC, 0);                 /*   for the source and target strings */
mem_init_byte (&prog,   class, &RD, 0);                 /*     and the subprogram */

do {                                                    /* process operations while "terminate" is FALSE */
    operand = mem_read_byte (&prog);                    /* get the next operation */

    if (DPRINTING (cpu_dev, DEB_MOPND)) {               /* if operand tracing is enabled */
        hp_debug (&cpu_dev, DEB_MOPND, BOV_FORMAT "  ", /*   then print the current subprogram address */
                  bank, prog.word_address, loop_count); /*     and loop count as octal values */

        fprint_edit (sim_deb, NULL, 0,                  /* print the operation mnemonic */
                     prog.initial_byte_address          /*   at the current physical byte address */
                       + prog.count - 1);

        fputc ('\n', sim_deb);                          /* end the trace with a newline */
        }

    opcode  = UPPER_HALF (operand);                     /* split the opcode */
    operand = LOWER_HALF (operand);                     /*   from the immediate operand */

    if (operand == 0 && opcode < 014)                   /* if this is an extended operand */
        operand = mem_read_byte (&prog);                /*   then read the full value from the next byte */


    switch (opcode) {                                   /* dispatch on the opcode */

        case 000:                                       /* MC - move characters */
            while (operand > 0) {                       /* while there are characters to move */
                byte = mem_read_byte (&source);         /*   get the next byte */

                mem_write_byte (&target, byte);         /* move it to the target */
                operand = operand - 1;                  /*   and count it */
                }
            break;


        case 001:                                       /* MA - move alphabetics */
            while (operand > 0) {                       /* while there are characters to move */
                byte = mem_read_byte (&source);         /*   get the next byte */

                if (byte >= 'A' && byte <= 'Z'          /* if the character is an uppercase letter */
                  || byte >= 'a' && byte <= 'z'         /*   or a lowercase letter */
                  || byte == ' ') {                     /*     or a space */
                    mem_write_byte (&target, byte);     /*       then move it to the target */
                    operand = operand - 1;              /*         and count it */
                    }

                else {                                  /* otherwise */
                    *trap = trap_Invalid_ASCII_Digit;   /*   the character is not a valid alphabetic */
                    terminate = TRUE;                   /*     so abandon the subprogram */
                    break;                              /*       and the move */
                    }
                }
            break;


        case 002:                                       /* MN - move numerics */
            while (operand > 0) {                       /* while there are characters to move */
                byte = mem_read_byte (&source);         /*   get the next byte */

                if (byte == ' ' && filling)             /* if it's a blank and zero-filling is on */
                    byte = '0';                         /*   then fill */

                else if (byte >= '0' && byte <= '9')    /* otherwise if the character is a digit */
                    filling = FALSE;                    /*   then turn zero-filling off */

                else {                                  /* otherwise */
                    *trap = trap_Invalid_ASCII_Digit;   /*   the character is not a valid numeric */
                    terminate = TRUE;                   /*     so abandon the subprogram */
                    break;                              /*       and the move */
                    }

                mem_write_byte (&target, byte);         /* move the character to the target */
                operand = operand - 1;                  /*   and count it */
                }
            break;


        case 003:                                       /* MNS - move numerics suppressed */
            while (operand > 0) {                       /* while there are characters to move */
                byte = mem_read_byte (&source);         /*   get the next byte */

                if (filling                             /* if zero-filling is on */
                  && (byte == ' ' || byte == '0'))      /*   and it's a blank or zero */
                    byte = fill_char;                   /*     then substitute the fill character */

                else if (byte >= '0' && byte <= '9')    /* otherwise if the character is a digit */
                    filling = FALSE;                    /*   then turn zero-filling off */

                else {                                  /* otherwise */
                    *trap = trap_Invalid_ASCII_Digit;   /*   the character is not a valid numeric */
                    terminate = TRUE;                   /*     so abandon the subprogram */
                    break;                              /*       and the move */
                    }

                mem_write_byte (&target, byte);         /* move the character to the target */
                operand = operand - 1;                  /*   and count it */
                }
            break;


        case 004:                                       /* MFL - move numerics with floating insertion */
            while (operand > 0) {                       /* while there are characters to move */
                byte = mem_read_byte (&source);         /*   get the next byte */

                if (filling                             /* if zero-filling is on */
                  && (byte == ' ' || byte == '0'))      /*   and it's a blank or zero */
                    byte = fill_char;                   /*     then substitute the fill character */

                else if (byte >= '0' && byte <= '9') {  /* otherwise if the character is a digit */
                    if (filling) {                      /*   then if zero-filling is still on */
                        filling = FALSE;                /*     then turn it off */

                        mem_write_byte (&target, float_char);   /* insert the float character before the digit */
                        }
                    }

                else {                                  /* otherwise */
                    *trap = trap_Invalid_ASCII_Digit;   /*   the character is not a valid numeric */
                    terminate = TRUE;                   /*     so abandon the subprogram */
                    break;                              /*       and the move */
                    }

                mem_write_byte (&target, byte);         /* move the character to the target */
                operand = operand - 1;                  /*   and count it */
                }
            break;


        case 005:                                       /* IC - insert character */
            byte = mem_read_byte (&prog);               /* get the insertion character */

            while (operand > 0) {                       /* while there are characters to insert */
                mem_write_byte (&target, byte);         /*   copy the character to the target */
                operand = operand - 1;                  /*     and count it */
                }
            break;


        case 006:                                       /* ICS - insert character suppressed */
            byte = mem_read_byte (&prog);               /* get the insertion character */

            if (filling)                                /* if zero-filling is on */
                byte = fill_char;                       /*   then substitute the fill character */

            while (operand > 0) {                       /* while there are characters to insert */
                mem_write_byte (&target, byte);         /*   copy the character to the target */
                operand = operand - 1;                  /*     and count it */
                }
            break;


        case 007:                                       /* ICI - insert characters immediate */
            while (operand > 0) {                       /* while there are characters to insert */
                byte = mem_read_byte (&prog);           /*   get the next byte */

                mem_write_byte (&target, byte);         /* move it to the target */
                operand = operand - 1;                  /*   and count it */
                }
            break;


        case 010:                                       /* ICSI - insert characters suppressed immediate */
            while (operand > 0) {                       /* while there are characters to insert */
                byte = mem_read_byte (&prog);           /*   get the next byte */

                if (filling)                            /* if zero-filling is on */
                    byte = fill_char;                   /*   then substitute the fill character */

                mem_write_byte (&target, byte);         /* copy the character to the target */
                operand = operand - 1;                  /*   and count it */
                }
            break;


        case 011:                                       /* BRIS - branch if significance */
            if (filling == FALSE) {                     /* if zero-filling is off */
                RD = RD - 1 + SEXT8 (operand) & R_MASK; /*   then add the signed displacement to the offset */
                mem_set_byte (&prog);                   /*     and reset the subprogram accessor */
                }
            break;


        case 012:                                       /* SUFT - subtract from target */
            mem_update_byte (&target);                  /* update the final target byte if needed */
            RC = RC - SEXT8 (operand) & R_MASK;         /* subtract the signed displacement from the offset */
            mem_set_byte (&target);                     /*   and reset the target accessor */
            break;


        case 013:                                       /* SUFS - subtract from source */
            RB = RB - SEXT8 (operand) & R_MASK;         /* subtract the signed displacement from the offset */
            mem_set_byte (&source);                     /*   and reset the source accessor */
            break;


        case 014:                                       /* ICP - insert character punctuation */
            mem_write_byte (&target, operand + ' ');    /* write the punctuation character to the target */
            break;


        case 015:                                       /* ICPS - insert character punctuation suppressed */
            if (filling)                                /* if zero-filling is on */
                byte = fill_char;                       /*   then substitute the fill character */
            else                                        /* otherwise */
                byte = operand + ' ';                   /*   use the supplied punctuation character */

            mem_write_byte (&target, byte);             /* write the character to the target */
            break;


        case 016:                                       /* IS - insert characters on sign */
            if (operand > 0) {                          /* if the character strings are present */
                count = operand;                        /*   then get the character count */

                if ((STA & STATUS_CC_MASK) == STATUS_CCL) { /* if the sign is negative */
                    RD = RD + count & R_MASK;               /*   then index to the negative character string */
                    mem_set_byte (&prog);                   /*     and reset the subprogram accessor */
                    }

                while (count > 0) {                     /* while there are characters to copy */
                    byte = mem_read_byte (&prog);       /*   get the next byte */

                    mem_write_byte (&target, byte);     /* copy the character to the target */
                    count = count - 1;                  /*   and count it */
                    }

                if ((STA & STATUS_CC_MASK) != STATUS_CCL) { /* if the sign is positive */
                    RD = RD + operand & R_MASK;             /*   then skip over the negative character string */
                    mem_set_byte (&prog);                   /*     and reset the subprogram accessor */
                    }
                }
            break;


        case 017:                                       /* two-byte operations */
            switch (operand) {                          /* dispatch on the second operation byte */

                case 000:                               /* TE - terminate edit */
                    terminate = TRUE;                   /* terminate the subprogram */
                    break;


                case 001:                                       /* ENDF - end floating point insertion */
                    if (filling)                                /* if zero-filling is on */
                        mem_write_byte (&target, float_char);   /*   then insert the float character */
                    break;


                case 002:                               /* SST1 - set significance to 1 */
                    filling = FALSE;                    /* set zero-filling off */
                    break;


                case 003:                               /* SST0 - set significance to 0 */
                    filling = TRUE;                     /* set zero-filling on */
                    break;


                case 004:                               /* MDWO - move digit with overpunch */
                    byte = mem_read_byte (&source);     /* get the digit */

                    if (byte == ' ' && filling)         /* if it's a blank and zero-filling is on */
                        byte = '0';                     /*   then fill */

                    else if (byte < '0' || byte > '9') {    /* otherwise if the character is not a digit */
                        *trap = trap_Invalid_ASCII_Digit;   /*   then the it is not a valid number */
                        terminate = TRUE;                   /*     so abandon the subprogram */
                        break;
                        }

                    if ((STA & STATUS_CC_MASK) == STATUS_CCL)       /* if the number is negative */
                        byte = overpunch [Negative] [byte - '0'];   /*   then overpunch a minus sign */
                    else                                            /* otherwise */
                        byte = overpunch [Positive] [byte - '0'];   /*   overpunch a plus sign */

                    mem_write_byte (&target, byte);     /* write the overpunched character to the target */
                    break;


                case 005:                               /* SFC - set fill character */
                    fill_char = mem_read_byte (&prog);  /* set the fill character from the next byte */
                    break;


                case 006:                                       /* SFLC - set float character on sign */
                    byte = mem_read_byte (&prog);               /* get the float characters */

                    if ((STA & STATUS_CC_MASK) == STATUS_CCL)   /* if the number is negative */
                        float_char = LOWER_HALF (byte) + ' ';   /*   then use the negative float character */
                    else                                        /* otherwise */
                        float_char = UPPER_HALF (byte) + ' ';   /*   use the positive float character */
                    break;


                case 007:                                       /* DFLC - define float character on sign */
                    float_char = mem_read_byte (&prog);         /* set the positive float character */

                    if ((STA & STATUS_CC_MASK) == STATUS_CCL)   /* if the number is negative */
                        float_char = mem_read_byte (&prog);     /*   then set the negative float character */
                    else                                        /* otherwise */
                        mem_read_byte (&prog);                  /*   skip over it */
                    break;


                case 010:                               /* SETC - set loop count */
                    loop_count = mem_read_byte (&prog); /* get the new loop count */
                    break;


                case 011:                                       /* DBNZ - decrement loop count and branch */
                    byte = mem_read_byte (&prog);               /* get the displacement */

                    loop_count = loop_count - 1 & D8_MASK;      /* decrement the loop count modulo 256 */

                    if (loop_count > 0) {                       /* if the count is not zero */
                        RD = RD - 1 - SEXT8 (byte) & R_MASK;    /*   then subtract the signed displacement from the offset */
                        mem_set_byte (&prog);                   /*     and reset the subprogram accessor */
                        }
                    break;


                default:                                /* invalid two-word opcodes */
                    break;                              /*   are ignored */
                }                                       /* end of two-word dispatcher */
            break;

        }                                               /* all cases are handled */


    if (terminate == FALSE                              /* if the subprogram is continuing */
      && cpu_interrupt_pending (status)) {              /*   and an interrupt is pending */
        cpu_push ();                                    /*     then push the stack down twice */
        cpu_push ();                                    /*       to save the subprogram execution state */

        RA = D16_UMAX;                                  /* set the resumption flag */

        RB = (filling ? 0 : D16_SIGN) | loop_count;     /* save the significance trigger and loop count */
        RC = TO_WORD (fill_char, float_char);           /* save the fill and float characters */

        mem_update_byte (&target);                      /* update the last word written */
        return FALSE;                                   /*   and return with an interrupt set up or a status error */
        }
    }

while (terminate == FALSE);                             /* continue subprogram execution until terminated */


mem_update_byte (&target);                              /* update the final target byte */

if (DPRINTING (cpu_dev, DEB_MOPND)) {                   /* if operand tracing is enabled */
    mem_set_byte (&source);                             /*   then reset the source and target accessors */
    mem_set_byte (&target);                             /*     to finalize the operand extents */

    if (source.length > 0)                                      /* if the source operand was used */
        fprint_operands (&source, &target, *trap);              /*   then print both source and target operands */
    else                                                        /* otherwise */
        fprint_operand (&target, "target", &fmt_byte_operand);  /*   print just the target operand */
    }

RA = 0;                                                 /* clear the resumption flag */

return TRUE;                                            /* return with completion status */
}


/* Compare two padded byte strings.

   This routine compares two byte strings with optional translation and sets the
   condition code in the status register to indicate the result.  Starting with
   the first, successive pairs of bytes are compared; if the string lengths are
   unequal, the shorter string is padded with blanks.  The comparison stops when
   the bytes are unequal or the end of the strings is reached.  The condition
   code is CCG if the source byte is greater than the target byte, CCL if the
   source byte is less than the target byte, or CCE if the strings are equal or
   both strings are of zero-length.

   On entry, the RA and RC TOS registers contain the lengths of the source and
   target strings, and the "source" and "target" parameters point at the byte
   accessors for the source and target strings, respectively.  The "table"
   parameter either points at a 256-byte translation table or is NULL if no
   translation is desired.  If supplied, the table is used to translate the
   source bytes and the target bytes if the target string is DB-relative.  If
   the target is PB-relative, no target translation is performed.

   The routine is interruptible between bytes.  If an interrupt is pending, the
   routine will return FALSE with the TOS registers updated to reflect the
   partial comparison; reentering the routine will complete the operation.  If
   the comparison runs to completion, the condition code will be set, and the
   routine will return TRUE.  In either case, the "status" parameter is set to
   the SCP status returned by the interrupt test.

   This routine implements the CMPS and CMPT instructions.
*/

static t_bool compare (BYTE_ACCESS *source, BYTE_ACCESS *target, BYTE_ACCESS *table, t_stat *status)
{
uint8 source_byte, target_byte;

*status = SCPE_OK;                                      /* initialize the return status */

while (RA > 0 || RC > 0) {                              /* while there are bytes to compare */
    if (RA == 0)                                        /*   if the source string is exhausted */
        source_byte = ' ';                              /*     then use a blank */
    else                                                /*   otherwise */
        source_byte = mem_read_byte (source);           /*     get the next source byte */

    if (RC == 0)                                        /* if the target string is exhausted */
        target_byte = ' ';                              /*   then use a blank */
    else                                                /* otherwise */
        target_byte = mem_read_byte (target);           /*   get the next target byte */

    if (table != NULL) {                                        /* if the translation table was supplied */
        source_byte = mem_lookup_byte (table, source_byte);     /*   then translate the source byte */

        if (target->class == data || RC == 0)                   /* if the target is in the data segment or is exhausted */
            target_byte = mem_lookup_byte (table, target_byte); /*   then translate the target byte */
        }

    if (source_byte != target_byte)                     /* if the bytes do not compare */
        break;                                          /*   then terminate the loop */

    if (RA > 0)                                         /* if source bytes remain */
        RA = RA - 1;                                    /*   then count the byte (cannot underflow) */

    if (RC > 0)                                         /* if target bytes remain */
        RC = RC - 1;                                    /*   then count the byte (cannot underflow) */

    if (cpu_interrupt_pending (status))                 /* if an interrupt is pending */
        return FALSE;                                   /*   then return with an interrupt set up or a status error */
    }

if (RA == 0 && RC == 0)                                 /* if the counts expired together */
    SET_CCE;                                            /*   then the strings are equal */
else if (source_byte > target_byte)                     /* otherwise if the source byte > the target byte */
    SET_CCG;                                            /*   set the source string is greater */
else                                                    /* otherwise the source byte < the target byte */
    SET_CCL;                                            /*   so the source string is less */

return TRUE;                                            /* return comparison completion status */
}


/* Format and print byte string operands.

   This routine formats and prints source and target byte string operands.  The
   source operand is always printed.  The target operand is printed only if the
   supplied trap condition is "trap_None"; otherwise, it is omitted.  Tracing
   must be enabled when the routine is called.
*/

static void fprint_operands (BYTE_ACCESS *source, BYTE_ACCESS *target, uint32 trap)
{
fprint_operand (source, "source", &fmt_byte_operand);

if (trap == trap_None)
    fprint_operand (target, "target", &fmt_byte_operand);

return;
}


/* Format, translate, and print byte string operands.

   This routine formats, optionally translates, and prints source and target
   byte string operands.  The source operand is always translated.  The target
   operand is translated only if it resides in the data segment.  Tracing must
   be enabled when the routine is called; the trace format is identical to that
   produced by the fprint_operand routine.
*/

static void fprint_translated_operands (BYTE_ACCESS *source, BYTE_ACCESS *target, BYTE_ACCESS *table)
{
hp_debug (&cpu_dev, DEB_MOPND, BOV_FORMAT "  source %d,\"%s\"\n",
          TO_BANK (source->first_byte_address / 2),
          TO_OFFSET (source->first_byte_address / 2),
          source->first_byte_offset, source->length,
          fmt_translated_byte_operand (source->first_byte_address,
                                       source->length,
                                       table->first_byte_address));

if (target->class == program)
    fprint_operand (target, "target", &fmt_byte_operand);

else
    hp_debug (&cpu_dev, DEB_MOPND, BOV_FORMAT "  target %d,\"%s\"\n",
              TO_BANK (target->first_byte_address / 2),
              TO_OFFSET (target->first_byte_address / 2),
              target->first_byte_offset, target->length,
              fmt_translated_byte_operand (target->first_byte_address,
                                           target->length,
                                           table->first_byte_address));
return;
}


/* Format and print a memory operand.

   The byte operand described by the byte accessor is sent to the debug trace
   log file.  Tracing must be enabled when the routine is called.

   On entry, "op" points at the byte accessor describing the operand, "label"
   points to text used to label the operand, and "operand_printer" points to the
   routine used to print the operand.  The latter may be "fprint_byte_operand"
   to print operands consisting of 8-bit characters, or "fprint_bcd_operand" to
   print extended-decimal (BCD) operands as character strings; the operand
   length field of the trace line will indicate the number of characters or BCD
   digits, respectively.

   The operand is printed in this format:

     >>CPU  opnd: 00.045177  000467    source 15,"NOW IS THE TIME"
                  ~~ ~~~~~~  ~~~~~~    ~~~~~~ ~~ ~~~~~~~~~~~~~~~~~
                  |    |       |         |    |          |
                  |    |       |         |    |          +-- operand value
                  |    |       |         |    +------------- operand length
                  |    |       |         +------------------ operand label
                  |    |       +---------------------------- octal relative byte offset from base register
                  |    +------------------------------------ octal operand address (effective address)
                  +----------------------------------------- octal operand bank (PBANK, DBANK, or SBANK)
*/

static void fprint_operand (BYTE_ACCESS *op, char *label, OP_PRINT operand_printer)
{
hp_debug (&cpu_dev, DEB_MOPND, BOV_FORMAT "  %s %d,\"%s\"\n",
          TO_BANK (op->first_byte_address / 2),
          TO_OFFSET (op->first_byte_address / 2),
          op->first_byte_offset, label, op->length,
          operand_printer (op->first_byte_address, op->length));

return;
}
