/* vax_fpa.c - VAX floating point accelerator simulator

   Copyright (c) 1998-2002, Robert M Supnik

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

   05-Jul-02	RMS	Changed internal routine names for C library conflict
   17-Apr-02	RMS	Fixed bug in EDIV zero quotient

   This module contains the instruction simulators for

	- 64 bit arithmetic (ASHQ, EMUL, EDIV)
	- single precision floating point
	- double precision floating point, D and G format

   To make life easier (for me), this module assumes that 64b
   integer operations are available.  Feel free to rewrite this
   in 32b arithmetic...
*/

#include "vax_defs.h"
#include <setjmp.h>

#define M32		0xFFFFFFFF			/* 32b */
#define ONES		0xFFFFFFFFFFFFFFFF		/* 64b */
#define FD_V_EXP	7				/* f/d exponent */
#define FD_M_EXP	0xFF
#define FD_BIAS		0x80				/* f/d bias */
#define FD_EXP		(FD_M_EXP << FD_V_EXP)
#define FD_HB		(1 << FD_V_EXP)			/* f/d hidden bit */
#define FD_FRACW	(0xFFFF & ~(FD_EXP | FPSIGN))
#define FD_FRACL	(FD_FRACW | 0xFFFF0000)		/* f/d fraction */
#define G_V_EXP		4				/* g exponent */
#define G_M_EXP		0x7FF
#define G_BIAS		0x400				/* g bias */
#define G_EXP		(G_M_EXP << G_V_EXP)
#define G_HB		(1 << G_V_EXP)			/* g hidden bit */
#define G_FRACW		(0xFFFF & ~(G_EXP | FPSIGN))
#define G_FRACL		(G_FRACW | 0xFFFF0000)		/* g fraction */
#define FD_GETEXP(x)	(((x) >> FD_V_EXP) & FD_M_EXP)
#define G_GETEXP(x)	(((x) >> G_V_EXP) & G_M_EXP)
#define UNSCRAM(h,l)	(((((t_uint64) (h)) << 48) & 0xFFFF000000000000) | \
			 ((((t_uint64) (h)) << 16) & 0x0000FFFF00000000) | \
			 ((((t_uint64) (l)) << 16) & 0x00000000FFFF0000) | \
			 ((((t_uint64) (l)) >> 16) & 0x000000000000FFFF))
#define CONCAT(h,l)	((((t_uint64) (h)) << 32) | \
			  ((uint32) (l)))

struct ufp {
	int32		sign;
	int32		exp;
	t_uint64	frac;  };

typedef struct ufp UFP;

#define UF_NM		0x8000000000000000		/* normalized */
#define UF_FMSK		0xFFFFFF0000000000		/* F fraction */
#define UF_FRND		0x0000008000000000		/* F round */
#define UF_DMSK		0xFFFFFFFFFFFFFF00		/* D fraction */
#define UF_DRND		0x0000000000000080		/* D round */
#define UF_GMSK		0xFFFFFFFFFFFFF800		/* G fraction */
#define UF_GRND		0x0000000000000400		/* G round */
#define UF_V_NM		63
#define UF_V_FDHI	40
#define UF_V_FDLO	(UF_V_FDHI - 32)
#define UF_V_GHI	43
#define UF_V_GLO	(UF_V_GHI - 32)
#define UF_GETFDHI(x)	(int32) ((((x) >> (16 + UF_V_FDHI)) & FD_FRACW) | \
			(((x) >> (UF_V_FDHI - 16)) & ~0xFFFF))
#define UF_GETFDLO(x)	(int32) ((((x) >> (16 + UF_V_FDLO)) & 0xFFFF) | \
			(((x) << (16 - UF_V_FDLO)) & ~0xFFFF))
#define UF_GETGHI(x)	(int32) ((((x) >> (16 + UF_V_GHI)) & G_FRACW) | \
			(((x) >> (UF_V_GHI - 16)) & ~0xFFFF))
#define UF_GETGLO(x)	(int32) ((((x) >> (16 + UF_V_GLO)) & 0xFFFF) | \
			(((x) << (16 - UF_V_GLO)) & ~0xFFFF))

extern int32 R[16];
extern int32 PSL;
extern int32 p1;
extern jmp_buf save_env;

extern int32 Read (t_addr va, int32 size, int32 acc);
void unpackf (int32 hi, UFP *a);
void unpackd (int32 hi, int32 lo, UFP *a);
void unpackg (int32 hi, int32 lo, UFP *a);
void norm (UFP *a);
int32 rpackfd (UFP *a, int32 *rh);
int32 rpackg (UFP *a, int32 *rh);
void vax_fadd (UFP *a, UFP *b, t_int64 mask);
void vax_fmul (UFP *a, UFP *b, int32 prec, int32 bias, t_int64 mask);
void vax_fdiv (UFP *b, UFP *a, int32 prec, int32 bias);
void vax_fmod (UFP *a, int32 bias, int32 *intgr, int32 *flg);

/* Quadword arithmetic shift

	opnd[0]		=	shift count (cnt.rb)
	opnd[1:2]	=	source (src.rq)
	opnd[3:4]	=	destination (dst.wq)
*/

int32 op_ashq (int32 *opnd, int32 *rh, int32 *flg)
{
t_int64 src, r;
int32 sc = opnd[0];

src = CONCAT (opnd[2], opnd[1]);			/* build src */
if (sc & BSIGN) {					/* right shift? */
	*flg = 0;					/* no ovflo */
	sc = 0x100 - sc;				/* |shift| */
	if (sc > 63) r = (opnd[2] & LSIGN)? -1: 0;	/* sc > 63? */
	else r = src >> sc;  }
else {	if (sc > 63) {					/* left shift */
		r = 0;					/* sc > 63? */
		*flg = (src != 0);  }			/* ovflo test */
	else {	r = src << sc;				/* do shift */
		*flg = (src != (r >> sc));  }  }	/* ovflo test */
*rh = (int32) (r >> 32);				/* hi result */
return ((int32) r);					/* lo result */
}

/* Extended multiply subroutine */

int32 op_emul (int32 mpy, int32 mpc, int32 *rh)
{
t_int64 lmpy = mpy;
t_int64 lmpc = mpc;

lmpy = lmpy * lmpc;
*rh = ((int32) (lmpy >> 32));
return (int32) lmpy;
}

/* Extended divide

	opnd[0]		=	divisor (non-zero)
	opnd[1:2]	=	dividend
*/

int32 op_ediv (int32 *opnd, int32 *rh, int32 *flg)
{
t_int64 ldvd, ldvr;
int32 quo, rem;

*flg = CC_V;						/* assume error */
*rh = 0;
ldvr = ((opnd[0] & LSIGN)? -opnd[0]: opnd[0]) & M32;	/* |divisor| */
ldvd = CONCAT (opnd[2], opnd[1]);			/* 64b dividend */
if (opnd[2] & LSIGN) ldvd = -ldvd;			/* |dividend| */
if (((ldvd >> 32) & M32) >= ldvr) return opnd[1];	/* divide work? */
quo = (int32) (ldvd / ldvr);				/* do divide */
rem = (int32) (ldvd % ldvr);
if ((opnd[0] ^ opnd[2]) & LSIGN) {			/* result -? */
	quo = -quo;					/* negate */
	if (quo && ((quo & LSIGN) == 0)) return opnd[1];  } /* right sign? */
else if (quo & LSIGN) return opnd[1];
if (opnd[2] & LSIGN) rem = -rem;			/* sign of rem */
*flg = 0;						/* no overflow */
*rh = rem;						/* set rem */
return quo;						/* return quo */
}

/* Move/test/move negated floating

   Note that only the high 32b is processed.
   If the high 32b is not zero, it is unchanged.
*/

int32 op_movfd (int32 val)
{
if (val & FD_EXP) return val;
if (val & FPSIGN) RSVD_OPND_FAULT;
return 0;
}

int32 op_mnegfd (int32 val)
{
if (val & FD_EXP) return (val ^ FPSIGN);
if (val & FPSIGN) RSVD_OPND_FAULT;
return 0;
}

int32 op_movg (int32 val)
{
if (val & G_EXP) return val;
if (val & FPSIGN) RSVD_OPND_FAULT;
return 0;
}

int32 op_mnegg (int32 val)
{
if (val & G_EXP) return (val ^ FPSIGN);
if (val & FPSIGN) RSVD_OPND_FAULT;
return 0;
}

/* Compare floating */

int32 op_cmpfd (int32 h1, int32 l1, int32 h2, int32 l2)
{
t_uint64 n1, n2;

if ((h1 & FD_EXP) == 0) {
	if (h1 & FPSIGN) RSVD_OPND_FAULT;
	h1 = l1 = 0;  }
if ((h2 & FD_EXP) == 0) {
	if (h2 & FPSIGN) RSVD_OPND_FAULT;
	h2 = l2 = 0;  }
if ((h1 ^ h2) & FPSIGN) return ((h1 & FPSIGN)? CC_N: 0);
n1 = UNSCRAM (h1, l1);
n2 = UNSCRAM (h2, l2);
if (n1 == n2) return CC_Z;
return (((n1 < n2) ^ ((h1 & FPSIGN) != 0))? CC_N: 0);
}

int32 op_cmpg (int32 h1, int32 l1, int32 h2, int32 l2)
{
t_uint64 n1, n2;

if ((h1 & G_EXP) == 0) {
	if (h1 & FPSIGN) RSVD_OPND_FAULT;
	h1 = l1 = 0;  }
if ((h2 & G_EXP) == 0) {
	if (h2 & FPSIGN) RSVD_OPND_FAULT;
	h2 = l2 = 0;  }
if ((h1 ^ h2) & FPSIGN) return ((h1 & FPSIGN)? CC_N: 0);
n1 = UNSCRAM (h1, l1);
n2 = UNSCRAM (h2, l2);
if (n1 == n2) return CC_Z;
return (((n1 < n2) ^ ((h1 & FPSIGN) != 0))? CC_N: 0);
}

/* Integer to floating convert */

int32 op_cvtifdg (int32 val, int32 *rh, int32 opc)
{
UFP a;

if (val == 0) {
	if (rh) *rh = 0;
	return 0;  }
if (val < 0) {
	a.sign = FPSIGN;
	val = - val;  }
else a.sign = 0;
a.exp = 32 + ((opc & 0x100)? G_BIAS: FD_BIAS);
a.frac = ((t_uint64) val) << (UF_V_NM - 31);
norm (&a);
if (opc & 0x100) return rpackg (&a, rh);
return rpackfd (&a, rh);
}

/* Floating to integer convert */

int32 op_cvtfdgi (int32 *opnd, int32 *flg, int32 opc)
{
UFP a;
int32 lnt = opc & 03;
int32 ubexp;
static t_uint64 maxv[4] = { 0x7F, 0x7FFF, 0x7FFFFFFF, 0x7FFFFFFF };

*flg = 0;
if (opc & 0x100) {
	unpackg (opnd[0], opnd[1], &a);
	ubexp = a.exp - G_BIAS;  }
else {	if (opc & 0x20) unpackd (opnd[0], opnd[1], &a);
	else unpackf (opnd[0], &a);
	ubexp = a.exp - FD_BIAS;  }
if ((a.exp == 0) || (ubexp < 0)) return 0;
if (ubexp <= UF_V_NM) {
	a.frac = a.frac >> (UF_V_NM - ubexp);		/* leave rnd bit */
	if ((opc & 03) == 03) a.frac = a.frac + 1;	/* if CVTR, round */
	a.frac = a.frac >> 1;				/* now justified */
	if (a.frac > (maxv[lnt] + (a.sign != 0))) *flg = CC_V;  }
else {	if (ubexp > (UF_V_NM + 32)) return 0;
	a.frac = a.frac << (ubexp - UF_V_NM - 1);	/* no rnd bit */
	*flg = CC_V;  }
return ((int32) (a.sign? (a.frac ^ M32) + 1: a.frac));
}

/* Floating to floating convert
   F to D is essentially done with MOVFD
*/

int32 op_cvtdf (int32 *opnd)
{
UFP a;

unpackd (opnd[0], opnd[1], &a);
return rpackfd (&a, NULL);
}

int32 op_cvtfg (int32 *opnd, int32 *rh)
{
UFP a;

unpackf (opnd[0], &a);
a.exp = a.exp - FD_BIAS + G_BIAS;
return rpackg (&a, rh);
}

int32 op_cvtgf (int32 *opnd)
{
UFP a;

unpackg (opnd[0], opnd[1], &a);
a.exp = a.exp - G_BIAS + FD_BIAS;
return rpackfd (&a, NULL);
}

/* Floating add and subtract */

int32 op_addf (int32 *opnd, t_bool sub)
{
UFP a, b;

unpackf (opnd[0], &a);					/* F format */
unpackf (opnd[1], &b);
if (sub) a.sign = a.sign ^ FPSIGN;			/* sub? -s1 */
vax_fadd (&a, &b, 0);					/* add fractions */
return rpackfd (&a, NULL);
}

int32 op_addd (int32 *opnd, int32 *rh, t_bool sub)
{
UFP a, b;

unpackd (opnd[0], opnd[1], &a);
unpackd (opnd[2], opnd[3], &b);
if (sub) a.sign = a.sign ^ FPSIGN;			/* sub? -s1 */
vax_fadd (&a, &b, 0);					/* add fractions */
return rpackfd (&a, rh);
}

int32 op_addg (int32 *opnd, int32 *rh, t_bool sub)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);
unpackg (opnd[2], opnd[3], &b);
if (sub) a.sign = a.sign ^ FPSIGN;			/* sub? -s1 */
vax_fadd (&a, &b, 0);					/* add fractions */
return rpackg (&a, rh);					/* round and pack */
}

void vax_fadd (UFP *a, UFP *b, t_int64 mask)
{
int32 ediff;
UFP t;

if (a -> exp == 0) {					/* s1 = 0? */
	*a = *b;
	return;  }
if (b -> exp == 0) return;				/* s2 = 0? */
if (a -> exp < b -> exp) {				/* s1 < s2? swap */
	t = *a;
	*a = *b;
	*b = t;  }
ediff = a -> exp - b -> exp;				/* exp diff */
if (a -> sign ^ b -> sign) {				/* eff sub? */
	if (ediff) {					/* exp diff? */
		b -> frac = (ediff > 63)? ONES:		/* shift b */
			((-((t_int64) b -> frac) >> ediff) |
			(ONES << (64 - ediff)));	/* preserve sign */
		a -> frac = a -> frac + b -> frac;  }	/* add frac */
	else {	if (a -> frac < b -> frac) {		/* same, check magn */
			a -> frac = b -> frac - a -> frac;	/* b > a */
			a -> sign = b -> sign;  }
		else a -> frac = a -> frac - b -> frac;  }	/* a >= b */
	a -> frac = a -> frac & ~mask;
	norm (a);  }					/* normalize */
else {	if (ediff >= 64) b -> frac = 0;
	else b -> frac = b -> frac >> ediff;		/* add, denorm */
	a -> frac = a -> frac + b -> frac;		/* add frac */
	if (a -> frac < b -> frac) {			/* chk for carry */
		a -> frac = UF_NM | (a -> frac >> 1);	/* shift in carry */
		a -> exp = a -> exp + 1;  }		/* skip norm */
	a -> frac = a -> frac & ~mask;  }
return;
}

/* Floating multiply

   Note that when the fractions are multiplied, the fraction position
   must be adjusted to maintain the right precision.  
*/

int32 op_mulf (int32 *opnd)
{
UFP a, b;
	
unpackf (opnd[0], &a);					/* F format */
unpackf (opnd[1], &b);
vax_fmul (&a, &b, 24, FD_BIAS, 0);			/* do multiply */
return rpackfd (&a, NULL);				/* round and pack */
}

int32 op_muld (int32 *opnd, int32 *rh)
{
UFP a, b;
	
unpackd (opnd[0], opnd[1], &a);
unpackd (opnd[2], opnd[3], &b);
vax_fmul (&a, &b, 56, FD_BIAS, 0);				/* do multiply */
return rpackfd (&a, rh);				/* round and pack */
}

int32 op_mulg (int32 *opnd, int32 *rh)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);				/* G format */
unpackg (opnd[2], opnd[3], &b);
vax_fmul (&a, &b, 53, G_BIAS, 0);			/* do multiply */
return rpackg (&a, rh);					/* round and pack */
}

/* Floating multiply - 64b * 64b with cross products
   Where is Alpha's UMULH when you need it?
*/

void vax_fmul (UFP *a, UFP *b, int32 prec, int32 bias, t_int64 mask)
{
t_uint64 ah, bh, al, bl, rhi, rlo, rmid1, rmid2;

if ((a -> exp == 0) || (b -> exp == 0)) {		/* zero argument? */
	a -> frac = a -> sign = a -> exp = 0;		/* result is zero */
	return;  }
a -> sign = a -> sign ^ b -> sign;			/* sign of result */
a -> exp = a -> exp + b -> exp - bias;			/* add exponents */
ah = (a -> frac >> 32) & M32;				/* split operands */
bh = (b -> frac >> 32) & M32;				/* into 32b chunks */
rhi = ah * bh;						/* high result */
if (prec > 32) {					/* 64b needed? */
	al = a -> frac & M32;
	bl = b -> frac & M32;
	rmid1 = ah * bl;
	rmid2 = al * bh;
	rlo = al * bl;
	rhi = rhi + ((rmid1 >> 32) & M32) + ((rmid2 >> 32) & M32);
	rmid1 = rlo + (rmid1 << 32);			/* add mid1 to lo */
	if (rmid1 < rlo) rhi = rhi + 1;			/* carry? incr hi */
	rmid2 = rmid1 + (rmid2 << 32);			/* add mid2 to to */
	if (rmid2 < rmid1) rhi = rhi + 1;  }		/* carry? incr hi */
a -> frac = rhi & ~mask;				/* mask out */
norm (a);						/* normalize */
return;
}

/* Floating divide */

int32 op_divf (int32 *opnd)
{
UFP a, b;

unpackf (opnd[0], &a);					/* F format */
unpackf (opnd[1], &b);
vax_fdiv (&a, &b, 26, FD_BIAS);				/* do divide */
return rpackfd (&b, NULL);				/* round and pack */
}

int32 op_divd (int32 *opnd, int32 *rh)
{
UFP a, b;

unpackd (opnd[0], opnd[1], &a);
unpackd (opnd[2], opnd[3], &b);
vax_fdiv (&a, &b, 58, FD_BIAS);				/* do divide */
return rpackfd (&b, rh);				/* round and pack */
}

int32 op_divg (int32 *opnd, int32 *rh)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);				/* G format */
unpackg (opnd[2], opnd[3], &b);
vax_fdiv (&a, &b, 55, G_BIAS);				/* do divide */
return rpackg (&b, rh);					/* round and pack */
}

/* Floating divide
   Needs to develop at least one rounding bit.  Since the first
   divide step can fail, caller should specify 2 more bits than
   the precision of the fraction.
*/

void vax_fdiv (UFP *a, UFP *b, int32 prec, int32 bias)
{
int32 i;
t_uint64 quo = 0;

if (a -> exp == 0) FLT_DZRO_FAULT;			/* divr = 0? */
if (b -> exp == 0) return;				/* divd = 0? */
b -> sign = b -> sign ^ a -> sign;			/* result sign */
b -> exp = b -> exp - a -> exp + bias + 1;		/* unbiased exp */
a -> frac = a -> frac >> 1;				/* allow 1 bit left */
b -> frac = b -> frac >> 1;
for (i = 0; (i < prec) && b -> frac; i++) {		/* divide loop */
	quo = quo << 1;					/* shift quo */
	if (b -> frac >= a -> frac) {			/* div step ok? */
		b -> frac = b -> frac - a -> frac;	/* subtract */
		quo = quo + 1;  }			/* quo bit = 1 */
	b -> frac = b -> frac << 1;  }			/* shift divd */
b -> frac = quo << (UF_V_NM - i + 1);			/* shift quo */
norm (b);						/* normalize */
return;
}

/* Extended modularize

   One of three floating point instructions dropped from the architecture,
   EMOD presents two sets of complications.  First, it requires an extended
   fraction multiply, with precise (and unusual) truncation conditions.
   Second, it has two write operands, a dubious distinction it shares
   with EDIV.
*/

int32 op_emodf (int32 *opnd, int32 *intgr, int32 *flg)
{
UFP a, b;

unpackf (opnd[1], &a);					/* unpack operands */
unpackf (opnd[2], &b);
a.frac = a.frac | (((t_uint64) opnd[0]) << 32);		/* extend src1 */
vax_fmul (&a, &b, 32, FD_BIAS, M32);			/* multiply */
vax_fmod (&a, FD_BIAS, intgr, flg);			/* sep int & frac */
return rpackfd (&a, NULL);				/* return frac */
}

int32 op_emodd (int32 *opnd, int32 *flo, int32 *intgr, int32 *flg)
{
UFP a, b;

unpackd (opnd[1], opnd[2], &a);				/* unpack operands */
unpackd (opnd[3], opnd[4], &b);
a.frac = a.frac | opnd[0];				/* extend src1 */
vax_fmul (&a, &b, 64, FD_BIAS, 0);			/* multiply */
vax_fmod (&a, FD_BIAS, intgr, flg);			/* sep int & frac */
return rpackfd (&a, flo);				/* return frac */
}

int32 op_emodg (int32 *opnd, int32 *flo, int32 *intgr, int32 *flg)
{
UFP a, b;

unpackg (opnd[1], opnd[2], &a);				/* unpack operands */
unpackg (opnd[3], opnd[4], &b);
a.frac = a.frac | (opnd[0] >> 5);
vax_fmul (&a, &b, 64, G_BIAS, 0);			/* multiply */			
vax_fmod (&a, G_BIAS, intgr, flg);			/* sep int & frac */
return rpackg (&a, flo);				/* return frac */
}

void vax_fmod (UFP *a, int32 bias, int32 *intgr, int32 *flg)
{
if (a -> exp <= bias) *intgr = 0;			/* 0 or <1? int = 0 */
else if (a -> exp <= (bias + 64)) {			/* in range? */
	*intgr = (int32) (a -> frac >> (64 - (a -> exp - bias)));
	a -> frac = a -> frac << (a -> exp - bias);  }
else *intgr = 0;					/* out of range */
if (a -> sign) *intgr = -*intgr;			/* -? comp int */
if ((a -> exp >= (bias + 32)) || (((a -> sign) != 0) && (*intgr < 0)))
	*flg = CC_V;					/* test ovflo */
else *flg = 0;
norm (a);						/* normalize */
return;
}

/* Polynomial evaluation
   The most mis-implemented instruction in the VAX (probably here too).
   POLY requires a precise combination of masking versus normalizing
   to achieve the desired answer.  In particular, both the multiply
   and add steps are masked prior to normalization.  In addition,
   negative small fractions must not be treated as 0 during denorm.
*/

void op_polyf (int32 *opnd, int32 acc)
{
UFP r, a, c;
int32 deg = opnd[1];
int32 ptr = opnd[2];
int32 i, wd, res;

if (deg > 31) RSVD_OPND_FAULT;				/* degree > 31? fault */
unpackf (opnd[0], &a);					/* unpack arg */
wd = Read (ptr, L_LONG, RD);				/* get C0 */
ptr = ptr + 4;
unpackf (wd, &r);					/* unpack C0 */
res = rpackfd (&r, NULL);				/* first result */
for (i = 0; (i < deg) && a.exp; i++) {			/* loop */
	unpackf (res, &r);				/* unpack result */
	vax_fmul (&r, &a, 32, FD_BIAS, M32);		/* r = r * arg */
	wd = Read (ptr, L_LONG, RD);			/* get Cnext */
	ptr = ptr + 4;
	unpackf (wd, &c);				/* unpack Cnext */
	vax_fadd (&r, &c, M32);				/* r = r + Cnext */
	res = rpackfd (&r, NULL);  }			/* round and pack */
R[0] = res;
R[1] = R[2] = 0;
R[3] = opnd[2] + 4 + (opnd[1] << 2);
return;
}

void op_polyd (int32 *opnd, int32 acc)
{
UFP r, a, c;
int32 deg = opnd[2];
int32 ptr = opnd[3];
int32 i, wd, wd1, res, resh;

if (deg > 31) RSVD_OPND_FAULT;				/* degree > 31? fault */
unpackd (opnd[0], opnd[1], &a);				/* unpack arg */
wd = Read (ptr, L_LONG, RD);				/* get C0 */
wd1 = Read (ptr + 4, L_LONG, RD);
ptr = ptr + 8;
unpackd (wd, wd1, &r);					/* unpack C0 */
res = rpackfd (&r, &resh);				/* first result */
for (i = 0; (i < deg) && a.exp; i++) {			/* loop */
	unpackd (res, resh, &r);			/* unpack result */
	vax_fmul (&r, &a, 32, FD_BIAS, 0);		/* r = r * arg */
	wd = Read (ptr, L_LONG, RD);			/* get Cnext */
	wd1 = Read (ptr + 4, L_LONG, RD);
	ptr = ptr + 8;
	unpackd (wd, wd1, &c);				/* unpack Cnext */
	vax_fadd (&r, &c, 0);				/* r = r + Cnext */
	res = rpackfd (&r, &resh);  }			/* round and pack */
R[0] = res;
R[1] = resh;
R[2] = 0;
R[3] = opnd[3] + 4 + (opnd[2] << 2);
return;
}

void op_polyg (int32 *opnd, int32 acc)
{
UFP r, a, c;
int32 deg = opnd[2];
int32 ptr = opnd[3];
int32 i, wd, wd1, res, resh;

if (deg > 31) RSVD_OPND_FAULT;				/* degree > 31? fault */
unpackg (opnd[0], opnd[1], &a);				/* unpack arg */
wd = Read (ptr, L_LONG, RD);				/* get C0 */
wd1 = Read (ptr + 4, L_LONG, RD);
ptr = ptr + 8;
unpackg (wd, wd1, &r);					/* unpack C0 */
res = rpackg (&r, &resh);				/* first result */
for (i = 0; (i < deg) && a.exp; i++) {			/* loop */
	unpackg (res, resh, &r);			/* unpack result */
	vax_fmul (&r, &a, 32, G_BIAS, 0);		/* r = r * arg */
	wd = Read (ptr, L_LONG, RD);			/* get Cnext */
	wd1 = Read (ptr + 4, L_LONG, RD);
	ptr = ptr + 8;
	unpackg (wd, wd1, &c);				/* unpack Cnext */
	vax_fadd (&r, &c, 0);				/* r = r + Cnext */
	res = rpackg (&r, &resh);  }			/* round and pack */
R[0] = res;
R[1] = resh;
R[2] = 0;
R[3] = opnd[3] + 4 + (opnd[2] << 2);
return;
}

/* Support routines */

void unpackf (int32 hi, UFP *r)
{
r -> sign = hi & FPSIGN;				/* get sign */
r -> exp = FD_GETEXP (hi);				/* get exponent */
if (r -> exp == 0) {					/* exp = 0? */
	if (r -> sign) RSVD_OPND_FAULT;			/* if -, rsvd op */
	r -> frac = 0;					/* else 0 */
	return;  }
hi = (((hi & FD_FRACW) | FD_HB) << 16) | ((hi >> 16) & 0xFFFF);
r -> frac = ((t_uint64) hi) << (32 + UF_V_FDLO);
return;
}

void unpackd (int32 hi, int32 lo, UFP *r)
{
r -> sign = hi & FPSIGN;				/* get sign */
r -> exp = FD_GETEXP (hi);				/* get exponent */
if (r -> exp == 0) {					/* exp = 0? */
	if (r -> sign) RSVD_OPND_FAULT;			/* if -, rsvd op */
	r -> frac = 0;					/* else 0 */
	return;  }
hi = (hi & FD_FRACL) | FD_HB;				/* canonical form */
r -> frac = UNSCRAM (hi, lo) << UF_V_FDLO;		/* guard bits */
return;
}

void unpackg (int32 hi, int32 lo, UFP *r)
{
r -> sign = hi & FPSIGN;				/* get sign */
r -> exp = G_GETEXP (hi);				/* get exponent */
if (r -> exp == 0) {					/* exp = 0? */
	if (r -> sign) RSVD_OPND_FAULT;			/* if -, rsvd op */
	r -> frac = 0;					/* else 0 */
	return;  }
hi = (hi & G_FRACL) | G_HB;				/* canonical form */
r -> frac = UNSCRAM (hi, lo) << UF_V_GLO;		/* guard bits */
return;
}

void norm (UFP *r)
{
int32 i;
static t_uint64 normmask[5] = {
 0xc000000000000000, 0xf000000000000000, 0xff00000000000000,
 0xffff000000000000, 0xffffffff00000000 };
static int32 normtab[6] = { 1, 2, 4, 8, 16, 32};

if (r -> frac == 0) {					/* if fraction = 0 */
	r -> sign = r -> exp = 0;			/* result is 0 */
	return;  }
while ((r -> frac & UF_NM) == 0) {			/* normalized? */
	for (i = 0; i < 5; i++) {			/* find first 1 */
		if (r -> frac & normmask[i]) break;  }
	r -> frac = r -> frac << normtab[i];		/* shift frac */
	r -> exp = r -> exp - normtab[i];  }		/* decr exp */
return;
}

int32 rpackfd (UFP *r, int32 *rh)
{
if (rh) *rh = 0;					/* assume 0 */
if (r -> frac == 0) return 0;				/* result 0? */
r -> frac = r -> frac + (rh? UF_DRND: UF_FRND);		/* round */
if ((r -> frac & UF_NM) == 0) {				/* carry out? */
	r -> frac = r -> frac >> 1;			/* renormalize */
	r -> exp = r -> exp + 1;  }
if (r -> exp > (int32) FD_M_EXP) FLT_OVFL_FAULT;	/* ovflo? fault */
if (r -> exp <= 0) {					/* underflow? */
	if (PSL & PSW_FU) FLT_UNFL_FAULT;		/* fault if fu */
	return 0;  }					/* else 0 */
if (rh) *rh = UF_GETFDLO (r -> frac);			/* get low */
return r -> sign | (r -> exp << FD_V_EXP) | UF_GETFDHI (r -> frac);
}

int32 rpackg (UFP *r, int32 *rh)
{
*rh = 0;						/* assume 0 */
if (r -> frac == 0) return 0;				/* result 0? */
r -> frac = r -> frac + UF_GRND;			/* round */
if ((r -> frac & UF_NM) == 0) {				/* carry out? */
	r -> frac = r -> frac >> 1;			/* renormalize */
	r -> exp = r -> exp + 1;  }
if (r -> exp > (int32) G_M_EXP) FLT_OVFL_FAULT;		/* ovflo? fault */
if (r -> exp <= 0) {					/* underflow? */
	if (PSL & PSW_FU) FLT_UNFL_FAULT;		/* fault if fu */
	return 0;  }					/* else 0 */
if (rh) *rh = UF_GETGLO (r -> frac);			/* get low */
return r -> sign | (r -> exp << G_V_EXP) | UF_GETGHI (r -> frac);
}
