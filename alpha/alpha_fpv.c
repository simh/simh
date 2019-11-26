/* alpha_fpv.c - Alpha VAX floating point simulator

   Copyright (c) 2003-2006, Robert M Supnik

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

   This module contains the instruction simulators for

        - single precision floating point, F
        - double precision floating point, G
*/

#include "alpha_defs.h"

#define IPMAX           0x7FFFFFFFFFFFFFFF              /* plus MAX (int) */
#define IMMAX           0x8000000000000000              /* minus MAX (int) */

/* Unpacked rounding constants */

#define UF_FRND         0x0000008000000000              /* F round */
#define UF_DRND         0x0000000000000080              /* D round */
#define UF_GRND         0x0000000000000400              /* G round */

extern t_uint64 FR[32];
extern jmp_buf save_env;

t_bool vax_unpack (t_uint64 op, UFP *a, uint32 ir);
t_bool vax_unpack_d (t_uint64 op, UFP *a, uint32 ir);
void vax_norm (UFP *a);
t_uint64 vax_rpack (UFP *a, uint32 ir, uint32 dp);
t_uint64 vax_rpack_d (UFP *a, uint32 ir);
int32 vax_fcmp (t_uint64 a, t_uint64 b, uint32 ir);
t_uint64 vax_cvtif (t_uint64 val, uint32 ir, uint32 dp);
t_uint64 vax_cvtfi (t_uint64 op, uint32 ir);
t_uint64 vax_fadd (t_uint64 a, t_uint64 b, uint32 ir, uint32 dp, t_bool sub);
t_uint64 vax_fmul (t_uint64 a, t_uint64 b, uint32 ir, uint32 dp);
t_uint64 vax_fdiv (t_uint64 a, t_uint64 b, uint32 ir, uint32 dp);

extern t_uint64 uemul64 (t_uint64 a, t_uint64 b, t_uint64 *hi);
extern t_uint64 ufdiv64 (t_uint64 dvd, t_uint64 dvr, uint32 prec, uint32 *sticky);
extern t_uint64 fsqrt64 (t_uint64 frac, int32 exp);

/* VAX floating point loads and stores */

t_uint64 op_ldf (t_uint64 op)
{
uint32 exp = F_GETEXP (op);

if (exp != 0) exp = exp + G_BIAS - F_BIAS;              /* zero? */     
return (((t_uint64) (op & F_SIGN))? FPR_SIGN: 0) |      /* finite non-zero */
    (((t_uint64) exp) << FPR_V_EXP) |
    (((t_uint64) SWAP_VAXF (op & ~(F_SIGN|F_EXP))) << F_V_FRAC);
}

t_uint64 op_ldg (t_uint64 op)
{
return SWAP_VAXG (op);                                  /* swizzle bits */
}

t_uint64 op_stf (t_uint64 op)
{
uint32 sign = FPR_GETSIGN (op)? F_SIGN: 0;
uint32 frac = (uint32) (op >> F_V_FRAC);
uint32 exp = FPR_GETEXP (op);

if (exp != 0) exp = exp + F_BIAS - G_BIAS;              /* zero? */
exp = (exp & F_M_EXP) << F_V_EXP;
return (t_uint64) (sign | exp | (SWAP_VAXF (frac) & ~(F_SIGN|F_EXP)));
}

t_uint64 op_stg (t_uint64 op)
{
return SWAP_VAXG (op);                                  /* swizzle bits */
}

/* VAX floating point operate */

void vax_fop (uint32 ir)
{
UFP b;
t_uint64 res;
uint32 fnc, ra, rb, rc;

fnc = I_GETFFNC (ir);                                   /* get function */
ra = I_GETRA (ir);                                      /* get registers */
rb = I_GETRB (ir);
rc = I_GETRC (ir);
switch (fnc) {                                          /* case on func */

    case 0x00:                                          /* ADDF */
        res = vax_fadd (FR[ra], FR[rb], ir, DT_F, 0);
        break;

    case 0x01:                                          /* SUBF */
        res = vax_fadd (FR[ra], FR[rb], ir, DT_F, 1);
        break;

    case 0x02:                                          /* MULF */
        res = vax_fmul (FR[ra], FR[rb], ir, DT_F);
        break;

    case 0x03:                                          /* DIVF */
        res = vax_fdiv (FR[ra], FR[rb], ir, DT_F);
        break;

    case 0x20:                                          /* ADDG */
        res = vax_fadd (FR[ra], FR[rb], ir, DT_G, 0);
        break;

    case 0x21:                                          /* SUBG */
        res = vax_fadd (FR[ra], FR[rb], ir, DT_G, 1);
        break;

    case 0x22:                                          /* MULG */
        res = vax_fmul (FR[ra], FR[rb], ir, DT_G);
        break;

    case 0x23:                                          /* DIVG */
        res = vax_fdiv (FR[ra], FR[rb], ir, DT_G);
        break;

    case 0x25:                                          /* CMPGEQ */
        if (vax_fcmp (FR[ra], FR[rb], ir) == 0) res = FP_TRUE;
        else res = 0;
        break;

    case 0x26:                                          /* CMPGLT */
        if (vax_fcmp (FR[ra], FR[rb], ir) < 0) res = FP_TRUE;
        else res = 0;
        break;

    case 0x27:                                          /* CMPGLE */
        if (vax_fcmp (FR[ra], FR[rb], ir) <= 0) res = FP_TRUE;
        else res = 0;
        break;

    case 0x1E:                                          /* CVTDG */
        if (vax_unpack_d (FR[rb], &b, ir)) res = 0;
        else res = vax_rpack (&b, ir, DT_G);
        break;

    case 0x2C:                                          /* CVTGF */
        if (vax_unpack (FR[rb], &b, ir)) res = 0;
        else res = vax_rpack (&b, ir, DT_F);
        break;

    case 0x2D:                                          /* CVTGD */
        if (vax_unpack (FR[rb], &b, ir)) res = 0;
        else res = vax_rpack_d (&b, ir);
        break;

    case 0x2F:                                          /* CVTGQ */
        res = vax_cvtfi (FR[rb], ir);
        break;

    case 0x3C:                                          /* CVTQF */
        res = vax_cvtif (FR[rb], ir, DT_F);
        break;

    case 0x3E:                                          /* CVTQG */
        res = vax_cvtif (FR[rb], ir, DT_G);
        break;

    default:
        res = FR[rc];
        break;
        }

if (rc != 31) FR[rc] = res & M64;
return;
}

/* VAX floating compare */

int32 vax_fcmp (t_uint64 s1, t_uint64 s2, uint32 ir)
{
UFP a, b;

if (vax_unpack (s1, &a, ir)) return +1;                 /* unpack, rsv? */
if (vax_unpack (s2, &b, ir)) return +1;                 /* unpack, rsv? */
if (s1 == s2) return 0;                                 /* equal? */
if (a.sign != b.sign) return (a.sign? -1: +1);          /* opp signs? */
return (((s1 < s2) ^ a.sign)? -1: +1);                  /* like signs */
}

/* VAX integer to floating convert */

t_uint64 vax_cvtif (t_uint64 val, uint32 ir, uint32 dp)
{
UFP a;

if (val == 0) return 0;                                 /* 0? return +0 */
if ((val & Q_SIGN) != 0) {                              /* < 0? */
    a.sign = 1;                                         /* set sign */
    val = NEG_Q (val);                                  /* |val| */
    }
else a.sign = 0;
a.exp = 64 + G_BIAS;                                    /* set exp */
a.frac = val;                                           /* set frac */
vax_norm (&a);                                          /* normalize */
return vax_rpack (&a, ir, dp);                          /* round and pack */
}

/* VAX floating to integer convert - note that rounding cannot cause a
   carry unless the fraction has been shifted right at least FP_GUARD
   places; in which case a carry out is impossible */

t_uint64 vax_cvtfi (t_uint64 op, uint32 ir)
{
UFP a;
uint32 rndm = I_GETFRND (ir);
int32 ubexp;

if (vax_unpack (op, &a, ir)) return 0;                  /* unpack, rsv? */
ubexp = a.exp - G_BIAS;                                 /* unbiased exp */
if (ubexp < 0) return 0;                                /* zero or too small? */
if (ubexp <= UF_V_NM) {                                 /* in range? */
    a.frac = a.frac >> (UF_V_NM - ubexp);               /* leave rnd bit */
    if (rndm) a.frac = a.frac + 1;                      /* not chopped, round */
    a.frac = a.frac >> 1;                               /* now justified */
    if ((a.frac > (a.sign? IMMAX: IPMAX)) &&            /* out of range? */
        (ir & I_FTRP_V))                                /* trap enabled? */
        arith_trap (TRAP_IOV, ir);                      /* set overflow */
    }
else {
    if (ubexp > (UF_V_NM + 64)) a.frac = 0;             /* out of range */
    else a.frac = (a.frac << (ubexp - UF_V_NM - 1)) & M64;      /* no rnd bit */
    if (ir & I_FTRP_V)                                  /* trap enabled? */
        arith_trap (TRAP_IOV, ir);                      /* set overflow */
    }
return (a.sign? NEG_Q (a.frac): a.frac);
}

/* VAX floating add */

t_uint64 vax_fadd (t_uint64 s1, t_uint64 s2, uint32 ir, uint32 dp, t_bool sub)
{
UFP a, b, t;
uint32 sticky;
int32 ediff;

if (vax_unpack (s1, &a, ir)) return 0;                  /* unpack, rsv? */
if (vax_unpack (s2, &b, ir)) return 0;                  /* unpack, rsv? */
if (sub) b.sign = b.sign ^ 1;                           /* sub? invert b sign */
if (a.exp == 0) a = b;                                  /* s1 = 0? */
else if (b.exp) {                                       /* s2 != 0? */
    if ((a.exp < b.exp) ||                              /* |s1| < |s2|? swap */
        ((a.exp == b.exp) && (a.frac < b.frac))) {
        t = a;
        a = b;
        b = t;
        }
    ediff = a.exp - b.exp;                              /* exp diff */
    if (a.sign ^ b.sign) {                              /* eff sub? */
        if (ediff > 63) b.frac = 1;                     /* >63? retain sticky */
        else if (ediff) {                               /* [1,63]? shift */
            sticky = ((b.frac << (64 - ediff)) & M64)? 1: 0; /* lost bits */
            b.frac = (b.frac >> ediff) | sticky;
            }
        a.frac = (a.frac - b.frac) & M64;               /* subtract fractions */
        vax_norm (&a);                                  /* normalize */
        }
    else {                                              /* eff add */
        if (ediff > 63) b.frac = 0;                     /* >63? b disappears */
        else if (ediff) b.frac = b.frac >> ediff;       /* denormalize */
        a.frac = (a.frac + b.frac) & M64;               /* add frac */
        if (a.frac < b.frac) {                          /* chk for carry */
            a.frac = UF_NM | (a.frac >> 1);             /* shift in carry */
            a.exp = a.exp + 1;                          /* skip norm */
            }
        }
    }                                                   /* end else if */
return vax_rpack (&a, ir, dp);                          /* round and pack */
}

/* VAX floating multiply */

t_uint64 vax_fmul (t_uint64 s1, t_uint64 s2, uint32 ir, uint32 dp)
{
UFP a, b;

if (vax_unpack (s1, &a, ir)) return 0;                  /* unpack, rsv? */
if (vax_unpack (s2, &b, ir)) return 0;                  /* unpack, rsv? */
if ((a.exp == 0) || (b.exp == 0)) return 0;             /* zero argument? */
a.sign = a.sign ^ b.sign;                               /* sign of result */
a.exp = a.exp + b.exp - G_BIAS;                         /* add exponents */
uemul64 (a.frac, b.frac, &a.frac);                      /* mpy fractions */
vax_norm (&a);                                          /* normalize */
return vax_rpack (&a, ir, dp);                          /* round and pack */
}

/* VAX floating divide
   Needs to develop at least one rounding bit.  Since the first
   divide step can fail, develop 2 more bits than the precision of
   the fraction. */

t_uint64 vax_fdiv (t_uint64 s1, t_uint64 s2, uint32 ir, uint32 dp)
{
UFP a, b;

if (vax_unpack (s1, &a, ir)) return 0;                  /* unpack, rsv? */
if (vax_unpack (s2, &b, ir)) return 0;                  /* unpack, rsv? */
if (b.exp == 0) {                                       /* divr = 0? */
    arith_trap (TRAP_DZE, ir);                          /* dze trap */
    return 0;
    }
if (a.exp == 0) return 0;                               /* divd = 0? */
a.sign = a.sign ^ b.sign;                               /* result sign */
a.exp = a.exp - b.exp + G_BIAS + 1;                     /* unbiased exp */
a.frac = a.frac >> 1;                                   /* allow 1 bit left */
b.frac = b.frac >> 1;
a.frac = ufdiv64 (a.frac, b.frac, 55, NULL);            /* divide */
vax_norm (&a);                                          /* normalize */
return vax_rpack (&a, ir, dp);                          /* round and pack */
}

/* VAX floating square root */

t_uint64 vax_sqrt (uint32 ir, uint32 dp)
{
t_uint64 op;
UFP b;

op = FR[I_GETRB (ir)];                                  /* get F[rb] */
if (vax_unpack (op, &b, ir)) return 0;                  /* unpack, rsv? */
if (b.exp == 0) return 0;                               /* zero? */
if (b.sign) {                                           /* minus? */
    arith_trap (TRAP_INV, ir);                          /* invalid operand */
    return 0;
    }
b.exp = ((b.exp + 1 - G_BIAS) >> 1) + G_BIAS;           /* result exponent */
b.frac = fsqrt64 (b.frac, b.exp);                       /* result fraction */
return vax_rpack (&b, ir, dp);                          /* round and pack */
}

/* Support routines */

t_bool vax_unpack (t_uint64 op, UFP *r, uint32 ir)
{
r->sign = FPR_GETSIGN (op);                             /* get sign */
r->exp = FPR_GETEXP (op);                               /* get exponent */
r->frac = FPR_GETFRAC (op);                             /* get fraction */
if (r->exp == 0) {                                      /* exp = 0? */
    if (op != 0) arith_trap (TRAP_INV, ir);             /* rsvd op? */
    r->frac = r->sign = 0;
    return TRUE;
    }
r->frac = (r->frac | FPR_HB) << FPR_GUARD;              /* ins hidden bit, guard */
return FALSE;
}

t_bool vax_unpack_d (t_uint64 op, UFP *r, uint32 ir)
{
r->sign = FDR_GETSIGN (op);                             /* get sign */
r->exp = FDR_GETEXP (op);                               /* get exponent */
r->frac = FDR_GETFRAC (op);                             /* get fraction */
if (r->exp == 0) {                                      /* exp = 0? */
    if (op != 0) arith_trap (TRAP_INV, ir);             /* rsvd op? */
    r->frac = r->sign = 0;
    return TRUE;
    }
r->exp = r->exp + G_BIAS - D_BIAS;                      /* change to G bias */
r->frac = (r->frac | FDR_HB) << FDR_GUARD;              /* ins hidden bit, guard */
return FALSE;
}

/* VAX normalize */

void vax_norm (UFP *r)
{
int32 i;
static t_uint64 normmask[5] = {
    0xc000000000000000, 0xf000000000000000, 0xff00000000000000,
    0xffff000000000000, 0xffffffff00000000
    };
static int32 normtab[6] = { 1, 2, 4, 8, 16, 32 };

r->frac = r->frac & M64;
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

/* VAX round and pack */

t_uint64 vax_rpack (UFP *r, uint32 ir, uint32 dp)
{
uint32 rndm = I_GETFRND (ir);
static const t_uint64 roundbit[2] = { UF_FRND, UF_GRND };
static const int32 expmax[2] = { G_BIAS - F_BIAS + F_M_EXP, G_M_EXP };
static const int32 expmin[2] = { G_BIAS - F_BIAS, 0 };

if (r->frac == 0) return 0;                             /* result 0? */
if (rndm) {                                             /* round? */
    r->frac = (r->frac + roundbit[dp]) & M64;           /* add round bit */
    if ((r->frac & UF_NM) == 0) {                       /* carry out? */
        r->frac = (r->frac >> 1) | UF_NM;               /* renormalize */
        r->exp = r->exp + 1;
        }
    }
if (r->exp > expmax[dp]) {                              /* ovflo? */
    arith_trap (TRAP_OVF, ir);                          /* set trap */
    r->exp = expmax[dp];                                /* return max */
    }
if (r->exp <= expmin[dp]) {                             /* underflow? */
    if (ir & I_FTRP_V) arith_trap (TRAP_UNF, ir);       /* enabled? set trap */
    return 0;                                           /* underflow to 0 */
    }
return (((t_uint64) r->sign) << FPR_V_SIGN) |
    (((t_uint64) r->exp) << FPR_V_EXP) |
    ((r->frac >> FPR_GUARD) & FPR_FRAC);
}

t_uint64 vax_rpack_d (UFP *r, uint32 ir)
{
if (r->frac == 0) return 0;                             /* result 0? */
r->exp = r->exp + D_BIAS - G_BIAS;                      /* rebias */
if (r->exp > FDR_M_EXP) {                               /* ovflo? */
    arith_trap (TRAP_OVF, ir);                          /* set trap */
    r->exp = FDR_M_EXP;                                 /* return max */
    }
if (r->exp <= 0) {                                      /* underflow? */
    if (ir & I_FTRP_V) arith_trap (TRAP_UNF, ir);       /* enabled? set trap */
    return 0;                                           /* underflow to 0 */
    }
return (((t_uint64) r->sign) << FDR_V_SIGN) |
    (((t_uint64) r->exp) << FDR_V_EXP) |
    ((r->frac >> FDR_GUARD) & FDR_FRAC);
}
