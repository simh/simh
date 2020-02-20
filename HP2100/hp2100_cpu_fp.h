/* hp2100_cpu_fp.h: HP 2100 floating point declarations

   Copyright (c) 2002-2013, Robert M. Supnik
   Copyright (c) 2005-2018, J. David Bryan

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
   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   02-Oct-18    JDB     Moved ZERO definition from hp2100_cpu5.c
   28-Jul-18    JDB     Combined hp2100_fp.h and hp2100_fp1.h
   15-Feb-17    JDB     Deleted unneeded guard macro definition
   24-Dec-14    JDB     Changed fp_ucom return from uint32 to uint16
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   01-Dec-06    JDB     Reworked FFP helpers for 1000-F support, deleted f_pwr2
   16-Oct-06    JDB     Generalized FP calling sequences for F-Series
   12-Oct-06    JDB     Altered x_trun for F-Series FFP compatibility
   26-Sep-06    JDB     Moved from hp2100_fp.c to simplify extensions


   This file contains declarations used by the CPU to interface to the firmware
   and hardware floating-point operations in the 2100 and 1000 computers..


   Implementation notes:

    1. The 2100/1000-M/E Fast FORTRAN Processor (FFP) and 1000 F-Series Floating
       Point Processor (FPP) simulations require that the host compiler supports
       64-bit integers and that the HAVE_INT64 symbol is defined during
       compilation.  If this symbol is defined, two-word floating-point
       operations are handled in the FPP code.  If it is not defined, then FFP
       and FPP operations are not available, and floating-point support is
       limited to the firmware implementation of the six basic FP instructions
       (add, subtract, multiply, divide, fix, and float)..
*/



#if defined (HAVE_INT64)                                /* int64 support is available */


/* Special operands */

#define ACCUM               NULL                        /* result not returned */
#define NOP                 ZERO                        /* operand not needed */

static const OP ZERO = { { 0, 0, 0, 0, 0 } };           /* zero operand */


/* Floating-Point Processor routines */

extern void   fp_prec  (uint16 opcode, OPSIZE *operand_l, OPSIZE *operand_r, OPSIZE *result);
extern uint32 fp_exec  (uint16 opcode, OP *result, OP operand_l, OP operand_r);
extern OP     fp_accum (const OP *operand, OPSIZE precision);
extern uint16 fp_ucom  (OP *mantissa, OPSIZE precision);
extern uint32 fp_pcom  (OP *packed, OPSIZE precision);
extern uint32 fp_trun  (OP *result, OP source, OPSIZE precision);
extern uint32 fp_cvt   (OP *result, OPSIZE source_precision, OPSIZE dest_precision);


#else                                                   /* int64 support is not available */


/* Firmware floating-point routines */

extern uint32 f_as  (uint32 op, t_bool sub);            /* FAD/FSB */
extern uint32 f_mul (uint32 op);                        /* FMP */
extern uint32 f_div (uint32 op);                        /* FDV */
extern uint32 f_fix (void);                             /* FIX */
extern uint32 f_flt (void);                             /* FLT */


#endif                                                  /* int64 conditional */



/* Fast FORTRAN Processor helpers */

extern uint32 fp_pack   (OP *result, OP mantissa, int32 exponent, OPSIZE precision);
extern uint32 fp_nrpack (OP *result, OP mantissa, int32 exponent, OPSIZE precision);
extern uint32 fp_unpack (OP *mantissa, int32 *exponent, OP packed, OPSIZE precision);
