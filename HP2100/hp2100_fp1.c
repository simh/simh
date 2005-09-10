/* hp2100_fp1.c: HP 2100/21MX extended-precision floating point routines

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

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - DOS/RTE Relocatable Library Reference Manual
        (24998-90001, Oct-1981)

   The extended-precision floating-point format is a 48-bit extension of the
   32-bit format used for single precision.  A packed "XP" number consists of a
   40-bit twos-complement mantissa and an 8-bit twos-complement exponent.  The
   exponent is rotated left so that the sign is in the LSB.  Pictorially, an XP
   number appears in memory as follows:

      15 14                                         0
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |S |              mantissa high                 | : M
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |                 mantissa middle               | : M + 1
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     |     mantissa low      |      exponent      |XS| : M + 2
     +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
      15                    8  7                 1  0

   In a normalized value, the sign and MSB of the mantissa differ.  Zero is
   represented by all three words = 0.

   Internally, an unpacked XP number is contained in a structure having a signed
   64-bit mantissa and a signed 32-bit exponent.  The mantissa is masked to 48
   bits and left-justified, while the exponent is right-justified.
*/

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_fp1.h"

#if defined (HAVE_INT64)                                /* we need int64 support */

/* packed starting bit numbers */

#define XP_PAD          16                              /* padding LSBs */

#define XP_V_MSIGN      (47 + XP_PAD)                   /* mantissa sign */
#define XP_V_MANT       ( 8 + XP_PAD)                   /* mantissa */
#define XP_V_EXP        ( 1 + XP_PAD)                   /* exponent */
#define XP_V_ESIGN      ( 0 + XP_PAD)                   /* exponent sign */

/* packed bit widths */

#define XP_W_MSIGN       1                              /* mantissa sign */
#define XP_W_MANT       39                              /* mantissa */
#define XP_W_EXP         7                              /* exponent */
#define XP_W_ESIGN       1                              /* exponent sign */

/* packed bit masks */

#define XP_M_MSIGN      (((t_uint64) 1 << XP_W_MSIGN) - 1)  /* mantissa sign */
#define XP_M_MANT       (((t_uint64) 1 << XP_W_MANT)  - 1)  /* mantissa */
#define XP_M_EXP        (((t_uint64) 1 << XP_W_EXP)   - 1)  /* exponent */
#define XP_M_ESIGN      (((t_uint64) 1 << XP_W_ESIGN) - 1)  /* exponent sign */

/* packed field masks */

#define XP_MSIGN        (XP_M_MSIGN << XP_V_MSIGN)      /* mantissa sign */
#define XP_MANT         (XP_M_MANT << XP_V_MANT)        /* mantissa */
#define XP_SMANT        (XP_MSIGN | XP_MANT)            /* signed mantissa */

/* unpacked starting bit numbers */

#define XP_V_UMANT      (0 + XP_PAD)                    /* signed mantissa */

/* unpacked bit widths */

#define XP_W_USMANT     48                              /* signed mantissa */

/* unpacked bit masks */

#define XP_M_USMANT     (((t_uint64) 1 << XP_W_USMANT) - 1)  /* mantissa */

/* unpacked field masks */

#define XP_USMANT       (XP_M_USMANT << XP_V_UMANT)     /* signed mantissa */

/* values */

#define XP_ONEHALF      ((t_int64) 1 << (XP_V_MSIGN - 1))   /* mantissa = 0.5 */
#define XP_HALSBPOS     ((t_int64) 1 << (XP_V_MANT - 1))    /* + 1/2 LSB */
#define XP_HALSBNEG     ((t_int64) XP_HALSBPOS - 1)         /* - 1/2 LSB */
#define XP_MAXPOS       ((t_int64) XP_MANT)                 /* maximum pos mantissa */
#define XP_MAXNEG       ((t_int64) XP_MSIGN)                /* maximum neg mantissa */
#define XP_MAXEXP       ((t_int64) XP_M_EXP)                /* maximum pos exponent */

/* helpers */

#define DENORM(x)       ((((x) ^ (x) << 1) & XP_MSIGN) == 0)

#define TO_INT64(xpn)   ((t_int64) ((t_uint64) (xpn).high << 32 | (xpn).low))

/* internal unpacked extended-precision representation */

typedef struct {
    t_int64             mantissa;
    int32               exponent;
    } XPU;

/* Private routines */


/* Pack an unpacked mantissa and exponent */

static XPN to_xpn (t_uint64 m, int32 e)
{
XPN packed;

packed.high = (uint32) (m >> 32);
packed.low = (uint32) (m | ((e & XP_M_EXP) << XP_V_EXP) | ((e < 0) << XP_V_ESIGN));
return packed;
}


/* Unpack a packed number */

static XPU unpack (XPN packed)
{
XPU unpacked;

unpacked.mantissa = TO_INT64 (packed) & XP_SMANT;       /* left-justify mantissa */
unpacked.exponent = (int8) ((packed.low >> XP_V_EXP & XP_M_EXP) |  /* sign-extend exponent */
                   packed.low >> (XP_V_ESIGN - XP_W_EXP));

return unpacked;
}


/* Normalize an unpacked number */

static void normalize (XPU *unpacked)
{
if (unpacked->mantissa) {                               /* non-zero? */
    while (DENORM (unpacked->mantissa)) {               /* normal form? */
        unpacked->exponent = unpacked->exponent - 1;    /* no, so shift */
        unpacked->mantissa = unpacked->mantissa << 1;
        }
    }
else unpacked->exponent = 0;                            /* clean for zero */
return;
}


/* Round and pack an unpacked number */

static uint32 pack (XPN *packed, XPU unpacked)
{
int32 sign, overflow = 0;

normalize (&unpacked);                                  /* normalize */
sign = (unpacked.mantissa < 0);                         /* save sign */
unpacked.mantissa = (unpacked.mantissa +                /* round the number */
    (sign? XP_HALSBNEG: XP_HALSBPOS)) & XP_SMANT;       /* mask off rounding bits */
if (sign != (unpacked.mantissa < 0)) {                  /* pos overflow? */
    unpacked.mantissa =                                 /* renormalize */
        (t_uint64) unpacked.mantissa >> 1 & XP_SMANT;   /* and remask */
    unpacked.exponent = unpacked.exponent + 1;
    }
else normalize (&unpacked);                             /* neg overflow? renorm */
unpacked.mantissa = unpacked.mantissa & XP_SMANT;
if (unpacked.mantissa == 0)                             /* result 0? */
    packed->high = packed->low = 0;                     /* return 0 */
else if (unpacked.exponent < -(XP_MAXEXP + 1)) {        /* underflow? */
    packed->high = packed->low = 0;                     /* return 0 */
    overflow = 1;                                       /* and set overflow */
    }
else if (unpacked.exponent > XP_MAXEXP) {               /* overflow? */
    if (sign) *packed = to_xpn (XP_MAXNEG, XP_MAXEXP);  /* return neg infinity */
    else *packed = to_xpn (XP_MAXPOS, XP_MAXEXP);       /* or pos infinity */
    overflow = 1;                                       /* with overflow */
    }
else *packed = to_xpn (unpacked.mantissa, unpacked.exponent);
return overflow;
}


/* Complement an unpacked number */

static void complement (XPU *result)
{
result->mantissa = -result->mantissa;                   /* negate mantissa */
if (result->mantissa == XP_MAXNEG) {                    /* maximum negative? */
    result->mantissa = (t_uint64) result->mantissa >> 1; /* renormalize to pos */
    result->exponent = result->exponent + 1;            /* correct exponent */
    }
return;
}


/* Add two unpacked numbers

   The mantissas are first aligned (if necessary) by scaling the smaller of the
   two operands.  If the magnitude of the difference between the exponents is
   greater than the number of significant bits, then the smaller number has been
   scaled to zero, and so the sum is simply the larger operand.  Otherwise, the
   sum is computed and checked for overflow, which has occured if the signs of
   the operands are the same but differ from that of the result.  Scaling and
   renormalization is perfomed if overflow occurred. */

static void add (XPU *sum, XPU augend, XPU addend)
{
int32 magn;

if (augend.mantissa == 0) *sum = addend;                /* x + 0 = x */
else if (addend.mantissa == 0) *sum = augend;           /* 0 + x = x */
else {
    magn = augend.exponent - addend.exponent;           /* difference exponents */
    if (magn > 0) {                                     /* addend smaller? */
        *sum = augend;                                  /* preset augend */
        addend.mantissa = addend.mantissa >> magn;      /* align addend */
        }
    else {                                              /* augend smaller? */
        *sum = addend;                                  /* preset addend */
        magn = -magn;                                   /* make difference positive */
        augend.mantissa = augend.mantissa >> magn;      /* align augend */
        }
    if (magn <= XP_W_MANT + 1) {                        /* check mangitude */
        sum->mantissa = addend.mantissa + augend.mantissa;      /* add mantissas */
        if (((addend.mantissa < 0) == (augend.mantissa < 0)) &&  /* chk overflow */
            ((addend.mantissa < 0) != (sum->mantissa < 0))) {
            sum->mantissa = (addend.mantissa & XP_MSIGN) |           /* restore sign */
                               (t_uint64) sum->mantissa >> 1;    /* renormalize */
            sum->exponent = sum->exponent + 1;          /* adjust exponent */
            }
        }
    }
return;
}


/* Multiply two unpacked numbers

   The firmware first negates the operands as necessary so that the values are
   positive.  Then it performs six of the nine 16-bit x 16-bit = 32-bit unsigned
   multiplications required for a full 96-bit product.  Given a 48-bit
   multiplicand "a1a2a3" and a 48-bit multiplier "b1b2b3", the firmware performs
   these calculations to develop a 48-bit product:

                                a1      a2      a3
                            +-------+-------+-------+
                                b1      b2      b3
                            +-------+-------+-------+
                            _________________________

                        a1  *   b3      [m1]
                    +-------+-------+
                        a2  *   b2      [m2]
                    +-------+-------+
                a1  *   b2              [m3]
            +-------+-------+
                        a3  *   b1      [m4]
                    +-------+-------+
                a2  *   b1              [m5]
            +-------+-------+
        a1  *   b1                      [m6]
    +-------+-------+
    _________________________________

             product
    +-------+-------+-------+

   The least-significant words of intermediate multiplications [m1], [m2], and
   [m4] are used only to develop a carry bit into the 48-bit sum.  The product
   is complemented if necessary to restore the sign.

   Instead of implementing this algorithm directly, it is more efficient under
   simulation to use 32 x 32 = 64-bit multiplications, thereby reducing the
   number required from six to four. */

static void multiply (XPU *product, XPU multiplicand, XPU multiplier)
{
uint32 ah, al, bh, bl, carry, sign = 0;
t_uint64 hi, m1, m2, m3;

if ((multiplicand.mantissa == 0) || (multiplier.mantissa == 0))  /* x * 0 = 0 */
    product->mantissa = product->exponent = 0;
else {
    if (multiplicand.mantissa < 0) {                    /* negative? */
        complement (&multiplicand);                     /* complement operand */
        sign = ~sign;                                   /* track sign */
        }
    if (multiplier.mantissa < 0) {                      /* negative? */
        complement (&multiplier);                       /* complement operand */
        sign = ~sign;                                   /* track sign */
        }
    product->exponent = multiplicand.exponent +         /* compute exponent */
        multiplier.exponent + 1;

    ah = (uint32) (multiplicand.mantissa >> 32);        /* split multiplicand */
    al = (uint32) multiplicand.mantissa;                /* into high and low parts */
    bh = (uint32) (multiplier.mantissa >> 32);          /* split multiplier */
    bl = (uint32) multiplier.mantissa;                  /* into high and low parts */

    hi = ((t_uint64) ah * bh);                          /* form four cross products */
    m1 = ((t_uint64) ah * bl);                          /* using 32 x 32 = 64-bit */
    m2 = ((t_uint64) al * bh);                          /* hardware multiplies */
    m3 = ((t_uint64) al * bl);

    carry = ((uint32) m1 + (uint32) m2 +                /* form a carry bit */
        (uint32) (m3 >> 32)) >> (31 - XP_V_UMANT);      /* and align to LSB - 1 */

    product->mantissa = (hi + (m1 >> 32) +              /* align, sum, and mask */
        (m2 >> 32) + carry) & XP_USMANT;
    if (sign) complement (product);                     /* negate if required */
    }
return;
}


/* Divide two unpacked numbers

   The firmware performs division by calculating (1.0 / divisor) and then
   multiplying by the dividend.  The simulator uses 64-bit division and uses a
   "divide-and-correct" algorithm similar to the one employed by the base set
   single-precision floating-point division routine. */

static void divide (XPU *quotient, XPU dividend, XPU divisor)
{
t_int64 bh, bl, m1, m2, m3, m4;

if (divisor.mantissa == 0) {                            /* division by zero? */
    if (dividend.mantissa < 0)
        quotient->mantissa = XP_MSIGN;                  /* return minus infinity */
    else quotient->mantissa = XP_MANT;                  /* or plus infinity */
    quotient->exponent = XP_MAXEXP + 1;
    }
else if (dividend.mantissa == 0)                        /* dividend zero? */
    quotient->mantissa = quotient->exponent = 0;        /* yes; result is zero */
else {
    quotient->exponent = dividend.exponent -            /* division subtracts exponents */
                         divisor.exponent + 1;

    bh = divisor.mantissa >> 32;                        /* split divisor */
    bl = divisor.mantissa & 0xFFFFFFFF;

    m1 = (dividend.mantissa >> 2) / bh;                 /* form 1st partial quotient */
    m2 = (dividend.mantissa >> 2) % bh;                 /* obtain remainder */
    m3 = bl * m1;                                       /* calculate correction */
    m4 = ((m2 - (m3 >> 32)) << 32) / bh;                /* form 2nd partial quotient */

    quotient->mantissa = (m1 << 32) + m4;               /* merge quotients */
    }
return;
}

#endif

/* Global routines */

/* Extended-precision memory read */

XPN ReadX (uint32 va)
{
XPN packed;

packed.high = ReadW (va) << 16 | ReadW ((va + 1) & VAMASK);
packed.low = ReadW ((va + 2) & VAMASK) << 16;           /* read and pack */
return packed;
}


/* Extended-precision memory write */

void WriteX (uint32 va, XPN packed)
{
WriteW (va, (packed.high >> 16) & DMASK);               /* write high word */
WriteW ((va + 1) & VAMASK, packed.high & DMASK);        /* write middle word */
WriteW ((va + 2) & VAMASK, (packed.low >> 16) & DMASK); /* write low word */
}


#if defined (HAVE_INT64)

/* Extended-precision add */

uint32 x_add (XPN *sum, XPN augend, XPN addend)
{
XPU usum, uaddend, uaugend;

uaugend = unpack (augend);                              /* unpack augend */
uaddend = unpack (addend);                              /* unpack addend */
add (&usum, uaugend, uaddend);                          /* calculate sum */
return pack (sum, usum);                                /* pack sum */
}


/* Extended-precision subtract */

uint32 x_sub (XPN *difference, XPN minuend, XPN subtrahend)
{
XPU udifference, uminuend, usubtrahend;

uminuend = unpack (minuend);                            /* unpack minuend */
usubtrahend = unpack (subtrahend);                      /* unpack subtrahend */
complement (&usubtrahend);                              /* calculate difference */
add (&udifference, uminuend, usubtrahend);              /* pack difference */
return pack (difference, udifference);
}


/* Extended-precision multiply */

uint32 x_mpy (XPN *product, XPN multiplicand, XPN multiplier)
{
XPU uproduct, umultiplicand, umultiplier;

umultiplicand = unpack (multiplicand);                  /* unpack multiplicand */
umultiplier = unpack (multiplier);                      /* unpack multiplier */
multiply (&uproduct, umultiplicand, umultiplier);       /* calculate product */
return pack (product, uproduct);                        /* pack product */
}


/* Extended-precision divide */

uint32 x_div (XPN *quotient, XPN dividend, XPN divisor)
{
XPU uquotient, udividend, udivisor;

udividend = unpack (dividend);                          /* unpack dividend */
udivisor = unpack (divisor);                            /* unpack divisor */
divide (&uquotient, udividend, udivisor);               /* calculate quotient */
return pack (quotient, uquotient);                      /* pack quotient */
}


/* Pack an extended-precision number

   An unpacked mantissa is passed as a "packed" number with an unused exponent.
*/
uint32 x_pak (XPN *result, XPN mantissa, int32 exponent)
{
XPU unpacked;

unpacked.mantissa = TO_INT64 (mantissa);                /* retrieve mantissa */
unpacked.exponent = exponent;                           /* and exponent */
return pack (result, unpacked);                         /* pack them */
}


/* Complement an extended-precision mantissa

   An unpacked mantissa is passed as a "packed" number with an unused exponent.
   We return the exponent increment, i.e., either zero or one, depending on
   whether a renormalization was required. */

uint32 x_com (XPN *mantissa)
{
XPU unpacked;

unpacked.mantissa = TO_INT64 (*mantissa);               /* retrieve mantissa */
unpacked.exponent = 0;                                  /* exponent is irrelevant */
complement (&unpacked);                                 /* negate it */
*mantissa = to_xpn (unpacked.mantissa, 0);              /* replace mantissa */
return (uint32) unpacked.exponent;                      /* return exponent increment */
}


/* Complement an extended-precision number */

uint32 x_dcm (XPN *packed)
{
XPU unpacked;

unpacked = unpack (*packed);                            /* unpack the number */
complement (&unpacked);                                 /* negate it */
return pack (packed, unpacked);                         /* and repack */
}


/* Truncate an extended-precision number */

void x_trun (XPN *result, XPN source)
{
t_uint64 mask;
uint32 bitslost;
XPU unpacked;
const XPU one = { XP_ONEHALF, 1 };                      /* 0.5 * 2 ** 1 = 1.0 */

unpacked = unpack (source);
if (unpacked.exponent < 0)                              /* number < 0.5? */
    result->high = result->low = 0;                     /* return 0 */
else if (unpacked.exponent > XP_W_MANT)                 /* no fractional bits? */
    *result = source;                                   /* already integer */
else {
    mask = (XP_MANT >> unpacked.exponent) & XP_MANT;    /* mask fractional bits */
    bitslost = (uint32) (unpacked.mantissa & mask);     /* flag if bits lost */
    unpacked.mantissa = unpacked.mantissa & ~mask;      /* mask off fraction */
    if ((unpacked.mantissa < 0) && bitslost)            /* negative? */
        add (&unpacked, unpacked, one);                 /* truncate toward zero */
    pack (result, unpacked);                            /* (overflow cannot occur) */
    }
return;
}

#endif                                                  /* defined (HAVE_INT64) */
