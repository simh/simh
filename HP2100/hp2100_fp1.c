/* hp2100_fp1.c: HP 1000 multiple-precision floating point routines

   Copyright (c) 2005-2017, J. David Bryan

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

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   07-Sep-17    JDB     Replaced "uint16" casts with "HP_WORD" for OP assignments
   16-May-16    JDB     Reformulated the definitions of op_mask
   24-Dec-14    JDB     Added casts for explicit downward conversions
                        Changed fp_ucom return from uint32 to uint16
   18-Mar-13    JDB     Changed type of mantissa masks array to to unsigned
   06-Feb-12    JDB     Added missing precision on constant "one" in fp_trun
   21-Jun-11    JDB     Completed the comments for divide; no code changes
   08-Jun-08    JDB     Quieted bogus gcc warning in fp_exec
   10-May-08    JDB     Fixed uninitialized return in fp_accum when setting
   19-Mar-08    JDB     Reworked "complement" to avoid inlining bug in gcc-4.x
   01-Dec-06    JDB     Reworked into generalized multiple-precision ops for FPP
   12-Oct-06    JDB     Altered x_trun for F-Series FFP compatibility
                        Added F-Series ..TCM FFP helpers

   Primary references:
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
       (92851-90001, Mar-1981)
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - DOS/RTE Relocatable Library Reference Manual
        (24998-90001, Oct-1981)


   This module implements multiple-precision floating-point operations to
   support the 1000 F-Series hardware Floating Point Processor.  It employs
   64-bit integer arithmetic for speed and simplicity of implementation.  The
   host compiler must support 64-bit integers, and the HAVE_INT64 symbol must be
   defined during compilation.  If this symbol is not defined, then FPP support
   is not available.

   HP 2100/1000 computers used a proprietary floating-point format.  The 2100
   had optional firmware that provided two-word floating-point add, subtract,
   multiply, and divide, as well as single-integer fix and float.  The 1000-M/E
   provided the same two-word firmware operations as standard equipment.
   Three-word extended-precision instructions for the 2100 and 1000-M/E were
   provided by the optional Fast FORTRAN Processor firmware.

   The 1000-F substituted a hardware floating point processor for the firmware
   in previous machines.  In addition to the two- and three-word formats, the
   F-Series introduced a four-word double-precision format.  A five-word format
   that provided extra range in the exponent by unpacking it from the mantissa
   was also provided, although this capability was not documented in the user
   manual.  In addition, the FPP improved the accuracy of floating-point
   calculations, as the firmware versions sacrificed a few bits of precision to
   gain speed.  Consequently, operations on the F-Series may return results that
   differ slightly from the same operations on the M/E-Series or the 2100.

   F-Series units after date code 1920 also provided two-word double-integer
   instructions in firmware, as well as double-integer fix and float operations.

   The original 32-bit floating-point format is as follows:

      15 14                                         0
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |MS|              mantissa high                 | : M
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |     mantissa low      |      exponent      |XS| : M + 1
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      15                    8  7                 1  0

   Both 23-bit mantissa and 7-bit exponent are in twos-complement form.  The
   exponent sign bit has been rotated into the LSB of the second word.

   The extended-precision floating-point format is a 48-bit extension of the
   32-bit format used for single precision.  A packed extended-precision value
   consists of a 39-bit mantissa and a 7-bit exponent.  The format is as
   follows:

      15 14                                         0
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |MS|              mantissa high                 | : M
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |                 mantissa middle               | : M + 1
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |     mantissa low      |      exponent      |XS| : M + 2
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      15                    8  7                 1  0

   The double-precision floating-point format is similar to the 48-bit
   extended-precision format, although with a 55-bit mantissa:

      15 14                                         0
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |MS|              mantissa high                 | : M
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |             mantissa middle high              | : M + 1
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |             mantissa middle low               | : M + 2
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |     mantissa low      |      exponent      |XS| : M + 3
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      15                    8  7                 1  0

   The FPP also supports a special five-word expanded-exponent format:

      15 14                                         0
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |MS|              mantissa high                 | : M
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |             mantissa middle high              | : M + 1
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |             mantissa middle low               | : M + 2
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |                mantissa low                   | : M + 3
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |                   exponent                 |XS| : M + 4
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      15                    8  7                 1  0

   The exponent is a full 16-bit twos-complement value, but the allowed range is
   only 10 bits, i.e., -512 to +511.

   In a normalized value, the sign and MSB of the mantissa differ.  Zero is
   represented by all words = 0.

   Internally, unpacked floating-point values are contained in a structure
   having a signed 64-bit mantissa and a signed 32-bit exponent.  Mantissas are
   left-justified with the unused bits masked to zero.  Exponents are
   right-justified.  The precision is indicated by the value of a structure
   field.

   HP terminology for the three-word floating-point format is confused.  Some
   documents refer to it as "double precision," while others use "extended
   precision."  The instruction mnemonics begin with "X" (e.g., .XADD),
   suggesting the extended-precision term.

   HP apparently intended that the four-word double-precision format would be
   called "triple-precision," as the instruction mnemonics begin with "T" (e.g.,
   ".TADD" for the four-word add instruction).  The source files for the
   software simulations of these instructions for the M/E-Series also explicitly
   refer to "triple precision math."  However, the engineering documentation and
   the F-Series reference manual both use the double-precision term.

   This module adopts the single/extended/double terminology and uses the
   initial letters of the instructions (F/X/T) to indicate the precision used.

   The FPP hardware consisted of two circuit boards that interfaced to the main
   CPU via the Microprogrammable Processor Port (MPP) that had been introduced
   with the 1000 E-Series.  One board contained argument registers and ALUs,
   split into separate mantissa and exponent parts.  The other contained a state
   machine sequencer.  FPP results were copied automatically to the argument
   registers in addition to being available over the MPP, so that chained
   operations could be executed from these "accumulators" without reloading.

   The FPP operated independently of the CPU.  An opcode, specifying one of the
   six operations (add, subtract, multiply, divide, fix, or float) was sent to
   the FPP, and a start command was given.  Operands of appropriate precision
   were then supplied to the FPP.  Once the operands were received, the FPP
   would execute and set a flag when the operation was complete.  The result
   would then be retrieved from the FPP.  The floating-point instruction
   firmware in the CPU initiated the desired FPP operation and handled operand
   reads from and result writes to main memory.

   Under simulation, "fp_exec" provides the six arithmetic operations analogous
   to FPP execution.  The remainder of the functions are helpers that were
   provided by firmware in the 1000-F but that can reuse code needed to simulate
   the FPP hardware.  As with the hardware, "fp_exec" retains the last result
   in an internal accumulator that may be referenced in subsequent operations.

   NOTE: this module also provides the floating-point support for the firmware
   single-precision 1000-M/E base set and extended-precision FFP instructions.
   Because the firmware and hardware implementations returned slightly different
   results, particularly with respect to round-off, conditional checks are
   implemented in the arithmetic routines.  In some cases, entirely different
   algorithms are used to ensure fidelity with the real machines.  Functionally,
   this means that the 2100/1000-M/E and 1000-F floating-point diagnostics are
   not interchangeable, and failures are to be expected if a diagnostic is run
   on the wrong machine.
*/

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"
#include "hp2100_fp1.h"


#if defined (HAVE_INT64)                                /* we need int64 support */

/* Field widths. */

#define IN_W_SIGN        1
#define IN_W_SMAGN      15
#define IN_W_DMAGN      31

#define FP_W_MSIGN       1
#define FP_W_FMANT      23
#define FP_W_XMANT      39
#define FP_W_TMANT      55
#define FP_W_EMANT      55
#define FP_W_EXPANDEXP   9
#define FP_W_EXP         7
#define FP_W_ESIGN       1

/* Starting bit numbers. */

#define IN_V_SIGN       (64 - IN_W_SIGN)
#define IN_V_SNUM       (64 - IN_W_SIGN - IN_W_SMAGN)
#define IN_V_DNUM       (64 - IN_W_SIGN - IN_W_DMAGN)

#define FP_V_FNUM       (64 - FP_W_MSIGN - FP_W_FMANT - FP_W_EXP - FP_W_ESIGN)
#define FP_V_XNUM       (64 - FP_W_MSIGN - FP_W_XMANT - FP_W_EXP - FP_W_ESIGN)
#define FP_V_TNUM       (64 - FP_W_MSIGN - FP_W_TMANT - FP_W_EXP - FP_W_ESIGN)
#define FP_V_ENUM       (64 - FP_W_MSIGN - FP_W_EMANT - FP_W_EXP - FP_W_ESIGN)

#define FP_V_MSIGN      (64 - FP_W_MSIGN)
#define FP_V_FMANT      (64 - FP_W_MSIGN - FP_W_FMANT)
#define FP_V_XMANT      (64 - FP_W_MSIGN - FP_W_XMANT)
#define FP_V_TMANT      (64 - FP_W_MSIGN - FP_W_TMANT)
#define FP_V_EMANT      (64 - FP_W_MSIGN - FP_W_EMANT)
#define FP_V_EXP         1
#define FP_V_ESIGN       0

/* Right-aligned field masks. */

#define IN_M_SIGN       (((t_uint64) 1 << IN_W_SIGN)  - 1)
#define IN_M_SMAGN      (((t_uint64) 1 << IN_W_SMAGN) - 1)
#define IN_M_DMAGN      (((t_uint64) 1 << IN_W_DMAGN) - 1)

#define FP_M_MSIGN      (((t_uint64) 1 << FP_W_MSIGN) - 1)
#define FP_M_FMANT      (((t_uint64) 1 << FP_W_FMANT) - 1)
#define FP_M_XMANT      (((t_uint64) 1 << FP_W_XMANT) - 1)
#define FP_M_TMANT      (((t_uint64) 1 << FP_W_TMANT) - 1)
#define FP_M_EMANT      (((t_uint64) 1 << FP_W_EMANT) - 1)

#define FP_M_EXPANDEXP  ((1 << FP_W_EXPANDEXP) - 1)
#define FP_M_EXP        ((1 << FP_W_EXP) - 1)
#define FP_M_ESIGN      ((1 << FP_W_ESIGN) - 1)

/* In-place field masks. */

#define IN_SIGN         (IN_M_SIGN << IN_V_SIGN)
#define IN_SMAGN        (IN_M_SMAGN << IN_V_SNUM)
#define IN_DMAGN        (IN_M_DMAGN << IN_V_DNUM)

#define FP_MSIGN        (FP_M_MSIGN << FP_V_MSIGN)
#define FP_FMANT        (FP_M_FMANT << FP_V_FMANT)
#define FP_XMANT        (FP_M_XMANT << FP_V_XMANT)
#define FP_TMANT        (FP_M_TMANT << FP_V_TMANT)
#define FP_EMANT        (FP_M_EMANT << FP_V_EMANT)
#define FP_EXP          (FP_M_EXP   << FP_V_EXP)
#define FP_ESIGN        (FP_M_ESIGN << FP_V_ESIGN)

/* In-place record masks. */

#define IN_SSMAGN       (IN_SIGN | IN_SMAGN)
#define IN_SDMAGN       (IN_SIGN | IN_DMAGN)

#define FP_SFMANT       (FP_MSIGN | FP_FMANT)
#define FP_SXMANT       (FP_MSIGN | FP_XMANT)
#define FP_STMANT       (FP_MSIGN | FP_TMANT)
#define FP_SEMANT       (FP_MSIGN | FP_EMANT)
#define FP_SEXP         (FP_ESIGN | FP_EXP)

/* Minima and maxima. */

#define FP_ONEHALF      ((t_int64) 1 << (FP_V_MSIGN - 1))   /* mantissa = 0.5 */
#define FP_MAXPMANT     ((t_int64) FP_EMANT)                /* maximum pos mantissa */
#define FP_MAXNMANT     ((t_int64) FP_MSIGN)                /* maximum neg mantissa */
#define FP_MAXPEXP      (FP_M_EXPANDEXP)                    /* maximum pos expanded exponent */
#define FP_MAXNEXP      (-(FP_MAXPEXP + 1))                 /* maximum neg expanded exponent */

/* Floating-point helpers. */

#define DENORM(x)       ((((x) ^ (x) << 1) & FP_MSIGN) == 0)

#define TO_EXP(e)       (int8) ((e >> FP_V_EXP & FP_M_EXP) | \
                                (e & FP_M_ESIGN ? ~FP_M_EXP : 0))

/* Property constants. */

static const t_int64 p_half_lsb[6] = { ((t_int64) 1 << IN_V_SNUM) - 1,     /* different than FP! */
                                       ((t_int64) 1 << IN_V_DNUM) - 1,     /* different than FP! */
                                       (t_int64) 1 << (FP_V_FMANT - 1),
                                       (t_int64) 1 << (FP_V_XMANT - 1),
                                       (t_int64) 1 << (FP_V_TMANT - 1),
                                       (t_int64) 1 << (FP_V_EMANT - 1) };

static const t_int64 n_half_lsb[6] = { 0,
                                       0,
                                       ((t_int64) 1 << (FP_V_FMANT - 1)) - 1,
                                       ((t_int64) 1 << (FP_V_XMANT - 1)) - 1,
                                       ((t_int64) 1 << (FP_V_TMANT - 1)) - 1,
                                       ((t_int64) 1 << (FP_V_EMANT - 1)) - 1 };

static const uint32  op_start[6]   = { IN_V_SNUM,
                                       IN_V_DNUM,
                                       FP_V_FMANT,
                                       FP_V_XMANT,
                                       FP_V_TMANT,
                                       FP_V_EMANT };

static const t_uint64 mant_mask[6] = { IN_SSMAGN,
                                       IN_SDMAGN,
                                       FP_SFMANT,
                                       FP_SXMANT,
                                       FP_STMANT,
                                       FP_SEMANT };

static const uint32  op_bits[6]    = { IN_W_SMAGN,
                                       IN_W_DMAGN,
                                       FP_W_FMANT + FP_W_MSIGN,
                                       FP_W_XMANT + FP_W_MSIGN,
                                       FP_W_TMANT + FP_W_MSIGN,
                                       FP_W_EMANT + FP_W_MSIGN };

static const t_int64 op_mask[6]    = { ~(((t_int64) 1 << IN_V_SNUM) - 1),
                                       ~(((t_int64) 1 << IN_V_DNUM) - 1),
                                       ~(((t_int64) 1 << FP_V_FNUM) - 1),
                                       ~(((t_int64) 1 << FP_V_XNUM) - 1),
                                       ~(((t_int64) 1 << FP_V_TNUM) - 1),
                                       ~(((t_int64) 1 << FP_V_ENUM) - 1) };

static const uint32  int_p_max[2]  = { IN_M_SMAGN,
                                       IN_M_DMAGN };


/* Internal unpacked floating-point representation. */

typedef struct {
    t_int64     mantissa;
    int32       exponent;
    OPSIZE      precision;
    } FPU;



/* Low-level helper routines. */


/* Arithmetic shift right for mantissa only.

   Returns TRUE if any one-bits are shifted out (for F-series only).
*/

static t_bool asr (FPU *operand, int32 shift)
{
t_uint64 mask;
t_bool bits_lost;

if (UNIT_CPU_MODEL == UNIT_1000_F) {                    /* F-series? */
    mask = ((t_uint64) 1 << shift) - 1;                 /* mask for lost bits */
    bits_lost = ((operand->mantissa & mask) != 0);      /* flag if any lost */
    }
else
    bits_lost = FALSE;

operand->mantissa = operand->mantissa >> shift;         /* mantissa is int, so ASR */
return bits_lost;
}


/* Logical shift right for mantissa and exponent.

   Shifts mantissa and corrects exponent for mantissa overflow.
   Returns TRUE if any one-bits are shifted out (for F-series only).
*/

static t_bool lsrx (FPU *operand, int32 shift)
{
t_uint64 mask;
t_bool bits_lost;

if (UNIT_CPU_MODEL == UNIT_1000_F) {                    /* F-series? */
    mask = ((t_uint64) 1 << shift) - 1;                 /* mask for lost bits */
    bits_lost = ((operand->mantissa & mask) != 0);      /* flag if any lost */
    }
else
    bits_lost = FALSE;

operand->mantissa = (t_uint64) operand->mantissa >> shift;  /* uint, so LSR */
operand->exponent = operand->exponent + shift;          /* correct exponent */
return bits_lost;
}


/* Unpack an operand into a long integer.

   Returns a left-aligned integer or mantissa.  Does not mask to precision; this
   should be done subsequently if desired.
*/

static t_int64 unpack_int (OP packed, OPSIZE precision)
{
uint32 i;
t_uint64 unpacked = 0;

if (precision == in_s)
    unpacked = (t_uint64) packed.word << 48;            /* unpack single integer */

else if (precision == in_d)
    unpacked = (t_uint64) packed.dword << 32;           /* unpack double integer */

else {
    if (precision == fp_e)                              /* five word operand? */
        precision = fp_t;                               /* only four mantissa words */

    for (i = 0; i < 4; i++)                             /* unpack fp 2 to 4 words */
        if (i < TO_COUNT (precision))
            unpacked = unpacked << 16 | packed.fpk[i];
        else
            unpacked = unpacked << 16;
    }

return (t_int64) unpacked;
}


/* Unpack a packed operand.

   The packed value is split into separate mantissa and exponent variables.  The
   multiple words of the mantissa are concatenated into a single 64-bit signed
   value, and the exponent is shifted with recovery of the sign.
*/

static FPU unpack (OP packed, OPSIZE precision)
{
FPU unpacked;

unpacked.precision = precision;                         /* set value's precision */

unpacked.mantissa =                                     /* unpack and mask mantissa */
    unpack_int (packed, precision) & (t_int64) mant_mask[precision];

switch (precision) {

    case fp_f:
    case fp_x:
    case fp_t:
        unpacked.exponent =                             /* unpack exponent from correct word */
            TO_EXP (packed.fpk[(uint32) precision - 1]);
        break;

    case fp_e:
        unpacked.exponent =                             /* unpack expanded exponent */
            (int16) (packed.fpk[4] >> FP_V_EXP |        /* rotate sign into place */
                     (packed.fpk[4] & 1 ? SIGN : 0));
        break;

    case fp_a:                                          /* no action for value in accum */
    case in_s:                                          /* integers don't use exponent */
    case in_d:                                          /* integers don't use exponent */
    default:
        unpacked.exponent = 0;
        break;
    }

return unpacked;
}


/* Pack a long integer into an operand. */

static OP pack_int (t_int64 unpacked, OPSIZE precision)
{
int32 i;
OP packed;

if (precision == in_s)
    packed.word = (HP_WORD) (unpacked >> 48) & DMASK;   /* pack single integer */

else if (precision == in_d)
    packed.dword = (uint32) (unpacked >> 32) & DMASK32; /* pack double integer */

else {
    if (precision == fp_e)                              /* five word operand? */
        precision = fp_t;                               /* only four mantissa words */

    for (i = 3; i >= 0; i--) {                          /* pack fp 2 to 4 words */
        packed.fpk[i] = (HP_WORD) unpacked & DMASK;
        unpacked = unpacked >> 16;
        }
    }

return packed;
}


/* Pack an unpacked floating-point number.

   The 64-bit mantissa is split into the appropriate number of 16-bit words.
   The exponent is rotated to incorporate the sign bit and merged into the
   appropriate word.
*/

static OP pack (FPU unpacked)
{
OP packed;
uint8 exp;

packed = pack_int (unpacked.mantissa, unpacked.precision);  /* pack mantissa */

exp = (uint8) (unpacked.exponent << FP_V_EXP |          /* rotate exponent */
              (unpacked.exponent < 0) << FP_V_ESIGN);

switch (unpacked.precision) {                           /* merge exponent into correct word */

    case in_s:                                          /* no action for integers */
    case in_d:
        break;

    case fp_f:                                          /* merge into last word */
    case fp_x:
    case fp_t:
        packed.fpk[(uint32) unpacked.precision - 1] =
            (packed.fpk[(uint32) unpacked.precision - 1] & ~FP_SEXP) | exp;
        break;

    case fp_e:                                          /* place in separate word */
        packed.fpk[4] = (HP_WORD) (unpacked.exponent << FP_V_EXP |
                                  (unpacked.exponent < 0) << FP_V_ESIGN);
        break;

    case fp_a:                                          /* no action for value in accum */
        break;
    }

return packed;
}


/* Normalize an unpacked floating-point number.

   Floating-point numbers are in normal form if the sign bit and the MSB of the
   mantissa differ.  Unnormalized numbers are shifted as needed with appropriate
   exponent modification.
*/

static void normalize (FPU *unpacked)
{

if (unpacked->mantissa)                                 /* non-zero? */
    while (DENORM (unpacked->mantissa)) {               /* normal form? */
        unpacked->exponent = unpacked->exponent - 1;    /* no, so left shift */
        unpacked->mantissa = unpacked->mantissa << 1;
        }
else
    unpacked->exponent = 0;                             /* clean for zero */
return;
}


/* Round an unpacked floating-point number and check for overflow.

   An unpacked floating-point number is rounded by adding one-half of the LSB
   value, maintaining symmetry around zero.  If rounding resulted in a mantissa
   overflow, the result logically is shifted to the right with an appropriate
   exponent modification.  Finally, the result is checked for exponent underflow
   or overflow, and the appropriate approximation (zero or infinity) is
   returned.

   Rounding in hardware involves a special mantissa extension register that
   holds three "guard" bits and one "sticky" bit.  These represent the value of
   bits right-shifted out the mantissa register.  Under simulation, we track
   such right-shifts and utilize the lower eight bits of the 64-bit mantissa
   value to simulate the extension register.

   Overflow depends on whether the FPP expanded-exponent form is being used
   (this expands the exponent range by two bits).  If overflow is detected, the
   value representing infinity is dependent on whether the operation is on
   behalf of the Fast FORTRAN Processor.  The F-Series FPP returns positive
   infinity on both positive and negative overflow for all precisions.  The 2100
   and M/E-Series FFPs return negative infinity on negative overflow of
   extended-precision values.  Single-precision overflows on these machines
   always return positive infinity.

   The number to be rounded must be normalized upon entry.
*/

static uint32 roundovf (FPU *unpacked, t_bool expand)
{
uint32 overflow;
t_bool sign;

sign = (unpacked->mantissa < 0);                        /* save mantissa sign */

if (sign)                                               /* round and mask the number */
    unpacked->mantissa =
        (unpacked->mantissa + n_half_lsb[unpacked->precision]) &
        (t_int64) mant_mask[unpacked->precision];
else
    unpacked->mantissa =
        (unpacked->mantissa + p_half_lsb[unpacked->precision]) &
        (t_int64) mant_mask[unpacked->precision];

if (sign != (unpacked->mantissa < 0))                   /* mantissa overflow? */
    lsrx (unpacked, 1);                                 /* correct by shifting */
else
    normalize (unpacked);                               /* renorm may be needed */

if (unpacked->mantissa == 0) {                          /* result zero? */
    unpacked->mantissa = 0;                             /* return zero */
    unpacked->exponent = 0;
    overflow = 0;                                       /* with overflow clear */
    }
else if (unpacked->exponent <                           /* result underflow? */
         (FP_MAXNEXP >> (expand ? 0 : 2))) {
    unpacked->mantissa = 0;                             /* return zero */
    unpacked->exponent = 0;
    overflow = 1;                                       /* and set overflow */
    }
else if (unpacked->exponent >                           /* result overflow? */
         (FP_MAXPEXP >> (expand ? 0 : 2))) {
    if (sign &&                                         /* negative value? */
        (unpacked->precision == fp_x) &&                /* extended precision? */
        (UNIT_CPU_MODEL != UNIT_1000_F)) {              /* not F-series? */
        unpacked->mantissa = FP_MAXNMANT;               /* return negative infinity */
        unpacked->exponent = FP_MAXPEXP & FP_M_EXP;
        }
    else {
        unpacked->mantissa = FP_MAXPMANT;               /* return positive infinity */
        unpacked->exponent = FP_MAXPEXP & FP_M_EXP;
        }
    overflow = 1;                                       /* and set overflow */
    }
else
    overflow = 0;                                       /* value is in range */

return overflow;
}


/* Normalize, round, and pack an unpacked floating-point number. */

static uint32 nrpack (OP *packed, FPU unpacked, t_bool expand)
{
uint32 overflow;

normalize (&unpacked);                                  /* normalize for rounding */
overflow = roundovf (&unpacked, expand);                /* round and check for overflow */
*packed = pack (unpacked);                              /* pack result */

return overflow;
}



/* Low-level arithmetic routines. */


/* Complement an unpacked number. */

static void complement (FPU *result)
{
if (result->mantissa == FP_MAXNMANT) {                  /* maximum negative? */
    result->mantissa = FP_ONEHALF;                      /* complement of -1.0 * 2 ^ n */
    result->exponent = result->exponent + 1;            /* is 0.5 * 2 ^ (n + 1) */
    }
else
    result->mantissa = -result->mantissa;               /* negate mantissa */
return;
}


/* Add two unpacked numbers.

   The mantissas are first aligned if necessary by scaling the smaller of the
   two operands.  If the magnitude of the difference between the exponents is
   greater than the number of significant bits, then the smaller number has been
   scaled to zero (swamped), and so the sum is simply the larger operand.
   Otherwise, the sum is computed and checked for overflow, which has occurred
   if the signs of the operands are the same but differ from that of the result.
   Scaling and renormalization is performed if overflow occurred.
*/

static void add (FPU *sum, FPU augend, FPU addend)
{
int32 magn;
t_bool bits_lost;

if (augend.mantissa == 0)
    *sum = addend;                                      /* X + 0 = X */

else if (addend.mantissa == 0)
    *sum = augend;                                      /* 0 + X = X */

else {
    magn = augend.exponent - addend.exponent;           /* difference exponents */

    if (magn > 0) {                                     /* addend smaller? */
        *sum = augend;                                  /* preset augend */
        bits_lost = asr (&addend, magn);                /* align addend */
        }
    else {                                              /* augend smaller? */
        *sum = addend;                                  /* preset addend */
        magn = -magn;                                   /* make difference positive */
        bits_lost = asr (&augend, magn);                /* align augend */
        }

    if (magn <= (int32) op_bits[augend.precision]) {    /* value swamped? */
        sum->mantissa =                                 /* no, add mantissas */
            addend.mantissa + augend.mantissa;

        if (((addend.mantissa < 0) == (augend.mantissa < 0)) && /* mantissa overflow? */
            ((addend.mantissa < 0) != (sum->mantissa < 0))) {
            bits_lost = bits_lost | lsrx (sum, 1);      /* restore value */
            sum->mantissa =                             /* restore sign */
                sum-> mantissa | (addend.mantissa & FP_MSIGN);
            }

        if (bits_lost)                                  /* any bits lost? */
            sum->mantissa = sum->mantissa | 1;          /* include one for rounding */
        }
    }
return;
}


/* Multiply two unpacked numbers.

   The single-precision firmware (FMP) operates differently from the firmware
   extended-precision (.XMPY) and the hardware multiplies of any precision.
   Firmware implementations use the MPY micro-order to form 16-bit x 16-bit =
   32-bit partial products and sum them to form the result.  The hardware uses a
   series of shifts and adds.  This means that firmware FMP and hardware FMP
   return slightly different values, as may be seen by attempting to run the
   firmware FMP diagnostic on the FPP.

   The FMP microcode calls a signed multiply routine to calculate three partial
   products (all but LSB * LSB).  Because the LSBs are unsigned, i.e., all bits
   significant, the two MSB * LSB products are calculated using LSB/2.  The
   unsigned right-shift ensures a positive LSB with no significant bits lost,
   because the lower eight bits are unused (they held the vacated exponent).  In
   order to sum the partial products, the LSB of the result of MSB * MSB is also
   right-shifted before addition.  Note, though, that this loses a significant
   bit.  After summation, the result is left-shifted to correct for the original
   right shifts.

   The .XMPY microcode negates both operands as necessary to produce positive
   values and then forms six of the nine 16-bit x 16-bit = 32-bit unsigned
   multiplications required for a full 96-bit product.  Given a 48-bit
   multiplicand "a1a2a3" and a 48-bit multiplier "b1b2b3", the firmware performs
   these calculations to develop a 48-bit product:

                                 a1      a2      a3
                             +-------+-------+-------+
                                 b1      b2      b3
                             +-------+-------+-------+
                             _________________________

                         a1  *   b3      [p1]
                     +-------+-------+
                         a2  *   b2      [p2]
                     +-------+-------+
                 a1  *   b2              [p3]
             +-------+-------+
                         a3  *   b1      [p4]
                     +-------+-------+
                 a2  *   b1              [p5]
             +-------+-------+
         a1  *   b1                      [p6]
     +-------+-------+
     _________________________________

              product
     +-------+-------+-------+

   The least-significant words of partial products [p1], [p2], and [p4] are used
   only to develop a carry bit into the 48-bit sum.  The product is complemented
   as necessary to restore the sign.

   The basic FPP hardware algorithm scans the multiplier and adds a shifted copy
   of the multiplicand whenever a one-bit is detected.  To avoid successive adds
   when a string of ones is encountered (because adds are more expensive than
   shifts), the hardware instead adds the multiplicand shifted by N + 1 + P and
   subtracts the multiplicand shifted by P to obtain the equivalent value with a
   maximum of two operations.

   Instead of implementing either the .XMPY firmware algorithm or the hardware
   shift-and-add algorithm directly, it is more efficient under simulation to
   use 32 x 32 = 64-bit multiplications, thereby reducing the number required
   from six to four (64-bit "c1c2" x 64-bit "d1d2"):

                         ah      al
                     +-------+-------+
                         bh      bl
                     +-------+-------+
                     _________________

                         al  *   bl      [ll]
                     +-------+-------+
                 ah  *   bl              [hl]
             +-------+-------+
                 al  *   bh              [lh]
             +-------+-------+
         ah  *   bh                      [hh]
     +-------+-------+
     _________________________________

          product
     +-------+-------+

   However, the FMP algorithm is implemented directly from the microcode to
   preserve the fidelity of the simulation, i.e., to lose the same amount
   of precision.
*/

static void multiply (FPU *product, FPU multiplicand, FPU multiplier)
{
uint32 ah, al, bh, bl, sign = 0;
t_uint64 hh, hl, lh, ll, carry;
int16 ch, cl, dh, dl;
t_bool firmware;

product->precision = multiplicand.precision;            /* set precision */

if ((multiplicand.mantissa == 0) ||                     /* 0 * X = 0 */
    (multiplier.mantissa == 0))                         /* X * 0 = 0 */
    product->mantissa = product->exponent = 0;

else {
    firmware = (UNIT_CPU_MODEL != UNIT_1000_F);         /* set firmware flag */

    if (!firmware || (product->precision != fp_f)) {    /* hardware? */
        if (multiplicand.mantissa < 0) {                /* negative? */
            complement (&multiplicand);                 /* complement operand */
            sign = ~sign;                               /* track sign */
            }
        if (multiplier.mantissa < 0) {                  /* negative? */
            complement (&multiplier);                   /* complement operand */
            sign = ~sign;                               /* track sign */
            }
        }

    product->exponent =                                 /* compute exponent */
        multiplicand.exponent + multiplier.exponent + 1;

    ah = (uint32) (multiplicand.mantissa >> 32);        /* split multiplicand */
    al = (uint32) (multiplicand.mantissa & DMASK32);    /* into high and low parts */
    bh = (uint32) (multiplier.mantissa >> 32);          /* split multiplier */
    bl = (uint32) (multiplier.mantissa & DMASK32);      /* into high and low parts */

    if (firmware && (product->precision == fp_f)) {     /* single-precision firmware? */
        ch = (int16) (ah >> 16) & DMASK;                /* split 32-bit multiplicand */
        cl = (int16) (ah & 0xfffe);                     /* into high and low parts */
        dh = (int16) (bh >> 16) & DMASK;                /* split 32-bit multiplier */
        dl = (int16) (bh & 0xfffe);                     /* into high and low parts */

        hh = (t_uint64) (((int32) ch * dh) & ~1);       /* form cross products */
        hl = (t_uint64) (((t_int64) ch * (t_int64) (uint16) dl +
                          (t_int64) dh * (t_int64) (uint16) cl) &
                         0xfffffffffffe0000);

        product->mantissa = (t_uint64) (((t_int64) hh << 32) +      /* sum partials */
                                        ((t_int64) hl << 16));
        }

    else {
        hh = ((t_uint64) ah * bh);                      /* form four cross products */
        hl = ((t_uint64) ah * bl);                      /* using 32 x 32 = */
        lh = ((t_uint64) al * bh);                      /* 64-bit multiplies */
        ll = ((t_uint64) al * bl);

        carry = ((ll >> 32) + (uint32) hl + (uint32) lh) >> 32;     /* form carry */

        product->mantissa = hh + (hl >> 32) + (lh >> 32) + carry;   /* sum partials */

        if (sign)                                           /* negate if required */
            complement (product);
        }
    }
return;
}


/* Divide two unpacked numbers.

   As with multiply, the single-precision firmware (FDV) operates differently
   from the firmware extended-precision (.XDIV) and the hardware divisions of
   any precision.  Firmware implementations use the DIV micro-order to form
   32-bit / 16-bit = 16-bit quotients and 16-bit remainders.  These are used in
   a "divide and correct" algorithm, wherein the quotient is estimated and then
   corrected by comparing the dividend to the product of the quotient and the
   divisor.  The hardware uses a series of shifts and subtracts.  This means
   that firmware FDV and hardware FDV once again return slightly different
   values.

   Under simulation, the classic divide-and-correct method is employed, using
   64-bit / 32-bit = 32-bit divisions.  This method considers the 64-bit
   dividend and divisor each to consist of two 32-bit "digits."  The 64-bit
   dividend "a1a2a3a4" is divided by the first 32-bit digit "b1b2" of the 64-bit
   divisor "b1b2b3b4", yielding a 32-bit trial quotient digit and a 32-bit
   remainder digit.  A correction is developed by subtracting the product of the
   second 32-bit digit "b3b4" of the divisor and the trial quotient digit from
   the remainder (we take advantage of the eight bits vacated by the exponent
   during unpacking to ensure that this product will not overflow into the sign
   bit).  If the remainder is negative, the trial quotient is too large, so it
   is decremented, and the (full 64-bit) divisor is added to the correction.
   This is repeated until the correction is non-negative, indicating that the
   first quotient digit is correct.  The process is then repeated using the
   remainder as the dividend to develop the second 32-bit digit of the quotient.
   The two digits are then concatenated for produce the final 64-bit value.

   (See, "Divide-and-Correct Methods for Multiple Precision Division" by Marvin
   L. Stein, Communications of the ACM, August 1964 for background.)

   The microcoded single-precision division avoids overflows by right-shifting
   some values, which leads to a loss of precision in the LSBs.  We duplicate
   the firmware algorithm here to preserve the fidelity of the simulation.
*/

static void divide (FPU *quotient, FPU dividend, FPU divisor)
{
uint32 sign = 0;
t_int64 bh, bl, r1, r0, p1, p0;
t_uint64 q, q1, q0;
t_bool firmware;
int32 ah, div, cp;
int16 dh, dl, pq1, pq2, cq;

quotient->precision = dividend.precision;               /* set precision */

if (divisor.mantissa == 0) {                            /* division by zero? */
    if (dividend.mantissa < 0)
        quotient->mantissa = FP_MSIGN;                  /* return minus infinity */
    else
        quotient->mantissa = ~FP_MSIGN;                 /* or plus infinity */
    quotient->exponent = FP_MAXPEXP + 1;
    }

else if (dividend.mantissa == 0)                        /* dividend zero? */
    quotient->mantissa = quotient->exponent = 0;        /* yes; result is zero */

else {
    firmware = (UNIT_CPU_MODEL != UNIT_1000_F);         /* set firmware flag */

    if (!firmware || (quotient->precision != fp_f)) {   /* hardware or FFP? */
        if (dividend.mantissa < 0) {                    /* negative? */
            complement (&dividend);                     /* complement operand */
            sign = ~sign;                               /* track sign */
            }
        if (divisor.mantissa < 0) {                     /* negative? */
            complement (&divisor);                      /* complement operand */
            sign = ~sign;                               /* track sign */
            }
        }

    quotient->exponent =                                /* division subtracts exponents */
        dividend.exponent - divisor.exponent;

    bh = divisor.mantissa >> 32;                        /* split divisor */
    bl = divisor.mantissa & DMASK32;                    /* into high and low parts */

    if (firmware && (quotient->precision == fp_f)) {    /* single-precision firmware? */
        quotient->exponent = quotient->exponent + 1;    /* fix exponent */

        ah = (int32) (dividend.mantissa >> 32);         /* split dividend */
        dh = (int16) (bh >> 16);                        /* split divisor again */
        dl = (int16) bh;

        div = ah >> 2;                                  /* ASR 2 to prevent overflow */

        pq1 = (int16) (div / dh);                       /* form first partial quotient */
        div = ((div % dh) & ~1) << 15;                  /* ASR 1, move rem to upper */
        pq2 = (int16) (div / dh);                       /* form second partial quotient */

        div = (uint16) dl << 13;                        /* move divisor LSB to upper, LSR 3 */
        cq = (int16) (div / dh);                        /* form correction quotient */
        cp = -cq * pq1;                                 /* and correction product */

        cp = (((cp >> 14) & ~3) + (int32) pq2) << 1;    /* add corr prod and 2nd partial quo */
        quotient->mantissa =                            /* add 1st partial quo and align */
            (t_uint64) (((int32) pq1 << 16) + cp) << 32;
        }

    else {                                              /* hardware or FFP */
        q1 = (t_uint64) (dividend.mantissa / bh);       /* form 1st trial quotient */
        r1 = dividend.mantissa % bh;                    /* and remainder */
        p1 = (r1 << 24) - (bl >> 8) * q1;               /* calculate correction */

        while (p1 < 0) {                                /* correction needed? */
            q1 = q1 - 1;                                /* trial quotient too large */
            p1 = p1 + (divisor.mantissa >> 8);          /* increase remainder */
            }

        q0 = (t_uint64) ((p1 << 8) / bh);               /* form 2nd trial quotient */
        r0 = (p1 << 8) % bh;                            /* and remainder */
        p0 = (r0 << 24) - (bl >> 8) * q0;               /* calculate correction */

        while (p0 < 0) {                                /* correction needed? */
            q0 = q0 - 1;                                /* trial quotient too large */
            p0 = p0 + (divisor.mantissa >> 8);          /* increase remainder */
            }

        q = (q1 << 32) + q0;                            /* sum quotient digits */

        if (q1 & 0xffffffff00000000) {                  /* did we lose MSB? */
            q = (q >> 1) | 0x8000000000000000;          /* shift right and replace bit */
            quotient->exponent = quotient->exponent + 1;/* bump exponent for shift */
            }

        if (q & 0x8000000000000000)                     /* lose normalization? */
            q = q >> 1;                                 /* correct */

        quotient->mantissa = (t_int64) q;
        }

    if (sign)
        complement (quotient);                          /* negate if required */
    }
return;
}


/* Fix an unpacked number.

   A floating-point value is converted to an integer.  The desired precision of
   the result (single or double integer) must be set before calling.

   Values less than 0.5 (i.e., with negative exponents) underflow to zero.  If
   the value exceeds the specified integer range, the maximum integer value is
   returned and overflow is set.  Otherwise, the floating-point value is
   right-shifted to zero the exponent.  The result is then rounded.
*/

static uint32 fix (FPU *result, FPU operand)
{
uint32 overflow;
t_bool bits_lost;

if (operand.exponent < 0) {                             /* value < 0.5? */
    result->mantissa = 0;                               /* result rounds to zero */
    overflow = 0;                                       /* clear for underflow */
    }

else if (operand.exponent >                             /* value > integer size? */
         (int32) op_bits[result->precision]) {
    result->mantissa =                                  /* return max int value */
        (t_uint64) int_p_max[result->precision] <<
        op_start[result->precision];
    overflow = 1;                                       /* and set overflow */
    }

else {                                                  /* value in range */
    bits_lost = asr (&operand,                          /* shift to zero exponent */
                     op_bits[result->precision] - operand.exponent);

    if (operand.mantissa < 0) {                         /* value negative? */
        if (bits_lost)                                  /* bits lost? */
            operand.mantissa = operand.mantissa | 1;    /* include one for rounding */

        operand.mantissa = operand.mantissa +           /* round result */
                           p_half_lsb[result->precision];
        }

    result->mantissa = operand.mantissa &               /* mask to precision */
                       op_mask[result->precision];
    overflow = 0;
    }

result->exponent = 0;                                   /* tidy up for integer value */
return overflow;
}


/* Float an integer to an unpacked number.

   An integer is converted to a floating-point value.  The desired precision of
   the result must be set before calling.

   Conversion is simply a matter of copying the integer value, setting an
   exponent that reflects the right-aligned position of the bits, and
   normalizing.
*/

static void ffloat (FPU *result, FPU operand)
{
result->mantissa = operand.mantissa;                    /* set value */
result->exponent = op_bits[operand.precision];          /* set exponent */
normalize (result);                                     /* normalize */
return;
}



/* High-level floating-point routines. */


/* Determine operand precisions.

   The precisions of the operands and result are determined by decoding an
   operation opcode and returned to the caller.  Pass NULL for both of the
   operands if only the result precision is wanted.  Pass NULL for the result if
   only the operand precisions are wanted.

   Implementation note:

    1. gcc-4.3.0 complains at -O3 that operand_l/r may not be initialized.
       Because of the mask, the switch statement covers all cases, but gcc
       doesn't realize this.  The "default" case is redundant but eliminates the
       warning.
*/

void fp_prec (uint16 opcode, OPSIZE *operand_l, OPSIZE *operand_r, OPSIZE *result)
{
OPSIZE fp_size, int_size;

fp_size  = (OPSIZE) ((opcode & 0003) + 2);              /* fp_f, fp_x, fp_t, fp_e */
int_size = (OPSIZE) ((opcode & 0004) >> 2);             /* in_s, in_d */

if (operand_l && operand_r) {                           /* want operand precisions? */
    switch (opcode & 0120) {                            /* mask out opcode bit 5 */
        case 0000:                                      /* add/mpy */
        case 0020:                                      /* sub/div */
            *operand_l = fp_size;                       /* assume first op is fp */

            if (opcode & 0004)                          /* operand internal? */
                *operand_r = fp_a;                      /* second op is accum */
            else
                *operand_r = fp_size;                   /* second op is fp */
            break;

        case 0100:                                      /* fix/accum as integer */
            *operand_l = fp_size;                       /* first op is fp */
            *operand_r = fp_a;                          /* second op is always null */
            break;

        case 0120:                                      /* flt/accum as float */
        default:                                        /* keeps compiler quiet for uninit warning */
            *operand_l = int_size;                      /* first op is integer */
            *operand_r = fp_a;                          /* second op is always null */
            break;
        }

    if (opcode & 0010)                                  /* operand internal? */
        *operand_l = fp_a;                              /* first op is accum */
    }

if (result)                                             /* want result precision? */
    if ((opcode & 0120) == 0100)                        /* fix? */
        *result = int_size;                             /* result is integer */
    else                                                /* all others */
        *result = fp_size;                              /* result is fp */

return;
}


/* Floating Point Processor executor.

   The executor simulates the MPP interface between the CPU and the FPP.  The
   operation to be performed is specified by the supplied opcode, which conforms
   to the FPP hardware interface, as follows:

     Bits  Value  Action
     ----  -----  ----------------------------------------------
       7     0    Exponent range is standard (+/-127)
             1    Exponent range is expanded (+/-511)

      6-4   000   Add
            001   Subtract
            010   Multiply
            011   Divide
            100   Fix
            101   Float
            110   (diagnostic)
            111   (diagnostic)

       3     0    Left operand is supplied
             1    Left operand in accumulator

       2     0    Right operand is supplied (ADD/SUB/MPY/DIV)
                  Single integer operation (FIX/FLT)
             1    Right operand in accumulator (ADD/SUB/MPY/DIV)
                  Double integer operation (FIX/FLT)

      1-0    00   2-word operation
             01   3-word operation
             10   4-word operation
             11   5-word operation

   If the opcode specifies that the left (or right) operand is in the
   accumulator, then the value supplied for that parameter is not used.  All
   results are automatically left in the accumulator.  If the result is not
   needed externally, then NULL may be passed for the result parameter.

   To support accumulator set/get operations under simulation, the opcode is
   expanded to include a special mode, indicated by bit 15 = 1.  In this mode,
   if the result parameter is NULL, then the accumulator is set from the value
   passed as operand_l.  If the result parameter is not null, then the
   accumulator value is returned as the result, and operand_l is ignored.  The
   precision of the operation is performed as specified by the OPSIZE value
   passed in bits 2-0 of the opcode.

   The function returns 1 if the operation overflows and 0 if not.
*/

uint32 fp_exec (uint16 opcode, OP *result, OP operand_l, OP operand_r)
{
static FPU accumulator;
FPU uoperand_l, uoperand_r;
OPSIZE op_l_prec, op_r_prec, rslt_prec;
uint32 overflow;

if (opcode & SIGN) {                                    /* accumulator mode? */
    rslt_prec = (OPSIZE) (opcode & 0017);               /* get operation precision */

    if (result) {                                       /* get accumulator? */
        op_l_prec = accumulator.precision;              /* save accum prec temp */
        accumulator.precision = rslt_prec;              /* set desired precision */
        *result = pack (accumulator);                   /* pack accumulator */
        accumulator.precision = op_l_prec;              /* restore correct prec */
        }
    else                                                /* set accumulator */
        accumulator = unpack (operand_l, rslt_prec);    /* unpack from operand */

    return 0;                                           /* no overflow from accum ops */
    }

fp_prec (opcode, &op_l_prec, &op_r_prec, &rslt_prec);   /* calc precs from opcode */

if (op_l_prec == fp_a)                                  /* left operand in accum? */
    uoperand_l = accumulator;                           /* copy it */
else                                                    /* operand supplied */
    uoperand_l = unpack (operand_l, op_l_prec);         /* unpack from parameter */

if (op_r_prec == fp_a)                                  /* right operand in accum? */
    uoperand_r = accumulator;                           /* copy it */
else                                                    /* operand supplied */
    uoperand_r = unpack (operand_r, op_r_prec);         /* unpack from parameter */


switch (opcode & 0160) {                                /* dispatch operation */

    case 0000:                                          /* add */
        add (&accumulator, uoperand_l, uoperand_r);
        break;

    case 0020:                                          /* subtract */
        complement (&uoperand_r);
        add (&accumulator, uoperand_l, uoperand_r);
        break;

    case 0040:                                          /* multiply */
        multiply (&accumulator, uoperand_l, uoperand_r);
        break;

    case 0060:                                          /* divide */
        divide (&accumulator, uoperand_l, uoperand_r);
        break;

    case 0100:                                          /* fix */
        accumulator.precision = rslt_prec;
        overflow = fix (&accumulator, uoperand_l);

        if (result)                                     /* result wanted? */
            *result = pack_int (accumulator.mantissa,   /* pack integer */
                                rslt_prec);
        return overflow;

    case 0120:                                          /* float */
        accumulator.precision = rslt_prec;
        ffloat (&accumulator, uoperand_l);

        if (result)                                     /* result wanted? */
            *result = pack (accumulator);               /* pack FP (FLT does not round) */
        return 0;

    case 0140:                                          /* (diagnostic) */
    case 0160:                                          /* (diagnostic) */
        return 0;
    }

if (UNIT_CPU_MODEL != UNIT_1000_F)                      /* firmware implementation? */
    accumulator.mantissa = accumulator.mantissa &       /* mask to precision */
                           op_mask[accumulator.precision];

normalize (&accumulator);                               /* normalize */
overflow = roundovf (&accumulator, opcode & 0200);      /* round and check for overflow */

if (result)                                             /* result wanted? */
    *result = pack (accumulator);                       /* pack result */

return overflow;
}


/* Set or get accumulator at desired precision.

   This function provides access to the FPP accumulator.  In hardware, the
   accumulator may be read at a given precision by sending the FPP an opcode
   encoded with the desired precision and then reading words from the FPP
   /without/ initiating the operation, i.e., without starting the processor.

   Under simulation, pass this function a NULL operand and the desired
   precision to read the accumulator.  Pass a pointer to an operand and the
   desired precision to set the accumulator; the return value in this case is
   not defined.
*/

OP fp_accum (const OP *operand, OPSIZE precision)
{
OP result = NOP;
uint16 opcode = (uint16) precision | SIGN;              /* add special mode bit */

if (operand)
    fp_exec (opcode, NULL, *operand, NOP);              /* set accum */
else
    fp_exec (opcode, &result, NOP, NOP);                /* get accum */
return result;
}


/* Pack an unpacked floating-point number.

   An unpacked mantissa is passed as a "packed" number with an unused exponent.
   The mantissa and separately-passed exponent are packed into the in-memory
   floating-point format.  Note that all bits are significant in the mantissa
   (no masking is done).
*/

uint32 fp_pack (OP *result, OP mantissa, int32 exponent, OPSIZE precision)
{
FPU unpacked;

unpacked.mantissa = unpack_int (mantissa, precision);   /* unpack mantissa */
unpacked.exponent = exponent;                           /* set exponent */
unpacked.precision = precision;                         /* set precision */
*result = pack (unpacked);                              /* pack them */
return 0;
}


/* Normalize, round, and pack an unpacked floating-point number.

   An unpacked mantissa is passed as a "packed" number with an unused exponent.
   The mantissa and separately-passed exponent are normalized, rounded, and
   packed into the in-memory floating-point format.  Note that all bits are
   significant in the mantissa (no masking is done).
*/

uint32 fp_nrpack (OP *result, OP mantissa, int32 exponent, OPSIZE precision)
{
FPU unpacked;

unpacked.mantissa = unpack_int (mantissa, precision);   /* unpack mantissa */
unpacked.exponent = exponent;                           /* set exponent */
unpacked.precision = precision;                         /* set precision */
return nrpack (result, unpacked, FALSE);                /* norm/rnd/pack them */
}


/* Unpack a packed floating-point number.

   A floating-point number, packed into the in-memory format, is unpacked into
   separate mantissa and exponent values.  The unpacked mantissa is returned in
   a "packed" structure with an exponent of zero.  Mantissa or exponent may be
   null if that part isn't wanted.
*/

uint32 fp_unpack (OP *mantissa, int32 *exponent, OP packed, OPSIZE precision)

{
FPU unpacked;

unpacked = unpack (packed, precision);                  /* unpack mantissa and exponent */

if (exponent)                                           /* exponent wanted? */
    *exponent = unpacked.exponent;                      /* return exponent */

if (mantissa)                                           /* mantissa wanted? */
    *mantissa = pack_int (unpacked.mantissa, fp_t);     /* return full-size mantissa */
return 0;
}


/* Complement an unpacked mantissa.

   An unpacked mantissa is passed as a "packed" number with a zero exponent.
   The exponent increment, i.e., either zero or one, depending on whether a
   renormalization was required, is returned.  Note that all bits are
   significant in the mantissa.
*/

uint16 fp_ucom (OP *mantissa, OPSIZE precision)
{
FPU unpacked;

unpacked.mantissa = unpack_int (*mantissa, precision);  /* unpack mantissa */
unpacked.exponent = 0;                                  /* clear undefined exponent */
unpacked.precision = precision;                         /* set precision */
complement (&unpacked);                                 /* negate it */
*mantissa = pack_int (unpacked.mantissa, precision);    /* replace mantissa */
return (uint16) unpacked.exponent;                      /* return exponent increment */
}


/* Complement a floating-point number. */

uint32 fp_pcom (OP *packed, OPSIZE precision)
{
FPU unpacked;

unpacked = unpack (*packed, precision);                 /* unpack the number */
complement (&unpacked);                                 /* negate it */
return nrpack (packed, unpacked, FALSE);                /* and norm/rnd/pack */
}


/* Truncate a floating-point number. */

uint32 fp_trun (OP *result, OP source, OPSIZE precision)
{
t_bool bits_lost;
FPU unpacked;
FPU one = { FP_ONEHALF, 1, fp_t };                      /* 0.5 * 2 ** 1 = 1.0 */
OP zero = { { 0, 0, 0, 0, 0 } };                        /* 0.0 */
t_uint64 mask = mant_mask[precision] & ~FP_MSIGN;

unpacked = unpack (source, precision);
if (unpacked.exponent < 0)                              /* number < 0.5? */
    *result = zero;                                     /* return 0 */
else if (unpacked.exponent >= (int32) op_bits[precision])   /* no fractional bits? */
    *result = source;                                   /* already integer */
else {
    mask = (mask >> unpacked.exponent) & mask;          /* mask fractional bits */
    bits_lost = ((unpacked.mantissa & mask) != 0);      /* flag if bits lost */
    unpacked.mantissa = unpacked.mantissa & ~mask;      /* mask off fraction */
    if ((unpacked.mantissa < 0) && bits_lost)           /* negative? */
        add (&unpacked, unpacked, one);                 /* truncate toward zero */
    nrpack (result, unpacked, FALSE);                   /* (overflow cannot occur) */
    }
return 0;                                               /* clear overflow on return */
}


/* Convert a floating-point number from one precision to another. */

uint32 fp_cvt (OP *result, OPSIZE source_precision, OPSIZE dest_precision)
{
FPU unpacked;

unpacked = unpack (*result, source_precision);
unpacked.precision = dest_precision;
return nrpack (result, unpacked, FALSE);                /* norm/rnd/pack */
}


#endif                                                  /* end of int64 support */
