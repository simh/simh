/* pdp10_mdfp.c: PDP-10 multiply/divide and floating point simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   2-Apr-04     RMS     Fixed bug in floating point unpack
                        Fixed bug in FIXR (found by Phil Stone, fixed by
                        Chris Smith)
   31-Aug-01    RMS     Changed int64 to t_int64 for Windoze
   10-Aug-01    RMS     Removed register in declarations

   Instructions handled in this module:
        imul            integer multiply
        idiv            integer divide
        mul             multiply
        div             divide
        dmul            double precision multiply
        ddiv            double precision divide
        fad(r)          floating add (and round)
        fsb(r)          floating subtract (and round)
        fmp(r)          floating multiply (and round)
        fdv(r)          floating divide and round
        fsc             floating scale
        fix(r)          floating to fixed (and round)
        fltr            fixed to floating and round
        dfad            double precision floating add/subtract
        dfmp            double precision floating multiply
        dfdv            double precision floating divide

   The PDP-10 stores double (quad) precision integers in sequential
   AC's or memory locations.  Integers are stored in 2's complement
   form.  Only the sign of the high order word matters; the signs
   in low order words are ignored on input and set to the sign of
   the result on output.  Quad precision integers exist only in the
   AC's as the result of a DMUL or the dividend of a DDIV.

    0 00000000011111111112222222222333333
    0 12345678901234567890123456789012345
   +-+-----------------------------------+
   |S|      high order integer           | AC(n), A
   +-+-----------------------------------+
   |S|      low order integer            | AC(n + 1), A + 1
   +-+-----------------------------------+
   |S|      low order integer            | AC(n + 2)
   +-+-----------------------------------+
   |S|      low order integer            | AC(n + 3)
   +-+-----------------------------------+

   The PDP-10 supports two floating point formats: single and double
   precision.  In both, the exponent is 8 bits, stored in excess
   128 notation.  The fraction is expected to be normalized.  A
   single precision floating point number has 27 bits of fraction;
   a double precision number has 62 bits of fraction (the sign
   bit of the second word is ignored and is set to zero).

   In a negative floating point number, the exponent is stored in
   one's complement form, the fraction in two's complement form.

    0 00000000 011111111112222222222333333
    0 12345678 901234567890123456789012345
   +-+--------+---------------------------+
   |S|exponent|      high order fraction  | AC(n), A
   +-+--------+---------------------------+
   |0|      low order fraction            | AC(n + 1), A + 1
   +-+------------------------------------+

   Note that treatment of the sign is different for double precision
   integers and double precision floating point.  DMOVN (implemented
   as an inline macro) follows floating point conventions.

   The original PDP-10 CPU (KA10) used a different format for double
   precision numbers and included certain instructions to make
   software support easier.  These instructions were phased out in
   the KL10 and KS10 and are treated as MUUO's.

   The KL10 added extended precision (11-bit exponent) floating point
   format (so-called G floating).  These instructions were not
   implemented in the KS10 and are treated as MUUO's.
*/

#include "pdp10_defs.h"
#include <setjmp.h>

typedef struct {                                        /* unpacked fp number */
    int32               sign;                           /* sign */
    int32               exp;                            /* exponent */
    t_uint64    fhi;                                    /* fraction high */
    t_uint64    flo;                                    /* for double prec */
    } UFP;

#define MSK32           0xFFFFFFFF
#define FIT27           (DMASK - 0x07FFFFFF)
#define FIT32           (DMASK - MSK32)
#define SFRC            TRUE                            /* frac 2's comp */
#define AFRC            FALSE                           /* frac abs value */

/* In packed floating point number */

#define FP_BIAS         0200                            /* exponent bias */
#define FP_N_FHI        27                              /* # of hi frac bits */
#define FP_V_FHI        0                               /* must be zero */
#define FP_M_FHI        INT64_C(0000777777777)
#define FP_N_EXP        8                               /* # of exp bits */
#define FP_V_EXP        (FP_V_FHI + FP_N_FHI)
#define FP_M_EXP        0377
#define FP_V_SIGN       (FP_V_EXP + FP_N_EXP)           /* sign */
#define FP_N_FLO        35                              /* # of lo frac bits */
#define FP_V_FLO        0                               /* must be zero */
#define FP_M_FLO        INT64_C(0377777777777)
#define GET_FPSIGN(x)   ((int32) (((x) >> FP_V_SIGN) & 1))
#define GET_FPEXP(x)    ((int32) (((x) >> FP_V_EXP) & FP_M_EXP))
#define GET_FPHI(x)     ((x) & FP_M_FHI)
#define GET_FPLO(x)     ((x) & FP_M_FLO)

/* In unpacked floating point number */

#define FP_N_GUARD      1                               /* # of guard bits */
#define FP_V_UFLO       FP_N_GUARD                      /* <35:1> */
#define FP_V_URNDD      (FP_V_UFLO - 1)                 /* dp round bit */
#define FP_V_UFHI       (FP_V_UFLO + FP_N_FLO)          /* <62:36> */
#define FP_V_URNDS      (FP_V_UFHI - 1)                 /* sp round bit */
#define FP_V_UCRY       (FP_V_UFHI + FP_N_FHI)          /* <63> */
#define FP_V_UNORM      (FP_V_UCRY - 1)                 /* normalized bit */
#define FP_UFHI         INT64_C(0x7FFFFFF000000000)
#define FP_UFLO         INT64_C(0x0000000FFFFFFFFE)
#define FP_UFRAC        INT64_C(0x7FFFFFFFFFFFFFFE)
#define FP_URNDD        INT64_C(0x0000000000000001)
#define FP_URNDS        INT64_C(0x0000000800000000)
#define FP_UNORM        INT64_C(0x4000000000000000)
#define FP_UCRY         INT64_C(0x8000000000000000)
#define FP_ONES         INT64_C(0xFFFFFFFFFFFFFFFF)

#define UNEG(x)         ((~x) + 1)
#define DUNEG(x)        x.flo = UNEG (x.flo); x.fhi = ~x.fhi + (x.flo == 0)

extern d10 *ac_cur;                                     /* current AC block */
extern int32 flags;                                     /* flags */
void mul (d10 a, d10 b, d10 *rs);
void funpack (d10 h, d10 l, UFP *r, t_bool sgn);
void fnorm (UFP *r, t_int64 rnd);
d10 fpack (UFP *r, d10 *lo, t_bool fdvneg);

/* Integer multiply - checked against KS-10 ucode */

d10 imul (d10 a, d10 b)
{
d10 rs[2];

if ((a == SIGN) && (b == SIGN)) {                       /* KS10 hack */
    SETF (F_AOV | F_T1);                                /* -2**35 squared */
    return SIGN;
    }
mul (a, b, rs);                                         /* mpy, dprec result */
if (rs[0] && (rs[0] != ONES)) {                         /* high not all sign? */
    rs[1] = TSTS (a ^ b)? SETS (rs[1]): CLRS (rs[1]);   /* set sign */
    SETF (F_AOV | F_T1);                                /* overflow */
    }
return rs[1];
}

/* Integer divide, return quotient, remainder  - checked against KS10 ucode
   The KS10 does not recognize -2^35/-1 as an error.  Instead, it produces
   2^35 (that is, -2^35) as the incorrect result.
*/

t_bool idiv (d10 a, d10 b, d10 *rs)
{
d10 dvd = ABS (a);                                      /* make ops positive */
d10 dvr = ABS (b);

if (dvr == 0) {                                         /* divide by 0? */
    SETF (F_DCK | F_AOV | F_T1);                        /* set flags, return */
    return FALSE;
    }
rs[0] = dvd / dvr;                                      /* get quotient */
rs[1] = dvd % dvr;                                      /* get remainder */
if (TSTS (a ^ b))                                       /* sign of result */
    rs[0] = NEG (rs[0]);
if (TSTS (a))                                           /* sign of remainder */
    rs[1] = NEG (rs[1]);
return TRUE;
}

/* Multiply, return double precision result - checked against KS10 ucode */

void mul (d10 s1, d10 s2, d10 *rs)
{
t_uint64 a = ABS (s1);
t_uint64 b = ABS (s2);
t_uint64 t, u, r;

if ((a == 0) || (b == 0)) {                             /* operand = 0? */
    rs[0] = rs[1] = 0;                                  /* result 0 */
    return;
    }
if ((a & FIT32) || (b & FIT32)) {                       /* fit in 64b? */
    t = a >> 18;                                        /* no, split in half */
    a = a & RMASK;                                      /* "dp" multiply */
    u = b >> 18;
    b = b & RMASK;
    r = (a * b) + (((a * u) + (b * t)) << 18);          /* low is only 35b */
    rs[0] = ((t * u) << 1) + (r >> 35);                 /* so lsh hi 1 */
    rs[1] = r & MMASK;
    }
else {
    r = a * b;                                          /* fits, native mpy */
    rs[0] = r >> 35;                                    /* split at bit 35 */
    rs[1] = r & MMASK;
    }

if (TSTS (s1 ^ s2)) {                                   /* result -? */
    MKDNEG (rs);
    }
else if (TSTS (rs[0])) {                                /* result +, 2**70? */
    SETF (F_AOV | F_T1);                                /* overflow */
    rs[1] = SETS (rs[1]);                               /* consistent - */
    }
return;
}

/* Divide, return quotient and remainder - checked against KS10 ucode
   Note that the initial divide check catches the case -2^70/-2^35;
   thus, the quotient can have at most 35 bits.
*/

t_bool divi (int32 ac, d10 b, d10 *rs)
{
int32 p1 = ADDAC (ac, 1);
d10 dvr = ABS (b);                                      /* make divr positive */
t_int64 t;
int32 i;
d10 dvd[2];

dvd[0] = AC(ac);                                        /* divd high */
dvd[1] = CLRS (AC(p1));                                 /* divd lo, clr sgn */
if (TSTS (AC(ac))) {                                    /* make divd positive */
    DMOVN (dvd);
    }
if (dvd[0] >= dvr) {                                    /* divide fail? */
    SETF (F_AOV | F_DCK | F_T1);                        /* set flags, return */
    return FALSE;
    }
if (dvd[0] & FIT27) {                                   /* fit in 63b? */
    for (i = 0, rs[0] = 0; i < 35; i++) {               /* 35 quotient bits */
        dvd[0] = (dvd[0] << 1) | ((dvd[1] >> 34) & 1);
        dvd[1] = (dvd[1] << 1) & MMASK;                 /* shift dividend */
        rs[0] = rs[0] << 1;                             /* shift quotient */
        if (dvd[0] >= dvr) {                            /* subtract work? */
            dvd[0] = dvd[0] - dvr;                      /* quo bit is 1 */
            rs[0] = rs[0] + 1;
            }
        }
    rs[1] = dvd[0];                                     /* store remainder */
    }
else {
    t = (dvd[0] << 35) | dvd[1];                        /* concatenate */
    rs[0] = t / dvr;                                    /* quotient */
    rs[1] = t % dvr;                                    /* remainder */
    }
if (TSTS (AC(ac) ^ b))                                  /* sign of result */
    rs[0] = NEG (rs[0]);
if (TSTS (AC(ac)))                                      /* sign of remainder */
    rs[1] = NEG (rs[1]);
return TRUE;
}

/* Double precision multiply.  This is done the old fashioned way.  Cross
   product multiplies would be a lot faster but would require more code.
*/

void dmul (int32 ac, d10 *mpy)
{
int32 p1 = ADDAC (ac, 1);
int32 p2 = ADDAC (ac, 2);
int32 p3 = ADDAC (ac, 3);
int32 i;
d10 mpc[2], sign;

mpc[0] = AC(ac);                                        /* mplcnd hi */
mpc[1] = CLRS (AC(p1));                                 /* mplcnd lo, clr sgn */
sign = mpc[0] ^ mpy[0];                                 /* sign of result */
if (TSTS (mpc[0])) {                                    /* get abs (mpcnd) */
    DMOVN (mpc);
    }
if (TSTS (mpy[0])) {                                    /* get abs (mpyer) */
    DMOVN (mpy);
    }
else mpy[1] = CLRS (mpy[1]);                            /* clear mpy lo sign */
AC(ac) = AC(p1) = AC(p2) = AC(p3) = 0;                  /* clear AC's */
if (((mpy[0] | mpy[1]) == 0) || ((mpc[0] | mpc[1]) == 0))
    return;
for (i = 0; i < 71; i++) {                              /* 71 mpyer bits */
    if (i) {                                            /* shift res, mpy */
        AC(p3) = (AC(p3) >> 1) | ((AC(p2) & 1) << 34);
        AC(p2) = (AC(p2) >> 1) | ((AC(p1) & 1) << 34);
        AC(p1) = (AC(p1) >> 1) | ((AC(ac) & 1) << 34);
        AC(ac) = AC(ac) >> 1;
        mpy[1] = (mpy[1] >> 1) | ((mpy[0] & 1) << 34);
        mpy[0] = mpy[0] >> 1;
        }
    if (mpy[1] & 1) {                                   /* if mpy lo bit = 1 */
        AC(p1) = AC(p1) + mpc[1];
        AC(ac) = AC(ac) + mpc[0] + (TSTS (AC(p1) != 0));
        AC(p1) = CLRS (AC(p1));
        }
    }
if (TSTS (sign)) {                                      /* result minus? */
    AC(p3) = (-AC(p3)) & MMASK;                         /* quad negate */
    AC(p2) = (~AC(p2) + (AC(p3) == 0)) & MMASK;
    AC(p1) = (~AC(p1) + (AC(p2) == 0)) & MMASK;
    AC(ac) = (~AC(ac) + (AC(p1) == 0)) & DMASK;
    }
else if (TSTS (AC(ac)))                                 /* wrong sign */
    SETF (F_AOV | F_T1);
if (TSTS (AC(ac))) {                                    /* if result - */
    AC(p1) = SETS (AC(p1));                             /* make signs consistent */
    AC(p2) = SETS (AC(p2));
    AC(p3) = SETS (AC(p3));
    }
return;
}

/* Double precision divide - checked against KS10 ucode */

void ddiv (int32 ac, d10 *dvr)
{
int32 i, cryin;
d10 sign, qu[2], dvd[4];

dvd[0] = AC(ac);                                        /* save dividend */
for (i = 1; i < 4; i++)
    dvd[i] = CLRS (AC(ADDAC (ac, i)));
sign = AC(ac) ^ dvr[0];                                 /* sign of result */
if (TSTS (AC(ac))) {                                    /* get abs (dividend) */
    for (i = 3, cryin = 1; i > 0; i--) {                /* negate quad */
        dvd[i] = (~dvd[i] + cryin) & MMASK;             /* comp + carry in */
        if (dvd[i])                                     /* next carry in */
            cryin = 0;
        }
    dvd[0] = (~dvd[0] + cryin) & DMASK;
    }
if (TSTS (dvr[0])) {                                    /* get abs (divisor) */
    DMOVN (dvr);
    }
else dvr[1] = CLRS (dvr[1]);
if (DCMPGE (dvd, dvr)) {                                /* will divide work? */
    SETF (F_AOV | F_DCK | F_T1);                        /* no, set flags */
    return;
    }
qu[0] = qu[1] = 0;                                      /* clear quotient */
for (i = 0; i < 70; i++) {                              /* 70 quotient bits */
    dvd[0] = ((dvd[0] << 1) | ((dvd[1] >> 34) & 1)) & DMASK;;
    dvd[1] = ((dvd[1] << 1) | ((dvd[2] >> 34) & 1)) & MMASK;
    dvd[2] = ((dvd[2] << 1) | ((dvd[3] >> 34) & 1)) & MMASK;
    dvd[3] = (dvd[3] << 1) & MMASK;                     /* shift dividend */
    qu[0] = (qu[0] << 1) | ((qu[1] >> 34) & 1);         /* shift quotient */
    qu[1] = (qu[1] << 1) & MMASK;
    if (DCMPGE (dvd, dvr)) {                            /* subtract work? */
        dvd[0] = dvd[0] - dvr[0] - (dvd[1] < dvr[1]);
        dvd[1] = (dvd[1] - dvr[1]) & MMASK;             /* do subtract */
        qu[1] = qu[1] + 1;                              /* set quotient bit */
        }
    }
if (TSTS (sign) && (qu[0] | qu[1])) {
    MKDNEG (qu);
    }
if (TSTS (AC(ac)) && (dvd[0] | dvd[1])) {
    MKDNEG (dvd);
    }
AC(ac) = qu[0];                                         /* quotient */
AC(ADDAC(ac, 1)) = qu[1];
AC(ADDAC(ac, 2)) = dvd[0];                              /* remainder */
AC(ADDAC(ac, 3)) = dvd[1];
return;
}

/* Single precision floating add/subtract - checked against KS10 ucode
   The KS10 shifts the smaller operand regardless of the exponent diff.
   This code will not shift more than 63 places; shifts beyond that
   cannot change the value of the smaller operand.

   If the signs of the operands are the same, the result sign is the
   same as the source sign; the sign of the result fraction is actually
   part of the data.  If the signs of the operands are different, the
   result sign is determined by the fraction sign.
*/

d10 fad (d10 op1, d10 op2, t_bool rnd, int32 inv)
{
int32 ediff;
UFP a, b, t;

if (inv)                                                /* subtract? -b */
    op2 = NEG (op2);
if (op1 == 0)                                           /* a = 0? result is b */
    funpack (op2, 0, &a, AFRC);
else if (op2 == 0)                                      /* b = 0? result is a */
    funpack (op1, 0, &a, AFRC);
else {
    funpack (op1, 0, &a, SFRC);                         /* unpack operands */
    funpack (op2, 0, &b, SFRC);                         /* fracs are 2's comp */
    ediff = a.exp - b.exp;                              /* get exp diff */
    if (ediff < 0) {                                    /* a < b? switch */
        t = a;
        a = b;
        b = t;
        ediff = -ediff;
        }
    if (ediff > 63)                                     /* cap diff at 63 */
        ediff = 63;
    if (ediff)                                          /* shift b (signed) */
        b.fhi = (t_int64) b.fhi >> ediff;
    a.fhi = a.fhi + b.fhi;                              /* add fractions */
    if (a.sign ^ b.sign) {                              /* add or subtract? */
        if (a.fhi & FP_UCRY) {                          /* subtract, frac -? */
            a.fhi = UNEG (a.fhi);                       /* complement result */
            a.sign = 1;                                 /* result is - */
            }
        else a.sign = 0;                                /* result is + */
        }
    else {
        if (a.sign)                                     /* add, src -? comp */
            a.fhi = UNEG (a.fhi);
        if (a.fhi & FP_UCRY) {                          /* check for carry */
            a.fhi = a.fhi >> 1;                         /* flo won't be used */
            a.exp = a.exp + 1;
            }
        }
    }
fnorm (&a, (rnd? FP_URNDS: 0));                         /* normalize, round */
return fpack (&a, NULL, FALSE);
}

/* Single precision floating multiply.  Because the fractions are 27b,
   a 64b multiply can be used for the fraction multiply.  The 27b
   fractions are positioned 0'frac'0000, resulting in 00'hifrac'0..0.
   The extra 0 is accounted for by biasing the result exponent.
*/

#define FP_V_SPM        (FP_V_UFHI - (32 - FP_N_FHI - 1))
d10 fmp (d10 op1, d10 op2, t_bool rnd)
{
UFP a, b;

funpack (op1, 0, &a, AFRC);                             /* unpack operands */
funpack (op2, 0, &b, AFRC);                             /* fracs are abs val */
if ((a.fhi == 0) || (b.fhi == 0))                       /* either 0?  */
    return 0;
a.sign = a.sign ^ b.sign;                               /* result sign */
a.exp = a.exp + b.exp - FP_BIAS + 1;                    /* result exponent */
a.fhi = (a.fhi >> FP_V_SPM) * (b.fhi >> FP_V_SPM);      /* high 27b of result */
fnorm (&a, (rnd? FP_URNDS: 0));                         /* normalize, round */
return fpack (&a, NULL, FALSE);
}

/* Single precision floating divide.  Because the fractions are 27b, a
   64b divide can be used for the fraction divide.  Note that 28b-29b
   of fraction are developed; the code will do one special normalize to
   make sure that the 28th bit is not lost.  Also note the special
   treatment of negative quotients with non-zero remainders; this
   implements the note on p2-23 of the Processor Reference Manual.
*/

t_bool fdv (d10 op1, d10 op2, d10 *rs, t_bool rnd)
{
UFP a, b;
t_uint64 savhi;
t_bool rem = FALSE;

funpack (op1, 0, &a, AFRC);                             /* unpack operands */
funpack (op2, 0, &b, AFRC);                             /* fracs are abs val */
if (a.fhi >= 2 * b.fhi) {                               /* will divide work? */
    SETF (F_AOV | F_DCK | F_FOV | F_T1);
    return FALSE;
    }
if ((savhi = a.fhi)) {                                  /* dvd = 0? quo = 0 */
    a.sign = a.sign ^ b.sign;                           /* result sign */
    a.exp = a.exp - b.exp + FP_BIAS + 1;                /* result exponent */
    a.fhi = a.fhi / (b.fhi >> (FP_N_FHI + 1));          /* do divide */
    if (a.sign && (savhi != (a.fhi * (b.fhi >> (FP_N_FHI + 1)))))
        rem = TRUE;                                     /* KL/KS hack */
    a.fhi = a.fhi << (FP_V_UNORM - FP_N_FHI - 1);       /* put quo in place */
    if ((a.fhi & FP_UNORM) == 0) {                      /* normalize 1b */
        a.fhi = a.fhi << 1;                             /* before masking */
        a.exp = a.exp - 1;
        }
    a.fhi = a.fhi & (FP_UFHI | FP_URNDS);               /* mask quo to 28b */
    }
fnorm (&a, (rnd? FP_URNDS: 0));                         /* normalize, round */
*rs = fpack (&a, NULL, rem);                            /* pack result */
return TRUE;
}

/* Single precision floating scale. */

d10 fsc (d10 val, a10 ea)
{
int32 sc = LIT8 (ea);
UFP a;

if (val == 0)
    return 0;
funpack (val, 0, &a, AFRC);                             /* unpack operand */
if (ea & RSIGN)                                         /* adjust exponent */
    a.exp = a.exp - sc;
else a.exp = a.exp + sc;
fnorm (&a, 0);                                          /* renormalize */
return fpack (&a, NULL, FALSE);                         /* pack result */
}

/* Float integer operand and round */

d10 fltr (d10 mb)
{
UFP a;
d10 val = ABS (mb);

a.sign = GET_FPSIGN (mb);                               /* get sign */
a.exp = FP_BIAS + 36;                                   /* initial exponent */
a.fhi = val << (FP_V_UNORM - 35);                       /* left justify op */
a.flo = 0;
fnorm (&a, FP_URNDS);                                   /* normalize, round */
return fpack (&a, NULL, FALSE);                         /* pack result */
}

/* Fix and truncate/round floating operand */

void fix (int32 ac, d10 mb, t_bool rnd)
{
int32 sc;
t_uint64 so;
UFP a;

funpack (mb, 0, &a, AFRC);                              /* unpack operand */
if (a.exp > (FP_BIAS + FP_N_FHI + FP_N_EXP))
    SETF (F_AOV | F_T1);
else if (a.exp < FP_BIAS)                               /* < 1/2? */
    AC(ac) = 0;
else {
    sc = FP_V_UNORM - (a.exp - FP_BIAS) + 1;
    AC(ac) = a.fhi >> sc;
    if (rnd) {
        so = a.fhi << (64 - sc);
        if (so >= (0x8000000000000000 + a.sign))
            AC(ac) = AC(ac) + 1;
        }
    if (a.sign)
        AC(ac) = NEG (AC(ac));
    }
return;
}

/* Double precision floating add/subtract
   Since a.flo is 0, adding b.flo is just a copy - this is incorporated into
   the denormalization step.  If there's no denormalization, bflo is zero too.
*/

void dfad (int32 ac, d10 *rs, int32 inv)
{
int32 p1 = ADDAC (ac, 1);
int32 ediff;
UFP a, b, t;

if (inv) {                                              /* subtract? -b */
    DMOVN (rs);
    }
if ((AC(ac) | AC(p1)) == 0)                             /* a == 0? sum = b */
    funpack (rs[0], rs[1], &a, AFRC);
else if ((rs[0] | rs[1]) == 0)                          /* b == 0? sum = a */
    funpack (AC(ac), AC(p1), &a, AFRC);
else {
    funpack (AC(ac), AC(p1), &a, SFRC);                 /* unpack operands */
    funpack (rs[0], rs[1], &b, SFRC);
    ediff = a.exp - b.exp;                              /* get exp diff */
    if (ediff < 0) {                                    /* a < b? switch */
        t = a;
        a = b;
        b = t;
        ediff = -ediff;
        }
    if (ediff > 127)                                    /* cap diff at 127 */
        ediff = 127;
    if (ediff > 63) {                                   /* diff > 63? */
        a.flo = (t_int64) b.fhi >> (ediff - 64);        /* b hi to a lo */
        b.fhi = b.sign? FP_ONES: 0;                     /* hi = all sign */
        }
    else if (ediff) {                                   /* diff <= 63 */
        a.flo = (b.flo >> ediff) | (b.fhi << (64 - ediff));
        b.fhi = (t_int64) b.fhi >> ediff;               /* shift b (signed) */
        }
    a.fhi = a.fhi + b.fhi;                              /* do add */
    if (a.sign ^ b.sign) {                              /* add or subtract? */
        if (a.fhi & FP_UCRY) {                          /* subtract, frac -? */
            DUNEG (a);                                  /* complement result */
            a.sign = 1;                                 /* result is - */
            }
        else a.sign = 0;                                /* result is + */
        }
    else {
        if (a.sign) {                                   /* add, src -? comp */
            DUNEG (a);
            };
        if (a.fhi & FP_UCRY) {                          /* check for carry */
            a.fhi = a.fhi >> 1;                         /* flo won't be used */
            a.exp = a.exp + 1;
            }
        }
    }
fnorm (&a, FP_URNDD);                                   /* normalize, round */
AC(ac) = fpack (&a, &AC(p1), FALSE);                    /* pack result */
return;
}

/* Double precision floating multiply
   The 62b fractions are multiplied, with cross products, to produce a
   124b fraction with two leading and two trailing 0's.  Because the
   product has 2 leading 0's, instead of the normal 1, an extra
   normalization step is needed.  Accordingly, the exponent calculation
   increments the result exponent, to compensate for normalization.
*/

void dfmp (int32 ac, d10 *rs)
{
int32 p1 = ADDAC (ac, 1);
t_uint64 xh, xl, yh, yl, mid;
UFP a, b;

funpack (AC(ac), AC(p1), &a, AFRC);                     /* unpack operands */
funpack (rs[0], rs[1], &b, AFRC);
if ((a.fhi == 0) || (b.fhi == 0)) {                     /* either 0? result 0 */
    AC(ac) = AC(p1) = 0;
    return;
    }
a.sign = a.sign ^ b.sign;                               /* result sign */
a.exp = a.exp + b.exp - FP_BIAS + 1;                    /* result exponent */
xh = a.fhi >> 32;                                       /* split 62b fracs */
xl = a.fhi & MSK32;                                     /* into 32b halves */
yh = b.fhi >> 32;
yl = b.fhi & MSK32;
a.fhi = xh * yh;                                        /* hi xproduct */
a.flo = xl * yl;                                        /* low xproduct */
mid = (xh * yl) + (yh * xl);                            /* fits in 64b */
a.flo = a.flo + (mid << 32);                            /* add mid lo to lo */
a.fhi = a.fhi + ((mid >> 32) & MSK32) + (a.flo < (mid << 32));
fnorm (&a, FP_URNDD);                                   /* normalize, round */
AC(ac) = fpack (&a, &AC(p1), FALSE);                    /* pack result */
return;
}

/* Double precision floating divide
   This algorithm develops a full 62 bits of quotient, plus one rounding
   bit, in the low order 63b of a 64b number.  To do this, we must assure
   that the initial divide step generates a 1.  If it would fail, shift
   the dividend left and decrement the result exponent accordingly.
*/

void dfdv (int32 ac, d10 *rs)
{
int32 p1 = ADDAC (ac, 1);
int32 i;
t_uint64 qu = 0;
UFP a, b;

funpack (AC(ac), AC(p1), &a, AFRC);                     /* unpack operands */
funpack (rs[0], rs[1], &b, AFRC);
if (a.fhi >= 2 * b.fhi) {                               /* will divide work? */
    SETF (F_AOV | F_DCK | F_FOV | F_T1);
    return;
    }
if (a.fhi) {                                            /* dvd = 0? quo = 0 */
    a.sign = a.sign ^ b.sign;                           /* result sign */
    a.exp = a.exp - b.exp + FP_BIAS + 1;                /* result exponent */
    if (a.fhi < b.fhi) {                                /* make sure initial */
        a.fhi = a.fhi << 1;                             /* divide step will work */
        a.exp = a.exp - 1;
        }
    for (i = 0; i < 63; i++) {                          /* 63b of quotient */
        qu = qu << 1;                                   /* shift quotient */
        if (a.fhi >= b.fhi) {                           /* will div work? */
            a.fhi = a.fhi - b.fhi;                      /* sub, quo = 1 */
            qu = qu + 1;
            }
        a.fhi = a.fhi << 1;                             /* shift dividend */
        }
    a.fhi = qu;
    }
fnorm (&a, FP_URNDD);                                   /* normalize, round */
AC(ac) = fpack (&a, &AC(p1), FALSE);                    /* pack result */
return;
}

/* Unpack floating point operand */

void funpack (d10 h, d10 l, UFP *r, t_bool sgn)
{
d10 fphi, fplo;

r->sign = GET_FPSIGN (h);
r->exp = GET_FPEXP (h);
fphi = GET_FPHI (h);
fplo = GET_FPLO (l);
r->fhi = (fphi << FP_V_UFHI) | (fplo << FP_V_UFLO);
r->flo = 0;
if (r->sign) {
    r->exp = r->exp ^ FP_M_EXP;                         /* 1s comp exp */
    if (sgn) {                                          /* signed frac? */
        if (r->fhi)                                     /* extend sign */ 
            r->fhi = r->fhi | FP_UCRY;
        else {
            r->exp = r->exp + 1;
            r->fhi = (t_uint64)(FP_UCRY | FP_UNORM);
            }
        }
    else {                                              /* abs frac */
        if (r->fhi)
            r->fhi = UNEG (r->fhi) & FP_UFRAC;
        else {
            r->exp = r->exp + 1;
            r->fhi = FP_UNORM;
            }
        }
    }
return;
}

/* Normalize and optionally round floating point operand */
 
void fnorm (UFP *a, t_int64 rnd)
{
int32 i;
static t_uint64 normmask[6] = {
 0x6000000000000000, 0x7800000000000000, 0x7F80000000000000,
 0x7FFF800000000000, 0x7FFFFFFF80000000, 0x7FFFFFFFFFFFFFFF
 };
static int32 normtab[7] = { 1, 2, 4, 8, 16, 32, 63 };
extern a10 pager_PC;

if (a->fhi & FP_UCRY) {                                 /* carry set? */
    sim_printf ("%%PDP-10 FP: carry bit set at normalization, PC = %o\n", pager_PC);
    a->flo = (a->flo >> 1) | ((a->fhi & 1) << 63);      /* try to recover */
    a->fhi = a->fhi >> 1;                               /* but root cause */
    a->exp = a->exp + 1;                                /* should be fixed! */
    }
if ((a->fhi | a->flo) == 0) {                           /* if fraction = 0 */
    a->sign = a->exp = 0;                               /* result is 0 */
    return;
    }
while ((a->fhi & FP_UNORM) == 0) {                      /* normalized? */
    for (i = 0; i < 6; i++) {
        if (a->fhi & normmask[i])
            break;
        }
    a->fhi = (a->fhi << normtab[i]) | (a->flo >> (64 - normtab[i]));
    a->flo = a->flo << normtab[i];
    a->exp = a->exp - normtab[i];
    }
if (rnd) {                                              /* rounding? */
    a->fhi = a->fhi + rnd;                              /* add round const */
    if (a->fhi & FP_UCRY) {                             /* if carry out, */
        a->fhi = a->fhi >> 1;                           /* renormalize */
        a->exp = a->exp + 1;
        }
    }
return;
}

/* Pack floating point result */

d10 fpack (UFP *r, d10 *lo, t_bool fdvneg)
{
d10 val[2];

if (r->exp < 0)
    SETF (F_AOV | F_FOV | F_FXU | F_T1);
else if (r->exp > FP_M_EXP)
    SETF (F_AOV | F_FOV | F_T1);
val[0] = (((((d10) r->exp) & FP_M_EXP) << FP_V_EXP) |
    ((r->fhi & FP_UFHI) >> FP_V_UFHI)) & DMASK;
if (lo)
    val[1] = ((r->fhi & FP_UFLO) >> FP_V_UFLO) & MMASK;
else val[1] = 0;
if (r->sign) {                                          /* negate? */
    if (fdvneg) {                                       /* fdvr special? */
        val[1] = ~val[1] & MMASK;                       /* 1's comp */
        val[0] = ~val[0] & DMASK;
        }
    else {                                              /* 2's comp */
        DMOVN (val);
        }
    }
if (lo)
    *lo = val[1];
return val[0];
}
