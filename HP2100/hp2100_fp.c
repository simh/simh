/* hp2100_fp.c: HP 2100 floating point instructions

   Copyright (c) 2002, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   The HP2100 uses a unique binary floating point format:

     15 14                                         0
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |S |               fraction high                | : A
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |     fraction low      |      exponent      |XS| : A + 1
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
     15                    8  7                 1  0
 
  	where	S = 0 for plus fraction, 1 for minus fraction
		fraction = s.bbbbb..., 24 binary digits
		exponent = 2**+/-n
		XS = 0 for plus exponent, 1 for minus exponent

   Numbers can be normalized or unnormalized but are always normalized
   when loaded.

   Unpacked floating point numbers are stored in structure ufp

	sign	=	fraction sign, 0 = +, 1 = -
	exp	=	exponent, 2's complement
	h'l	=	fraction, 2's comp, with 1 high guard bit

   Questions:
   1. Are fraction and exponent magnitude or 2's complement? 2's complement
   2. Do operations round? yes, with IEEE like standards (sticky bits)
*/

#include "hp2100_defs.h"

struct ufp {						/* unpacked fp */
	int32	sign;					/* sign */
	int32	exp;					/* exp */
	uint32	h;					/* frac */
	uint32	l;  };

#define FP_V_SIGN	31				/* sign */
#define FP_M_SIGN	01
#define FP_GETSIGN(x)	(((x) >> FP_V_SIGN) & FP_M_SIGN)
#define FP_V_FRH	8				/* fraction */
#define FP_M_FRH	077777777
#define FP_FRH		(FP_M_FRH << FP_V_FRH)
#define FP_V_EXP	1				/* exponent */
#define FP_M_EXP	0177
#define FP_GETEXP(x)	(((x) >> FP_V_EXP) & FP_M_EXP)
#define FP_V_EXPS	0				/* exp sign */
#define FP_M_EXPS	01
#define FP_GETEXPS(x)	(((x) >> FP_V_EXPS) & FP_M_EXPS)

#define UFP_GUARD	1				/* 1 extra left */
#define UFP_V_SIGN	(FP_V_SIGN - UFP_GUARD)		/* sign */
#define UFP_SIGN	(1 << UFP_V_SIGN)
#define UFP_CRY		(1 << (UFP_V_SIGN + 1))		/* carry */
#define UFP_NORM	(1 << (UFP_V_SIGN - 1))		/* normalized */
#define UFP_V_LOW	(FP_V_FRH - UFP_GUARD)		/* low bit */
#define UFP_LOW		(1 << UFP_V_LOW)
#define UFP_RND		(1 << (UFP_V_LOW - 1))		/* round */
#define UFP_STKY	(UFP_RND - 1)			/* sticky bits */

#define FPAB		((((uint32) AR) << 16) | ((uint32) BR))

#define HFMASK		0x7FFFFFFF			/* hi frac mask */
#define LFMASK		0xFFFFFFFF			/* lo frac mask */

/* Fraction shift; 0 < shift < 32 */

#define FR_ARSH(v,s)	v.l = ((v.l >> (s)) | \
				(v.h << (32 - (s)))) & LFMASK; \
			v.h = ((v.h >> (s)) | ((v.h & UFP_SIGN)? \
				(LFMASK << (32 - (s))): 0)) & HFMASK

#define FR_LRSH(v,s)	v.l = ((v.l >> (s)) | \
				(v.h << (32 - (s)))) & LFMASK; \
			v.h = (v.h >> (s)) & HFMASK

#define FR_NEG(v)	v.l = (~v.l + 1) & LFMASK; \
			v.h = (~v.h + (v.l == 0)) & HFMASK

#define FR_NEGH(v)	v = (~v + 1) & HFMASK

extern uint16 *M;
void UnpackFP (struct ufp *fop, uint32 opnd, t_bool abs);
void NormFP (struct ufp *fop);
int32 StoreFP (struct ufp *fop, t_bool rnd);

/* Floating to integer conversion */

int32 f_fix (void)
{
struct ufp res;

UnpackFP (&res, FPAB, 0);				/* unpack A-B, norm */
if ((res.h == 0) || (res.exp <= 0)) {			/* result zero? */
	AR = 0;
	return 0;  }
if (res.exp > 15) {
	AR = 077777;
	return 1;  }
FR_ARSH (res, (30 - res.exp));				/* right align frac */
if (res.sign && res.l) res.h = res.h + 1;		/* round? */
AR = res.h & DMASK;					/* store result */
return 0;
}

/* Integer to floating conversion */

void f_flt (void)
{
struct ufp res = { 0, 15, 0, 0 };			/* +, 2**15 */

res.h = ((uint32) AR) << 15;				/* left justify */
if (res.h & UFP_SIGN) res.sign = 1;			/* set sign */
NormFP (&res);						/* normalize */
StoreFP (&res, 0);					/* store result */
return;
}

/* Floating point add/subtract */

int32 f_as (uint32 opnd, t_bool sub)
{
struct ufp fop1, fop2, t;
int32 ediff;

UnpackFP (&fop1, FPAB, 0);				/* unpack A-B, norm */
UnpackFP (&fop2, opnd, 0);				/* get op, norm */
if (sub) {						/* subtract? */
    fop2.sign = fop2.sign ^ 1;				/* negate sign */
    fop2.h = FR_NEGH (fop2.h);				/* negate frac */
    if (fop2.h & UFP_SIGN) {				/* -1/2? */
	fop2.h = UFP_NORM;				/* special case */
	fop2.exp = fop2.exp + 1;  }  }
if (fop1.h == 0) fop1 = fop2;				/* op1 = 0? res = op2 */
else if (fop2.h != 0) {					/* op2 = 0? no add */
    if (fop1.exp < fop2.exp) {				/* |op1| < |op2|? */
	t = fop2;					/* swap operands */
	fop2 = fop1;
	fop1 = t;  }
    ediff = fop1.exp - fop2.exp;			/* get exp diff */
    if (ediff <= 24) {
	if (ediff) {  FR_ARSH (fop2, ediff);  }		/* denorm, signed */
	fop1.h = fop1.h + fop2.h;			/* add fractions */
	if (fop1.sign ^ fop2.sign) {			/* eff subtract */
	    if (fop1.h & UFP_SIGN) fop1.sign = 1;	/* result neg? */
	    else fop1.sign = 0;
	    NormFP (&fop1);  }				/* normalize result */
        else if (fop1.h & (fop1.sign? UFP_CRY: UFP_SIGN)) {	/* add, cry out? */
	    fop1.h = fop1.h >> 1;			/* renormalize */
	    fop1.exp = fop1.exp + 1;  }			/* incr exp */
	}						/* end if ediff */
    }							/* end if fop2 */
return StoreFP (&fop1, 1);				/* store result */
}

/* Floating point multiply */

int32 f_mul (uint32 opnd)
{
struct ufp fop1, fop2;
struct ufp res = { 0, 0, 0, 0 };
int32 i;

UnpackFP (&fop1, FPAB, 1);				/* unpack |A-B|, norm */
UnpackFP (&fop2, opnd, 1);				/* unpack |op|, norm */
if (fop1.h && fop2.h) {					/* if both != 0 */
    res.sign = fop1.sign ^ fop2.sign;			/* sign = diff */
    res.exp = fop1.exp + fop2.exp;			/* exp = sum */
	for (i = 0; i < 24; i++) {			/* 24 iterations */
	    if (fop2.h & UFP_LOW)			/* mplr bit set? */
		 res.h = res.h + fop1.h;		/* add mpcn to res */
	    fop2.h = fop2.h >> 1;			/* shift mplr */
	    FR_LRSH (res, 1);  }			/* shift res */
    if (res.sign) FR_NEG (res);				/* correct sign */
    NormFP (&res);					/* normalize */
    }
return StoreFP (&res, 1);				/* store */
}

/* Floating point divide */

int32 f_div (uint32 opnd)
{
struct ufp fop1, fop2;
struct ufp quo = { 0, 0, 0, 0 };
int32 i;

UnpackFP (&fop1, FPAB, 1);				/* unpack |A-B|, norm */
UnpackFP (&fop2, opnd, 1);				/* unpack |op|, norm */
if (fop2.h == 0) return 1;				/* div by zero? */
if (fop1.h) {						/* dvd != 0? */
    quo.sign = fop1.sign ^ fop2.sign;			/* sign = diff */
    quo.exp = fop1.exp - fop2.exp;			/* exp = diff */
    if (fop1.h < fop2.h) {				/* will sub work? */
	fop1.h = fop1.h << 1;				/* ensure success */
	quo.exp = quo.exp - 1;  }
    for (i = 0; i < 24; i++) {				/* 24 digits */
	quo.h = quo.h << 1;				/* shift quotient */
	if (fop1.h >= fop2.h) {				/* subtract work? */
	    fop1.h = fop1.h - fop2.h;			/* decrement */
	    quo.h = quo.h + UFP_RND;  }			/* add quo bit */
	fop1.h = fop1.h << 1;  }			/* shift divd */
    }							/* end if fop1.h */
if (quo.sign) quo.h = FR_NEGH (quo.h);			/* correct sign */
NormFP (&quo);						/* negate */
return StoreFP (&quo, 1);				/* store result */
}

/* Utility routines */

/* Unpack operand */

void UnpackFP (struct ufp *fop, uint32 opnd, t_bool abs)
{
fop -> h = (opnd & FP_FRH) >> UFP_GUARD;		/* get frac, guard */
if (fop -> h) {						/* non-zero? */
    fop -> sign = FP_GETSIGN (opnd);			/* get sign */
    fop -> exp = FP_GETEXP (opnd);			/* get exp */
    if (FP_GETEXPS (opnd))				/* get exp sign */
	fop -> exp = fop -> exp | ~FP_M_EXP;		/* if -, sext */
    if (abs && fop -> sign) {				/* want abs val? */
	fop -> h = FR_NEGH (fop -> h);			/* negate frac*/
	if (fop -> h == UFP_SIGN) {			/* -1/2? */
	    fop -> h = fop -> h >> 1;			/* special case */
	    fop -> exp = fop -> exp + 1;  }  }
    NormFP (fop);  }					/* normalize */
else fop -> sign = fop -> exp = 0;			/* clean zero */
fop -> l = 0;
return;
}

/* Normalize unpacked floating point number */

void NormFP (struct ufp *fop)
{
if (fop -> h | fop -> l) {				/* any fraction? */
    uint32 test = (fop -> h >> 1) & UFP_NORM;
    while ((fop -> h & UFP_NORM) == test) {		/* until norm */
	fop -> exp = fop -> exp - 1;
	fop -> h = (fop -> h << 1) | (fop -> l >> 31);
	fop -> l = fop -> l << 1;  }  }
else fop -> sign = fop -> exp = 0;			/* clean 0 */
return;
}

/* Round fp number, store, generate overflow */

int32 StoreFP (struct ufp *fop, t_bool rnd)
{
int32 hi, ov;

if (rnd && (fop -> h & UFP_RND) &&
   ((fop -> sign == 0) || (fop -> h & UFP_STKY) || fop -> l)) {
    fop -> h = fop -> h + UFP_RND;			/* round */
    if (fop -> h & ((fop -> sign)? UFP_CRY: UFP_SIGN)) {
	fop -> h = fop -> h >> 1;
	fop -> exp = fop -> exp + 1;  }  }
if (fop -> h == 0) hi = ov = 0;				/* result 0? */
else if (fop -> exp < -(FP_M_EXP + 1)) {		/* underflow? */
    hi = 0;						/* store clean 0 */
    ov = 1;  }
else if (fop -> exp > FP_M_EXP) {			/* overflow? */
    hi = 0x7FFFFFFE;					/* all 1's */
    ov = 1;  }
else {	hi = ((fop -> h << UFP_GUARD) & FP_FRH) |	/* merge frac */
	     ((fop -> exp & FP_M_EXP) << FP_V_EXP);	/* and exp */
	if (fop -> exp < 0) hi = hi | (1 << FP_V_EXPS);  }	/* add exp sign */
AR = (hi >> 16) & DMASK;
BR = hi & DMASK;
return ov;
}
