/* hp2100_cpu1.c: HP 2100/1000 EAU/FP/IOP microcode simulator

   Copyright (c) 2005-2016, Robert M. Supnik
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
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   CPU1         Extended Arithmetic Unit, Floating Point, and I/O Processor
                instructions

   23-Jan-19    JDB     Moved fmt_ab to hp2100_cpu5.c
   02-Oct-18    JDB     Replaced DMASK with D16_MASK or R_MASK as appropriate
   02-Aug-18    JDB     Moved UIG dispatcher to hp2100_cpu0.c
                        Moved FP and IOP dispatchers from hp2100_cpu2.c
   25-Jul-18    JDB     Use cpu_configuration instead of cpu_unit.flags for tests
   07-Sep-17    JDB     Removed unnecessary "uint16" casts
   01-Aug-17    JDB     Changed TIMER and RRR 16 to test for undefined stops
   07-Jul-17    JDB     Changed "iotrap" from uint32 to t_bool
   26-Jun-17    JDB     Replaced SEXT with SEXT16
   22-Apr-17    JDB     Improved the EAU shift/rotate instructions
   21-Mar-17    JDB     Fixed UIG 1 comment regarding 2000 IOP and F-Series
   31-Jan-17    JDB     Added fmt_ab to print A/B-register error codes
   30-Jan-17    JDB     Removed fprint_ops, fprint_regs (now redundant)
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   05-Apr-14    JDB     Corrected typo in comments for cpu_ops
   09-May-12    JDB     Separated assignments from conditional expressions
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Moved option-present tests to UIG dispatchers
                        Call "user microcode" dispatcher for unclaimed UIG instructions
   20-Apr-08    JDB     Fixed VIS and SIGNAL to depend on the FPP and HAVE_INT64
   28-Nov-07    JDB     Added fprint_ops, fprint_regs for debug printouts
   17-Nov-07    JDB     Enabled DIAG as NOP on 1000 F-Series
   04-Jan-07    JDB     Added special DBI dispatcher for non-INT64 diagnostic
   29-Dec-06    JDB     Allows RRR as NOP if 2114 (diag config test)
   01-Dec-06    JDB     Substitutes FPP for firmware FP if HAVE_INT64
   16-Oct-06    JDB     Generalized operands for F-Series FP types
   26-Sep-06    JDB     Split hp2100_cpu1.c into multiple modules to simplify extensions
                        Added iotrap parameter to UIG dispatchers for RTE microcode
   22-Feb-05    JDB     Removed EXECUTE instruction (is NOP in actual microcode)
   21-Jan-05    JDB     Reorganized CPU option and operand processing flags
                        Split code along microcode modules
   15-Jan-05    RMS     Cloned from hp2100_cpu.c

   Primary references:
     - HP 1000 M/E/F-Series Computers Technical Reference Handbook
         (5955-0282, March 1980)
     - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
         (92851-90001, March 1981)
     - Macro/1000 Reference Manual
         (92059-90001, December 1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.


   This source file contains the Extended Arithmetic Unit simulator, the
   single-precision floating-pointer simulator, and the HP 2000 I/O Processor
   instructions simulator.

*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"

#if !defined (HAVE_INT64)                               /* int64 support unavailable */

  #include "hp2100_cpu_fp.h"

#endif



/* EAU.

   The Extended Arithmetic Unit (EAU) adds ten instructions with double-word
   operands, including multiply, divide, shifts, and rotates.  Option
   implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A    12579A  12579A   std     std     std     std

   The instruction codes are mapped to routines as follows:

     Instr.    Bits
      Code   15-8 7-4   2116    2100   1000-M  1000-E  1000-F  Note
     ------  ---- ---  ------  ------  ------  ------  ------  ---------------------
     100000   200  00                          [diag]  [diag]  [self test]
     100020   200  01   ASL     ASL     ASL     ASL     ASL    Bits 3-0 encode shift
     100040   200  02   LSL     LSL     LSL     LSL     LSL    Bits 3-0 encode shift
     100060   200  03                          TIMER   TIMER   [deterministic delay]
     100100   200  04   RRL     RRL     RRL     RRL     RRL    Bits 3-0 encode shift
     100200   200  10   MPY     MPY     MPY     MPY     MPY
     100400   201  xx   DIV     DIV     DIV     DIV     DIV
     101020   202  01   ASR     ASR     ASR     ASR     ASR    Bits 3-0 encode shift
     101040   202  02   LSR     LSR     LSR     LSR     LSR    Bits 3-0 encode shift
     101100   202  04   RRR     RRR     RRR     RRR     RRR    Bits 3-0 encode shift
     104200   210  xx   DLD     DLD     DLD     DLD     DLD
     104400   211  xx   DST     DST     DST     DST     DST

   The remaining codes for bits 7-4 are undefined and will cause a simulator
   stop if enabled.  On a real 1000-M, all undefined instructions in the 200
   group decode as MPY, and all in the 202 group decode as NOP.  On a real
   1000-E, instruction patterns 200/05 through 200/07 and 202/03 decode as NOP;
   all others cause erroneous execution.

   EAU instruction decoding on the 1000 M-series is convoluted.  The JEAU
   microorder maps IR bits 11, 9-7 and 5-4 to bits 2-0 of the microcode jump
   address.  The map is detailed on page IC-84 of the ERD.

   The 1000 E/F-series add two undocumented instructions to the 200 group: TIMER
   and DIAG.  These are described in the ERD on page IA 5-5, paragraph 5-7.  The
   M-series executes these as MPY and RRL, respectively.  A third instruction,
   EXECUTE (100120), is also described but was never implemented, and the
   E/F-series microcode execute a NOP for this instruction code.

   If the EAU is not installed in a 2115 or 2116, EAU instructions execute as
   NOPs or cause unimplemented instruction stops if enabled.


   Implementation notes:

    1. Under simulation, TIMER and DIAG cause undefined instruction stops if the
       CPU is not an E/F-Series.  Note that TIMER is intentionally executed by
       several HP programs to differentiate between M- and E/F-series machines.

    2. DIAG is not implemented under simulation.  On the E/F, it performs a
       destructive test of all installed memory.  Because of this, it is only
       functional if the machine is halted, i.e., if the instruction is
       executed with the INSTR STEP button.  If it is executed in a program,
       the result is NOP.

    3. The RRR 16 instruction is intentionally executed by the diagnostic
       configurator on the 2114, which does not have an EAU, to differentiate
       between 2114 and 2100/1000 CPUs.

    4. The shift count is calculated unconditionally, as six of the ten
       instructions will be using the value.

    5. An arithmetic left shift must be handled as a special case because the
       shifted operand bits "skip over" the sign bit.  That is, the bits are
       lost from the next-most-significant bit while preserving the MSB.  For
       all other shifts, including the arithmetic right shift, the operand may
       be shifted and then merged with the appropriate fill bits.

    6. The C standard specifies that the results of bitwise shifts with negative
       signed operands are undefined (for left shifts) or implementation-defined
       (for right shifts).  Therefore, we must use unsigned operands and handle
       arithmetic shifts explicitly.
*/

t_stat cpu_eau (void)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 rs, qs, v1, v2, operand, fill, mask, shift;
int32 sop1, sop2;

if (!(cpu_configuration & CPU_EAU))                     /* if the EAU is not installed */
    return STOP (cpu_ss_unimpl);                        /*   then the instructions execute as NOPs */

if (IR & 017)                                           /* if the shift count is 1-15 */
    shift = IR & 017;                                   /*   then use it verbatim */
else                                                    /* otherwise the count iz zero */
    shift = 16;                                         /*   so use a shift count of 16 */

switch ((IR >> 8) & 0377) {                             /* decode IR<15:8> */

    case 0200:                                          /* EAU group 0 */
        switch ((IR >> 4) & 017) {                      /* decode IR<7:4> */

            case 000:                                       /* DIAG 100000 */
                if (!(cpu_configuration & CPU_1000_E_F))    /* if the CPU is not an E- or F-series */
                    return STOP (cpu_ss_undef);             /*   then the instruction is undefined */
                break;                                      /*     and executes as NOP */


            case 001:                                   /* ASL 100020-100037 */
                operand = TO_DWORD (BR, AR);            /* form the double-word operand */

                mask = D32_UMAX << 31 - shift;          /* form a mask for the bits that will be lost */

                if (operand & D32_SIGN)                     /* if the operand is negative */
                    O = (~operand & mask & D32_MASK) != 0;  /*   then set overflow if any of the lost bits are zeros */
                else                                        /* otherwise it's positive */
                    O = (operand & mask & D32_MASK) != 0;   /*   so set overflow if any of the lost bits are ones */

                operand = operand << shift & D32_SMAX   /* shift the operand left */
                            | operand & D32_SIGN;       /*   while keeping the original sign bit */

                BR = UPPER_WORD (operand);              /* split the operand */
                AR = LOWER_WORD (operand);              /*   into its constituent parts */
                break;


            case 002:                                   /* LSL 100040-100057 */
                operand = TO_DWORD (BR, AR) << shift;   /* shift the double-word operand left */

                BR = UPPER_WORD (operand);              /* split the operand */
                AR = LOWER_WORD (operand);              /*   into its constituent parts */
                break;


            case 004:                                   /* RRL 100100-100117 */
                operand = TO_DWORD (BR, AR);            /* form the double-word operand */
                fill = operand;                         /*   and fill with operand bits */

                operand = operand << shift              /* rotate the operand left */
                            | fill >> 32 - shift;       /*   while filling in on the right */

                BR = UPPER_WORD (operand);              /* split the operand */
                AR = LOWER_WORD (operand);              /*   into its constituent parts */
                break;


            case 003:                                   /* TIMER 100060 */
                if (cpu_configuration & CPU_1000_E_F) { /* if the CPU is an E- or F-series */
                    BR = BR + 1 & R_MASK;               /*   then increment B */

                    if (BR != 0)                        /* if B did not roll over */
                        PR = err_PR;                    /*   then repeat the instruction */
                    break;
                    }

                else {                                  /* otherwise it's a 21xx or 1000 M-Series */
                    reason = STOP (cpu_ss_undef);       /*   and the instruction is undefined */

                    if (reason != SCPE_OK               /* if a stop is indicated */
                      || cpu_configuration & CPU_21XX)  /*   or the CPU is a 21xx */
                        break;                          /*     then the instruction executes as NOP */
                    }

            /* fall through into the MPY case if 1000 M-Series */

            case 010:                                   /* MPY 100200 (OP_K) */
                reason = cpu_ops (OP_K, op);            /* get operand */

                if (reason == SCPE_OK) {                /* successful eval? */
                    sop1 = SEXT16 (AR);                 /* sext AR */
                    sop2 = SEXT16 (op[0].word);         /* sext mem */
                    sop1 = sop1 * sop2;                 /* signed mpy */
                    BR = UPPER_WORD (sop1);             /* to BR'AR */
                    AR = LOWER_WORD (sop1);
                    O = 0;                              /* no overflow */
                    }
                break;


            default:                                    /* others undefined */
                return STOP (cpu_ss_unimpl);
            }

        break;


    case 0201:                                          /* DIV 100400 (OP_K) */
        reason = cpu_ops (OP_K, op);                    /* get operand */

        if (reason != SCPE_OK)                          /* eval failed? */
            break;

        rs = qs = BR & D16_SIGN;                        /* save divd sign */

        if (rs) {                                       /* neg? */
            AR = (~AR + 1) & R_MASK;                    /* make B'A pos */
            BR = (~BR + (AR == 0)) & R_MASK;            /* make divd pos */
            }

        v2 = op[0].word;                                /* divr = mem */

        if (v2 & D16_SIGN) {                            /* neg? */
            v2 = (~v2 + 1) & D16_MASK;                  /* make divr pos */
            qs = qs ^ D16_SIGN;                         /* sign of quotient */
            }

        if (BR >= v2)                                   /* if the divisor is too small */
            O = 1;                                      /*   then set overflow */

        else {                                          /* maybe... */
            O = 0;                                      /* assume ok */
            v1 = (BR << 16) | AR;                       /* 32b divd */
            AR = (v1 / v2) & R_MASK;                    /* quotient */
            BR = (v1 % v2) & R_MASK;                    /* remainder */

            if (AR) {                                   /* quotient > 0? */
                if (qs)                                 /* apply quo sign */
                    AR = NEG16 (AR);

                if ((AR ^ qs) & D16_SIGN)               /* still wrong? ovflo */
                    O = 1;
                }

            if (rs)
                BR = NEG16 (BR);                        /* apply rem sign */
            }
        break;


    case 0202:                                          /* EAU group 2 */
        switch ((IR >> 4) & 017) {                      /* decode IR<7:4> */

            case 001:                                   /* ASR 101020-101037 */
                O = 0;                                  /* clear ovflo */

                operand = TO_DWORD (BR, AR);            /* form the double-word operand */
                fill = (operand & D32_SIGN ? ~0 : 0);   /*   and fill with copies of the sign bit */

                operand = operand >> shift              /* shift the operand right */
                            | fill << 32 - shift;       /*   while filling in with sign bits */

                BR = UPPER_WORD (operand);              /* split the operand */
                AR = LOWER_WORD (operand);              /*   into its constituent parts */
                break;


            case 002:                                   /* LSR 101040-101057 */
                operand = TO_DWORD (BR, AR) >> shift;   /* shift the double-word operand right */

                BR = UPPER_WORD (operand);              /* split the operand */
                AR = LOWER_WORD (operand);              /*   into its constituent parts */
                break;


            case 004:                                   /* RRR 101100-101117 */
                operand = TO_DWORD (BR, AR);            /* form the double-word operand */
                fill = operand;                         /*   and fill with operand bits */

                operand = operand >> shift              /* rotate the operand right */
                            | fill << 32 - shift;       /*   while filling in on the left */

                BR = UPPER_WORD (operand);              /* split the operand */
                AR = LOWER_WORD (operand);              /*   into its constituent parts */
                break;


            default:                                    /* others undefined */
                return STOP (cpu_ss_undef);
            }

        break;


    case 0210:                                          /* DLD 104200 (OP_D) */
        reason = cpu_ops (OP_D, op);                    /* get operand */

        if (reason == SCPE_OK) {                        /* successful eval? */
            AR = UPPER_WORD (op[0].dword);              /* load AR */
            BR = LOWER_WORD (op[0].dword);              /* load BR */
            }
        break;


    case 0211:                                          /* DST 104400 (OP_A) */
        reason = cpu_ops (OP_A, op);                    /* get operand */

        if (reason == SCPE_OK) {                        /* successful eval? */
            WriteW (op[0].word, AR);                    /* store AR */
            WriteW ((op[0].word + 1) & LA_MASK, BR);    /* store BR */
            }
        break;


    default:                                            /* should never get here */
        return SCPE_IERR;                               /* bad call from cpu_instr */
    }

return reason;
}



#if !defined (HAVE_INT64)                               /* int64 support unavailable */

/* Single-Precision Floating Point Instructions.

   The 2100 and 1000 CPUs share the single-precision (two word) floating-point
   instruction codes.  Floating-point firmware was an option on the 2100 and was
   standard on the 1000-M and E.  The 1000-F had a standard hardware Floating
   Point Processor that executed these six instructions and added extended- and
   double-precision floating- point instructions, as well as double-integer
   instructions (the FPP is simulated separately).

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A    12901A   std     std     N/A

   The instruction codes for the 2100 and 1000-M/E systems are mapped to
   routines as follows:

     Instr.  2100/1000-M/E   Description
     ------  -------------  -----------------------------------
     105000       FAD       Single real add
     105020       FSB       Single real subtract
     105040       FMP       Single real multiply
     105060       FDV       Single real divide
     105100       FIX       Single integer to single real fix
     105120       FLT       Single real to single integer float

   Bits 3-0 are not decoded by these instructions, so FAD (e.g.) would be
   executed by any instruction in the range 105000-105017.

   Implementation note: rather than have two simulators that each executes the
   single-precision FP instruction set, we compile conditionally, based on the
   availability of 64-bit integer support in the host compiler.  64-bit integers
   are required for the FPP, so if they are available, then the FPP is used to
   handle the six single-precision instructions for the 2100 and M/E-Series, and
   this function is omitted.  If support is unavailable, this function is used
   instead.

   Implementation note: the operands to FAD, etc. are floating-point values, so
   OP_F would normally be used.  However, the firmware FP support routines want
   floating-point operands as 32-bit integer values, so OP_D is used to achieve
   this.
*/

static const OP_PAT op_fp[8] = {
  OP_D,    OP_D,    OP_D,    OP_D,                      /*  FAD    FSB    FMP    FDV  */
  OP_N,    OP_N,    OP_N,    OP_N                       /*  FIX    FLT    ---    ---  */
  };

t_stat cpu_fp (void)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

entry = (IR >> 4) & 017;                                /* mask to entry point */

if (op_fp [entry] != OP_N) {
    reason = cpu_ops (op_fp [entry], op);               /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<7:4> */

    case 000:                                           /* FAD 105000 (OP_D) */
        O = f_as (op[0].dword, 0);                      /* add, upd ovflo */
        break;

    case 001:                                           /* FSB 105020 (OP_D) */
        O = f_as (op[0].dword, 1);                      /* sub, upd ovflo */
        break;

    case 002:                                           /* FMP 105040 (OP_D) */
        O = f_mul (op[0].dword);                        /* mul, upd ovflo */
        break;

    case 003:                                           /* FDV 105060 (OP_D) */
        O = f_div (op[0].dword);                        /* div, upd ovflo */
        break;

    case 004:                                           /* FIX 105100 (OP_N) */
        O = f_fix ();                                   /* fix, upd ovflo */
        break;

    case 005:                                           /* FLT 105120 (OP_N) */
        O = f_flt ();                                   /* float, upd ovflo */
        break;

    default:                                            /* should be impossible */
        return SCPE_IERR;
        }

return reason;
}

#endif                                                  /* int64 support unavailable */



/* HP 2000 I/O Processor.

   The IOP accelerates certain operations of the HP 2000 Time-Share BASIC system
   I/O processor.  Most 2000 systems were delivered with 2100 CPUs, although IOP
   microcode was developed for the 1000-M and 1000-E.  As the I/O processors
   were specific to the 2000 system, general compatibility with other CPU
   microcode options was unnecessary, and indeed no other options were possible
   for the 2100.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A    13206A  13207A  22702A   N/A

   The routines are mapped to instruction codes as follows:

     Instr.     2100      1000-M/E   Description
     ------  ----------  ----------  --------------------------------------------
     SAI     105060-117  101400-037  Store A indexed by B (+/- offset in IR<4:0>)
     LAI     105020-057  105400-037  Load A indexed by B (+/- offset in IR<4:0>)
     CRC     105150      105460      Generate CRC
     REST    105340      105461      Restore registers from stack
     READF   105220      105462      Read F register (stack pointer)
     INS       --        105463      Initialize F register (stack pointer)
     ENQ     105240      105464      Enqueue
     PENQ    105257      105465      Priority enqueue
     DEQ     105260      105466      Dequeue
     TRSLT   105160      105467      Translate character
     ILIST   105000      105470      Indirect address list (similar to $SETP)
     PRFEI   105222      105471      Power fail exit with I/O
     PRFEX   105223      105472      Power fail exit
     PRFIO   105221      105473      Power fail I/O
     SAVE    105362      105474      Save registers to stack

     MBYTE   105120      105765      Move bytes (MBT)
     MWORD   105200      105777      Move words (MVW)
     SBYTE   105300      105764      Store byte (SBT)
     LBYTE   105320      105763      Load byte (LBT)

   The INS instruction was not required in the 2100 implementation because the
   stack pointer was actually the memory protect fence register and so could be
   loaded directly with an OTA/B 05.  Also, the 1000 implementation did not
   offer the MBYTE, MWORD, SBYTE, and LBYTE instructions because the equivalent
   instructions from the standard Extended Instruction Group were used instead.

   Note that the 2100 MBYTE and MWORD instructions operate slightly differently
   from the 1000 MBT and MVW instructions.  Specifically, the move count is
   signed on the 2100 and unsigned on the 1000.  A negative count on the 2100
   results in a NOP.

   The simulator remaps the 2100 instructions to the 1000 codes.  The four EIG
   equivalents are dispatched to the EIG simulator.  The rest are handled here.

   Additional reference:
     - HP 2000 Computer System Sources and Listings Documentation
         (22687-90020, undated), section 3, pages 2-74 through 2-91


   Implementation notes:

    1. The SAVE and RESTR instructions use the (otherwise unused) SP register on
       the 1000 as the stack pointer.  On the 2100, there is no SP register, so
       the instructions use the memory protect fence register as the stack
       pointer.  We update the 2100 fence because it could affect CPU operation
       if MP is turned on (although, in practice, the 2100 IOP does not use
       memory protect and so never enables it).
*/

static const OP_PAT op_iop[16] = {
  OP_V,    OP_N,    OP_N,    OP_N,                      /* CRC    RESTR  READF  INS   */
  OP_N,    OP_N,    OP_N,    OP_V,                      /* ENQ    PENQ   DEQ    TRSLT */
  OP_AC,   OP_CVA,  OP_A,    OP_CV,                     /* ILIST  PRFEI  PRFEX  PRFIO */
  OP_N,    OP_N,    OP_N,    OP_N                       /* SAVE    ---    ---    ---  */
  };

t_stat cpu_iop (uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint8 byte;
uint32 entry, i;
HP_WORD hp, tp, t, wc, MA;

if (cpu_configuration & CPU_2100) {                     /* 2100 IOP? */
    if ((IR >= 0105020) && (IR <= 0105057))             /* remap LAI */
        IR = 0105400 | (IR - 0105020);
    else if ((IR >= 0105060) && (IR <= 0105117))        /* remap SAI */
        IR = 0101400 | (IR - 0105060);
    else {
        switch (IR) {                                   /* remap others */
        case 0105000: IR = 0105470; break;              /* ILIST */
        case 0105120: return cpu_eig (0105765, intrq);  /* MBYTE (maps to MBT) */
        case 0105150: IR = 0105460; break;              /* CRC   */
        case 0105160: IR = 0105467; break;              /* TRSLT */
        case 0105200: return cpu_eig (0105777, intrq);  /* MWORD (maps to MVW) */
        case 0105220: IR = 0105462; break;              /* READF */
        case 0105221: IR = 0105473; break;              /* PRFIO */
        case 0105222: IR = 0105471; break;              /* PRFEI */
        case 0105223: IR = 0105472; break;              /* PRFEX */
        case 0105240: IR = 0105464; break;              /* ENQ   */
        case 0105257: IR = 0105465; break;              /* PENQ  */
        case 0105260: IR = 0105466; break;              /* DEQ   */
        case 0105300: return cpu_eig (0105764, intrq);  /* SBYTE (maps to SBT) */
        case 0105320: return cpu_eig (0105763, intrq);  /* LBYTE (maps to LBT) */
        case 0105340: IR = 0105461; break;              /* REST  */
        case 0105362: IR = 0105474; break;              /* SAVE  */

        default:                                        /* all others invalid */
            return STOP (cpu_ss_unimpl);
            }
        }
    }

entry = IR & 077;                                       /* mask to entry point */

if (entry <= 037) {                                     /* LAI/SAI 10x400-437 */
    MA = ((entry - 020) + BR) & LA_MASK;                /* +/- offset */

    if (IR & AB_MASK)                                   /* if this is an LAI instruction */
        AR = ReadW (MA);                                /*   then load the A register */
    else                                                /* otherwise */
        WriteW (MA, AR);                                /*   store the A register */

    return reason;
    }

else if (entry <= 057)                                  /* IR = 10x440-457? */
    return STOP (cpu_ss_unimpl);                        /* not part of IOP */

entry = entry - 060;                                    /* offset 10x460-477 */

if (op_iop [entry] != OP_N) {
    reason = cpu_ops (op_iop [entry], op);              /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<5:0> */

    case 000:                                           /* CRC 105460 (OP_V) */
        t = ReadW (op[0].word) ^ (AR & 0377);           /* xor prev CRC and char */
        for (i = 0; i < 8; i++) {                       /* apply polynomial */
            t = (t >> 1) | ((t & 1) << 15);             /* rotate right */
            if (t & D16_SIGN) t = t ^ 020001;           /* old t<0>? xor */
            }
        WriteW (op[0].word, t);                         /* rewrite CRC */
        break;

    case 001:                                           /* RESTR 105461 (OP_N) */
        SPR = (SPR - 1) & LA_MASK;                      /* decr stack ptr */
        t = ReadW (SPR);                                /* get E and O */
        O = ((t >> 1) ^ 1) & 1;                         /* restore O */
        E = t & 1;                                      /* restore E */
        SPR = (SPR - 1) & LA_MASK;                      /* decr sp */
        BR = ReadW (SPR);                               /* restore B */
        SPR = (SPR - 1) & LA_MASK;                      /* decr sp */
        AR = ReadW (SPR);                               /* restore A */
        if (cpu_configuration & CPU_2100)               /* 2100 keeps sp in MP FR */
            mp_fence = SPR;                             /*   (in case MP is turned on) */
        break;

    case 002:                                           /* READF 105462 (OP_N) */
        AR = SPR;                                       /* copy stk ptr */
        break;

    case 003:                                           /* INS 105463 (OP_N) */
        SPR = AR;                                       /* init stk ptr */
        break;

    case 004:                                           /* ENQ 105464 (OP_N) */
        hp = ReadW (AR & LA_MASK);                      /* addr of head */
        tp = ReadW ((AR + 1) & LA_MASK);                /* addr of tail */
        WriteW ((BR - 1) & LA_MASK, 0);                 /* entry link */
        WriteW ((tp - 1) & LA_MASK, BR);                /* tail link */
        WriteW ((AR + 1) & LA_MASK, BR);                /* queue tail */
        if (hp != 0) PR = (PR + 1) & LA_MASK;           /* q not empty? skip */
        break;

    case 005:                                           /* PENQ 105465 (OP_N) */
        hp = ReadW (AR & LA_MASK);                      /* addr of head */
        WriteW ((BR - 1) & LA_MASK, hp);                /* becomes entry link */
        WriteW (AR & LA_MASK, BR);                      /* queue head */
        if (hp == 0)                                    /* q empty? */
            WriteW ((AR + 1) & LA_MASK, BR);            /* queue tail */
        else PR = (PR + 1) & LA_MASK;                   /* skip */
        break;

    case 006:                                           /* DEQ 105466 (OP_N) */
        BR = ReadW (AR & LA_MASK);                      /* addr of head */
        if (BR) {                                       /* queue not empty? */
            hp = ReadW ((BR - 1) & LA_MASK);            /* read hd entry link */
            WriteW (AR & LA_MASK, hp);                  /* becomes queue head */
            if (hp == 0)                                /* q now empty? */
                WriteW ((AR + 1) & LA_MASK, (AR + 1) & R_MASK);
            PR = (PR + 1) & LA_MASK;                    /* skip */
            }
        break;

    case 007:                                           /* TRSLT 105467 (OP_V) */
        wc = ReadW (op[0].word);                        /* get count */
        if (wc & D16_SIGN) break;                       /* cnt < 0? */
        while (wc != 0) {                               /* loop */
            MA = (AR + AR + ReadB (BR)) & LA_MASK;
            byte = ReadB (MA);                          /* xlate */
            WriteB (BR, byte);                          /* store char */
            BR = (BR + 1) & R_MASK;                     /* incr ptr */
            wc = (wc - 1) & D16_MASK;                   /* decr cnt */
            if (wc && intrq) {                          /* more and intr? */
                WriteW (op[0].word, wc);                /* save count */
                PR = err_PR;                            /* stop for now */
                break;
                }
            }
        break;

    case 010:                                           /* ILIST 105470 (OP_AC) */
        do {                                            /* for count */
            WriteW (op[0].word, AR);                    /* write AR to mem */
            AR = (AR + 1) & R_MASK;                     /* incr AR */
            op[0].word = (op[0].word + 1) & LA_MASK;    /* incr MA */
            op[1].word = (op[1].word - 1) & D16_MASK;   /* decr count */
            }
        while (op[1].word != 0);
        break;

    case 011:                                           /* PRFEI 105471 (OP_CVA) */
        WriteW (op[1].word, 1);                         /* set flag */
        reason = cpu_iog (op[0].word);                  /* execute I/O instr */
        op[0].word = op[2].word;                        /* set rtn and fall through */

    case 012:                                           /* PRFEX 105472 (OP_A) */
        PCQ_ENTRY;
        PR = ReadW (op[0].word) & LA_MASK;              /* jump indirect */
        WriteW (op[0].word, 0);                         /* clear exit */
        break;

    case 013:                                           /* PRFIO 105473 (OP_CV) */
        WriteW (op[1].word, 1);                         /* set flag */
        reason = cpu_iog (op[0].word);                  /* execute instr */
        break;

    case 014:                                           /* SAVE 105474 (OP_N) */
        WriteW (SPR, AR);                               /* save A */
        SPR = (SPR + 1) & LA_MASK;                      /* incr stack ptr */
        WriteW (SPR, BR);                               /* save B */
        SPR = (SPR + 1) & LA_MASK;                      /* incr stack ptr */
        t = (HP_WORD) ((O ^ 1) << 1 | E);               /* merge E and O */
        WriteW (SPR, t);                                /* save E and O */
        SPR = (SPR + 1) & LA_MASK;                      /* incr stack ptr */
        if (cpu_configuration & CPU_2100)               /* 2100 keeps sp in MP FR */
            mp_fence = SPR;                             /*   (in case MP is turned on) */
        break;

    default:                                            /* instruction unimplemented */
        return STOP (cpu_ss_unimpl);
        }

return reason;
}
