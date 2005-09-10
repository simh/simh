/* hp2100_fp1.h: HP 2100/21MX extended-precision floating point definitions

   Copyright (c) 2005, J. David Bryan

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.
*/

#ifndef _HP2100_FP1_H_
#define _HP2100_FP1_H_  0

/* HP memory representation of an extended-precision number */

typedef struct {
    uint32      high;
    uint32      low;
    } XPN;


#define AS_XPN(x) (*(XPN *) &(x))                       /* view as XPN */

XPN ReadX (uint32 va);
void WriteX (uint32 va, XPN packed);

uint32 x_add (XPN *sum, XPN augend, XPN addend);
uint32 x_sub (XPN *difference, XPN minuend, XPN subtrahend);
uint32 x_mpy (XPN *product, XPN multiplicand, XPN multiplier);
uint32 x_div (XPN *quotient, XPN dividend, XPN divisor);
uint32 x_pak (XPN *result, XPN mantissa, int32 exponent);
uint32 x_com (XPN *mantissa);
uint32 x_dcm (XPN *packed);
void x_trun (XPN *result, XPN source);

#endif
