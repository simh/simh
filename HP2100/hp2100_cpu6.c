/* hp2100_cpu6.c: HP 1000 RTE-6/VM OS microcode simulator

   Copyright (c) 2006-2018, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   CPU6         RTE-6/VM Operating System instructions

   02-Oct-18    JDB     Replaced DMASK with D16_MASK or R_MASK as appropriate
   30-Jul-18    JDB     Replaced direct set of map mode with call to "meu_set_state"
   14-Jun-18    JDB     Renamed PRO to MP
   22-Jul-17    JDB     Renamed "intaddr" to CIR
   10-Jul-17    JDB     Renamed the global routine "iogrp" to "cpu_iog"
   07-Jul-17    JDB     Changed "iotrap" from uint32 to t_bool
   27-Mar-17    JDB     Expanded comments to describe instruction encoding
   17-Jan-17    JDB     Revised to use tprintf and TRACE_OPND for debugging
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   17-May-16    JDB     Set local variable instead of call parameter for .SIP test
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Mar-13    JDB     Use MP abort handler declaration in hp2100_cpu.h
   09-May-12    JDB     Separated assignments from conditional expressions
   29-Oct-10    JDB     DMA channels renamed from 0,1 to 1,2 to match documentation
   18-Sep-08    JDB     Corrected .SIP debug formatting
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   26-Jun-08    JDB     Rewrote device I/O to model backplane signals
   27-Nov-07    JDB     Implemented OS instructions
   26-Sep-06    JDB     Created

   Primary references:
     - HP 1000 M/E/F-Series Computers Technical Reference Handbook
         (5955-0282, March 1980)
     - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
         (92851-90001, March 1981)
     - Macro/1000 Reference Manual
         (92059-90001, December 1992)


   The RTE-6/VM Operating System Instructions were added to accelerate certain
   time-consuming operations of the RTE-6/VM operating system, HP product number
   92084A.  Microcode was available for the E- and F-Series; the M-Series used
   software equivalents.

   The opcodes reside in the range 105340-105357.  The encodings are:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 0   0   0 |  $LIBR
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |          address of the Temporary Data Block or zero          |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The $LIBR instruction saves the data contained in the Temporary Data Block of
   a reentrant subroutine or turns the interrupt system off for a privileged
   subroutine.  The parameter points to the TDB or is zero, respectively.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 0   0   1 |  $LIBX
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |             address of the subroutine entry point             |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The $LIBX instruction restores the data contained in the Temporary Data Block
   of a reentrant subroutine or turns the interrupt system on for a privileged
   subroutine and then returns to the routine's caller.  The parameter points to
   the subroutine entry point, which contains the return address.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 0   1   0 |  .TICK
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :              return location if an EQT timed out              :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :          return location if an EQT did not time out           :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .TICK instruction increments the timeout counters of a set of EQTs.  On
   entry, the A register contains the address of word 15 of the first EQT, and
   the B register contains the count of EQTs to process.  If an EQT timed out,
   the instruction returns at P+1 with the A register pointing at word 15 of the
   EQT, and the B register containing the remaining number of EQTs to process.
   If no EQT timed out, the instruction returns at P+2 with the A register
   pointing at word 15 of the EQT following the last one checked, and the B
   register containing zero.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 0   1   1 |  .TNAM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :             return location if name is not found              :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if name is found                :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .TNAM instruction finds the ID segment address corresponding to a given
   program name.  On entry, the A register contains the address of the keyword
   block, and the B register contains a pointer to the program name.  If the ID
   segment is found, return is to P+2 with the A register containing the address
   of word 15 of the ID segment, the B register containing the address of the ID
   segment, and the E register set to 1 for a short ID segment and 0 for a long
   ID segment.  If the ID segment is not found, return is to P+1 with the
   registers unspecified.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 1   0   0 |  .STIO
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                        return address                         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |          first I/O instruction address to configure           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                    ...
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |           last I/O instruction address to configure           |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .STIO instruction configures a list of I/O instructions.  On entry, each
   parameter following the return address points at an instruction to configure
   with the select code specified in the A register.  The instruction returns
   with all registers unchanged.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 1   0   1 |  .FNW
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       increment address                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :             return location if word is not found              :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :               return location if word is found                :  P+3
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .FNW instruction finds a word in a buffer.  On entry, the A register
   contains the value of the word to find, the B register contains the address
   of the buffer, the X register contains the number of words to compare, and
   the first parameter points to the increment between words.  If the word was
   found, return is to P+3 with the match address in the B register and the
   count of remaining comparisons in the X register.  If the word was not found,
   return is to P+2 with the address of the next comparison location in the B
   register and zero in the X register.  The A register is unchanged in either
   case.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 1   1   0 |  .IRT
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  P-register restore address                   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .IRT instruction is used to return from an interrupt.  It restores the A,
   B, E, O, X, and Y registers from the XSUSP and XI save areas and restores the
   P register from XSUSP,I into the location specified by the parameter, which
   will be the jump target of a following UJP or SJP instruction.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   0 | 1   1   1 |  .LLS
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                  address of the search value                  |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |            address of the offset to the search key            |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :        return location if the linked list is in error         :  P+3
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :       return location if the search value is not found        :  P+4
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :         return location if the search value is found          :  P+5
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .LLS instruction conducts a linked-list search.  On entry, the A register
   contains a pointer to the head of the list, the B register is zero, and the E
   register is 0 to find a value matching the search value or 1 to find a value
   greater than the search value.  The first parameter points at the value to
   find, and the second parameter points at the offset within each list entry
   from the link pointer to the value to compare.  The instruction returns to
   P+3 if the list structure is erroneous (a link has its sign bit set), to P+4
   if the search value is not present in the list, or to P+5 if the search value
   was found.  In the latter two cases, the A register points to the link word
   of the current entry, and the B register points to the link word of the
   previous entry.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 0   0   0 |  .SIP
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :          return location if no interrupt is pending           :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :            return location if interrupt is pending            :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .SIP instruction skips if an interrupt is pending.  It is used to avoid
   exiting and then immediately reentering RTE when an interrupt is pending at
   exit.  A pending interrupt must be serviced by executing the .YLD
   instruction.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 0   0   1 |  .YLD
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |              point of resumption after interrupt              |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .YLD instruction yields control to the trap cell instruction for the
   pending interrupt.  Before transferring control, the P-register set to the
   value contained in the second instruction word.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 0   1   0 |  .CPM
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    first argument address                     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    second argument address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :          return location if argument 1 = argument 2           :  P+3
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :          return location if argument 1 < argument 2           :  P+4
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :          return location if argument 1 > argument 2           :  P+5
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The .CPM instruction compares the two words pointed to by the parameters and
   returns to P+3 if the words are equal, to P+4 if word 1 is less than word 2,
   or to P+5 if word 1 is greater than word 2.  The registers are unchanged by
   this instruction.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 0   1   1 |  .ETEQ
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .ETEQ instruction sets up the base-page EQT addresses to point at the EQT
   whose address is contained in the A register.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   0   0 |  .ENTN
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    parameter block address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .ENTN instruction transfers the direct addresses of parameters from the
   calling sequence of a utility subroutine to the parameter block specified.
   The sequence differs from the .ENTR sequence in that there is no DEF *+n
   parameter immediately following the call, so the number of parameters
   specified must match the size of the parameter block.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   0   1 |  $OTST
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     :       return location if the firmware is not installed        :  P+1
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+
     :         return location if the firmware is installed          :  P+2
     +- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -+

   The $OTST instruction is used to determine programmatically if the OS
   firmware has been installed.  It sets the X-register to the firmware revision
   code, sets S to 102077 (HLT 77B), and returns to P+2.  In hardware, it also
   sets Y to the RPL switch settings and sets A to the contents of the loader
   ROM location specified by the B-register.  In simulation, these last two
   actions are not implemented.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   1   0 |  .ENTC
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                    parameter block address                    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .ENTC instruction transfers the direct addresses of parameters from the
   calling sequence of a privileged or reentrant subroutine to the parameter
   block specified.  The sequence differs from the .ENTP sequence in that there
   is no DEF *+n parameter immediately following the call, so the number of
   parameters specified must match the size of the parameter block.


      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   1   1 |  .DSPI
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The .DSPI instruction copies the lower six bits of the A register into the
   display indicator register that controls the A, B, M, T, P, and S LEDs on the
   front panel of the CPU.  This is not simulated.


   Opcodes 105354-105357 are "dual use" instructions that take different
   actions, depending on whether they are executed from a trap cell during an
   interrupt.  When executed from a trap cell, they have these encodings:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   0   0 |  $DCPC
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The $DCPC instruction is placed in base-page locations 6 and 7 to handle DCPC
   completion interrupts.


     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   0   1 |  $MPV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The $MPV instruction is placed in base-page location 5 to handle memory
   protect, parity error, and MEM violation interrupts.


     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   1   0 |  $DEV
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The $DEV instruction is placed in base-page locations corresponding to the
   select codes of devices whose interrupts are handled by $CIC.


     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | 1   0   1 | 0   1   1 | 1   0   1 | 1   1   1 |  $TBG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The $TBG instruction is placed in base-page location corresponding to the
   select code of the time-base generator interface to handle TBG tick
   interrupts.
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"



/* Offsets to data and addresses within RTE */

static const HP_WORD xi    = 0001647u;          /* XI address */
static const HP_WORD intba = 0001654u;          /* INTBA address */
static const HP_WORD intlg = 0001655u;          /* INTLG address */
static const HP_WORD eqt1  = 0001660u;          /* EQT1  address */
static const HP_WORD eqt11 = 0001672u;          /* EQT11 address */
static const HP_WORD pvcn  = 0001712u;          /* PVCN  address */
static const HP_WORD xsusp = 0001730u;          /* XSUSP address */
static const HP_WORD dummy = 0001737u;          /* DUMMY address */
static const HP_WORD mptfl = 0001770u;          /* MPTFL address */
static const HP_WORD eqt12 = 0001771u;          /* EQT12 address */
static const HP_WORD eqt15 = 0001774u;          /* EQT15 address */
static const HP_WORD vctr  = 0002000u;          /* VCTR address */


/* RTE communication vector.

   The instructions depend on a status area and a set of (direct) addresses for
   communication with RTE.  Location 2000 in the system map contains a pointer
   to the block, which is arranged as follows:

     Location  Label  Contents   Use
     --------  -----  ---------  -------------------------------------------------
       L+00    $DMS   BSS 1      DMS status at interrupt
       L+01    $INT   BSS 1      Interrupt system status
       L+02    INTCD  BSS 1      Interrupting select code
       L+03           DEF $CLCK  Address of the TBG handler
       L+04           DEF $CIC4  Address of the illegal interrupt handler
       L+05           DEF $CIC2  Address of the normal drivers handler
       L+06           DEF $SKED  Address of the interrupt program scheduler
       L+07           DEF $RQST  Address of the EXEC request processor
       L+10           DEF $CIC   Address of the P-Register location for $PERR
       L+11           DEF $PERR  Address of the parity error processor
       L+12           DEF $MPER  Address of the error routine for $LIBR
       L+13           DEF $LXND  Address of the privileged mode cleanup for $LIBX
*/

typedef enum {
    dms_offset = 0,                             /* DMS status */
    int_offset,                                 /* interrupt system status */
    sc_offset,                                  /* select code */
    clck_offset,                                /* TBG IRQ handler */
    cic4_offset,                                /* illegal IRQ handler */
    cic2_offset,                                /* device IRQ handler */
    sked_offset,                                /* prog sched IRQ handler */
    rqst_offset,                                /* EXEC request handler */
    cic_offset,                                 /* IRQ location */
    perr_offset,                                /* parity error IRQ handler */
    mper_offset,                                /* memory protect IRQ handler */
    lxnd_offset                                 /* $LIBX return */
    } VECTOR_OFFSETS;



/* Save the CPU registers.

   The CPU registers are saved in the current ID segment in preparation for
   interrupt handling.  Although the RTE base page has separate pointers for the
   P, A, B, and E/O registers, they are always contiguous, and the microcode
   simply increments the P-register pointer (XSUSP) to store the remaining
   values.

   This routine is called from the trap cell interrupt handlers and from the
   $LIBX processor.  In the latter case, the privileged system interrupt
   handling is not required, so it is bypassed.  In either case, the current map
   will be the system map when we are called, and memory protect will be off.
*/

static void save_regs (t_bool int_ack)
{
HP_WORD save_area, priv_fence;

save_area = ReadW (xsusp);                              /* addr of PABEO save area */

WriteW (save_area + 0, PR);                             /* save P */
WriteW (save_area + 1, AR);                             /* save A */
WriteW (save_area + 2, BR);                             /* save B */
WriteW (save_area + 3, (E << 15) & D16_SIGN | O & 1);   /* save E and O */

save_area = ReadW (xi);                                 /* addr of XY save area */
WriteWA (save_area + 0, XR);                            /* save X (in user map) */
WriteWA (save_area + 1, YR);                            /* save Y (in user map) */

if (int_ack) {                                          /* do priv setup only if IAK */
    priv_fence = ReadW (dummy);                         /* get priv fence select code */

    if (priv_fence) {                                   /* privileged system? */
        io_control (priv_fence, iog_STC);               /* STC SC on priv fence */
        io_control (DMA1,       iog_CLC);               /* CLC 6 to inh IRQ on DCPC 1 */
        io_control (DMA2,       iog_CLC);               /* CLC 7 to inh IRQ on DCPC 2 */
        io_control (CPU,        iog_STF);               /* turn interrupt system back on */
        }
    }

return;
}


/* Save the machine state at interrupt.

   This routine is called from each of the trap cell instructions during an
   interrupt acknowledgement.  Its purpose is to save the complete state of the
   machine in preparation for interrupt handling.

   For the MP/DMS/PE interrupt, the interrupting device must not be cleared and
   the CPU registers must not be saved until it is established that the
   interrupt is not caused by a parity error.  Parity errors cannot be
   inhibited, so the interrupt may have occurred while running in RTE with the
   interrupt system off.  Saving the registers would overwrite the user's
   registers that were saved at RTE entry.  The state of the interrupt system is
   determined by executing a SFS 0,C instruction, which increments P if the
   interrupt system was on, and then turns it off by clearing the flag.

   Note that the trap cell instructions are dual-use and invoke this routine
   only when they are executed during interrupts.  Therefore, the current map
   will always be the system map.
*/

static void save_state (void)
{
HP_WORD vectors, int_sys_off;

mp_disable ();                                          /* turn MP off */

int_sys_off = (HP_WORD) ! io_control (CPU, iog_SFS_C);  /* set flag if interrupt system is already off */

vectors = ReadW (vctr);                                 /* get address of vectors (in SMAP) */

WriteW (vectors + dms_offset, meu_update_status ());    /* save DMS status (SSM) */
WriteW (vectors + int_offset, int_sys_off);             /* save int status */
WriteW (vectors + sc_offset,  CIR);                     /* save select code */

WriteW (mptfl, 1);                                      /* show MP is off */

if (CIR != MPPE) {                                      /* only if not MP interrupt */
    io_control (CIR, iog_CLF);                          /* issue CLF to device */
    save_regs (TRUE);                                   /* save CPU registers */
    }

return;
}


/* Get the interrupt table entry corresponding to a select code.

   Return the word in the RTE interrupt table that corresponds to the
   interrupting select code.  Return 0 if the select code is beyond the end of
   the table.
*/

static HP_WORD cpu_get_intbl (HP_WORD select_code)
{
HP_WORD interrupt_table;                                /* interrupt table (starts with SC 06) */
HP_WORD table_length;                                   /* length of interrupt table */

interrupt_table = ReadW (intba);                        /* get int table address */
table_length = ReadW (intlg);                           /* get int table length */

if (select_code - 6 > table_length)                     /* SC beyond end of table? */
    return 0;                                           /* return 0 for illegal interrupt */
else
    return ReadW (interrupt_table + select_code - 6);   /* else return table entry */
}


/* RTE-6/VM Operating System Instructions.

   The OS instructions were added to accelerate certain time-consuming
   operations of the RTE-6/VM operating system, HP product number 92084A.
   Microcode was available for the E- and F-Series; the M-Series used software
   equivalents.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    92084A  92084A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     $LIBR    105340   Enter privileged/reentrant library routine
     $LIBX    105341   Exit privileged/reentrant library routine
     .TICK    105342   TBG tick interrupt handler
     .TNAM    105343   Find ID segment that matches name
     .STIO    105344   Configure I/O instructions
     .FNW     105345   Find word with user increment
     .IRT     105346   Interrupt return processing
     .LLS     105347   Linked list search

     .SIP     105350   Skip if interrupt pending
     .YLD     105351   .SIP completion return point
     .CPM     105352   Compare words LT/EQ/GT
     .ETEQ    105353   Set up EQT pointers in base page
     .ENTN    105354   Transfer parameter addresses (utility)
     $OTST *  105355   OS firmware self test
     .ENTC    105356   Transfer parameter addresses (priv/reent)
     .DSPI    105357   Set display indicator

   Opcodes 105354-105357 are "dual use" instructions that take different
   actions, depending on whether they are executed from a trap cell during an
   interrupt.  When executed from a trap cell, they have these actions:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     $DCPC *  105354   DCPC channel interrupt processing
     $MPV  *  105355   MP/DMS/PE interrupt processing
     $DEV  *  105356   Standard device interrupt processing
     $TBG  *  105357   TBG interrupt processing

   * These mnemonics are recognized by symbolic examine/deposit but are not
     official HP mnemonics.

   The default (user microcode) dispatcher will allow the firmware self-test
   instruction (105355) to execute as NOP.  This is because RTE-6/VM will always
   test for the presence of OS and VMA firmware on E/F-Series machines.  If the
   firmware is not present, then these instructions will return to P+1, and RTE
   will then HLT 21.  This means that RTE-6/VM will not run on an E/F-Series
   machine without the OS and VMA firmware.

   However, RTE allows the firmware instructions to be disabled for debugging
   purposes.  If the firmware is present and returns to P+2 but sets the X
   register to 0, then RTE will use software equivalents.  We enable this
   condition when the OS firmware is enabled (SET CPU VMA), the OS debug flag is
   set (SET CPU DEBUG=OS), but debug output has been disabled (SET CONSOLE
   NODEBUG).  That is:

                 OS     Debug
     Firmware   Debug   Output   Tracing   Self-Test Instruction
     ========   =====   ======   =======   =====================
     disabled     x       x        off     NOP
     enabled    clear     x        off     X = revision code
     enabled     set     off       off     X = 0
     enabled     set     on        on      X = revision code

   Additional references:

    - RTE-6/VM OS Microcode Source (92084-18831, revision 8).
    - RTE-6/VM Technical Specifications (92084-90015, Apr-1983).


   Implementation notes:

    1. The microcode differentiates between interrupt processing and normal
       execution of the "dual use" instructions by testing the CPU flag.
       Interrupt vectoring sets the flag; a normal instruction fetch clears it.
       Under simulation, interrupt vectoring is indicated by the value of the
       "int_ack" parameter (FALSE = normal instruction, TRUE = interrupt
       acknowledge instruction).

    2. The operand patterns for .ENTN and .ENTC normally would be coded as
       "OP_A", as each takes a single address as a parameter.  However, because
       they might also be executed from a trap cell (as $DCPC and $DEV), we
       cannot assume that P+1 is an address, or we might cause a DM abort when
       trying to resolve indirects.  Therefore, "OP_A" handling is done within
       each routine, once the type of use is determined.

    3. The microcode for .ENTC, .ENTN, .FNW, .LLS, .TICK, and .TNAM explicitly
       checks for interrupts during instruction execution.  In addition, the
       .STIO, .CPM, and .LLS instructions implicitly check for interrupts during
       parameter indirect resolution.  Because the simulator calculates
       interrupt requests only between instructions, this behavior is not
       simulated.

    4. The microcode executes certain I/O instructions (e.g., CLF 0) by building
       the instruction in the IR and executing an IOG micro-order.  We simulate
       this behavior by calling the "io_control" routine with the appropriate
       I/O signal assertion.

    5. The $OTST and .DSPI microcode provides features (reading the RPL switches
       and boot loader ROM data, loading the display register) that are not
       simulated.  The remaining functions of the $OTST instruction are
       provided.  The .DSPI instruction is a NOP or unimplemented instruction
       stop.

    6. The $LIBX instruction is executed to complete either a privileged or
       reentrant execution.  In the former case, the privileged nest counter
       ($PVCN) is decremented.  In the latter, $PVCN decrement is attempted but
       the write will trap with an MP violation, as reentrant routines execute
       with the interrupt system on.  RTE will then complete the release of
       memory allocated for the original $LIBR call.

    7. The documentation for the .SIP and .YLD instructions is misleading in
       several places.  Comments in the RTE $SIP source file say that .SIP
       doesn't return if a "known" interrupt is pending.  Actually, .SIP always
       returns, either to P+1 for no pending interrupt, or to P+2 if one is
       pending.  There is no check for "known" interrupt handlers.  The
       microcode source comments say that the interrupting select code is
       returned in the B register.  Actually, the B register is unchanged.  The
       RTE Tech Specs say that .SIP "services any pending system interrupts."
       Actually, .SIP only checks for interrupts; no servicing is performed.

       For .YLD, the microcode comments say that two parameters are passed: the
       new P value, and the interrupting select code.  Actually, only the new P
       value is passed.

       The .SIP and .YLD simulations follow the actual microcode rather than the
       documentation.

    8. The "%.0u" print specification in the trace call absorbs the zero "CIR"
       value parameter without printing when an interrupt is not pending.

    9. The $LIBR microcode checks for memory protect on by reading the MEM
       status register and checking the protected mode bit (bit 11).  In
       simulation, we check for MP on with the "mp_is_on" routine, as the status
       register is not global.
*/

static const OP_PAT op_os [16] = {
  OP_A,    OP_A,    OP_N,    OP_N,                      /* $LIBR  $LIBX  .TICK  .TNAM  */
  OP_A,    OP_K,    OP_A,    OP_KK,                     /* .STIO  .FNW   .IRT   .LLS   */
  OP_N,    OP_C,    OP_KK,   OP_N,                      /* .SIP   .YLD   .CPM   .ETEQ  */
  OP_N,    OP_N,    OP_N,    OP_N                       /* .ENTN  $OTST  .ENTC  .DSPI  */
  };

t_stat cpu_rte_os (t_bool int_ack)
{
static const char * const no      [2] = { "",     "no " };
static const char * const not     [2] = { "not ", ""    };
static const char * const list    [3] = { "list error", "not found", "found" };
static const char * const compare [3] = { "equal",      "less than", "greater than" };

OPS     op;
OP_PAT  pattern;
uint32  i, irq;
HP_WORD entry, count, cp, sa, da, ma, eqta;
HP_WORD vectors, save_area, priv_fence, eoreg, eqt, key;
char    test [6], target [6];
t_bool  mpv;
t_stat  reason = SCPE_OK;

entry = IR & 017;                                       /* mask to entry point */
pattern = op_os [entry];                                /* get operand pattern */

if (pattern != OP_N) {
    reason = cpu_ops (pattern, op);                     /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

    case 000:                                           /* $LIBR 105340 (OP_A) */
        if (op[0].word != 0                             /* reentrant call? */
          || mp_is_on () && ReadW (dummy) != 0) {       /* or priv call + MP on + priv sys? */
            meu_privileged (If_User_Map);               /* priv viol if called from user map */

            meu_set_state (ME_Enabled, System_Map);     /* set system map */

            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */
            PR = ReadW (vectors + mper_offset);         /* vector to $MPER for processing */
            }

        else {                                          /* privileged call */
            if (mp_is_on ()) {                          /* memory protect on? */
                mp_disable ();                          /* turn it off */
                io_control (CPU, iog_CLF);              /* turn interrupt system off */
                WriteW (mptfl, 1);                      /* show MP is off */

                save_area = ReadW (xsusp);              /* get addr of P save area */
                WriteS (save_area, PR - 2 & LA_MASK);   /* set point of suspension using the system map */
                }

            WriteW (pvcn, ReadW (pvcn) + 1 & D16_MASK); /* increment priv nest counter */
            }
        break;


    case 001:                                           /* $LIBX 105341 (OP_A) */
        PR = ReadW (op[0].word);                        /* set P to return point */
        count = ReadW (pvcn) - 1 & D16_MASK;            /* decrement priv nest counter */
        WriteW (pvcn, count);                           /* write it back */

        if (count == 0) {                               /* end of priv mode? */
            meu_set_state (ME_Enabled, System_Map);     /* set system map */
            save_regs (FALSE);                          /* save registers */
            vectors = ReadW (vctr);                     /* get address of vectors */
            PR = ReadW (vectors + lxnd_offset);         /* vector to $LXND for processing */
            }
        break;


    case 002:                                           /* .TICK 105342 (OP_N) */
        do {
            eqt = ReadW (AR) + 1 & D16_MASK;            /* bump timeout from EQT15 */

            if (eqt != 1) {                             /* was timeout active? */
                WriteW (AR, eqt);                       /* yes, write it back */

                if (eqt == 0)                           /* did timeout expire? */
                    break;                              /* P+1 return for timeout */
                }

            AR = AR + 15 & R_MASK;                      /* point at next EQT15 */
            BR = BR - 1 & R_MASK;                       /* decrement count of EQTs */
        } while ((BR > 0) && (eqt != 0));               /* loop until timeout or done */

        if (BR == 0)                                    /* which termination condition? */
            PR = (PR + 1) & LA_MASK;                    /* P+2 return for no timeout */

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%stimeout)\n",
                 PR, IR, PR - err_PR, no [PR - err_PR - 1]);
        break;


    case 003:                                           /* .TNAM 105343 (OP_N) */
        E = 1;                                          /* preset flag for not found */
        cp = BR << 1 & D16_MASK;                        /* form char addr (B is direct) */

        for (i = 0; i < 5; i++) {                       /* copy target name */
            target[i] = (char) ReadB (cp);              /* name is only 5 chars */
            cp = cp + 1 & D16_MASK;
            }

        if ((target[0] == '\0') && (target[1] == '\0')) /* if name is null, */
            break;                                      /* return immed to P+1 */

        key = ReadW (AR);                               /* get first keyword addr */

        while (key != 0) {                              /* end of keywords? */
            cp = key + 12 << 1 & D16_MASK;              /* form char addr of name */

            for (i = 0; i < 6; i++) {                   /* copy test name */
                test[i] = (char) ReadB (cp);            /* name is only 5 chars */
                cp = cp + 1 & D16_MASK;                 /* but copy 6 to get flags */
                }

            if (strncmp (target, test, 5) == 0) {       /* names match? */
                AR = key + 15 & R_MASK;                 /* A = addr of IDSEG [15] */
                BR = key;                               /* B = addr of IDSEG [0] */
                E = (uint32) ((test[5] >> 4) & 1);      /* E = short ID segment bit */
                PR = (PR + 1) & LA_MASK;                /* P+2 for found return */
                break;
                }

            AR = AR + 1 & R_MASK;                       /* bump to next keyword */
            key = ReadW (AR);                           /* get next keyword */
            };

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%sfound)\n",
                 PR, IR, PR - err_PR, not [PR - err_PR - 1]);
        break;


    case 004:                                           /* .STIO 105344 (OP_A) */
        count = op[0].word - PR;                        /* get count of operands */

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  instruction count is %u\n",
                 PR, IR, count);

        for (i = 0; i < count; i++) {
            MR = ReadW (PR);                            /* get operand address */

            reason = cpu_resolve_indirects (TRUE);      /* resolve indirects */

            if (reason != SCPE_OK) {                    /* resolution failed? */
                PR = err_PR;                            /* IRQ restarts instruction */
                break;
                }

            WriteW (MR, ReadW (MR) & ~SC_MASK | AR);    /* set SC into instruction */
            PR = (PR + 1) & LA_MASK;                    /* bump to next */
            }
        break;


    case 005:                                           /* .FNW  105345 (OP_K) */
        while (XR != 0) {                               /* all comparisons done? */
            key = ReadW (BR);                           /* read a buffer word */

            if (key == AR) {                            /* does it match? */
                PR = (PR + 1) & LA_MASK;                /* P+3 found return */
                break;
                }

            BR = BR + op[0].word & R_MASK;              /* increment buffer ptr */
            XR = XR - 1 & R_MASK;                       /* decrement remaining count */
            }
                                                        /* P+2 not found return */

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%sfound)\n",
                 PR, IR, PR - err_PR, not [PR - err_PR - 2]);
        break;


    case 006:                                           /* .IRT  105346 (OP_A) */
        save_area = ReadW (xsusp);                      /* addr of PABEO save area */

        WriteW (op[0].word, ReadW (save_area + 0));     /* restore P to DEF RTN */

        AR = ReadW (save_area + 1);                     /* restore A */
        BR = ReadW (save_area + 2);                     /* restore B */

        eoreg = ReadW (save_area + 3);                  /* get combined E and O */
        E = (eoreg >> 15) & 1;                          /* restore E */
        O = eoreg & 1;                                  /* restore O */

        save_area = ReadW (xi);                         /* addr of XY save area */
        XR = ReadWA (save_area + 0);                    /* restore X (from user map) */
        YR = ReadWA (save_area + 1);                    /* restore Y (from user map) */

        io_control (CPU, iog_CLF);                      /* turn interrupt system off */
        WriteW (mptfl, 0);                              /* show MP is on */

        priv_fence = ReadW (dummy);                     /* get priv fence select code */

        if (priv_fence) {                               /* privileged system? */
            io_control (priv_fence, iog_CLC);           /* CLC SC on priv fence */
            io_control (priv_fence, iog_STF);           /* STF SC on priv fence */

            if (cpu_get_intbl (DMA1) & D16_SIGN)        /* DCPC 1 active? */
                io_control (DMA1, iog_STC);             /* STC 6 to enable IRQ on DCPC 1 */

            if (cpu_get_intbl (DMA2) & D16_SIGN)        /* DCPC 2 active? */
                io_control (DMA2, iog_STC);             /* STC 7 to enable IRQ on DCPC 2 */
            }
        break;


    case 007:                                           /* .LLS  105347 (OP_KK) */
        AR = AR & ~D16_SIGN;                            /* clear sign bit of A */

        while ((AR != 0) && ((AR & D16_SIGN) == 0)) {   /* end of list or bad list? */
            key = ReadW ((AR + op[1].word) & LA_MASK);  /* get key value */

            if ((E == 0) && (key == op[0].word) ||      /* for E = 0, key = arg? */
                (E != 0) && (key >  op[0].word))        /* for E = 1, key > arg? */
                break;                                  /* search is done */

            BR = AR;                                    /* B = last link */
            AR = ReadW (AR);                            /* A = next link */
            }

        if (AR == 0)                                    /* exhausted list? */
            PR = (PR + 1) & LA_MASK;                    /* P+4 arg not found */

        else if ((AR & D16_SIGN) == 0)                  /* good link? */
            PR = (PR + 2) & LA_MASK;                    /* P+5 arg found */

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%s)\n",
                 PR, IR, PR - err_PR, list [PR - err_PR - 3]);
        break;


    case 010:                                           /* .SIP  105350 (OP_N) */
        irq = io_poll_interrupts (SET);                 /* check for interrupt requests */

        if (irq)                                        /* was interrupt pending? */
            PR = PR + 1 & R_MASK;                       /* P+2 return for pending IRQ */

        tprintf (cpu_dev, TRACE_OPND,
                 (irq ? OPND_FORMAT "  return location is P+2 (pending), CIR %02o\n"
                      : OPND_FORMAT "  return location is P+1 (not pending)%.0u\n"),
                 PR, IR, irq);
        break;


    case 011:                                           /* .YLD  105351 (OP_C) */
        PR = op[0].word;                                /* pick up point of resumption */
        io_control (CPU, iog_STF);                      /* turn interrupt system on */
        cpu_interrupt_enable = SET;                     /* enable so irq occurs immed */
        break;


    case 012:                                           /* .CPM  105352 (OP_KK) */
        if (INT16 (op[0].word) > INT16 (op[1].word))
            PR = (PR + 2) & LA_MASK;                    /* P+5 arg1 > arg2 */

        else if (INT16 (op[0].word) < INT16 (op[1].word))
            PR = (PR + 1) & LA_MASK;                    /* P+4 arg1 < arg2 */

        tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (%s)\n",
                 PR, IR, PR - err_PR, compare [PR - err_PR - 3]);
        break;


    case 013:                                           /* .ETEQ 105353 (OP_N) */
        eqt = ReadW (eqt1);                             /* get addr of EQT1 */

        if (AR != eqt) {                                /* already set up? */
            for (eqta = eqt1; eqta <= eqt11; eqta++)    /* init EQT1-EQT11 */
                WriteW (eqta, AR++ & R_MASK);
            for (eqta = eqt12; eqta <= eqt15; eqta++)   /* init EQT12-EQT15 */
                WriteW (eqta, AR++ & R_MASK);           /* (not contig with EQT1-11) */
            }

        AR = AR & R_MASK;                               /* ensure wraparound */
        break;


    case 014:                                           /* .ENTN/$DCPC 105354 (OP_N) */
        if (int_ack) {                                  /* in trap cell? */
            save_state ();                              /* DMA interrupt */
            AR = cpu_get_intbl (CIR) & ~D16_SIGN;       /* get intbl value and strip sign */
            goto DEVINT;                                /* vector by intbl value */
            }

        else {                                          /* .ENTN instruction */
            ma = (PR - 2) & LA_MASK;                    /* get addr of entry point */

        ENTX:                                           /* enter here from .ENTC */
            reason = cpu_ops (OP_A, op);                /* get instruction operand */
            da = op[0].word;                            /* get addr of 1st formal */
            count = ma - da;                            /* get count of formals */
            sa = ReadW (ma);                            /* get addr of 1st actual */
            WriteW (ma, (sa + count) & LA_MASK);        /* adjust return point to skip actuals */

            tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  parameter count is %u\n",
                     PR, IR, count);

            for (i = 0; i < count; i++) {               /* parameter loop */
                MR = ReadW (sa);                        /* get addr of actual */
                sa = (sa + 1) & LA_MASK;                /* increment address */

                reason = cpu_resolve_indirects (TRUE);  /* resolve indirects */

                if (reason != SCPE_OK) {                /* resolution failed? */
                    PR = err_PR;                        /* irq restarts instruction */
                    break;
                    }

                WriteW (da, MR);                        /* put addr into formal */
                da = (da + 1) & LA_MASK;                /* increment address */
                }

            if (entry == 016)                           /* call was .ENTC? */
                AR = sa;                                /* set A to return address */
            }
        break;


    case 015:                                           /* $OTST/$MPV 105355 (OP_N) */
        if (int_ack) {                                  /* in trap cell? */
            mpv = mp_trace_violation ();                /* get MP/PE flag */

            save_state ();                              /* MP/DMS/PE interrupt */
            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */

            if (mpv) {                                  /* MP/DMS violation */
                save_regs (TRUE);                       /* save registers */
                PR = ReadW (vectors + rqst_offset);     /* vector to $RQST for processing */
                }

            else {                                      /* parity error */
                WriteW (vectors + cic_offset, PR);      /* save point of suspension in $CIC */
                PR = ReadW (vectors + perr_offset);     /* vector to $PERR for processing */
                }
            }

        else {                                          /* self-test instruction */
            YR = 0000000;                               /* RPL switch (not implemented) */
            AR = 0000000;                               /* LDR [B] (not implemented) */
            SR = 0102077;                               /* test passed code */
            PR = (PR + 1) & LA_MASK;                    /* P+2 return for firmware OK */

            if (cpu_dev.dctrl & DEBUG_NOOS)             /* if the OS debug flag is set */
                XR = 0;                                 /*   then return rev 0 so that RTE won't use microcode */
            else                                        /* otherwise */
                XR = 010;                               /*   return firmware revision code 10B */

            tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  return location is P+%u (firmware %sinstalled)\n",
                     PR, IR, PR - err_PR, not [PR - err_PR - 1]);
            }
        break;


    case 016:                                           /* .ENTC/$DEV 105356 (OP_N) */
        if (int_ack) {                                  /* in trap cell? */
            save_state ();                              /* device interrupt */
            AR = cpu_get_intbl (CIR);                   /* get interrupt table value */

        DEVINT:
            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */

            if (INT16 (AR) < 0)                         /* negative (program ID)? */
                PR = ReadW (vectors + sked_offset);     /* vector to $SKED for processing */

            else if (AR > 0)                            /* positive (EQT address)? */
                PR = ReadW (vectors + cic2_offset);     /* vector to $CIC2 for processing */

            else                                        /* zero (illegal interrupt) */
                PR = ReadW (vectors + cic4_offset);     /* vector to $CIC4 for processing */
            }

        else {                                          /* .ENTC instruction */
            ma = (PR - 4) & LA_MASK;                    /* get addr of entry point */
            goto ENTX;                                  /* continue with common processing */
            }
        break;


    case 017:                                           /* .DSPI/$TBG 105357 (OP_N) */
        if (int_ack) {                                  /* in trap cell? */
            save_state ();                              /* TBG interrupt */
            vectors = ReadW (vctr);                     /* get address of vectors (in SMAP) */
            PR = ReadW (vectors + clck_offset);         /* vector to $CLCK for processing */
            }

        else                                            /* .DSPI instruction */
            reason = STOP (cpu_ss_unimpl);              /* not implemented yet */

        break;
    }

return reason;
}
