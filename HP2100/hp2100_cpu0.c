/* hp2100_cpu0.c: HP 1000 user microcode and unimplemented instruction set stubs

   Copyright (c) 2006-2017, J. David Bryan

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

   CPU0         User microcode and unimplemented firmware options

   01-Aug-17    JDB     Changed .FLUN and self-tests to test for unimplemented stops
   09-May-12    JDB     Separated assignments from conditional expressions
   04-Nov-10    JDB     Removed DS note regarding PIF card (is now implemented)
   18-Sep-08    JDB     .FLUN and self-tests for VIS and SIGNAL are NOP if not present
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
                        Added "user microcode" dispatcher for unclaimed instructions
   26-Feb-08    HV      Removed and implemented "cpu_vis" and "cpu_signal"
   22-Nov-07    JDB     Removed and implemented "cpu_rte_ema"
   12-Nov-07    JDB     Removed and implemented "cpu_rte_vma" and "cpu_rte_os"
   01-Dec-06    JDB     Removed and implemented "cpu_sis".
   26-Sep-06    JDB     Created

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
        (92851-90001, Mar-1981)
   - Macro/1000 Reference Manual (92059-90001, Dec-1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.


   This file contains template simulations for the firmware options that have
   not yet been implemented.  When a given firmware option is implemented, it
   should be moved out of this file and into another (or its own, depending on
   complexity).

   It also contains a user-microprogram dispatcher to allow simulation of
   site-specific firmware.  All UIG instructions unclaimed by installed firmware
   options are directed here and may be simulated by writing the appropriate
   code.
*/


#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"


/* Distributed System.

   Distributed System firmware was provided with the HP 91740A DS/1000 product
   for use with the HP 12771A (12665A) Serial Interface and 12773A Modem
   Interface system interconnection kits.  Firmware permitted high-speed
   transfers with minimum impact to the processor.  The advent of the
   "intelligent" 12794A and 12825A HDLC cards, the 12793A and 12834A Bisync
   cards, and the 91750A DS-1000/IV software obviated the need for CPU firmware,
   as essentially the firmware was moved onto the I/O cards.

   Primary documentation for the DS instructions has not been located.  However,
   examination of the DS/1000 sources reveals that two instruction were used by
   the DVA65 Serial Interface driver (91740-18071) and placed in the trap cells
   of the communications interfaces.  Presumably they handled interrupts from
   the cards.

   Implementation of the DS instructions will also require simulation of the
   12665A Hardwired Serial Data Interface Card.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A    91740A  91740B  91740B

   The routines are mapped to instruction codes as follows:

     Instr.  1000-M  1000-E/F  Description
     ------  ------  --------  ----------------------------------------------
             105520   105300   "Open loop" (trap cell handler)
             105521   105301   "Closed loop" (trap cell handler)
             105522   105302   [unknown]
     [test]  105524   105304   [self test]
               --     105310   7974 boot loader ROM extension

   Notes:

     1. The E/F-Series opcodes were moved from 105340-357 to 105300-317 at
        revision 1813.

     2. DS/1000 ROM data are available from Bitsavers.

   Additional references (documents unavailable):
    - HP 91740A M-Series Distributed System (DS/1000) Firmware Installation
                Manual (91740-90007).
    - HP 91740B Distributed System (DS/1000) Firmware Installation Manual
                (91740-90009).
*/

static const OP_PAT op_ds[16] = {
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N                     /*  ---    ---    ---    ---  */
  };

t_stat cpu_ds (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

entry = IR & 017;                                       /* mask to entry point */

if (op_ds [entry] != OP_N) {
    reason = cpu_ops (op_ds[entry], op, intrq);         /* get instruction operands */
    if (reason != SCPE_OK)                              /* did the evaluation fail? */
        return reason;                                  /* return the reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
    }

return reason;
}


/* User firmware dispatcher.

   All UIG instructions unclaimed by installed firmware options are directed
   here.  User- or site-specific firmware may be simulated by dispatching to the
   appropriate simulator routine.  Unimplemented instructions should return
   "STOP (cpu_ss_unimpl)" to cause a simulator stop if enabled.

   Implementation notes:

    1. This routine may be passed any opcode in the ranges 101400-101737 and
       105000-105737.  The 10x740-777 range is dedicated to the EIG instructions
       and is unavailable for user microprograms.

    2. HP operating systems and subsystems depend on the following instructions
       to execute as NOP and return success if the corresponding firmware is not
       installed:

         105226  --  Fast FORTRAN Processor .FLUN instruction
         105355  --  RTE-6/VM OS self-test instruction
         105477  --  Vector Instruction Set self-test
         105617  --  SIGNAL/1000 self-test

       These instructions are executed to determine firmware configuration
       dynamically.  If you use any of these opcodes for your own use, be aware
       that certain HP programs may fail.

    3. User microprograms occupied one or more firmware modules, each containing
       16 potential instruction entry points.  A skeleton dispatcher for the 32
       possible modules is implemented below, along with a sample module.
*/

t_stat cpu_user (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;

if (UNIT_CPU_TYPE == UNIT_TYPE_211X)                    /* 2116/15/14 CPU? */
    return STOP (cpu_ss_unimpl);                        /* user microprograms not supported */

switch ((IR >> 4) & 037) {                              /* decode IR<8:4> */

/*  case 000:                                           ** 105000-105017 */
/*      return cpu_user_00 (IR, intrq);                 ** uncomment to handle instruction */

/*  case 001:                                           ** 105020-105037 */
/*      return cpu_user_01 (IR, intrq);                 ** uncomment to handle instruction */

/*  case 0nn:                                           ** other cases as needed */
/*      return cpu_user_nn (IR, intrq);                 ** uncomment to handle instruction */

    case 020:                                           /* 10x400-10x417 */
        return cpu_user_20 (IR, intrq);                 /* call sample dispatcher */

/*  case 021:                                           ** 10x420-10x437 */
/*      return cpu_user_21 (IR, intrq);                 ** uncomment to handle instruction */

/*  case 0nn:                                           ** other cases as needed */
/*      return cpu_user_nn (IR, intrq);                 ** uncomment to handle instruction */

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
    }

return reason;
}


/* Example user microprogram simulator.

   User- or site-specific firmware may be simulated by writing the appropriate
   code below.  Unimplemented instructions should return "STOP (cpu_ss_unimpl)"
   to cause a simulator stop if enabled.

   For information on the operand patterns used in the "op_user" array, see the
   comments preceding the "cpu_ops" routine in "hp2100_cpu1.c" and the "operand
   processing encoding" constants in "hp2100_cpu1.h".
*/

static const OP_PAT op_user_20[16] = {
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N                     /*  ---    ---    ---    ---  */
  };

t_stat cpu_user_20 (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

entry = IR & 017;                                       /* mask to entry point */

if (op_user_20 [entry] != OP_N) {
    reason = cpu_ops (op_user_20 [entry], op, intrq);   /* get instruction operands */
    if (reason != SCPE_OK)                              /* did the evaluation fail? */
        return reason;                                  /* return the reason for failure */
    }

switch (entry) {                                        /* decode IR<4:0> */

    case 000:                                           /* 10x400 */
/*      break;                                          ** uncomment to handle instruction */

    case 001:                                           /* 10x401 */
/*      break;                                          ** uncomment to handle instruction */

/*  case 0nn:                                           ** other cases as needed */
/*      break;                                          ** uncomment to handle instruction */

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
    }

return reason;
}
