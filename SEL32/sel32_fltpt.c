/* sel32_fltpt.c: SEL 32 floating point instructions processing.

   Copyright (c) 2018-2021, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This set of subroutines simulate the excess 64 floating point instructions.
   ADFW - add memory float to register
   ADFD - add memory double to register pair
   SUFW - subtract memory float from register
   SUFD - subtract memory double from register pair
   MPFW - multiply register by memory float
   MPFD - multiply register pair by memory double
   DVFW - divide register by memory float
   DVFD - divide register pair by memory double
   FIXW - convert float to integer (32 bit)
   FIXD - convert double to long long (64 bit)
   FLTW - convert integer (32 bit) to float
   FLTD - convert long long (64 bit) to double
   ADRFW - add regist float to register
   SURFW - subtract register float from register
   DVRFW - divide register float by register float
   MPRFW - multiply register float by register float
   ADRFD - add register pair double to register pair double
   SURFD - subtract register pair double from register pair double
   DVRFD - divide register pair double by register pair double
   MPRFD - multiply register pair double by register pair double

   Floating Point Formats
   float
   S - 1 sign bit
   X - 7 bit exponent
   M - 24 bit mantissa
   S XXXXXXX MMMMMMMM MMMMMMMM MMMMMMMM
   double
   S - 1 sign bit
   X - 7 bit exponent
   M - 56 bit mantissa
   S XXXXXXX MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM MMMMMMMM

*/

#include "sel32_defs.h"
#include <math.h>

uint32   s_fixw(uint32 val, uint32 *cc);
uint32   s_fltw(uint32 val, uint32 *cc);

t_uint64 s_fixd(t_uint64 val, uint32 *cc);
t_uint64 s_fltd(t_uint64 val, uint32 *cc);

uint32   s_nor(uint32 reg, uint32 *exp);
t_uint64 s_nord(t_uint64 reg, uint32 *exp);

uint32   s_mpfw(uint32 reg, uint32 mem, uint32 *cc);
uint32   s_dvfw(uint32 reg, uint32 mem, uint32 *cc);

uint32   s_adfw(uint32 reg, uint32 mem, uint32 *cc);
uint32   s_sufw(uint32 reg, uint32 mem, uint32 *cc);

t_uint64 s_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
t_uint64 s_sufd(t_uint64 reg, t_uint64 mem, uint32 *cc);

t_uint64 s_mpfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
t_uint64 s_dvfd(t_uint64 reg, t_uint64 mem, uint32 *cc);

uint32   s_normfw(uint32 num, uint32 *cc);
t_uint64 s_normfd(t_uint64 num, uint32 *cc);

#define NORMASK 0xf8000000              /* normalize 5 bit mask */
#define DNORMASK 0xf800000000000000ll   /* double normalize 5 bit mask */
#define EXMASK  0x7f000000              /* exponent mask */
#define FRMASK  0x80ffffff              /* fraction mask */
#define DEXMASK 0x7f00000000000000ll    /* exponent mask */
#define DFSVAL  0xff00000000000000ll    /* minus full scale value */
#define DFRMASK 0x80ffffffffffffffll    /* fraction mask */
#define NEGATE32(val)   ((~val) + 1)    /* negate a value 16/32/64 bits */

/**************************************************************
* Common routine for finishing the various F.P. instruction   *
*                                                             *
* Floating point operations not terminating with an arith-    *
* metic exception produce the following condition codes:      *
*                                                             *
* CC1   CC2   CC3   CC4               Definition              *
* -------------------------------------------------------     *
*  0     1     0     0    no exception, fraction positive     *
*  0     0     1     0    no exception, fraction negative     *
*  0     0     0     1    no exception, fraction = zero       *
*                                                             *
*                                                             *
* an arithmetic exception produces the follwing condition     *
* code settings:                                              *
*                                                             *
* CC1   CC2   CC3   CC4               Definition              *
* --------------------------------------------------------    *
*  1     0     1     0    exp underflow, fraction negative    *
*  1     0     1     1    exp overflow, fraction negative     *
*  1     1     0     0    exp underflow, fraction positive    *
*  1     1     0     1    exp overflow, fraction positive     *
*                                                             *
**************************************************************/  

/* normalize floating point fraction */
uint32 s_nor(uint32 reg, uint32 *exp) {
    uint32 texp = 0;                    /* no exponent yet */

    if (reg != 0) {                     /* do nothing if reg is already zero */
        uint32 mv = reg & NORMASK;      /* mask off bits 0-4 */
        while ((mv == 0) || (mv == NORMASK)) {
            /* not normalized yet, so shift 4 bits left */
            reg <<= 4;                  /* move over 4 bits */
            texp++;                     /* bump shift count */
            mv = reg & NORMASK;         /* just look at bits 0-4 */
        }
        /* bits 0-4 of reg is neither 0 nor all ones */
        /* show that reg is normalized */
        texp = (uint32)(0x40-(int32)texp);  /* subtract shift count from 0x40 */
    }
    *exp = texp;                        /* return exponent */
    return (reg);                       /* return normalized register */
}

/* normalize double floating point number */
t_uint64 s_nord(t_uint64 reg, uint32 *exp) {
    uint32 texp = 0;                    /* no exponent yet */

    if (reg != 0) {                     /* do nothing if reg is already zero */
        t_uint64 mv = reg & DNORMASK;   /* mask off bits 0-4 */
        while ((mv == 0) || (mv == DNORMASK)) {
            /* not normalized yet, so shift 4 bits left */
            reg <<= 4;                  /* move over 4 bits */
            texp++;                     /* bump shift count */
            mv = reg & DNORMASK;        /* just look at bits 0-4 */
        }
        /* bits 0-4 of reg is neither 0 nor all ones */
        /* show that reg is normalized */
        texp = (uint32)(0x40-(int32)texp);  /* subtract shift count from 0x40 */
    }
    *exp = texp;                        /* return exponent */
    return (reg);                       /* return normalized double register */
}

/* normalize the memory value when adding number to zero */
uint32 s_normfw(uint32 num, uint32 *cc) {
    uint32      ret;
    int32       val;                    /* temp word */
    int32       exp;                    /* exponent */
    int32       CCs;                    /* condition codes */
    uint8       sign;                   /* original sign */

    if (num == 0) {                     /* make sure we have a number */
        *cc = CC4BIT;                   /* set the cc's */
        return 0;                       /* return zero */
    }
    sign = 0;

    /* special case 0x80000000 (-0) to set CCs to 1011 * value to 0x80000001 */
    if (num == 0x80000000) {
        CCs = CC1BIT|CC3BIT|CC4BIT;     /* we have AE, exp overflow, neg frac */
        ret = 0x80000001;               /* return max neg value */

        /* return normalized number */
        *cc = CCs;                      /* set the cc's */
        return ret;                     /* return result */
    }

    /* special case pos exponent & zero mantissa to be 0 */
    if (((num & 0x80000000) == 0) && ((num & 0xff000000) > 0) && ((num & 0x00ffffff) == 0)) {
        ret = 0;                        /* 0 to any power is still 0 */
        CCs = CC4BIT;                   /* set zero CC */

        /* return normalized number */
        *cc = CCs;                      /* set the cc's */
        return ret;                     /* return result */
    }

    /* if we have 1xxx xxxx 0000 0000 0000 0000 0000 0000 */
    /* we need to convert to 1yyy yyyy 1111 0000 0000 0000 0000 0000 */
    /* where y = x - 1 */
    if ((num & 0x80ffffff) == 0x80000000) {
        int nexp = (0x7f000000 & num) - 0x01000000;
        num = 0x80000000 | (nexp & 0x7f000000) | 0x00f00000;
    }

    exp = (num & 0x7f000000) >> 24;     /* get exponent */
    if (num & 0x80000000) {             /* test for neg */
        sign = 1;                       /* we are neq */
        num = NEGATE32(num);            /* two's complement */
        exp ^= 0x7f;                    /* complement exponent */
    }
    val = num & 0x00ffffff;             /* get mantissa */

    /* now make sure number is normalized */
    while ((val != 0) && ((val & 0x00f00000) == 0)) {
        val <<= 4;                      /* move up a nibble */
        exp--;                          /* and decrease exponent */
    }

    if (exp < 0) {                      /* check for underflow */
        CCs = CC1BIT;                   /* we have underflow */
        if (sign & 1)                   /* we are neq */
            CCs |= CC3BIT;              /* set neg CC */
        else
            CCs |= CC2BIT;              /* set pos CC */
        ret = 0;                        /* number too small, make 0 */
        exp = 0;                        /* exponent too */

        /* return normalized number */
        *cc = CCs;                      /* set the cc's */
        return ret;                     /* return result */
    }

    /* rebuild normalized number */
    val = ((val & 0x00ffffff) | ((exp & 0x7f) << 24));
    if (sign & 1)                       /* we are neq */
        val = NEGATE32(val);            /* two's complement */
    if (val == 0)
        CCs = CC4BIT;                   /* show zero */
    else if (val & 0x80000000)          /* neqative? */
        CCs = CC3BIT;                   /* show negative */
    else
        CCs = CC2BIT;                   /* show positive */
    ret = val;                          /* return normalized number */

    /* return normalized number */
    *cc = CCs;                          /* set the cc's */
    return ret;                         /* return result */
}

#ifdef FOR_DEBUG
/* sfpval - determine floating point data value */
float sfpval(uint32 val)
{
    uint32      wd32;
    float       num;
    int32       exp;

    exp = ((val >> 24) & 0x7f) - 0x40;  /* get exponent and remove excess 0x40 */
    wd32 = val & 0x00ffffff;            /* get mantissa */
    num = (float)wd32;                  /* make it a float */
    num *= exp2f((4*exp) - 24);         /* raise to power of exponent */
    if (val & 0x80000000)
        num *= -1.0;                    /* if negative, return negative num */
    return num;                         /* return value */
}

/* dfpval - determine double floating point data value */
double dfpval(t_uint64 wd64)
{
    double      dbl;
    int32       exp;
    t_uint64   sav = wd64;

    if (wd64 & 0x8000000000000000ll)
        wd64 = NEGATE32(wd64);
    exp = ((wd64 >> 56) & 0x7f) - 0x40; /* get exponent and remove excess 0x40 */
    wd64 &= 0x00ffffffffffffffll;       /* get 56 bit mantissa */
    dbl = (double)wd64;                 /* make it a double float */
    dbl *= exp2((4*exp) - 56);          /* raise to power of exponent */
    if (sav & 0x8000000000000000ll)
        dbl *= -1.0;                    /* if negative, return negative num */
    return dbl;                         /* return value */
}
#endif /* FOR_DEBUG */

/* normalize the memory value when adding number to zero */
t_uint64 s_normfd(t_uint64 num, uint32 *cc) {
    t_uint64    ret;
    t_uint64    val;                    /* temp word */
    int32       exp;                    /* exponent */
    int32       CCs;                    /* condition codes */
    uint8       sign;                   /* original sign */

    if (num == 0) {                     /* make sure we have a number */
        *cc = CC4BIT;                   /* set the cc's */
        return 0;                       /* return zero */
    }
    sign = 0;

    /* special case 0x8000000000000000 (-0) to set CCs to 1011 */
    /* and value to 0x8000000000000001 */
    if (num == 0x8000000000000000LL) {
        CCs = CC1BIT|CC3BIT|CC4BIT; /* we have AE, exp overflow, neg frac */
        ret = 0x8000000000000001LL;     /* return max neg value */
        /* return normalized number */
        *cc = CCs;                      /* set the cc's */
        return ret;                     /* return normalized result */
    }

    /* special case pos exponent & zero mantissa to be 0 */
    if (((num & 0x8000000000000000LL) == 0) && ((num & 0xff00000000000000LL) > 0) && 
        (num & 0x00ffffffffffffffLL) == 0) {
        ret = 0;                        /* 0 to any power is still 0 */
        CCs = CC4BIT;                   /* set zero CC */
        /* return normalized number */
        *cc = CCs;                      /* set the cc's */
        return ret;                     /* return normalized result */
    }

    /* if we have 1xxx xxxx 0000 0000 0000 0000 0000 0000 */
    /* we need to convert to 1yyy yyyy 1111 0000 0000 0000 0000 0000 */
    /* where y = x - 1 */
    if ((num & 0x80ffffffffffffffLL) == 0x8000000000000000LL) {
        t_uint64 nexp = (0x7f00000000000000LL & num) - 0x0100000000000000LL;
        num = 0x8000000000000000LL | (nexp & 0x7f00000000000000LL) | 0x00f0000000000000LL;
    }

    exp = (num & 0x7f00000000000000LL) >> 56; /* get exponent */
    if (num & 0x8000000000000000LL) {   /* test for neg */
        sign = 1;                       /* we are neq */
        num = NEGATE32(num);            /* two's complement */
        exp ^= 0x7f;                    /* complement exponent */
    }
    val = num & 0x00ffffffffffffffLL;   /* get mantissa */

    /* now make sure number is normalized */
    while ((val != 0) && ((val & 0x00f0000000000000LL) == 0)) {
        val <<= 4;                      /* move up a nibble */
        exp--;                          /* and decrease exponent */
    }

    if (exp < 0) {
        CCs = CC1BIT;                   /* we have underflow */
        if (sign & 1)                   /* we are neg */
            CCs |= CC3BIT;              /* set neg CC */
        else
            CCs |= CC2BIT;              /* set pos CC */
        ret = 0;                        /* number too small, make 0 */
        /* return normalized number */
        *cc = CCs;                      /* set the cc's */
        return ret;                     /* return normalized result */
    }

    /* rebuild normalized number */
    ret = ((val & 0x00ffffffffffffffll) | (((t_uint64)exp & 0x7f) << 56));
    if (sign & 1)                       /* we were neg */
        ret = NEGATE32(ret);            /* two's complement */
    if (ret == 0)
        CCs = CC4BIT;                   /* show zero */
    else if (ret & 0x8000000000000000LL)    /* neqative? */
        CCs = CC3BIT;                   /* show negative */
    else
        CCs = CC2BIT;                   /* show positive */

    /* return normalized number */
    *cc = CCs;                          /* set the cc's */
    return ret;                         /* return normalized result */
}

/* convert from 32 bit float to 32 bit integer */
/* set CC1 if overflow/underflow exception */
uint32 s_fixw(uint32 fltv, uint32 *cc) {
    uint32 CC = 0, temp, temp2, sc;
    uint32 neg = 0;                     /* clear neg flag */

    if (fltv & MSIGN) {                 /* check for negative */
        fltv = NEGATE32(fltv);          /* make src positive */
        neg = 1;                        /* set neg val flag */
    } else {
        if (fltv == 0) {                /* value zero? */
            temp = 0;                   /* return zero */
            goto setcc;                 /* go set CC's */
        }
        /* gt 0, fall through */
    }
    temp2 = (uint32)(fltv >> 24);       /* get exponent */
    fltv <<= 8;                         /* move src to upper 3 bytes */
    temp2 -= 64;                        /* take off excess notation */
    temp = temp2;                       /* save val */
    if ((int32)temp2 == 0)              /* exp of zero means zero */
        goto setcc;                     /* go set CC's */
    if (temp2 & MSIGN) {
        /* set CC1 for underflow */
        if (neg) {
            temp = 0x7fffffff;          /* too big, set to max value */
            goto OVFLO;                 /* go set CC's */
        } else {
            temp = 0;                   /* assume zero for small values */
            goto UNFLO;                 /* go set CC's */
        }
    }

    temp2 -= 8;                         /* see if in range */
    temp = fltv;                        /* save val */
    if ((temp2 == 0) && (fltv == 0x80000000) && (neg == 1))
        goto setcc;                     /* go set CC's */
    if ((int32)temp2 > 0) {
        /* set CC1 for overflow */
        temp = 0;                       /* assume zero for small values */
        goto OVFLO;                     /* go set CC's */
    }
    sc = (NEGATE32(temp2) * 4);         /* pos shift cnt * 4 */
    fltv >>= sc;                        /* do 4 bit shifts */

    /* see if overflow to neg */
    /* set CC1 for overflow */
    if (fltv & MSIGN) {
        /* set CC1 for overflow */
        temp = 0;                       /* assume zero for small values */
        goto OVFLO;                     /* go set CC's */
    }

    /* see if original value was negative */
    if (neg)
        fltv = NEGATE32(fltv);          /* put back to negative */
    temp = fltv;                        /* return integer value */
    /* come here to set cc's and return */
    /* temp has return value */
setcc:
    if (temp & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp for destination reg */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */

    /* handle underflow/overflow */
OVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
UNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (neg)                            /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */
}

/* convert from 32 bit integer to 32 bit float */
/* No overflow (CC1) can be generated */
uint32 s_fltw(uint32 intv, uint32 *cc) {
    uint32  CC = 0;
    uint32  ret;
    uint32  neg = 0;                    /* zero sign flag */
    uint32  exp = 0;                    /* exponent */
    uint32  val = (int32)intv;          /* integer value */

    if (intv & 0x80000000) {
        val = NEGATE32(intv);
        neg = 1;
    }
    while ((val != 0) && ((val & 0xf0000000) == 0)) {
        val <<= 4;                      /* no round up */
        exp--;
    }
    if (val != 0)
        exp += 0x48;                    /* set default exponent */
    /* shift value rt 8 bits and round */
    if (val & 0x80) {
        if (neg) {
            if (val & 0x7f)
                val = (val >> 8) + 1;   /* round up */
            else
                val = (val >> 8);       /* no round up */
        } else {
            val = (val >> 8) + 1;       /* round up */
        }
    } else
        val = (val >> 8);               /* no round up */
    if (val & 0x01000000) {
        val = (val >> 4);               /* move 1 nibble */
        exp++;
    }
    ret = (exp << 24) | (val & 0x00ffffff); /* merge value */
    if (neg)
        ret = NEGATE32(ret);
    if (ret & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (ret == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp for destination reg */
    *cc = CC;                           /* save CC's */
    return ret;                         /* return results */
}

/* convert from 64 bit double to 64 bit integer */
/* set CC1 if overflow/underflow exception */
t_uint64 s_fixd(t_uint64 dblv, uint32 *cc) {
    uint32 temp2, CC = 0, neg = 0, sc = 0;
    t_uint64 dest;

    /* neg and CC flags already set to zero */
    if (dblv & DMSIGN) {
        dblv = NEGATE32(dblv);          /* make src positive */
        neg = 1;                        /* set neg val flag */
    } else {
        if (dblv == 0) {
            dest = 0;                   /* return zero */
            goto dodblcc;               /* go set CC's */
        }
        /* gt 0, fall through */
    }

    temp2 = (uint32)(dblv >> 56);       /* get exponent */
    dblv <<= 8;                         /* move fraction to upper 7 bytes */
    temp2 -= 64;                        /* take off excess notation */
    dest = temp2;                       /* save val */
    if ((int32)temp2 == 0)              /* zero exp means zero */
        goto dodblcc;                   /* go set CC's */
    if (temp2 & MSIGN) {
        /* set CC1 for underflow */
        if (neg) {
            dest = 0x7fffffffffffffff;  /* too big, set to max value */
            goto DOVFLO;                /* go set CC's */
        } else {
            dest = 0;                   /* assume zero for small values */
            goto DUNFLO;                /* go set CC's */
        }
    }

    temp2 -= 16;                        /* see if in range */
    dest = dblv;                        /* save val */
    if ((temp2 == 0) && (dblv == DMSIGN) && (neg == 1))
        goto dodblcc;                   /* go set CC's */
    if ((int32)temp2 > 0) {
        /* set CC1 for overflow */
        dest = 0;                       /* assume zero for small values */
        goto DOVFLO;                    /* go set CC's */
    }
    sc = (NEGATE32(temp2) * 4);         /* pos shift cnt * 4 */
    dblv >>= sc;                        /* do 4 bit shifts */

    /* see if overflow to neg */
    if (dblv & DMSIGN) {
        /* set CC1 for overflow */
        dest = 0;                       /* assume zero for small values */
        goto DOVFLO;                    /* go set CC's */
    }
    /* see if original values was negative */
    if (neg)
        dblv = NEGATE32(dblv);          /* put back to negative */
    dest = dblv;                        /* return integer value */

dodblcc:
    /* dest has return value */
    if (dest & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (dest == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    *cc = CC;                           /* return CC's */
    return dest;                        /* return result */

    /* handle underflow/overflow */
DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (neg)                            /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    return dest;                        /* return result */
}

/* convert from 64 bit integer to 64 bit double */
/* No overflow (CC1) can be generated */
t_uint64 s_fltd(t_uint64 intv, uint32 *cc) {
    t_uint64 ret = 0;                   /* zero return val */
    uint32  neg = 0;                    /* zero sign flag */
    uint32  CC = 0;                     /* n0 CC's yet */
    uint32  exp = 0;                    /* exponent */
    t_uint64 val = intv;                /* integer value */

    if (intv & DMSIGN) {
        val = NEGATE32(intv);           /* make src positive */
        neg = 1;                        /* set neg flag */
    } else {
        if (intv == 0) {                /* see if zero */
            ret = 0;                    /* return zero */
            CC = CC4BIT;                /* CC4 for zero */
            /* return 0 for destination regs */
            *cc = CC;                   /* return CC's */
            return ret;                 /* return result */
        }
        /* gt 0, fall through */
    }

    /* see if normalized */
    while ((val) && ((val & 0xf000000000000000ll) == 0)) {
        val <<= 4;                      /* zero, shift in next nibble */
        exp--;                          /* decr exp value */
    }
    if (val != 0)
        exp += 0x50;                    /* default exponent */

    /* shift value rt 8 bits and round */
    if (val & 0x91ll) {
        if (neg) {
            if (val & 0x7fl)
                val = (val >> 8) + 1;   /* round up */
            else
                val = (val >> 8);       /* no round up */
        } else {
            if (val & 0x7fl)
                val = (val >> 8);       /* no round up */
            else
                val = (val >> 8) + 1;   /* round up */
        }
    } else
        val = (val >> 8);               /* no round up */

    if (val & 0x0100000000000000ll) {
        val = (val >> 4);               /* no round up */
        exp++;
    }
    ret = (((t_uint64)exp) << 56) | (val & 0x00ffffffffffffffll); /* merge value */

    if (neg)
        ret = NEGATE32(ret);
    if (ret & DMSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else if (ret == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */

    /* return temp for destination regs */
    *cc = CC;                           /* return CC's */
    return ret;                         /* return result */
}

#define CMASK   0x10000000              /* carry mask */
#define EMASK   0x7f000000              /* single exponent mask */
#define UMASK   0x0ffffff0              /* single fp mask */
#define XMASK   0x0fffffff              /* single fp mask */
#define MMASK   0x00ffffff              /* single mantissa mask */
#define NMASK   0x0f000000              /* single nibble mask */
#define ZMASK   0x00f00000              /* single nibble mask */

/* this new version is perfect against the diags, so good */
/* do new SEL floating add derived from IBM370 code */
/* Add/Sub single floating point */
uint32 s_adfw(uint32 reg, uint32 mem, uint32 *cc)
{
    uint32      res, ret;
    char        sign = 0;
    int         er, em, temp;
    uint32      CC;

    /* first we want to make sure the numbers are normalized */
    ret = s_normfw(reg, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    reg = ret;                          /* use normalized value */
    if (mem == 0) {                     /* test for add of zero */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return normalized results */
    }

    ret = s_normfw(mem, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    mem = ret;                          /* use normalized value */
    if (reg == 0) {                     /* test for add to zero */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results results */
    }

    /* extract reg exponent and mantissa */
    if (reg & MSIGN) {                  /* reg negative */
        sign |= 2;                      /* set neg flag */
        reg = NEGATE32(reg);            /* make negative positive */
    }
    er = (reg & EMASK) >> 24;           /* extract reg exponent */
    reg &= MMASK;                       /* extract reg mantissa */

    /* extract mem exponent and mantissa */
    if (mem & MSIGN) {                  /* mem negative */
        sign |= 1;                      /* set neg flag */
        mem = NEGATE32(mem);            /* make negative positive */
    }
    em = (mem & EMASK) >> 24;           /* extract mem exponent */
    mem &= MMASK;                       /* extract mem mantissa */

    temp = er - em;                     /* get signed exp difference */
    mem = mem << 4;                     /* align mem for guard digit */
    reg = reg << 4;                     /* align reg for guard digit */

    if (temp > 0) {                     /* reg exp > mem exp */
        if (temp > 8) {
            mem = 0;                    /* if too much difference, make zero */
        } else {
            /* Shift mem right if reg has larger exponent */
            while (temp-- != 0) {
                mem >>= 4;              /* adjust for exponent difference */
                em++;                   /* bump exponent */
            }
        }
    } else
    if (temp < 0) {                     /* reg < mem exp */
        if (temp < -8) {
            reg = 0;                    /* if too much difference, make zero */
            er = em;                    /* make exponents the same using mem exp */
        } else {
            /* Shift reg right if mem has larger exponent */
            while (temp++ != 0) {
                reg >>= 4;              /* adjust for exponent difference */
                er++;                   /* bump exponent */
            }
        }
    }

    /* exponents should be equal now */
    /* add results */
    if (sign == 2 || sign == 1) {
        /* different signs so do subtract */
        mem ^= XMASK;                   /* complement the value */
        mem++;                          /* increment the value */
        res = reg + mem;                /* add the values */
        if (res & CMASK) {              /* see if carry */
            res &= XMASK;               /* clear off the carry bit */
        } else {
            sign ^= 2;                  /* flip the sign */
            res ^= XMASK;               /* and negate the value by comp */
            res++;                      /* incr */
        }
    } else {
        res = reg + mem;                /* same sign, just add */
        if (sign == 3)
            res += 7;                   /* round number */
    }

    /* If overflow, shift right 4 bits */
    if (res & CMASK) {                  /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 128) {                /* if exponent too large, overflow */
            CC = CC1BIT|CC4BIT;         /* set arithmetic overflow */
            /* OVERFLOW */
            /* set CC2 & CC3 on exit */
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            if (CC & CC3BIT)            /* NEG overflow? */
                res = 0x80000001;       /* yes */
            else
                res = 0x7FFFFFFF;       /* no, pos */
            /* Store result */
            *cc = CC;                   /* save CC's */
            return res;
        }
    }

    CC = 0;                             /* no CC's yet */
    /* Set condition codes */
    if (res != 0)                       /* see if non zero */
        CC |= (sign & 2) ? 1 : 2;
    else {
        er = sign = 0;                  /* we have zero CC4 */
    }

    /* normalize the fraction */
    if (CC != 0) {                      /* check for zero value */
        while ((res != 0) && ((res & NMASK) == 0)) {
            res <<= 4;                  /* adjust mantisa by a nibble */
            er--;                       /* and adjust exponent smaller by 1 */
        }
        /* Check if underflow */
        if (er < 0) {
            /* UNDERFLOW */
            CC |= CC1BIT;               /* set arithmetic exception */
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            res = 0;                    /* make all zero */
            /* Store result */
            *cc = CC;                   /* save CC's */
            return res;                 /* return value */
        }
    } else {
        /* result is zero */
        sign = er = 0;                  /* make abs zero */
    }

    res >>= 4;                          /* remove the guard nibble */

    /* create result */
    res |= (er << 24) & EXMASK;         /* combine exponent & mantissa */

    /* set the CC's */
    if (CC == 0) {
        CC = CC4BIT;                    /* zero value */
    } else {
        if (sign & 2)  
            res = NEGATE32(res);        /* make negative */
        CC = (CC & 3) << 28;            /* neg is CC3, pos is CC2 */
    }

    *cc = CC;                           /* save CC's */
    return res;                         /* return result */
}

/* subtract memory floating point number from register floating point number */
uint32 s_sufw(uint32 reg, uint32 mem, uint32 *cc) {
    return s_adfw(reg, NEGATE32(mem), cc);
}

/* multiply register floating point number by memory floating point number */
/* set CC1 if overflow/underflow */
/* use revised normalization code */
uint32 s_mpfw(uint32 reg, uint32 mem, uint32 *cc) {
    uint32      res, ret;
    int         sign = 0;
    int         lsb = 0;
    int         er, em, temp;
    uint32      CC;

    /* first we want to make sure the numbers are normalized */
    ret = s_normfw(reg, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    reg = ret;                          /* use normalized value */

    ret = s_normfw(mem, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    mem = ret;                          /* use normalized value */

    /* see if multiply by zero */
    if ((reg == 0) || (mem == 0)) {     /* test for mult by zero */
        *cc = CC4BIT;                   /* set CC 4 for 0 */
        return 0;                       /* return results */
    }

    /* extract reg exponent and mantissa */
    if (reg & MSIGN) {                  /* reg negative */
        sign ^= 1;                      /* set neg flag */
        reg = NEGATE32(reg);            /* make negative positive */
    }
    if (reg & 0x1)                      /* test lsb */
        lsb = 1;                        /* reg is odd */
    er = (reg & EXMASK) >> 24;          /* extract reg exponent */
    reg &= MMASK;                       /* extract reg mantissa */

    /* extract mem exponent and mantissa */
    if (mem & MSIGN) {                  /* mem negative */
        sign ^= 1;                      /* set neg flag */
        mem = NEGATE32(mem);            /* make negative positive */
    }
    if (mem & 0x1)                      /* test lsb */
        lsb = 1;                        /* reg is odd */
    em = (mem & EXMASK) >> 24;          /* extract mem exponent */
    mem &= MMASK;                       /* extract mem mantissa */

    er = er + em - 0x40;                /* get the exp value */
    reg = reg << 4;                     /* create guard digit */
    mem = mem << 4;                     /* create guard digit */

    res = 0;                            /* zero result for multiply */
    /* Do multiply with guard bit */
    for (temp = 0; temp < 28; temp++) {
        /* Add if we need too */
        if (reg & 1)
            res += mem;
        /* Shift right by one */
        reg >>= 1;
        res >>= 1;
    }

    /* fix up some boundry rounding */
    if ((res >= 0x01000000) && (sign == 0)) {
        res += 0x8;
    }
    if ((res == 0x00FFFFFF) && (sign == 1) && (er != 1)) {
        if (lsb == 1) {
            if ((er != 0x41) && (er != 0x81)) {
                res += 0x1;
            }
        }
    }

    /* If overflow, shift right 4 bits */
    if (res & 0x70000000) {             /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 128) {                /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT;                /* set arithmetic exception */
            CC |= (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            if (CC & CC3BIT)            /* NEG overflow? */
                res = 0x80000001;       /* double yes */
            else
                res = 0x7FFFFFFF;       /* no, pos */
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
    }

    /* Align the results & normalize */
    if (res != 0) {
        while ((res != 0) && (res & NMASK) == 0) {
            res <<= 4;
            er--;
        }
        /* Check if overflow */
        if (er >= 128) {                /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT|CC4BIT;         /* set arithmetic exception */
            if (sign & 1) {
                CC |= CC3BIT;
                res = 0x80000001;       /* neg overflow 1011 */
            } else {
                CC |= CC2BIT;
                res = 0x7FFFFFFF;       /* pos overflow 1101 */
            }
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
        /* Check if underflow */
        if (er < 0) {
            /* UNDERFLOW */
            res = 0;                    /* make return value zero */
            CC = (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            CC |= CC1BIT;               /* set arithmetic exception */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
        res >>= 4;                      /* remove guard nibble */
    } else
        er = sign = 0;

    res &= MMASK;                       /* clear exponent */

    res |= ((((uint32)er) << 24) & EXMASK); /* merge exp and mantissa */

    if (sign == 1)                      /* is result to be negative */
        res = NEGATE32(res);            /* make value negative */

    CC = 0;
    if (res != 0)                       /* see if non zero */
        CC = (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
    else
        CC = CC4BIT;                    /* set zero cc */

    /* return results */
    *cc = CC;                           /* save CC's */
    return res;                         /* return results */
}

/* divide register float by memory float */
uint32 s_dvfw(uint32 reg, uint32 mem, uint32 *cc) {
    uint32 CC = 0, temp, temp2, sign;
    uint32 expm, expr;
    t_uint64 dtemp;

    /* process operator */
    sign = mem & MSIGN;                 /* save original value for sign */
    if (mem == 0)                       /* check for divide by zero */
        goto DOVFLO;                    /* go process divide overflow */

    if (mem & MSIGN)                    /* check for negative */
        mem = NEGATE32(mem);            /* make mem positive */

    expm = (mem >> 24);                 /* get operand exponent */
    mem <<= 8;                          /* move fraction to upper 3 bytes */
    mem >>= 1;                          /* adjust fraction for divide */

    /* process operand */
    if (reg == 0) {
        temp = 0;                       /* return zero */
        goto setcc;                     /* go set CC's */
    }
    if (reg & MSIGN) {                  /* check for negative */
        reg = NEGATE32(reg);            /* make reg positive */
        sign ^= MSIGN;                  /* complement sign */
    }
    expr = (reg >> 24);                 /* get operator exponent */
    reg <<= 8;                          /* move fraction to upper 3 bytes */
    reg >>= 6;                          /* adjust fraction for divide */

    temp = expr - expm;                 /* subtract exponents */
    dtemp = ((t_uint64)reg) << 32;      /* put reg fraction in upper 32 bits */
    temp2 = (uint32)(dtemp / mem);      /* divide reg fraction by mem fraction */
    temp2 >>= 3;                        /* shift out excess bits */
    temp2 <<= 3;                        /* replace with zero bits */

    if (sign & MSIGN)
        temp2 = NEGATE32(temp2);        /* if negative, negate fraction */
    /* normalize the result in temp and put exponent into expr */
    temp2 = s_nor(temp2, &expr);        /* normalize fraction */
    temp += 1;                          /* adjust exponent */

//RROUND:
    if (temp2 == MSIGN) {               /* check for minus zero */
        temp2 = 0xF8000000;             /* yes, fixup value */
        expr++;                         /* bump exponent */
    }

    if ((int32)temp2 >= 0x7fffffc0)     /* check for special rounding */
        goto RRND2;                     /* no special handling */

    if (expr != 0x40) {                 /* result normalized? */
        goto RRND2;                     /* if not, don't round */
    }
    /* result normalized */
    if ((sign & MSIGN) == 0)
        goto RRND1;                     /* if sign set, don't round yet */
    expr += temp;                       /* add exponent */

    if (expr & MSIGN)                   /* test for underflow */
        goto DUNFLO;                    /* go process underflow */

    if ((int32)expr > 0x7f)             /* test for overflow */
        goto DOVFLO;                    /* go process overflow */

    expr ^= FMASK;                      /* complement exponent */
    temp2 += 0x40;                      /* round at bit 25 */
    goto RRND3;                         /* go merge code */

RRND1:
    temp2 += 0x40;                      /* round at bit 25 */
RRND2:
    expr += temp;                       /* add exponent */

    if (expr & MSIGN)                   /* test for underflow */
        goto DUNFLO;                    /* go process underflow */

    if ((int32)expr > 0x7f)             /* test for overflow */
        goto DOVFLO;                    /* go process overflow */

    if (sign & MSIGN)                   /* test for negative */
        expr ^= FMASK;                  /* yes, complement exponent */
RRND3:
    temp2 <<= 1;                        /* adjust fraction */
    temp = (expr << 24) | (temp2 >> 8); /* merge exp & fraction */
    goto setcc;                         /* go set CC's */

DOVFLO:
    CC |= CC4BIT;                       /* set CC4 for exponent overflow */
DUNFLO:
    CC |= CC1BIT;                       /* set CC1 for arithmetic exception */
    if (sign & MSIGN)                   /* test for negative */
        CC |= CC3BIT;                   /* set neg fraction bit CC3 */
    else
        CC |= CC2BIT;                   /* set pos fraction bit CC2 */
    *cc = CC;                           /* return CC's */
    /* return value is not valid, but return fixup value anyway */
    switch ((CC >> 27) & 3) {           /* rt justify CC3 & CC4 */
    case 0:
        return 0;                       /* pos underflow */
        break;
    case 1:
        return 0x7fffffff;              /* positive overflow */
        break;
    case 2:
        return 0;                       /* neg underflow */
        break;
    case 3:
        return 0x80000001;              /* negative overflow */
        break;
    }
setcc:
    /* come here to set cc's and return */
    /* temp has return value */
    if (temp & MSIGN)
        CC |= CC3BIT;                   /* CC3 for neg */
    else
    if (temp == 0)
        CC |= CC4BIT;                   /* CC4 for zero */
    else 
        CC |= CC2BIT;                   /* CC2 for greater than zero */
    /* return temp to destination reg */
    *cc = CC;                           /* return CC's */
    return temp;                        /* return result */
}

#define DMMASK  0x00ffffffffffffffLL    /* double mantissa mask */
#define DCMASK  0x1000000000000000LL    /* double carry mask */
#define DIBMASK 0x0fffffffffffffffLL    /* double fp nibble mask */
#define DUMASK  0x0ffffffffffffff0LL    /* double fp mask */
#define DNMASK  0x0f00000000000000LL    /* double nibble mask */
#define DZMASK  0x00f0000000000000LL    /* shifted nibble mask */

/* add memory floating point number to register floating point number */
/* this code creates an extra guard digit, so it is more accurate than SEL */
/* The code was modified to have the same results as SEL, so we will use this one */
/* set CC1 if overflow/underflow */
t_uint64 s_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc)
{
    t_uint64    res, ret;
    uint8       sign = 0;
    int         er, em, temp;
    uint32      CC;

    /* first we want to make sure the numbers are normalized */
    ret = s_normfd(reg, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    reg = ret;                          /* use normalized value */
    if (mem == 0) {                     /* test for add of zero */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return normalized results */
    }

    ret = s_normfd(mem, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    mem = ret;                          /* use normalized value */
    if (reg == 0) {                     /* test for add to zero */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results results */
    }

    /* process the memory operand value */
    /* extract exponent and mantissa */
    if (reg & DMSIGN) {                 /* reg negative */
        sign |= 2;                      /* set neg flag */
        reg = NEGATE32(reg);            /* make negative positive */
    }
    er = (reg & DEXMASK) >> 56;         /* extract reg exponent */
    reg &= DMMASK;                      /* extract reg mantissa */

    /* extract mem exponent and mantissa */
    if (mem & DMSIGN) {                 /* mem negative */
        sign |= 1;                      /* set neg flag */
        mem = NEGATE32(mem);            /* make negative positive */
    }
    em = (mem & DEXMASK) >> 56;         /* extract mem exponent */
    mem &= DMMASK;                      /* extract mem mantissa */

    mem = mem << 4;                     /* align mem for normalization */
    reg = reg << 4;                     /* align reg for normalization */
    temp = er - em;                     /* get signed exp difference */

    if (temp > 0) {                     /* reg exp > mem exp */
        if (temp > 15) {  
            mem = 0;                    /* if too much difference, make zero */
        } else {
            /* Shift mem right if reg has larger exponent */
            mem >>= (4 * temp);         /* adjust for exponent difference */
        }
    } else
    if (temp < 0) {                     /* reg < mem exp */
        if (temp < -15) {
            reg = 0;                    /* if too much difference, make zero */
        } else
            /* Shift reg right if mem larger */
            reg >>= (4 * (-temp));      /* adjust for exponent difference */
        er = em;                        /* make exponents the same */
    }

    /* er now has equal exponent for both values */
    /* add results */
    if (sign == 2 || sign == 1) {
        /* different signs so do subtract */
        mem ^= DIBMASK;                 /* complement the value and inc */
        mem++;                          /* negate all but upper nibble */
        res = reg + mem;                /* add the values */
        if (res & DCMASK) {             /* see if carry */
            res &= DIBMASK;             /* clear off the carry bit */
        } else {
            sign ^= 2;                  /* flip the sign */
            res ^= DIBMASK;             /* and negate the value */
            res++;                      /* negate all but the upper nibble */
        }
    } else {
        res = reg + mem;                /* same sign, just add */
        if ((res & 0x0fffffffffffff80ll) != 0x0fffffffffffff80ll) {
            if (sign == 3)
                res += 7;               /* round number */
            if ((sign == 3) && (er == 0x7e))
                res |= 0x0f00ll;        /* round number more */
            if (sign == 0)
                res += 0xf;             /* round number */
        }
    }
    /* following statement effectively removes guard nibble to be like SEL */
    res &= 0xfffffffffffffff0ll;        /* remove extra bits */

    /* If overflow, shift right 4 bits */
    if (res & DCMASK) {                 /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 128) {                /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT|CC4BIT;         /* set arithmetic overflow */
            /* set CC2 & CC3 on exit */
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            if (CC & CC3BIT)            /* NEG overflow? */
                res = 0x8000000000000001;   /* double yes */
            else
                res = 0x7FFFFFFFFFFFFFFF;   /* no, pos */
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
    }

    CC = 0;
    /* Set condition codes */
    if (res != 0)                       /* see if non zero */
        CC |= (sign & 2) ? 1 : 2;
    else {
        er = sign = 0;                  /* we have zero CC4 */
    }

    /* normalize the fraction */
    if (res != 0) {                     /* see if non zero */
        while ((res != 0) && (res & DNMASK) == 0) {
            res <<= 4;                  /* adjust mantisa by a nibble */
            er--;                       /* and adjust exponent smaller by 1 */
        }
        /* Check if exponent underflow */
        if (er < 0) {
            /* UNDERFLOW */
            CC |= CC1BIT;               /* set arithmetic exception */
            CC |= (sign & 2)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            res = 0;                    /* make all zero */
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
    } else {
        /* result is zero */
        sign = er = 0;                  /* make abs zero */
    }

    res >>= 4;                          /* remove the carryout nibble */
    res &= DMMASK;                      /* clear exponent */

    res |= ((((t_uint64)er) << 56) & DEXMASK); /* merge exp and mantissa */

    /* Set condition codes */
    if (CC == 0) {
        CC = CC4BIT;                    /* set zero cc */
    } else {
        if (sign & 2)                   /* see if negative */
            res = NEGATE32(res);        /* make negative */
        CC = (CC & 3) << 28;            /* neg is CC3, pos is CC2 */
    }
    /* store results */
    *cc = CC;                           /* save CC's */
    return res;                         /* return results */
}

/* subtract memory floating point number from register floating point number */
t_uint64 s_sufd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    return s_adfd(reg, NEGATE32(mem), cc);
}

/* multiply register floating point number by memory floating point number */
/* set CC1 if overflow/underflow */
/* use revised normalization code */
t_uint64 s_mpfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    t_uint64   res, ret;
    int         sign = 0;
    int         lsb = 0;
    int         er, em, temp;
    uint32      CC;

    /* first we want to make sure the numbers are normalized */
    ret = s_normfd(reg, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    reg = ret;                          /* use normalized value */

    ret = s_normfd(mem, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    mem = ret;                          /* use normalized value */

    /* see if multiply by zero */
    if ((reg == 0ll) || (mem == 0ll)) { /* test for mult by zero */
        *cc = CC4BIT;                   /* set CC 4 for 0 */
        return 0ll;                     /* return results results */
    }

    /* extract reg exponent and mantissa */
    if (reg & DMSIGN) {                 /* reg negative */
        sign ^= 1;                      /* set neg flag */
        reg = NEGATE32(reg);            /* make negative positive */
    }
    if (reg & 0x1ll)                    /* test lsb */
        lsb = 1;                        /* reg is odd */
    er = (reg & DEXMASK) >> 56;         /* extract reg exponent */
    reg &= DMMASK;                      /* extract reg mantissa */

    /* extract mem exponent and mantissa */
    if (mem & DMSIGN) {                 /* mem negative */
        sign ^= 1;                      /* set neg flag */
        mem = NEGATE32(mem);            /* make negative positive */
    }
    if (mem & 0x1ll)                    /* test lsb */
        lsb = 1;                        /* reg is odd */
    em = (mem & DEXMASK) >> 56;         /* extract mem exponent */
    mem &= DMMASK;                      /* extract mem mantissa */

    er = er + em - 0x40;                /* get the exp value */

    res = 0;                            /* zero result for multiply */
    /* multiply by doing shifts and adds */
    for (temp = 0; temp < 56; temp++) {
        /* Add if we need too */
        if (reg & 1)
            res += mem;
        /* Shift right by one */
        reg >>= 1;
        res >>= 1;
    }
    er++;                               /* adjust exp for extra nible shift */

    /* fix up some boundry conditions */
    if ((res >= 0x0010000000000000ll) && (sign == 1)) {
        res += 0x1;
    }
    else
    if ((res == 0x000FFFFFFFFFFFFFll) && (sign == 1) && (er != 1)) {
        if (lsb == 0) {
            if ((er == 0x41) || (er == 0x81)) {
                er++;
            }
        } else {
            res += 0x1ll;
        }
    }

    /* If overflow, shift right 4 bits */
    if (res & DEXMASK) {                /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 0x80) {               /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT;                /* set arithmetic exception */
            CC |= (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            if (CC & CC3BIT)            /* NEG overflow? */
                res = 0x8000000000000001ll;   /* double yes */
            else
                res = 0x7FFFFFFFFFFFFFFFll;   /* no, pos */
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
    }

    /* Align the results */
    if (res != 0) {
        while ((res != 0) && (res & DNMASK) == 0) {
            res <<= 4;                  /* move over mantessa */
            er--;                       /* reduce exponent cocunt by 1 */
            if ((res == 0x00FFFFFFFFFFFFF0ll) && (sign == 1)) {
                if (lsb == 0) {
                    er--;
                }
                else {
                    res += 0x10ll;
                }
            }
        }
        /* Check if overflow */
        if (er >= 128) {                /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT|CC4BIT;         /* set arithmetic exception */
            if (sign & 1) {
                CC |= CC3BIT;           /* neg CC */
                res = 0x8000000000000001ll; /* neg overflow 1011 */
            } else {
                CC |= CC2BIT;           /* pos CC */
                res = 0x7FFFFFFFFFFFFFFFll; /* pos overflow 1101 */
            }
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
        /* Check if underflow */
        if (er < 0) {
            /* UNDERFLOW */
            res = 0;                    /* make return value zero */
            CC = (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            CC |= CC1BIT;               /* set arithmetic exception */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
        res >>= 4;                      /* remove guard nibble */
    } else {
        er = sign = 0;                  /* have real zero */
    }

    res &= DMMASK;                      /* clear exponent */

    res |= ((((t_uint64)er) << 56) & DEXMASK); /* merge exp and mantissa */
    if (sign == 1)                      /* is result to be negative */
        res = NEGATE32(res);            /* make value negative */

    /* determine CC's for result */
    CC = 0;
    if (res != 0)                       /* see if non zero */
        CC = (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
    else
        CC = CC4BIT;                    /* set zero cc */

    /* return results */
    *cc = CC;                           /* save CC's */
    return res;                         /* return results */
}

/* divide register floating point number by memory floating point number */
/* set CC1 if overflow/underflow */
/* use revised normalization code */
t_uint64 s_dvfd(t_uint64 reg, t_uint64 mem, uint32 *cc) {
    t_uint64    res, ret;
    int         sign = 0;
    int         sign2 = 0;
    int         lsb = 0;
    int         er, em, temp;
    uint32      CC;

    /* first we want to make sure the numbers are normalized */
    ret = s_normfd(reg, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    reg = ret;                          /* use normalized value */

    ret = s_normfd(mem, &CC);           /* get the reg value */
    if (CC & CC1BIT) {                  /* see if we have AE */
        *cc = CC;                       /* save CC's */
        return ret;                     /* return results */
    }
    mem = ret;                          /* use normalized value */

    /* see if divide by or into zero */
    if ((reg == 0ll) || (mem == 0ll)) { /* test for divide by zero */
        *cc = CC4BIT;                   /* set CC 4 for 0 */
        return 0ll;                     /* return results */
    }

    /* extract reg exponent and mantissa */
    if (reg & DMSIGN) {                 /* reg negative */
        sign ^= 1;                      /* set neg flag */
        reg = NEGATE32(reg);            /* make negative positive */
        sign2 |= 2;                     /* set neg flag */
    }
    if (reg & 0x1ll)                    /* test lsb */
        lsb = 1;                        /* reg is odd */
    er = (reg & DEXMASK) >> 56;         /* extract reg exponent */
    reg &= DMMASK;                      /* extract reg mantissa */

    /* extract mem exponent and mantissa */
    if (mem & DMSIGN) {                 /* mem negative */
        sign ^= 1;                      /* set neg flag */
        mem = NEGATE32(mem);            /* make negative positive */
        sign2 |= 1;                     /* set neg flag */
    }
    if (mem & 0x1ll)                    /* test lsb */
        lsb = 1;                        /* reg is odd */
    em = (mem & DEXMASK) >> 56;         /* extract mem exponent */
    mem &= DMMASK;                      /* extract mem mantissa */

    er = er - em + 0x40;                /* get the exp value */

    /* move left 1 nibble for divide */
    /* Shift numbers up 4 bits so as not to lose precision below */
    reg <<= 4;
    mem <<= 4;

    /* see if we need to adjust divisor if larger that dividend */
    if (reg > mem) {
        reg >>= 4;
        er++;
    }

    mem ^= DIBMASK;                     /* change sign of mem val to do add */
    mem++;                              /* comp & incr */

    res = 0;                            /* zero result for multiply */
    /* do divide by using shift & add (subt) */
    for (temp = 56; temp > 0; temp--) {
        t_uint64   tmp;

        /* Add if we need too */
        /* Shift left by one */
        reg <<= 1;
        /* Subtract remainder to dividend */
        tmp = reg + mem;

        /* Shift quotent left one bit */
        res <<= 1;

        /* If remainder larger then divisor replace */
        if ((tmp & DCMASK) != 0) {
            reg = tmp;
            res |= 1;
        }
    }

    /* Compute one final set to see if rounding needed */
    /* Shift left by one */
    reg <<= 1;
    /* Subtract remainder to dividend */
    reg += mem;

    /* If .5 off, round, but do not cause carry overflow */
    if (((reg & DMSIGN) != 0) && (res != 0x00FFFFFFFFFFFFFFll)){
        res++;
    }

    /* fix up some boundry condtions to make diags happy */
    if (res == 0x00FFFFFFFFFFFFF1ll) {
        res += 0x0fll;                  /* round up by nibble */
    }
    else
    if (res == 0x00FFFFFFFFFFFFF8ll) {
        res &= 0x0FFFFFFFFFFFFFC0ll;    /* remove some extra bits */
    }
    else
    if (res == 0x00FFFFFFFFFFFFFFll) {
        if (lsb == 0) {
            res += 0x1ll;               /* round up by bit to force carry */
        } else {
            if (sign) {
                if (sign2 == 1) {
                    res &= 0x00FFFFFFFFFFFFF0ll;    /* clear last nibble */
                } else {
                    res += 0x1ll;       /* round up by bit to for carry */
                }
            } else {
                if (sign2 == 3) {
                    res += 0x1ll;       /* round up by bit to for carry */
                } else {
                    res &= 0x00FFFFFFFFFFFFF0ll;    /* clear last nibble */
                }
            }
        }
    }
    /* diags should be happy now */

    /* If overflow, shift right 4 bits */
    if (res & DEXMASK) {                /* see if overflow carry */
        res >>= 4;                      /* move mantissa down 4 bits */
        er++;                           /* and adjust exponent */
        if (er >= 128) {                /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT|CC4BIT;         /* set arithmetic exception */
            CC |= (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            if (CC & CC3BIT)            /* NEG overflow? */
                res = 0x8000000000000001ll;   /* double yes */
            else
                res = 0x7FFFFFFFFFFFFFFFll;   /* no, pos */
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
    }

    /* Align the results */
    if ((res) != 0) {
        while ((res != 0) && (res & DZMASK) == 0) {
            res <<= 4;
            er--;
        }
        /* Check if overflow */
        if (er >= 128) {                /* if exponent is too large, overflow */
            /* OVERFLOW */
            CC = CC1BIT|CC4BIT;         /* set arithmetic exception */
            if (sign & 1) {
                CC |= CC3BIT;
                res = 0x8000000000000001ll;   /* neg overflow 1011 */
            } else {
                CC |= CC2BIT;
                res = 0x7FFFFFFFFFFFFFFFll;   /* pos overflow 1101 */
            }
            /* store results */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
        /* Check if underflow */
        if (er < 0) {
            /* UNDERFLOW */
            res = 0;                    /* make return value zero */
            CC = (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
            CC |= CC1BIT;               /* set arithmetic exception */
            *cc = CC;                   /* save CC's */
            return res;                 /* return results */
        }
    } else {
        er = sign = 0;
    }

    res &= DMMASK;                      /* clear exponent space */

    res |= ((((t_uint64)er) << 56) & DEXMASK); /* merge exp and mantissa */
    if (sign & 1)                       /* is result to be negative */
        res = NEGATE32(res);            /* make value negative */

    /* determine CC's for result */
    CC = 0;
    if (res != 0)                       /* see if non zero */
        CC = (sign & 1)?CC3BIT:CC2BIT;  /* neg is CC3, pos is CC2 */
    else
        CC = CC4BIT;                    /* set zero cc */

    /* return results */
    *cc = CC;                           /* save CC's */
    return res;                         /* return results */
}

