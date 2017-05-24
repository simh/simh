/* hp2100_fp1.h: HP 2100/1000 multiple-precision floating point definitions

   Copyright (c) 2005-2014, J. David Bryan

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

   24-Dec-14    JDB     Changed fp_ucom return from uint32 to uint16
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   16-Oct-06    JDB     Generalized FP calling sequences for F-Series
   12-Oct-06    JDB     Altered x_trun for F-Series FFP compatibility
*/

#ifndef HP2100_FP1_H_
#define HP2100_FP1_H_  0


/* Special operands. */

#define ACCUM   NULL                                    /* result not returned */
static const OP NOP = { { 0, 0, 0, 0, 0 } };            /* unneeded operand */


/* Generalized floating-point handlers. */

void   fp_prec   (uint16 opcode, OPSIZE *operand_l, OPSIZE *operand_r, OPSIZE *result);
uint32 fp_exec   (uint16 opcode, OP *result, OP operand_l, OP operand_r);
OP     fp_accum  (const OP *operand, OPSIZE precision);
uint32 fp_pack   (OP *result, OP mantissa, int32 exponent, OPSIZE precision);
uint32 fp_nrpack (OP *result, OP mantissa, int32 exponent, OPSIZE precision);
uint32 fp_unpack (OP *mantissa, int32 *exponent, OP packed, OPSIZE precision);
uint16 fp_ucom   (OP *mantissa, OPSIZE precision);
uint32 fp_pcom   (OP *packed, OPSIZE precision);
uint32 fp_trun   (OP *result, OP source, OPSIZE precision);
uint32 fp_cvt    (OP *result, OPSIZE source_precision, OPSIZE dest_precision);

#endif
