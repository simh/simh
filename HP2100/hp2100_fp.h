/* hp2100_fp.h: HP 2100/21MX floating point declarations

   Copyright (c) 2002-2013, Robert M. Supnik
   Copyright (c) 2017,      J. David Bryan

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

   15-Feb-17    JDB     Deleted unneeded guard macro definition
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   01-Dec-06    JDB     Reworked FFP helpers for 1000-F support, deleted f_pwr2
   26-Sep-06    JDB     Moved from hp2100_fp.c to simplify extensions
*/


/* Firmware floating-point routines */

uint32 f_as (uint32 op, t_bool sub);                    /* FAD/FSB */
uint32 f_mul (uint32 op);                               /* FMP */
uint32 f_div (uint32 op);                               /* FDV */
uint32 f_fix (void);                                    /* FIX */
uint32 f_flt (void);                                    /* FLT */

/* Firmware FFP helpers */

uint32 fp_pack   (OP *result, OP mantissa, int32 exponent, OPSIZE precision);
uint32 fp_nrpack (OP *result, OP mantissa, int32 exponent, OPSIZE precision);
uint32 fp_unpack (OP *mantissa, int32 *exponent, OP packed, OPSIZE precision);
