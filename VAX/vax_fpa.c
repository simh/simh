/* vax_fpa.c - VAX f_, d_, g_floating instructions

   Copyright (c) 1998-2005, Robert M Supnik

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

   27-Sep-05    RMS     Fixed bug in 32b structure definitions (from Jason Stevens)
   30-Sep-04    RMS     Comment and formating changes based on vax_octa.c
   18-Apr-04    RMS     Moved format definitions to vax_defs.h
   19-Jun-03    RMS     Simplified add algorithm
   16-May-03    RMS     Fixed bug in floating to integer convert overflow
                        Fixed multiple bugs in EMODx
                        Integrated 32b only code
   05-Jul-02    RMS     Changed internal routine names for C library conflict
   17-Apr-02    RMS     Fixed bug in EDIV zero quotient

   This module contains the instruction simulators for

        - 64 bit arithmetic (ASHQ, EMUL, EDIV)
        - single precision floating point
        - double precision floating point, D and G format
*/

#include "vax_defs.h"
#include <setjmp.h>

extern int32 R[16];
extern int32 PSL;
extern int32 p1;
extern jmp_buf save_env;

extern int32 Read (uint32 va, int32 size, int32 acc);

#if defined (USE_INT64)

#define M64             0xFFFFFFFFFFFFFFFF              /* 64b */
#define FD_FRACW        (0xFFFF & ~(FD_EXP | FPSIGN))
#define FD_FRACL        (FD_FRACW | 0xFFFF0000)         /* f/d fraction */
#define G_FRACW         (0xFFFF & ~(G_EXP | FPSIGN))
#define G_FRACL         (G_FRACW | 0xFFFF0000)          /* g fraction */
#define UNSCRAM(h,l)    (((((t_uint64) (h)) << 48) & 0xFFFF000000000000) | \
                        ((((t_uint64) (h)) << 16) & 0x0000FFFF00000000) | \
                        ((((t_uint64) (l)) << 16) & 0x00000000FFFF0000) | \
                        ((((t_uint64) (l)) >> 16) & 0x000000000000FFFF))
#define CONCAT(h,l)     ((((t_uint64) (h)) << 32) | ((uint32) (l)))

typedef struct {
    int32               sign;
    int32               exp;
    t_uint64            frac;
    } UFP;

#define UF_NM           0x8000000000000000              /* normalized */
#define UF_FRND         0x0000008000000000              /* F round */
#define UF_DRND         0x0000000000000080              /* D round */
#define UF_GRND         0x0000000000000400              /* G round */
#define UF_V_NM         63
#define UF_V_FDHI       40
#define UF_V_FDLO       (UF_V_FDHI - 32)
#define UF_V_GHI        43
#define UF_V_GLO        (UF_V_GHI - 32)
#define UF_GETFDHI(x)   (int32) ((((x) >> (16 + UF_V_FDHI)) & FD_FRACW) | \
                        (((x) >> (UF_V_FDHI - 16)) & ~0xFFFF))
#define UF_GETFDLO(x)   (int32) ((((x) >> (16 + UF_V_FDLO)) & 0xFFFF) | \
                        (((x) << (16 - UF_V_FDLO)) & ~0xFFFF))
#define UF_GETGHI(x)    (int32) ((((x) >> (16 + UF_V_GHI)) & G_FRACW) | \
                        (((x) >> (UF_V_GHI - 16)) & ~0xFFFF))
#define UF_GETGLO(x)    (int32) ((((x) >> (16 + UF_V_GLO)) & 0xFFFF) | \
                        (((x) << (16 - UF_V_GLO)) & ~0xFFFF))

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

        opnd[0]         =       shift count (cnt.rb)
        opnd[1:2]       =       source (src.rq)
        opnd[3:4]       =       destination (dst.wq)
*/

int32 op_ashq (int32 *opnd, int32 *rh, int32 *flg)
{
t_int64 src, r;
int32 sc = opnd[0];

src = CONCAT (opnd[2], opnd[1]);                        /* build src */
if (sc & BSIGN) {                                       /* right shift? */
    *flg = 0;                                           /* no ovflo */
    sc = 0x100 - sc;                                    /* |shift| */
    if (sc > 63) r = (opnd[2] & LSIGN)? -1: 0;          /* sc > 63? */
    else r = src >> sc;
    }
else {
    if (sc > 63) {                                      /* left shift */
        r = 0;                                          /* sc > 63? */
        *flg = (src != 0);                              /* ovflo test */
        }
    else {
        r = src << sc;                                  /* do shift */
        *flg = (src != (r >> sc));                      /* ovflo test */
        }
    }
*rh = (int32) ((r >> 32) & LMASK);                      /* hi result */
return ((int32) (r & LMASK));                           /* lo result */
}

/* Extended multiply subroutine */

int32 op_emul (int32 mpy, int32 mpc, int32 *rh)
{
t_int64 lmpy = mpy;
t_int64 lmpc = mpc;

lmpy = lmpy * lmpc;
*rh = (int32) ((lmpy >> 32) & LMASK);
return ((int32) (lmpy & LMASK));
}

/* Extended divide

        opnd[0]         =       divisor (non-zero)
        opnd[1:2]       =       dividend
*/

int32 op_ediv (int32 *opnd, int32 *rh, int32 *flg)
{
t_int64 ldvd, ldvr;
int32 quo, rem;

*flg = CC_V;                                            /* assume error */
*rh = 0;
ldvr = ((opnd[0] & LSIGN)? -opnd[0]: opnd[0]) & LMASK;  /* |divisor| */
ldvd = CONCAT (opnd[2], opnd[1]);                       /* 64b dividend */
if (opnd[2] & LSIGN) ldvd = -ldvd;                      /* |dividend| */
if (((ldvd >> 32) & LMASK) >= ldvr) return opnd[1];     /* divide work? */
quo = (int32) (ldvd / ldvr);                            /* do divide */
rem = (int32) (ldvd % ldvr);
if ((opnd[0] ^ opnd[2]) & LSIGN) {                      /* result -? */
    quo = -quo;                                         /* negate */
    if (quo && ((quo & LSIGN) == 0)) return opnd[1];    /* right sign? */
    }
else if (quo & LSIGN) return opnd[1];
if (opnd[2] & LSIGN) rem = -rem;                        /* sign of rem */
*flg = 0;                                               /* no overflow */
*rh = rem & LMASK;                                      /* set rem */
return (quo & LMASK);                                   /* return quo */
}

/* Compare floating */

int32 op_cmpfd (int32 h1, int32 l1, int32 h2, int32 l2)
{
t_uint64 n1, n2;

if ((h1 & FD_EXP) == 0) {
    if (h1 & FPSIGN) RSVD_OPND_FAULT;
    h1 = l1 = 0;
    }
if ((h2 & FD_EXP) == 0) {
    if (h2 & FPSIGN) RSVD_OPND_FAULT;
    h2 = l2 = 0;
    }
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
    h1 = l1 = 0;
    }
if ((h2 & G_EXP) == 0) {
    if (h2 & FPSIGN) RSVD_OPND_FAULT;
    h2 = l2 = 0;
    }
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
    return 0;
    }
if (val < 0) {
    a.sign = FPSIGN;
    val = - val;
    }
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
    ubexp = a.exp - G_BIAS;
    }
else {
    if (opc & 0x20) unpackd (opnd[0], opnd[1], &a);
    else unpackf (opnd[0], &a);
    ubexp = a.exp - FD_BIAS;
    }
if ((a.exp == 0) || (ubexp < 0)) return 0;
if (ubexp <= UF_V_NM) {
    a.frac = a.frac >> (UF_V_NM - ubexp);               /* leave rnd bit */
    if ((opc & 03) == 03) a.frac = a.frac + 1;          /* if CVTR, round */
    a.frac = a.frac >> 1;                               /* now justified */
    if (a.frac > (maxv[lnt] + (a.sign? 1: 0))) *flg = CC_V;
    }
else {
    *flg = CC_V;                                        /* set overflow */
    if (ubexp > (UF_V_NM + 32)) return 0;
    a.frac = a.frac << (ubexp - UF_V_NM - 1);           /* no rnd bit */
    }
return ((int32) ((a.sign? (a.frac ^ LMASK) + 1: a.frac) & LMASK));
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

unpackf (opnd[0], &a);                                  /* unpack operands */
unpackf (opnd[2], &b);
a.frac = a.frac | (((t_uint64) opnd[1]) << 32);         /* extend src1 */
vax_fmul (&a, &b, 32, FD_BIAS, LMASK);                  /* multiply */
vax_fmod (&a, FD_BIAS, intgr, flg);                     /* sep int & frac */
return rpackfd (&a, NULL);                              /* return frac */
}

int32 op_emodd (int32 *opnd, int32 *flo, int32 *intgr, int32 *flg)
{
UFP a, b;

unpackd (opnd[0], opnd[1], &a);                         /* unpack operands */
unpackd (opnd[3], opnd[4], &b);
a.frac = a.frac | opnd[2];                              /* extend src1 */
vax_fmul (&a, &b, 64, FD_BIAS, 0);                      /* multiply */
vax_fmod (&a, FD_BIAS, intgr, flg);                     /* sep int & frac */
return rpackfd (&a, flo);                               /* return frac */
}

int32 op_emodg (int32 *opnd, int32 *flo, int32 *intgr, int32 *flg)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);                         /* unpack operands */
unpackg (opnd[3], opnd[4], &b);
a.frac = a.frac | (opnd[2] >> 5);                       /* extend src1 */
vax_fmul (&a, &b, 64, G_BIAS, 0);                       /* multiply */                  
vax_fmod (&a, G_BIAS, intgr, flg);                      /* sep int & frac */
return rpackg (&a, flo);                                /* return frac */
}

/* Unpacked floating point routines */

void vax_fadd (UFP *a, UFP *b, t_int64 mask)
{
int32 ediff;
UFP t;

if (a->exp == 0) {                                      /* s1 = 0? */
    *a = *b;
    return;
    }
if (b->exp == 0) return;                                /* s2 = 0? */
if ((a->exp < b->exp) ||                                /* |s1| < |s2|? swap */
    ((a->exp == b->exp) && (a->frac < b->frac))) {
    t = *a;
    *a = *b;
    *b = t;
    }
ediff = a->exp - b->exp;                                /* exp diff */
if (a->sign ^ b->sign) {                                /* eff sub? */
    if (ediff) {                                        /* exp diff? */
        if (ediff > 63) b->frac = M64;                  /* retain sticky */
        else b->frac = ((-((t_int64) b->frac) >> ediff) | /* denormalize */
            (M64 << (64 - ediff)));                     /* preserve sign */
        a->frac = a->frac + b->frac;                    /* add frac */
        }
    else a->frac = a->frac - b->frac;                   /* sub frac */
    a->frac = a->frac & ~mask;
    norm (a);                                           /* normalize */
    }
else {
    if (ediff > 63) b->frac = 0;                        /* add */
    else if (ediff) b->frac = b->frac >> ediff;         /* denormalize */
    a->frac = a->frac + b->frac;                        /* add frac */
    if (a->frac < b->frac) {                            /* chk for carry */
        a->frac = UF_NM | (a->frac >> 1);               /* shift in carry */
        a->exp = a->exp + 1;                            /* skip norm */
        }
    a->frac = a->frac & ~mask;
    }
return;
}

/* Floating multiply - 64b * 64b with cross products */

void vax_fmul (UFP *a, UFP *b, int32 prec, int32 bias, t_int64 mask)
{
t_uint64 ah, bh, al, bl, rhi, rlo, rmid1, rmid2;

if ((a->exp == 0) || (b->exp == 0)) {                   /* zero argument? */
    a->frac = a->sign = a->exp = 0;                     /* result is zero */
    return;
    }
a->sign = a->sign ^ b->sign;                            /* sign of result */
a->exp = a->exp + b->exp - bias;                        /* add exponents */
ah = (a->frac >> 32) & LMASK;                           /* split operands */
bh = (b->frac >> 32) & LMASK;                           /* into 32b chunks */
rhi = ah * bh;                                          /* high result */
if (prec > 32) {                                        /* 64b needed? */
    al = a->frac & LMASK;
    bl = b->frac & LMASK;
    rmid1 = ah * bl;
    rmid2 = al * bh;
    rlo = al * bl;
    rhi = rhi + ((rmid1 >> 32) & LMASK) + ((rmid2 >> 32) & LMASK);
    rmid1 = rlo + (rmid1 << 32);                        /* add mid1 to lo */
    if (rmid1 < rlo) rhi = rhi + 1;                     /* carry? incr hi */
    rmid2 = rmid1 + (rmid2 << 32);                      /* add mid2 to to */
    if (rmid2 < rmid1) rhi = rhi + 1;                   /* carry? incr hi */
    }
a->frac = rhi & ~mask;                                  /* mask out */
norm (a);                                               /* normalize */
return;
}

/* Floating modulus - there are three cases

   exp <= bias                  - integer is 0, fraction is input,
                                  no overflow
   bias < exp <= bias+64        - separate integer and fraction,
                                  integer overflow may occur
   bias+64 < exp                - result is integer, fraction is 0
                                  integer overflow
*/

void vax_fmod (UFP *a, int32 bias, int32 *intgr, int32 *flg)
{
if (a->exp <= bias) *intgr = *flg = 0;                  /* 0 or <1? int = 0 */
else if (a->exp <= (bias + 64)) {                       /* in range? */
    *intgr = (int32) (a->frac >> (64 - (a->exp - bias)));
    if ((a->exp > (bias + 32)) ||                       /* test ovflo */
        ((a->exp == (bias + 32)) &&
         (((uint32) *intgr) > (a->sign? 0x80000000: 0x7FFFFFFF))))
        *flg = CC_V;
    else *flg = 0;
    if (a->sign) *intgr = -*intgr;                      /* -? comp int */
    a->frac = a->frac << (a->exp - bias);
    a->exp = bias;
    }
else {
    *intgr = 0;                                         /* out of range */
    a->frac = a->sign = a->exp = 0;                     /* result 0 */
    *flg = CC_V;                                        /* overflow */
    }
norm (a);                                               /* normalize */
return;
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

if (a->exp == 0) FLT_DZRO_FAULT;                        /* divr = 0? */
if (b->exp == 0) return;                                /* divd = 0? */
b->sign = b->sign ^ a->sign;                            /* result sign */
b->exp = b->exp - a->exp + bias + 1;                    /* unbiased exp */
a->frac = a->frac >> 1;                                 /* allow 1 bit left */
b->frac = b->frac >> 1;
for (i = 0; (i < prec) && b->frac; i++) {               /* divide loop */
    quo = quo << 1;                                     /* shift quo */
    if (b->frac >= a->frac) {                           /* div step ok? */
        b->frac = b->frac - a->frac;                    /* subtract */
        quo = quo + 1;                                  /* quo bit = 1 */
        }
    b->frac = b->frac << 1;                             /* shift divd */
    }
b->frac = quo << (UF_V_NM - i + 1);                     /* shift quo */
norm (b);                                               /* normalize */
return;
}

/* Support routines */

void unpackf (int32 hi, UFP *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = FD_GETEXP (hi);                                /* get exponent */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign) RSVD_OPND_FAULT;                       /* if -, rsvd op */
    r->frac = 0;                                        /* else 0 */
    return;
    }
hi = (((hi & FD_FRACW) | FD_HB) << 16) | ((hi >> 16) & 0xFFFF);
r->frac = ((t_uint64) hi) << (32 + UF_V_FDLO);
return;
}

void unpackd (int32 hi, int32 lo, UFP *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = FD_GETEXP (hi);                                /* get exponent */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign) RSVD_OPND_FAULT;                       /* if -, rsvd op */
    r->frac = 0;                                        /* else 0 */
    return;
    }
hi = (hi & FD_FRACL) | FD_HB;                           /* canonical form */
r->frac = UNSCRAM (hi, lo) << UF_V_FDLO;                /* guard bits */
return;
}

void unpackg (int32 hi, int32 lo, UFP *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = G_GETEXP (hi);                                 /* get exponent */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign) RSVD_OPND_FAULT;                       /* if -, rsvd op */
    r->frac = 0;                                        /* else 0 */
    return;
    }
hi = (hi & G_FRACL) | G_HB;                             /* canonical form */
r->frac = UNSCRAM (hi, lo) << UF_V_GLO;                 /* guard bits */
return;
}

void norm (UFP *r)
{
int32 i;
static t_uint64 normmask[5] = {
 0xc000000000000000, 0xf000000000000000, 0xff00000000000000,
 0xffff000000000000, 0xffffffff00000000
 };
static int32 normtab[6] = { 1, 2, 4, 8, 16, 32};

if (r->frac == 0) {                                     /* if fraction = 0 */
    r->sign = r->exp = 0;                               /* result is 0 */
    return;
    }
while ((r->frac & UF_NM) == 0) {                        /* normalized? */
    for (i = 0; i < 5; i++) {                           /* find first 1 */
        if (r->frac & normmask[i]) break;
        }
    r->frac = r->frac << normtab[i];                    /* shift frac */
    r->exp = r->exp - normtab[i];                       /* decr exp */
    }
return;
}

int32 rpackfd (UFP *r, int32 *rh)
{
if (rh) *rh = 0;                                        /* assume 0 */
if (r->frac == 0) return 0;                             /* result 0? */
r->frac = r->frac + (rh? UF_DRND: UF_FRND);             /* round */
if ((r->frac & UF_NM) == 0) {                           /* carry out? */
    r->frac = r->frac >> 1;                             /* renormalize */
    r->exp = r->exp + 1;
    }
if (r->exp > (int32) FD_M_EXP) FLT_OVFL_FAULT;          /* ovflo? fault */
if (r->exp <= 0) {                                      /* underflow? */
    if (PSL & PSW_FU) FLT_UNFL_FAULT;                   /* fault if fu */
    return 0;                                           /* else 0 */
    }
if (rh) *rh = UF_GETFDLO (r->frac);                     /* get low */
return r->sign | (r->exp << FD_V_EXP) | UF_GETFDHI (r->frac);
}

int32 rpackg (UFP *r, int32 *rh)
{
*rh = 0;                                                /* assume 0 */
if (r->frac == 0) return 0;                             /* result 0? */
r->frac = r->frac + UF_GRND;                            /* round */
if ((r->frac & UF_NM) == 0) {                           /* carry out? */
    r->frac = r->frac >> 1;                             /* renormalize */
    r->exp = r->exp + 1;
    }
if (r->exp > (int32) G_M_EXP) FLT_OVFL_FAULT;           /* ovflo? fault */
if (r->exp <= 0) {                                      /* underflow? */
    if (PSL & PSW_FU) FLT_UNFL_FAULT;                   /* fault if fu */
    return 0;                                           /* else 0 */
    }
if (rh) *rh = UF_GETGLO (r->frac);                      /* get low */
return r->sign | (r->exp << G_V_EXP) | UF_GETGHI (r->frac);
}

#else                                                   /* 32b code */

#define WORDSWAP(x)     ((((x) & WMASK) << 16) | (((x) >> 16) & WMASK))

typedef struct {
    uint32              lo;
    uint32              hi;
    } UDP;

typedef struct {
    int32               sign;
    int32               exp;
    UDP                 frac;
    } UFP;

#define UF_NM_H         0x80000000                      /* normalized */
#define UF_FRND_H       0x00000080                      /* F round */
#define UF_FRND_L       0x00000000
#define UF_DRND_H       0x00000000                      /* D round */
#define UF_DRND_L       0x00000080
#define UF_GRND_H       0x00000000                      /* G round */
#define UF_GRND_L       0x00000400
#define UF_V_NM         63

void unpackf (uint32 hi, UFP *a);
void unpackd (uint32 hi, uint32 lo, UFP *a);
void unpackg (uint32 hi, uint32 lo, UFP *a);
void norm (UFP *a);
int32 rpackfd (UFP *a, int32 *rh);
int32 rpackg (UFP *a, int32 *rh);
void vax_fadd (UFP *a, UFP *b, uint32 mask);
void vax_fmul (UFP *a, UFP *b, int32 prec, int32 bias, uint32 mask);
void vax_fmod (UFP *a, int32 bias, int32 *intgr, int32 *flg);
void vax_fdiv (UFP *b, UFP *a, int32 prec, int32 bias);
int32 vax_fcmp (UFP *a, UFP *b);
void dp_add (UDP *a, UDP *b);
void dp_inc (UDP *a);
void dp_sub (UDP *a, UDP *b);
void dp_imul (uint32 a, uint32 b, UDP *r);
void dp_lsh (UDP *a, uint32 sc);
void dp_rsh (UDP *a, uint32 sc);
void dp_rsh_s (UDP *a, uint32 sc, uint32 neg);
void dp_neg (UDP *a);
int32 dp_cmp (UDP *a, UDP *b);

/* Quadword arithmetic shift

        opnd[0]         =       shift count (cnt.rb)
        opnd[1:2]       =       source (src.rq)
        opnd[3:4]       =       destination (dst.wq)
*/

int32 op_ashq (int32 *opnd, int32 *rh, int32 *flg)
{
UDP r, sr;
uint32 sc = opnd[0];

r.lo = opnd[1];                                         /* get source */
r.hi = opnd[2];
*flg = 0;                                               /* assume no ovflo */
if (sc & BSIGN)                                         /* right shift? */
    dp_rsh_s (&r, 0x100 - sc, r.hi & LSIGN);            /* signed right */
else {
    dp_lsh (&r, sc);                                    /* left shift */
    sr = r;                                             /* copy result */
    dp_rsh_s (&sr, sc, sr.hi & LSIGN);                  /* signed right */
    if ((sr.hi != ((uint32) opnd[2])) ||                /* reshift != orig? */
        (sr.lo != ((uint32) opnd[1]))) *flg = 1;        /* overflow */
    }
*rh = r.hi;                                             /* hi result */
return r.lo;                                            /* lo result */
}

/* Extended multiply subroutine */

int32 op_emul (int32 mpy, int32 mpc, int32 *rh)
{
UDP r;
int32 sign = mpy ^ mpc;                                 /* sign of result */

if (mpy & LSIGN) mpy = -mpy;                            /* abs value */
if (mpc & LSIGN) mpc = -mpc;
dp_imul (mpy & LMASK, mpc & LMASK, &r);                 /* 32b * 32b -> 64b */
if (sign & LSIGN) dp_neg (&r);                          /* negative result? */
*rh = r.hi;
return r.lo;
}

/* Extended divide

        opnd[0]         =       divisor (non-zero)
        opnd[1:2]       =       dividend
*/

int32 op_ediv (int32 *opnd, int32 *rh, int32 *flg)
{
UDP dvd;
uint32 i, dvr, quo;

dvr = opnd[0];                                          /* get divisor */
dvd.lo = opnd[1];                                       /* get dividend */
dvd.hi = opnd[2];
*flg = CC_V;                                            /* assume error */
*rh = 0;
if (dvd.hi & LSIGN) dp_neg (&dvd);                      /* |dividend| */
if (dvr & LSIGN) dvr = NEG (dvr);                       /* |divisor| */
if (dvd.hi >= dvr) return opnd[1];                      /* divide work? */
for (i = quo = 0; i < 32; i++) {                        /* 32 iterations */
    quo = quo << 1;                                     /* shift quotient */
    dp_lsh (&dvd, 1);                                   /* shift dividend */
    if (dvd.hi >= dvr) {                                /* step work? */
        dvd.hi = (dvd.hi - dvr) & LMASK;                /* subtract dvr */
        quo = quo + 1;
        }
    }
if ((opnd[0] ^ opnd[2]) & LSIGN) {                      /* result -? */
    quo = NEG (quo);                                    /* negate */
    if (quo && ((quo & LSIGN) == 0)) return opnd[1];    /* right sign? */
    }
else if (quo & LSIGN) return opnd[1];
if (opnd[2] & LSIGN) *rh = NEG (dvd.hi);                /* sign of rem */
else *rh = dvd.hi;
*flg = 0;                                               /* no overflow */
return quo;                                             /* return quo */
}

/* Compare floating */

int32 op_cmpfd (int32 h1, int32 l1, int32 h2, int32 l2)
{
UFP a, b;
int32 r;

unpackd (h1, l1, &a);
unpackd (h2, l2, &b);
if (a.sign != b.sign) return (a.sign? CC_N: 0);
r = a.exp - b.exp;
if (r == 0) r = dp_cmp (&a.frac, &b.frac);
if (r < 0) return (a.sign? 0: CC_N);
if (r > 0) return (a.sign? CC_N: 0);
return CC_Z;
}

int32 op_cmpg (int32 h1, int32 l1, int32 h2, int32 l2)
{
UFP a, b;
int32 r;

unpackg (h1, l1, &a);
unpackg (h2, l2, &b);
if (a.sign != b.sign) return (a.sign? CC_N: 0);
r = a.exp - b.exp;
if (r == 0) r = dp_cmp (&a.frac, &b.frac);
if (r < 0) return (a.sign? 0: CC_N);
if (r > 0) return (a.sign? CC_N: 0);
return CC_Z;
}

/* Integer to floating convert */

int32 op_cvtifdg (int32 val, int32 *rh, int32 opc)
{
UFP a;

if (val == 0) {                                         /* zero? */
    if (rh) *rh = 0;                                    /* return true 0 */
    return 0;
    }
if (val < 0) {                                          /* negative? */
    a.sign = FPSIGN;                                    /* sign = - */
    val = -val;
    }
else a.sign = 0;                                        /* else sign = + */
a.exp = 32 + ((opc & 0x100)? G_BIAS: FD_BIAS);          /* initial exp */
a.frac.hi = val & LMASK;                                /* fraction */
a.frac.lo = 0;
norm (&a);                                              /* normalize */
if (opc & 0x100) return rpackg (&a, rh);                /* pack and return */
return rpackfd (&a, rh);
}

/* Floating to integer convert */

int32 op_cvtfdgi (int32 *opnd, int32 *flg, int32 opc)
{
UFP a;
int32 lnt = opc & 03;
int32 ubexp;
static uint32 maxv[4] = { 0x7F, 0x7FFF, 0x7FFFFFFF, 0x7FFFFFFF };

*flg = 0;
if (opc & 0x100) {                                      /* G? */
    unpackg (opnd[0], opnd[1], &a);                     /* unpack */
    ubexp = a.exp - G_BIAS;                             /* unbiased exp */
    }
else {
    if (opc & 0x20) unpackd (opnd[0], opnd[1], &a);     /* F or D */
    else unpackf (opnd[0], &a);                         /* unpack */
    ubexp = a.exp - FD_BIAS;                            /* unbiased exp */
    }
if ((a.exp == 0) || (ubexp < 0)) return 0;              /* true zero or frac? */
if (ubexp <= UF_V_NM) {                                 /* exp in range? */
    dp_rsh (&a.frac, UF_V_NM - ubexp);                  /* leave rnd bit */
    if (lnt == 03) dp_inc (&a.frac);                    /* if CVTR, round */
    dp_rsh (&a.frac, 1);                                /* now justified */
    if ((a.frac.hi != 0) ||
        (a.frac.lo > (maxv[lnt] + (a.sign? 1: 0)))) *flg = CC_V;
    }
else {
    *flg = CC_V;                                        /* always ovflo */
    if (ubexp > (UF_V_NM + 32)) return 0;               /* in ext range? */
    dp_lsh (&a.frac, ubexp - UF_V_NM - 1);              /* no rnd bit */
    }
return (a.sign? NEG (a.frac.lo): a.frac.lo);            /* return lo frac */
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

unpackf (opnd[0], &a);                                  /* unpack operands */
unpackf (opnd[2], &b);
a.frac.hi = a.frac.hi | opnd[1];                        /* extend src1 */
vax_fmul (&a, &b, 32, FD_BIAS, LMASK);                  /* multiply */
vax_fmod (&a, FD_BIAS, intgr, flg);                     /* sep int & frac */
return rpackfd (&a, NULL);                              /* return frac */
}

int32 op_emodd (int32 *opnd, int32 *flo, int32 *intgr, int32 *flg)
{
UFP a, b;

unpackd (opnd[0], opnd[1], &a);                         /* unpack operands */
unpackd (opnd[3], opnd[4], &b);
a.frac.lo = a.frac.lo | opnd[2];                        /* extend src1 */
vax_fmul (&a, &b, 64, FD_BIAS, 0);                      /* multiply */
vax_fmod (&a, FD_BIAS, intgr, flg);                     /* sep int & frac */
return rpackfd (&a, flo);                               /* return frac */
}

int32 op_emodg (int32 *opnd, int32 *flo, int32 *intgr, int32 *flg)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);                         /* unpack operands */
unpackg (opnd[3], opnd[4], &b);
a.frac.lo = a.frac.lo | (opnd[2] >> 5);                 /* extend src1 */
vax_fmul (&a, &b, 64, G_BIAS, 0);                       /* multiply */                  
vax_fmod (&a, G_BIAS, intgr, flg);                      /* sep int & frac */
return rpackg (&a, flo);                                /* return frac */
}

/* Unpacked floating point routines */

/* Floating add */

void vax_fadd (UFP *a, UFP *b, uint32 mask)
{
int32 ediff;
UFP t;

if (a->exp == 0) {                                      /* s1 = 0? */
    *a = *b;
    return;
    }
if (b->exp == 0) return;                                /* s2 = 0? */
if ((a->exp < b->exp) ||                                /* |s1| < |s2|? swap */
    ((a->exp == b->exp) && (dp_cmp (&a->frac, &b->frac) < 0))) {
    t = *a;
    *a = *b;
    *b = t;
    }
ediff = a->exp - b->exp;                                /* exp diff */
if (a->sign ^ b->sign) {                                /* eff sub? */
    if (ediff) {                                        /* exp diff? */
        dp_neg (&b->frac);                              /* negate fraction */
        dp_rsh_s (&b->frac, ediff, 1);                  /* signed right */
        dp_add (&a->frac, &b->frac);                    /* "add" frac */
        }
    else dp_sub (&a->frac, &b->frac);                   /* a >= b */
    a->frac.lo = a->frac.lo & ~mask;                    /* trim low result */
    norm (a);                                           /* normalize */
    }
else {
    if (ediff) dp_rsh (&b->frac, ediff);                /* add, denormalize */
    dp_add (&a->frac, &b->frac);                        /* add frac */
    if (dp_cmp (&a->frac, &b->frac) < 0) {              /* chk for carry */
        dp_rsh (&a->frac, 1);                           /* renormalize */
        a->frac.hi = a->frac.hi | UF_NM_H;              /* add norm bit */
        a->exp = a->exp + 1;                            /* skip norm */
        }
    a->frac.lo = a->frac.lo & ~mask;                    /* trim low result */
    }
return;
}

/* Floating compare */

int32 vax_fcmp (UFP *a, UFP *b)
{
int32 r;

if (a->sign != b->sign) return ((a->sign)? CC_N: 0);
r = a->exp - b->exp;
if (r == 0) r = dp_cmp (&a->frac, &b->frac);
if (r == 0) return CC_Z;
if (r < 0) return (a->sign? 0: CC_N);
else return (a->sign? CC_N: 0);
}

/* Floating multiply - 64b * 64b with cross products */

void vax_fmul (UFP *a, UFP *b, int32 prec, int32 bias, uint32 mask)
{
UDP rhi, rlo, rmid1, rmid2;

if ((a->exp == 0) || (b->exp == 0)) {                   /* zero argument? */
    a->frac.hi = a->frac.lo = 0;                        /* result is zero */
    a->sign = a->exp = 0;
    return;
    }
a->sign = a->sign ^ b->sign;                            /* sign of result */
a->exp = a->exp + b->exp - bias;                        /* add exponents */
dp_imul (a->frac.hi, b->frac.hi, &rhi);                 /* high result */
if (prec > 32) {                                        /* 64b needed? */
    dp_imul (a->frac.hi, b->frac.lo, &rmid1);           /* cross products */
    dp_imul (a->frac.lo, b->frac.hi, &rmid2);
    dp_imul (a->frac.lo, b->frac.lo, &rlo);             /* low result */
    rhi.lo = (rhi.lo + rmid1.hi) & LMASK;               /* add hi cross */
    if (rhi.lo < rmid1.hi)                              /* to low high res */
        rhi.hi = (rhi.hi + 1) & LMASK;
    rhi.lo = (rhi.lo + rmid2.hi) & LMASK;
    if (rhi.lo < rmid2.hi)
         rhi.hi = (rhi.hi + 1) & LMASK;
    rlo.hi = (rlo.hi + rmid1.lo) & LMASK;               /* add mid1 to low res */
    if (rlo.hi < rmid1.lo) dp_inc (&rhi);               /* carry? incr high res */
    rlo.hi = (rlo.hi + rmid2.lo) & LMASK;               /* add mid2 to low res */
    if (rlo.hi < rmid1.hi) dp_inc (&rhi);               /* carry? incr high res */
    }
a->frac.hi = rhi.hi;                                    /* mask low fraction */
a->frac.lo = rhi.lo & ~mask;
norm (a);                                               /* normalize */
return;
}

/* Floating modulus - there are three cases

   exp <= bias                  - integer is 0, fraction is input,
                                  no overflow
   bias < exp <= bias+64        - separate integer and fraction,
                                  integer overflow may occur
   bias+64 < exp                - result is integer, fraction is 0
                                  integer overflow
*/

void vax_fmod (UFP *a, int32 bias, int32 *intgr, int32 *flg)
{
UDP ifr;

if (a->exp <= bias) *intgr = *flg = 0;                  /* 0 or <1? int = 0 */
else if (a->exp <= (bias + 64)) {                       /* in range? */
    ifr = a->frac;
    dp_rsh (&ifr, 64 - (a->exp - bias));                /* separate integer */
    if ((a->exp > (bias + 32)) ||                       /* test ovflo */
        ((a->exp == (bias + 32)) &&
         (ifr.lo > (a->sign? 0x80000000: 0x7FFFFFFF))))
        *flg = CC_V;
    else *flg = 0;
    *intgr = ifr.lo;
    if (a->sign) *intgr = -*intgr;                      /* -? comp int */
    dp_lsh (&a->frac, a->exp - bias);                   /* excise integer */
    a->exp = bias;
    }
else {
    *intgr = 0;                                         /* out of range */
    a->frac.hi = a->frac.lo = a->sign = a->exp = 0;     /* result 0 */
    *flg = CC_V;                                        /* overflow */
    }
norm (a);                                               /* normalize */
return;
}

/* Floating divide
   Needs to develop at least one rounding bit.  Since the first
   divide step can fail, caller should specify 2 more bits than
   the precision of the fraction.
*/

void vax_fdiv (UFP *a, UFP *b, int32 prec, int32 bias)
{
int32 i;
UDP quo = { 0, 0 };

if (a->exp == 0) FLT_DZRO_FAULT;                        /* divr = 0? */
if (b->exp == 0) return;                                /* divd = 0? */
b->sign = b->sign ^ a->sign;                            /* result sign */
b->exp = b->exp - a->exp + bias + 1;                    /* unbiased exp */
dp_rsh (&a->frac, 1);                                   /* allow 1 bit left */
dp_rsh (&b->frac, 1);
for (i = 0; i < prec; i++) {                            /* divide loop */
    dp_lsh (&quo, 1);                                   /* shift quo */
    if (dp_cmp (&b->frac, &a->frac) >= 0) {             /* div step ok? */
        dp_sub (&b->frac, &a->frac);                    /* subtract */
        quo.lo = quo.lo + 1;                            /* quo bit = 1 */
        }
    dp_lsh (&b->frac, 1);                               /* shift divd */
    }
dp_lsh (&quo, UF_V_NM - prec + 1);                      /* put in position */
b->frac = quo;
norm (b);                                               /* normalize */
return;
}

/* Double precision integer routines */

int32 dp_cmp (UDP *a, UDP *b)
{
if (a->hi < b->hi) return -1;                           /* compare hi */
if (a->hi > b->hi) return +1;
if (a->lo < b->lo) return -1;                           /* hi =, compare lo */
if (a->lo > b->lo) return +1;
return 0;                                               /* hi, lo equal */
}

void dp_add (UDP *a, UDP *b)
{
a->lo = (a->lo + b->lo) & LMASK;                        /* add lo */
if (a->lo < b->lo) a->hi = a->hi + 1;                   /* carry? */
a->hi = (a->hi + b->hi) & LMASK;                        /* add hi */
return;
}

void dp_inc (UDP *a)
{
a->lo = (a->lo + 1) & LMASK;                            /* inc lo */
if (a->lo == 0) a->hi = (a->hi + 1) & LMASK;            /* carry? inc hi */
return;
}

void dp_sub (UDP *a, UDP *b)
{
if (a->lo < b->lo) a->hi = a->hi - 1;                   /* borrow? decr hi */
a->lo = (a->lo - b->lo) & LMASK;                        /* sub lo */
a->hi = (a->hi - b->hi) & LMASK;                        /* sub hi */
return;
}

void dp_lsh (UDP *r, uint32 sc)
{
if (sc > 63) r->hi = r->lo = 0;                         /* > 63? result 0 */
else if (sc > 31) {                                     /* [32,63]? */
    r->hi = (r->lo << (sc - 32)) & LMASK;
    r->lo = 0;
    }
else if (sc != 0) {
    r->hi = ((r->hi << sc) | (r->lo >> (32 - sc))) & LMASK;
    r->lo = (r->lo << sc) & LMASK;
    }
return;
}

void dp_rsh (UDP *r, uint32 sc)
{
if (sc > 63) r->hi = r->lo = 0;                         /* > 63? result 0 */
else if (sc > 31) {                                     /* [32,63]? */
    r->lo = (r->hi >> (sc - 32)) & LMASK;
    r->hi = 0;
    }
else if (sc != 0) {
    r->lo = ((r->lo >> sc) | (r->hi << (32 - sc))) & LMASK;
    r->hi = (r->hi >> sc) & LMASK;
    }
return;
}

void dp_rsh_s (UDP *r, uint32 sc, uint32 neg)
{
dp_rsh (r, sc);                                         /* do unsigned right */
if (neg && sc) {                                        /* negative? */
    if (sc > 63) r->hi = r->lo = LMASK;                 /* > 63? result -1 */
    else {
        UDP ones = { LMASK, LMASK };
        dp_lsh (&ones, 64 - sc);                        /* shift ones */
        r->hi = r->hi | ones.hi;                        /* or into result */
        r->lo = r->lo | ones.lo;
        }
    }
return;
}

void dp_imul (uint32 a, uint32 b, UDP *r)
{
uint32 ah, bh, al, bl, rhi, rlo, rmid1, rmid2;

if ((a == 0) || (b == 0)) {                             /* zero argument? */
    r->hi = r->lo = 0;                                  /* result is zero */
    return;
    }
ah = (a >> 16) & WMASK;                                 /* split operands */
bh = (b >> 16) & WMASK;                                 /* into 16b chunks */
al = a & WMASK;
bl = b & WMASK;
rhi = ah * bh;                                          /* high result */
rmid1 = ah * bl;
rmid2 = al * bh;
rlo = al * bl;
rhi = rhi + ((rmid1 >> 16) & WMASK) + ((rmid2 >> 16) & WMASK);
rmid1 = (rlo + (rmid1 << 16)) & LMASK;                  /* add mid1 to lo */
if (rmid1 < rlo) rhi = rhi + 1;                         /* carry? incr hi */
rmid2 = (rmid1 + (rmid2 << 16)) & LMASK;                        /* add mid2 to to */
if (rmid2 < rmid1) rhi = rhi + 1;                       /* carry? incr hi */
r->hi = rhi & LMASK;                                    /* mask result */
r->lo = rmid2;
return;
}

void dp_neg (UDP *r)
{
r->lo = NEG (r->lo);
r->hi = (~r->hi + (r->lo == 0)) & LMASK;
return;
}

/* Support routines */

void unpackf (uint32 hi, UFP *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = FD_GETEXP (hi);                                /* get exponent */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign) RSVD_OPND_FAULT;                       /* if -, rsvd op */
    r->frac.hi = r->frac.lo = 0;                        /* else 0 */
    return;
    }
r->frac.hi = WORDSWAP ((hi & ~(FPSIGN | FD_EXP)) | FD_HB);
r->frac.lo = 0;
dp_lsh (&r->frac, FD_GUARD);
return;
}

void unpackd (uint32 hi, uint32 lo, UFP *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = FD_GETEXP (hi);                                /* get exponent */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign) RSVD_OPND_FAULT;                       /* if -, rsvd op */
    r->frac.hi = r->frac.lo = 0;                        /* else 0 */
    return;
      }
r->frac.hi = WORDSWAP ((hi & ~(FPSIGN | FD_EXP)) | FD_HB);
r->frac.lo = WORDSWAP (lo);
dp_lsh (&r->frac, FD_GUARD);
return;
}

void unpackg (uint32 hi, uint32 lo, UFP *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = G_GETEXP (hi);                                 /* get exponent */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign) RSVD_OPND_FAULT;                       /* if -, rsvd op */
    r->frac.hi = r->frac.lo = 0;                        /* else 0 */
    return;
    }
r->frac.hi = WORDSWAP ((hi & ~(FPSIGN | G_EXP)) | G_HB);
r->frac.lo = WORDSWAP (lo);
dp_lsh (&r->frac, G_GUARD);
return;
}

void norm (UFP *r)
{
int32 i;
static uint32 normmask[5] = {
 0xc0000000, 0xf0000000, 0xff000000, 0xffff0000, 0xffffffff
 };
static int32 normtab[6] = { 1, 2, 4, 8, 16, 32};

if ((r->frac.hi == 0) && (r->frac.lo == 0)) {           /* if fraction = 0 */
    r->sign = r->exp = 0;                               /* result is 0 */
    return;
    }
while ((r->frac.hi & UF_NM_H) == 0) {                   /* normalized? */
    for (i = 0; i < 5; i++) {                           /* find first 1 */
        if (r->frac.hi & normmask[i]) break;
        }
    dp_lsh (&r->frac, normtab[i]);                      /* shift frac */
    r->exp = r->exp - normtab[i];                       /* decr exp */
    }
return;
}

int32 rpackfd (UFP *r, int32 *rh)
{
static UDP f_round = { UF_FRND_L, UF_FRND_H };
static UDP d_round = { UF_DRND_L, UF_DRND_H };

if (rh) *rh = 0;                                        /* assume 0 */
if ((r->frac.hi == 0) && (r->frac.lo == 0)) return 0;   /* result 0? */
if (rh) dp_add (&r->frac, &d_round);                    /* round */
else dp_add (&r->frac, &f_round);
if ((r->frac.hi & UF_NM_H) == 0) {                      /* carry out? */
    dp_rsh (&r->frac, 1);                               /* renormalize */
    r->exp = r->exp + 1;
    }
if (r->exp > (int32) FD_M_EXP) FLT_OVFL_FAULT;          /* ovflo? fault */
if (r->exp <= 0) {                                      /* underflow? */
    if (PSL & PSW_FU) FLT_UNFL_FAULT;                   /* fault if fu */
    return 0;                                           /* else 0 */
    }
dp_rsh (&r->frac, FD_GUARD);                            /* remove guard */
if (rh) *rh = WORDSWAP (r->frac.lo);                    /* get low */
return r->sign | (r->exp << FD_V_EXP) |
    (WORDSWAP (r->frac.hi) & ~(FD_HB | FPSIGN | FD_EXP));
}

int32 rpackg (UFP *r, int32 *rh)
{
static UDP g_round = { UF_GRND_L, UF_GRND_H };

*rh = 0;                                                /* assume 0 */
if ((r->frac.hi == 0) && (r->frac.lo == 0)) return 0;   /* result 0? */
dp_add (&r->frac, &g_round);                            /* round */
if ((r->frac.hi & UF_NM_H) == 0) {                      /* carry out? */
    dp_rsh (&r->frac, 1);                               /* renormalize */
    r->exp = r->exp + 1;
    }
if (r->exp > (int32) G_M_EXP) FLT_OVFL_FAULT;           /* ovflo? fault */
if (r->exp <= 0) {                                      /* underflow? */
    if (PSL & PSW_FU) FLT_UNFL_FAULT;                   /* fault if fu */
    return 0;                                           /* else 0 */
    }
dp_rsh (&r->frac, G_GUARD);                             /* remove guard */
*rh = WORDSWAP (r->frac.lo);                            /* get low */
return r->sign | (r->exp << G_V_EXP) |
    (WORDSWAP (r->frac.hi) & ~(G_HB | FPSIGN | G_EXP));
}

#endif

/* Floating point instructions */

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

/* Floating to floating convert - F to D is essentially done with MOVFD */

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

unpackf (opnd[0], &a);                                  /* F format */
unpackf (opnd[1], &b);
if (sub) a.sign = a.sign ^ FPSIGN;                      /* sub? -s1 */
vax_fadd (&a, &b, 0);                                   /* add fractions */
return rpackfd (&a, NULL);
}

int32 op_addd (int32 *opnd, int32 *rh, t_bool sub)
{
UFP a, b;

unpackd (opnd[0], opnd[1], &a);
unpackd (opnd[2], opnd[3], &b);
if (sub) a.sign = a.sign ^ FPSIGN;                      /* sub? -s1 */
vax_fadd (&a, &b, 0);                                   /* add fractions */
return rpackfd (&a, rh);
}

int32 op_addg (int32 *opnd, int32 *rh, t_bool sub)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);
unpackg (opnd[2], opnd[3], &b);
if (sub) a.sign = a.sign ^ FPSIGN;                      /* sub? -s1 */
vax_fadd (&a, &b, 0);                                   /* add fractions */
return rpackg (&a, rh);                                 /* round and pack */
}

/* Floating multiply */

int32 op_mulf (int32 *opnd)
{
UFP a, b;
    
unpackf (opnd[0], &a);                                  /* F format */
unpackf (opnd[1], &b);
vax_fmul (&a, &b, 24, FD_BIAS, 0);                      /* do multiply */
return rpackfd (&a, NULL);                              /* round and pack */
}

int32 op_muld (int32 *opnd, int32 *rh)
{
UFP a, b;
    
unpackd (opnd[0], opnd[1], &a);
unpackd (opnd[2], opnd[3], &b);
vax_fmul (&a, &b, 56, FD_BIAS, 0);                      /* do multiply */
return rpackfd (&a, rh);                                /* round and pack */
}

int32 op_mulg (int32 *opnd, int32 *rh)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);                         /* G format */
unpackg (opnd[2], opnd[3], &b);
vax_fmul (&a, &b, 53, G_BIAS, 0);                       /* do multiply */
return rpackg (&a, rh);                                 /* round and pack */
}

/* Floating divide */

int32 op_divf (int32 *opnd)
{
UFP a, b;

unpackf (opnd[0], &a);                                  /* F format */
unpackf (opnd[1], &b);
vax_fdiv (&a, &b, 26, FD_BIAS);                         /* do divide */
return rpackfd (&b, NULL);                              /* round and pack */
}

int32 op_divd (int32 *opnd, int32 *rh)
{
UFP a, b;

unpackd (opnd[0], opnd[1], &a);
unpackd (opnd[2], opnd[3], &b);
vax_fdiv (&a, &b, 58, FD_BIAS);                         /* do divide */
return rpackfd (&b, rh);                                /* round and pack */
}

int32 op_divg (int32 *opnd, int32 *rh)
{
UFP a, b;

unpackg (opnd[0], opnd[1], &a);                         /* G format */
unpackg (opnd[2], opnd[3], &b);
vax_fdiv (&a, &b, 55, G_BIAS);                          /* do divide */
return rpackg (&b, rh);                                 /* round and pack */
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

if (deg > 31) RSVD_OPND_FAULT;                          /* degree > 31? fault */
unpackf (opnd[0], &a);                                  /* unpack arg */
wd = Read (ptr, L_LONG, RD);                            /* get C0 */
ptr = ptr + 4;
unpackf (wd, &r);                                       /* unpack C0 */
res = rpackfd (&r, NULL);                               /* first result */
for (i = 0; (i < deg) && a.exp; i++) {                  /* loop */
    unpackf (res, &r);                                  /* unpack result */
    vax_fmul (&r, &a, 32, FD_BIAS, LMASK);              /* r = r * arg */
    wd = Read (ptr, L_LONG, RD);                        /* get Cnext */
    ptr = ptr + 4;
    unpackf (wd, &c);                                   /* unpack Cnext */
    vax_fadd (&r, &c, LMASK);                           /* r = r + Cnext */
    res = rpackfd (&r, NULL);                           /* round and pack */
    }
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

if (deg > 31) RSVD_OPND_FAULT;                          /* degree > 31? fault */
unpackd (opnd[0], opnd[1], &a);                         /* unpack arg */
wd = Read (ptr, L_LONG, RD);                            /* get C0 */
wd1 = Read (ptr + 4, L_LONG, RD);
ptr = ptr + 8;
unpackd (wd, wd1, &r);                                  /* unpack C0 */
res = rpackfd (&r, &resh);                              /* first result */
for (i = 0; (i < deg) && a.exp; i++) {                  /* loop */
    unpackd (res, resh, &r);                            /* unpack result */
    vax_fmul (&r, &a, 32, FD_BIAS, 0);                  /* r = r * arg */
    wd = Read (ptr, L_LONG, RD);                        /* get Cnext */
    wd1 = Read (ptr + 4, L_LONG, RD);
    ptr = ptr + 8;
    unpackd (wd, wd1, &c);                              /* unpack Cnext */
    vax_fadd (&r, &c, 0);                               /* r = r + Cnext */
    res = rpackfd (&r, &resh);                          /* round and pack */
    }
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

if (deg > 31) RSVD_OPND_FAULT;                          /* degree > 31? fault */
unpackg (opnd[0], opnd[1], &a);                         /* unpack arg */
wd = Read (ptr, L_LONG, RD);                            /* get C0 */
wd1 = Read (ptr + 4, L_LONG, RD);
ptr = ptr + 8;
unpackg (wd, wd1, &r);                                  /* unpack C0 */
res = rpackg (&r, &resh);                               /* first result */
for (i = 0; (i < deg) && a.exp; i++) {                  /* loop */
    unpackg (res, resh, &r);                            /* unpack result */
    vax_fmul (&r, &a, 32, G_BIAS, 0);                   /* r = r * arg */
    wd = Read (ptr, L_LONG, RD);                        /* get Cnext */
    wd1 = Read (ptr + 4, L_LONG, RD);
    ptr = ptr + 8;
    unpackg (wd, wd1, &c);                              /* unpack Cnext */
    vax_fadd (&r, &c, 0);                               /* r = r + Cnext */
    res = rpackg (&r, &resh);                           /* round and pack */
    }
R[0] = res;
R[1] = resh;
R[2] = 0;
R[3] = opnd[3] + 4 + (opnd[2] << 2);
return;
}
