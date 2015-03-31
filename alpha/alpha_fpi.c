/* alpha_fpi.c - Alpha IEEE floating point simulator

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

        - single precision floating point, S
        - double precision floating point, T

   Portions of this module (specifically, the convert floating to integer
   routine and the square root routine) are a derivative work from SoftFloat,
   written by John Hauser.  SoftFloat includes the following license terms:

   Written by John R. Hauser.  This work was made possible in part by the
   International Computer Science Institute, located at Suite 600, 1947 Center
   Street, Berkeley, California 94704.  Funding was partially provided by the
   National Science Foundation under grant MIP-9311980.  The original version
   of this code was written as part of a project to build a fixed-point vector
   processor in collaboration with the University of California at Berkeley,
   overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
   is available through the Web page 'http://www.cs.berkeley.edu/~jhauser/
   arithmetic/SoftFloat.html'.

   THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort has
   been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT TIMES
   RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO PERSONS
   AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ALL LOSSES,
   COSTS, OR OTHER PROBLEMS THEY INCUR DUE TO THE SOFTWARE, AND WHO FURTHERMORE
   EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER SCIENCE
   INSTITUTE (possibly via similar legal warning) AGAINST ALL LOSSES, COSTS, OR
   OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND CLIENTS DUE TO THE SOFTWARE.

   Derivative works are acceptable, even for commercial purposes, so long as
   (1) the source code for the derivative work includes prominent notice that
   the work is derivative, and (2) the source code includes prominent notice with
   these four paragraphs for those parts of this code that are retained.
*/

#include "alpha_defs.h"

#define UFT_ZERO        0                               /* unpacked: zero */
#define UFT_FIN         1                               /* finite */
#define UFT_DENORM      2                               /* denormal */
#define UFT_INF         3                               /* infinity */
#define UFT_NAN         4                               /* not a number */

#define Q_FINITE(x)     ((x) <= UFT_FIN)                /* finite */
#define Q_SUI(x)        (((x) & I_FTRP) == I_FTRP_SVI)

/* Register format constants */

#define QNAN            0x0008000000000000              /* quiet NaN flag */
#define CQNAN           0xFFF8000000000000              /* canonical quiet NaN */
#define FPZERO          0x0000000000000000              /* plus zero (fp) */
#define FMZERO          0x8000000000000000              /* minus zero (fp) */
#define FPINF           0x7FF0000000000000              /* plus infinity (fp) */
#define FMINF           0xFFF0000000000000              /* minus infinity (fp) */
#define FPMAX           0x7FEFFFFFFFFFFFFF              /* plus MAX (fp) */
#define FMMAX           0xFFEFFFFFFFFFFFFF              /* minus MAX (fp) */
#define IPMAX           0x7FFFFFFFFFFFFFFF              /* plus MAX (int) */
#define IMMAX           0x8000000000000000              /* minus MAX (int) */

/* Unpacked rounding constants */

#define UF_SRND         0x0000008000000000              /* S normal round */
#define UF_SINF         0x000000FFFFFFFFFF              /* S infinity round */
#define UF_TRND         0x0000000000000400              /* T normal round */
#define UF_TINF         0x00000000000007FF              /* T infinity round */

extern t_uint64 FR[32];
extern uint32 fpcr;
extern jmp_buf save_env;

t_bool ieee_unpack (t_uint64 op, UFP *r, uint32 ir);
void ieee_norm (UFP *r);
t_uint64 ieee_rpack (UFP *r, uint32 ir, uint32 dp);
void ieee_trap (uint32 trap, uint32 instenb, uint32 fpcrdsb, uint32 ir);
int32 ieee_fcmp (t_uint64 a, t_uint64 b, uint32 ir, uint32 signal_nan);
t_uint64 ieee_cvtst (t_uint64 op, uint32 ir);
t_uint64 ieee_cvtts (t_uint64 op, uint32 ir);
t_uint64 ieee_cvtif (t_uint64 val, uint32 ir, uint32 dp);
t_uint64 ieee_cvtfi (t_uint64 op, uint32 ir);
t_uint64 ieee_fadd (t_uint64 a, t_uint64 b, uint32 ir, uint32 dp, t_bool sub);
t_uint64 ieee_fmul (t_uint64 a, t_uint64 b, uint32 ir, uint32 dp);
t_uint64 ieee_fdiv (t_uint64 a, t_uint64 b, uint32 ir, uint32 dp);
uint32 estimateSqrt32 (uint32 exp, uint32 a);
t_uint64 estimateDiv128 (t_uint64 hi, t_uint64 lo, t_uint64 dvr);

extern t_uint64 uemul64 (t_uint64 a, t_uint64 b, t_uint64 *hi);
extern t_uint64 ufdiv64 (t_uint64 dvd, t_uint64 dvr, uint32 prec, uint32 *sticky);
t_uint64 fsqrt64 (t_uint64 frac, int32 exp);

/* IEEE S load */

t_uint64 op_lds (t_uint64 op)
{
uint32 exp = S_GETEXP (op);                             /* get exponent */

if (exp == S_NAN) exp = FPR_NAN;                        /* inf or NaN? */
else if (exp != 0) exp = exp + T_BIAS - S_BIAS;         /* zero or denorm? */
return (((t_uint64) (op & S_SIGN))? FPR_SIGN: 0) |      /* reg format */
    (((t_uint64) exp) << FPR_V_EXP) |
    (((t_uint64) (op & ~(S_SIGN|S_EXP))) << S_V_FRAC);
}

/* IEEE S store */

t_uint64 op_sts (t_uint64 op)
{
uint32 sign = FPR_GETSIGN (op)? S_SIGN: 0;
uint32 frac = ((uint32) (op >> S_V_FRAC)) & M32;
uint32 exp = FPR_GETEXP (op);

if (exp == FPR_NAN) exp = S_NAN;                        /* inf or NaN? */
else if (exp != 0) exp = exp + S_BIAS - T_BIAS;         /* non-zero? */
exp = (exp & S_M_EXP) << S_V_EXP;
return (t_uint64) (sign | exp | (frac & ~(S_SIGN|S_EXP)));
}

/* IEEE floating operate */

void ieee_fop (uint32 ir)
{
UFP a, b;
uint32 ftpa, ftpb, fnc, ra, rb, rc;
t_uint64 res;

fnc = I_GETFFNC (ir);                                   /* get function */
ra = I_GETRA (ir);                                      /* get registers */
rb = I_GETRB (ir);
rc = I_GETRC (ir);
switch (fnc) {                                          /* case on func */

    case 0x00:                                          /* ADDS */
        res = ieee_fadd (FR[ra], FR[rb], ir, DT_S, 0);
        break;

    case 0x01:                                          /* SUBS */
        res = ieee_fadd (FR[ra], FR[rb], ir, DT_S, 1);
        break;

    case 0x02:                                          /* MULS */
        res = ieee_fmul (FR[ra], FR[rb], ir, DT_S);
        break;

    case 0x03:                                          /* DIVS */
        res = ieee_fdiv (FR[ra], FR[rb], ir, DT_S);
        break;

    case 0x20:                                          /* ADDT */
        res = ieee_fadd (FR[ra], FR[rb], ir, DT_T, 0);
        break;

    case 0x21:                                          /* SUBT */
        res = ieee_fadd (FR[ra], FR[rb], ir, DT_T, 1);
        break;

    case 0x22:                                          /* MULT */
        res = ieee_fmul (FR[ra], FR[rb], ir, DT_T);
        break;

    case 0x23:                                          /* DIVT */
        res = ieee_fdiv (FR[ra], FR[rb], ir, DT_T);
        break;

    case 0x24:                                          /* CMPTUN */
        ftpa = ieee_unpack (FR[ra], &a, ir);            /* unpack */
        ftpb = ieee_unpack (FR[rb], &b, ir);
        if ((ftpa == UFT_NAN) || (ftpb == UFT_NAN))     /* if NaN, T */
            res = FP_TRUE;
        else res = 0;
        break;

    case 0x25:                                          /* CMPTEQ */
        if (ieee_fcmp (FR[ra], FR[rb], ir, 0) == 0) res = FP_TRUE;
        else res = 0;
        break;

    case 0x26:                                          /* CMPTLT */
        if (ieee_fcmp (FR[ra], FR[rb], ir, 1) < 0) res = FP_TRUE;
        else res = 0;
        break;

    case 0x27:                                          /* CMPTLE */
        if (ieee_fcmp (FR[ra], FR[rb], ir, 1) <= 0) res = FP_TRUE;
        else res = 0;
        break;

    case 0x2C:                                          /* CVTST, CVTTS */
        if (ir & 0x2000) res = ieee_cvtst (FR[rb], ir); /* CVTST */
        else res = ieee_cvtts (FR[rb], ir);             /* CVTTS */
        break;

    case 0x2F:                                          /* CVTTQ */
        res = ieee_cvtfi (FR[rb], ir);
        break;

    case 0x3C:                                          /* CVTQS */
        res = ieee_cvtif (FR[rb], ir, DT_S);
        break;

    case 0x3E:                                          /* CVTQT */
        res = ieee_cvtif (FR[rb], ir, DT_T);
        break;

    default:
        if ((ir & I_FSRC) == I_FSRC_X) ABORT (EXC_RSVI);
        res = FR[rc];
        break;
        }

if (rc != 31) FR[rc] = res & M64;
return;
}

/* IEEE S to T convert - LDS doesn't handle denorms correctly */

t_uint64 ieee_cvtst (t_uint64 op, uint32 ir)
{
UFP b;
uint32 ftpb;

ftpb = ieee_unpack (op, &b, ir);                        /* unpack; norm dnorm */
if (ftpb == UFT_DENORM) {                               /* denormal? */
    b.exp = b.exp + T_BIAS - S_BIAS;                    /* change 0 exp to T */
    return ieee_rpack (&b, ir, DT_T);                   /* round, pack */
    }
else return op;                                         /* identity */
}

/* IEEE T to S convert */

t_uint64 ieee_cvtts (t_uint64 op, uint32 ir)
{
UFP b;
uint32 ftpb;

ftpb = ieee_unpack (op, &b, ir);                        /* unpack */
if (Q_FINITE (ftpb)) return ieee_rpack (&b, ir, DT_S);  /* finite? round, pack */
if (ftpb == UFT_NAN) return (op | QNAN);                /* nan? cvt to quiet */
if (ftpb == UFT_INF) return op;                         /* inf? unchanged */
return 0;                                               /* denorm? 0 */
}

/* IEEE floating compare

   - Take care of NaNs
   - Force -0 to +0
   - Then normal compare will work (even on inf and denorms) */

int32 ieee_fcmp (t_uint64 s1, t_uint64 s2, uint32 ir, uint32 trap_nan)
{
UFP a, b;
uint32 ftpa, ftpb;

ftpa = ieee_unpack (s1, &a, ir);
ftpb = ieee_unpack (s2, &b, ir);
if ((ftpa == UFT_NAN) || (ftpb == UFT_NAN)) {           /* NaN involved? */
    if (trap_nan) ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);
    return +1;                                          /* force failure */
    }
if (ftpa == UFT_ZERO) a.sign = 0;                       /* only +0 allowed */
if (ftpb == UFT_ZERO) b.sign = 0;
if (a.sign != b.sign) return (a.sign? -1: +1);          /* unequal signs? */
if (a.exp != b.exp) return ((a.sign ^ (a.exp < b.exp))? -1: +1);
if (a.frac != b.frac) return ((a.sign ^ (a.frac < b.frac))? -1: +1);
return 0;
}

/* IEEE integer to floating convert */

t_uint64 ieee_cvtif (t_uint64 val, uint32 ir, uint32 dp)
{
UFP a;

if (val == 0) return 0;                                 /* 0? return +0 */
if (val & FPR_SIGN) {                                   /* < 0? */
    a.sign = 1;                                         /* set sign */
    val = NEG_Q (val);                                  /* |val| */
    }
else a.sign = 0;
a.exp = 63 + T_BIAS;                                    /* set exp */
a.frac = val;                                           /* set frac */
ieee_norm (&a);                                         /* normalize */
return ieee_rpack (&a, ir, dp);                         /* round and pack */
}

/* IEEE floating to integer convert - rounding code from SoftFloat
   The Alpha architecture specifies return of the low order bits of
   the true result, whereas the IEEE standard specifies the return
   of the maximum plus or minus value */

t_uint64 ieee_cvtfi (t_uint64 op, uint32 ir)
{
UFP a;
t_uint64 sticky;
uint32 rndm, ftpa, ovf;
int32 ubexp;

ftpa = ieee_unpack (op, &a, ir);                        /* unpack */
if (!Q_FINITE (ftpa)) {                                 /* inf, NaN, dnorm? */
    ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);             /* inv operation */
    return 0;
    }
if (ftpa == UFT_ZERO) return 0;                         /* zero? */
ovf = 0;                                                /* assume no ovflo */
ubexp = a.exp - T_BIAS;                                 /* unbiased exp */
if (ubexp < 0) {                                        /* < 1? */
    if (ubexp == -1) sticky = a.frac;                   /* [.5,1)? */
    else sticky = 1;                                    /* (0,.5) */
    a.frac = 0;
    }
else if (ubexp < UF_V_NM) {                             /* in range? */
    sticky = (a.frac << (64 - (UF_V_NM - ubexp))) & M64;
    a.frac = a.frac >> (UF_V_NM - ubexp);               /* result */
    }
else if (ubexp == UF_V_NM) sticky = 0;                  /* at limit of range? */
else {
    if ((ubexp - UF_V_NM) > 63) a.frac = 0;             /* out of range */
    else a.frac = (a.frac << (ubexp - UF_V_NM)) & M64;
    ovf = 1;                                            /* overflow */
    sticky = 0;                                         /* no rounding */
    }
rndm = I_GETFRND (ir);                                  /* get round mode */
if (((rndm == I_FRND_N) && (sticky & Q_SIGN)) ||        /* nearest? */
    ((rndm == I_FRND_P) && !a.sign && sticky) ||        /* +inf and +? */
    ((rndm == I_FRND_M) && a.sign && sticky)) {         /* -inf and -? */
    a.frac = (a.frac + 1) & M64;
    if (a.frac == 0) ovf = 1;                           /* overflow? */
    if ((rndm == I_FRND_N) && (sticky == Q_SIGN))       /* round nearest hack */
        a.frac = a.frac & ~1;
    }
if (a.frac > (a.sign? IMMAX: IPMAX)) ovf = 1;           /* overflow? */
if (ovf) ieee_trap (TRAP_IOV, ir & I_FTRP_V, 0, 0);     /* overflow trap */
if (ovf || sticky)                                      /* ovflo or round? */
    ieee_trap (TRAP_INE, Q_SUI (ir), FPCR_INED, ir);
return (a.sign? NEG_Q (a.frac): a.frac);
}

/* IEEE floating add

   - Take care of NaNs and infinites
   - Test for zero (fast exit)
   - Sticky logic for floating add
        > If result normalized, sticky in right place
        > If result carries out, renormalize, retain sticky
   - Sticky logic for floating subtract
        > If shift < guard, no sticky bits; 64b result is exact
          If shift <= 1, result may require extensive normalization,
          but there are no sticky bits to worry about
        > If shift >= guard, there is a sticky bit,
          but normalization is at most 1 place, sticky bit is retained
          for rounding purposes (but not in low order bit) */

t_uint64 ieee_fadd (t_uint64 s1, t_uint64 s2, uint32 ir, uint32 dp, t_bool sub)
{
UFP a, b, t;
uint32 ftpa, ftpb;
uint32 sticky, rndm;
int32 ediff;

ftpa = ieee_unpack (s1, &a, ir);                        /* unpack operands */
ftpb = ieee_unpack (s2, &b, ir);
if (ftpb == UFT_NAN) return s2 | QNAN;                  /* B = NaN? quiet B */
if (ftpa == UFT_NAN) return s1 | QNAN;                  /* A = NaN? quiet A */
if (sub) b.sign = b.sign ^ 1;                           /* sign of B */
if (ftpb == UFT_INF) {                                  /* B = inf? */
    if ((ftpa == UFT_INF) && (a.sign ^ b.sign)) {       /* eff sub of inf? */
        ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);         /* inv op trap */
        return CQNAN;                                   /* canonical NaN */
        }
    return (sub? (s2 ^ FPR_SIGN): s2);                  /* return B */
    }
if (ftpa == UFT_INF) return s1;                         /* A = inf? ret A */
rndm = I_GETFRND (ir);                                  /* inst round mode */
if (rndm == I_FRND_D) rndm = FPCR_GETFRND (fpcr);       /* dynamic? use FPCR */
if (ftpa == UFT_ZERO) {                                 /* A = 0? */
    if (ftpb != UFT_ZERO) a = b;                        /* B != 0? result is B */
    else if (a.sign != b.sign)                          /* both 0, subtract? */
        a.sign = (rndm == I_FRND_M);                    /* +0 unless RM */
    }
else if (ftpb != UFT_ZERO) {                            /* s2 != 0? */
    if ((a.exp < b.exp) ||                              /* s1 < s2? swap */
       ((a.exp == b.exp) && (a.frac < b.frac))) {
        t = a;
        a = b;
        b = t;
        }
    ediff = a.exp - b.exp;                              /* exp diff */
    if (ediff > 63) b.frac = 1;                         /* >63? retain sticky */
    else if (ediff) {                                   /* [1,63]? shift */
        sticky = ((b.frac << (64 - ediff)) & M64)? 1: 0;        /* lost bits */
        b.frac = ((b.frac >> ediff) & M64) | sticky;
        }
    if (a.sign ^ b.sign) {                              /* eff sub? */
        a.frac = (a.frac - b.frac) & M64;               /* subtract fractions */
        if (a.frac == 0) {                              /* result 0? */
            a.exp = 0;
            a.sign = (rndm == I_FRND_M);                /* +0 unless RM */
            }
        else ieee_norm (&a);                            /* normalize */
        }
    else {                                              /* eff add */
        a.frac = (a.frac + b.frac) & M64;               /* add frac */
        if (a.frac < b.frac) {                          /* chk for carry */
            a.frac = UF_NM | (a.frac >> 1) |            /* shift in carry */
                (a.frac & 1);                           /* retain sticky */
            a.exp = a.exp + 1;                          /* skip norm */
            }
        }
    }                                                   /* end else if */
return ieee_rpack (&a, ir, dp);                         /* round and pack */
}

/* IEEE floating multiply 

   - Take care of NaNs and infinites
   - Test for zero operands (fast exit)
   - 64b x 64b fraction multiply, yielding 128b result
   - Normalize (at most 1 bit)
   - Insert "sticky" bit in low order fraction, for rounding
   
   Because IEEE fractions have a range of [1,2), the result can have a range
   of [1,4).  Results in the range of [1,2) appear to be denormalized by one
   place, when in fact they are correct.  Results in the range of [2,4) appear
   to be in correct, when in fact they are 2X larger.  This problem is taken
   care of in the result exponent calculation. */

t_uint64 ieee_fmul (t_uint64 s1, t_uint64 s2, uint32 ir, uint32 dp)
{
UFP a, b;
uint32 ftpa, ftpb;
t_uint64 resl;

ftpa = ieee_unpack (s1, &a, ir);                        /* unpack operands */
ftpb = ieee_unpack (s2, &b, ir);
if (ftpb == UFT_NAN) return s2 | QNAN;                  /* B = NaN? quiet B */
if (ftpa == UFT_NAN) return s1 | QNAN;                  /* A = NaN? quiet A */
a.sign = a.sign ^ b.sign;                               /* sign of result */
if ((ftpa == UFT_ZERO) || (ftpb == UFT_ZERO)) {         /* zero operand? */
    if ((ftpa == UFT_INF) || (ftpb == UFT_INF)) {       /* 0 * inf? */
        ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);         /* inv op trap */
        return CQNAN;                                   /* canonical NaN */
        }
    return (a.sign? FMZERO: FPZERO);                    /* return signed 0 */
    }
if (ftpb == UFT_INF) return (a.sign? FMINF: FPINF);     /* B = inf? */
if (ftpa == UFT_INF) return (a.sign? FMINF: FPINF);     /* A = inf? */
a.exp = a.exp + b.exp + 1 - T_BIAS;                     /* add exponents */
resl = uemul64 (a.frac, b.frac, &a.frac);               /* multiply fracs */
ieee_norm (&a);                                         /* normalize */
a.frac = a.frac | (resl? 1: 0);                         /* sticky bit */
return ieee_rpack (&a, ir, dp);                         /* round and pack */
}

/* Floating divide

   - Take care of NaNs and infinites
   - Check for zero cases
   - Divide fractions (55b to develop a rounding bit)
   - Set sticky bit if remainder non-zero
   
   Because IEEE fractions have a range of [1,2), the result can have a range
   of (.5,2).  Results in the range of [1,2) are correct.  Results in the
   range of (.5,1) need to be normalized by one place. */

t_uint64 ieee_fdiv (t_uint64 s1, t_uint64 s2, uint32 ir, uint32 dp)
{
UFP a, b;
uint32 ftpa, ftpb, sticky;

ftpa = ieee_unpack (s1, &a, ir);
ftpb = ieee_unpack (s2, &b, ir);
if (ftpb == UFT_NAN) return s2 | QNAN;                  /* B = NaN? quiet B */
if (ftpa == UFT_NAN) return s1 | QNAN;                  /* A = NaN? quiet A */
a.sign = a.sign ^ b.sign;                               /* sign of result */
if (ftpb == UFT_INF) {                                  /* B = inf? */
    if (ftpa == UFT_INF) {                              /* inf/inf? */
        ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);         /* inv op trap */
        return CQNAN;                                   /* canonical NaN */
        }
    return (a.sign? FMZERO: FPZERO);                    /* !inf/inf, ret 0 */
    }
if (ftpa == UFT_INF)                                    /* A = inf? */
    return (a.sign? FMINF: FPINF);                      /* return inf */
if (ftpb == UFT_ZERO) {                                 /* B = 0? */
    if (ftpa == UFT_ZERO) {                             /* 0/0? */
        ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);         /* inv op trap */
        return CQNAN;                                   /* canonical NaN */
        }
    ieee_trap (TRAP_DZE, 1, FPCR_DZED, ir);             /* div by 0 trap */
    return (a.sign? FMINF: FPINF);                      /* return inf */
    }
if (ftpa == UFT_ZERO) return (a.sign? FMZERO: FPZERO);  /* A = 0? */
a.exp = a.exp - b.exp + T_BIAS;                         /* unbiased exp */
a.frac = a.frac >> 1;                                   /* allow 1 bit left */
b.frac = b.frac >> 1;
a.frac = ufdiv64 (a.frac, b.frac, 55, &sticky);         /* divide */
ieee_norm (&a);                                         /* normalize */
a.frac = a.frac | sticky;                               /* insert sticky */
return ieee_rpack (&a, ir, dp);                         /* round and pack */
}

/* IEEE floating square root

   - Take care of NaNs, +infinite, zero
   - Check for negative operand
   - Compute result exponent
   - Compute sqrt of fraction */

t_uint64 ieee_sqrt (uint32 ir, uint32 dp)
{
t_uint64 op;
uint32 ftpb;
UFP b;

op = FR[I_GETRB (ir)];                                  /* get F[rb] */
ftpb = ieee_unpack (op, &b, ir);                        /* unpack */
if (ftpb == UFT_NAN) return op | QNAN;                  /* NaN? */
if ((ftpb == UFT_ZERO) ||                               /* zero? */
    ((ftpb == UFT_INF) && !b.sign)) return op;          /* +infinity? */
if (b.sign) {                                           /* minus? */
    ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);             /* signal inv op */
    return CQNAN;
    }
b.exp = ((b.exp - T_BIAS) >> 1) + T_BIAS - 1;           /* result exponent */
b.frac = fsqrt64 (b.frac, b.exp);                       /* result fraction */
return ieee_rpack (&b, ir, dp);                         /* round and pack */
}

/* Support routines */

t_bool ieee_unpack (t_uint64 op, UFP *r, uint32 ir)
{
r->sign = FPR_GETSIGN (op);                             /* get sign */
r->exp = FPR_GETEXP (op);                               /* get exponent */
r->frac = FPR_GETFRAC (op);                             /* get fraction */
if (r->exp == 0) {                                      /* exponent = 0? */
    if (r->frac == 0) return UFT_ZERO;                  /* frac = 0? then true 0 */
    if (fpcr & FPCR_DNZ) {                              /* denorms to 0? */
        r->frac = 0;                                    /* clear fraction */
        return UFT_ZERO;
        }
    r->frac = r->frac << FPR_GUARD;                     /* guard fraction */
    ieee_norm (r);                                      /* normalize dnorm */
    ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);             /* signal inv op */
    return UFT_DENORM;
    }
if (r->exp == FPR_NAN) {                                /* exponent = max? */
    if (r->frac == 0) return UFT_INF;                   /* frac = 0? then inf */
    if (!(r->frac & QNAN))                              /* signaling NaN? */
        ieee_trap (TRAP_INV, 1, FPCR_INVD, ir);         /* signal inv op */
    return UFT_NAN;
    }
r->frac = (r->frac | FPR_HB) << FPR_GUARD;              /* ins hidden bit, guard */
return UFT_FIN;                                         /* finite */
}

/* Normalize - input must be zero, finite, or denorm */

void ieee_norm (UFP *r)
{
int32 i;
static t_uint64 normmask[5] = {
    0xc000000000000000, 0xf000000000000000, 0xff00000000000000,
    0xffff000000000000, 0xffffffff00000000
    };
static int32 normtab[6] = { 1, 2, 4, 8, 16, 32 };

r->frac = r->frac & M64;
if (r->frac == 0) {                                     /* if fraction = 0 */
    r->sign = 0;
    r->exp = 0;                                         /* result is 0 */
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

/* Round and pack

   Much of the treachery of the IEEE standard is buried here
   - Rounding modes (chopped, +infinity, nearest, -infinity)
   - Inexact (set if there are any rounding bits, regardless of rounding)
   - Overflow (result is infinite if rounded, max if not)
   - Underflow (no denorms!)
   
   Underflow handling is particularly complicated
   - Result is always 0
   - UNF and INE are always set in FPCR
   - If /U is set,
     o If /S is clear, trap
     o If /S is set, UNFD is set, but UNFZ is clear, ignore UNFD and
       trap, because the hardware cannot produce denormals
     o If /S is set, UNFD is set, and UNFZ is set, do not trap
   - If /SUI is set, and INED is clear, trap */

t_uint64 ieee_rpack (UFP *r, uint32 ir, uint32 dp)
{
static const t_uint64 stdrnd[2] = { UF_SRND, UF_TRND };
static const t_uint64 infrnd[2] = { UF_SINF, UF_TINF };
static const int32 expmax[2] = { T_BIAS - S_BIAS + S_M_EXP - 1, T_M_EXP - 1 };
static const int32 expmin[2] = { T_BIAS - S_BIAS, 0 };
t_uint64 rndadd, rndbits, res;
uint32 rndm;

if (r->frac == 0)                                       /* result 0? */
    return ((t_uint64) r->sign << FPR_V_SIGN);
rndm = I_GETFRND (ir);                                  /* inst round mode */
if (rndm == I_FRND_D) rndm = FPCR_GETFRND (fpcr);       /* dynamic? use FPCR */
rndbits = r->frac & infrnd[dp];                         /* isolate round bits */
if (rndm == I_FRND_N) rndadd = stdrnd[dp];              /* round to nearest? */
else if (((rndm == I_FRND_P) && !r->sign) ||            /* round to inf and */
    ((rndm == I_FRND_M) && r->sign))                    /* right sign? */
    rndadd = infrnd[dp];
else rndadd = 0;
r->frac = (r->frac + rndadd) & M64;                     /* round */
if ((r->frac & UF_NM) == 0) {                           /* carry out? */
    r->frac = (r->frac >> 1) | UF_NM;                   /* renormalize */
    r->exp = r->exp + 1;
    }
if (rndbits)                                            /* inexact? */
    ieee_trap (TRAP_INE, Q_SUI (ir), FPCR_INED, ir);    /* set inexact */
if (r->exp > expmax[dp]) {                              /* ovflo? */
    ieee_trap (TRAP_OVF, 1, FPCR_OVFD, ir);             /* set overflow trap */
    ieee_trap (TRAP_INE, Q_SUI (ir), FPCR_INED, ir);    /* set inexact */
    if (rndadd)                                         /* did we round? */
        return (r->sign? FMINF: FPINF);                 /* return infinity */
    return (r->sign? FMMAX: FPMAX);                     /* no, return max */
    }
if (r->exp <= expmin[dp]) {                             /* underflow? */
    ieee_trap (TRAP_UNF, ir & I_FTRP_U,                 /* set underflow trap */
        (fpcr & FPCR_UNDZ)? FPCR_UNFD: 0, ir);          /* (dsbl only if UNFZ set) */
    ieee_trap (TRAP_INE, Q_SUI (ir), FPCR_INED, ir);    /* set inexact */
    return 0;                                           /* underflow to +0 */
    }
res = (((t_uint64) r->sign) << FPR_V_SIGN) |            /* form result */
    (((t_uint64) r->exp) << FPR_V_EXP) |
    ((r->frac >> FPR_GUARD) & FPR_FRAC);
if ((rndm == I_FRND_N) && (rndbits == stdrnd[dp]))      /* nearest and halfway? */
    res = res & ~1;                                     /* clear lo bit */
return res;
}

/* IEEE arithmetic trap - only one can be set at a time! */

void ieee_trap (uint32 trap, uint32 instenb, uint32 fpcrdsb, uint32 ir)
{
fpcr = fpcr | (trap << 19);                             /* FPCR to trap summ offset */
if ((instenb == 0) ||                                   /* not enabled in inst? ignore */
    ((ir & I_FTRP_S) && (fpcr & fpcrdsb))) return;      /* /S and disabled? ignore */
arith_trap (trap, ir);                                  /* set Alpha trap */
return;
}

/* Fraction square root routine - code from SoftFloat */

t_uint64 fsqrt64 (t_uint64 asig, int32 exp)
{
t_uint64 zsig, remh, reml, t;
uint32 sticky = 0;

zsig = estimateSqrt32 (exp, (uint32) (asig >> 32));

/* Calculate the final answer in two steps.  First, do one iteration of
   Newton's approximation.  The divide-by-2 is accomplished by clever
   positioning of the operands.  Then, check the bits just below the
   (double precision) rounding bit to see if they are close to zero
   (that is, the rounding bits are close to midpoint).  If so, make
   sure that the result^2 is <below> the input operand */

asig = asig >> ((exp & 1)? 3: 2);                       /* leave 2b guard */
zsig = estimateDiv128 (asig, 0, zsig << 32) + (zsig << 30 );
if ((zsig & 0x1FF) <= 5) {                              /* close to even? */
    reml = uemul64 (zsig, zsig, &remh);                 /* result^2 */
    remh = asig - remh - (reml? 1:0);                   /* arg - result^2 */
    reml = NEG_Q (reml);
    while (Q_GETSIGN (remh) != 0) {                     /* if arg < result^2 */
        zsig = zsig - 1;                                /* decr result */
        t = (zsig << 1) | 1;                            /* incr result^2 */
        reml = reml + t;                                /* and retest */
        remh = remh + (zsig >> 63) + ((reml < t)? 1: 0);
        }
    if ((remh | reml) != 0 ) sticky = 1;                /* not exact? */
    }
return zsig;
}

/* Estimate 32b SQRT

   Calculate an approximation to the square root of the 32-bit significand given
   by 'a'.  Considered as an integer, 'a' must be at least 2^31.  If bit 0 of
   'exp' (the least significant bit) is 1, the integer returned approximates
   2^31*sqrt('a'/2^31), where 'a' is considered an integer.  If bit 0 of 'exp'
   is 0, the integer returned approximates 2^31*sqrt('a'/2^30).  In either
   case, the approximation returned lies strictly within +/-2 of the exact
   value. */

uint32 estimateSqrt32 (uint32 exp, uint32 a)
{
uint32 index, z;
static const uint32 sqrtOdd[] = {
    0x0004, 0x0022, 0x005D, 0x00B1, 0x011D, 0x019F, 0x0236, 0x02E0,
    0x039C, 0x0468, 0x0545, 0x0631, 0x072B, 0x0832, 0x0946, 0x0A67
    };
static const uint32 sqrtEven[] = {
    0x0A2D, 0x08AF, 0x075A, 0x0629, 0x051A, 0x0429, 0x0356, 0x029E,
    0x0200, 0x0179, 0x0109, 0x00AF, 0x0068, 0x0034, 0x0012, 0x0002
    };

index = (a >> 27) & 0xF;                                /* bits<30:27> */
if (exp & 1) {                                          /* odd exp? */
    z = 0x4000 + (a >> 17) - sqrtOdd[index];            /* initial guess */
    z = ((a / z) << 14) + (z << 15);                    /* Newton iteration */
    a = a >> 1;
    }
else {
    z = 0x8000 + (a >> 17) - sqrtEven[index];           /* initial guess */
    z = (a / z) + z;                                    /* Newton iteration */
    z = (z >= 0x20000) ? 0xFFFF8000: (z << 15);
    if (z <= a) z = (a >> 1) | 0x80000000;
    }
return (uint32) ((((((t_uint64) a) << 31) / ((t_uint64) z)) + (z >> 1)) & M32);
}

/* Estimate 128b unsigned divide */

t_uint64 estimateDiv128 (t_uint64 a0, t_uint64 a1, t_uint64 b)
{
t_uint64 b0, b1;
t_uint64 rem0, rem1, term0, term1;
t_uint64 z;

if (b <= a0) return 0xFFFFFFFFFFFFFFFF;
b0 = b >> 32;
z = ((b0 << 32) <= a0)? 0xFFFFFFFF00000000: ((a0 / b0) << 32);
term1 = uemul64 (b, z, &term0);
rem0 = a0 - term0 - (a1 < term1);
rem1 = a1 - term1;
while (Q_GETSIGN (rem0)) {
    z = z - ((t_uint64) 0x100000000);
    b1 = b << 32;
    rem1 = b1 + rem1;
    rem0 = b0 + rem0 + (rem1 < b1);
    }
rem0 = (rem0 << 32) | (rem1 >> 32);
z |= (((b0 << 32) <= rem0)? 0xFFFFFFFF : (rem0 / b0));
return z;
}
