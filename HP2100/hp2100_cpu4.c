/* hp2100_cpu4.c: HP 1000 FPP/SIS

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

   CPU4         Floating Point Processor and Scientific Instruction Set

   07-Sep-17    JDB     Replaced "uint16" cast with "HP_WORD" for FPK assignment
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   09-May-12    JDB     Separated assignments from conditional expressions
   06-Feb-12    JDB     Added OPSIZE casts to fp_accum calls in .FPWR/.TPWR
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   18-Mar-08    JDB     Fixed B register return bug in /CMRT
   01-Dec-06    JDB     Substitutes FPP for firmware FP if HAVE_INT64

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
        (92851-90001, Mar-1981)
   - Macro/1000 Reference Manual (92059-90001, Dec-1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.
*/

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"

#if defined (HAVE_INT64)                                /* int64 support available */

#include "hp2100_fp1.h"


/* Floating-Point Processor.

   The 1000 F-Series replaces the six 2100/1000-M/E single-precision firmware
   floating-point instructions with a hardware floating-point processor (FPP).
   The FPP executes single-, extended-, and double-precision floating-point
   instructions, as well as double-integer instructions.  All of the
   floating-point instructions, as well as the single- and double-integer fix
   and float instructions, are handled here.  Pure double-integer instructions
   are dispatched to the double-integer handler for simulation.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A     std

   For the F-Series, the instruction codes are mapped to routines as follows:

     Instr.  1000-F  Description
     ------  ------  -------------------------------------
     105000   FAD    Single real add
     105001  .XADD   Extended real add
     105002  .TADD   Double real add
     105003  [EAD]   [5-word add]
     105004  [tst]   [Floating Point Processor self test]
     105005  [xpd]   [Expand exponent]
     105006  [rst]   [Floating Point Processor reset]
     105007  [stk]   [Process stack of operands]
     105010  [chk]   [FPP addressing check]
     105014  .DAD    Double integer add
     105020   FSB    Single real subtract
     105021  .XSUB   Extended real subtract
     105022  .TSUB   Double real subtract
     105023  [ESB]   [5-word subtract]
     105034  .DSB    Double integer subtract
     105040   FMP    Single real multiply
     105041  .XMPY   Extended real multiply
     105042  .TMPY   Double real multiply
     105043  [EMP]   [5-word multiply]
     105054  .DMP    Double integer multiply
     105060   FDV    Single real divide
     105061  .XDIV   Extended real divide
     105062  .TDIV   Double real divide
     105063  [EDV]   [5-word divide]
     105074  .DDI    Double integer divide
     105100   FIX    Single real to integer fix
     105101  .XFXS   Extended real to integer fix (.DINT)
     105102  .TXFS   Double real to integer fix (.TINT)
     105103  [EFS]   [5-word FIXS]
     105104  .FIXD   Real to double integer fix
     105105  .XFXD   Extended real to double integer fix
     105106  .TFXD   Double real to double integer fix
     105107  [EFD]   [5-word FIXD]
     105114  .DSBR   Double integer subtraction (reversed)
     105120   FLT    Integer to single real float
     105121  .XFTS   Integer to extended real float (.IDBL)
     105122  .TFTS   Integer to double real float (.ITBL)
     105123  [ELS]   [5-word FLTS]
     105124  .FLTD   Double integer to real float
     105125  .XFTD   Double integer to extended real float
     105126  .TFTD   Double integer to double real float
     105127  [ELD]   [5-word FLTD]
     105134  .DDIR   Double integer divide (reversed)

   Implementation note: rather than have two simulators that each executes the
   single-precision FP instruction set, we compile conditionally, based on the
   availability of 64-bit integer support in the host compiler.  64-bit integers
   are required for the FPP, so if they are available, then we handle the
   single-precision instructions for the 2100 and M/E-Series here, and the
   firmware simulation is omitted.  If support is unavailable, then the firmware
   function is used instead.

   Notes:

     1. Single-precision arithmetic instructions (.FAD, etc.) and extended- and
        double-precision F-Series FPP arithmetic instructions (.XADD, .TADD,
        etc.) return positive infinity on both positive and negative overflow.
        The equivalent extended-precision M/E-Series FFP instructions return
        negative infinity on negative overflow and positive infinity on positive
        overflow.

     2. The items in brackets above are undocumented instructions that are used
        by the 12740 FPP-SIS-FFP diagnostic only.

     3. The five-word arithmetic instructions (e.g., 105003) use an expanded
        operand format that dedicates a separate word to the exponent.  See the
        implementation notes in the hardware floating-point processor simulation
        for details.

     4. The "self test" instruction (105004) returned to P+1 for early F-Series
        units without double-integer support.  Units incorporating such support
        returned to P+2.

     5. The "expand exponent" instruction (105005) is used as a "prefix"
        instruction to enable a 10-bit exponent range.  It is placed immediately
        before a 5-word arithmetic instruction sequence, e.g., immediately
        preceding an EAD instruction sequence.  The arithmetic instruction
        executes normally, except that under/overflow is not indicated unless
        the exponent exceeds the 10-bit range, instead of the normal 8-bit
        range.  If overflow is indicated, the exponent is still set to +128.

        Note that as 2-, 3-, and 4-word packed numbers only have room for 8-bit
        exponents, the Expand Exponent instruction serves no useful purpose in
        conjunction with instructions associated with these precisions.  If
        used, the resulting values may be in error, as overflow from the 8-bit
        exponents will not be indicated.

     6. The "FPP reset" instruction (105006) is provided to reset a hung box,
        e.g., in cases where an improper number of parameters is supplied.  The
        hardware resets its internal state machine in response to this
        instruction.  Under simulation, the instruction has no effect, as the
        simulated FPP cannot hang.

     7. The "process stack" instruction (105007) executes a series of FPP
        instruction sets in sequence.  Each set consists of a single FPP
        instruction and associated operands that specifies the operation,
        followed by a "result" instruction and operand.  The result instruction
        is optional and is only used to specify the result precision; the
        instruction itself is not executed.  If the result instruction is NOP,
        then the result precision is that of the executed FPP instruction.  If
        the result operand is null, then the result is kept in the internal FPP
        accumulator for later use.

        The calling sequence is as follows:

                  STK               Process stack instruction
                  DEF ERRTN         Address of error return
                  DEF SET1          Address of first instruction set
                  DEF SET2          Address of second instruction set
                   .
                   .
                   .
            ERRTN EQU *             Return here if execution in error
            OKRTN EQU *             Return here if execution OK

        Instruction sets are specified as follows (e.g.):

            SET1  .TADD             Operation instruction (NOP to terminate series)
                  DEC 4             Number of words in first operand (or 0 if accum)
                  DEF OP1           Address of first operand
                  DEC 4             Number of words in second operand (or 0 if accum)
                  DEF OP2           Address of second operand
                  .XADD             Result precision conversion instruction (or NOP)
                  DEC 3             Number of words to store (or 0 if no store)
                  DEF RSLT          Address of buffer to hold value

        The primary use of the "process stack" instruction is to enable chained
        operations employing the FPP accumulator for intermediate results and to
        enable expanded exponent usage across multiple instructions.

     8. The "addressing check" instruction sets bit 0 of the L register to 1,
        copies the X register value to the FPP, and then reads the FPP and
        stores the result in the Y register.  Setting the L register bit 0 to 1
        normally deselects the FPP, so that the value in Y is 177777.  However,
        the FPP box has a strap that inverts the selection logic, even though
        the box will not work with the base-set firmware if this is done.  The
        "addressing check" instruction is provided to test whether the strap is
        in the alternate location.  Under simulation, the return value is always
        177777, indicating that the strap is correctly set.

   Additional references:
    - DOS/RTE Relocatable Library Reference Manual (24998-90001, Oct-1981)
    - FPP-SIS-FFP Diagnostic Source (12740-18001, Rev. 1926)
*/

static const OP_PAT op_fpp[96] = {
  OP_RF,   OP_AXX,  OP_ATT,  OP_AEE,                    /*  FAD   .XADD  .TADD  .EADD */
  OP_N,    OP_C,    OP_N,    OP_A,                      /* [tst]  [xpd]  [rst]  [stk] */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* [chk]   ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* .DAD    ---    ---    ---  */
  OP_RF,   OP_AXX,  OP_ATT,  OP_AEE,                    /*  FSB   .XSUB  .TSUB  .ESUB */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* .DSB    ---    ---    ---  */
  OP_RF,   OP_AXX,  OP_ATT,  OP_AEE,                    /*  FMP   .XMPY  .TMPY  .EMPY */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* .DMP    ---    ---    ---  */
  OP_RF,   OP_AXX,  OP_ATT,  OP_AEE,                    /*  FDV   .XDIV  .TDIV  .EDIV */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* .DDI    ---    ---    ---  */
  OP_R,    OP_X,    OP_T,    OP_E,                      /*  FIX   .XFXS  .TFXS  .EFXS */
  OP_R,    OP_X,    OP_T,    OP_E,                      /* .FIXD  .XFXD  .TFXD  .EFXD */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* .DSBR   ---    ---    ---  */
  OP_I,    OP_IA,   OP_IA,   OP_IA,                     /*  FLT   .XFTS  .TFTS  .EFTS */
  OP_J,    OP_JA,   OP_JA,   OP_JA,                     /* .FLTD  .XFTD  .TFTD  .EFTD */
  OP_N,    OP_N,    OP_N,    OP_N,                      /*  ---    ---    ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N                       /* .DDIR   ---    ---    ---  */
  };

t_stat cpu_fpp (uint32 IR, uint32 intrq)
{
OP fpop;
OPS op;
OPSIZE op1_prec, op2_prec, rslt_prec, cvt_prec;
HP_WORD rtn_addr, stk_ptr;
uint16 opcode;
uint32 entry;
t_stat reason = SCPE_OK;

if (UNIT_CPU_MODEL == UNIT_1000_F)                      /* F-Series? */
    opcode = (uint16) (IR & 0377);                      /* yes, use full opcode */
else
    opcode = (uint16) (IR & 0160);                      /* no, use 6 SP FP opcodes */

entry = opcode & 0177;                                  /* map to <6:0> */

if (op_fpp [entry] != OP_N) {
    reason = cpu_ops (op_fpp [entry], op, intrq);       /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<6:0> */
    case 0000:                                          /* FAD 105000 (OP_RF) */
    case 0020:                                          /* FSB 105020 (OP_RF) */
    case 0040:                                          /* FMP 105040 (OP_RF) */
    case 0060:                                          /* FDV 105060 (OP_RF) */
        O = fp_exec (opcode, &fpop, op[0], op[1]);      /* execute operation */
        AR = fpop.fpk[0];                               /* return result to A/B */
        BR = fpop.fpk[1];
        break;

    case 0001:                                          /* .XADD 105001 (OP_AXX) */
    case 0002:                                          /* .TADD 105002 (OP_ATT) */
    case 0003:                                          /* .EADD 105003 (OP_AEE) */

    case 0021:                                          /* .XSUB 105021 (OP_AXX) */
    case 0022:                                          /* .TSUB 105022 (OP_ATT) */
    case 0023:                                          /* .ESUB 105023 (OP_AEE) */

    case 0041:                                          /* .XMPY 105041 (OP_AXX) */
    case 0042:                                          /* .TMPY 105042 (OP_ATT) */
    case 0043:                                          /* .EMPY 105043 (OP_AEE) */

    case 0061:                                          /* .XDIV 105061 (OP_AXX) */
    case 0062:                                          /* .TDIV 105062 (OP_ATT) */
    case 0063:                                          /* .EDIV 105063 (OP_AEE) */
        O = fp_exec (opcode, &fpop, op[1], op[2]);      /* execute operation */
        fp_prec (opcode, NULL, NULL, &rslt_prec);       /* determine result precision */
        WriteOp (op[0].word, fpop, rslt_prec);          /* write result */
        break;

    case 0004:                                          /* [tst] 105004 (OP_N) */
        XR = 3;                                         /* firmware revision */
        SR = 0102077;                                   /* test passed code */
        PR = (PR + 1) & VAMASK;                         /* P+2 return for firmware w/DBI */
        break;

    case 0005:                                          /* [xpd] 105005 (OP_C) */
        return cpu_fpp (op[0].word | 0200, intrq);      /* set bit 7, execute instr */

    case 0006:                                          /* [rst] 105006 (OP_N) */
        break;                                          /* do nothing for FPP reset */

    case 0007:                                          /* [stk] 105007 (OP_A) */
        O = 0;                                          /* clear overflow */
        stk_ptr = PR;                                   /* save ptr to next buf */
        rtn_addr = op[0].word;                          /* save return address */

        while (TRUE) {
            PR = ReadW (stk_ptr) & VAMASK;              /* point at next instruction set */
            stk_ptr = (stk_ptr + 1) & VAMASK;

            reason = cpu_ops (OP_CCACACCA, op, intrq);  /* get instruction set */

            if (reason) {
                PR = err_PC;                            /* irq restarts */
                break;
                }

            if (op[0].word == 0) {                      /* opcode = NOP? */
                PR = (rtn_addr + 1) & VAMASK;           /* bump to good return */
                break;                                  /* done */
                }

            fp_prec ((uint16) (op[0].word & 0377),      /* determine operand precisions */
                     &op1_prec, &op2_prec, &rslt_prec);

            if (TO_COUNT(op1_prec) != op[1].word) {     /* first operand precisions agree? */
                PR = rtn_addr;                          /* no, so take error return */
                break;
                }

            else if (op1_prec != fp_a)                  /* operand in accumulator? */
                op[1] = ReadOp (op[2].word, op1_prec);  /* no, so get operand 1 */

            if (TO_COUNT(op2_prec) != op[3].word) {     /* second operand precisions agree? */
                PR = rtn_addr;                          /* no, so take error return */
                break;
                }

            else if (op2_prec != fp_a)                  /* operand in accumulator? */
                op[2] = ReadOp (op[4].word, op2_prec);  /* no, so get operand 2 */

            O = O |                                     /* execute instruction */
                fp_exec ((uint16) (op[0].word & 0377),  /* and accumulate overflow */
                                  &fpop, op[1], op[2]);

            if (op[5].word) {                           /* precision conversion? */
                fp_prec ((uint16) (op[5].word & 0377),  /* determine conversion precision */
                         NULL, NULL, &cvt_prec);

                fpop = fp_accum (NULL, cvt_prec);       /* convert result */
                }
            else                                        /* no conversion specified */
                cvt_prec = rslt_prec;                   /* so use original precision */

            if (op[6].word)                             /* store result? */
                WriteOp (op[7].word, fpop, cvt_prec);   /* yes, so write it */
            }

        break;

    case 0010:                                          /* [chk] 105010 (OP_N) */
        YR = 0177777;                                   /* -1 if selection strap OK */
        break;

    case 0014:                                          /* .DAD 105014 (OP_N) */
        return cpu_dbi (0105321, intrq);                /* remap to double int handler */

    case 0034:                                          /* .DSB 105034 (OP_N) */
        return cpu_dbi (0105327, intrq);                /* remap to double int handler */

    case 0054:                                          /* .DMP 105054 (OP_N) */
        return cpu_dbi (0105322, intrq);                /* remap to double int handler */

    case 0074:                                          /* .DDI 105074 (OP_N) */
        return cpu_dbi (0105325, intrq);                /* remap to double int handler */

    case 0100:                                          /*  FIX  105100 (OP_R) */
    case 0101:                                          /* .XFXS 105101 (OP_X) */
    case 0102:                                          /* .TFXS 105102 (OP_T) */
    case 0103:                                          /* .EFXS 105103 (OP_E) */
        O = fp_exec (opcode, &fpop, op[0], NOP);        /* fix to integer */
        AR = fpop.fpk[0];                               /* save result */
        break;

    case 0104:                                          /* .FIXD 105104 (OP_R) */
    case 0105:                                          /* .XFXD 105105 (OP_X) */
    case 0106:                                          /* .TFXD 105106 (OP_T) */
    case 0107:                                          /* .EFXD 105107 (OP_E) */
        O = fp_exec (opcode, &fpop, op[0], NOP);        /* fix to integer */
        AR = (fpop.dword >> 16) & DMASK;                /* save result */
        BR = fpop.dword & DMASK;                        /* in A and B */
        break;

    case 0114:                                          /* .DSBR 105114 (OP_N) */
        return cpu_dbi (0105334, intrq);                /* remap to double int handler */

    case 0120:                                          /*  FLT  105120 (OP_I) */
    case 0124:                                          /* .FLTD 105124 (OP_J) */
        O = fp_exec (opcode, &fpop, op[0], NOP);        /* float to single */
        AR = fpop.fpk[0];                               /* save result */
        BR = fpop.fpk[1];                               /* into A/B */
        break;

    case 0121:                                          /* .XFTS 105121 (OP_IA) */
    case 0122:                                          /* .TFTS 105122 (OP_IA) */
    case 0123:                                          /* .EFTS 105123 (OP_IA) */
    case 0125:                                          /* .XFTD 105125 (OP_JA) */
    case 0126:                                          /* .TFTD 105126 (OP_JA) */
    case 0127:                                          /* .EFTD 105127 (OP_JA) */
        O = fp_exec (opcode, &fpop, op[0], NOP);        /* float integer */
        fp_prec (opcode, NULL, NULL, &rslt_prec);       /* determine result precision */
        WriteOp (op[1].word, fpop, rslt_prec);          /* write result */
        break;

    case 0134:                                          /* .DDIR 105134 (OP_N) */
        return cpu_dbi (0105326, intrq);                /* remap to double int handler */

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
        }

return reason;
}


/* Scientific Instruction Set.

   The SIS adds single-precision trigonometric and logarithmic, and
   double-precision polynomial evaluation instructions to the 1000-F instruction
   set.  The SIS is standard on the 1000-F.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A     std

   The routines are mapped to instruction codes as follows:

     Instr.  1000-F  Description
     ------  ------  ----------------------------------------------
     TAN     105320  Tangent
     SQRT    105321  Square root
     ALOG    105322  Natural logarithm
     ATAN    105323  Arc tangent
     COS     105324  Cosine
     SIN     105325  Sine
     EXP     105326  E to the power X
     ALOGT   105327  Common logarithm
     TANH    105330  Hyperbolic tangent
     DPOLY   105331  Double-precision polynomial evaluation
     /CMRT   105332  Double-precision common range reduction
     /ATLG   105333  Compute (1-x)/(1+x) for .ATAN and .LOG
     .FPWR   105334  Single-precision exponentiation
     .TPWR   105335  Double-precision exponentiation
     [tst]   105337  [self test]

   The SIS simulation follows the F-Series SIS microcode, which, in turn,
   follows the algebraic approximations given in the Relocatable Library manual
   descriptions of the equivalent software routines.

   Notes:

     1. The word following the DPOLY instruction contains up to three flag bits
        to indicate one of several polynomial forms to evaluate.  The comments
        in the DPOLY software library routine source interchange the actions of
        the bit 14 and bit 0 flags.  The DPOLY description in the Technical
        Reference Handbook is correct.

     2. Several instructions (e.g., DPOLY) are documented as leaving undefined
        values in the A, B, X, Y, E, or O registers.  Simulation does not
        attempt to reproduce the same values as would be obtained with the
        hardware.

     3. The SIS uses the hardware FPP of the F-Series.  FPP malfunctions are
        detected by the SIS firmware and are indicated by a memory-protect
        violation and setting the overflow flag.  Under simulation,
        malfunctions cannot occur.

     4. We use OP_IIT for the .FPWR operand pattern.  The "II" is redundant, but
        it aligns the operands with the OP_IAT of .TPWR, so the code may be
        shared.

   Additional references:
    - DOS/RTE Relocatable Library Reference Manual (24998-90001, Oct-1981)
    - HP 1000 E-Series and F-Series Computer Microprogramming Reference Manual
      (02109-90004, Apr-1980).
*/


/* Common single-precision range reduction for SIN, COS, TAN, and EXP.

   This routine is called by the SIN, COS, TAN, and EXP handlers to reduce the
   range of the argument.  Reduction is performed in extended-precision.  We
   calculate:

     multiple = (nearest even integer to argument * multiplier)
     argument = argument * multiplier - multiple
*/

static uint32 reduce (OP *argument, int32 *multiple, OP multiplier)
{
OP product, count;
uint32 overflow;

fp_cvt (argument, fp_f, fp_x);                          /* convert to extended precision */
fp_exec (0041, &product, *argument, multiplier);        /* product = argument * multiplier */
overflow = fp_exec (0111, &count, NOP, NOP);            /* count = FIX (acc) */

if ((int16) count.word >= 0)                            /* nearest even integer */
    count.word = count.word + 1;
count.word = count.word & ~1;
*multiple = (int16) count.word;

if (overflow == 0) {                                    /* in range? */
    fp_exec (0121, ACCUM, count, NOP);                  /* acc = FLT (count) */
    overflow = fp_exec (0025, ACCUM, product, NOP);     /* acc = product - acc */
    *argument = fp_accum (NULL, fp_f);                  /* trim to single-precision */
    }
return overflow;
}


/* SIS dispatcher. */

static const OP_PAT op_sis[16] = {
  OP_R,       OP_R,       OP_R,       OP_R,             /* TAN    SQRT   ALOG   ATAN  */
  OP_R,       OP_R,       OP_R,       OP_R,             /* COS    SIN    EXP    ALOGT */
  OP_R,       OP_CATAKK,  OP_AAT,     OP_A,             /* TANH   DPOLY  /CMRT  /ATLG */
  OP_IIF,     OP_IAT,     OP_N,       OP_N              /* .FPWR  .TPWR   ---   [tst] */
  };

t_stat cpu_sis (uint32 IR, uint32 intrq)
{
OPS op;
OP arg, coeff, pwr, product, count, result;
int16 f, p;
int32 multiple, power, exponent, rsltexp;
uint32 entry, i;
t_bool flag, sign;
t_stat reason = SCPE_OK;

static const OP tan_c4  = { { 0137763, 0051006 } };     /* DEC -4.0030956 */
static const OP tan_c3  = { { 0130007, 0051026 } };     /* DEC -1279.5424 */
static const OP tan_c2  = { { 0040564, 0012761 } };     /* DEC  0.0019974806 */
static const OP tan_c1  = { { 0045472, 0001375 } };     /* DEC  0.14692695 */

static const OP alog_c3 = { { 0065010, 0063002 } };     /* DEC  1.6567626301 */
static const OP alog_c2 = { { 0125606, 0044404 } };     /* DEC -2.6398577035 */
static const OP alog_c1 = { { 0051260, 0037402 } };     /* DEC  1.2920070987 */

static const OP atan_c4 = { { 0040257, 0154404 } };     /* DEC  2.0214656 */
static const OP atan_c3 = { { 0132062, 0133406 } };     /* DEC -4.7376165 */
static const OP atan_c2 = { { 0047407, 0173775 } };     /* DEC  0.154357652 */
static const OP atan_c1 = { { 0053447, 0014002 } };     /* DEC  1.3617611 */

static const OP sin_c4  = { { 0132233, 0040745 } };     /* DEC -0.000035950439 */
static const OP sin_c3  = { { 0050627, 0122361 } };     /* DEC  0.002490001 */
static const OP sin_c2  = { { 0126521, 0011373 } };     /* DEC -0.0807454325 */
static const OP sin_c1  = { { 0062207, 0166400 } };     /* DEC  0.78539816 */

static const OP cos_c4  = { { 0126072, 0002753 } };     /* DEC -0.00031957  */
static const OP cos_c3  = { { 0040355, 0007767 } };     /* DEC  0.015851077 */
static const OP cos_c2  = { { 0130413, 0011377 } };     /* DEC -0.30842483 */
static const OP cos_c1  = { { 0040000, 0000002 } };     /* DEC  1.0 */

static const OP sqrt_a2 = { { 0045612, 0067400 } };     /* DEC  0.5901621 */
static const OP sqrt_b2 = { { 0065324, 0126377 } };     /* DEC  0.4173076 */
static const OP sqrt_a1 = { { 0065324, 0126400 } };     /* DEC  0.8346152 */
static const OP sqrt_b1 = { { 0045612, 0067400 } };     /* DEC  0.5901621 */

static const OP exp_c2  = { { 0073000, 0070771 } };     /* DEC  0.05761803 */
static const OP exp_c1  = { { 0056125, 0041406 } };     /* DEC  5.7708162 */

static const OP tanh_c3 = { { 0050045, 0022004 } };     /* DEC  2.5045337 */
static const OP tanh_c2 = { { 0041347, 0101404 } };     /* DEC  2.0907609 */
static const OP tanh_c1 = { { 0052226, 0047375 } };     /* DEC  0.16520923 */

static const OP minus_1   = { { 0100000, 0000000 } };   /* DEC -1.0 */
static const OP plus_1    = { { 0040000, 0000002 } };   /* DEC +1.0 */
static const OP plus_half = { { 0040000, 0000000 } };   /* DEC +0.5 */
static const OP ln_2      = { { 0054271, 0006000 } };   /* DEC  0.6931471806 (ln 2.0) */
static const OP log_e     = { { 0067455, 0166377 } };   /* DEC  0.43429228 (log e) */
static const OP pi_over_4 = { { 0062207, 0166400 } };   /* Pi / 4.0 */
static const OP pi_over_2 = { { 0062207, 0166402 } };   /* Pi / 2.0 */

static const OP four_over_pi = { { 0050574, 0140667, 0023402 } };   /* 4.0 / Pi */
static const OP two_over_ln2 = { { 0056125, 0016624, 0127404 } };   /* 2.0 / ln(2.0) */

static const OP t_one  = { { 0040000, 0000000, 0000000, 0000002 } };   /* DEY 1.0 */


entry = IR & 017;                                       /* mask to entry point */

if (op_sis [entry] != OP_N) {
    reason = cpu_ops (op_sis [entry], op, intrq);       /* get instruction operands */

    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

    case 000:                                           /* TAN 105320 (OP_R) */
        O = reduce (&op[0], &multiple, four_over_pi);   /* reduce range */

        if (O) {                                        /* out of range? */
            op[0].fpk[0] = '0' << 8 | '9';              /* return '09' */
            op[0].fpk[1] = 'O' << 8 | 'R';              /* return 'OR' */
            break;                                      /* error return is P+1 */
            }

        fp_exec (0040, &op[1], op[0], op[0]);           /* op1 = arg ^ 2 */
        fp_exec (0010, ACCUM, NOP, tan_c4);             /* acc = acc + C4 */
        fp_exec (0064, ACCUM, tan_c3, NOP);             /* acc = C3 / acc */
        fp_exec (0010, ACCUM, NOP, op[1]);              /* acc = acc + op1 */
        fp_exec (0050, ACCUM, NOP, tan_c2);             /* acc = acc * C2 */
        fp_exec (0010, ACCUM, NOP, tan_c1);             /* acc = acc + C1 */
        fp_exec (0050, &op[0], NOP, op[0]);             /* res = acc * arg */

        if (multiple & 0002)                            /* multiple * 2 odd? */
            fp_exec (0064, &op[0], minus_1, NOP);       /* res = -1.0 / acc */

        PR = (PR + 1) & VAMASK;                         /* normal return is P+2 */
        break;


    case 001:                                           /* SQRT 105321 (OP_R) */
        O = 0;                                          /* clear overflow */

        if (op[0].fpk[0] == 0) {                        /* arg = 0? */
            PR = (PR + 1) & VAMASK;                     /* normal return is P+2 */
            break;
            }

        else if ((int16) op[0].fpk[0] < 0) {            /* sqrt of neg? */
            op[0].fpk[0] = '0' << 8 | '3';              /* return '03' */
            op[0].fpk[1] = 'U' << 8 | 'N';              /* return 'UN' */
            O = 1;                                      /* set overflow */
            break;                                      /* error return is P+1 */
            }

        fp_unpack (&op[1], &exponent, op[0], fp_f);     /* unpack argument */

        if (exponent & 1) {                             /* exponent odd? */
            fp_exec (0040, ACCUM, op[1], sqrt_a1);      /* acc = op1 * A1 */
            fp_exec (0010, &op[2], NOP, sqrt_b1);       /* op2 = acc + B1 */
            op[1].fpk[1] = op[1].fpk[1] + 2;            /* op1 = op1 * 2.0 */
            }
        else {                                          /* exponent even */
            fp_exec (0040, ACCUM, op[1], sqrt_a2);      /* acc = op1 * A2 */
            fp_exec (0010, &op[2], NOP, sqrt_b2);       /* op2 = acc + B2 */
            }

        fp_exec (0064, ACCUM, op[1], NOP);              /* acc = op1 / acc */
        fp_exec (0010, &op[2], NOP, op[2]);             /* op2 = acc + op2 */

        op[1].fpk[1] = op[1].fpk[1] + 4;                /* op1 = op1 * 4.0 */

        fp_exec (0064, ACCUM, op[1], NOP);              /* acc = op1 / acc */
        fp_exec (0010, &op[0], NOP, op[2]);             /* res = acc + op2 */

        power = (exponent >> 1) - 2;

        if (op[0].fpk[0]) {                             /* calc x * 2**n */
            fp_unpack (&op[1], &exponent, op[0], fp_f); /* unpack argument */
            exponent = exponent + power;                /* multiply by 2**n */

            if ((exponent > 0177) ||                    /* exponent overflow? */
                (exponent < -0200)) {                   /* or underflow? */
                O = 1;                                  /* rtn unscaled val, set ovf */
                break;                                  /* error return is P+1 */
                }

            else
                fp_pack (&op[0], op[1], exponent, fp_f);/* repack result */
            }

        PR = (PR + 1) & VAMASK;                         /* normal return is P+2 */
        break;


    case 002:                                           /* ALOG  105322 (OP_R) */
    case 007:                                           /* ALOGT 105327 (OP_R) */
        O = 0;                                          /* clear overflow */

        if ((int16) op[0].fpk[0] <= 0) {                /* log of neg or zero? */
            op[0].fpk[0] = '0' << 8 | '2';              /* return '02' */
            op[0].fpk[1] = 'U' << 8 | 'N';              /* return 'UN' */
            O = 1;                                      /* set overflow */
            break;                                      /* error return is P+1 */
            }

        fp_unpack (&op[1], &exponent, op[0], fp_f);     /* unpack argument */

        if (op[0].fpk[0] < 0055000) {                   /* out of range? */
            exponent = exponent - 1;                    /* drop exponent */
            op[1].fpk[1] = op[1].fpk[1] | 2;            /* set "exponent" to 1 */
            }

        op[2].fpk[0] = (HP_WORD) exponent;
        fp_exec (0120, &op[3], op[2], NOP);             /* op3 = FLT(exponent) */

        fp_exec (0020, &op[4], op[1], plus_1);          /* op4 = op1 - 1.0 */
        fp_exec (0000, ACCUM, op[1], plus_1);           /* acc = op1 + 1.0 */
        fp_exec (0064, &op[5], op[4], NOP);             /* op5 = op4 / acc */

        fp_exec (0054, ACCUM, NOP, NOP);                /* acc = acc * acc */
        fp_exec (0030, ACCUM, NOP, alog_c3);            /* acc = acc - c3 */
        fp_exec (0064, ACCUM, alog_c2, NOP);            /* acc = c2 / acc */
        fp_exec (0010, ACCUM, NOP, alog_c1);            /* acc = acc + c1 */
        fp_exec (0050, ACCUM, NOP, op[5]);              /* acc = acc * op5 */
        fp_exec (0010, ACCUM, NOP, op[3]);              /* acc = acc + op3 */
        fp_exec (0050, &op[0], NOP, ln_2);              /* res = acc * ln2 */

        if (entry == 007)                               /* ALOGT? */
            fp_exec (0050, &op[0], NOP, log_e);         /* res = acc * log(e) */

        PR = (PR + 1) & VAMASK;                         /* normal return is P+2 */
        break;


    case 003:                                           /* ATAN  105323 (OP_R) */
        O = 0;                                          /* clear overflow */

        if (op[0].fpk[0] == 0)                          /* argument zero? */
            break;                                      /* result zero */

        flag = (op[0].fpk[1] & 1);                      /* get exponent sign */
        sign = ((int16) op[0].fpk[0] < 0);              /* get argument sign */

        if (flag == 0) {                                /* exp pos? (abs >= 0.5)? */
            if (sign)                                   /* argument negative? */
                fp_pcom (&op[0], fp_f);                 /* make positive */

            if (op[0].fpk[1] & 0374) {                  /* arg >= 2? */
                fp_exec(0060, &op[0], plus_1, op[0]);   /* arg = 1.0 / arg */
                op[2] = pi_over_2;                      /* constant = pi / 2.0 */
                }
            else {
                fp_exec (0020, &op[1], plus_1, op[0]);  /* op1 = 1.0 - arg */
                fp_exec (0000, ACCUM, plus_1, op[0]);   /* acc = 1.0 + arg */
                fp_exec (0064, &op[0], op[1], NOP);     /* arg = op1 / acc */
                op[2] = pi_over_4;                      /* constant = pi / 4.0 */
                }
            }

        fp_exec (0040, &op[1], op[0], op[0]);           /* op1 = arg * arg */
        fp_exec (0010, ACCUM, NOP, atan_c4);            /* acc = acc + C4 */
        fp_exec (0064, ACCUM, atan_c3, NOP);            /* acc = C3 / acc */
        fp_exec (0010, ACCUM, NOP, op[1]);              /* acc = acc + op1 */
        fp_exec (0050, ACCUM, NOP, atan_c2);            /* acc = acc * C2 */
        fp_exec (0010, ACCUM, NOP, atan_c1);            /* acc = acc + C1 */
        fp_exec (0064, &op[0], op[0], NOP);             /* res = arg / acc */

        if (flag == 0) {                                /* exp pos? (abs >= 0.5)? */
            fp_exec (0030, &op[0], NOP, op[2]);         /* res = acc - pi / n */

            if (sign == 0)                              /* argument positive? */
                fp_pcom (&op[0], fp_f);                 /* make negative */
            }

        break;


    case 004:                                           /* COS 105324 (OP_R) */
    case 005:                                           /* SIN 105325 (OP_R) */
        O = reduce (&op[0], &multiple, four_over_pi);   /* reduce range */

        if (O) {                                        /* out of range? */
            op[0].fpk[0] = '0' << 8 | '5';              /* return '05' */
            op[0].fpk[1] = 'O' << 8 | 'R';              /* return 'OR' */
            break;                                      /* error return is P+1 */
            }

        multiple = multiple / 2 + (entry == 004);       /* add one for cosine */
        flag = (multiple & 1);                          /* decide on series */

        fp_exec (0040, &op[1], op[0], op[0]);           /* op1 = arg ^ 2 */

        if (flag) {
            fp_exec (0050, ACCUM, NOP, cos_c4);         /* acc = acc * c4 */
            fp_exec (0010, ACCUM, NOP, cos_c3);         /* acc = acc + c3 */
            fp_exec (0050, ACCUM, NOP, op[1]);          /* acc = acc * op1 */
            fp_exec (0010, ACCUM, NOP, cos_c2);         /* acc = acc + c2 */
            fp_exec (0050, ACCUM, NOP, op[1]);          /* acc = acc * op1 */
            fp_exec (0010, &op[0], NOP, cos_c1);        /* res = acc + c1 */
            }

        else {
            fp_exec (0050, ACCUM, NOP, sin_c4);         /* acc = acc * c4 */
            fp_exec (0010, ACCUM, NOP, sin_c3);         /* acc = acc + c3 */
            fp_exec (0050, ACCUM, NOP, op[1]);          /* acc = acc * op1 */
            fp_exec (0010, ACCUM, NOP, sin_c2);         /* acc = acc + c2 */
            fp_exec (0050, ACCUM, NOP, op[1]);          /* acc = acc * op1 */
            fp_exec (0010, ACCUM, NOP, sin_c1);         /* acc = acc + c1 */
            fp_exec (0050, &op[0], NOP, op[0]);         /* res = acc * arg */
            }

        if (multiple & 0002)                            /* multiple * 2 odd? */
            fp_pcom (&op[0], fp_f);                     /* make negative */

        PR = (PR + 1) & VAMASK;                         /* normal return is P+2 */
        break;


    case 006:                                           /* EXP 105326 (OP_R) */
        sign = ((int16) op[0].fpk[0] < 0);              /* get argument sign */

        O = reduce (&op[0], &multiple, two_over_ln2);   /* reduce range */
        multiple = multiple / 2;                        /* get true multiple */

        if ((sign == 0) && (O | (multiple > 128))) {    /* pos and ovf or out of range? */
            op[0].fpk[0] = '0' << 8 | '7';              /* return '07' */
            op[0].fpk[1] = 'O' << 8 | 'F';              /* return 'OF' */
            O = 1;                                      /* set overflow */
            break;                                      /* error return is P+1 */
            }

        else if (sign && (multiple < -128)) {           /* neg and out of range? */
            op[0].fpk[0] = 0;                           /* result is zero */
            op[0].fpk[1] = 0;
            O = 0;                                      /* clear for underflow */
            PR = (PR + 1) & VAMASK;                     /* normal return is P+2 */
            break;
            }

        fp_exec (0040, ACCUM, op[0], op[0]);            /* acc = arg ^ 2 */
        fp_exec (0050, ACCUM, NOP, exp_c2);             /* acc = acc * c2 */
        fp_exec (0030, ACCUM, NOP, op[0]);              /* acc = acc - op0 */
        fp_exec (0010, ACCUM, NOP, exp_c1);             /* acc = acc + c1 */
        fp_exec (0064, ACCUM, op[0], NOP);              /* acc = op0 / acc */
        fp_exec (0010, &op[0], NOP, plus_half);         /* res = acc + 0.5 */

        power = multiple + 1;

        if (op[0].fpk[0]) {                             /* calc x * 2**n */
            fp_unpack (&op[1], &exponent, op[0], fp_f); /* unpack argument */
            exponent = exponent + power;                /* multiply by 2**n */

            if ((exponent > 0177) ||                    /* exponent overflow? */
                (exponent < -0200)) {                   /* or underflow? */
                if (sign == 0) {                        /* arg positive? */
                    op[0].fpk[0] = '0' << 8 | '7';      /* return '07' */
                    op[0].fpk[1] = 'O' << 8 | 'F';      /* return 'OF' */
                    O = 1;                              /* set overflow */
                    }
                else {
                    op[0].fpk[0] = 0;                   /* result is zero */
                    op[0].fpk[1] = 0;
                    O = 0;                              /* clear for underflow */
                    }
                break;                                  /* error return is P+1 */
                }

            else {
                fp_pack (&op[0], op[1], exponent, fp_f);/* repack value */
                O = 0;
                }
            }

        PR = (PR + 1) & VAMASK;                         /* normal return is P+2 */
        break;


    case 010:                                           /* TANH 105330 (OP_R) */
        O = 0;
        sign = ((int16) op[0].fpk[0] < 0);              /* get argument sign */

        if (op[0].fpk[1] & 1) {                         /* abs (arg) < 0.5? */
            fp_exec (0040, ACCUM, op[0], op[0]);        /* acc = arg ^ 2 */
            fp_exec (0010, ACCUM, NOP, tanh_c3);        /* acc = acc + c3 */
            fp_exec (0064, ACCUM, tanh_c2, NOP);        /* acc = c2 / acc */
            fp_exec (0010, ACCUM, NOP, tanh_c1);        /* acc = acc + c1 */
            fp_exec (0050, &op[0], NOP, op[0]);         /* res = acc * arg */
            }

        else if (op[0].fpk[1] & 0370)                   /* abs (arg) >= 8.0? */
            if (sign)                                   /* arg negative?  */
                op[0] = minus_1;                        /* result = -1.0 */
            else                                        /* arg positive */
                op[0] = plus_1;                         /* result = +1.0 */

        else {                                          /* 0.5 <= abs (arg) < 8.0 */
            BR = BR + 2;                                /* arg = arg * 2.0 */
            cpu_sis (0105326, intrq);                   /* calc exp (arg) */
            PR = (PR - 1) & VAMASK;                     /* correct P (always good rtn) */

            op[0].fpk[0] = AR;                          /* save value */
            op[0].fpk[1] = BR;

            fp_exec (0020, &op[1], op[0], plus_1);      /* op1 = op0 - 1.0 */
            fp_exec (0000, ACCUM, op[0], plus_1);       /* acc = op0 + 1.0 */
            fp_exec (0064, &op[0], op[1], NOP);         /* res = op1 / acc */
            }

        break;


    case 011:                                           /* DPOLY 105331 (OP_CATAKK) */
        O = 0;                                          /* clear overflow */
        AR = op[0].word;                                /* get flag word */

        if ((int16) AR >= 0) {                          /* flags present? */
            AR = 1;                                     /* no, so set default */
            arg = op[2];                                /* arg = X */
            }

        else                                            /* bit 15 set */
            fp_exec (0042, &arg, op[2], op[2]);         /* arg = X ^ 2 */

        coeff = ReadOp (op[3].word, fp_t);              /* get first coefficient */
        op[3].word = (op[3].word + 4) & VAMASK;         /* point at next */
        fp_accum (&coeff, fp_t);                        /* acc = coeff */

        for (i = 0; i < op[4].word; i++) {              /* compute numerator */
            fp_exec (0052, ACCUM, NOP, arg);            /* acc = P[m] * arg */
            coeff = ReadOp (op[3].word, fp_t);          /* get next coefficient */
            op[3].word = (op[3].word + 4) & VAMASK;     /* point at next */
            fp_exec (0012, ACCUM, NOP, coeff);          /* acc = acc + P[m-1] */
            }

        if (AR & 1)                                     /* bit 0 set? */
            op[6] = fp_accum (NULL, fp_t);              /* save numerator */
        else
            fp_exec (0046, &op[6], op[2], NOP);         /* acc = X * acc */


        if (op[5].word) {                               /* n > 0 ? */
            fp_accum (&t_one, fp_t);                    /* acc = 1.0 */

            for (i = 0; i < op[5].word; i++) {          /* compute denominator */
                fp_exec (0052, ACCUM, NOP, arg);        /* acc = P[m] * arg */
                coeff = ReadOp (op[3].word, fp_t);      /* get next coefficient */
                op[3].word = (op[3].word + 4) & VAMASK; /* point at next */
                fp_exec (0012, ACCUM, NOP, coeff);      /* acc = acc + P[m-1] */
                }

            if (AR & 0040000)                           /* bit 14 set? */
                fp_exec (0032, ACCUM, NOP, op[6]);      /* acc = den - num */

            fp_exec (0066, &op[6], op[6], NOP);         /* op6 = num / den */
            }

        WriteOp (op[1].word, op[6], fp_t);              /* write result */

        if (O)                                          /* overflow? */
            op[0].fpk[0] = 0;                           /* microcode rtns with A = 0 */
        break;


    case 012:                                           /* /CMRT 105332 (OP_AAT) */
        O = 0;
        f = (int16) AR;                                 /* save flags */

        coeff = ReadOp (op[1].word, fp_t);              /* get coefficient (C) */

        fp_unpack (NULL, &exponent, op[2], fp_t);       /* unpack exponent */

        if ((f == -1) || (exponent < 4)) {              /* TANH or abs (arg) < 16.0? */

            /* result = x * c - n */

            fp_exec (0042, &product, op[2], coeff);     /* product = arg * C */
            O = fp_exec (0112, &count, NOP, NOP);       /* count = FIX (acc) */

            if ((int16) count.word >= 0)                /* nearest even integer */
                count.word = count.word + 1;
            BR = count.word = count.word & ~1;          /* save LSBs of N */

            O = O | fp_exec (0122, ACCUM, count, NOP);  /* acc = FLT (count) */

            if (O) {                                    /* out of range? */
                op[0].fpk[0] = 0;                       /* microcode rtns with A = 0 */
                break;                                  /* error return is P+1 */
                }

            fp_exec (0026, &result, product, NOP);      /* acc = product - acc */
            fp_unpack (NULL, &rsltexp, result, fp_t);   /* unpack exponent */

            /* determine if cancellation matters */

            if ((f < 0) || (f == 2) || (f == 6) ||      /* EXP, TANH, or COS? */
                (exponent - rsltexp < 5)) {             /* bits lost < 5? */
                WriteOp (op[0].word, result, fp_t);     /* write result */
                PR = (PR + 1) & VAMASK;                 /* P+2 return for good result */
                op[0].fpk[1] = BR;                      /* return LSBs of N in B */
                break;                                  /* all done! */
                }
            }

        /* result = (xu * cu - n) + (x - xu) * c + xu * cl */

        if (exponent >= (8 + 16 * (f >= 0))) {          /* exp >= 8 (EXP,TANH)? */
            op[0].fpk[0] = 0;                           /*  or 24 (SIN/COS/TAN)? */
            break;                                      /* range error return is P+1 */
            }

        op[3].fpk[0] = coeff.fpk[0];                    /* form upper bits of C (CU) */
        op[3].fpk[1] = coeff.fpk[1] & 0177770;
        op[3].fpk[2] = 0;
        op[3].fpk[3] = coeff.fpk[3] & 0000377;

        op[4].fpk[0] = op[2].fpk[0];                    /* form upper bits of X (XU) */
        op[4].fpk[1] = op[2].fpk[1] & 0177770;
        op[4].fpk[2] = 0;
        op[4].fpk[3] = op[2].fpk[3] & 0000377;

        fp_exec (0042, &op[5], op[3], op[4]);           /* op5 = cu * xu */

        fp_exec (0116, &op[6], NOP, NOP);               /* op6 = fix (acc) (2wd) */

        if ((int32) op[6].dword >= 0)                   /* nearest even integer */
            op[6].dword = op[6].dword + 1;
        op[6].dword = op[6].dword & ~1;
        BR = op[6].dword & DMASK;                       /* save LSBs of N */

        O = fp_exec (0126, ACCUM, op[6], NOP);          /* acc = flt (op6) */

        if (O) {                                        /* overflow? */
            op[0].fpk[0] = 0;                           /* microcode rtns with A = 0 */
            break;                                      /* range error return is P+1 */
            }

        fp_exec (0026, &op[7], op[5], NOP);             /* op7 = cu * xu - n */

        fp_exec (0022, ACCUM, op[2], op[4]);            /* acc = x - xu */
        fp_exec (0052, ACCUM, NOP, coeff);              /* acc = (x - xu) * c */
        fp_exec (0012, &op[5], NOP, op[7]);             /* op5 = acc + (cu * xu - n) */

        op[1].word = (op[1].word + 4) & VAMASK;         /* point at second coefficient */
        coeff = ReadOp (op[1].word, fp_t);              /* get coefficient (CL) */

        fp_exec (0042, ACCUM, op[4], coeff);            /* acc = xu * cl */
        fp_exec (0012, &result, NOP, op[5]);            /* result = acc + (x - xu) * c + (cu * xu - n) */

        WriteOp (op[0].word, result, fp_t);             /* write result */
        PR = (PR + 1) & VAMASK;                         /* P+2 return for good result */
        op[0].fpk[1] = BR;                              /* return LSBs of N in B */
        break;


    case 013:                                           /* /ATLG 105333 (OP_A) */
        arg = ReadOp (op[0].word, fp_t);                /* get argument */

        fp_exec (0022, &op[1], t_one, arg);             /* op1 = 1.0 - arg */
        fp_exec (0002, ACCUM, t_one, arg);              /* acc = 1.0 + arg */
        fp_exec (0066, &op[1], op[1], NOP);             /* res = op1 / acc */

        WriteOp (op[0].word, op[1], fp_t);              /* write result */
        break;


    case 014:                                           /* .FPWR 105334 (OP_IIF) */
        p = 0;                                          /* set to single-precision */
        goto NPWR;

    case 015:                                           /* .TPWR 105335 (OP_IAT) */
        p = 2;                                          /* set to double-precision */

    NPWR:
        if (op[2].fpk[0]) {                             /* non-zero base? */
            fp_exec (0120, &pwr, op[0], NOP);           /* float power */

            sign = ((int16) pwr.fpk[0] < 0);            /* save sign of power */
            i = (pwr.fpk[0] << 2) & DMASK;              /* clear it */

            fp_unpack (NULL, &exponent, pwr, fp_f);     /* unpack exponent */

            if (sign == 0)
                exponent = exponent - 1;

            O = 0;                                      /* clear overflow */
            fp_accum (&op[2], (OPSIZE) (fp_f + p));     /* acc = arg */

            while (exponent-- > 0) {
                O = O | fp_exec ((uint16) (0054 | p),   /* square acc */
                                 ACCUM, NOP, NOP);

                if (i & SIGN)
                    O = O | fp_exec ((uint16) (0050 | p),   /* acc = acc * arg */
                                     ACCUM, NOP, op[2]);
                i = i << 1;
                }

            op[2] = fp_accum (NULL, (OPSIZE) (fp_f + p));   /* get accum */

            if (op[2].fpk[0] == 0)                      /* result zero? */
                O = 1;                                  /* underflow */
            }

        if (entry == 014)                               /* .FPWR ? */
            op[0] = op[2];                              /* copy result */
        else                                            /* .TPWR */
            WriteOp (op[1].word, op[2], fp_t);          /* write result */

        break;


    case 017:                                           /* [tst] 105337 (OP_N) */
        XR = 4;                                         /* firmware revision */
        SR = 0102077;                                   /* test passed code */
        PR = (PR + 1) & VAMASK;                         /* P+2 return for firmware w/DPOLY */
        return reason;


    default:                                            /* others unimplemented */
        return STOP (cpu_ss_unimpl);
        }

AR = op[0].fpk[0];                                      /* save result */
BR = op[0].fpk[1];                                      /* into A/B */
return reason;
}

#endif                                                  /* end of int64 support */
