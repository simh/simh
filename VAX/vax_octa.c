/* vax_octa.c - VAX octaword and h_floating instructions

   Copyright (c) 2004-2011, Robert M Supnik

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

   This module simulates the VAX h_floating instruction set.

   15-Sep-11    RMS     Fixed integer overflow bug in EMODH
                        Fixed POLYH normalizing before add mask bug
                        (Camiel Vanderhoeven)
   28-May-08    RMS     Inlined physical memory routines
   10-May-06    RMS     Fixed bug in reported VA on faulting cross-page write
   03-May-06    RMS     Fixed MNEGH to test negated sign, clear C
                        Fixed carry propagation in qp_inc, qp_neg, qp_add
                        Fixed pack routines to test for zero via fraction
                        Fixed ACBH to set cc's on result
                        Fixed POLYH to set R3 correctly
                        Fixed POLYH to not exit prematurely if arg = 0
                        Fixed POLYH to mask mul reslt to 127b
                        Fixed fp add routine to test for zero via fraction
                         to support "denormal" argument from POLYH
                        Fixed EMODH to concatenate 15b of 16b extension
                        (Tim Stark)
   15-Jul-04    RMS     Cloned from 32b VAX floating point implementation
*/

#include "vax_defs.h"

#define WORDSWAP(x)     ((((x) & WMASK) << 16) | (((x) >> 16) & WMASK))

typedef struct {
    uint32              f0;                             /* low */
    uint32              f1;
    uint32              f2;
    uint32              f3;                             /* high */
    } UQP;

typedef struct {
    int32               sign;
    int32               exp;
    UQP                 frac;
    } UFPH;

#define UH_NM_H         0x80000000                      /* normalized */
#define UH_FRND         0x00000080                      /* F round */
#define UH_DRND         0x00000080                      /* D round */
#define UH_GRND         0x00000400                      /* G round */
#define UH_HRND         0x00004000                      /* H round */
#define UH_V_NM         127

int32 op_tsth (int32 val);
int32 op_cmph (int32 *hf1, int32 *hf2);
int32 op_cvtih (int32 val, int32 *hf);
int32 op_cvthi (int32 *hf, int32 *flg, int32 opc);
int32 op_cvtfdh (int32 vl, int32 vh, int32 *hf);
int32 op_cvtgh (int32 vl, int32 vh, int32 *hf);
int32 op_cvthfd (int32 *hf, int32 *vh);
int32 op_cvthg (int32 *hf, int32 *vh);
int32 op_addh (int32 *opnd, int32 *hf, t_bool sub);
int32 op_mulh (int32 *opnd, int32 *hf);
int32 op_divh (int32 *opnd, int32 *hf);
int32 op_emodh (int32 *opnd, int32 *hflt, int32 *intgr, int32 *flg);
void op_polyh (int32 *opnd, int32 acc);
void h_write_b (int32 spec, int32 va, int32 val, int32 acc, InstHistory *hst);
void h_write_w (int32 spec, int32 va, int32 val, int32 acc, InstHistory *hst);
void h_write_l (int32 spec, int32 va, int32 val, int32 acc, InstHistory *hst);
void h_write_q (int32 spec, int32 va, int32 vl, int32 vh, int32 acc, InstHistory *hst);
void h_write_o (int32 spec, int32 va, int32 *val, int32 acc, InstHistory *hst);
void vax_hadd (UFPH *a, UFPH *b, uint32 mlo);
void vax_hmul (UFPH *a, UFPH *b, uint32 mlo);
void vax_hmod (UFPH *a, int32 *intgr, int32 *flg);
void vax_hdiv (UFPH *a, UFPH *b);
uint32 qp_add (UQP *a, UQP *b);
uint32 qp_sub (UQP *a, UQP *b);
void qp_inc (UQP *a);
void qp_lsh (UQP *a, uint32 sc);
void qp_rsh (UQP *a, uint32 sc);
void qp_rsh_s (UQP *a, uint32 sc, uint32 neg);
void qp_neg (UQP *a);
int32 qp_cmp (UQP *a, UQP *b);
void h_unpackfd (int32 hi, int32 lo, UFPH *a);
void h_unpackg (int32 hi, int32 lo, UFPH *a);
void h_unpackh (int32 *hflt, UFPH *a);
void h_normh (UFPH *a);
int32 h_rpackfd (UFPH *a, int32 *rl);
int32 h_rpackg (UFPH *a, int32 *rl);
int32 h_rpackh (UFPH *a, int32 *hflt);

static int32 z_octa[4] = { 0, 0, 0, 0 };

/* Octaword instructions */

int32 op_octa (int32 *opnd, int32 cc, int32 opc, int32 acc, int32 spec, int32 va, InstHistory *hst)
{
int32 r, rh, temp, flg;
int32 r_octa[4];

if ((cpu_instruction_set & VAX_EXTAC) == 0) {      /* Implemented? */
    RSVD_INST_FAULT(opc);
    return cc;
    }

switch (opc) {

/* PUSHAO

        opnd[0] =       src.ao
*/

    case PUSHAO:
        Write (SP - 4, opnd[0], L_LONG, WA);            /* push operand */
        SP = SP - 4;                                    /* decr stack ptr */
        CC_IIZP_L (opnd[0]);                            /* set cc's */
        break;

/* MOVAO

        opnd[0] =       src.ro
        opnd[1:2] =     dst.wl
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case MOVAO:
        h_write_l (spec, va, opnd[0], acc, hst);        /* write operand */
        CC_IIZP_L (opnd[0]);                            /* set cc's */
        break;

/* CLRO

        opnd[0:1] =     dst.wl
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case CLRO:
        h_write_o (spec, va, z_octa, acc, hst);         /* write 0's */
        CC_ZZ1P;                                        /* set cc's */
        break;

/* TSTH

        opnd[0:3] =     src.rh
*/

    case TSTH:
        r = op_tsth (opnd[0]);                          /* test for 0 */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

/* MOVO, MOVH, MNEGH

        opnd[0:3] =     src.ro
        opnd[4:5] =     dst.wo
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case MOVO:
        h_write_o (spec, va, opnd, acc, hst);           /* write src */
        CC_IIZP_O (opnd[0], opnd[1], opnd[2], opnd[3]); /* set cc's */
        break;

    case MOVH:
        if ((r = op_tsth (opnd[0]))) {                  /* test for 0 */
            h_write_o (spec, va, opnd, acc, hst);       /* nz, write result */
            CC_IIZP_FP (r);                             /* set cc's */
            }
        else {                                          /* zero */
            h_write_o (spec, va, z_octa, acc, hst);     /* write 0 */
            cc = (cc & CC_C) | CC_Z;                    /* set cc's */
            }
        break;

    case MNEGH:
        if ((r = op_tsth (opnd[0]))) {                  /* test for 0 */
            opnd[0] = opnd[0] ^ FPSIGN;                 /* nz, invert sign */
            h_write_o (spec, va, opnd, acc, hst);       /* write result */
            CC_IIZZ_FP (opnd[0]);                       /* set cc's */
            }
        else {                                          /* zero */
            h_write_o (spec, va, z_octa, acc, hst);     /* write 0 */
            cc = CC_Z;                                  /* set cc's */
            }
        break;

/* CMPH

        opnd[0:3] =     src1.rh
        opnd[4:7] =     src2.rh
*/

    case CMPH:
        cc = op_cmph (opnd + 0, opnd + 4);              /* set cc's */
        break;

/* CVTBH, CVTWH, CVTLH

        opnd[0] =       src.rx
        opnd[1:2] =     dst.wh
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case CVTBH:
        r = op_cvtih (SXTB (opnd[0]), r_octa);          /* convert */
        h_write_o (spec, va, r_octa, acc, hst);         /* write reslt */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case CVTWH:
        r = op_cvtih (SXTW (opnd[0]), r_octa);          /* convert */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case CVTLH:
        r = op_cvtih (opnd[0], r_octa);                 /* convert */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

/* CVTHB, CVTHW, CVTHL, CVTRHL

        opnd[0:3] =     src.rh
        opnd[4:5] =     dst.wx
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case CVTHB:
        r = op_cvthi (opnd, &flg, opc) & BMASK;         /* convert */
        h_write_b (spec, va, r, acc, hst);              /* write result */
        CC_IIZZ_B (r);                                  /* set cc's */
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTHW:
        r = op_cvthi (opnd, &flg, opc) & WMASK;         /* convert */
        h_write_w (spec, va, r, acc, hst);              /* write result */
        CC_IIZZ_W (r);                                  /* set cc's */
        if (flg) {
            V_INTOV;
            }
        break;

    case CVTHL: case CVTRHL:
        r = op_cvthi (opnd, &flg, opc) & LMASK;         /* convert */
        h_write_l (spec, va, r, acc, hst);              /* write result */
        CC_IIZZ_L (r);                                  /* set cc's */
        if (flg) {
            V_INTOV;
            }
        break;

/* CVTFH

        opnd[0] =       src.rf
        opnd[1:2] =     dst.wh
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case CVTFH:
        r = op_cvtfdh (opnd[0], 0, r_octa);             /* convert */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

/* CVTDH, CVTGH

        opnd[0:1] =     src.rx
        opnd[2:3] =     dst.wh
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case CVTDH:
        r = op_cvtfdh (opnd[0], opnd[1], r_octa);       /* convert */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case CVTGH:
        r = op_cvtgh (opnd[0], opnd[1], r_octa);        /* convert */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

/* CVTHF, CVTHD, CVTHG

        opnd[0:3] =     src.rh
        opnd[4:5] =     dst.wx
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case CVTHF:
        r = op_cvthfd (opnd, NULL);                     /* convert */
        h_write_l (spec, va, r, acc, hst);              /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case CVTHD:
        r = op_cvthfd (opnd, &rh);                      /* convert */
        h_write_q (spec, va, r, rh, acc, hst);          /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case CVTHG:
        r = op_cvthg (opnd, &rh);                       /* convert */
        h_write_q (spec, va, r, rh, acc, hst);          /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

/* ADDH2, SUBH2, MULH2, DIVH2

        op[0:3] =       src.rh
        op[4:7] =       dst.mh
        spec =          last specifier
        va =            address if last specifier is memory

   ADDH3, SUBH3, MULH3, DIVH3

        op[0:3] =       src1.rh
        op[4:7] =       src2.rh
        op[8:9] =       dst.wh
        spec =          last specifier
        va =            address if last specifier is memory
*/


    case ADDH2: case ADDH3:
        r = op_addh (opnd, r_octa, FALSE);              /* add */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case SUBH2: case SUBH3:
        r = op_addh (opnd, r_octa, TRUE);               /* subtract */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case MULH2: case MULH3:
        r = op_mulh (opnd, r_octa);                     /* multiply */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

    case DIVH2: case DIVH3:
        r = op_divh (opnd, r_octa);                     /* divide */
        h_write_o (spec, va, r_octa, acc, hst);         /* write result */
        CC_IIZZ_FP (r);                                 /* set cc's */
        break;

/* ACBH
        
        opnd[0:3] =     limit.rh
        opnd[4:7] =     add.rh
        opnd[8:11] =    index.mh
        spec =          last specifier
        va =            last va
        brdest =        branch destination
*/

    case ACBH:
        r = op_addh (opnd + 4, r_octa, FALSE);          /* add + index */
        CC_IIZP_FP (r);                                 /* set cc's */
        temp = op_cmph (r_octa, opnd);                  /* result : limit */
        h_write_o (spec, va, r_octa, acc, hst);         /* write 2nd */
        if ((temp & CC_Z) || ((opnd[4] & FPSIGN)?       /* test br cond */
            !(temp & CC_N): (temp & CC_N)))
            cc = cc | LSIGN;                            /* hack for branch */
        break;

/* POLYH

        opnd[0:3] =     arg.rh
        opnd[4] =       deg.rb
        opnd[5] =       table.ah
*/
        
    case POLYH:
        op_polyh (opnd, acc);                           /* eval polynomial */
        CC_IIZZ_FP (R[0]);                              /* set cc's */
        break;

/* EMODH

        opnd[0:3] =     multiplier
        opnd[4] =       extension
        opnd[5:8] =     multiplicand
        opnd[9:10] =    integer destination (int.wl)
        opnd[11:12] =   floating destination (flt.wh)
        spec =          last specifier
        va =            address if last specifier is memory
*/

    case EMODH:
        r = op_emodh (opnd, r_octa, &temp, &flg);       /* extended mod */
        if (opnd[11] < 0) {                             /* 2nd memory? */
            Read (opnd[12], L_BYTE, WA);                /* prove write */
            Read ((opnd[12] + 15) & LMASK, L_BYTE, WA);
            }
        if (opnd[9] >= 0)                               /* store 1st */
            R[opnd[9]] = temp;
        else Write (opnd[10], temp, L_LONG, WA);
        h_write_o (spec, va, r_octa, acc, hst);         /* write 2nd */
        CC_IIZZ_FP (r);                                 /* set cc's */
        if (flg) {
            V_INTOV;
            }
        break;

    default:
        RSVD_INST_FAULT(opc);
        }

return cc;
}

/* Test h_floating

   Note that only the high 32b is processed.
   If the high 32b is not zero, the rest of the fraction is unchanged. */

int32 op_tsth (int32 val)
{
if (val & H_EXP)                                        /* non-zero? */
    return val;
if (val & FPSIGN)                                       /* reserved? */
    RSVD_OPND_FAULT(op_tsth);
return 0;                                               /* clean 0 */
}

/* Compare h_floating */

int32 op_cmph (int32 *hf1, int32 *hf2)
{
UFPH a, b;
int32 r;

h_unpackh (hf1, &a);                                    /* unpack op1 */
h_unpackh (hf2, &b);                                    /* unpack op2 */
if (a.sign != b.sign)                                   /* opp signs? */
    return (a.sign? CC_N: 0);
if (a.exp != b.exp)                                     /* cmp exp */
    r = a.exp - b.exp;
else r = qp_cmp (&a.frac, &b.frac);                     /* if =, cmp frac */
if (r < 0)                                              /* !=, maybe set N */
    return (a.sign? 0: CC_N);
if (r > 0)
    return (a.sign? CC_N: 0);
return CC_Z;                                            /* =, set Z */
}

/* Integer to h_floating convert */

int32 op_cvtih (int32 val, int32 *hf)
{
UFPH a;

if (val == 0) {                                         /* zero? */
    hf[0] = hf[1] = hf[2] = hf[3] = 0;                  /* result is 0 */
    return 0;
    }
if (val < 0) {                                          /* negative? */
    a.sign = FPSIGN;                                    /* sign = - */
    val = -val;
    }
else a.sign = 0;                                        /* else sign = + */
a.exp = 32 + H_BIAS;                                    /* initial exp */
a.frac.f3 = val & LMASK;                                /* fraction hi */
a.frac.f2 = a.frac.f1 = a.frac.f0 = 0;
h_normh (&a);                                           /* normalize */
return h_rpackh (&a, hf);                               /* round and pack */
}

/* H_floating to integer convert */

int32 op_cvthi (int32 *hf, int32 *flg, int32 opc)
{
UFPH a;
int32 lnt = opc & 03;
int32 ubexp;
static uint32 maxv[4] = { 0x7F, 0x7FFF, 0x7FFFFFFF, 0x7FFFFFFF };

*flg = 0;                                               /* clear ovflo */
h_unpackh (hf, &a);                                     /* unpack */
ubexp = a.exp - H_BIAS;                                 /* unbiased exp */
if ((a.exp == 0) || (ubexp < 0))                        /* true zero or frac? */
    return 0;
if (ubexp <= UH_V_NM) {                                 /* exp in range? */
    qp_rsh (&a.frac, UH_V_NM - ubexp);                  /* leave rnd bit */
    if (lnt == 03)                                      /* if CVTR, round */
        qp_inc (&a.frac);
    qp_rsh (&a.frac, 1);                                /* now justified */
    if (a.frac.f3 || a.frac.f2 || a.frac.f1 ||
        (a.frac.f0 > (maxv[lnt] + (a.sign? 1: 0))))
        *flg = CC_V;
    }
else {
    *flg = CC_V;                                        /* always ovflo */
    if (ubexp > (UH_V_NM + 32))                         /* in ext range? */
        return 0;
    qp_lsh (&a.frac, ubexp - UH_V_NM - 1);              /* no rnd bit */
    }
return (a.sign? NEG (a.frac.f0): a.frac.f0);            /* return lo frac */
}

/* Floating to floating convert - F/D to H, G to H, H to F/D, H to G */

int32 op_cvtfdh (int32 vl, int32 vh, int32 *hflt)
{
UFPH a;

h_unpackfd (vl, vh, &a);                                /* unpack f/d */
a.exp = a.exp - FD_BIAS + H_BIAS;                       /* if nz, adjust exp */
return h_rpackh (&a, hflt);                             /* round and pack */
}

int32 op_cvtgh (int32 vl, int32 vh, int32 *hflt)
{
UFPH a;

h_unpackg (vl, vh, &a);                                 /* unpack g */
a.exp = a.exp - G_BIAS + H_BIAS;                        /* if nz, adjust exp */
return h_rpackh (&a, hflt);                             /* round and pack */
}

int32 op_cvthfd (int32 *hflt, int32 *rh)
{
UFPH a;

h_unpackh (hflt, &a);                                   /* unpack h */
a.exp = a.exp - H_BIAS + FD_BIAS;                       /* if nz, adjust exp */
return h_rpackfd (&a, rh);                              /* round and pack */
}

int32 op_cvthg (int32 *hflt, int32 *rh)
{
UFPH a;

h_unpackh (hflt, &a);                                   /* unpack h */
a.exp = a.exp - H_BIAS + G_BIAS;                        /* if nz, adjust exp */
return h_rpackg (&a, rh);                               /* round and pack */
}

/* Floating add and subtract */

int32 op_addh (int32 *opnd, int32 *hflt, t_bool sub)
{
UFPH a, b;

h_unpackh (&opnd[0], &a);                               /* unpack s1, s2 */
h_unpackh (&opnd[4], &b);
if (sub)                                                /* sub? -s1 */
    a.sign = a.sign ^ FPSIGN;
vax_hadd (&a, &b, 0);                                   /* do add */
return h_rpackh (&a, hflt);                             /* round and pack */
}

/* Floating multiply */

int32 op_mulh (int32 *opnd, int32 *hflt)
{
UFPH a, b;
    
h_unpackh (&opnd[0], &a);                               /* unpack s1, s2 */
h_unpackh (&opnd[4], &b);
vax_hmul (&a, &b, 0);                                   /* do multiply */
return h_rpackh (&a, hflt);                             /* round and pack */
}

/* Floating divide */

int32 op_divh (int32 *opnd, int32 *hflt)
{
UFPH a, b;

h_unpackh (&opnd[0], &a);                               /* unpack s1, s2 */
h_unpackh (&opnd[4], &b);
vax_hdiv (&a, &b);                                      /* do divide */
return h_rpackh (&b, hflt);                             /* round and pack */
}

/* Polynomial evaluation

   The most mis-implemented instruction in the VAX (probably here too).
   POLY requires a precise combination of masking versus normalizing
   to achieve the desired answer.  In particular, both the multiply
   and add steps are masked prior to normalization.  In addition,
   negative small fractions must not be treated as 0 during denorm. */

void op_polyh (int32 *opnd, int32 acc)
{
UFPH r, a, c;
int32 deg = opnd[4];
int32 ptr = opnd[5];
int32 i, wd[4], res[4];

if (deg > 31)                                           /* deg > 31? fault */
    RSVD_OPND_FAULT(op_polyh);
h_unpackh (&opnd[0], &a);                               /* unpack arg */
wd[0] = Read (ptr, L_LONG, RD);                         /* get C0 */
wd[1] = Read (ptr + 4, L_LONG, RD);
wd[2] = Read (ptr + 8, L_LONG, RD);
wd[3] = Read (ptr + 12, L_LONG, RD);
ptr = ptr + 16;                                         /* adv ptr */
h_unpackh (wd, &r);                                     /* unpack C0 */
h_rpackh (&r, res);                                     /* first result */
for (i = 0; i < deg; i++) {                             /* loop */
    h_unpackh (res, &r);                                /* unpack result */
    vax_hmul (&r, &a, 1);                               /* r = r * arg */
    wd[0] = Read (ptr, L_LONG, RD);                     /* get Cn */
    wd[1] = Read (ptr + 4, L_LONG, RD);
    wd[2] = Read (ptr + 8, L_LONG, RD);
    wd[3] = Read (ptr + 12, L_LONG, RD);
    ptr = ptr + 16;
    h_unpackh (wd, &c);                                 /* unpack Cnext */
    vax_hadd (&r, &c, 1);                               /* r = r + Cnext */
    h_rpackh (&r, res);                                 /* round and pack */
    }
R[0] = res[0];                                          /* result */
R[1] = res[1];
R[2] = res[2];
R[3] = res[3];
R[4] = 0;
R[5] = ptr;
return;
}

/* Extended modularize

   EMOD presents two sets of complications.  First, it requires an extended
   fraction multiply, with precise (and unusual) truncation conditions.
   Second, it has two write operands, a dubious distinction it shares
   with EDIV. */

int32 op_emodh (int32 *opnd, int32 *hflt, int32 *intgr, int32 *flg)
{
UFPH a, b;

h_unpackh (&opnd[0], &a);                               /* unpack operands */
h_unpackh (&opnd[5], &b);
a.frac.f0 = a.frac.f0 | (opnd[4] >> 1);                 /* extend src1 */
vax_hmul (&a, &b, 0);                                   /* multiply */
vax_hmod (&a, intgr, flg);                              /* sep int & frac */
return h_rpackh (&a, hflt);                             /* round and pack frac */
}

/* Unpacked floating point routines */

/* Floating add */

void vax_hadd (UFPH *a, UFPH *b, uint32 mlo)
{
int32 ediff;
UFPH t;

if ((a->frac.f3 == 0) && (a->frac.f2 == 0) &&           /* s1 = 0? */
    (a->frac.f1 == 0) && (a->frac.f0 == 0)) {
    *a = *b;                                            /* result is s2 */
    return;
    }
if ((b->frac.f3 == 0) && (b->frac.f2 == 0) &&           /* s2 = 0? */
    (b->frac.f1 == 0) && (b->frac.f0 == 0))
    return;
if ((a->exp < b->exp) ||                                /* |s1| < |s2|? */
    ((a->exp == b->exp) && (qp_cmp (&a->frac, &b->frac) < 0))) {
    t = *a;                                         /* swap */
    *a = *b;
    *b = t;
    }
ediff = a->exp - b->exp;                                /* exp diff */
if (a->sign ^ b->sign) {                                /* eff sub? */
    qp_neg (&b->frac);                                  /* negate fraction */
    if (ediff)                                          /* denormalize */
        qp_rsh_s (&b->frac, ediff, 1);
    qp_add (&a->frac, &b->frac);                        /* "add" frac */
    a->frac.f0 = a->frac.f0 & ~mlo;                     /* mask before norm */
    h_normh (a);                                        /* normalize */
    }
else {
    if (ediff)                                          /* add, denormalize */
        qp_rsh (&b->frac, ediff);
    if (qp_add (&a->frac, &b->frac)) {                  /* add frac, carry? */
        qp_rsh (&a->frac, 1);                           /* renormalize */
        a->frac.f3 = a->frac.f3 | UH_NM_H;              /* add norm bit */
        a->exp = a->exp + 1;                            /* incr exp */
        }
    a->frac.f0 = a->frac.f0 & ~mlo;                      /* mask */
    }
return;
}

/* Floating multiply - 128b * 128b */

void vax_hmul (UFPH *a, UFPH *b, uint32 mlo)
{
int32 i, c;
UQP accum = { 0, 0, 0, 0 };

if ((a->exp == 0) || (b->exp == 0)) {                   /* zero argument? */
    a->frac.f0 = a->frac.f1 = 0;                        /* result is zero */
    a->frac.f2 = a->frac.f3 = 0;
    a->sign = a->exp = 0;
    return;
    }
a->sign = a->sign ^ b->sign;                            /* sign of result */
a->exp = a->exp + b->exp - H_BIAS;                      /* add exponents */
for (i = 0; i < 128; i++) {                             /* quad precision */
    if (a->frac.f0 & 1)                                 /* mplr low? add */
        c = qp_add (&accum, &b->frac);
    else c = 0;
    qp_rsh (&accum, 1);                                 /* shift result */
    if (c)                                              /* add carry out */
        accum.f3 = accum.f3 | UH_NM_H;
    qp_rsh (&a->frac, 1);                               /* shift mplr */
    }
a->frac = accum;                                        /* result */
a->frac.f0 = a->frac.f0 & ~mlo;                         /* mask low frac */
h_normh (a);                                            /* normalize */
return;
}

/* Floating modulus - there are three cases

   exp <= bias                  - integer is 0, fraction is input,
                                  no overflow
   bias < exp <= bias+128       - separate integer and fraction,
                                  integer overflow may occur
   bias+128 < exp               - result is integer, fraction is 0
                                  integer overflow
*/

void vax_hmod (UFPH *a, int32 *intgr, int32 *flg)
{
UQP ifr;

if (a->exp <= H_BIAS)                                   /* 0 or <1? int = 0 */
    *intgr = *flg = 0;
else if (a->exp <= (H_BIAS + 128)) {                    /* in range? */
    ifr = a->frac;
    qp_rsh (&ifr, 128 - (a->exp - H_BIAS));             /* separate integer */
    if ((a->exp > (H_BIAS + 32)) ||                     /* test ovflo */
        ((a->exp == (H_BIAS + 32)) &&
         (ifr.f0 > (a->sign? 0x80000000: 0x7FFFFFFF))))
        *flg = CC_V;
    else *flg = 0;
    *intgr = ifr.f0;
    if (a->sign)                                        /* -? comp int */
        *intgr = -*intgr;
    qp_lsh (&a->frac, a->exp - H_BIAS);                 /* excise integer */
    a->exp = H_BIAS;
    }
else {
    if (a->exp < (H_BIAS + 160)) {                      /* left shift needed? */
        ifr = a->frac;
        qp_lsh (&ifr, a->exp - H_BIAS - 128);
        *intgr = ifr.f0;
        }
    else *intgr = 0;                                    /* out of range */
    if (a->sign)
        *intgr = -*intgr;
    a->frac.f0 = a->frac.f1 = 0;                        /* result 0 */
    a->frac.f2 = a->frac.f3 = 0;
    a->sign = a->exp = 0;
    *flg = CC_V;                                        /* overflow */
    }
h_normh (a);                                            /* normalize */
return;
}

/* Floating divide

   Carried out to 128 bits, although fewer are required */

void vax_hdiv (UFPH *a, UFPH *b)
{
int32 i;
UQP quo = { 0, 0, 0, 0 };

if (a->exp == 0)                                        /* divr = 0? */
    FLT_DZRO_FAULT;
if (b->exp == 0)                                        /* divd = 0? */
    return; 
b->sign = b->sign ^ a->sign;                            /* result sign */
b->exp = b->exp - a->exp + H_BIAS + 1;                  /* unbiased exp */
qp_rsh (&a->frac, 1);                                   /* allow 1 bit left */
qp_rsh (&b->frac, 1);
for (i = 0; i < 128; i++) {                             /* divide loop */
    qp_lsh (&quo, 1);                                   /* shift quo */
    if (qp_cmp (&b->frac, &a->frac) >= 0) {             /* div step ok? */
        qp_sub (&b->frac, &a->frac);                    /* subtract */
        quo.f0 = quo.f0 + 1;                            /* quo bit = 1 */
        }
    qp_lsh (&b->frac, 1);                               /* shift divd */
    }
b->frac = quo;
h_normh (b);                                            /* normalize */
return;
}

/* Quad precision integer routines */

int32 qp_cmp (UQP *a, UQP *b)
{
if (a->f3 < b->f3)                                      /* compare hi */
    return -1;
if (a->f3 > b->f3)
    return +1;
if (a->f2 < b->f2)                                      /* hi =, compare mid1 */
    return -1;
if (a->f2 > b->f2)
    return +1;
if (a->f1 < b->f1)                                      /* mid1 =, compare mid2 */
    return -1;
if (a->f1 > b->f1)
    return +1;
if (a->f0 < b->f0)                                      /* mid2 =, compare lo */
    return -1;
if (a->f0 > b->f0)
    return +1;
return 0;                                               /* all equal */
}

uint32 qp_add (UQP *a, UQP *b)
{
uint32 cry1, cry2, cry3, cry4;

a->f0 = (a->f0 + b->f0) & LMASK;                        /* add lo */
cry1 = (a->f0 < b->f0);                                 /* carry? */
a->f1 = (a->f1 + b->f1 + cry1) & LMASK;                 /* add mid2 */
cry2 = (a->f1 < b->f1) || (cry1 && (a->f1 == b->f1));   /* carry? */
a->f2 = (a->f2 + b->f2 + cry2) & LMASK;                 /* add mid1 */
cry3 = (a->f2 < b->f2) || (cry2 && (a->f2 == b->f2));   /* carry? */
a->f3 = (a->f3 + b->f3 + cry3) & LMASK;                 /* add hi */
cry4 = (a->f3 < b->f3) || (cry3 && (a->f3 == b->f3));   /* carry? */
return cry4;                                            /* return carry out */
}

void qp_inc (UQP *a)
{
a->f0 = (a->f0 + 1) & LMASK;                            /* inc lo */
if (a->f0 == 0) {                                       /* propagate carry */
    a->f1 = (a->f1 + 1) & LMASK;
    if (a->f1 == 0) {
        a->f2 = (a->f2 + 1) & LMASK;
        if (a->f2 == 0) {
            a->f3 = (a->f3 + 1) & LMASK;
            }
        }
    }
return;
}

uint32 qp_sub (UQP *a, UQP *b)
{
uint32 brw1, brw2, brw3, brw4;

brw1 = (a->f0 < b->f0);                                 /* borrow? */
a->f0 = (a->f0 - b->f0) & LMASK;                        /* sub lo */
brw2 = (a->f1 < b->f1) || (brw1 && (a->f1 == b->f1));   /* borrow? */
a->f1 = (a->f1 - b->f1 - brw1) & LMASK;                 /* sub mid1 */
brw3 = (a->f2 < b->f2) || (brw2 && (a->f2 == b->f2));   /* borrow? */
a->f2 = (a->f2 - b->f2 - brw2) & LMASK;                 /* sub mid2 */
brw4 = (a->f3 < b->f3) || (brw3 && (a->f3 == b->f3));   /* borrow? */
a->f3 = (a->f3 - b->f3 - brw3) & LMASK;                 /* sub high */
return brw4;
}

void qp_neg (UQP *a)
{
uint32 cryin;

cryin = 1;
a->f0 = (~a->f0 + cryin) & LMASK;
if (a->f0 != 0)
    cryin = 0;
a->f1 = (~a->f1 + cryin) & LMASK;
if (a->f1 != 0)
    cryin = 0;
a->f2 = (~a->f2 + cryin) & LMASK;
if (a->f2 != 0)
    cryin = 0;
a->f3 = (~a->f3 + cryin) & LMASK;
return;
}

void qp_lsh (UQP *r, uint32 sc)
{
if (sc >= 128)                                          /* > 127? result 0 */
    r->f3 = r->f2 = r->f1 = r->f0 = 0;
else if (sc >= 96) {                                    /* [96,127]? */
    r->f3 = (r->f0 << (sc - 96)) & LMASK;
    r->f2 = r->f1 = r->f0 = 0;
    }
else if (sc > 64) {                                     /* [65,95]? */
    r->f3 = ((r->f1 << (sc - 64)) | (r->f0 >> (96 - sc))) & LMASK;
    r->f2 = (r->f0 << (sc - 64)) & LMASK;
    r->f1 = r->f0 = 0;
    }
else if (sc == 64) {                                    /* [64]? */
    r->f3 = r->f1;
    r->f2 = r->f0;
    r->f1 = r->f0 = 0;
    }
else if (sc > 32) {                                     /* [33,63]? */
    r->f3 = ((r->f2 << (sc - 32)) | (r->f1 >> (64 - sc))) & LMASK;
    r->f2 = ((r->f1 << (sc - 32)) | (r->f0 >> (64 - sc))) & LMASK;
    r->f1 = (r->f0 << (sc - 32)) & LMASK;
    r->f0 = 0;
    }
else if (sc == 32) {                                    /* [32]? */
    r->f3 = r->f2;
    r->f2 = r->f1;
    r->f1 = r->f0;
    r->f0 = 0;
    }
else if (sc != 0) {                                     /* [31,1]? */
    r->f3 = ((r->f3 << sc) | (r->f2 >> (32 - sc))) & LMASK;
    r->f2 = ((r->f2 << sc) | (r->f1 >> (32 - sc))) & LMASK;
    r->f1 = ((r->f1 << sc) | (r->f0 >> (32 - sc))) & LMASK;
    r->f0 = (r->f0 << sc) & LMASK;
    }
return;
}

void qp_rsh (UQP *r, uint32 sc)
{
if (sc >= 128)                                          /* > 127? result 0 */
    r->f3 = r->f2 = r->f1 = r->f0 = 0;
else if (sc >= 96) {                                    /* [96,127]? */
    r->f0 = (r->f3 >> (sc - 96)) & LMASK;
    r->f1 = r->f2 = r->f3 = 0;
    }
else if (sc > 64) {                                     /* [65,95]? */
    r->f0 = ((r->f2 >> (sc - 64)) | (r->f3 << (96 - sc))) & LMASK;
    r->f1 = (r->f3 >> (sc - 64)) & LMASK;
    r->f2 = r->f3 = 0;
    }
else if (sc == 64) {                                    /* [64]? */
    r->f0 = r->f2;
    r->f1 = r->f3;
    r->f2 = r->f3 = 0;
    }
else if (sc > 32) {                                     /* [33,63]? */
    r->f0 = ((r->f1 >> (sc - 32)) | (r->f2 << (64 - sc))) & LMASK;
    r->f1 = ((r->f2 >> (sc - 32)) | (r->f3 << (64 - sc))) & LMASK;
    r->f2 = (r->f3 >> (sc - 32)) & LMASK;
    r->f3 = 0;
    }
else if (sc == 32) {                                    /* [32]? */
    r->f0 = r->f1;
    r->f1 = r->f2;
    r->f2 = r->f3;
    r->f3 = 0;
    }
else if (sc != 0) {                                     /* [31,1]? */
    r->f0 = ((r->f0 >> sc) | (r->f1 << (32 - sc))) & LMASK;
    r->f1 = ((r->f1 >> sc) | (r->f2 << (32 - sc))) & LMASK;
    r->f2 = ((r->f2 >> sc) | (r->f3 << (32 - sc))) & LMASK;
    r->f3 = (r->f3 >> sc) & LMASK;
    }
return;
}

void qp_rsh_s (UQP *r, uint32 sc, uint32 neg)
{
qp_rsh (r, sc);                                         /* do unsigned right */
if (neg && sc) {                                        /* negative? */
    if (sc >= 128)
        r->f0 = r->f1 = r->f2 = r->f3 = LMASK;          /* > 127? result -1 */
    else {
        UQP ones = { LMASK, LMASK, LMASK, LMASK };
        qp_lsh (&ones, 128 - sc);                       /* shift ones */
        r->f0 = r->f0 | ones.f0;                        /* or into result */
        r->f1 = r->f1 | ones.f1;
        r->f2 = r->f2 | ones.f2;
        r->f3 = r->f3 | ones.f3;
        }
    }
return;
}

/* Support routines */

void h_unpackfd (int32 hi, int32 lo, UFPH *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = FD_GETEXP (hi);                                /* get exponent */
r->frac.f0 = r->frac.f1 = 0;                            /* low bits 0 */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign)                                        /* if -, rsvd op */
        RSVD_OPND_FAULT(h_unpackfd);
    r->frac.f2 = r->frac.f3 = 0;                        /* else 0 */
    return;
    }
r->frac.f3 = WORDSWAP ((hi & ~(FPSIGN | FD_EXP)) | FD_HB);
r->frac.f2 = WORDSWAP (lo);
qp_lsh (&r->frac, FD_GUARD);
return;
}

void h_unpackg (int32 hi, int32 lo, UFPH *r)
{
r->sign = hi & FPSIGN;                                  /* get sign */
r->exp = G_GETEXP (hi);                                 /* get exponent */
r->frac.f0 = r->frac.f1 = 0;                            /* low bits 0 */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign)                                        /* if -, rsvd op */
        RSVD_OPND_FAULT(h_unpackg);
    r->frac.f2 = r->frac.f3 = 0;                        /* else 0 */
    return;
    }
r->frac.f3 = WORDSWAP ((hi & ~(FPSIGN | G_EXP)) | G_HB);
r->frac.f2 = WORDSWAP (lo);
qp_lsh (&r->frac, G_GUARD);
return;
}

void h_unpackh (int32 *hflt, UFPH *r)
{
int32 thflt0;

r->sign = hflt[0] & FPSIGN;                             /* get sign */
r->exp = H_GETEXP (hflt[0]);                            /* get exponent */
if (r->exp == 0) {                                      /* exp = 0? */
    if (r->sign)                                        /* if -, rsvd op */
        RSVD_OPND_FAULT(h_unpackh);
    r->frac.f0 = r->frac.f1 = 0;                        /* else 0 */
    r->frac.f2 = r->frac.f3 = 0;
    return;
    }
thflt0 = ((hflt[0] & ~(FPSIGN | H_EXP)) | H_HB);
r->frac.f3 = WORDSWAP (thflt0);
r->frac.f2 = WORDSWAP (hflt[1]);
r->frac.f1 = WORDSWAP (hflt[2]);
r->frac.f0 = WORDSWAP (hflt[3]);
qp_lsh (&r->frac, H_GUARD);
return;
}

void h_normh (UFPH *r)
{
int32 i;
static uint32 normmask[5] = {
 0xc0000000, 0xf0000000, 0xff000000, 0xffff0000, 0xffffffff };
static int32 normtab[6] = { 1, 2, 4, 8, 16, 32};

if ((r->frac.f0 == 0) && (r->frac.f1 == 0) &&
    (r->frac.f2 == 0) && (r->frac.f3 == 0)) {           /* if fraction = 0 */
    r->sign = r->exp = 0;                               /* result is 0 */
    return;
    }
while ((r->frac.f3 & UH_NM_H) == 0) {                   /* normalized? */
    for (i = 0; i < 5; i++) {                           /* find first 1 */
        if (r->frac.f3 & normmask[i])
            break;
        }
    qp_lsh (&r->frac, normtab[i]);                      /* shift frac */
    r->exp = r->exp - normtab[i];                       /* decr exp */
    }
return;
}

int32 h_rpackfd (UFPH *r, int32 *rh)
{
static UQP f_round = { 0, 0, 0, UH_FRND };
static UQP d_round = { 0, 0, UH_DRND, 0 };

if (rh)                                                 /* assume 0 */
    *rh = 0;
if ((r->frac.f3 == 0) && (r->frac.f2 == 0))             /* frac = 0? done */
    return 0;
qp_add (&r->frac, rh? &d_round: &f_round);
if ((r->frac.f3 & UH_NM_H) == 0) {                      /* carry out? */
    qp_rsh (&r->frac, 1);                               /* renormalize */
    r->exp = r->exp + 1;
    }
if (r->exp > (int32) FD_M_EXP)                          /* ovflo? fault */
    FLT_OVFL_FAULT;
if (r->exp <= 0) {                                      /* underflow? */
    if (PSL & PSW_FU)                                   /* fault if fu */
        FLT_UNFL_FAULT;
    return 0;                                           /* else 0 */
    }
qp_rsh (&r->frac, FD_GUARD);                            /* remove guard */
if (rh)
    *rh = WORDSWAP (r->frac.f2);
return r->sign | (r->exp << FD_V_EXP) |
    (WORDSWAP (r->frac.f3) & ~(FD_HB | FPSIGN | FD_EXP));
}

int32 h_rpackg (UFPH *r, int32 *rh)
{
static UQP g_round = { 0, 0, UH_GRND, 0 };

*rh = 0;                                                /* assume 0 */
if ((r->frac.f3 == 0) && (r->frac.f2 == 0))             /* frac = 0? done */
    return 0;
qp_add (&r->frac, &g_round);                            /* round */
if ((r->frac.f3 & UH_NM_H) == 0) {                      /* carry out? */
    qp_rsh (&r->frac, 1);                               /* renormalize */
    r->exp = r->exp + 1;
    }
if (r->exp > (int32) G_M_EXP)                           /* ovflo? fault */
    FLT_OVFL_FAULT;
if (r->exp <= 0) {                                      /* underflow? */
    if (PSL & PSW_FU)                                   /* fault if fu */
        FLT_UNFL_FAULT;
    return 0;                                           /* else 0 */
    }
qp_rsh (&r->frac, G_GUARD);                             /* remove guard */
*rh = WORDSWAP (r->frac.f2);                            /* get low */
return r->sign | (r->exp << G_V_EXP) |
    (WORDSWAP (r->frac.f3) & ~(G_HB | FPSIGN | G_EXP));
}

int32 h_rpackh (UFPH *r, int32 *hflt)
{
static UQP h_round = { UH_HRND, 0, 0, 0 };

hflt[0] = hflt[1] = hflt[2] = hflt[3] = 0;              /* assume 0 */
if ((r->frac.f3 == 0) && (r->frac.f2 == 0) &&           /* frac = 0? done */
    (r->frac.f1 == 0) && (r->frac.f0 == 0))
    return 0;
if (qp_add (&r->frac, &h_round)) {                      /* round, carry out? */
    qp_rsh (&r->frac, 1);                               /* renormalize */
    r->exp = r->exp + 1;
    }
if (r->exp > (int32) H_M_EXP)                           /* ovflo? fault */
    FLT_OVFL_FAULT;
if (r->exp <= 0) {                                      /* underflow? */
    if (PSL & PSW_FU)                                   /* fault if fu */
        FLT_UNFL_FAULT;
    return 0;                                           /* else 0 */
    }
qp_rsh (&r->frac, H_GUARD);                             /* remove guard */
hflt[0] = r->sign | (r->exp << H_V_EXP) |
    (WORDSWAP (r->frac.f3) & ~(H_HB | FPSIGN | H_EXP));
hflt[1] = WORDSWAP (r->frac.f2);
hflt[2] = WORDSWAP (r->frac.f1);
hflt[3] = WORDSWAP (r->frac.f0);
return hflt[0];
}

void h_write_b (int32 spec, int32 va, int32 val, int32 acc, InstHistory *hst)
{
int32 rn;

if (hst)
    hst->res[0] = val;
if (spec > (GRN | nPC))
    Write (va, val, L_BYTE, WA);
else {
    rn = spec & 0xF;
    R[rn] = (R[rn] & ~BMASK) | val;
    }
return;
}

void h_write_w (int32 spec, int32 va, int32 val, int32 acc, InstHistory *hst)
{
int32 rn;

if (hst)
    hst->res[0] = val;
if (spec > (GRN | nPC))
    Write (va, val, L_WORD, WA);
else {
    rn = spec & 0xF;
    R[rn] = (R[rn] & ~WMASK) | val;
    }
return;
}

void h_write_l (int32 spec, int32 va, int32 val, int32 acc, InstHistory *hst)
{
if (hst)
    hst->res[0] = val;
if (spec > (GRN | nPC))
    Write (va, val, L_LONG, WA);
else R[spec & 0xF] = val;
return;
}

void h_write_q (int32 spec, int32 va, int32 vl, int32 vh, int32 acc, InstHistory *hst)
{
int32 rn, mstat;

if (hst) {
    hst->res[0] = vl;
    hst->res[1] = vh;
    }
if (spec > (GRN | nPC)) {
    if ((Test (va + 7, WA, &mstat) >= 0) ||
        (Test (va, WA, &mstat) < 0))
        Write (va, vl, L_LONG, WA);
    Write (va + 4, vh, L_LONG, WA);
    }
else {
    rn = spec & 0xF;
    if (rn >= nSP)
        RSVD_ADDR_FAULT;
    R[rn] = vl;
    R[rn + 1] = vh;
    }
return;
}

void h_write_o (int32 spec, int32 va, int32 *val, int32 acc, InstHistory *hst)
{
int32 rn, mstat;

if (hst) {
    hst->res[0] = val[0];
    hst->res[1] = val[0];
    hst->res[2] = val[0];
    hst->res[3] = val[0];
    }
if (spec > (GRN | nPC)) {
    if ((Test (va + 15, WA, &mstat) >= 0) ||
        (Test (va, WA, &mstat) < 0))
        Write (va, val[0], L_LONG, WA);
    Write (va + 4, val[1], L_LONG, WA);
    Write (va + 8, val[2], L_LONG, WA);
    Write (va + 12, val[3], L_LONG, WA);
    }
else {
    rn = spec & 0xF;
    if (rn >= nAP)
        RSVD_ADDR_FAULT;
    R[rn] = val[0];
    R[rn + 1] = val[1];
    R[rn + 2] = val[2];
    R[rn + 3] = val[3];
    }
return;
}
