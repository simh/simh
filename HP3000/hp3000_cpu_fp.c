/* hp3000_cpu_fp.c: HP 3000 floating-point arithmetic simulator

   Copyright (c) 2016, J. David Bryan

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

   11-Jun-16    JDB     Bit mask constants are now unsigned
   03-Feb-16    JDB     First release version
   25-Aug-15    JDB     Fixed FSUB zero subtrahend bug (from Norwin Malmberg)
   01-Apr-15    JDB     Passes the floating point tests in the CPU diagnostic (D420A1)
   29-Mar-15    JDB     Created

   References:
     - HP 3000 Series II System Microprogram Listing
         (30000-90023, August 1976)
     - HP 3000 Series II/III System Reference Manual
         (30000-90020, July 1978)
     - Machine Instruction Set Reference Manual
         (30000-90022, June 1984)


   This module implements multiple-precision floating-point operations to
   support the HP 3000 CPU instruction set.  It employs 64-bit integer
   arithmetic for speed and simplicity of implementation.  The host compiler
   must support 64-bit integers.

   HP 3000 computers use a proprietary floating-point format.  All 3000s
   support two-word "single-precision" floating-point arithmetic as standard
   equipment.  The original HP 3000 CX and Series I CPUs support three-word
   "extended-precision" floating-point arithmetic when the optional HP 30011A
   Extended Instruction Set microcode was installed.  The Series II and later
   machines replace the three-word instructions with four-word "double-
   precision" floating-point arithmetic and include the EIS as part of the
   standard equipment.

   Floating-point numbers have this format:

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | S |      exponent biased by +256      |   positive mantissa   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       positive mantissa                       |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       positive mantissa                       | (extended)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |                       positive mantissa                       | (double)
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   That is, the three- and four-word formats merely extended the mantissa with
   no change to the exponent range.

   The mantissa is represented in sign-magnitude format.  The mantissa is always
   positive, with an assumed "1" to the left of the MSB, and the sign bit is set
   for negative values.  The exponent is in "excess-256" format, i.e.,
   represented as an unsigned value biased by +256, giving an unbiased range of
   -256 to +255.  The binary point is assumed to be between the leading "1" and
   the MSB, so a zero value must be handled as a special case of all bits equal
   to zero, which otherwise would represent the value +1.0 * 2 ** -256.
   Normalization shifts the mantissa left and decrements the exponent until a
   "1" bit appears in bit 9.

   The use of sign-magnitude format means that floating-point negation merely
   complements the sign bit, and floating-point comparison simply checks the
   signs and, if they are the same, then applies an integer comparison to the
   packed values.  However, it also implies the existence of a "negative zero"
   value, represented by all zeros except for the sign bit.  This value is
   undefined; if a negative zero is supplied as an operand to one of the
   arithmetic routines, it is treated as positive zero.  Negative zero is never
   returned even if, e.g., it is supplied as the dividend or multiplier.

   This implementation provides add, subtract, multiply, divide, float, and fix
   operations on two-, three-, and four-word floating point operands.  The
   routines are called via a common floating-point executor ("fp_exec") by
   supplying the operation to be performed and the operand(s) on which to act.
   An operand contains the packed (i.e., in-memory) representation and the
   precision of the value.  The returned value includes the packed
   representation and the precision, along with a value that indicates whether
   or not the operation resulted in an arithmetic trap.  It is the
   responsibility of the caller to take the trap if it is indicated.
*/



#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_cpu_fp.h"



/* Program constants */

#define EXPONENT_BIAS       256                 /* the exponent is biased by +256 */

#define MIN_EXPONENT        -256                /* the smallest representable exponent */
#define MAX_EXPONENT        +255                /* the largest representable exponent */

#define EXPONENT_MASK       0077700u            /* the mask to isolate the exponent in the first word */
#define MANTISSA_MASK       0000077u            /* the mask to isolate the mantissa in the first word */

#define EXPONENT_SHIFT      6                   /* the exponent alignment shift */
#define MANTISSA_SHIFT      0                   /* the mantissa alignment shift */

#define UNPACKED_BITS       54                  /* the number of significant bits in the unpacked mantissa */

#define IMPLIED_BIT         ((t_uint64) 1uL << UNPACKED_BITS)       /* the implied MSB in the mantissa */
#define CARRY_BIT           ((t_uint64) 1uL << UNPACKED_BITS + 1)   /* the carry from the MSB in the mantissa */

#define DELTA_ALIGNMENT     (D64_WIDTH - UNPACKED_BITS)             /* net shift to align the binary point */


/* Floating-point accessors */

#define MANTISSA(w)         ((t_uint64) (((w) & MANTISSA_MASK) >> MANTISSA_SHIFT))
#define EXPONENT(w)         ((int32)    (((w) & EXPONENT_MASK) >> EXPONENT_SHIFT))

#define TO_EXPONENT(w)      ((w) + EXPONENT_BIAS << EXPONENT_SHIFT & EXPONENT_MASK)

#define DENORMALIZED(m)     (((m) & IMPLIED_BIT) == 0)


/* Floating-point unpacked representation */

typedef struct {
    t_uint64   mantissa;                        /* the unsigned mantissa */
    int32      exponent;                        /* the unbiased exponent */
    t_bool     negative;                        /* TRUE if the mantissa is negative */
    FP_OPSIZE  precision;                       /* the precision currently expressed by the value */
    } FPU;

static const FPU zero = { 0, 0, FALSE, fp_f };  /* an unpacked zero value */


/* Floating-point descriptors */

static const int32 mantissa_bits [] = {         /* the number of mantissa bits, indexed by FP_OPSIZE */
    16 - 1,                                     /*   in_s bits available - sign bit */
    32 - 1,                                     /*   in_d bits available - sign bit */
    22 + 1,                                     /*   fp_f bits explicit + bits implied */
    38 + 1,                                     /*   fp_x bits explicit + bits implied */
    54 + 1                                      /*   fp_e bits explicit + bits implied */
    };

static const t_uint64 mantissa_mask [] = {      /* the mask to the mantissa bits, indexed by FP_OPSIZE */
    ((t_uint64) 1 << 16) - 1 <<  0,             /*   in_s 16-bit mantissa */
    ((t_uint64) 1 << 32) - 1 <<  0,             /*   in_d 32-bit mantissa */
    ((t_uint64) 1 << 22) - 1 << 32,             /*   fp_f 22-bit mantissa */
    ((t_uint64) 1 << 38) - 1 << 16,             /*   fp_x 38-bit mantissa */
    ((t_uint64) 1 << 54) - 1 <<  0              /*   fp_e 54-bit mantissa */
    };


static const t_uint64 half_lsb [] = {           /* half of the LSB for rounding, indexed by FP_OPSIZE */
    0,                                          /*   in_s not used */
    0,                                          /*   in_d not used */
    (t_uint64) 1 << 31,                         /*   fp_f word 2 LSB */
    (t_uint64) 1 << 15,                         /*   fp_x word 3 LSB */
    (t_uint64) 1 <<  0                          /*   fp_e word 4 LSB */
    };


/* Floating-point local utility routine declarations */

static FPU     unpack          (FP_OPND packed);
static FP_OPND norm_round_pack (FPU     unpacked);

static TRAP_CLASS add      (FPU *sum,        FPU augend,       FPU addend);
static TRAP_CLASS subtract (FPU *difference, FPU minuend,      FPU subtrahend);
static TRAP_CLASS multiply (FPU *product,    FPU multiplicand, FPU multiplier);
static TRAP_CLASS divide   (FPU *quotient,   FPU dividend,     FPU divisor);

static TRAP_CLASS ffloat (FPU *real,    FPU integer);
static TRAP_CLASS fix    (FPU *integer, FPU real, t_bool round);



/* Floating-point global routines */


/* Execute a floating-point operation.

   The operator specified by the "operation" parameter is applied to the
   "left_op" and to the "right_op" (if applicable), and the result is returned.
   The "precision" fields of the operands must be set to the representations
   stored within before calling this routine.

   On entry, the left and right (if needed) operands are unpacked, and the
   executor for the specified operation is called.  The result is normalized,
   rounded, and packed.  Any trap condition detected by the operator routine is
   set into the packed operand, unless the normalize/round/pack routine detected
   its own trap condition.  Finally, the packed result is returned.
*/

FP_OPND fp_exec (FP_OPR operator, FP_OPND left_op, FP_OPND right_op)
{
FPU        left, right, result;
FP_OPND    result_op;
TRAP_CLASS trap;

left = unpack (left_op);                                /* unpack the left-hand operand */

if (operator <= fp_div)                                 /* if the operator requires two operands */
    right = unpack (right_op);                          /*   then unpack the right-hand operation */

switch (operator) {                                     /* dispatch the floating-point operation */

    case fp_add:
        trap = add (&result, left, right);              /* add the two operands */
        break;

    case fp_sub:
        trap = subtract (&result, left, right);         /* subtract the two operands */
        break;

    case fp_mpy:
        trap = multiply (&result, left, right);         /* multiply the two operands */
        break;

    case fp_div:
        trap = divide (&result, left, right);           /* divide the two operands */
        break;

    case fp_flt:
        trap = ffloat (&result, left);                  /* convert the integer operand to a floating-point number */
        break;

    case fp_fixr:
        trap = fix (&result, left, TRUE);               /* round the floating-point operand to an integer */
        break;

    case fp_fixt:
        trap = fix (&result, left, FALSE);              /* truncate the floating-point operand to an integer */
        break;

    default:
        result = zero;                                  /* if an unimplemented operation is requested */
        result.precision = left.precision;              /*   then return a zero of the appropriate precision */
        trap = trap_Unimplemented;                      /*     and trap for an Unimplemented Instruction */
        break;
    }                                                   /* all cases are handled */

result_op = norm_round_pack (result);                   /* normalize, round, and pack the result */

if (result_op.trap == trap_None)                        /* if the pack operation succeeded */
    result_op.trap = trap;                              /*   then set any arithmetic trap returned by the operation */

return result_op;                                       /* return the result */
}



/* Floating-point local utility routine declarations */


/* Unpack a packed operand.

   A packed integer or floating-point value is split into separate mantissa and
   exponent variables.  The multiple words of the mantissa are concatenated into
   a single 64-bit unsigned value, and the exponent is shifted with recovery of
   the sign.

   The absolute values of single and double integers are unpacked into the
   mantissas and preshifted by 32 or 16 bits, respectively, to reduce the
   shifting needed for normalization.  The resulting value is unnormalized, but
   the exponent is set correctly to reflect the preshift.  The precisions for
   unpacked integers are set to single-precision but are valid for extended- and
   double-precision, as the unpacked representations are identical.

   The packed floating-point representation contains an implied "1" bit
   preceding the binary point in the mantissa, except if the floating-point
   value is zero.  The unpacked mantissa includes the implied bit.  The bias is
   removed from the exponent, producing a signed value, and the sign of the
   mantissa is set from the sign of the packed value.

   A packed zero value is represented by all words set to zero.  In the unpacked
   representation, the mantissa is zero, the exponent is the minimum value
   (-256), and the sign is always positive (as "negative zero" is undefined).


   Implementation notes:

    1. Integers could have been copied directly to the mantissa with the
       exponents set to the appropriate values (54 in this case).  However, the
       current implementation unpacks integers only in preparation for repacking
       as floating-point numbers i.e., to implement the "float" operator.  This
       would require a larger number of shifts to normalize the values -- as
       many as 54 to normalize the value 1.  Preshifting reduces the number of
       normalizing shifts needed to between 6 and 22.
*/

static FPU unpack (FP_OPND packed)
{
FPU    unpacked;
uint32 word;

switch (packed.precision) {                             /* dispatch based on the operand precision */

    case in_s:                                          /* unpack a single integer */
        word = packed.words [0];                        /*   from the first word */

        if (word & D16_SIGN) {                          /* if the value is negative */
            word = NEG16 (word);                        /*   then make it positive */
            unpacked.negative = TRUE;                   /*     and set the mantissa sign flag */
            }

        else                                            /* otherwise the value is positive */
            unpacked.negative = FALSE;                  /*   so clear the sign flag */

        unpacked.mantissa = (t_uint64) word << 32;      /* store the preshifted value as the mantissa */
        unpacked.exponent = UNPACKED_BITS - 32;         /*   and set the exponent to account for the shift */
        unpacked.precision = fp_f;                      /* set the precision */
        break;


    case in_d:                                                  /* unpack a double integer */
        word = TO_DWORD (packed.words [0], packed.words [1]);   /*   from the first two words */

        if (word & D32_SIGN) {                          /* if the value is negative */
            word = NEG32 (word);                        /*   then make it positive */
            unpacked.negative = TRUE;                   /*     and set the mantissa sign flag */
            }

        else                                            /* otherwise the value is positive */
            unpacked.negative = FALSE;                  /*   so clear the sign flag */

        unpacked.mantissa = (t_uint64) word << 16;      /* store the preshifted value as the mantissa */
        unpacked.exponent = UNPACKED_BITS - 16;         /*   and set the exponent to account for the shift */
        unpacked.precision = fp_f;                      /* set the precision */
        break;


    case fp_f:                                              /* unpack a single-precision */
    case fp_x:                                              /*   extended-precision */
    case fp_e:                                              /*     or double-precision floating-point number */
        unpacked.mantissa = MANTISSA (packed.words [0]);    /*       starting with the first word */

        for (word = 1; word <= 3; word++) {                 /* unpack from one to three more words */
            unpacked.mantissa <<= 16;                       /* shift the accumulated value */

            if (word < TO_COUNT (packed.precision))         /* if all words are not included yet */
                unpacked.mantissa |= packed.words [word];   /*   then merge the next word into value */
            }

        unpacked.exponent =                                 /* store the exponent */
           EXPONENT (packed.words [0]) - EXPONENT_BIAS;     /*   after removing the bias */

        if (unpacked.exponent == MIN_EXPONENT               /* if the biased exponent and mantissa are zero */
          && unpacked.mantissa == 0)                        /*   then the mantissa is positive */
            unpacked.negative = FALSE;                      /*     regardless of the packed sign */

        else {                                                          /* otherwise the value is non-zero */
            unpacked.mantissa |= IMPLIED_BIT;                           /*   so add back the implied "1" bit */
            unpacked.negative = ((packed.words [0] & D16_SIGN) != 0);   /*     and set the sign as directed */
            }

        unpacked.precision = packed.precision;          /* set the precision */
        break;
    }                                                   /* all cases are handled */

return unpacked;                                        /* return the unpacked value */
}


/* Normalize, round, and pack an unpacked value.

   An unpacked value is normalized, rounded, and packed into the representation
   indicated by the operand precision.  If the supplied value cannot be
   represented, the appropriate trap indication is returned.

   A single- or double-integer is packed into the first word or two words of the
   result as a twos-complement value.  If the value is too large for the result
   precision, an Integer Overflow trap is indicated, and a zero value is
   returned.

   For a real of any precision, the mantissa is first normalized by shifting
   right if the carry bit is set, or by shifting left until the implied bit is
   set.  The exponent is adjusted for any shifts performed.  The value is then
   rounded by adding one-half of the least-significant bit; if that causes a
   carry, the exponent is adjusted again.  Finally, the mantissa is masked to
   the number of bits corresponding to the desired precision and packed into the
   in-memory representation.  The exponent is checked, and it exceeds the
   permitted range, the appropriate trap indication is returned.


   Implementation notes:

    1. If a carry occurs due to rounding, the mantissa is not shifted because
       the carry bit will be masked off during packing.  Incrementing the
       exponent in this case is sufficient.

    2. Masking the mantissa is required to remove the carry and implied bits
       before packing.  Masking the value bits in excess of the specified
       precision is not required but is desirable to avoid implying more
       precision than actually is present.

    3. The result value +/-1 x 2 ** -256 is considered an underflow, as the
       packed representation is identical to the zero representation, i.e., an
       all-zeros value.
*/

static FP_OPND norm_round_pack (FPU unpacked)
{
FP_OPND packed;
int32   integer;

packed.precision = unpacked.precision;                  /* set the precision */

if (unpacked.mantissa == 0) {                           /* if the mantissa is zero */
    packed.words [0] = 0;                               /*   then set */
    packed.words [1] = 0;                               /*     the packed */
    packed.words [2] = 0;                               /*       representation to */
    packed.words [3] = 0;                               /*         all zeros */

    packed.trap = trap_None;                            /* report that packing succeeded */
    }

else if (unpacked.precision <= in_d)                                /* if packing a single or double integer */
    if (unpacked.exponent >= mantissa_bits [unpacked.precision]) {  /*   then if the value is too large to fit */
        packed.words [0] = 0;                                       /*     then return */
        packed.words [1] = 0;                                       /*       a zero value */
        packed.trap = trap_Integer_Overflow;                        /*         and an overflow trap */
        }

    else {                                                          /* otherwise */
        integer = (int32)                                           /*   convert the value to an integer */
           (unpacked.mantissa >> UNPACKED_BITS - unpacked.exponent  /*     by shifting right to align */
           & mantissa_mask [unpacked.precision]);                   /*       and masking to the desired precision */

        if (unpacked.negative)                          /* if the value is negative */
            integer = - integer;                        /*   then negate the result */

        packed.words [0] = UPPER_WORD (integer);        /* split the result */
        packed.words [1] = LOWER_WORD (integer);        /*   into the first two words */

        packed.trap = trap_None;                        /* report that packing succeeded */
        }

else {                                                  /* otherwise a real number is to be packed */
    if (unpacked.mantissa & CARRY_BIT) {                /* if a carry out of the MSB has occurred */
        unpacked.mantissa >>= 1;                        /*   then shift the mantissa to normalize */
        unpacked.exponent +=  1;                        /*     and increment the exponent to compensate */
        }

    else                                                /* otherwise */
        while (DENORMALIZED (unpacked.mantissa)) {      /*   while the mantissa is not in normal form */
            unpacked.mantissa <<= 1;                    /*     shift the mantissa toward the implied-bit position */
            unpacked.exponent -=  1;                    /*       and decrement the exponent to compensate */
            }


    unpacked.mantissa += half_lsb [unpacked.precision]; /* round the mantissa by adding one-half of the LSB */

    if (unpacked.mantissa & CARRY_BIT)                  /* if rounding caused a carry out of the MSB */
        unpacked.exponent = unpacked.exponent + 1;      /*   then increment the exponent to compensate */


    unpacked.mantissa &= mantissa_mask [unpacked.precision];    /* mask the mantissa to the specified precision */

    packed.words [0] = (HP_WORD) (unpacked.mantissa >> 48) & DV_MASK    /* pack the first word of the mantissa */
                       | TO_EXPONENT (unpacked.exponent)                /*   with the exponent */
                       | (unpacked.negative ? D16_SIGN : 0);            /*     and the sign bit */

    packed.words [1] = (HP_WORD) (unpacked.mantissa >> 32) & DV_MASK;   /* pack the second */
    packed.words [2] = (HP_WORD) (unpacked.mantissa >> 16) & DV_MASK;   /*   and third */
    packed.words [3] = (HP_WORD) (unpacked.mantissa >>  0) & DV_MASK;   /*     and fourth words */

    if (unpacked.exponent < MIN_EXPONENT                                /* if the exponent is too small */
      || unpacked.exponent == MIN_EXPONENT && unpacked.mantissa == 0)   /*   or the result would be all zeros */
        packed.trap = trap_Float_Underflow;                             /*     then report an underflow trap */

    else if (unpacked.exponent > MAX_EXPONENT)                          /* otherwise if the exponent is too large */
        packed.trap = trap_Float_Overflow;                              /*   then report an overflow trap */

    else                                                                /* otherwise */
        packed.trap = trap_None;                                        /*   report that packing succeeded */
    }

return packed;                                          /* return the packed value */
}


/* Add two unpacked numbers.

   The sum of the two operands is returned.  If one operand is zero and the
   other is not, the non-zero operand is returned.  If both operand are zero, a
   "defined zero" is returned in case one or both operands are "negative zeros."

   Otherwise, the difference between the operand exponents is determined.  If
   the magnitude of the difference between the exponents is greater than the
   number of significant bits, then the smaller number has been scaled to zero
   (swamped), and so the sum is simply the larger operand.  However, if the sum
   will be significant, the smaller mantissa is shifted to align with the larger
   mantissa, and the larger exponent is used (as, after the scaling shift, the
   smaller value has the same exponent).  Finally, if the operand signs are the
   same, the result is the sum of the mantissas.  If the signs are different,
   then the sum is the smaller value subtracted from the larger value, and the
   result adopts the sign of the larger value.


   Implementation notes:

    1. If the addend is zero, the microcode converts the undefined value
       "negative zero" to the defined positive zero if it is passed as the
       augend.  This also applies to the subtraction operator, which passes a
       negative zero if the subtrahend is zero.
*/

static TRAP_CLASS add (FPU *sum, FPU augend, FPU addend)
{
int32 magnitude;

if (addend.mantissa == 0)                              /* if the addend is zero */
    if (augend.mantissa == 0) {                        /*   then if the augend is also zero */
        *sum = zero;                                   /*     then the sum is (positive) zero */
        sum->precision = augend.precision;             /*       with the appropriate precision */
        }

    else                                                /*   otherwise the augend is non-zero */
        *sum = augend;                                  /*     so the sum is just the augend */

else if (augend.mantissa == 0)                          /* otherwise if the augend is zero */
    *sum = addend;                                      /*   then the sum is just the addend */

else {                                                  /* otherwise both operands are non-zero */
    magnitude = augend.exponent - addend.exponent;      /*   so determine the magnitude of the difference */

    if (abs (magnitude) > mantissa_bits [augend.precision]) /* if one of the operands is swamped */
        if (magnitude > 0)                                  /*   then if the addend is smaller */
            *sum = augend;                                  /*     then return the augend */
        else                                                /*   otherwise */
            *sum = addend;                                  /*     return the addend */

    else {                                              /* otherwise the addition is required */
        sum->precision = addend.precision;              /*   so set the precision to that of the operands */

        if (magnitude > 0) {                            /* if the addend is smaller */
            addend.mantissa >>= magnitude;              /*   then shift right to align the addend */
            sum->exponent = augend.exponent;            /*     and use the augend's exponent */
            }

        else {                                          /* otherwise the augend is smaller or the same */
            augend.mantissa >>= - magnitude;            /*   shift right to align the augend */
            sum->exponent = addend.exponent;            /*     and use the addend's exponent */
            }

        if (addend.negative == augend.negative) {               /* if the mantissa signs are the same */
            sum->mantissa = addend.mantissa + augend.mantissa;  /*   then add the mantissas */
            sum->negative = addend.negative;                    /*     and use the addend sign for the sum */
            }

        else if (addend.mantissa > augend.mantissa) {           /* otherwise if the addend is larger */
            sum->mantissa = addend.mantissa - augend.mantissa;  /*   then subtract the augend */
            sum->negative = addend.negative;                    /*     and use the addend sign */
            }

        else {                                                  /* otherwise the augend is larger */
            sum->mantissa = augend.mantissa - addend.mantissa;  /*   so subtract the addend */
            sum->negative = augend.negative;                    /*     and use the augend sign */
            }
        }
    }

return trap_None;                                       /* report that the addition succeeded */
}


/* Subtract two unpacked numbers.

   The difference of the two operands is returned.  Subtraction is implemented
   by negating the subtrahend and then adding the minuend.


   Implementation notes:

    1. If the subtrahend is zero, negating produces the undefined "negative
       zero."  However, the "add" routine handles this as positive zero, so we
       do not need to worry about this condition.
*/

static TRAP_CLASS subtract (FPU *difference, FPU minuend, FPU subtrahend)
{
subtrahend.negative = ! subtrahend.negative;            /* invert the sign of the subtrahend */

add (difference, minuend, subtrahend);                  /* add to obtain the difference */

return trap_None;                                       /* report that the subtraction succeeded */
}


/* Multiply two unpacked numbers.

   The product of the two operands is returned.  Conceptually, the
   implementation requires a 64 x 64 = 128-bit multiply, rounded to the upper 64
   bits.  Instead of implementing the FMPY or EMPY firmware algorithm directly,
   which uses 16 x 16 = 32-bit partial-product multiplies, it is more efficient
   under simulation to use 32 x 32 = 64-bit multiplications by splitting the
   operand mantissas ("a" and "b") into 32-bit high and low ("h" and "l") parts
   and forming the cross-products:

                      64-bit operands
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

       64-bit product
     +-------+-------+

   If either operand is zero, a "defined zero" is returned in case one or both
   operands are "negative zeros."  Otherwise, the product exponent is set to the
   sum of the operand exponents, and the four 64-bit cross-products are formed.
   The lower 64 bits of the products are summed to form the carry into the upper
   64 bits, which are summed to produce the product.  The product mantissa is
   aligned, and the product sign is set negative if the operand signs differ.

   Mantissas are represented internally as fixed-point numbers with 54 bits to
   the right of the binary point.  That is, the real number represented is the
   integer mantissa value * (2 ** -54), where the right-hand term represents the
   delta for a change of one bit.  Multiplication is therefore:

     (product * delta) = (multiplicand * delta) * (multiplier * delta)

   The product is:

     product = (multiplicand * multiplier) * (delta * delta) / delta

   ...which reduces to:

     product = multiplicand * multiplier * delta

   Multiplying the product by (2 ** -54) is equivalent to right-shifting by 54.
   However, using only the top 64 bits of the 128-bit product is equivalent to
   right-shifting by 64, so the net correction is a left-shift by 10.


   Implementation notes:

    1. 32 x 32 = 64-bit multiplies use intrinsic instructions on the IA-32
       processor family.
*/

static TRAP_CLASS multiply (FPU *product, FPU multiplicand, FPU multiplier)
{
uint32   ah, al, bh, bl;
t_uint64 hh, hl, lh, ll, carry;

if (multiplicand.mantissa == 0 || multiplier.mantissa == 0) {   /* if either operand is zero */
    *product = zero;                                            /*   then the product is (positive) zero */
    product->precision = multiplicand.precision;                /*     with the appropriate precision */
    }

else {                                                  /* otherwise both operands are non-zero */
    product->precision = multiplicand.precision;        /*   so set the precision to that of the operands */

    product->exponent = multiplicand.exponent           /* the product exponent */
                          + multiplier.exponent;        /*   is the sum of the operand exponents */

    ah = (uint32) (multiplicand.mantissa >> D32_WIDTH); /* split the multiplicand */
    al = (uint32) (multiplicand.mantissa &  D32_MASK);  /*   into high and low double-words */

    bh = (uint32) (multiplier.mantissa   >> D32_WIDTH); /* split the multiplier */
    bl = (uint32) (multiplier.mantissa   &  D32_MASK);  /*   into high and low double-words */

    hh = ((t_uint64) ah * bh);                          /* form the */
    hl = ((t_uint64) ah * bl);                          /*   four cross products */
    lh = ((t_uint64) al * bh);                          /*     using 32 x 32 = 64-bit multiplies */
    ll = ((t_uint64) al * bl);                          /*       for efficiency */

    carry = ((ll >> D32_WIDTH) + (hl & D32_MASK)        /* add the upper half of "ll" to the lower halves of "hl" */
              + (lh & D32_MASK)) >> D32_WIDTH;          /*   and "lh" and shift to leave just the carry bit */

    product->mantissa = hh + (hl >> D32_WIDTH)          /* add "hh" to the upper halves of "hl" and "lh" */
                          + (lh >> D32_WIDTH) + carry;  /*   and the carry bit */

    product->mantissa <<= DELTA_ALIGNMENT;              /* align the result */

    product->negative =                                 /* set the product sign negative */
       (multiplicand.negative != multiplier.negative);  /*   if the operand signs differ */
    }

return trap_None;                                       /* report that the multiplication succeeded */
}


/* Divide two unpacked numbers.

   The quotient of the two operands is returned, and the remainder is discarded.
   Conceptually, the implementation requires a 128 / 64 = 64-bit division, with
   64 bits of zeros appended to the dividend to get the required precision.
   However, instead of implementing the FDIV or EDIV firmware algorithm
   directly, which uses 32 / 16 = 16-bit trial divisions, it is more efficient
   under simulation to use 64 / 32 = 64-bit divisions with the classic
   divide-and-correct method.

   This method considers the 64-bit dividend and divisor each to consist of two
   32-bit "digits."  The 64-bit dividend "ah,al" is divided by the first 32-bit
   digit "bh" of the 64-bit divisor "bh,bl", yielding a 64-bit trial quotient
   and a 64-bit remainder.  A correction is developed by subtracting the product
   of the second 32-bit digit "bl" of the divisor and the trial quotient from
   the remainder.  If the remainder is negative, the trial quotient is too
   large, so it is decremented, and the (full 64-bit) divisor is added to the
   correction.  This is repeated until the correction is non-negative,
   indicating that the first quotient digit is correct.  The process is then
   repeated using the corrected remainder as the dividend to develop the second
   64-bit trial quotient and second quotient digit.  The first quotient digit is
   positioned, and the two quotient digits are then added to produce the final
   64-bit quotient.  The quotient mantissa is aligned, and the quotient sign is
   set negative if the operand signs differ.

   Mantissas are represented internally as fixed-point numbers with 54 bits to
   the right of the binary point.  That is, the real number represented is the
   integer mantissa value * (2 ** -54), where the right-hand term represents the
   delta for a change of one bit.  Division is therefore:

     (quotient * delta) = (dividend * delta) / (divisor * delta)

   The quotient is:

     quotient = (dividend / divisor) * (delta / (delta * delta))

   ...which reduces to:

     quotient = (dividend / divisor) / delta

   Dividing the quotient by (2 ** -54) is equivalent to left-shifting by 54.
   However, using only the top 64 bits of the 128-bit product is equivalent to
   right-shifting by 64, so the net correction is a right-shift by 10.

   See "Divide-and-Correct Methods for Multiple Precision Division" by Marvin L.
   Stein, Communications of the ACM, August 1964 for background.


   Implementation notes:

    1. IA-32 processors do not have a 64 / 32 = 64-bit divide instruction (they
       have a 64 / 32 = 32 instruction instead).  Therefore, a run-time library
       routine for 64 / 64 = 64 is generated.  Consequently, "bh" and "bl" are
       declared as 64-bit variables, as this produces simpler code than if they
       were declared as 32-bit variables.

    2. "bh" is guaranteed to be non-zero because the divisor mantissa is
       normalized on entry.  Therefore, no division-by-zero check is needed.

    3. The quotient alignment shift logically expresses ((q1 << 32) + q2) >> 10,
       but it must be implemented as (q1 << 22) + (q2 >> 10) as otherwise the
       left-shift would lose significant bits.
*/

static TRAP_CLASS divide (FPU *quotient, FPU dividend, FPU divisor)
{
t_uint64 bh, bl, q1, q2, r1, r2;
t_int64  c1, c2;

if (divisor.mantissa == 0) {                            /* if the divisor is zero */
    *quotient = dividend;                               /*   then return the dividend */
    return trap_Float_Zero_Divide;                      /*     and report the error */
    }

else if (dividend.mantissa == 0) {                      /* otherwise if the dividend is zero */
    *quotient = zero;                                   /*   then the quotient is (positive) zero */
    quotient->precision = dividend.precision;           /*     with the appropriate precision */
    }

else {                                                  /* otherwise both operands are non-zero */
    quotient->precision = dividend.precision;           /*   so set the precision to that of the operands */

    quotient->exponent = dividend.exponent              /* the quotient exponent */
                          - divisor.exponent;           /*   is the difference of the operand exponents */

    bh = divisor.mantissa >> D32_WIDTH;                 /* split the divisor */
    bl = divisor.mantissa &  D32_MASK;                  /*   into high and low halves */

    q1 = dividend.mantissa / bh;                        /* form the first trial quotient */
    r1 = dividend.mantissa % bh;                        /*   and remainder */

    c1 = r1 - bl * q1;                                  /* form the first corrected remainder */

    while (c1 < 0) {                                    /* while a correction is required */
        q1 = q1 - 1;                                    /*   the first trial quotient is too large */
        c1 = c1 + divisor.mantissa;                     /*     so reduce it and increase the remainder */
        }

    q2 = c1 / bh;                                       /* form the second trial quotient */
    r2 = c1 % bh;                                       /*   and remainder */

    c2 = r2 - bl * q2;                                  /* form the second corrected remainder */

    while (c2 < 0) {                                    /* while a correction is required */
        q2 = q2 - 1;                                    /*   the second trial quotient is too large */
        c2 = c2 + divisor.mantissa;                     /*     so reduce it and increase the remainder */
        }

    quotient->mantissa = (q1 << D32_WIDTH - DELTA_ALIGNMENT)    /* sum the quotient digits */
                           + (q2 >> DELTA_ALIGNMENT);           /*   and align the result */

    quotient->negative =                                /* set the quotient sign negative */
       (dividend.negative != divisor.negative);         /*   if the operand signs differ */
    }

return trap_None;                                       /* report that the division succeeded */
}


/* Float an integer to a floating-point value.

   The integer operand is converted to a floating-point value and returned.  The
   desired precision of the result must be set before calling.

   Conversion is simply a matter of copying the integer value.  When the
   unpacked value is normalized, it will be converted to floating-point format.


   Implementation notes:

    1. The incoming integer has already been unpacked into fp_f format, so we do
       not need to set the precision here.
*/

static TRAP_CLASS ffloat (FPU *real, FPU integer)
{
*real = integer;                                        /* copy the unpacked value */
return trap_None;                                       /* report that the float succeeded */
}


/* Fix an unpacked floating-point value to an integer.

   A floating-point value is converted to a double-word integer.  If the "round"
   parameter is true, the value is rounded before conversion; otherwise, it is
   truncated.

   If the real value is less than 0.5, then the integer value is zero.
   Otherwise, if rounding is requested, add 0.5 (created by shifting a "1" into
   the position immediately to the right of the least significant bit of the
   integer result) to the value.  Finally, the result precision is set.  The
   remaining conversion occurs when the result is packed.


   Implementation notes:

    1. The FIXR/FIXT microcode gives an Integer Overflow for exponent > 30, even
       though -2 ** 31 (143700 000000) does fit in the result.
*/

static TRAP_CLASS fix (FPU *integer, FPU real, t_bool round)
{
if (real.exponent < -1)                                 /* if the real value is < 0.5 */
    integer->mantissa = 0;                              /*   then the integer value is 0 */

else {                                                  /* otherwise the value is convertible */
    integer->mantissa = real.mantissa;                  /*   so set the mantissa */

    if (round && real.exponent < UNPACKED_BITS)         /* if rounding is requested and the value won't overflow */
        integer->mantissa +=                            /*   then add one-half of the LSB to the value */
           (t_uint64) 1 << (UNPACKED_BITS - real.exponent - 1);
    }

integer->exponent = real.exponent;                      /* copy the exponent */
integer->negative = real.negative;                      /*   and sign */
integer->precision = in_d;                              /*     and set to pack to a double integer */

return trap_None;                                       /* report that the fix succeeded */
}
