/* id4_fp.c: Interdata 4 floating point instructions

   Copyright (c) 1993-2001, Robert M. Supnik

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

   The Interdata 4 uses IBM 360 single precision floating point format:

	 0             7 8             15              23              31
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|S|   exponent  |                  fraction                     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   where	S = 0 for plus, 1 for minus
		exponent = 16**n, in excess 64 code
		fraction = .hhhhhh, seen as 6 hexadecimal digits

   Numbers could be normalized or unnormalized but are always normalized
   when loaded.

   The Interdata 4 has 8 floating point registers, F0, F2, ... , FE.
   In floating point instructions, the low order bit of the register 
   specification is ignored.

   On floating point overflow, the exponent and fraction are set to 1's.
   On floating point underflow, the exponent and fraction are set to 0's.
*/

#include "id4_defs.h"

struct ufp {						/* unpacked fp */
	int32 sign;					/* sign */
	int32 exp;					/* unbiased exp */
	unsigned int32 frh;				/* fr high */
	unsigned int32 frl;  };				/* fr low */

#define FP_V_SIGN	31				/* sign */
#define FP_M_SIGN	0x1
#define FP_GETSIGN(x)	(((x) >> FP_V_SIGN) & FP_M_SIGN)
#define FP_V_EXP	24				/* exponent */
#define FP_M_EXP	0x7F
#define FP_GETEXP(x)	(((x) >> FP_V_EXP) & FP_M_EXP)
#define FP_V_FRH	0				/* fraction */
#define FP_M_FRH	0xFFFFFF
#define FP_GETFRH(x)	(((x) >> FP_V_FRH) & FP_M_FRH)

#define FP_BIAS		0x40				/* exp bias */
#define FP_CARRY	(1 << FP_V_EXP )		/* carry out */
#define FP_NORM		(0xF << (FP_V_EXP - 4))		/* normalized */
#define FP_ROUND	0x80000000
#define FP_DMASK	0xFFFFFFFF

/* Variable and constant shifts; for constants, 0 < k < 32 */

#define FP_SHFR_V(v,s)	if ((s) < 32) { \
				v.frl = ((v.frl >> (s)) | \
					(v.frh << (32 - (s)))) & FP_DMASK; \
				v.frh = (v.frh >> (s)) & FP_DMASK; } \
			else {	v.frl = v.frh >> ((s) - 32); \
				v.frh = 0; }
#define FP_SHFR_K(v,s)	v.frl = ((v.frl >> (s)) | \
				(v.frh << (32 - (s)))) & FP_DMASK; \
			v.frh = (v.frh >> (s)) & FP_DMASK

extern int32 R[16];
extern uint32 F[8];
extern uint16 M[];
void ReadFP2 (struct ufp *fop, int32 op, int32 r2, int32 ea);
void UnpackFP (struct ufp *fop, unsigned int32 val);
void NormFP (struct ufp *fop);
int32 StoreFP (struct ufp *fop, int32 r1);

/* Floating point load */

int32 le (op, r1, r2, ea)
{
struct ufp fop2;

ReadFP2 (&fop2, op, r2, ea);				/* get op, normalize */
return StoreFP (&fop2, r1);				/* store, chk unflo */
}

/* Floating point compare */

int32 ce (op, r1, r2, ea)
{
struct ufp fop1, fop2;

ReadFP2 (&fop2, op, r2, ea);				/* get op2, normalize */
UnpackFP (&fop1, F[r1 >> 1]);				/* get op1 */
if (fop1.sign ^ fop2.sign)				/* signs differ? */
	return (fop2.sign? CC_G: CC_L);
if (fop1.exp != fop2.exp)				/* exps differ? */
	return (((fop1.exp > fop2.exp) ^ fop1.sign)? CC_G: CC_L);
if (fop1.frh != fop2.frh)				/* fracs differ? */
	return (((fop1.frh > fop2.frh) ^ fop1.sign)? CC_G: CC_L);
return 0;
}

/* Floating point add/subtract */

int32 ase (op, r1, r2, ea)
{
struct ufp fop1, fop2, t;
int32 ediff;

ReadFP2 (&fop2, op, r2, ea);				/* get op2, normalize */
UnpackFP (&fop1, F[r1 >> 1]);				/* get op1 */
if (op & 1) fop2.sign = fop2.sign ^ 1;			/* if sub, inv sign2 */
if (fop1.frh == 0) fop1 = fop2;				/* if op1 = 0, res = op2 */
else if (fop2.frh != 0) {				/* if op2 = 0, no add */
	if ((fop1.exp < fop2.exp) || ((fop1.exp == fop2.exp) &&
		(fop1.frh < fop2.frh))) {		/* |op1| < |op2|? */
		t = fop2;				/* swap operands */
		fop2 = fop1;
		fop1 = t;  }
	if (ediff = fop1.exp - fop2.exp) {		/* exp differ? */
		if (ediff > 14) fop2.frh = 0;		/* limit shift */
		else {	FP_SHFR_V (fop2, ediff * 4);  }  }
	if (fop1.sign ^ fop2.sign) {			/* eff subtract */
		fop1.frl = 0 - fop2.frl;		/* sub fractions */
		fop1.frh = fop1.frh - fop2.frh - (fop1.frl != 0);
		NormFP (&fop1);  }			/* normalize result */
	else {	fop1.frl = fop2.frl;			/* add fractions */	
		fop1.frh = fop1.frh + fop2.frh;		
		if (fop1.frh & FP_CARRY) {		/* carry out? */
			FP_SHFR_K (fop1, 4);		/* renormalize */
			fop1.exp = fop1.exp + 1;  }  }	/* incr exp */
	}						/* end if fop2 */
return StoreFP (&fop1, r1);				/* store result */
}

/* Floating point multiply

   Note that the 24b * 24b multiply yields 2 extra hex digits of 0,
   which are accounted for by biasing the normalize count
 */

int32 me (op, r1, r2, ea)
{
struct ufp fop1, fop2;
unsigned int32 hi1, hi2, mid, lo1, lo2;

ReadFP2 (&fop2, op, r2, ea);				/* get op2, norm */
UnpackFP (&fop1, F[r1 >> 1]);				/* get op1 */
if (fop1.frh && fop2.frh) {				/* if both != 0 */
	fop1.sign = fop1.sign ^ fop2.sign;		/* sign = diff */
	fop1.exp = fop1.exp + fop2.exp - FP_BIAS + 2;	/* exp = sum */
	hi1 = (fop1.frh >> 16) & 0xFF;			/* 24b * 24b */
	hi2 = (fop2.frh >> 16) & 0xFF;			/* yields 48b */
	lo1 = fop1.frh & 0xFFFF;			/* 32b */
	lo2 = fop2.frh & 0xFFFF;			/* 16b */
	fop1.frl = lo1 * lo2;				/* 25b */
	mid = (hi1 * lo2) + (lo1 * hi2);
	fop1.frh = hi1 * hi2;
	fop1.frl = fop1.frl + ((mid << 16) & FP_DMASK);
	fop1.frh = fop1.frh + (mid >> 8) + (fop1.frl < ((mid << 16) & FP_DMASK));
	NormFP (&fop1);					/* normalize */
	return StoreFP (&fop1, r1);  }			/* store result */
else F[r1 >> 0] = 0;					/* result = 0 */
return 0;
}

/* Floating point divide */

int32 de (op, r1, r2, ea)
{
struct ufp fop1, fop2;
int32 i;
unsigned int32 divd;

ReadFP2 (&fop2, op, r2, ea);				/* get op2, norm */
UnpackFP (&fop1, F[r1 >> 1]);				/* get op1 */
if (fop2.frh == 0) return -1;				/* div by zero? */
if (fop1.frh) {						/* dvd != 0? */
	fop1.sign = fop1.sign ^ fop2.sign;		/* sign = diff */
	fop1.exp = fop1.exp - fop2.exp + FP_BIAS;	/* exp = diff */
	if (fop1.frh >= fop2.frh) divd = fop1.frh;	/* 1st sub ok? */
	else {	divd = fop1.frh << 4;			/* ensure success */
		fop1.exp = fop1.exp - 1;  }
	for (i = fop1.frh = 0; i < 6; i++) {		/* 6 hex digits */
		fop1.frh = fop1.frh << 4;		/* shift quotient */
		while (divd >= fop2.frh) {		/* while sub works */
			divd = divd - fop2.frh;		/* decrement */
			fop1.frh = fop1.frh + 1;  }	/* add quo bit */
		divd = divd << 4;  }			/* shift divd */
	if (divd >= (fop2.frh << 3)) fop1.frl = FP_ROUND;
	/* don't need to normalize */
	}						/* end if fop1.frh */
return StoreFP (&fop1, r1);				/* store result */
}

/* Utility routines */

/* Unpack floating point number */

void UnpackFP (struct ufp *fop, unsigned int32 val)
{
fop -> frl = 0;						/* low frac zero */
if (fop -> frh = FP_GETFRH (val)) {			/* any fraction? */
	fop -> sign = FP_GETSIGN (val);			/* get sign */
	fop -> exp = FP_GETEXP (val);			/* get exp */
	NormFP (fop);  }				/* normalize */
else fop -> sign = fop -> exp = 0;			/* clean zero */
return;
}

/* Read memory operand */

void ReadFP2 (struct ufp *fop, int32 op, int32 r2, int32 ea)
{
unsigned int32 t;

if (op & OP_4B) 					/* RX? */
	t = (ReadW (ea) << 16) | ReadW ((ea + 2) & AMASK);
else t = F[r2 >> 1];					/* RR */
UnpackFP (fop, t);					/* unpack op */
return;
}

/* Normalize unpacked floating point number */

void NormFP (struct ufp *fop)
{
if (fop -> frh || fop -> frl) {				/* any fraction? */
	while ((fop -> frh & FP_NORM) == 0) {		/* until norm */
		fop -> frh = (fop -> frh << 4) | ((fop -> frl >> 28) & 0xF);
		fop -> frl = fop -> frl << 4;
		fop -> exp = fop -> exp - 1;  }  }
else fop -> sign = fop -> exp = 0;			/* clean 0 */
return;
}

/* Round fp number, store, generate condition codes */

int32 StoreFP (struct ufp *fop, int32 r1)
{
if (fop -> frl & FP_ROUND) {				/* round bit set? */
	fop -> frh = fop -> frh + 1;			/* add 1 to frac */
	if (fop -> frh & FP_CARRY) {			/* carry out? */
		fop -> frh = fop -> frh >> 4;		/* renormalize */
		fop -> exp = fop -> exp + 1;  }  }	/* incr exp */
if (fop -> frh == 0) {					/* result 0? */
	F[r1 >> 1] = 0;					/* store clean 0 */
	return 0;  }
if (fop -> exp <= 0) {					/* underflow? */
	F[r1 >> 1] = 0;					/* store clean 0 */
	return CC_V;  }
if (fop -> exp > FP_M_EXP) {				/* overflow? */
	F[r1 >> 1] = (fop -> sign)? 0xFFFFFFFF: 0x3FFFFFFF;
	return (CC_V | ((fop -> sign)? CC_L: CC_G));  }
F[r1 >> 1] = ((fop -> sign & FP_M_SIGN) << FP_V_SIGN) |	/* pack result */
	((fop -> exp & FP_M_EXP) << FP_V_EXP) |
	((fop -> frh & FP_M_FRH) << FP_V_FRH);
if (fop -> sign) return CC_L;				/* generate cc's */
if (F[r1 >> 1]) return CC_G;
return 0;
}
