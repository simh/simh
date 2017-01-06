/* hp3000_cpu_fp.h: HP 3000 floating-point interface declarations

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
   21-Jan-16    JDB     First release version
   29-Mar-15    JDB     Created


   This file contains declarations used by the CPU to interface with the
   floating-point operations executor.
*/



/* Program constants */

#define SIGN_BIT            0100000u
#define EXPONENT_BITS       0077700u
#define ASSUMED_BIT         0000100u
#define FRACTION_BITS       0000077u


/* Operand precisions:

    - S = 1-word integer
    - D = 2-word integer
    - F = 2-word single-precision floating-point
    - X = 3-word extended-precision floating-point
    - E = 4-word double-precision floating-point


   Implementation notes:

    1. The ordering of the enumeration constants is important, as we depend on
       the "fp" type codes to reflect the number of words used by the packed
       representation.
*/

typedef enum {
    in_s = 0,                                   /* 1-word integer */
    in_d = 1,                                   /* 2-word integer */
    fp_f = 2,                                   /* 2-word single-precision floating-point */
    fp_x = 3,                                   /* 3-word extended-precision floating-point */
    fp_e = 4                                    /* 4-word double-precision floating-point */
    } FP_OPSIZE;


/* Conversion from operand size to word count */

#define TO_COUNT(s)     ((uint32) (s + (s < fp_f)))


/* Floating point operations */

typedef enum {
    fp_add,
    fp_sub,
    fp_mpy,
    fp_div,
    fp_flt,
    fp_fixr,
    fp_fixt
    } FP_OPR;


/* General operand.

   An in-memory representation of an integer or packed floating-point number.
   An actual value will use one, two, three, or four words, as indicated by the
   "precision" specified.

   "trap" is significant only for result values; it is ignored for operand
   values.  A good result will have trap_None.
*/

typedef struct {
    HP_WORD    words [4];                       /* integer or floating-point value */
    FP_OPSIZE  precision;                       /* operand size descriptor */
    uint32     trap;                            /* validity of the result */
    } FP_OPND;


/* Special operands */

static const FP_OPND FP_NOP = { { 0, 0, 0, 0 }, in_s, trap_None };    /* an unneeded operand */


/* Floating-point global routines */

extern FP_OPND fp_exec (FP_OPR operator, FP_OPND left, FP_OPND right);
