/* sigma_cis.c: Sigma decimal instructions

   Copyright (c) 2007-2018, Robert M Supnik

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

   Questions:

   1. On the Sigma 9, in ASCII mode, is an ASCII blank used in EBS?

   02-Jun-2018  RMS     Fixed unsigned < 0 in decimal compare (Mark Pizzolato)
*/

#include "sigma_defs.h"

/* Decimal string structure */

#define DSTRLNT         4                               /* words per dec string */
#define DECA            12                              /* first dec accum reg */

/* Standard characters */

#define ZONE_E          0xF0                            /* EBCDIC zone bits */
#define ZONE_A          0x30                            /* ASCII zone bits */
#define ZONE            ((PSW1 & PSW1_AS)? ZONE_A: ZONE_E)
#define PKPLUS_E        0xC                             /* EBCDIC preferred plus */
#define PKPLUS_A        0xA                             /* ASCII preferred plus */
#define PKPLUS          ((PSW1 & PSW1_AS)? PKPLUS_A: PKPLUS_E)
#define BLANK_E         0x40                            /* EBCDIC blank */
#define BLANK_A         0x20                            /* ASCII blank */
#define BLANK           ((PSW1 & PSW1_AS)? BLANK_A: BLANK_E)

/* Edit special characters */

#define ED_DS           0x20                            /* digit select */
#define ED_SS           0x21                            /* start significance */
#define ED_FS           0x22                            /* field separator */
#define ED_SI           0x23                            /* immediate significance */

/* Decimal strings run low order (word 0/R15) to high order (word 3/R12) */

typedef struct {
    uint32              sign;
    uint32              val[DSTRLNT];
    } dstr_t;

/* Copy decimal accumulator to decimal string, no validation or sign separation */

#define ReadDecA(src)   for (i = 0; i < DSTRLNT; i++) \
                            src.val[DSTRLNT - 1 - i] = R[DECA + i];

static dstr_t Dstr_zero = { 0, 0, 0, 0, 0 };

extern uint32 *R;
extern uint32 CC;
extern uint32 PSW1;
extern uint32 bvamqrx;
extern uint32 cpu_model;

uint32 ReadDstr (uint32 lnt, uint32 addr, dstr_t *dec);
uint32 WriteDstr (uint32 lnt, uint32 addr, dstr_t *dec);
void WriteDecA (dstr_t *dec, t_bool cln);
void SetCC2Dstr (uint32 lnt, dstr_t *dst);
uint32 TestDstrValid (dstr_t *src);
uint32 DstrInvd (void);
uint32 AddDstr (dstr_t *src1, dstr_t *src2, dstr_t *dst, uint32 cin);
void SubDstr (dstr_t *src1, dstr_t *src2, dstr_t *dst);
int32 CmpDstr (dstr_t *src1, dstr_t *src2);
uint32 LntDstr (dstr_t *dsrc);
uint32 NibbleLshift (dstr_t *dsrc, uint32 sc, uint32 cin);
uint32 NibbleRshift (dstr_t *dsrc, uint32 sc, uint32 cin);
t_bool GenLshift (dstr_t *dsrc, uint32 sc);
void GenRshift (dstr_t *dsrc, uint32 sc);
uint32 ed_getsrc (uint32 sa, uint32 *c, uint32 *d);
void ed_advsrc (uint32 rn, uint32 c);
t_bool cis_test_int (dstr_t *src1, uint32 *kint);
void cis_dm_int (dstr_t *src, dstr_t *dst, uint32 kint);
void cis_dd_int (dstr_t *src, dstr_t *dst, uint32 t, uint32 *kint);

/* Decimal instructions */

uint32 cis_dec (uint32 op, uint32 lnt, uint32 bva)
{
dstr_t src1, src2, src2x, dst;
uint32 i, t, kint, ldivr, ldivd, ad, c, d, end;
int32 sc, scmp;
uint32 tr;

if (lnt == 0)                                           /* adjust length */
    lnt = 16;
CC &= ~(CC1|CC2);                                       /* clear CC1, CC2 */

switch (op) {                                           /* case on opcode */

    case OP_DL:                                         /* decimal load */
        if ((tr = ReadDstr (lnt, bva, &dst)) != 0)      /* read mem string */
            return tr;
        WriteDecA (&dst, FALSE);                        /* store result */
        break;

    case OP_DST:                                        /* decimal store */
        ReadDecA (dst);                                 /* read dec accum */
        if ((tr = TestDstrValid (&dst)) != 0)           /* valid? */
            return tr;
        if ((tr = WriteDstr (lnt, bva, &dst)) != 0)     /* write to mem */
            return tr;
        break;

    case OP_DS:                                         /* decimal subtract */
    case OP_DA:                                         /* decimal add */
        ReadDecA (src1);                                /* read dec accum */
        if ((tr = TestDstrValid (&src1)) != 0)          /* valid? */ 
            return tr;
        if ((tr = ReadDstr (lnt, bva, &src2)) != 0)     /* read mem string */
            return tr;
        if (op == OP_DS)                                /* sub? invert sign */
            src2.sign = src2.sign ^ 1;
        if (src1.sign ^ src2.sign) {                    /* opp signs? sub */
            if (CmpDstr (&src1, &src2) < 0) {           /* src1 < src2? */
                SubDstr (&src1, &src2, &dst);           /* src2 - src1 */
                dst.sign = src2.sign;                   /* sign = src2 */
                }
            else {
                SubDstr (&src2, &src1, &dst);           /* src1 - src2 */
                dst.sign = src1.sign;                   /* sign = src1 */
                }
            }
        else {                                          /* addition */
            if (AddDstr (&src1, &src2, &dst, 0)) {      /* add, overflow? */
                CC |= CC2;                              /* set CC2 */
                return (PSW1 & PSW1_DM)? TR_DEC: 0;     /* trap if enabled */
                }
            dst.sign = src1.sign;                       /* set result sign */
            }
        WriteDecA (&dst, TRUE);                         /* store result */
        break;

    case OP_DC:                                         /* decimal compare */
        ReadDecA ( src1);                               /* read dec accum */
        if ((tr = TestDstrValid (&src1)) != 0)          /* valid? */ 
            return tr;
        if ((tr = ReadDstr (lnt, bva, &src2)) != 0)     /* read mem string */
            return tr;
        LntDstr (&src1);                                /* clean -0 */
        LntDstr (&src2);
        if (src1.sign ^ src2.sign)                      /* signs differ? */
            CC = src1.sign? CC4: CC3;                   /* set < or > */
        else {                                          /* same signs */
            scmp = CmpDstr (&src1, &src2);              /* compare strings */
            if (scmp < 0)
                CC = (src1.sign? CC3: CC4);
            else if (scmp > 0)
                CC = (src1.sign? CC4: CC3);
            else CC = 0;
            }
        break;

/* Decimal multiply - algorithm from George Plue.

   The Sigma does decimal multiply one digit at a time, using the multiplicand
   and a doubled copy of the multiplicand. Multiplying by digits 1-5 is
   synthesized by 1-3 adds; multiplying by digits 6-9 is synthesized by 1-2
   subtractions, and adding 1 to the next multiplier digit. (That is,
   multiplying by 7 is done by multiplying by "10 - 3".) This requires at
   most one extra add to fixup the last digit, and minimizes the overall
   number of adds (average 1.5 adds per multiplier digit). Note that
   multiplication proceeds from right to left.

   The Sigma 5-9 allowed decimal multiply to be interrupted; the 5X0 series
   did not. An interrupted multiply uses a sign digit in R12 and R13 as the
   divider between the remaining multiplier (to the left of the sign, and
   in the low-order digit of R15) and the partial product (to the right of
   the sign). Because the partial product may be negative, leading 0x99's
   may have been stripped and need to be restored.
   
   The real Sigma's probably didn't run a validty test after separation of
   the partial product and multiplier, but it doesn't hurt, and prevents
   certain corner cases from causing errors. */

    case OP_DM:                                         /* decimal multiply */
        if (lnt >= 9)                                   /* invalid length? */
            return DstrInvd ();
        ReadDecA (src1);                                /* get dec accum */
        if ((tr = ReadDstr (lnt, bva, &src2)) != 0)     /* read mem string */
            return tr;
        dst = Dstr_zero;                                /* clear result */
        kint = 0;                                       /* assume no int */
        if (!QCPU_5X0 &&                                /* S5-9? */
            (cis_test_int (&src1, &kint))) {            /* interrupted? */
                src1.sign = 0;
                cis_dm_int (&src1, &dst, kint);         /* restore */
                }
        else if ((tr = TestDstrValid (&src1)) != 0)     /* mpyr valid? */ 
             return tr;
        if (LntDstr (&src1) && LntDstr (&src2)) {       /* both opnds != 0? */
            dst.sign = src1.sign ^ src2.sign;           /* sign of result */
            AddDstr (&src2, &src2, &src2x, 0);          /* get 2*mplcnd */
            for (i = 1; i <= 16; i++) {                 /* 16 iterations */
                if (i >= kint) {                        /* past int point? */
                    NibbleRshift (&src1, 1, 0);         /* mpyr right 4 */
                    d = src1.val[0] & 0xF;              /* get digit */
                    switch (d) {                        /* case */
                    case 5:                             /* + 2 + 2 + 1 */
                        AddDstr (&src2x, &dst, &dst, 0);
                    case 3:                             /* + 2 + 1 */
                        AddDstr (&src2x, &dst, &dst, 0);
                    case 1:                             /* + 1 */
                        AddDstr (&src2, &dst, &dst, 0);
                    case 0:
                        break;
                    case 4:                             /* + 2 + 2 */
                        AddDstr (&src2x, &dst, &dst, 0);
                    case 2:                             /* + 2 */
                        AddDstr (&src2x, &dst, &dst, 0);
                        break;
                    case 6:                             /* - 2 - 2 + 10 */
                        SubDstr (&src2x, &dst, &dst);
                    case 8:                             /* - 2 + 10 */
                        SubDstr (&src2x, &dst, &dst);
                        src1.val[0] += 0x10;            /* + 10 */
                        break;
                    case 7:                             /* -2 - 1 + 10 */
                        SubDstr (&src2x, &dst, &dst);
                    case 9:                             /* -1 + 10 */
                        SubDstr (&src2, &dst, &dst);
                    default:                            /* + 10 */
                        src1.val[0] += 0x10;
                        }                               /* end switch */
                    }                                   /* end if >= kint */
                NibbleLshift (&src2, 1, 0);             /* shift mplcnds */
                NibbleLshift (&src2x, 1, 0);
                }                                       /* end for */
            }                                           /* end if != 0 */
        WriteDecA (&dst, TRUE);                         /* store result */
        break;

/* Decimal divide overflow calculation - if the dividend has true length d,
   and the divisor true length r, then the quotient will have (d - r) or
   (d - r + 1) digits. Therefore, if (d - r) > 15, the quotient will not
   fit. However, if (d - r) == 15, it may or may not fit, depending on 
   whether the first subtract succeeds. Therefore, it's necessary to test
   after the divide to see if the quotient has one extra digit. */

    case OP_DD:                                         /* decimal divide */
        if (lnt >= 9)                                   /* invalid length? */
            return DstrInvd ();
        ReadDecA (src1);                                /* read dec accum */
        if ((tr = ReadDstr (lnt, bva, &src2)) != 0)     /* read mem string */
            return tr;
        dst = Dstr_zero;                                /* clear result */
        kint = 0;                                       /* no interrupt */
        if (!QCPU_5X0 &&                                /* S5-9? */
            (cis_test_int (&src1, &t))) {               /* interrupted? */
                src1.sign = 0;
                cis_dd_int (&src1, &dst, t, &kint);     /* restore */
                t = t - 1;
                }
        else {                                          /* normal start? */
            if ((tr = TestDstrValid (&src1)) != 0)      /* divd valid? */ 
                return tr;
            ldivr = LntDstr (&src2);                    /* divr lnt */
            ldivd = LntDstr (&src1);                    /* divd lnt */
            if ((ldivr == 0) ||                         /* div by zero? */
                (ldivd > (ldivr + 15))) {               /* quo too big? */
                CC |= CC2;                              /* divide check */
                return (PSW1 & PSW1_DM)? TR_DEC: 0;     /* trap if enabled */
                }
            if (CmpDstr (&src1, &src2) < 0) {           /* no divide? */
                R[12] = src1.val[1];                    /* remainder */
                R[13] = src1.val[0] | (PKPLUS + src1.sign);
                R[14] = 0;                             /* quotient */
                R[15] = PKPLUS;
                CC = 0;
                return SCPE_OK;
                }
            t = ldivd - ldivr;
            }
        dst.sign = src1.sign ^ src2.sign;               /* calculate sign */
        GenLshift (&src2, t);                           /* align */
        for (i = 0; i <= t; i++) {                      /* divide loop */
            for (d = kint;                              /* find digit */
                (d < 10) && (CmpDstr (&src1, &src2) >= 0);
                d++)
                SubDstr (&src2, &src1, &src1);
            dst.val[0] = (dst.val[0] & ~0xF) | d;       /* insert quo dig */
            NibbleLshift (&dst, 1, 0);                  /* shift quotient */
            NibbleRshift (&src2, 1, 0);                 /* shift divisor */
            kint = 0;                                   /* no more int */
            }                                           /* end divide loop */
        if (dst.val[2]) {                               /* quotient too big? */
            CC |= CC2;                                  /* divide check */
            return (PSW1 & PSW1_DM)? TR_DEC: 0;         /* trap if enabled */
            }
        CC = dst.sign? CC4: CC3;                        /* set CC's */
        R[12] = src1.val[1];                            /* remainder */
        R[13] = src1.val[0] | (PKPLUS + src1.sign);
        R[14] = dst.val[1];                             /* quotient */
        R[15] = dst.val[0] | (PKPLUS + dst.sign);
        break;

    case OP_DSA:                                        /* decimal shift */
        ReadDecA (dst);                                 /* read dec accum */
        if ((tr = TestDstrValid (&dst)) != 0)           /* valid? */
            return tr;
        CC = 0;                                         /* clear CC's */
        sc = SEXT_H_W (bva >> 2);                       /* shift count */
        if (sc > 31)                                    /* sc in [-31,31] */
            sc = 31;
        if (sc < -31)
            sc = -31;
        if (sc < 0) {                                   /* right shift? */
            sc = -sc;                                   /* |shift| */
            GenRshift (&dst, sc);                       /* do shift */    
            dst.val[0] = dst.val[0] & ~0xF;             /* clear sign */
            }                                           /* end right shift */
        else if (sc) {                                  /* left shift? */
            if (GenLshift (&dst, sc))                   /* do shift */
                CC |= CC2;
            }                                           /* end left shift */
        WriteDecA (&dst, FALSE);                        /* store result */
        break;

    case OP_PACK:                                       /* zoned to packed */
        dst = Dstr_zero;                                /* clear result */
        end = (2 * lnt) - 1;                            /* zoned length */
        for (i = 1; i <= end; i++) {                    /* loop thru char */
            ad = (bva + end - i) & bvamqrx;             /* zoned character */
            if ((tr = ReadB (ad, &c, VR)) != 0)         /* read char */
                return tr;
            if (i == 1) {                               /* sign + digit? */
                uint32 s;
                s = (c >> 4) & 0xF;                     /* get sign */
                if (s < 0xA)
                    return DstrInvd ();
                if ((s == 0xB) || (s == 0xD))           /* negative */
                    dst.sign = 1;
                }
            d = c & 0xF;                                /* get digit */
            if (d > 0x9)
                return DstrInvd ();
            dst.val[i / 8] = dst.val[i / 8] | (d << ((i % 8) * 4));
            }
        WriteDecA (&dst, FALSE);                        /* write result */
        break;

    case OP_UNPK:                                       /* packed to zoned */
        ReadDecA (dst);                                 /* read dec accum */
        if ((tr = TestDstrValid (&dst)) != 0)           /* valid? */
            return tr;
        end = (2 * lnt) - 1;                            /* zoned length */
        if ((tr = ReadB (bva, &c, VW)) != 0)            /* prove writeable */
            return tr;
        for (i = 1; i <= end; i++) {                    /* loop thru chars */
            c = (dst.val[i / 8] >> ((i % 8) * 4)) & 0xF; /* get digit */
            if (i == 1)                                 /* first? */
                c |= ((PKPLUS + dst.sign) << 4);        /* or in sign */
            else c |= ZONE;                             /* no, or in zone */
            ad = (bva + end - i) & bvamqrx;
            if ((tr = WriteB (ad, c, VW)) != 0)         /* write to memory */
                return tr;
            }
        SetCC2Dstr (lnt, &dst);                         /* see if too long */
        break;
        }
return 0;
}

/* Test for interrupted multiply or divide */

t_bool cis_test_int (dstr_t *src, uint32 *kint)
{
int32 i;
uint32 wd, sc, d;

for (i = 15; i >= 1; i--) {                             /* test 15 nibbles */
    wd = (DSTRLNT/2) + (i / 8);
    sc = (i % 8) * 4;
    d = (src->val[wd] >> sc) & 0xF;
    if (d >= 0xA) {
        *kint = (uint32) i;
        return TRUE;
        }
    }
return FALSE;
}

/* Resume interrupted multiply

   The sign that was found is the "fence" between the the remaining multiplier
   and the partial product:
                                R   val
   +--+--+--+--+--+--+--+--+
   |   mpyer         |sn|pp|    12  3
   +--+--+--+--+--+--+--+--+
   |    partial product    |    13  2
   +--+--+--+--+--+--+--+--+
   |    partial product    |    14  1
   +--+--+--+--+--+--+--+--+
   |    partial product |mp|    15  0
   +--+--+--+--+--+--+--+--+

   This routine separates the multiplier and partial product, returns the
   multiplier as a valid decimal string in src, and the partial product
   as a value with no sign in dst */

void cis_dm_int (dstr_t *src, dstr_t *dst, uint32 kint)
{
uint32 ppneg, wd, sc, d, curd;
int32 k;

*dst = *src;                                            /* copy input */
wd = (DSTRLNT/2) + (kint / 8);
sc = (kint % 8) * 4;
d = (src->val[wd] >> sc) & 0xF;                         /* get sign fence */
ppneg = ((d >> 2) & 1) ^ 1;                             /* partial prod neg? */
curd = (src->val[0] & 0xF) + ppneg;                     /* bias cur digit */
src->val[wd] = (src->val[wd] & ~(0xF << sc)) |          /* replace sign */
                (curd << sc);                           /* with digit */
GenRshift (src, kint + 15);                             /* right justify */
src->sign = ((d == 0xB) || (d == 0xD))? 1: 0;           /* set mpyr sign */
src->val[0] = src->val[0] & ~0xF;                       /* clear sign pos */

/* Mask out multiplier */

for (k = DSTRLNT - 1; k >= (int32) wd; k--)             /* words hi to lo */
    dst->val[k] &= ~(0xFFFFFFFFu <<
        ((k > (int32) wd)? 0: sc));

/* Recreate missing high order digits for negative partial product */

if (ppneg) {                                            /* negative? */
    for (k = (DSTRLNT * 4) - 1; k != 0; k--) {          /* bytes hi to lo */
        wd = k / 4;
        sc = (k % 4) * 8;
        if (((dst->val[wd] >> sc) & 0xFF) != 0)
            break;
        dst->val[wd] |= (0x99 << sc);                   /* repl 00 with 99 */
        }                                               /* end for */  
    }
dst->val[0] &= ~0xF;                                    /* clear pp sign */
return;
}

/* Resume interrupted divide

   The sign that was found is the "fence" between the the quotient and the
   remaining dividend product:
                                R   val
   +--+--+--+--+--+--+--+--+
   |   quotient      |sn|dv|    12  3
   +--+--+--+--+--+--+--+--+
   |       dividend        |    13  2
   +--+--+--+--+--+--+--+--+
   |       dividend        |    14  1
   +--+--+--+--+--+--+--+--+
   |       dividend     |qu|    15  0
   +--+--+--+--+--+--+--+--+

   This routine separates the quotient and the remaining dividend, returns
   the dividend as a valid decimal string, the quotient as a decimal string
   without sign, and kint is the partial value of the last quotient digit. */

void cis_dd_int (dstr_t *src, dstr_t *dst, uint32 nib, uint32 *kint)
{
uint32 wd, sc, d, curd;
int32 k;

wd = (DSTRLNT/2) + (nib / 8);
sc = (nib % 8) * 4;
curd = src->val[0] & 0xF;                               /* last quo digit */
*dst = *src;                                            /* copy input */
GenRshift (dst, nib + 16);                              /* right justify quo */
d = dst->val[0] & 0xF;                                  /* get sign fence */
dst->val[0] = (dst->val[0] & ~0xF) | curd;              /* repl with digit */
*kint = curd;

/* Mask out quotient */

for (k = DSTRLNT - 1; k >= (int32) wd; k--)             /* words hi to lo */
    src->val[k] &= ~(0xFFFFFFFFu <<
        ((k > (int32) wd)? 0: sc));
src->sign = ((d == 0xB) || (d == 0xD))? 1: 0;           /* set divd sign */
src->val[0] = src->val[0] & ~0xF;                       /* clr sign digit */
return;
}

/* Get packed decimal string from memory

   Arguments:
        lnt     =       decimal string length
        adr     =       decimal string address
        src     =       decimal string structure
   Output:
        trap or abort signal

   Per the Sigma spec, bad digits or signs cause a fault or abort */

uint32 ReadDstr (uint32 lnt, uint32 adr, dstr_t *src)
{
uint32 i, c, bva;
uint32 tr;

*src = Dstr_zero;                                       /* clear result */
for (i = 0; i < lnt; i++) {                             /* loop thru string */
    bva = (adr + lnt - i - 1) & bvamqrx;                /* from low to high */
    if ((tr = ReadB (bva, &c, VR)) != 0)                /* read byte */
        return tr;
    src->val[i / 4] = src->val[i / 4] | (c << ((i % 4) * 8));
    }                                                   /* end for */
return TestDstrValid (src);
}

/* Separate sign, validate sign and digits of decimal string */

uint32 TestDstrValid (dstr_t *src)
{
uint32 i, j, s, t;

s = src->val[0] & 0xF;                                  /* get sign */
if (s < 0xA)                                            /* valid? */
    return DstrInvd ();
if ((s == 0xB) || (s == 0xD))                           /* negative? */
    src->sign = 1;
else src->sign = 0;
src->val[0] &= ~0xF;                                    /* clear sign */

for (i = 0; i < DSTRLNT; i++) {                         /* check 4 words */
    for (j = 0; j < 8; j++) {                           /* 8 digit/word */
        t = (src->val[i] >> (28 - (j * 4))) & 0xF;      /* get digit */
        if (t > 0x9)                                    /* invalid digit? */
            return DstrInvd ();                         /* exception */
        }
    }
return 0;
}

/* Invalid digit or sign: set CC1, trap or abort instruction */

uint32 DstrInvd (void)
{
CC |= CC1;                                              /* set CC1 */
if (PSW1 & PSW1_DM)                                     /* if enabled, trap */
    return TR_DEC;
return WSIGN;                                           /* otherwise, abort */
}
       
/* Store decimal string

   Arguments:
        lnt     =       decimal string length
        adr     =       decimal string address
        dst     =       decimal string structure

   Returns memory management traps (if any)
   Bad digits and invalid sign are impossible
*/

uint32 WriteDstr (uint32 lnt, uint32 adr, dstr_t *dst)
{
uint32 i, bva, c;
uint32 tr;

dst->val[0] = dst->val[0] | (PKPLUS + dst->sign);       /* set sign */
if ((tr = ReadB (adr, &c, VW)) != 0)                    /* prove writeable */
    return tr;
for (i = 0; i < lnt; i++) {                             /* loop thru bytes */
    c = (dst->val[i / 4] >> ((i % 4) * 8)) & 0xFF;      /* from low to high */
    bva = (adr + lnt - i - 1) & bvamqrx;
    if ((tr = WriteB (bva, c, VW)) != 0)                /* store byte */
        return tr;
    }                                                   /* end for */
SetCC2Dstr (lnt, dst);                                  /* check overflow */
return 0;
}

/* Store result in decimal accumulator

   Arguments:
        dst     =       decimal string structure
        cln     =       clean -0 if true

   Sets condition codes CC3 and CC4
   Bad digits and invalid sign are impossible */

void WriteDecA (dstr_t *dst, t_bool cln)
{
uint32 i, nz;

CC &= ~(CC3|CC4);                                       /* assume zero */
for (i = 0, nz = 0; i < DSTRLNT; i++) {                 /* save 32 digits */
    R[DECA + i] = dst->val[DSTRLNT - 1 - i];
    nz |= dst->val[DSTRLNT - 1 - i];
    }
if (nz)                                                 /* non-zero? */
    CC |= (dst->sign)? CC4: CC3;                        /* set CC3 or CC4 */
else if (cln)                                           /* zero, clean? */
    dst->sign = 0;                                      /* clear sign */
R[DECA + DSTRLNT - 1] |= (PKPLUS + dst->sign);          /* or in sign */
return;
}

/* Set CC2 for decimal string store

   Arguments:
        lnt     =       string length
        dst     =       decimal string structure
   Output:
        sets CC2 if information won't fit */

void SetCC2Dstr (uint32 lnt, dstr_t *dst)
{
uint32 i, limit, mask;
static uint32 masktab[8] = {
    0xFFFFFFF0, 0xFFFFFF00, 0xFFFFF000, 0xFFFF0000,
    0xFFF00000, 0xFF000000, 0xF0000000, 0x00000000
    };

lnt = (lnt * 2) - 1;                                    /* number of digits */
mask = 0;                                               /* can't ovflo */
limit = lnt / 8;                                        /* limit for test */
for (i = 0; i < DSTRLNT; i++) {                         /* loop thru value */
    if (i == limit)                                     /* @limit, get mask */
        mask = masktab[lnt % 8];
    else if (i > limit)                                 /* >limit, test all */
        mask = 0xFFFFFFFF;
    if (dst->val[i] & mask)                             /* test for ovflo */
        CC |= CC2;
    }
return;
}

/* Add decimal string magnitudes

   Arguments:
        s1      =       src1 decimal string
        s2      =       src2 decimal string
        ds      =       dest decimal string
        cy      =       carry in
   Output:
        1 if carry, 0 if no carry

   This algorithm courtesy Anton Chernoff, circa 1992 or even earlier.

   We trace the history of a pair of adjacent digits to see how the
   carry is fixed; each parenthesized item is a 4b digit.

   Assume we are adding:

        (a)(b)  I
   +    (x)(y)  J

   First compute I^J:

        (a^x)(b^y)      TMP

   Note that the low bit of each digit is the same as the low bit of
   the sum of the digits, ignoring the carry, since the low bit of the
   sum is the xor of the bits.

   Now compute I+J+66 to get decimal addition with carry forced left
   one digit:

        (a+x+6+carry mod 16)(b+y+6 mod 16)      SUM

   Note that if there was a carry from b+y+6, then the low bit of the
   left digit is different from the expected low bit from the xor.
   If we xor this SUM into TMP, then the low bit of each digit is 1
   if there was a carry, and 0 if not.  We need to subtract 6 from each
   digit that did not have a carry, so take ~(SUM ^ TMP) & 0x11, shift
   it right 4 to the digits that are affected, and subtract 6*adjustment
   (actually, shift it right 3 and subtract 3*adjustment).
*/

uint32 AddDstr (dstr_t *s1, dstr_t *s2, dstr_t *ds, uint32 cy)
{
uint32 i;
uint32 sm1, sm2, tm1, tm2, tm3, tm4;

for (i = 0; i < DSTRLNT; i++) {                         /* loop low to high */
    tm1 = s1->val[i] ^ (s2->val[i] + cy);               /* xor operands */
    sm1 = s1->val[i] + (s2->val[i] + cy);               /* sum operands */
    sm2 = sm1 + 0x66666666;                             /* force carry out */
    cy = ((sm1 < s1->val[i]) || (sm2 < sm1));           /* check for ovflo */
    tm2 = tm1 ^ sm2;                                    /* get carry flags */
    tm3 = (tm2 >> 3) | (cy << 29);                      /* compute adjust */
    tm4 = 0x22222222 & ~tm3;                            /* clrr where carry */
    ds->val[i] = (sm2 - (3 * tm4)) & WMASK;             /* final result */
    }
return cy;
}

/* Subtract decimal string magnitudes

   Arguments:
        s1      =       src1 decimal string
        s2      =       src2 decimal string
        ds      =       dest decimal string

   Note: the routine assumes that s1 <= s2

*/

void SubDstr (dstr_t *s1, dstr_t *s2, dstr_t *ds)
{
uint32 i;
dstr_t complm;

for (i = 0; i < DSTRLNT; i++)                           /* 9's comp s2 */
    complm.val[i] = 0x99999999 - s1->val[i];
AddDstr (&complm, s2, ds, 1);                           /* s1 + ~s2 + 1 */
return;
}

/* Compare decimal string magnitudes

   Arguments:
        s1      =       src1 decimal string
        s2      =       src2 decimal string
   Output:
        1 if >, 0 if =, -1 if <
*/

int32 CmpDstr (dstr_t *s1, dstr_t *s2)
{
int32 i;

for (i = DSTRLNT - 1; i >=0; i--) {
    if (s1->val[i] > s2->val[i])
        return 1;
    if (s1->val[i] < s2->val[i])
        return -1;
    }
return 0;
}

/* Get exact length of decimal string, clean -0

   Arguments:
        dst     =       decimal string structure
   Output:
        number of non-zero digits
*/

uint32 LntDstr (dstr_t *dst)
{
int32 nz, i;

for (nz = DSTRLNT - 1; nz >= 0; nz--) {
    if (dst->val[nz]) {
        for (i = 7; i >= 0; i--) {
            if ((dst->val[nz] >> (i * 4)) & 0xF)
                return (nz * 8) + i;
            }
        }
    }
dst->sign = 0;
return 0;
}

/* Word shift right

   Arguments:
        dsrc    =       decimal string structure
        sc      =       shift count in nibbles
*/

void GenRshift (dstr_t *dsrc, uint32 cnt)
{
uint32 i, sc, sc1;

sc = cnt / 8;
sc1 = cnt % 8;
if (sc) {
    for (i = 0; i < DSTRLNT; i++) {
        if ((i + sc) < DSTRLNT)
            dsrc->val[i] = dsrc->val[i + sc];
        else dsrc->val[i] = 0;
        }
    }
if (sc1)
    NibbleRshift (dsrc, sc1, 0);
return;
}

/* General shift left

   Arguments:
        dsrc    =       decimal string structure
        cnt      =      shift count in nibbles
*/

t_bool GenLshift (dstr_t *dsrc, uint32 cnt)
{
t_bool i, c, sc, sc1;

c = 0;
sc = cnt / 8;
sc1 = cnt % 8;
if (sc) {
    for (i = DSTRLNT - 1; (int32) i >= 0; i--) {
        if (i >= sc)
            dsrc->val[i] = dsrc->val[i - sc];
        else {
            c |= dsrc->val[i];
            dsrc->val[i] = 0;
            }
        }
    }
if (sc1)
    c |= NibbleLshift (dsrc, sc1, 0);
return (c? TRUE: FALSE);
}               

/* Nibble shift right

   Arguments:
        dsrc    =       decimal string structure
        sc      =       shift count in nibbles
        cin     =       carry in
*/

uint32 NibbleRshift (dstr_t *dsrc, uint32 sc, uint32 cin)
{
int32 i;
uint32 s, nc;

if ((s = sc * 4)) {
    for (i = DSTRLNT - 1; (int32) i >= 0; i--) {
        nc = (dsrc->val[i] << (32 - s)) & WMASK;
        dsrc->val[i] = ((dsrc->val[i] >> s) |
            cin) & WMASK;
        cin = nc;
        }
    return cin;
    }
return 0;
}

/* Nibble shift left

   Arguments:
        dsrc    =       decimal string structure
        sc      =       shift count in nibbles
        cin     =       carry in
*/

uint32 NibbleLshift (dstr_t *dsrc, uint32 sc, uint32 cin)
{
uint32 i, s, nc;

if ((s = sc * 4)) {
    for (i = 0; i < DSTRLNT; i++) {
        nc = dsrc->val[i] >> (32 - s);
        dsrc->val[i] = ((dsrc->val[i] << s) |
            cin) & WMASK;
        cin = nc;
        }
    return cin;
    }
return 0;
}
/* Edit instruction */

uint32 cis_ebs (uint32 rn, uint32 disp)
{
uint32 sa, da, c, d, dst, fill, pat;
uint32 tr;

disp = SEXT_LIT_W (disp) & WMASK;                       /* sext operand */
fill = S_GETMCNT (R[rn]);                               /* fill char */
while (S_GETMCNT (R[rn|1])) {                           /* while pattern */
    sa = (disp + R[rn]) & bvamqrx;                      /* dec str addr */
    da = R[rn|1] & bvamqrx;                             /* pattern addr */
    if ((tr = ReadB (da, &pat, VR)) != 0)               /* get pattern byte */
        return tr;
    switch (pat) {                                      /* case on pattern */

    case ED_DS:                                         /* digit select */
        if ((tr = ed_getsrc (sa, &c, &d)) != 0)         /* get src digit */
            return tr;
        if (CC & CC4)                                   /* signif? unpack */
            dst = ZONE | d;
        else if (d) {                                   /* non-zero? */
            R[1] = da;                                  /* save addr */
            dst = ZONE | d;                             /* unpack */
            CC |= CC4;                                  /* set signif */
            }
        else dst = fill;                                /* otherwise fill */
        if ((tr = WriteB (da, dst, VW)) != 0)           /* overwrite dst */
            return tr;
        ed_advsrc (rn, c);                              /* next src digit */
        break;

    case ED_SS:                                         /* signif start */
        if ((tr = ed_getsrc (sa, &c, &d)) != 0)         /* get src digit */
            return tr;
        if (CC & CC4)                                   /* signif? unpack */
            dst = ZONE | d;
        else if (d) {                                   /* non-zero? */
            R[1] = da;                                  /* save addr */
            dst = ZONE | d;                             /* unpack */
            }
        else {                                          /* otherwise */
            R[1] = da + 1;                              /* save next */
            dst = fill;                                 /* fill */
            }
        CC |= CC4;                                      /* set signif */
        if ((tr = WriteB (da, dst, VW)) != 0)           /* overwrite dst */
            return tr;
        ed_advsrc (rn, c);                              /* next src digit */
        break;

    case ED_SI:                                         /* signif immediate */
        if ((tr = ed_getsrc (sa, &c, &d)) != 0)         /* get src digit */
            return tr;
        R[1] = da;                                      /* save addr */
        dst = ZONE | d;                                 /* unpack */
        CC |= CC4;                                      /* set signif */
        if ((tr = WriteB (da, dst, VW)) != 0)           /* overwrite dst */
            return tr;
        ed_advsrc (rn, c);                              /* next src digit */
        break;

    case ED_FS:                                         /* field separator */
        CC &= ~(CC1|CC3|CC4);                           /* clr all exc CC2 */
        if ((tr = WriteB (da, fill, VW)) != 0)          /* overwrite dst */
            return tr;
        break;

    default:                                            /* all others */
        if ((CC & CC4) == 0) {                          /* signif off? */
            dst = (CC & CC1)? BLANK: fill;              /* blank or fill */
            if ((tr = WriteB (da, dst, VW)) != 0)       /* overwrite dst */
                return tr;
            }
         break;
         }                                              /* end switch dst */
    R[rn|1] = (R[rn|1] + S_ADDRINC) & WMASK;            /* next pattern */
    }                                                   /* end while */
return 0;
}

/* Routine to get and validate the next source digit */

uint32 ed_getsrc (uint32 sa, uint32 *c, uint32 *d)
{
uint32 tr;

if ((tr = ReadB (sa, c, VR)) != 0)                      /* read source byte */
    return tr;
*d = ((CC & CC2)? *c: *c >> 4) & 0xF;                   /* isolate digit */
if (*d > 0x9)                                           /* invalid? */
    return TR_DEC;
if (*d)                                                 /* non-zero? */
    CC |= CC3;
return 0;
}

/* Routine to advance source string */

void ed_advsrc (uint32 rn, uint32 c)
{
c = c & 0xF;                                            /* get low digit */
if (((CC & CC2) == 0) && (c > 0x9)) {                   /* sel left, with sign? */
    if ((c == 0xB) || (c == 0xD))                       /* minus? */
        CC = CC | (CC1|CC4);                            /* CC1, CC4 */
    else CC = (CC | CC1) & ~CC4;                        /* no, CC1, ~CC4 */
    R[rn] = R[rn] + 1;                                  /* skip two digits */
    }
else {                                                  /* adv 1 digit */
    if (CC & CC2)
        R[rn] = R[rn] + 1;
    CC = CC ^ CC2;
    }
return;
}
