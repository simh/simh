/* id_fp.c: Interdata floating point instructions

   Copyright (c) 2000-2008, Robert M. Supnik

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

   The Interdata uses IBM 360 floating point format:

     0             7 8             15              23              31
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |S|   exponent  |                  fraction                     | :single
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                           fraction low                        | :double
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   where        S = 0 for plus, 1 for minus
                exponent = 16**n, in excess 64 code
                fraction = .hhhhhh, seen as 6-14 hexadecimal digits

   Numbers can be normalized or unnormalized but are always normalized
   when loaded.

   Interdata has 8 floating point registers, F0, F2, ... , FE.  In floating
   point instructions, the low order bit of the register number is ignored. 

   On floating point overflow, the exponent and fraction are set to 1's.
   On floating point underflow, the exponent and fraction are set to 0's.

   Interdata has both 32b only and 32b/64b floating point implementations.
   In 32b only implementations, add and subtract are truncated, but multiply
   and divide are rounded, and the floating point registers are kept in
   memory.  In 64b implementations, all single precision precision operations
   are rounded, double precision operations are truncated, and the floating
   point registers are kept in separate hardware arrays.
*/

#include "id_defs.h"

struct ufp {                                            /* unpacked fp */
    int32       sign;                                   /* sign */
    int32       exp;                                    /* unbiased exp */
    uint32      h;                                      /* fr high */
    uint32      l;                                      /* fr low */
    };

#define FP_V_SIGN       31                              /* sign */
#define FP_M_SIGN       0x1
#define FP_GETSIGN(x)   (((x) >> FP_V_SIGN) & FP_M_SIGN)
#define FP_V_EXP        24                              /* exponent */
#define FP_M_EXP        0x7F
#define FP_GETEXP(x)    (((x) >> FP_V_EXP) & FP_M_EXP)
#define FP_V_FRH        0                               /* fraction */
#define FP_M_FRH        0xFFFFFF
#define FP_GETFRH(x)    (((x) >> FP_V_FRH) & FP_M_FRH)
#define FP_GETFRL(x)    (x)

#define FP_BIAS         0x40                            /* exp bias */
#define FP_CARRY        (1 << FP_V_EXP )                /* carry out */
#define FP_NORM         (0xF << (FP_V_EXP - 4))         /* normalized */
#define FP_ROUND        0x80000000

/* Double precision fraction add/subtract/compare */

#define FR_ADD(d,s)     d.l = (d.l + s.l) & DMASK32; \
                        d.h = (d.h + s.h + (d.l < s.l)) & DMASK32           

#define FR_SUB(d,s)     d.h = (d.h - s.h - (d.l < s.l)) & DMASK32; \
                        d.l = (d.l - s.l) & DMASK32

#define FR_GE(s1,s2)    ((s1.h > s2.h) || \
                        ((s1.h == s2.h) && (s1.l >= s2.l)))

/* Variable and constant shifts; for constants, 0 < k < 32 */

#define FR_RSH_V(v,s)   if ((s) < 32) { \
                            v.l = ((v.l >> (s)) | \
                                (v.h << (32 - (s)))) & DMASK32; \
                            v.h = (v.h >> (s)) & DMASK32; \
                            } \
                        else { \
                            v.l = v.h >> ((s) - 32); \
                            v.h = 0; \
                            }

#define FR_RSH_K(v,s)   v.l = ((v.l >> (s)) | \
                            (v.h << (32 - (s)))) & DMASK32; \
                        v.h = (v.h >> (s)) & DMASK32

#define FR_LSH_K(v,s)   v.h = ((v.h << (s)) | \
                            (v.l >> (32 - (s)))) & DMASK32; \
                        v.l = (v.l << (s)) & DMASK32

#define Q_RND(op)       (OP_DPFP (op) == 0)
#define Q_RND_AS(op)    ((OP_DPFP (op) == 0) && fp_in_hwre)

extern uint32 *R;
extern uint32 F[8];
extern dpr_t D[8];
extern uint32 fp_in_hwre;
extern uint32 ReadF (uint32 loc, uint32 rel);
extern void WriteF (uint32 loc, uint32 dat, uint32 rel);
void ReadFP2 (struct ufp *fop, uint32 op, uint32 r2, uint32 ea);
void UnpackFPR (struct ufp *fop, uint32 op, uint32 r1);
void NormUFP (struct ufp *fop);
uint32 StoreFPR (struct ufp *fop, uint32 op, uint32 r1, uint32 rnd);
uint32 StoreFPX (struct ufp *fop, uint32 op, uint32 r1);

/* Floating point load */

uint32 f_l (uint32 op, uint32 r1, uint32 r2, uint32 ea)
{
struct ufp fop2;

ReadFP2 (&fop2, op, r2, ea);                            /* get op, normalize */
return StoreFPR (&fop2, op, r1, 0);                     /* store, chk unflo */
}

/* Floating point compare */

uint32 f_c (uint32 op, uint32 r1, uint32 r2, uint32 ea)
{
struct ufp fop1, fop2;

ReadFP2 (&fop2, op, r2, ea);                            /* get op2, norm */
UnpackFPR (&fop1, op, r1);                              /* get op1, norm */
if (fop1.sign ^ fop2.sign)                              /* signs differ? */
    return (fop2.sign? CC_G: (CC_C | CC_L));
if (fop1.exp != fop2.exp)                               /* exps differ? */
    return (((fop1.exp > fop2.exp) ^ fop1.sign)? CC_G: (CC_C | CC_L));
if (fop1.h != fop2.h)                                   /* hi fracs differ? */
    return (((fop1.h > fop2.h) ^ fop1.sign)? CC_G: (CC_C | CC_L));
if (OP_DPFP (op) && (fop1.l != fop2.l))                 /* dp: low fracs diff? */
    return (((fop1.l > fop2.l) ^ fop1.sign)? CC_G: (CC_C | CC_L));
return 0;
}

/* Floating to integer conversion */

uint32 f_fix (uint32 op, uint32 r1, uint32 r2)          /* 16b */
{
struct ufp res;
uint32 cc;

UnpackFPR (&res, op, r2);                               /* get op2, norm */
if ((res.h == 0) || (res.exp < 0x41)) {                 /* result zero? */
    R[r1] = 0;
    return 0;
    }
if ((res.exp > 0x44) ||                                 /* result too big? */
    ((res.exp == 0x44) && (res.h >= 0x00800000))) {
    res.h = MMASK16;
    cc = CC_V;
    }
else {
    res.h = res.h >> ((0x46 - res.exp) * 4);            /* right align frac */
    cc = 0;
    }
if (res.sign) {
    R[r1] = ((res.h ^ DMASK16) + 1) & DMASK16;          /* negate result */
    return cc | CC_L;
    }
R[r1] = res.h & DMASK16;
return cc | CC_G;
}

uint32 f_fix32 (uint32 op, uint32 r1, uint32 r2)        /* 32b */
{
struct ufp res;
uint32 cc;

UnpackFPR (&res, op, r2);                               /* get op2, norm */
if ((res.h == 0) || (res.exp < 0x41)) {                 /* result zero? */
    R[r1] = 0;
    return 0;
    }
if ((res.exp > 0x48) ||                                 /* result too big? */
    ((res.exp == 0x48) && (res.h >= 0x00800000))) {
    res.h = MMASK32;
    cc = CC_V;
    }
else {
    FR_LSH_K (res, 8);                                  /* get all in 32b */
    res.h = res.h >> ((0x48 - res.exp) * 4);            /* right align frac */
    cc = 0;
    }
if (res.sign) {
    R[r1] = (res.h ^ DMASK32) + 1;                      /* negate result */
    return cc | CC_L;
    }
R[r1] = res.h;
return cc | CC_G;
}

/* Integer to floating conversion */

uint32 f_flt (uint32 op, uint32 r1, uint32 r2)          /* 16b */
{
struct ufp res = { 0, 0x44, 0, 0 };                     /* +, 16**4 */
uint32 cc;

if (R[r2] == 0)                                         /* zero arg? */
    cc = 0;
else if (R[r2] & SIGN16) {                              /* neg arg? */
    res.sign = FP_M_SIGN;                               /* set sign */
    res.h = ((~R[r2] + 1) & DMASK16) << 8;              /* get magnitude */
    cc = CC_L;
    }
else {
    res.h = R[r2] << 8;                                 /* pos nz arg */
    cc = CC_G;
    }
NormUFP (&res);                                         /* normalize */
StoreFPR (&res, op, r1, 0);                             /* store result */
return cc;
}

uint32 f_flt32 (uint32 op, uint32 r1, uint32 r2)        /* 32b */
{
struct ufp res = { 0, 0x48, 0, 0 };                     /* +, 16**8 */
uint32 cc, t;

t = R[r2];                                              /* int op */
if (t) {                                                /* nonzero arg? */
    if (t & SIGN32) {                                   /* neg arg? */
        res.sign = FP_M_SIGN;                           /* set sign */
        t = (~t + 1) & DMASK32;                         /* get magnitude */
        cc = CC_L;
        }
    else cc = CC_G;                                     /* pos nz arg */
    res.h = t >> 8;                                     /* hi frac */
    res.l = t << 24;                                    /* lo frac */
    }
else cc = 0;                                            /* zero arg */
NormUFP (&res);                                         /* normalize */
StoreFPR (&res, op, r1, 0);                             /* store result */
return cc;
}

/* Floating point add/subtract */

uint32 f_as (uint32 op, uint32 r1, uint32 r2, uint32 ea)
{
struct ufp fop1, fop2, t;
int32 ediff;

ReadFP2 (&fop2, op, r2, ea);                            /* get op2, norm */
UnpackFPR (&fop1, op, r1);                              /* get op1, norm */
if (op & 1)                                             /* if sub, inv sign2 */
    fop2.sign = fop2.sign ^ 1;
if (fop1.h == 0)                                        /* if op1 = 0, res = op2 */
    fop1 = fop2;
else if (fop2.h != 0) {                                 /* if op2 = 0, no add */
    if ((fop1.exp < fop2.exp) ||                        /* |op1| < |op2|? */
        ((fop1.exp == fop2.exp) &&
        ((fop1.h < fop2.h) ||
        ((fop1.h == fop2.h) && (fop1.l < fop2.l))))) {
        t = fop2;                                       /* swap operands */
        fop2 = fop1;
        fop1 = t;
        }
    ediff = fop1.exp - fop2.exp;                        /* exp difference */
    if (OP_DPFP (op) || fp_in_hwre) {                   /* dbl prec or hwre? */
        if (ediff >= 14)                                /* diff too big? */
            fop2.h = fop2.l = 0;
        else if (ediff) {                               /* any difference? */
            FR_RSH_V (fop2, ediff * 4);                 /* shift frac */
            }
        }
    else {                                              /* sgl prec ucode */
        if (ediff >= 6)                                 /* diff too big? */
            fop2.h = 0;
        else if (ediff)                                 /* any difference? */
            fop2.h = fop2.h >> (ediff * 4);             /* shift frac */
        }
    if (fop1.sign ^ fop2.sign) {                        /* eff subtract */
        FR_SUB (fop1, fop2);                            /* sub fractions */
        NormUFP (&fop1);                                /* normalize result */
        }
    else {
        FR_ADD (fop1, fop2);                            /* add fractions */
        if (fop1.h & FP_CARRY) {                        /* carry out? */
            FR_RSH_K (fop1, 4);                         /* renormalize */
            fop1.exp = fop1.exp + 1;                    /* incr exp */
            }
        }
    }                                                   /* end if fop2 */
return StoreFPR (&fop1, op, r1, Q_RND_AS (op));         /* store result */
}

/* Floating point multiply

   Notes:
   - Exponent overflow/underflow is tested right after the exponent
     add, without regard to potential changes due to normalization
   - Exponent underflow is tested right after normalization, without
     regard to changes due to rounding
   - Single precision hardware multiply may generate up to 48b
   - Double precision multiply generates 56b with no guard bits
*/

uint32 f_m (uint32 op, uint32 r1, uint32 r2, uint32 ea)
{
struct ufp fop1, fop2;
struct ufp res = { 0, 0, 0, 0 };
uint32 i;

ReadFP2 (&fop2, op, r2, ea);                            /* get op2, norm */
UnpackFPR (&fop1, op, r1);                              /* get op1, norm */
if (fop1.h && fop2.h) {                                 /* if both != 0 */
    res.sign = fop1.sign ^ fop2.sign;                   /* sign = diff */
    res.exp = fop1.exp + fop2.exp - FP_BIAS;            /* exp = sum */
    if ((res.exp < 0) || (res.exp > FP_M_EXP))          /* ovf/undf? */
        return StoreFPX (&res, op, r1);                 /* early out */
    if ((fop1.l | fop2.l) == 0) {                       /* 24b x 24b? */
        for (i = 0; i < 24; i++) {                      /* 24 iterations */
            if (fop2.h & 1)                             /* add hi only */
                res.h = res.h + fop1.h;
            FR_RSH_K (res, 1);                          /* shift dp res */
            fop2.h = fop2.h >> 1;
            }
        }
    else {                                              /* some low 0's */
        if (fop2.l != 0) {                              /* 56b x 56b? */
            for (i = 0; i < 32; i++) {                  /* do low 32b */
                if (fop2.l & 1) {
                    FR_ADD (res, fop1);
                    }
                FR_RSH_K (res, 1);
                fop2.l = fop2.l >> 1;
                }
            }
        for (i = 0; i < 24; i++) {                      /* do hi 24b */
            if (fop2.h & 1) {
                FR_ADD (res, fop1);
                }
            FR_RSH_K (res, 1);
            fop2.h = fop2.h >> 1;
            }
        }
    NormUFP (&res);                                     /* normalize */
    if (res.exp < 0)                                    /* underflow? */
        return StoreFPX (&res, op, r1);                 /* early out */
    }
return StoreFPR (&res, op, r1, Q_RND (op));             /* store */
}

/* Floating point divide - see overflow/underflow notes for multiply */

uint32 f_d (uint32 op, uint32 r1, uint32 r2, uint32 ea)
{
struct ufp fop1, fop2;
struct ufp quo = { 0, 0, 0, 0 };
int32 i;

ReadFP2 (&fop2, op, r2, ea);                            /* get op2, norm */
UnpackFPR (&fop1, op, r1);                              /* get op1, norm */
if (fop2.h == 0)                                        /* div by zero? */
    return CC_C | CC_V;
if (fop1.h) {                                           /* dvd != 0? */
    quo.sign = fop1.sign ^ fop2.sign;                   /* sign = diff */
    quo.exp = fop1.exp - fop2.exp + FP_BIAS;            /* exp = diff */
    if ((quo.exp < 0) || (quo.exp > FP_M_EXP))          /* ovf/undf? */
        return StoreFPX (&quo, op, r1);                 /* early out */
    if (!FR_GE (fop1, fop2)) {
        FR_LSH_K (fop1, 4);                             /* ensure success */
        }
    else {                                              /* exp off by 1 */
        quo.exp = quo.exp + 1;                          /* incr exponent */
        if (quo.exp > FP_M_EXP)                         /* overflow? */
            return StoreFPX (&quo, op, r1);             /* early out */
        }
    for (i = 0; i < (OP_DPFP (op)? 14: 6); i++) {       /* 6/14 hex digits */
        FR_LSH_K (quo, 4);                              /* shift quotient */
        while (FR_GE (fop1, fop2)) {                    /* while sub works */
            FR_SUB (fop1, fop2);                        /* decrement */
            quo.l = quo.l + 1;                          /* add quo bit */
            }
        FR_LSH_K (fop1, 4);                             /* shift divd */
        }
    if (!OP_DPFP (op)) {                                /* single? */
        quo.h = quo.l;                                  /* move quotient */
        if (fop1.h >= (fop2.h << 3))
            quo.l = FP_ROUND;
        else quo.l = 0;
        }                                               /* don't need to normalize */
    }                                                   /* end if fop1.h */
return StoreFPR (&quo, op, r1, Q_RND (op));             /* store result */
}

/* Utility routines */

/* Unpack floating point number */

void UnpackFPR (struct ufp *fop, uint32 op, uint32 r1)
{
uint32 hi;

if (OP_DPFP (op)) {                                     /* double prec? */
    hi = D[r1 >> 1].h;                                  /* get hi */
    fop->l = FP_GETFRL (D[r1 >> 1].l);                  /* get lo */
    }
else {
    hi = ReadFReg (r1);                                 /* single prec */
    fop->l = 0;                                         /* lo is zero */
    }
fop->h = FP_GETFRH (hi);                                /* get hi frac */
if (fop->h || fop->l) {                                 /* non-zero? */
    fop->sign = FP_GETSIGN (hi);                        /* get sign */
    fop->exp = FP_GETEXP (hi);                          /* get exp */
    NormUFP (fop);                                      /* normalize */
    }
else fop->sign = fop->exp = 0;                          /* clean zero */
return;
}

/* Read memory operand */

void ReadFP2 (struct ufp *fop, uint32 op, uint32 r2, uint32 ea)
{
uint32 hi;

if (OP_TYPE (op) > OP_RR) {                             /* mem ref? */
    hi = ReadF (ea, VR);                                /* get hi */
    if (OP_DPFP (op))                                   /* dp? get lo */
        fop->l = ReadF (ea + 4, VR);
    else fop->l = 0;                                    /* sp, lo = 0 */
    }
else {
    if (OP_DPFP (op)) {                                 /* RR */
        hi = D[r2 >> 1].h;                              /* dp? get dp reg */
        fop->l = D[r2 >> 1].l;
        }
    else {
        hi = ReadFReg (r2);                              /* get sp reg */
        fop->l = 0;
        }
    }
fop->h = FP_GETFRH (hi);                                /* get hi frac */
if (fop->h || fop->l) {                                 /* non-zero? */
    fop->sign = FP_GETSIGN (hi);                        /* get sign */
    fop->exp = FP_GETEXP (hi);                          /* get exp */
    NormUFP (fop);                                      /* normalize */
    }
else fop->sign = fop->exp = 0;                          /* clean zero */
return;
}

/* Normalize unpacked floating point number */

void NormUFP (struct ufp *fop)
{
if ((fop->h & FP_M_FRH) || fop->l) {                    /* any fraction? */
    while ((fop->h & FP_NORM) == 0) {                   /* until norm */
        fop->h = (fop->h << 4) | ((fop->l >> 28) & 0xF);
        fop->l = fop->l << 4;
        fop->exp = fop->exp - 1;
        }
    }
else fop->sign = fop->exp = 0;                          /* clean 0 */
return;
}

/* Round fp number, store, generate condition codes */

uint32 StoreFPR (struct ufp *fop, uint32 op, uint32 r1, uint32 rnd)
{
uint32 hi, cc;

if (rnd && (fop->l & FP_ROUND)) {                       /* round? */
    fop->h = fop->h + 1;                                /* add 1 to frac */
    if (fop->h & FP_CARRY) {                            /* carry out? */
        fop->h = fop->h >> 4;                           /* renormalize */
        fop->exp = fop->exp + 1;                        /* incr exp */
        }
    }
if (fop->h == 0) {                                      /* result 0? */
    hi = fop->l = 0;                                    /* store clean 0 */
    cc = 0;
    }
else if (fop->exp < 0) {                                /* underflow? */
    hi = fop->l = 0;                                    /* store clean 0 */
    cc = CC_V;
    }
else if (fop->exp > FP_M_EXP) {                         /* overflow? */
    hi = (fop->sign)? 0xFFFFFFFF: 0x7FFFFFFF;
    fop->l = 0xFFFFFFFF;
    cc = (CC_V | ((fop->sign)? CC_L: CC_G));
    }
else {
    hi = ((fop->sign & FP_M_SIGN) << FP_V_SIGN) |       /* pack result */
        ((fop->exp & FP_M_EXP) << FP_V_EXP) |
        ((fop->h & FP_M_FRH) << FP_V_FRH);
    cc = (fop->sign)? CC_L: CC_G;                       /* set cc's */
    }
if (OP_DPFP (op)) {                                     /* double precision? */
    D[r1 >> 1].h = hi;
    D[r1 >> 1].l = fop->l;
    }
else {
    WriteFReg (r1, hi);
    }
return cc;
}

/* Generate exception result */

uint32 StoreFPX (struct ufp *fop, uint32 op, uint32 r1)
{
uint32 cc = CC_V;

if (fop->exp < 0)                                       /* undf? clean 0 */
    fop->h = fop->l = 0;
else {
    fop->h = (fop->sign)? 0xFFFFFFFF: 0x7FFFFFFF;       /* overflow */
    fop->l = 0xFFFFFFFF;
    cc = cc | ((fop->sign)? CC_L: CC_G);
    }
if (OP_DPFP (op)) {                                     /* double precision? */
    D[r1 >> 1].h = fop->h;
    D[r1 >> 1].l = fop->l;
    }
else {
    WriteFReg (r1, fop->h);
    }
return cc;
}
