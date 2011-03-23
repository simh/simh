/* i7094_cpu1.c: IBM 7094 CPU complex instructions

   Copyright (c) 2003-2008, Robert M. Supnik

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

#include "i7094_defs.h"

#define FP_HIFRAC(x)    ((uint32) ((x) >> FP_N_FR) & FP_FMASK)
#define FP_LOFRAC(x)    ((uint32) (x) & FP_FMASK)

#define FP_PACK38(s,e,f) (((s)? AC_S: 0) | ((t_uint64) (f)) | \
                        (((t_uint64) ((e) & FP_M_ACCH)) << FP_V_CH))
#define FP_PACK36(s,e,f) (((s)? SIGN: 0) | ((t_uint64) (f)) | \
                        (((t_uint64) ((e) & FP_M_CH)) << FP_V_CH))

extern t_uint64 AC, MQ, SI, KEYS;
extern uint32 PC;
extern uint32 SLT, SSW;
extern uint32 cpu_model, stop_illop;
extern uint32 ind_ovf, ind_dvc, ind_ioc, ind_mqo;
extern uint32 mode_ttrap, mode_strap, mode_ctrap, mode_ftrap;
extern uint32 mode_storn, mode_multi;
extern uint32 chtr_pend, chtr_inht, chtr_inhi;
extern uint32 ch_flags[NUM_CHAN];

typedef struct {                                        /* unpacked fp */
    uint32              s;                              /* sign: 0 +, 1 - */
    int32               ch;                             /* exponent */
    t_uint64            fr;                             /* fraction (54b) */
    } UFP;

uint32 op_frnd (void);
t_uint64 fp_fracdiv (t_uint64 dvd, t_uint64 dvr, t_uint64 *rem);
void fp_norm (UFP *op);
void fp_unpack (t_uint64 h, t_uint64 l, t_bool q_ac, UFP *op);
uint32 fp_pack (UFP *op, uint32 mqs, int32 mqch);

extern t_bool fp_trap (uint32 spill);
extern t_bool sel_trap (uint32 va);
extern t_stat ch_op_reset (uint32 ch, t_bool ch7909);

/* Integer add

   Sherman: "As the result of an addition or subtraction, if the C(AC) is
   zero, the sign of AC is unchanged." */

void op_add (t_uint64 op)
{
t_uint64 mac = AC & AC_MMASK;                           /* get magnitudes */
t_uint64 mop = op & MMASK;

AC = AC & AC_S;                                         /* isolate AC sign */
if ((AC? 1: 0) ^ ((op & SIGN)? 1: 0)) {                 /* signs diff? sub */
    if (mac >= mop)                                     /* AC >= MQ */
        AC = AC | (mac - mop);
    else AC = (AC ^ AC_S) | (mop - mac);                /* <, sign change */
    }
else {
    AC = AC | ((mac + mop) & AC_MMASK);                 /* signs same, add */
    if ((AC ^ mac) & AC_P)                              /* P change? overflow */
        ind_ovf = 1;
    }
return;
}

/* Multiply */

void op_mpy (t_uint64 ac, t_uint64 sr, uint32 sc)
{
uint32 sign;

if (sc == 0)                                            /* sc = 0? nop */
    return;
sign = ((MQ & SIGN)? 1: 0) ^ ((sr & SIGN)? 1: 0);       /* result sign */
ac = ac & AC_MMASK;                                     /* clear AC sign */
sr = sr & MMASK;                                        /* mpy magnitude */
MQ = MQ & MMASK;                                        /* MQ magnitude */
if (sr && MQ) {                                         /* mpy != 0? */
    while (sc--) {                                      /* for sc */
        if (MQ & 1)                                     /* MQ35? AC += mpy */
            ac = (ac + sr) & AC_MMASK;
        MQ = (MQ >> 1) | ((ac & 1) << 34);              /* AC'MQ >> 1 */
        ac = ac >> 1;
        }
    }
else ac = MQ = 0;                                       /* result = 0 */
if (sign) {                                             /* negative? */
    ac = ac | AC_S;                                     /* insert signs */
    MQ = MQ | SIGN;
    }
AC = ac;                                                /* update AC */
return;
}

/* Divide */

t_bool op_div (t_uint64 sr, uint32 sc)
{
uint32 signa, signm;

if (sc == 0)                                            /* sc = 0? nop */
    return FALSE;
signa = (AC & AC_S)? 1: 0;                              /* get signs */
signm = (sr & SIGN)? 1: 0;
sr = sr & MMASK;                                        /* get dvr magn */
if ((AC & AC_MMASK) >= sr)                              /* |AC| >= |sr|? */
    return TRUE;
AC = AC & AC_MMASK;                                     /* AC, MQ magn */
MQ = MQ & MMASK;
while (sc--) {                                          /* for sc */
    AC = ((AC << 1) & AC_MMASK) | (MQ >> 34);           /* AC'MQ << 1 */
    MQ = (MQ << 1) & MMASK;
    if (AC >= sr) {                                     /* AC >= dvr? */
        AC = AC - sr;                                   /* AC -= dvr */
        MQ = MQ | 1;                                    /* set quo bit */
        }
    }
if (signa ^ signm)                                      /* quo neg? */
    MQ = MQ | SIGN;
if (signa)                                              /* rem neg? */
    AC = AC | AC_S;
return FALSE;                                           /* div ok */
}

/* Shifts */

void op_als (uint32 addr)
{
uint32 sc = addr & SCMASK;

if ((sc >= 35)?                                         /* shift >= 35? */
    ((AC & MMASK) != 0):                                /* test all bits for ovf */
    (((AC & MMASK) >> (35 - sc)) != 0))                 /* test only 35-sc bits */
    ind_ovf = 1;
if (sc >= 37)                                           /* sc >= 37? result 0 */
    AC = AC & AC_S;
else AC = (AC & AC_S) | ((AC << sc) & AC_MMASK);        /* shift, save sign */
return;
}

void op_ars (uint32 addr)
{
uint32 sc = addr & SCMASK;

if (sc >= 37)                                           /* sc >= 37? result 0 */
    AC = AC & AC_S;
else AC = (AC & AC_S) | ((AC & AC_MMASK) >> sc);        /* shift, save sign */
return;
}

void op_lls (uint32 addr)
{
uint32 sc;                                              /* get sc */

AC = AC & AC_MMASK;                                     /* clear AC sign */
for (sc = addr & SCMASK; sc != 0; sc--) {               /* for SC */
    AC = ((AC << 1) & AC_MMASK) | ((MQ >> 34) & 1);     /* AC'MQ << 1 */
    MQ = (MQ & SIGN) | ((MQ << 1) & MMASK);             /* preserve MQ sign */
    if (AC & AC_P)                                      /* if P, overflow */
        ind_ovf = 1;
    }
if (MQ & SIGN)                                          /* set ACS from MQS */
    AC = AC | AC_S;
return;
}

void op_lrs (uint32 addr)
{
uint32 sc = addr & SCMASK;
t_uint64 mac;

MQ = MQ & MMASK;                                        /* get MQ magnitude */
if (sc != 0) {
    mac = AC & AC_MMASK;                                /* get AC magnitude, */
    AC = AC & AC_S;                                     /* sign */
    if (sc < 35) {                                      /* sc [1,34]? */
        MQ = ((MQ >> sc) | (mac << (35 - sc))) & MMASK; /* MQ has AC'MQ */
        AC = AC | (mac >> sc);                          /* AC has AC only */
        }
    else if (sc < 37) {                                 /* sc [35:36]? */
        MQ = (mac >> (sc - 35)) & MMASK;                /* MQ has AC only */
        AC = AC | (mac >> sc);                          /* AC has <QP> */
        }
    else if (sc < 72)                                   /* sc [37:71]? */
        MQ = (mac >> (sc - 35)) & MMASK;                /* MQ has AC only */
    else MQ = 0;                                        /* >72? MQ = 0 */
    }
if (AC & AC_S)                                          /* set MQS from ACS */
    MQ = MQ | SIGN;
return;
}

void op_lgl (uint32 addr)
{
uint32 sc;                                              /* get sc */

for (sc = addr & SCMASK; sc != 0; sc--) {               /* for SC */
    AC = (AC & AC_S) | ((AC << 1) & AC_MMASK) |         /* AC'MQ << 1 */
        ((MQ >> 35) & 1);                               /* preserve AC sign */
    MQ = (MQ << 1) & DMASK;
    if (AC & AC_P)                                      /* if P, overflow */
        ind_ovf = 1;
    }
return;
}

void op_lgr (uint32 addr)
{
uint32 sc = addr & SCMASK;
t_uint64 mac;

if (sc != 0) {
    mac = AC & AC_MMASK;                                /* get AC magnitude, */
    AC = AC & AC_S;                                     /* sign */
    if (sc < 36) {                                      /* sc [1,35]? */
        MQ = ((MQ >> sc) | (mac << (36 - sc))) & DMASK; /* MQ has AC'MQ */
        AC = AC | (mac >> sc);                          /* AC has AC only */
        }
    else if (sc == 36) {                                /* sc [36]? */
        MQ = mac & DMASK;                               /* MQ = AC<P,1:35> */
        AC = AC | (mac >> 36);                          /* AC = AC<Q> */
        }
    else if (sc < 73)                                   /* sc [37, 72]? */
        MQ = (mac >> (sc - 36)) & DMASK;                /* MQ has AC only */
    else MQ = 0;                                        /* >72, AC,MQ = 0 */
    }
return;
}

/* Plus sense - undefined operations are NOPs */

t_stat op_pse (uint32 addr)
{
uint32 ch, spill;

switch (addr) {

    case 00000:                                         /* CLM */
        if (cpu_model & I_9X)                           /* 709X only */
            AC = AC & AC_S;
        break;

    case 00001:                                         /* LBT */
        if ((AC & 1) != 0)
            PC = (PC + 1) & AMASK;
        break;

    case 00002:                                         /* CHS */
        AC = AC ^ AC_S;
        break;

    case 00003:                                         /* SSP */
        AC = AC & ~AC_S;
        break;

    case 00004:                                         /* ENK */
        MQ = KEYS;
        break;

    case 00005:                                         /* IOT */
        if (ind_ioc)
            ind_ioc = 0;
        else PC = (PC + 1) & AMASK;
        break;

    case 00006:                                         /* COM */
        AC = AC ^ AC_MMASK;
        break;

    case 00007:                                         /* ETM */
        if (cpu_model & I_9X)                           /* 709X only */
            mode_ttrap = 1;
        break;

    case 00010:                                         /* RND */
        if ((cpu_model & I_9X) && (MQ & B1))            /* 709X only, MQ1 set? */
            op_add ((t_uint64) 1);                      /* incr AC */
        break;

    case 00011:                                         /* FRN */
        if (cpu_model & I_9X) {                         /* 709X only */
            spill = op_frnd ();
            if (spill)
                fp_trap (spill);
            }
        break;

    case 00012:                                         /* DCT */
        if (ind_dvc)
            ind_dvc = 0;
        else PC = (PC + 1) & AMASK;
        break;

    case 00014:                                         /* RCT */
        chtr_inhi = 1;                                  /* 1 cycle delay */
        chtr_inht = 0;                                  /* clr inhibit trap */
        chtr_pend = 0;                                  /* no trap now */
        break;

    case 00016:                                         /* LMTM */
        if (cpu_model & I_94)                           /* 709X only */
            mode_multi = 0;
        break;

    case 00140:                                         /* SLF */
        if (cpu_model & I_9X)                       /* 709X only */
            SLT = 0;
        break;

    case 00141: case 00142: case 00143: case 00144:     /* SLN */
        if (cpu_model & I_9X)                           /* 709X only */
            SLT = SLT | (1u << (00144 - addr));
        break;

    case 00161: case 00162: case 00163:                 /* SWT */
    case 00164: case 00165: case 00166:
        if ((SSW & (1u << (00166 - addr))) != 0)
            PC = (PC + 1) & AMASK;
        break;

    case 01000: case 02000: case 03000: case 04000:     /* BTT */
    case 05000: case 06000: case 07000: case 10000:
        if (cpu_model & I_9X) {                         /* 709X only */
            if (sel_trap (PC))                          /* sel trap? */
                break;
            ch = GET_U_CH (addr);                       /* get channel */
            if (ch_flags[ch] & CHF_BOT)                 /* BOT? */
                ch_flags[ch] &= ~CHF_BOT;               /* clear */
            else PC = (PC + 1) & AMASK;                 /* else skip */
            }
        break;

    case 001350: case 002350: case 003350: case 004350: /* RICx */
    case 005350: case 006350: case 007350: case 010350:
        ch = GET_U_CH (addr);                           /* get channel */
        return ch_op_reset (ch, 1);

    case 001352: case 002352: case 003352: case 004352: /* RDCx */
    case 005352: case 006352: case 007352: case 010352:
        ch = GET_U_CH (addr);                           /* get channel */
        return ch_op_reset (ch, 0);
        }                                               /* end case */

return SCPE_OK;
}

/* Minus sense */

t_stat op_mse (uint32 addr)
{
uint32 t, ch;

switch (addr) {

    case 00000:                                         /* CLM */
        if (cpu_model & I_9X)                           /* 709X only */
            AC = AC & AC_S;
        break;

    case 00001:                                         /* PBT */
        if ((AC & AC_P) != 0)
            PC = (PC + 1) & AMASK;
        break;

    case 00002:                                         /* EFTM */
        if (cpu_model & I_9X) {                         /* 709X only */
            mode_ftrap = 1;
            ind_mqo = 0;                                /* clears MQ ovf */
            }
        break;

    case 00003:                                         /* SSM */
        if (cpu_model & I_9X)                           /* 709X only */
            AC = AC | AC_S;
        break;

    case 00004:                                         /* LFTM */
        if (cpu_model & I_9X)                           /* 709X only */
            mode_ftrap = 0;
        break;

    case 00005:                                         /* ESTM */
        if (cpu_model & I_9X)                           /* 709X only */
            mode_strap = 1;
        break;

    case 00006:                                         /* ECTM */
        if (cpu_model & I_9X)                           /* 709X only */
            mode_ctrap = 1;
        break;

    case 00007:                                         /* LTM */
        if (cpu_model & I_9X)                           /* 709X only */
            mode_ttrap = 0;
        break;

    case 00010:                                         /* LSNM */
        if (cpu_model & I_9X)                           /* 709X only */
            mode_storn = 0;
        break;

    case 00012:                                         /* RTT (704) */
        if (cpu_model & I_9X)                           /* 709X only */
            sel_trap (PC);
        break;

    case 00016:                                         /* EMTM */
        mode_multi = 1;
        break;

    case 00140:                                         /* SLF */
        if (cpu_model & I_9X)                           /* 709X only */
            SLT = 0;
        break;

    case 00141: case 00142: case 00143: case 00144:     /* SLT */
        if (cpu_model & I_9X) {                         /* 709X only */
            t = SLT & (1u << (00144 - addr));
            SLT = SLT & ~t;
            if (t != 0)
                PC = (PC + 1) & AMASK;
            }
        break;

    case 00161: case 00162: case 00163:                 /* SWT */
    case 00164: case 00165: case 00166:
        if ((cpu_model & I_9X) &&                       /* 709X only */
            ((SSW & (1u << (00166 - addr))) != 0))
            PC = (PC + 1) & AMASK;
        break;

    case 001000: case 002000: case 003000: case 004000: /* ETT */
    case 005000: case 006000: case 007000: case 010000:
        if (sel_trap (PC))                              /* sel trap? */
            break;
        ch = GET_U_CH (addr);                           /* get channel */
        if (ch_flags[ch] & CHF_EOT)                     /* EOT? */
            ch_flags[ch] = ch_flags[ch] & ~CHF_EOT;     /* clear */
        else PC = (PC + 1) & AMASK;                     /* else skip */
        break;
        }

return SCPE_OK;
}

/* Floating add 

   Notes:
   - AC<Q,P> enter into the initial exponent comparison.  If either is set,
     the numbers are always swapped.  AC<P> gets OR'd into AC<S> during the
     swap, and AC<Q,P> are cleared afterwards
   - The early end test is actually > 077 if AC <= SR and > 100 if
     AC > SR.  However, any shift >= 54 will produce a zero fraction,
     so the difference can be ignored */

uint32 op_fad (t_uint64 sr, t_bool norm)
{
UFP op1, op2, t;
int32 mqch, diff;

MQ = 0;                                                 /* clear MQ */
fp_unpack (AC, 0, 1, &op1);                             /* unpack AC */
fp_unpack (sr, 0, 0, &op2);                             /* unpack sr */
if (op1.ch > op2.ch) {                                  /* AC exp > SR exp? */
    if (AC & AC_P)                                      /* AC P or's with S */
        op1.s = 1;
    t = op1;                                            /* swap operands */
    op1 = op2;
    op2 = t;
    op2.ch = op2.ch & FP_M_CH;                          /* clear P,Q */
    }
diff = op2.ch - op1.ch;                                 /* exp diff */
if (diff) {                                             /* any shift? */
    if ((diff < 0) || (diff > 077))                     /* diff > 63? */
        op1.fr = 0;
    else op1.fr = op1.fr >> diff;                       /* no, denormalize */
    }
if (op1.s ^ op2.s) {                                    /* subtract? */
    if (op1.fr >= op2.fr) {                             /* op1 > op2? */
        op2.fr = op1.fr - op2.fr;                       /* op1 - op2 */
        op2.s = op1.s;                                  /* op2 sign is result */
        }
    else op2.fr = op2.fr - op1.fr;                      /* else op2 - op1 */
    }
else {
    op2.fr = op2.fr + op1.fr;                           /* op2 + op1 */
    if (op2.fr & FP_FCRY) {                             /* carry? */
        op2.fr = op2.fr >> 1;                           /* renormalize */
        op2.ch++;                                       /* incr exp */
        }
    }
if (norm) {                                             /* normalize? */
    if (op2.fr) {                                       /* non-zero frac? */
        fp_norm (&op2);
        mqch = op2.ch - FP_N_FR;
        }
    else op2.ch = mqch = 0;                             /* else true zero */
    }
else mqch = op2.ch - FP_N_FR;
return fp_pack (&op2, op2.s, mqch);                     /* pack AC, MQ */
}

/* Floating multiply */

uint32 op_fmp (t_uint64 sr, t_bool norm)
{
UFP op1, op2;
int32 mqch;
uint32 f1h, f2h;

fp_unpack (MQ, 0, 0, &op1);                             /* unpack MQ */
fp_unpack (sr, 0, 0, &op2);                             /* unpack sr */
op1.s = op1.s ^ op2.s;                                  /* result sign */
if ((op2.ch == 0) && (op2.fr == 0)) {                   /* sr a normal 0? */
    AC = op1.s? AC_S: 0;                                /* result is 0 */
    MQ = op1.s? SIGN: 0;
    return 0;
    }
f1h = FP_HIFRAC (op1.fr);                               /* get hi fracs */
f2h = FP_HIFRAC (op2.fr);
op1.fr = ((t_uint64) f1h) * ((t_uint64) f2h);           /* f1h * f2h */
op1.ch = (op1.ch & FP_M_CH) + op2.ch - FP_BIAS;         /* result exponent */
if (norm) {                                             /* normalize? */
    if (!(op1.fr & FP_FNORM)) {                         /* not normalized? */
        op1.fr = op1.fr << 1;                           /* shift frac left 1 */
        op1.ch--;                                       /* decr exp */
        }
    if (FP_HIFRAC (op1.fr))                             /* hi result non-zero? */
        mqch = op1.ch - FP_N_FR;                        /* set MQ exp */
    else op1.ch = mqch = 0;                             /* clear AC, MQ exp */
    }
else mqch = op1.ch - FP_N_FR;                           /* set MQ exp */
return fp_pack (&op1, op1.s, mqch);                     /* pack AC, MQ */
}

/* Floating divide */

uint32 op_fdv (t_uint64 sr)
{
UFP op1, op2;
int32 mqch;
uint32 spill, quos;
t_uint64 rem;

fp_unpack (AC, 0, 1, &op1);                             /* unpack AC */
fp_unpack (sr, 0, 0, &op2);                             /* unpack sr */
quos = op1.s ^ op2.s;                                   /* quotient sign */
if (op1.fr >= (2 * op2.fr)) {                           /* |AC| >= 2*|sr|? */
    MQ = quos? SIGN: 0;                                 /* MQ = sign only */
    return TRAP_F_DVC;                                  /* divide check */
    }
if (op1.fr == 0) {                                      /* |AC| == 0? */
    MQ = quos? SIGN: 0;                                 /* MQ = sign only */
    AC = 0;                                             /* AC = +0 */
    return 0;                                           /* done */
    }
op1.ch = op1.ch & FP_M_CH;                              /* remove AC<Q,P> */
if (op1.fr >= op2.fr) {                                 /* |AC| >= |sr|? */
    op1.fr = op1.fr >> 1;                               /* denorm AC */
    op1.ch++;
    }
op1.fr = fp_fracdiv (op1.fr, op2.fr, &rem);             /* fraction divide */
op1.fr = op1.fr | (rem << FP_N_FR);                     /* rem'quo */
mqch = op1.ch - op2.ch + FP_BIAS;                       /* quotient exp */
op1.ch = op1.ch - FP_N_FR;                              /* remainder exp */
spill = fp_pack (&op1, quos, mqch);                     /* pack up */
return (spill? (spill | TRAP_F_SGL): 0);                /* if spill, set SGL */
}

/* Double floating add 

   Notes:
   - AC<Q,P> enter into the initial exponent comparison.  If either is set,
     the numbers are always swapped.  AC<P> gets OR'd into AC<S> during the
     swap, and AC<Q,P> are cleared afterwards
   - For most cases, SI ends up with the high order part of the larger number
   - The 'early end' cases (smaller number is shifted away) must be tracked
     exactly for SI impacts.  The early end cases are:

        (a) AC > SR, diff > 0100, and AC normalized
        (b) AC <= SR, diff > 077, and SR normalized

     In case (a), SI is unchanged.  In case (b), SI ends up with the SR sign
     and characteristic but the MQ (!) fraction */

uint32 op_dfad (t_uint64 sr, t_uint64 sr1, t_bool norm)
{
UFP op1, op2, t;
int32 mqch, diff;

fp_unpack (AC, MQ, 1, &op1);                            /* unpack AC'MQ */
fp_unpack (sr, sr1, 0, &op2);                           /* unpack sr'sr1 */
if (op1.ch > op2.ch) {                                  /* AC exp > SR exp? */
    if (((op1.ch - op2.ch) > 0100) && (AC & B9)) ;      /* early out */
    else SI = FP_PACK36 (op1.s, op1.ch, FP_HIFRAC (op1.fr));
    if (AC & AC_P)                                      /* AC P or's with S */
        op1.s = 1;
    t = op1;                                            /* swap operands */
    op1 = op2;
    op2 = t;
    op2.ch = op2.ch & FP_M_CH;                          /* clear P,Q */
    }
else {                                                  /* AC <= SR */
    if (((op2.ch - op1.ch) > 077) && (sr & B9))         /* early out */
        SI = FP_PACK36 (op2.s, op2.ch, FP_LOFRAC (MQ));
    else SI = FP_PACK36 (op2.s, op2.ch, FP_HIFRAC (op2.fr));
    } 
diff = op2.ch - op1.ch;                                 /* exp diff */
if (diff) {                                             /* any shift? */
    if ((diff < 0) || (diff > 077))                     /* diff > 63? */
        op1.fr = 0;
    else op1.fr = op1.fr >> diff;                       /* no, denormalize */
    }
if (op1.s ^ op2.s) {                                    /* subtract? */
    if (op1.fr >= op2.fr) {                             /* op1 > op2? */
        op2.fr = op1.fr - op2.fr;                       /* op1 - op2 */
        op2.s = op1.s;                                  /* op2 sign is result */
        }
    else  op2.fr = op2.fr - op1.fr;                     /* op2 - op1 */
    }
else {
    op2.fr = op2.fr + op1.fr;                           /* op2 + op1 */
    if (op2.fr & FP_FCRY) {                             /* carry? */
        op2.fr = op2.fr >> 1;                           /* renormalize */
        op2.ch++;                                       /* incr exp */
        }
    }
if (norm) {                                             /* normalize? */
    if (op2.fr) {                                       /* non-zero frac? */
        fp_norm (&op2);
        mqch = op2.ch - FP_N_FR;
        }
    else op2.ch = mqch = 0;                             /* else true zero */
    }
else mqch = op2.ch - FP_N_FR;
return fp_pack (&op2, op2.s, mqch);                     /* pack AC, MQ */
}

/* Double floating multiply

   Notes (notation is A+B' * C+D', where ' denotes 2^-27):
   - The instruction returns 0 if A and C are both zero, because B*D is never
     done as part of the algorithm
   - For most cases, SI ends up with B*C, with a zero sign and exponent
   - For the A+B' both zero 'early end' case SI ends up with A or C,
     depending on whether the operation is normalized or not */

uint32 op_dfmp (t_uint64 sr, t_uint64 sr1, t_bool norm)
{
UFP op1, op2;
int32 mqch;
uint32 f1h, f2h, f1l, f2l;
t_uint64 tx;

fp_unpack (AC, MQ, 1, &op1);                            /* unpack AC'MQ */
fp_unpack (sr, sr1, 0, &op2);                           /* unpack sr'sr1 */
op1.s = op1.s ^ op2.s;                                  /* result sign */
f1h = FP_HIFRAC (op1.fr);                               /* A */
f1l = FP_LOFRAC (op1.fr);                               /* B */
f2h = FP_HIFRAC (op2.fr);                               /* C */
f2l = FP_LOFRAC (op2.fr);                               /* D */
if (((op1.ch == 0) && (op1.fr == 0)) ||                 /* AC'MQ normal 0? */
    ((op2.ch == 0) && (op2.fr == 0)) ||                 /* sr'sr1 normal 0? */
    ((f1h == 0) && (f2h == 0))) {                       /* both hi frac zero? */
    AC = op1.s? AC_S: 0;                                /* result is 0 */
    MQ = op1.s? SIGN: 0;
    SI = sr;                                            /* SI has C */
    return 0;
    }
op1.ch = (op1.ch & FP_M_CH) + op2.ch - FP_BIAS;         /* result exponent */
if (op1.fr) {                                           /* A'B != 0? */
    op1.fr = ((t_uint64) f1h) * ((t_uint64) f2h);       /* A * C */
    tx = ((t_uint64) f1h) * ((t_uint64) f2l);           /* A * D */
    op1.fr = op1.fr + (tx >> FP_N_FR);                  /* add in hi 27b */
    tx = ((t_uint64) f1l) * ((t_uint64) f2h);           /* B * C */
    op1.fr = op1.fr + (tx >> FP_N_FR);                  /* add in hi 27b */
    SI = tx >> FP_N_FR;                                 /* SI keeps B * C */
    }
else {
    if (norm)                                           /* early out */
        SI = sr;
    else SI = FP_PACK36 (op2.s, op2.ch, 0);
    }
if (norm) {                                             /* normalize? */
    if (!(op1.fr & FP_FNORM)) {                         /* not normalized? */
        op1.fr = op1.fr << 1;                           /* shift frac left 1 */
        op1.ch--;                                       /* decr exp */
        }
    if (FP_HIFRAC (op1.fr)) {                           /* non-zero? */
        mqch = op1.ch - FP_N_FR;                        /* set MQ exp */
        }
    else op1.ch = mqch = 0;                             /* clear AC, MQ exp */
    }
else mqch = op1.ch - FP_N_FR;                           /* set MQ exp */
return fp_pack (&op1, op1.s, mqch);                     /* pack AC, MQ */
}

/* Double floating divide


   Notes:
   - This is a Taylor series expansion (where ' denotes >> 27):

        (A+B') * (C+D')^-1 = (A+B') * C^-1 - (A+B') * D'* C^-2 +...

     to two terms, which can be rewritten as terms Q1, Q2:

        Q1 = (A+B')/C
        Q2' = (R - Q1*D)'/C

   - Tracking the sign of Q2' is complicated:

        Q1 has the sign of the quotient, s_AC ^ s_SR
        D has the sign of the divisor, s_SR
        R has the sign of the dividend, s_AC
        Q1*D sign is s_AC ^ s_SR ^ s^SR = s^AC
        Therefore, R and Q1*D have the same sign, s_AC
        Q2' sign is s^AC ^ s_SR, which is the sign of the quotient

   - For first divide check, SI is 0
   - For other cases, including second divide check, SI ends up with Q1
   - R-Q1*D is only calculated to the high 27b; using the full 54b
     throws off the result
   - The second divide must check for divd >= divr, otherwise an extra
     bit of quotient would be devloped, throwing off the result
   - A late ECO added full post-normalization; single precision divide
     does no normalization */

uint32 op_dfdv (t_uint64 sr, t_uint64 sr1)
{
UFP op1, op2;
int32 mqch;
uint32 csign, ac_s;
t_uint64 f1h, f2h, tr, tq1, tq1d, trmq1d, tq2;

fp_unpack (AC, MQ, 1, &op1);                            /* unpack AC'MQ */
fp_unpack (sr, 0, 0, &op2);                             /* unpack sr only */
ac_s = op1.s;                                           /* save AC sign */
op1.s = op1.s ^ op2.s;                                  /* sign of result */
f1h = FP_HIFRAC (op1.fr);
f2h = FP_HIFRAC (op2.fr);
if (f1h >= (2 * f2h)) {                                 /* |A| >= 2*|C|? */
    SI = 0;                                             /* clear SI */
    return TRAP_F_DVC;                                  /* divide check */
    }
if (f1h == 0) {                                         /* |AC| == 0? */
    SI = MQ = op1.s? SIGN: 0;                           /* MQ, SI = sign only */
    AC = op1.s? AC_S: 0;                                /* AC = sign only */
    return 0;                                           /* done */
    }
op1.ch = op1.ch & FP_M_CH;                              /* remove AC<Q,P> */
if (f1h >= f2h) {                                       /* |A| >= |C|? */
    op1.fr = op1.fr >> 1;                               /* denorm AC */
    op1.ch++;
    }
op1.ch = op1.ch - op2.ch + FP_BIAS;                     /* exp of quotient */
tq1 = fp_fracdiv (op1.fr, op2.fr, &tr);                 /* |A+B| / |C| */
tr = tr << FP_N_FR;                                     /* R << 27 */
tq1d = (tq1 * ((t_uint64) FP_LOFRAC (sr1))) &           /* Q1 * D */
    ~((t_uint64) FP_FMASK);                             /* top 27 bits */
csign = (tr < tq1d);                                    /* correction sign */
if (csign)                                              /* |R|<|Q1*D|? compl */
    trmq1d = tq1d - tr;
else trmq1d = tr - tq1d;                                /* no, subtr ok */
SI = FP_PACK36 (op1.s, op1.ch, tq1);                    /* SI has Q1 */
if (trmq1d >= (2 * op2.fr)) {                           /* |R-Q1*D| >= 2*|C|? */
    AC = FP_PACK38 (csign ^ ac_s, 0, FP_HIFRAC (trmq1d)); /* AC has R-Q1*D */
    MQ = (csign ^ ac_s)? SIGN: 0;                       /* MQ = sign only */
    return TRAP_F_DVC;                                  /* divide check */
    }
tq2 = fp_fracdiv (trmq1d, op2.fr, NULL);                /* |R-Q1*D| / |C| */
if (trmq1d >= op2.fr)                                   /* can only gen 27b quo */
    tq2 &= ~((t_uint64) 1);
op1.fr = tq1 << FP_N_FR;                                /* shift Q1 into place */
if (csign)                                              /* sub or add Q2 */
    op1.fr = op1.fr - tq2;
else op1.fr = op1.fr + tq2;
fp_norm (&op1);                                         /* normalize */
if (op1.fr)                                             /* non-zero? */
    mqch = op1.ch - FP_N_FR;
else op1.ch = mqch = 0;                                 /* clear AC, MQ exp */
return fp_pack (&op1, op1.s, mqch);                     /* pack AC, MQ */
}

/* Floating round */

uint32 op_frnd (void)
{
UFP op;
uint32 spill;

spill = 0;                                              /* no error */
if (MQ & B9) {                                          /* MQ9 set? */
    fp_unpack (AC, 0, 1, &op);                          /* unpack AC */
    op.fr = op.fr + ((t_uint64) (1 << FP_N_FR));        /* round up */
    if (op.fr & FP_FCRY) {                              /* carry out? */
        op.fr = op.fr >> 1;                             /* renormalize */
        op.ch++;                                        /* incr exp */
        if (op.ch == (FP_M_CH + 1))                     /* ovf with QP = 0? */
            spill = TRAP_F_OVF | TRAP_F_AC;
        }
    AC = FP_PACK38 (op.s, op.ch, FP_HIFRAC (op.fr));    /* pack AC */
    }
return spill;
}

/* Fraction divide - 54/27'0 yielding quotient and remainder */

t_uint64 fp_fracdiv (t_uint64 dvd, t_uint64 dvr, t_uint64 *rem)
{
dvr = dvr >> FP_N_FR;
if (rem)
    *rem = dvd % dvr;
return (dvd / dvr);
}
        
/* Floating point normalize */

void fp_norm (UFP *op)
{
op->fr = op->fr & FP_DFMASK;                            /* mask fraction */
if (op->fr == 0)                                        /* zero? */
    return;
while ((op->fr & FP_FNORM) == 0) {                      /* until norm */
    op->fr = op->fr << 1;                               /* lsh 1 */
    op->ch--;                                           /* decr exp */
    }
return;
}

/* Floating point unpack */

void fp_unpack (t_uint64 h, t_uint64 l, t_bool q_ac, UFP *op)
{
if (q_ac) {                                             /* AC? */
    op->s = (h & AC_S)? 1: 0;                           /* get sign */
    op->ch = (uint32) ((h >> FP_V_CH) & FP_M_ACCH);     /* get exp */
    }
else {
    op->s = (h & SIGN)? 1: 0;                           /* no, mem */
    op->ch = (uint32) ((h >> FP_V_CH) & FP_M_CH);
    }
op->fr = (((t_uint64) FP_LOFRAC (h)) << FP_N_FR) |      /* get frac hi */
    ((t_uint64) FP_LOFRAC (l));                         /* get frac lo */
return;
}

/* Floating point pack */

uint32 fp_pack (UFP *op, uint32 mqs, int32 mqch)
{
uint32 spill;

AC = FP_PACK38 (op->s, op->ch, FP_HIFRAC (op->fr));     /* pack AC */
MQ = FP_PACK36 (mqs, mqch, FP_LOFRAC (op->fr));         /* pack MQ */
if (op->ch > FP_M_CH)                                   /* check AC exp */
    spill = TRAP_F_OVF | TRAP_F_AC;
else if (op->ch < 0)
    spill = TRAP_F_AC;
else spill = 0;
if (mqch > FP_M_CH)                                     /* check MQ exp */
    spill |= (TRAP_F_OVF | TRAP_F_MQ);
else if (mqch < 0)
    spill |= TRAP_F_MQ;
return spill;
}
