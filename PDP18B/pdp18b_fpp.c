/* pdp18b_fpp.c: FP15 floating point processor simulator

   Copyright (c) 2003-2016, Robert M Supnik

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

   fpp          PDP-15 floating point processor

   07-Mar-16    RMS     Revised for dynamically allocated memory
   19-Mar-12    RMS     Fixed declaration of pc queue (Mark Pizzolato)
   06-Jul-06    RMS     Fixed bugs in left shift, multiply
   31-Oct-04    RMS     Fixed URFST to mask low 9b of fraction
                        Fixed exception PC setting
   10-Apr-04    RMS     JEA is 15b not 18b
 
   The FP15 instruction format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1  1  1  0  0  1|    subop  | microcoded modifiers  | floating point
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |in|                   address                        |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   Indirection is always single level.

   The FP15 supports four data formats:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | S|              2's complement integer              | A: integer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | S|             2's complement integer (high)        | A: extended integer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                2's complement integer (low)         | A+1
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |     fraction (low)       |SE|2's complement exponent| A: single floating
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |SF|                 fraction (high)                  | A+1
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |SE|             2's complement exponent              | A: double floating
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |SF|                 fraction (high)                  | A+1
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                    fraction (low)                   | A+2
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

*/

#include "pdp18b_defs.h"

/* Instruction */

#define FI_V_OP         8                               /* subopcode */
#define FI_M_OP         017
#define FI_GETOP(x)     (((x) >> FI_V_OP) & FI_M_OP)
#define FI_NOLOAD       0200                            /* don't load */
#define FI_DP           0100                            /* single/double */
#define FI_FP           0040                            /* int/flt point */
#define FI_NONORM       0020                            /* don't normalize */
#define FI_NORND        0010                            /* don't round */
#define FI_V_SGNOP      0                               /* A sign change */
#define FI_M_SGNOP      03
#define FI_GETSGNOP(x)  (((x) >> FI_V_SGNOP) & FI_M_SGNOP)

/* Exception register */

#define JEA_V_SIGN      17                              /* A sign */
#define JEA_V_GUARD     16                              /* guard */
#define JEA_EAMASK      077777                          /* exc address */
#define JEA_OFF_OVF     0                               /* ovf offset */
#define JEA_OFF_UNF     2                               /* unf offset */
#define JEA_OFF_DIV     4                               /* div offset */
#define JEA_OFF_MM      6                               /* mem mgt offset */

/* Status codes - must relate directly to JEA offsets */

#define FP_OK           0                               /* no error - mbz */
#define FP_OVF          (JEA_OFF_OVF + 1)               /* overflow */
#define FP_UNF          (JEA_OFF_UNF + 1)               /* underflow */
#define FP_DIV          (JEA_OFF_DIV + 1)               /* divide exception */
#define FP_MM           (JEA_OFF_MM + 1)                /* mem mgt error */

/* Unpacked floating point fraction */

#define UFP_FH_CARRY    0400000                         /* carry out */
#define UFP_FH_NORM     0200000                         /* normalized */
#define UFP_FH_MASK     0377777                         /* hi mask */
#define UFP_FL_MASK     0777777                         /* low mask */
#define UFP_FL_SMASK    0777000                         /* low mask, single */
#define UFP_FL_SRND     0000400                         /* round bit, single */

#define GET_SIGN(x)     (((x) >> 17) & 1)
#define SEXT18(x)       (((x) & SIGN)? ((x) | ~DMASK): ((x) & DMASK))
#define SEXT9(x)        (((x) & 0400)? ((x) | ~0377): ((x) & 0377))

enum fop {
    FOP_TST, FOP_SUB, FOP_RSUB, FOP_MUL,
    FOP_DIV, FOP_RDIV, FOP_LD, FOP_ST,
    FOP_FLT, FOP_FIX, FOP_LFMQ, FOP_JEA,
    FOP_ADD, FOP_BR, FOP_DIAG, FOP_UND
    };

typedef struct {
    int32               exp;                            /* exponent */
    int32               sign;                           /* sign */
    int32               hi;                             /* hi frac, 17b */
    int32               lo;                             /* lo frac, 18b */
    } UFP;

static int32 fir;                                       /* instruction */
static int32 jea;                                       /* exc address */
static int32 fguard;                                    /* guard bit */
static int32 stop_fpp = STOP_RSRV;                      /* stop if fp dis */
#define fma fma_X   /* Avoid name conflict with math.h defined fma() routine */
static UFP fma;                                         /* FMA */
static UFP fmb;                                         /* FMB */
static UFP fmq;                                         /* FMQ - hi,lo only */

extern int32 *M;
#if defined (PDP15)
extern int32 pcq[PCQ_SIZE];                             /* PC queue */
#else
extern int16 pcq[PCQ_SIZE];                             /* PC queue */
#endif
extern int32 pcq_p;
extern int32 PC;
extern int32 trap_pending, usmd;

t_stat fp15_reset (DEVICE *dptr);
t_stat fp15_opnd (int32 ir, int32 addr, UFP *a);
t_stat fp15_store (int32 ir, int32 addr, UFP *a);
t_stat fp15_iadd (int32 ir, UFP *a, UFP *b, t_bool sub);
t_stat fp15_imul (int32 ir, UFP *a, UFP *b);
t_stat fp15_idiv (int32 ir, UFP *a, UFP *b);
t_stat fp15_fadd (int32 ir, UFP *a, UFP *b, t_bool sub);
t_stat fp15_fmul (int32 ir, UFP *a, UFP *b);
t_stat fp15_fdiv (int32 ir, UFP *a, UFP *b);
t_stat fp15_fix (int32 ir, UFP *a);
t_stat fp15_norm (int32 ir, UFP *a, UFP *b, t_bool rnd);
t_stat fp15_exc (t_stat sta);
void fp15_asign (int32 ir, UFP *a);
void dp_add (UFP *a, UFP *b);
void dp_sub (UFP *a, UFP *b);
void dp_inc (UFP *a);
int32 dp_cmp (UFP *a, UFP *b);
void dp_mul (UFP *a, UFP *b);
void dp_lsh_1 (UFP *a, UFP *b);
void dp_rsh_1 (UFP *a, UFP *b);
void dp_dnrm_r (int32 ir, UFP *a, int32 sc);
void dp_swap (UFP *a, UFP *b);

extern t_stat Read (int32 ma, int32 *dat, int32 cyc);
extern t_stat Write (int32 ma, int32 dat, int32 cyc);
extern int32 Incr_addr (int32 addr);
extern int32 Jms_word (int32 t);

/* FPP data structures

   fpp_dev      FPP device descriptor
   fpp_unit     FPP unit
   fpp_reg      FPP register list
   fpp_mod      FPP modifier list
*/

UNIT fpp_unit = { UDATA (NULL, 0, 0) };

REG fpp_reg[] = {
    { ORDATAD (FIR, fir, 12, "floating instruction register") },
    { ORDATAD (EPA, fma.exp, 18, "EPA (A exponent") },
    { FLDATAD (FMAS, fma.sign, 0, "FMA sign") },
    { ORDATAD (FMAH, fma.hi, 17, "FMA<1:17>") },
    { ORDATAD (FMAL, fma.lo, 18, "FMA<18:35>") },
    { ORDATAD (EPB, fmb.exp, 18, "EPB (B exponent)") },
    { FLDATAD (FMBS, fmb.sign, 0, "FMB sign") },
    { ORDATAD (FMBH, fmb.hi, 17, "FMB<1:17>") },
    { ORDATAD (FMBL, fmb.lo, 18, "FMB<18:35>" ) },
    { FLDATAD (FGUARD, fguard, 0, "guard bit") },
    { ORDATAD (FMQH, fmq.hi, 17, "FMQ<1:17>") },
    { ORDATAD (FMQL, fmq.lo, 18, "FMQ<18:35>") },
    { ORDATAD (JEA, jea, 15, "exception address register") },
    { FLDATAD (STOP_FPP, stop_fpp, 0, "stop if FB15 instruction decoded while FB15 is disabled") },
    { NULL }
    };

DEVICE fpp_dev = {
    "FPP", &fpp_unit, fpp_reg, NULL,
    1, 8, 1, 1, 8, 18,
    NULL, NULL, &fp15_reset,
    NULL, NULL, NULL,
    NULL, DEV_DISABLE
    };

/* Instruction decode for FP15

   The CPU actually fetches the instruction and the word after.  If the
   instruction is 71XXXX, the CPU executes it as a NOP, and the FP15 fools
   the CPU into thinking that the second word is also a NOP.

   Indirect addresses are resolved during fetch, unless the NOLOAD modifier
   is set and the instruction is not a store. */

t_stat fp15 (int32 ir)
{
int32 ar, ma, fop, dat;
t_stat sta = FP_OK;

if (fpp_dev.flags & DEV_DIS)                            /* disabled? */
    return (stop_fpp? STOP_FPDIS: SCPE_OK);
fir = ir & 07777;                                       /* save subop + mods */
ma = PC;                                                /* fetch next word */
PC = Incr_addr (PC);
if (Read (ma, &ar, RD))                                 /* error? MM exc */
    return fp15_exc (FP_MM);
fop = FI_GETOP (fir);                                   /* get subopcode */
if ((ar & SIGN) &&                                      /* indirect? */
   ((fop == FOP_ST) || !(ir & FI_NOLOAD))) {            /* store or load? */
    ma = ar & AMASK;                                    /* fetch indirect */
    if (Read (ma, &ar, RD))
        return fp15_exc (FP_MM);
    }
fma.exp = SEXT18 (fma.exp);                             /* sext exponents */
fmb.exp = SEXT18 (fmb.exp);
switch (fop) {                                          /* case on subop */

    case FOP_TST:                                       /* NOP */
        break;

    case FOP_SUB:                                       /* subtract */
        if ((sta = fp15_opnd (fir, ar, &fmb)))          /* fetch op to FMB */
            break;
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_fadd (fir, &fma, &fmb, 1);       /* yes, fp sub */
        else sta = fp15_iadd (fir, &fma, &fmb, 1);      /* no, int sub */
        break;

    case FOP_RSUB:                                      /* reverse sub */
        fmb = fma;                                      /* FMB <- FMA */
        if ((sta = fp15_opnd (fir, ar, &fma)))          /* fetch op to FMA */
            break;
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_fadd (fir, &fma, &fmb, 1);       /* yes, fp sub */
        else sta = fp15_iadd (fir, &fma, &fmb, 1);      /* no, int sub */
        break;

    case FOP_MUL:                                       /* multiply */
        if ((sta = fp15_opnd (fir, ar, &fmb)))          /* fetch op to FMB */
            break;
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_fmul (fir, &fma, &fmb);          /* yes, fp mul */
        else sta = fp15_imul (fir, &fma, &fmb);         /* no, int mul */
        break;

    case FOP_DIV:                                       /* divide */
        if ((sta = fp15_opnd (fir, ar, &fmb)))          /* fetch op to FMB */
            break;
        if ((sta = fp15_opnd (fir, ar, &fmb)))break;    /* fetch op to FMB */
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_fdiv (fir, &fma, &fmb);          /* yes, fp div */
        else sta = fp15_idiv (fir, &fma, &fmb);         /* no, int div */
        break;

    case FOP_RDIV:                                      /* reverse divide */
        fmb = fma;                                      /* FMB <- FMA */
        if ((sta = fp15_opnd (fir, ar, &fma)))          /* fetch op to FMA */
            break;
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_fdiv (fir, &fma, &fmb);          /* yes, fp div */
        else sta = fp15_idiv (fir, &fma, &fmb);         /* no, int div */
        break;

    case FOP_LD:                                        /* load */
        if ((sta = fp15_opnd (fir, ar, &fma)))          /* fetch op to FMA */
            break;
        fp15_asign (fir, &fma);                         /* modify A sign */
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_norm (ir, &fma, NULL, 0);        /* norm, no round */
        break;

    case FOP_ST:                                        /* store */
        fp15_asign (fir, &fma);                         /* modify A sign */
        sta = fp15_store (fir, ar, &fma);               /* store result */
        break;

    case FOP_FLT:                                       /* float */
        if ((sta = fp15_opnd (fir, ar, &fma)))          /* fetch op to FMA */
            break;
        fma.exp = 35;
        fp15_asign (fir, &fma);                         /* adjust A sign */
        sta = fp15_norm (ir, &fma, NULL, 0);            /* norm, no found */
        break;

    case FOP_FIX:                                       /* fix */
        if ((sta = fp15_opnd (fir, ar, &fma)))          /* fetch op to FMA */
            break;
        sta = fp15_fix (fir, &fma);                     /* fix */
        break;

    case FOP_LFMQ:                                      /* load FMQ */
        if ((sta = fp15_opnd (fir, ar, &fma)))          /* fetch op to FMA */
            break;
        dp_swap (&fma, &fmq);                           /* swap FMA, FMQ */
        fp15_asign (fir, &fma);                         /* adjust A sign */
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_norm (ir, &fma, &fmq, 0);        /* yes, norm, no rnd */
        break;

    case FOP_JEA:                                       /* JEA */
        if (ir & 0200) {                                /* store? */
            dat = jea | (fma.sign << JEA_V_SIGN) | (fguard << JEA_V_GUARD);
            sta = Write (ar, dat, WR);
            }
        else {                                          /* no, load */
            if ((sta = Read (ar, &dat, RD)))
                break;
            fguard = (dat >> JEA_V_GUARD) & 1;
            jea = dat & JEA_EAMASK;
            }
        break;

    case FOP_ADD:                                       /* add */
        if ((sta = fp15_opnd (fir, ar, &fmb)))          /* fetch op to FMB */
            break;
        if (fir & FI_FP)                                /* fp? */
            sta = fp15_fadd (fir, &fma, &fmb, 0);       /* yes, fp add */
        else sta = fp15_iadd (fir, &fma, &fmb, 0);      /* no, int add */
        break;

    case FOP_BR:                                        /* branch */
        if (((fir & 001) && ((fma.hi | fma.lo) == 0)) ||
            ((fir & 002) && fma.sign) ||
            ((fir & 004) && !fma.sign) ||
            ((fir & 010) && ((fma.hi | fma.lo) != 0)) ||
            ((fir & 020) && fguard)) {                  /* cond met? */
            PCQ_ENTRY;                                  /* save current PC */
            PC = (PC & BLKMASK) | (ar & IAMASK);        /* branch within 32K */
            }
        break;

    default:
        break;
        }                                               /* end switch op */

fma.exp = fma.exp & DMASK;                              /* mask exp to 18b */
fmb.exp = fmb.exp & DMASK;
if (sta != FP_OK) return fp15_exc (sta);                /* error? */
return SCPE_OK;
}

/* Operand load and store */

t_stat fp15_opnd (int32 ir, int32 addr, UFP *fpn)
{
int32 i, numwd, wd[3];

fguard = 0;                                             /* clear guard */
if (ir & FI_NOLOAD)                                     /* no load? */
    return FP_OK;
if (ir & FI_FP)                                         /* fp? at least 2 */
    numwd = 2;
else numwd = 1;                                         /* else at least 1 */
if (ir & FI_DP)                                         /* dp? 1 more */
    numwd = numwd + 1;
for (i = 0; i < numwd; i++) {                           /* fetch words */
    if (Read (addr, &wd[i], RD))
        return FP_MM;
    addr = (addr + 1) & AMASK;
    }
if (ir & FI_FP) {                                       /* fp? */
    fpn->sign = GET_SIGN (wd[1]);                       /* frac sign */
    fpn->hi = wd[1] & UFP_FH_MASK;                      /* frac high */
    if (ir & FI_DP) {                                   /* dp? */
        fpn->exp = SEXT18 (wd[0]);                      /* exponent */
        fpn->lo = wd[2];                                /* frac low */
        }
    else {                                              /* sp */
        fpn->exp = SEXT9 (wd[0]);                       /* exponent */
        fpn->lo = wd[0] & UFP_FL_SMASK;                 /* frac low */
        }
    }
else {
    fpn->sign = GET_SIGN (wd[0]);                       /* int, get sign */
    if (ir & FI_DP) {                                   /* dp? */
        fpn->lo = wd[1];                                /* 2 words */
        fpn->hi = wd[0];
        }
    else {                                              /* single */
        fpn->lo = wd[0];                                /* 1 word */
        fpn->hi = fpn->sign? DMASK: 0;                  /* sign extended */
        }
    if (fpn->sign) {                                    /* negative? */
        fpn->lo = (-fpn->lo) & UFP_FL_MASK;             /* take abs val */
        fpn->hi = (~fpn->hi + (fpn->lo == 0)) & UFP_FH_MASK;
        }
    }
return FP_OK;
}

t_stat fp15_store (int32 ir, int32 addr, UFP *a)
{
int32 i, numwd, wd[3];
t_stat sta;

fguard = 0;                                             /* clear guard */
if (ir & FI_FP) {                                       /* fp? */
    if ((sta = fp15_norm (ir, a, NULL, 0)))             /* normalize */
        return sta;
    if (ir & FI_DP) {                                   /* dp? */
        wd[0] = a->exp & DMASK;                         /* exponent */
        wd[1] = (a->sign << 17) | a->hi;                /* hi frac */
        wd[2] = a->lo;                                  /* low frac */
        numwd = 3;                                      /* 3 words */
        }
    else {                                              /* single */
        if (!(ir & FI_NORND) && (a->lo & UFP_FL_SRND)) { /* round? */
            a->lo = (a->lo + UFP_FL_SRND) & UFP_FL_SMASK;
            a->hi = (a->hi + (a->lo == 0)) & UFP_FH_MASK;
            if ((a->hi | a->lo) == 0) {                 /* carry out? */
                a->hi = UFP_FH_NORM;                    /* shift back */
                a->exp = a->exp + 1;
                }
            }
        if (a->exp > 0377)                              /* sp ovf? */
            return FP_OVF;
        if (a->exp < -0400)                             /* sp unf? */
            return FP_UNF;
        wd[0] = (a->exp & 0777) | (a->lo & UFP_FL_SMASK); /* low frac'exp */
        wd[1] = (a->sign << 17) | a->hi;                /* hi frac */
        numwd = 2;                                      /* 2 words */
        }
    }
else {
    fmb.lo = (-a->lo) & UFP_FL_MASK;                    /* 2's complement */
    fmb.hi = (~a->hi + (fmb.lo == 0)) & UFP_FH_MASK;    /* to FMB */
    if (ir & FI_DP) {                                   /* dp? */
        if (a->sign) {                                  /* negative? */
            wd[0] = fmb.hi | SIGN;                      /* store FMB */
            wd[1] = fmb.lo;
            }
        else {                                          /* pos, store FMA */
            wd[0] = a->hi;
            wd[1] = a->lo;
            }
        numwd = 2;                                      /* 2 words */
        }
    else {                                              /* single */
        if (a->hi || (a->lo & SIGN))                    /* check int ovf */
            return FP_OVF;
        if (a->sign)                                    /* neg? store FMB */
            wd[0] = fmb.lo;
        else wd[0] = a->lo;                             /* pos, store FMA */
        numwd = 1;                                      /* 1 word */
        }
    }
for (i = 0; i < numwd; i++) {                           /* store words */
    if (Write (addr, wd[i], WR))
        return FP_MM;
    addr = (addr + 1) & AMASK;
    }
return FP_OK;
}

/* Integer arithmetic routines */

/* Integer add - overflow only on add, if carry out of high fraction */

t_stat fp15_iadd (int32 ir, UFP *a, UFP *b, t_bool sub)
{
fmq.hi = fmq.lo = 0;                                    /* clear FMQ */
if (a->sign ^ b->sign ^ sub)                            /* eff subtract? */
    dp_sub (a, b);
else {
    dp_add (a, b);                                      /* no, add */   
    if (a->hi & UFP_FH_CARRY) {                         /* carry out? */
        a->hi = a->hi & UFP_FH_MASK;                    /* mask to 35b */
        return FP_OVF;                                  /* overflow */
        }
    }
fp15_asign (ir, a);                                     /* adjust A sign */
return FP_OK;
}

/* Integer multiply - overflow if high result (FMQ after swap) non-zero */

t_stat fp15_imul (int32 ir, UFP *a, UFP *b)
{
a->sign = a->sign ^ b->sign;                            /* sign of result */
dp_mul (a, b);                                          /* a'FMQ <- a * b */
dp_swap (a, &fmq);                                      /* swap a, FMQ */
if (fmq.hi | fmq.lo)                                    /* FMQ != 0? ovf */
    return FP_OVF;
fp15_asign (ir, a);                                     /* adjust A sign */
return FP_OK;
}

/* Integer divide - actually done as fraction divide

   - If divisor zero, error
   - If dividend zero, done
   - Normalize dividend and divisor together
   - If divisor normalized but dividend not, result is zero
   - If divisor not normalized, normalize and count shifts
   - Do fraction divide for number of shifts, +1, steps
   
   Note that dp_lsh_1 returns a 72b result; the last right shift
   guarantees a 71b remainder.  The quotient cannot exceed 71b */

t_stat fp15_idiv (int32 ir, UFP *a, UFP *b)
{
int32 i, sc;

a->sign = a->sign ^ b->sign;                            /* sign of result */
fmq.hi = fmq.lo = 0;                                    /* clear quotient */
a->exp = 0;                                             /* clear a exp */
if ((b->hi | b->lo) == 0)                               /* div by 0? */
    return FP_DIV;
if ((a->hi | a->lo) == 0)                               /* div into 0? */
    return FP_OK;
while (((a->hi & UFP_FH_NORM) == 0) &&                  /* normalize divd */
    ((b->hi & UFP_FH_NORM) == 0)) {                     /* and divr */
    dp_lsh_1 (a, NULL);                                 /* lsh divd, divr */
    dp_lsh_1 (b, NULL);                                 /* can't carry out */
    }
if (!(a->hi & UFP_FH_NORM) && (b->hi & UFP_FH_NORM)) {  /* divr norm, divd not? */
    dp_swap (a, &fmq);                                  /* quo = 0 (fmq), rem = a */
    return FP_OK;
    }
while ((b->hi & UFP_FH_NORM) == 0) {                    /* normalize divr */
    dp_lsh_1 (b, NULL);                                 /* can't carry out */
    a->exp = a->exp + 1;                                /* count steps */
    }
sc = a->exp;
for (i = 0; i <= sc; i++) {                             /* n+1 steps */
    dp_lsh_1 (&fmq, NULL);                              /* left shift quo */
    if (dp_cmp (a, b) >= 0) {                           /* sub work? */
        dp_sub (a, b);                                  /* a -= b */
        if (i == 0)                                     /* first step? */
            a->exp = a->exp + 1;
        fmq.lo = fmq.lo | 1;                            /* set quo bit */
        }
    dp_lsh_1 (a, NULL);                                 /* left shift divd */
    }
dp_rsh_1 (a, NULL);                                     /* shift back */
dp_swap (a, &fmq);                                      /* swap a, FMQ */
fp15_asign (ir, a);                                     /* adjust A sign */
return FP_OK;
}

/* Floating point arithmetic routines */

/* Floating add
   - Special add case, overflow if carry out increments exp out of range
   - All cases, overflow/underflow detected in normalize */

t_stat fp15_fadd (int32 ir, UFP *a, UFP *b, t_bool sub)
{
int32 ediff;

fmq.hi = fmq.lo = 0;                                    /* clear FMQ */
ediff = a->exp - b->exp;                                /* exp diff */
if (((a->hi | a->lo) == 0) || (ediff < -35)) {          /* a = 0 or "small"? */
    *a = *b;                                            /* rslt is b */
    a->sign = a->sign ^ sub;                            /* or -b if sub */
    }
else if (((b->hi | b->lo) != 0) && (ediff <= 35)) {     /* b!=0 && ~"small"? */
    if (ediff > 0)                                      /* |a| > |b|? dnorm b */
        dp_dnrm_r (ir, b, ediff);
    else if (ediff < 0) {                               /* |a| < |b|? */
        a->exp = b->exp;                                /* b exp is rslt */
        dp_dnrm_r (ir, a, -ediff);                      /* denorm A */
        }
    if (a->sign ^ b->sign ^ sub)                        /* eff sub? */
        dp_sub (a, b);
    else {                                              /* eff add */
        dp_add (a, b);                                  /* add */
        if (a->hi & UFP_FH_CARRY) {                     /* carry out? */
            fguard = a->lo & 1;                         /* set guard */
            dp_rsh_1 (a, NULL);                         /* right shift */
            a->exp = a->exp + 1;                        /* incr exponent */
            if (!(ir & FI_NORND) && fguard)             /* rounding? */
                dp_inc (a);
            }
        }
    }                                                   /* end if b != 0 */
fp15_asign (ir, a);                                     /* adjust A sign */
return fp15_norm (ir, a, NULL, 0);                      /* norm, no round */
}

/* Floating multiply - overflow/underflow detected in normalize */

t_stat fp15_fmul (int32 ir, UFP *a, UFP *b)
{
a->sign = a->sign ^ b->sign;                            /* sign of result */
a->exp = a->exp + b->exp;                               /* exp of result */
dp_mul (a, b);                                          /* mul fractions */
fp15_asign (ir, a);                                     /* adjust A sign */
return fp15_norm (ir, a, &fmq, 1);                      /* norm and round */
}

/* Floating divide - overflow/underflow detected in normalize */

t_stat fp15_fdiv (int32 ir, UFP *a, UFP *b)
{
int32 i;

a->sign = a->sign ^ b->sign;                            /* sign of result */
a->exp = a->exp - b->exp;                               /* exp of result */
fmq.hi = fmq.lo = 0;                                    /* clear quotient */
if (!(b->hi & UFP_FH_NORM))                             /* divr not norm? */
    return FP_DIV;
if (a->hi | a->lo) {                                    /* divd non-zero? */
    fp15_norm (0, a, NULL, 0);                          /* normalize divd */
    for (i = 0; (fmq.hi & UFP_FH_NORM) == 0; i++) {     /* until quo */
        dp_lsh_1 (&fmq, NULL);                          /* left shift quo */
        if (dp_cmp (a, b) >= 0) {                       /* sub work? */
            dp_sub (a, b);                              /* a = a - b */
            if (i == 0)
                a->exp = a->exp + 1;
            fmq.lo = fmq.lo | 1;                        /* set quo bit */
            }
        dp_lsh_1 (a, NULL);                             /* left shift divd */
        }
    dp_rsh_1 (a, NULL);                                 /* shift back */
    dp_swap (a, &fmq);                                  /* swap a, FMQ */
    }
fp15_asign (ir, a);                                     /* adjust A sign */
return fp15_norm (ir, a, &fmq, 1);                      /* norm and round */
}

/* Floating to integer - overflow only if exponent out of range */

t_stat fp15_fix (int32 ir, UFP *a)
{
int32 i;

fmq.hi = fmq.lo = 0;                                    /* clear FMQ */
if (a->exp > 35)                                        /* exp > 35? ovf */
    return FP_OVF;
if (a->exp < 0)                                         /* exp <0 ? rslt 0 */
    a->hi = a->lo = 0;
else {
    for (i = a->exp; i < 35; i++)                       /* denorm frac */
        dp_rsh_1 (a, &fmq);
    if (fmq.hi & UFP_FH_NORM) {                         /* last out = 1? */
        fguard = 1;                                     /* set guard */
        if (!(ir & FI_NORND))                           /* round */
            dp_inc (a);
        }
    }
fp15_asign (ir, a);                                     /* adjust A sign */
return FP_OK;
}

/* Double precision routines */

/* Double precision add - returns 72b result (including carry) */

void dp_add (UFP *a, UFP *b)
{
a->lo = (a->lo + b->lo) & UFP_FL_MASK;                  /* add low */
a->hi = a->hi + b->hi + (a->lo < b->lo);                /* add hi + carry */
return;
}

/* Double precision increment - returns 72b result (including carry) */

void dp_inc (UFP *a)
{
a->lo = (a->lo + 1) & UFP_FL_MASK;                      /* inc low */
a->hi = a->hi + (a->lo == 0);                           /* propagate carry */
return;
}

/* Double precision subtract - result always fits in 71b */

void dp_sub (UFP *a, UFP *b)
{
if (dp_cmp (a,b) >= 0) {                                /* |a| >= |b|? */
    a->hi = (a->hi - b->hi - (a->lo < b->lo)) & UFP_FH_MASK;
    a->lo = (a->lo - b->lo) & UFP_FL_MASK;              /* a - b */
    }
else {
    a->hi = (b->hi - a->hi - (b->lo < a->lo)) & UFP_FH_MASK;
    a->lo = (b->lo - a->lo) & UFP_FL_MASK;              /* b - a */
    a->sign = a->sign ^ 1;                              /* change a sign */
    }
return;
}

/* Double precision compare - returns +1 (>), 0 (=), -1 (<) */

int32 dp_cmp (UFP *a, UFP *b)
{
if (a->hi < b->hi)
    return -1;
if (a->hi > b->hi) 
    return +1;
if (a->lo < b->lo)
    return -1;
if (a->lo > b->lo)
    return +1;
return 0;
}

/* Double precision multiply - returns 70b result in a'fmq */

void dp_mul (UFP *a, UFP *b)
{
int32 i;

fmq.hi = a->hi;                                         /* FMQ <- a */
fmq.lo = a->lo;
a->hi = a->lo = 0;                                      /* a <- 0 */
if ((fmq.hi | fmq.lo) == 0)
    return;
if ((b->hi | b->lo) == 0) {
    fmq.hi = fmq.lo = 0;
    return;
    }
for (i = 0; i < 35; i++) {                              /* 35 iterations */
    if (fmq.lo & 1)                                     /* FMQ<35>? a += b */
        dp_add (a, b);
    dp_rsh_1 (a, &fmq);                                 /* rsh a'FMQ */
    }
return;         
}

/* Double (quad) precision left shift - returns 72b (143b) result */

void dp_lsh_1 (UFP *a, UFP *b)
{
int32 t = b? b->hi: 0;

a->hi = (a->hi << 1) | ((a->lo >> 17) & 1);
a->lo = ((a->lo << 1) | ((t >> 16) & 1)) & UFP_FL_MASK;
if (b) {
    b->hi = ((b->hi << 1) | ((b->lo >> 17) & 1)) & UFP_FH_MASK;
    b->lo = (b->lo << 1) & UFP_FL_MASK;
    }
return;
}

/* Double (quad) precision right shift - returns 71b (142b) result */

void dp_rsh_1 (UFP *a, UFP *b)
{
if (b) {
    b->lo = (b->lo >> 1) | ((b->hi & 1) << 17);
    b->hi = (b->hi >> 1) | ((a->lo & 1) << 16);
    }
a->lo = (a->lo >> 1) | ((a->hi & 1) << 17);
a->hi = a->hi >> 1;
return;
}

/* Double precision denormalize and round - returns 71b result */

void dp_dnrm_r (int32 ir, UFP *a, int32 sc)
{
int32 i;

if (sc <= 0)                                            /* legit? */
    return;
for (i = 0; i < sc; i++)                                /* dnorm to fmq */
    dp_rsh_1 (a, &fmq);
if (!(ir & FI_NORND) && (fmq.hi & UFP_FH_NORM))         /* round & fmq<1>? */
    dp_inc (a);                                         /* incr a */
return;
}

/* Double precision swap */

void dp_swap (UFP *a, UFP *b)
{
int32 t;

t = a->hi;                                              /* swap fractions */
a->hi = b->hi;
b->hi = t;
t = a->lo;
a->lo = b->lo;
b->lo = t;
return;
}

/* Support routines */

void fp15_asign (int32 fir, UFP *a)
{
int32 sgnop = FI_GETSGNOP (fir);

switch (sgnop) {                                        /* modify FMA sign */

    case 1:
        a->sign = 0;
        break;

    case 2:
        a->sign = 1;
        break;

    case 3:
        a->sign = a->sign ^ 1;
        break;

    default:
        break;
        }

return;
}

/* FP15 normalization and rounding

   - Do normalization if enabled (NOR phase, part 1)
     Normalization also does zero detect
   - Do rounding if enabled (NOR phase, part 2) */

t_stat fp15_norm (int32 ir, UFP *a, UFP *b, t_bool rnd)
{
a->hi = a->hi & UFP_FH_MASK;                            /* mask a */
a->lo = a->lo & UFP_FL_MASK;
if (b) {                                                /* if b, mask */
    b->hi = b->hi & UFP_FH_MASK;
    b->lo = b->lo & UFP_FL_MASK;
    }
if (!(ir & FI_NONORM)) {                                /* norm enabled? */
    if ((a->hi | a->lo) || (b && (b->hi | b->lo))) {    /* frac != 0? */
        while ((a->hi & UFP_FH_NORM) == 0) {            /* until norm */
            dp_lsh_1 (a, b);                            /* lsh a'b, no cry */
            a->exp = a->exp - 1;                        /* decr exp */
            }
        }
    else a->sign = a->exp = 0;                          /* true zero */
    }
if (rnd && b && (b->hi & UFP_FH_NORM)) {                /* rounding? */
    fguard = 1;                                         /* set guard */
    if (!(ir & FI_NORND)) {                             /* round enabled? */
        dp_inc (a);                                     /* add 1 */
        if (a->hi & UFP_FH_CARRY) {                     /* carry out? */
            a->hi = UFP_FH_NORM;                        /* set hi bit */
            a->exp = a->exp + 1;                        /* incr exp */
            }
        }
    }
if (a->exp > (int32) 0377777)                           /* overflow? */
    return FP_OVF;
if (a->exp < (int32) -0400000)                          /* underflow? */
    return FP_UNF;
return FP_OK;
}

/* Exception */

t_stat fp15_exc (t_stat sta)
{
int32 ma, mb;

if (sta == FP_MM)                                       /* if mm, kill trap */
    trap_pending = 0;
ma = (jea & JEA_EAMASK) + sta - 1;                      /* JEA address */
PCQ_ENTRY;                                              /* record branch */
PC = Incr_addr (PC);                                    /* PC+1 for "JMS" */
mb = Jms_word (usmd);                                   /* form JMS word */
if (Write (ma, mb, WR))                                 /* store */
    return SCPE_OK;
PC = (ma + 1) & IAMASK;                                 /* new PC */
return SCPE_OK;
}
    
/* Reset routine */

t_stat fp15_reset (DEVICE *dptr)
{
jea = 0;
fir = 0;
fguard = 0;
fma.exp = fma.hi = fma.lo = fma.sign = 0;
fmb.exp = fmb.hi = fmb.lo = fmb.sign = 0;
fmq.exp = fmq.hi = fmq.lo = fmq.sign = 0;
return SCPE_OK;
}
