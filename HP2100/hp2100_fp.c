/* hp2100_fp.c: HP 2100 floating point instructions

   Copyright (c) 2002-2015, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   03-Jan-15    JDB     Made the utility routines static
   21-Jan-08    JDB     Corrected fp_unpack mantissa high-word return
                        (from Mark Pizzolato)
   01-Dec-06    JDB     Reworked FFP helpers for 1000-F support, deleted f_pwr2
   22-Jul-05    RMS     Fixed compiler warning in Solaris (from Doug Glyn)
   25-Feb-05    JDB     Added FFP helpers f_pack, f_unpack, f_pwr2
   11-Feb-05    JDB     Fixed missing negative overflow renorm in StoreFP
   26-Dec-04    RMS     Separated A/B from M[0/1] for DMA IO (from Dave Bryan)
   15-Jul-03    RMS     Fixed signed/unsigned warning
   21-Oct-02    RMS     Recoded for compatibility with 21MX microcode algorithms


   The HP2100 uses a unique binary floating point format:

     15 14                                         0
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |S |               fraction high                | : A
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |     fraction low      |      exponent      |XS| : A + 1
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     15                    8  7                 1  0

        where   S = 0 for plus fraction, 1 for minus fraction
                fraction = s.bbbbb..., 24 binary digits
                exponent = 2**+/-n
                XS = 0 for plus exponent, 1 for minus exponent

   Numbers can be normalized or unnormalized but are always normalized
   when loaded.

   Unpacked floating point numbers are stored in structure ufp

        exp     =       exponent, 2's complement
        h'l     =       fraction, 2's comp, left justified

   This routine tries to reproduce the algorithms of the 2100/21MX
   microcode in order to achieve 'bug-for-bug' compatibility.  In
   particular,

   - The FIX code produces various results in B.
   - The fraction multiply code uses 16b x 16b multiplies to produce
     a 31b result.  It always loses the low order bit of the product.
   - The fraction divide code is an approximation that may produce
     an error of 1 LSB.
   - Signs are tracked implicitly as part of the fraction.  Unnormalized
     inputs may cause the packup code to produce the wrong sign.
   - "Unclean" zeros (zero fraction, non-zero exponent) are processed
     like normal operands.

   Implementation notes:

    1. The 2100/1000-M/E Fast FORTRAN Processor (FFP) and 1000 F-Series Floating
       Point Processor (FPP) simulations require that the host compiler support
       64-bit integers and the HAVE_INT64 symbol be defined during compilation.
       If this symbol is defined, two-word floating-point operations are handled
       in the FPP code, and this module is not used.  If it is not defined, then
       FFP and FPP operations are not available, and this module provides the
       floating-point support.
*/

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"
#include "hp2100_fp.h"

#if !defined (HAVE_INT64)                               /* int64 support unavailable */

struct ufp {                                            /* unpacked fp */
    int32       exp;                                    /* exp */
    uint32      fr;                                     /* frac */
    };

#define FP_V_SIGN       31                              /* sign */
#define FP_M_SIGN       01
#define FP_V_FR         8                               /* fraction */
#define FP_M_FR         077777777
#define FP_V_EXP        1                               /* exponent */
#define FP_M_EXP        0177
#define FP_V_EXPS       0                               /* exp sign */
#define FP_M_EXPS       01
#define FP_SIGN         (FP_M_SIGN << FP_V_SIGN)
#define FP_FR           (FP_M_FR << FP_V_FR)
#define FP_EXP          (FP_M_EXP << FP_V_EXP)
#define FP_EXPS         (FP_M_EXPS << FP_V_EXPS)
#define FP_GETSIGN(x)   (((x) >> FP_V_SIGN) & FP_M_SIGN)
#define FP_GETEXP(x)    (((x) >> FP_V_EXP) & FP_M_EXP)
#define FP_GETEXPS(x)   (((x) >> FP_V_EXPS) & FP_M_EXPS)

#define FP_NORM         (1 << (FP_V_SIGN - 1))          /* normalized */
#define FP_LOW          (1 << FP_V_FR)
#define FP_RNDP         (1 << (FP_V_FR - 1))            /* round for plus */
#define FP_RNDM         (FP_RNDP - 1)                   /* round for minus */

#define FPAB            ((((uint32) AR) << 16) | ((uint32) BR))

/* Fraction shift; 0 < shift < 32 */

#define FR_ARS(v,s)     (((v) >> (s)) | (((v) & FP_SIGN)? \
                        (((uint32) DMASK32) << (32 - (s))): 0)) & DMASK32

#define FR_NEG(v)       ((~(v) + 1) & DMASK32)

/* Utility routines */

static uint32 UnpackFP (struct ufp *fop, uint32 opnd);
static void NormFP (struct ufp *fop);
static uint32 PackFP (struct ufp *fop);
static uint32 StoreFP (struct ufp *fop);

/* Floating to integer conversion */

uint32 f_fix (void)
{
struct ufp fop;
uint32 res = 0;

UnpackFP (&fop, FPAB);                                  /* unpack op */
if (fop.exp < 0) {                                      /* exp < 0? */
    AR = 0;                                             /* result = 0 */
    return 0;                                           /* B unchanged */
    }
if (fop.exp > 15) {                                     /* exp > 15? */
    BR = AR;                                            /* B has high bits */
    AR = 077777;                                        /* result = 77777 */
    return 1;                                           /* overflow */
    }
if (fop.exp < 15) {                                     /* if not aligned */
    res = FR_ARS (fop.fr, 15 - fop.exp);                /* shift right */
    AR = (res >> 16) & DMASK;                           /* AR gets result */
    }
BR = AR;
if ((AR & SIGN) && ((fop.fr | res) & DMASK))            /* any low bits lost? */
    AR = (AR + 1) & DMASK;                              /* round up */
return 0;
}

/* Integer to floating conversion */

uint32 f_flt (void)
{
struct ufp res = { 15, 0 };                             /* +, 2**15 */

res.fr = ((uint32) AR) << 16;                           /* left justify */
StoreFP (&res);                                         /* store result */
return 0;                                               /* clr overflow */
}

/* Floating point add/subtract */

uint32 f_as (uint32 opnd, t_bool sub)
{
struct ufp fop1, fop2, t;
int32 ediff;

UnpackFP (&fop1, FPAB);                                 /* unpack A-B */
UnpackFP (&fop2, opnd);                                 /* get op */
if (sub) {                                              /* subtract? */
    fop2.fr = FR_NEG (fop2.fr);                         /* negate frac */
    if (fop2.fr == ((uint32) FP_SIGN)) {                /* -1/2? */
        fop2.fr = fop2.fr >> 1;                         /* special case */
        fop2.exp = fop2.exp + 1;
        }
    }
if (fop1.fr == 0) fop1 = fop2;                          /* op1 = 0? res = op2 */
else if (fop2.fr != 0) {                                /* op2 = 0? no add */
    if (fop1.exp < fop2.exp) {                          /* |op1| < |op2|? */
        t = fop2;                                       /* swap operands */
        fop2 = fop1;
        fop1 = t;
        }
    ediff = fop1.exp - fop2.exp;                        /* get exp diff */
    if (ediff <= 24) {
        if (ediff) fop2.fr = FR_ARS (fop2.fr, ediff);   /* denorm, signed */
        if ((fop1.fr ^ fop2.fr) & FP_SIGN)              /* unlike signs? */
            fop1.fr = fop1.fr + fop2.fr;                /* eff subtract */
        else {                                          /* like signs */
            fop1.fr = fop1.fr + fop2.fr;                /* eff add */
            if (fop2.fr & FP_SIGN) {                    /* both -? */
                if ((fop1.fr & FP_SIGN) == 0) {         /* overflow? */
                    fop1.fr = FP_SIGN | (fop1.fr >> 1); /* renormalize */
                    fop1.exp = fop1.exp + 1;            /* incr exp */
                    }
                }
            else if (fop1.fr & FP_SIGN) {               /* both +, cry out? */
                fop1.fr = fop1.fr >> 1;                 /* renormalize */
                fop1.exp = fop1.exp + 1;                /* incr exp */
                }
            }                                           /* end else like */
        }                                               /* end if ediff */
    }                                                   /* end if fop2 */
return StoreFP (&fop1);                                 /* store result */
}

/* Floating point multiply - passes diagnostic */

uint32 f_mul (uint32 opnd)
{
struct ufp fop1, fop2;
struct ufp res = { 0, 0 };
int32 shi1, shi2, t1, t2, t3, t4, t5;

UnpackFP (&fop1, FPAB);                                 /* unpack A-B */
UnpackFP (&fop2, opnd);                                 /* unpack op */
if (fop1.fr && fop2.fr) {                               /* if both != 0 */
    res.exp = fop1.exp + fop2.exp + 1;                  /* exp = sum */
    shi1 = SEXT (fop1.fr >> 16);                        /* mpy hi */
    shi2 = SEXT (fop2.fr >> 16);                        /* mpc hi */
    t1 = shi2 * ((int32) ((fop1.fr >> 1) & 077600));    /* mpc hi * (mpy lo/2) */
    t2 = shi1 * ((int32) ((fop2.fr >> 1) & 077600));    /* mpc lo * (mpy hi/2) */
    t3 = t1 + t2;                                       /* cross product */
    t4 = (shi1 * shi2) & ~1;                            /* mpy hi * mpc hi */
    t5 = (SEXT (t3 >> 16)) << 1;                        /* add in cross */
    res.fr = (t4 + t5) & DMASK32;                       /* bit<0> is lost */
    }
return StoreFP (&res);                                  /* store */
}

/* Floating point divide - reverse engineered from diagnostic */

static uint32 divx (uint32 ba, uint32 dvr, uint32 *rem)
{
int32 sdvd = 0, sdvr = 0;
uint32 q, r;

if (ba & FP_SIGN) sdvd = 1;                             /* 32b/16b signed dvd */
if (dvr & SIGN) sdvr = 1;                               /* use old-fashioned */
if (sdvd) ba = (~ba + 1) & DMASK32;                     /* unsigned divides, */
if (sdvr) dvr = (~dvr + 1) & DMASK;                     /* as results may ovflo */
q = ba / dvr;
r = ba % dvr;
if (sdvd ^ sdvr) q = (~q + 1) & DMASK;
if (sdvd) r = (~r + 1) & DMASK;
if (rem) *rem = r;
return q;
}

uint32 f_div (uint32 opnd)
{
struct ufp fop1, fop2;
struct ufp quo = { 0, 0 };
uint32 ba, q0, q1, q2, dvrh;

UnpackFP (&fop1, FPAB);                                 /* unpack A-B */
UnpackFP (&fop2, opnd);                                 /* unpack op */
dvrh = (fop2.fr >> 16) & DMASK;                         /* high divisor */
if (dvrh == 0) {                                        /* div by zero? */
    AR = 0077777;                                       /* return most pos */
    BR = 0177776;
    return 1;
    }
if (fop1.fr) {                                          /* dvd != 0? */
    quo.exp = fop1.exp - fop2.exp + 1;                  /* exp = diff */
    ba = FR_ARS (fop1.fr, 2);                           /* prevent ovflo */
    q0 = divx (ba, dvrh, &ba);                          /* Q0 = dvd / dvrh */
    ba = (ba & ~1) << 16;                               /* remainder */
    ba = FR_ARS (ba, 1);                                /* prevent ovflo */
    q1 = divx (ba, dvrh, NULL);                         /* Q1 = rem / dvrh */
    ba = (fop2.fr & 0xFF00) << 13;                      /* dvrl / 8 */
    q2 = divx (ba, dvrh, NULL);                         /* dvrl / dvrh */
    ba = -(SEXT (q2)) * (SEXT (q0));                    /* -Q0 * Q2 */
    ba = (ba >> 16) & 0xFFFF;                           /* save ms half */
    if (q1 & SIGN) quo.fr = quo.fr - 0x00010000;        /* Q1 < 0? -1 */
    if (ba & SIGN) quo.fr = quo.fr - 0x00010000;        /* -Q0*Q2 < 0? */
    quo.fr = quo.fr + ((ba << 2) & 0xFFFF) + q1;        /* rest prod, add Q1 */
    quo.fr = quo.fr << 1;                               /* shift result */
    quo.fr = quo.fr + (q0 << 16);                       /* add Q0 */
    }                                                   /* end if fop1.h */
return StoreFP (&quo);                                  /* store result */
}

/* Utility routines */

/* Unpack operand */

static uint32 UnpackFP (struct ufp *fop, uint32 opnd)
{
fop->fr = opnd & FP_FR;                                 /* get frac */
fop->exp = FP_GETEXP (opnd);                            /* get exp */
if (FP_GETEXPS (opnd)) fop->exp = fop->exp | ~FP_M_EXP; /* < 0? sext */
return FP_GETSIGN (opnd);                               /* return sign */
}

/* Normalize unpacked floating point number */

static void NormFP (struct ufp *fop)
{
if (fop->fr) {                                          /* any fraction? */
    uint32 test = (fop->fr >> 1) & FP_NORM;
    while ((fop->fr & FP_NORM) == test) {               /* until norm */
        fop->exp = fop->exp - 1;
        fop->fr = (fop->fr << 1);
        }
    }
else fop->exp = 0;                                      /* clean 0 */
return;
}

/* Pack fp number */

static uint32 PackFP (struct ufp *fop)
{
return (fop->fr & FP_FR) |                              /* merge frac */
       ((fop->exp & FP_M_EXP) << FP_V_EXP) |            /* and exp */
       ((fop->exp < 0)? (1 << FP_V_EXPS): 0);           /* add exp sign */
}

/* Round fp number, store, generate overflow */

static uint32 StoreFP (struct ufp *fop)
{
uint32 sign, svfr, hi, ov = 0;

NormFP (fop);                                           /* normalize */
svfr = fop->fr;                                         /* save fraction */
sign = FP_GETSIGN (fop->fr);                            /* save sign */
fop->fr = (fop->fr + (sign? FP_RNDM: FP_RNDP)) & FP_FR; /* round */
if ((fop->fr ^ svfr) & FP_SIGN) {                       /* sign change? */
    fop->fr = fop->fr >> 1;                             /* renormalize */
    fop->exp = fop->exp + 1;
    }
else NormFP (fop);                                      /* check for norm */
if (fop->fr == 0) hi = 0;                               /* result 0? */
else if (fop->exp < -(FP_M_EXP + 1)) {                  /* underflow? */
    hi = 0;                                             /* store clean 0 */
    ov = 1;
    }
else if (fop->exp > FP_M_EXP) {                         /* overflow? */
    hi = 0x7FFFFFFE;                                    /* all 1's */
    ov = 1;
    }
else hi = PackFP (fop);                                 /* pack mant and exp */
AR = (hi >> 16) & DMASK;
BR = hi & DMASK;
return ov;
}


/* Single-precision Fast FORTRAN Processor helpers. */

/* Pack mantissa and exponent and return fp value. */

uint32 fp_pack (OP *result, OP mantissa, int32 exponent, OPSIZE precision)
{
struct ufp fop;
uint32 val;

fop.fr = ((uint32) mantissa.fpk[0] << 16) | mantissa.fpk[1];
fop.exp = exponent;
val = PackFP (&fop);
result->fpk[0] = (int16) (val >> 16);
result->fpk[1] = (int16) val;
return 0;
}

/* Normalize, round, and pack mantissa and exponent and return fp value. */

uint32 fp_nrpack (OP *result, OP mantissa, int32 exponent, OPSIZE precision)
{
struct ufp fop;
uint32 ovf;

fop.fr = ((uint32) mantissa.fpk[0] << 16) | mantissa.fpk[1];
fop.exp = exponent;
ovf = StoreFP (&fop);
result->fpk[0] = AR;
result->fpk[1] = BR;
return ovf;
}

/* Unpack fp number in into mantissa and exponent. */

uint32 fp_unpack (OP *mantissa, int32 *exponent, OP packed, OPSIZE precision)
{
struct ufp fop;
uint32 operand;

operand = ((uint32) packed.fpk[0] << 16) | packed.fpk[1];
UnpackFP (&fop, operand);
mantissa->fpk[0] = (uint16) (fop.fr >> 16);
mantissa->fpk[1] = (uint16) fop.fr;
*exponent = fop.exp;
return 0;
}

#endif                                                  /* int64 support unavailable */
