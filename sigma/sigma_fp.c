/* sigma_fp.c: XDS Sigma floating point simulator

   Copyright (c) 2007-2008, Robert M Supnik

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
*/

#include "sigma_defs.h"

#define UFP_V_GUARD     4
#define UFP_NORM        (FP_NORM << UFP_V_GUARD)
#define UFP_CARRY       (UFP_NORM << 4)
#define UFP_FRHI        (UFP_CARRY|UFP_NORM|FP_M_FRHI)
#define UFP_FRLO        0xFFFFFFFF

/* Double precision fraction add/subtract/compare */
/* Note: UFP_ADD (s, r, r) will not work!!! */

#define UFP_ADD(s1,s2,d) do { \
                            d.l = (s1.l + s2.l) & UFP_FRLO; \
                            d.h = (s1.h + s2.h + (d.l < s2.l)) & UFP_FRHI; \
                            } while (0)

#define UFP_SUB(s1,s2,d) do { \
                            d.h = (s1.h - s2.h - (s1.l < s2.l)) & UFP_FRHI; \
                            d.l = (s1.l - s2.l) & UFP_FRLO; \
                            } while (0)

#define UFP_GE(s1,s2)   ((s1.h > s2.h) || \
                        ((s1.h == s2.h) && (s1.l >= s2.l)))

/* Variable and constant shifts; for constants, 0 < k < 32 */

#define UFP_RSH_V(v,s)  do { \
                            if ((s) < 32) { \
                                v.l = ((v.l >> (s)) | \
                                (   v.h << (32 - (s)))) & UFP_FRLO; \
                                v.h = v.h >> (s); \
                                } \
                            else if ((s) < 64) { \
                                v.l = v.h >> ((s) - 32); \
                                v.h = 0; \
                                } \
                            else v.l = v.h = 0; \
                            } while (0)

#define UFP_RSH_K(v,s)  do { \
                            v.l = ((v.l >> (s)) | \
                                (v.h << (32 - (s)))) & UFP_FRLO; \
                            v.h = v.h >> (s); \
                            } while (0)

#define UFP_LSH_K(v,s)  do { \
                            v.h = ((v.h << (s)) | \
                                (v.l >> (32 - (s)))) & UFP_FRHI; \
                            v.l = (v.l << (s)) & UFP_FRLO; \
                            } while (0)

#define UFP_RSH_KP(v,s) do { \
                            v->l = ((v->l >> (s)) | \
                                (v->h << (32 - (s)))) & UFP_FRLO; \
                            v->h = v->h >> (s); \
                            } while (0)

#define UFP_LSH_KP(v,s) do { \
                            v->h = ((v->h << (s)) | \
                                (v->l >> (32 - (s)))) & UFP_FRHI; \
                            v->l = (v->l << (s)) & UFP_FRLO; \
                            } while (0)

typedef struct {
    uint32      sign;
    int32       exp;
    uint32      h;
    uint32      l;
    } ufp_t;

extern uint32 *R;
extern uint32 PSW1;
extern uint32 CC;

void fp_unpack (uint32 hi, uint32 lo, ufp_t *dst);
t_bool fp_clnzro (ufp_t *src, t_bool abnorm);
uint32 fp_pack (ufp_t *src, uint32 rn, t_bool dbl, t_bool rndtrap);
uint32 fp_norm (ufp_t *src);

uint32 fp (uint32 op, uint32 rn, uint32 bva)
{
uint32 rh, rl, mh, ml, i, ediff, nsh;
t_bool s1nz, s2nz;
t_bool dbl = ((op & 0x20) == 0);
ufp_t fop1, fop2, t;
ufp_t res = { 0, 0, 0, 0 };
uint32 tr;

if (dbl) {                                              /* double prec? */
    rh = R[rn];                                         /* get reg operands */
    rl = R[rn|1];
    if ((tr = ReadD (bva, &mh, &ml, VR)) != 0)          /* get mem word */
        return tr;
    }
else {                                                  /* single precision */
    rh = R[rn];                                         /* pad to double */
    rl = 0;
    if ((tr = ReadW (bva, &mh, VR)) != 0)
        return tr;
    ml = 0;
    }
fp_unpack (rh, rl, &fop1);                              /* unpack, test */
fp_unpack (mh, ml, &fop2);
CC = 0;

switch (op) {                                           /* case on opcode */

    case OP_FSS:                                        /* subtract */
    case OP_FSL:
        fop2.sign = fop2.sign ^ 1;                      /* invert mem sign */
                                                        /* fall through */
    case OP_FAS:                                        /* add */
    case OP_FAL:
        s1nz = fp_clnzro (&fop1, TRUE);                 /* test, clean op1 */
        s2nz = fp_clnzro (&fop2, TRUE);
        if (!s1nz)                                      /* op1 = 0? res = op2 */
            res = fop2;
        else if (!s2nz)                                 /* op2 = 0? res = op1 */
            res = fop1;
        else {                                          /* both non-zero */
            if (fop1.exp < fop2.exp) {                  /* exp1 < exp2? */
                t = fop2;                               /* swap operands */
                fop2 = fop1;
                fop1 = t;
                }
            ediff = fop1.exp - fop2.exp;                /* exp difference */
            res.sign = fop1.sign;                       /* result sign, exp */
            res.exp = fop1.exp;
            if (ediff) {                                /* any difference? */
                UFP_RSH_V (fop2, ediff * 4);            /* shift frac */
                if (dbl) {                              /* double? */
                    if ((PSW1 & PSW1_FR) == 0)          /* rounding off? */
                        fop2.l &= ~0xF;                 /* no guard */
                    }
                else fop2.l = 0;                        /* single? clr lo */
                }
            if (fop1.sign ^ fop2.sign) {                /* eff subtract */
                if (UFP_GE (fop1, fop2)) {              /* fop1 >= fop2? */
                    UFP_SUB (fop1, fop2, res);          /* sub fractions */
                    }
                else {                                  /* fop2 > fop1 */
                    UFP_SUB (fop2, fop1, res);          /* rev subtract */
                    res.sign = fop2.sign;               /* change signs */
                    }
                }                                       /* end subtract */
            else {                                      /* eff add */
                UFP_ADD (fop1, fop2, res);              /* add fractions */
                if (res.h & UFP_CARRY) {                /* carry out? */
                    UFP_RSH_K (res, 4);                 /* renormalize */
                    res.exp = res.exp + 1;              /* incr exp */
                    }
                }                                       /* end add */
            }                                           /* end nz operands */
        if (!dbl)                                       /* single? clr lo */
            res.l = 0;
        if ((PSW1 & PSW1_FN) == 0) {                    /* postnormalize? */
            if ((res.h | res.l) == 0) {                 /* result zero? */
                CC = CC1;                               /* set signif flag */
                if (PSW1 & PSW1_FS)                     /* trap enabled? */
                    return TR_FLT;
                return fp_pack (&res, rn, dbl, FALSE);  /* pack up */
                }
            nsh = fp_norm (&res);                       /* normalize */
            if ((res.exp < 0) &&                        /* underflow? */
               !(PSW1 & PSW1_FZ) &&                     /* !FN */
                (PSW1 & PSW1_FS) &&                     /* FS */
                (nsh > 2)) {                            /* shifts > 2? */
                CC = CC1 | (res.sign? CC4: CC3);        /* signif CC's */
                return TR_FLT;                          /* trap */
                }                                       /* end if underflow */
            else if (nsh > 2) {                         /* shifts > 2? */
                CC |= CC1 | (res.sign? CC4: CC3);       /* set CC1 */
                if (PSW1 & PSW1_FS)                     /* trap enabled? */
                    return TR_FLT;
                }
            }                                           /* end if postnorm */
        return fp_pack (&res, rn, dbl, TRUE);           /* pack result */

    case OP_FMS:
    case OP_FML:                                        /* floating multiply */
        s1nz = fp_clnzro (&fop1, FALSE);                /* test, clean op1 */
        s2nz = fp_clnzro (&fop2, FALSE);
        if (s1nz && s2nz) {                             /* both non-zero? */
            fp_norm (&fop1);                            /* prenormalize */
            fp_norm (&fop2);
            UFP_RSH_K (fop2, 4);                        /* undo guard */
            res.sign = fop1.sign ^ fop2.sign;           /* result sign */
            res.exp = fop1.exp + fop2.exp - FP_BIAS;    /* result exp */
            if (!dbl) {                                 /* 24b x 24b? */
                for (i = 0; i < 24; i++) {              /* 24 iterations */
                    if (fop2.h & 1)
                        res.h = res.h + fop1.h;         /* add hi only */
                    UFP_RSH_K (res, 1);                 /* shift dp res */
                    fop2.h = fop2.h >> 1;
                    }
                res.l = 0;                              /* single prec */
                }
            else {                                      /* some low 0's */
                for (i = 0; i < 56; i++) {              /* 56 iterations */
                    if (fop2.l & 1) {
                        UFP_ADD (res, fop1, res);
                        }
                    UFP_RSH_K (res, 1);
                    UFP_RSH_K (fop2, 1);
                    }
                }
            fp_norm (&res);                             /* normalize result */
            }
        return fp_pack (&res, rn, dbl, TRUE);           /* pack result */

    case OP_FDS:
    case OP_FDL:                                        /* floating divide */
        s1nz = fp_clnzro (&fop1, FALSE);                /* test, clean op1 */
        s2nz = fp_clnzro (&fop2, FALSE);
        if (!s2nz) {                                    /* divide by zero? */
            CC = CC2;                                   /* set CC2 */
            return TR_FLT;                              /* trap */
            }
        if (s1nz) {                                     /* divd non-zero? */
            fp_norm (&fop1);                            /* prenormalize */
            fp_norm (&fop2);
            res.sign = fop1.sign ^ fop2.sign;           /* result sign */
            res.exp = fop1.exp - fop2.exp + FP_BIAS;    /* result exp */
            if (!UFP_GE (fop1, fop2)) {
                UFP_LSH_K (fop1, 4);                    /* ensure success */
                }
            else res.exp = res.exp + 1;                 /* incr exponent */
            for (i = 0; i < (uint32)(dbl? 15: 7); i++) {/* 7/15 hex digits */
                UFP_LSH_K (res, 4);                     /* shift quotient */
                while (UFP_GE (fop1, fop2)) {           /* while sub works */
                    UFP_SUB (fop1, fop2, fop1);         /* decrement */
                    res.l = res.l + 1;                  /* add quo bit */
                    }
                UFP_LSH_K (fop1, 4);                    /* shift divd */
                }                                       /* end hex loop */
            if (!dbl) {                                 /* single? */
                res.h = res.l;                          /* move quotient */
                res.l = 0;
                }
            fp_norm (&res);                             /* normalize result */
            }
        return fp_pack (&res, rn, dbl, TRUE);           /* pack result */
        }                                               /* end case */

return SCPE_IERR;
}

void ShiftF (uint32 rn, uint32 stype, uint32 sc)
{
uint32 opnd, opnd1;
ufp_t src;

opnd = R[rn];                                           /* get operands */
opnd1 = stype? R[rn|1]: 0;                              /* zextend single */
fp_unpack (opnd, opnd1, &src);                          /* unpack */

CC = 0;
if (sc & SCSIGN) {                                      /* right? */
    sc = SHF_M_SC + 1 - sc;
    while (sc > 0) {                                    /* while count */
        UFP_RSH_K (src, 4);                             /* shift right hex */
        if (stype)                                      /* zero guard */
            src.l &= ~0xF;
        else src.h &= ~0xF;
        src.exp++;                                      /* incr exp */
        sc--;
        if (src.exp > FP_M_EXP) {                       /* overflow? */
            CC |= CC2;                                  /* set CC2, stop */
            break;
            }
        }                                               /* end while */
    if ((src.h | src.l) == 0) {                         /* result 0? */
        if (stype)                                      /* result is true 0 */
            R[rn|1] = 0;
        R[rn] = 0;
        CC = 0;
        return;
        }
    }
else {                                                  /* left */
    if ((src.h | src.l) == 0) {                         /* fraction 0? */
        if (stype)                                      /* result is true 0 */
            R[rn|1] = 0;
        R[rn] = 0;
        CC = CC1;
        return;
        }
    while ((sc > 0) && ((src.h & UFP_NORM) == 0)) {     /* while count & !norm */
        UFP_LSH_K (src, 4);                             /* hex shift left */
        src.exp--;                                      /* decr exp */
        sc--;
        if (src.exp < 0) {                              /* underflow? */
            CC |= CC2;                                  /* set CC2, stop */
            break;
            }
        }                                               /* end while */
    if (src.h & UFP_NORM)                               /* normalized? */
        CC |= CC1;                                      /* set CC1 */
    }
fp_pack (&src, rn, stype, FALSE);                       /* pack result */
return;
}

void fp_unpack (uint32 hi, uint32 lo, ufp_t *dst)
{
dst->sign = FP_GETSIGN (hi);                            /* get sign */
if (dst->sign)                                          /* negative? */
    NEG_D (hi, lo);                                     /* 2's compl */
dst->h = FP_GETFRHI (hi);                               /* get fraction */
dst->l = FP_GETFRLO (lo);
dst->exp = FP_GETEXP (hi);                              /* get exp */
UFP_LSH_KP (dst, 4);                                    /* guard result */
return;
}

/* Test for and clean a floating point zero
   abnorm defines whether to allow "abnormal" zeros */

t_bool fp_clnzro (ufp_t *src, t_bool abnorm)
{
if (((src->h | src->l) == 0) &&                         /* frac zero and */
    (!abnorm || (src->exp == 0))) {                     /* exp zero or !ab */
    src->sign = 0;                                      /* true zero */
    src->exp = 0;
    return FALSE;
    }
return TRUE;                                            /* non-zero */
}

uint32 fp_pack (ufp_t *src, uint32 rn, t_bool dbl, t_bool rndtrap)
{
static ufp_t fp_zero = { 0, 0, 0, 0};
uint32 opnd, opnd1;

if (src->h || (dbl && src->l)) {                        /* result != 0? */
    CC |= (src->sign? CC4: CC3);                        /* set CC's */
    if (rndtrap) {                                      /* round, trap? */
        if (PSW1 & PSW1_FR) {                           /* round? */
            if (dbl) {                                  /* double prec? */
                src->l = (src->l + 0x8) & UFP_FRLO;
                src->h = src->h + (src->l < 0x8);
                }     
            else src->h = src->h + 0x8;                 /* no, single */
            if (src->h & UFP_CARRY) {                   /* carry out? */
                UFP_RSH_KP (src, 4);                    /* renormalize */
                src->exp = src->exp + 1;
                }
            }                                           /* end if round */
        if (src->exp > FP_M_EXP) {                      /* overflow? */
            CC |= CC2;                                  /* flag */
            return TR_FLT;
            }
        else if (src->exp < 0) {                        /* underflow? */
            if (PSW1 & PSW1_FZ) {                       /* trap enabled? */
                CC |= CC1 | CC2;                        /* flag */
                return TR_FLT;
                }
            *src = fp_zero;                             /* result 0 */
            CC = CC1|CC2;                               /* special CC's */
            }
        }                                               /* end rnd trap */
    UFP_RSH_KP (src, 4);                                /* remove guard */
    if (!dbl)                                           /* single? lose lower */
        src->l = 0;
    if ((src->h | src->l) == 0)                         /* result now 0? */
        src->exp = src->sign = 0;
    }
else *src = fp_zero;
opnd = ((src->exp & FP_M_EXP) << FP_V_EXP) |            /* repack */
    ((src->h & FP_M_FRHI) << FP_V_FRHI);
opnd1 = src->l & FP_M_FRLO;
if (src->sign)                                          /* negative? */
    NEG_D (opnd, opnd1);
R[rn] = opnd;                                           /* store result */
if (dbl && ((rn & 1) == 0))
    R[rn|1] = opnd1;
return 0;
}

uint32 fp_norm (ufp_t *src)
{
uint32 nsh;

nsh = 0;
src->h &= UFP_FRHI;
if (src->h || src->l) {                                 /* if non-zero */
    while ((src->h & UFP_NORM) == 0) {                  /* until normalized */
        UFP_LSH_KP (src, 4);                            /* hex shift left */
        src->exp--;                                     /* decr exponent */
        nsh++;                                          /* count shifts */
        }
    }
return nsh;
}

