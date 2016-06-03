/* pdp8_fpp.c: PDP-8 floating point processor (FPP8A)

   Copyright (c) 2007-2011, Robert M Supnik

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

   fpp          FPP8A floating point processor

   03-Jan-10    RMS     Initialized variables statically, for VMS compiler
   19-Apr-09    RHM     FPICL does not clear all command and status reg bits
                            modify fpp_reset to conform with FPP
   27-Mar-09    RHM     Fixed handling of Underflow fix (zero FAC on underflow)
                        Implemented FPP division and multiplication algorithms
                        FPP behavior on traps - FEXIT does not update APT
                        Follow FPP settings for OPADD 
                        Correct detection of DP add/sub overflow
                        Detect and handle add/sub overshift
                        Single-step mode made consistent with FPP
                        Write calculation results prior to traps
   24-Mar-09    RMS     Many fixes from Rick Murphy:
                        Fix calculation of ATX shift amount
                        Added missing () to read, write XR macros
                        Fixed indirect address calculation
                        Fixed == written as = in normalization
                        Fixed off-by-one count bug in multiplication
                        Removed extraneous ; in divide
                        Fixed direction of compare in divide
                        Fixed count direction bug in alignment

   Floating point formats:

    00 01 02 03 04 05 06 07 08 09 10 11
   +--+--+--+--+--+--+--+--+--+--+--+--+
   | S|          hi integer            | : double precision
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |             lo integer            |
   +--+--+--+--+--+--+--+--+--+--+--+--+

    00 01 02 03 04 05 06 07 08 09 10 11
   +--+--+--+--+--+--+--+--+--+--+--+--+
   | S|          exponent              | : floating point
   +--+--+--+--+--+--+--+--+--+--+--+--+
   | S|          hi fraction           |
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |             lo fraction           |
   +--+--+--+--+--+--+--+--+--+--+--+--+


    00 01 02 03 04 05 06 07 08 09 10 11
   +--+--+--+--+--+--+--+--+--+--+--+--+
   | S|          exponent              | : extended precision
   +--+--+--+--+--+--+--+--+--+--+--+--+
   | S|          hi fraction           |
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |            next fraction          |
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |            next fraction          |
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |            next fraction          |
   +--+--+--+--+--+--+--+--+--+--+--+--+
   |             lo fraction           |
   +--+--+--+--+--+--+--+--+--+--+--+--+

   Exponents are 2's complement, as are fractions.  Normalized numbers have
   the form:

   0.0...0
   0.<non-zero>
   1.<non-zero>
   1.1...0

   Note that 1.0...0 is normalized but considered illegal, since it cannot
   be represented as a positive number. When a result is normalized, 1.0...0
   is converted to 1.1...0 with exp+1.
*/

#include "pdp8_defs.h"

extern int32 int_req;
extern uint16 M[];
extern int32 stop_inst;
extern UNIT cpu_unit;

#define SEXT12(x)       (((x) & 04000)? (x) | ~07777: (x) & 03777)

/* Index registers are in memory */

#define fpp_read_xr(xr) fpp_read (fpp_xra + (xr))
#define fpp_write_xr(xr,d) fpp_write (fpp_xra + (xr), d)

/* Command register */

#define FPC_DP          04000                           /* integer double */
#define FPC_UNFX        02000                           /* exit on fl undf */
#define FPC_FIXF        01000                           /* lock mem field */
#define FPC_IE          00400                           /* int enable */
#define FPC_V_FAST      4                               /* startup bits */
#define FPC_M_FAST      017
#define FPC_LOCK        00010                           /* lockout */
#define FPC_V_APTF      0
#define FPC_M_APTF      07                              /* apta field */
#define FPC_STA         (FPC_DP|FPC_LOCK)
#define FPC_GETFAST(x)  (((x) >> FPC_V_FAST) & FPC_M_FAST)
#define FPC_GETAPTF(x)  (((x) >> FPC_V_APTF) & FPC_M_APTF)

/* Status register */

#define FPS_DP          (FPC_DP)                        /* integer double */
#define FPS_TRPX        02000                           /* trap exit */
#define FPS_HLTX        01000                           /* halt exit */
#define FPS_DVZX        00400                           /* div zero exit */
#define FPS_IOVX        00200                           /* int ovf exit */
#define FPS_FOVX        00100                           /* flt ovf exit */
#define FPS_UNF         00040                           /* underflow */
#define FPS_XXXM        00020                           /* FADDM/FMULM */
#define FPS_LOCK        (FPC_LOCK)                      /* lockout */
#define FPS_EP          00004                           /* ext prec */
#define FPS_PAUSE       00002                           /* paused */
#define FPS_RUN         00001                           /* running */

/* Floating point number: 3-6 words */

#define FPN_FRSIGN      04000
#define FPN_NFR_FP      2                               /* std precision */
#define FPN_NFR_EP      5                               /* ext precision */
#define FPN_NFR_MDS     6                               /* mul/div precision */
#define EXACT           (uint32)((fpp_sta & FPS_EP)? FPN_NFR_EP: FPN_NFR_FP)
#define EXTEND          ((uint32) FPN_NFR_EP)

typedef struct {
    int32       exp;
    uint32      fr[FPN_NFR_MDS+1];
    } FPN;

uint32 fpp_apta = 0;                                    /* APT pointer */
uint32 fpp_aptsvf = 0;                                  /* APT saved field */
uint32 fpp_opa = 0;                                     /* operand pointer */
uint32 fpp_fpc = 0;                                     /* FP PC */
uint32 fpp_bra = 0;                                     /* base reg pointer */
uint32 fpp_xra = 0;                                     /* indx reg pointer */
uint32 fpp_cmd = 0;                                     /* command */
uint32 fpp_sta = 0;                                     /* status */
uint32 fpp_flag = 0;                                    /* flag */
FPN fpp_ac;                                             /* FAC */
uint32 fpp_ssf = 0;                                     /* single-step flag */
uint32 fpp_last_lockbit = 0;                            /* last lockbit */

static FPN fpp_zero = { 0, { 0, 0, 0, 0, 0 } };
static FPN fpp_one = { 1, { 02000, 0, 0, 0, 0 } };

int32 fpp55 (int32 IR, int32 AC);
int32 fpp56 (int32 IR, int32 AC);
void fpp_load_apt (uint32 apta);
void fpp_dump_apt (uint32 apta, uint32 sta);
uint32 fpp_1wd_dir (uint32 ir);
uint32 fpp_2wd_dir (uint32 ir);
uint32 fpp_indir (uint32 ir);
uint32 fpp_ad15 (uint32 hi);
uint32 fpp_adxr (uint32 ir, uint32 base_ad);
void fpp_add (FPN *a, FPN *b, uint32 sub);
void fpp_mul (FPN *a, FPN *b);
void fpp_div (FPN *a, FPN *b);
t_bool fpp_imul (FPN *a, FPN *b);
uint32 fpp_fr_add (uint32 *c, uint32 *a, uint32 *b, uint32 cnt);
void fpp_fr_sub (uint32 *c, uint32 *a, uint32 *b, uint32 cnt);
void fpp_fr_mul (uint32 *c, uint32 *a, uint32 *b, t_bool fix);
t_bool fpp_fr_div (uint32 *c, uint32 *a, uint32 *b);
uint32 fpp_fr_neg (uint32 *a, uint32 cnt);
int32 fpp_fr_cmp (uint32 *a, uint32 *b, uint32 cnt);
int32 fpp_fr_test (uint32 *a, uint32 v0, uint32 cnt);
uint32 fpp_fr_abs (uint32 *a, uint32 *b, uint32 cnt);
void fpp_fr_fill (uint32 *a, uint32 v, uint32 cnt);
void fpp_fr_lshn (uint32 *a, uint32 sc, uint32 cnt);
void fpp_fr_lsh12 (uint32 *a, uint32 cnt);
void fpp_fr_lsh1 (uint32 *a, uint32 cnt);
void fpp_fr_rsh1 (uint32 *a, uint32 sign, uint32 cnt);
void fpp_fr_algn (uint32 *a, uint32 sc, uint32 cnt);
t_bool fpp_cond_met (uint32 cond);
t_bool fpp_norm (FPN *a, uint32 cnt);
void fpp_round (FPN *a);
t_bool fpp_test_xp (FPN *a);
void fpp_copy (FPN *a, FPN *b);
void fpp_zcopy (FPN *a, FPN *b);
void fpp_read_op (uint32 ea, FPN *a);
void fpp_write_op (uint32 ea, FPN *a);
uint32 fpp_read (uint32 ea);
void fpp_write (uint32 ea, uint32 val);
uint32 apt_read (uint32 ea);
void apt_write (uint32 ea, uint32 val);
t_stat fpp_svc (UNIT *uptr);
t_stat fpp_reset (DEVICE *dptr);

/* FPP data structures

   fpp_dev      FPP device descriptor
   fpp_unit     FPP unit descriptor
   fpp_reg      FPP register list
*/

DIB fpp_dib = { DEV_FPP, 2, { &fpp55, &fpp56 } };

UNIT fpp_unit = { UDATA (&fpp_svc, 0, 0) };

REG fpp_reg[] = {
    { ORDATAD (FPACE, fpp_ac.exp, 12, "floating accumulator") },
    { ORDATAD (FPAC0, fpp_ac.fr[0], 12, "first mantissa") },
    { ORDATAD (FPAC1, fpp_ac.fr[1], 12, "second mantissa") },
    { ORDATAD (FPAC2, fpp_ac.fr[2], 12, "third mantissa") },
    { ORDATAD (FPAC3, fpp_ac.fr[3], 12, "fourth mantissa") },
    { ORDATAD (FPAC4, fpp_ac.fr[4], 12, "fifth mantissa") },
    { ORDATAD (CMD, fpp_cmd, 12, "FPP command register") },
    { ORDATAD (STA, fpp_sta, 12, "status register") },
    { ORDATAD (APTA, fpp_apta, 15, "active parameter table (APT) pointer") },
    { GRDATAD (APTSVF, fpp_aptsvf, 8, 3, 12, "APT field") },
    { ORDATAD (FPC, fpp_fpc, 15, "floating program counter") },
    { ORDATAD (BRA, fpp_bra, 15, "base register") },
    { ORDATAD (XRA, fpp_xra, 15, "pointer to index register 0") },
    { ORDATAD (OPA, fpp_opa, 15, "operand address register") },
    { ORDATAD (SSF, fpp_ssf, 12, "single step flag") },
    { ORDATAD (LASTLOCK, fpp_last_lockbit, 12, "lockout from FPCOM") },
    { FLDATAD (FLAG, fpp_flag, 0, "done flag") },
    { NULL }
    };

DEVICE fpp_dev = {
    "FPP", &fpp_unit, fpp_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &fpp_reset,
    NULL, NULL, NULL,
    &fpp_dib, DEV_DISABLE | DEV_DIS
    };

/* IOT routines */

int32 fpp55 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* FPINT */
        return (fpp_flag? IOT_SKP | AC: AC);            /* skip on flag */

    case 2:                                             /* FPICL */
        fpp_reset (&fpp_dev);                           /* reset device */
        break;

    case 3:                                             /* FPCOM */
        if (!fpp_flag && !(fpp_sta & FPS_RUN)) {        /* flag clr, !run? */
            fpp_cmd = AC;                               /* load cmd */
            fpp_last_lockbit = fpp_cmd & FPS_LOCK;      /* remember lock state */
            fpp_sta = (fpp_sta & ~FPC_STA) |            /* copy flags */
                (fpp_cmd & FPC_STA);                    /* to status */
            }
        break;

    case 4:                                             /* FPHLT */
        if (fpp_sta & FPS_RUN) {                        /* running? */
            if (fpp_sta & FPS_PAUSE)                    /* paused? */
                fpp_fpc = (fpp_fpc - 1) & ADDRMASK;     /* decr FPC */
            fpp_sta &= ~FPS_PAUSE;                      /* no longer paused */
            sim_cancel (&fpp_unit);                     /* stop execution */
            fpp_dump_apt (fpp_apta, FPS_HLTX);          /* dump APT */
            fpp_ssf = 1;                                /* assume sstep */
            }
        else if (!fpp_flag)
            fpp_ssf = 1;                                /* FPST sing steps */
        if (fpp_sta & FPS_DVZX)                         /* fix diag timing */
            fpp_sta |= FPS_HLTX;
        break;

    case 5:                                             /* FPST */
        if (!fpp_flag && !(fpp_sta & FPS_RUN)) {        /* flag clr, !run? */
            if (fpp_ssf)
                fpp_sta |= fpp_last_lockbit; 
            fpp_sta &= ~FPS_HLTX;                       /* Clear halted */
            fpp_apta = (FPC_GETAPTF (fpp_cmd) << 12) | AC;
            fpp_load_apt (fpp_apta);                    /* load APT */
            fpp_opa = fpp_fpc;
            sim_activate (&fpp_unit, 0);                /* start unit */
            return IOT_SKP | AC;
            }
        if ((fpp_sta & (FPS_RUN|FPS_PAUSE)) == (FPS_RUN|FPS_PAUSE)) {
            fpp_sta &= ~FPS_PAUSE;                      /* continue */
            sim_activate (&fpp_unit, 0);                /* start unit */
            return (IOT_SKP | AC);
            }
        break;

    case 6:                                             /* FPRST */
        return fpp_sta;

    case 7:                                             /* FPIST */
        if (fpp_flag) {                                 /* if flag set */
            uint32 old_sta = fpp_sta;
            fpp_flag = 0;                               /* clr flag, status */
            fpp_sta &= ~(FPS_DP|FPS_EP|FPS_TRPX|FPS_DVZX|FPS_IOVX|FPS_FOVX|FPS_UNF);
            int_req &= ~INT_FPP;                        /* clr int req */
            return IOT_SKP | old_sta;                   /* ret old status */
            }
        break;

    default:
        return (stop_inst << IOT_V_REASON) | AC;
        }                                               /* end switch */

return AC;
}

int32 fpp56 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 7:                                             /* FPEP */
        if ((AC & 04000) && !(fpp_sta & FPS_RUN)) {     /* if AC0, not run, */
            fpp_sta = (fpp_sta | FPS_EP) & ~FPS_DP;     /* set ep */
            AC = 0;
            }
        break;

    default:
        return (stop_inst << IOT_V_REASON) | AC;
        }                                               /* end switch */

return AC;
}

/* Service routine */

t_stat fpp_svc (UNIT *uptr)
{
FPN x;
uint32 ir, op, op2, op3, ad, ea, wd;
uint32 i;
int32 sc;

fpp_ac.exp = SEXT12 (fpp_ac.exp);                       /* sext AC exp */
do {                                                    /* repeat */
    ir = fpp_read (fpp_fpc);                            /* get instr */
    fpp_fpc = (fpp_fpc + 1) & ADDRMASK;                 /* incr FP PC */
    op = (ir >> 7) & 037;                               /* get op+mode */
    op2 = (ir >> 3) & 017;                              /* get subop */
    op3 = ir & 07;                                      /* get field/xr */
    fpp_sta &= ~FPS_XXXM;                               /* not mem op */

    switch (op) {                                       /* case on op+mode */
    case 000:                                           /* operates */

        switch (op2) {                                  /* case on subop */
        case 000:                                       /* no-operands */
            switch (op3) {                              /* case on subsubop */

            case 0:                                     /* FEXIT */
            /* if already trapped, don't update APT, just update status */
                if (fpp_sta & (FPS_DVZX|FPS_IOVX|FPS_FOVX|FPS_UNF)) 
                    fpp_sta |= FPS_HLTX;
                else
                    fpp_dump_apt (fpp_apta, 0);
                break;

            case 1:                                     /* FPAUSE */
                fpp_sta |= FPS_PAUSE;
                break;

            case 2:                                     /* FCLA */
                fpp_copy (&fpp_ac, &fpp_zero);          /* clear FAC */
                break;

            case 3:                                     /* FNEG */
                fpp_fr_neg (fpp_ac.fr, EXACT);          /* do exact length */
                break;

            case 4:                                     /* FNORM */
                if (!(fpp_sta & FPS_DP)) {              /* fp or ep only */
                    fpp_copy (&x, &fpp_ac);             /* copy AC */
                    fpp_norm (&x, EXACT);               /* do exact length */
                    fpp_copy (&fpp_ac, &x);             /* copy back */
                    }
                break;

            case 5:                                     /* STARTF */
                if (fpp_sta & FPS_EP) {                 /* if ep, */
                    fpp_copy (&x, &fpp_ac);             /* copy AC */
                    fpp_round (&x);                     /* round */
                    fpp_copy (&fpp_ac, &x);             /* copy back */
                    }
                fpp_sta &= ~(FPS_DP|FPS_EP);
                break;

            case 6:                                     /* STARTD */
                fpp_sta = (fpp_sta | FPS_DP) & ~FPS_EP;
                break;

            case 7:                                     /* JAC */
                fpp_fpc = ((fpp_ac.fr[0] & 07) << 12) | fpp_ac.fr[1];
                break;
                }
            break;

        case 001:                                       /* ALN */
            if (op3 != 0) {                             /* if xr, */
                wd = fpp_read_xr (op3);                 /* use val */
                fpp_opa = fpp_xra + op3;
                }
            else wd = 027;                              /* else 23 */
            if (!(fpp_sta & FPS_DP)) {                  /* fp or ep? */
                sc = (SEXT12(wd) - fpp_ac.exp) & 07777; /* alignment */
                sc = SEXT12 (sc);
                fpp_ac.exp = SEXT12(wd);                /* new exp */
                }
            else sc = SEXT12 (wd);                      /* dp - simple cnt */
            if (sc < 0)                                 /* left? */
                fpp_fr_lshn (fpp_ac.fr,  -sc, EXACT);
            else fpp_fr_algn (fpp_ac.fr, sc, EXACT);
            if (fpp_fr_test (fpp_ac.fr, 0, EXACT) == 0) /* zero? */
                fpp_ac.exp = 0;                         /* clean exp */
            break;

        case 002:                                       /* ATX */
            if (fpp_sta & FPS_DP)                       /* dp? */
                fpp_write_xr (op3, fpp_ac.fr[1]);       /* xr<-FAC<12:23> */
            else {
                fpp_copy (&x, &fpp_ac);                 /* copy AC */
                sc = 027 - x.exp;                       /* shift amt */
                if (sc < 0)                             /* left? */
                    fpp_fr_lshn (x.fr, -sc, EXACT);
                else fpp_fr_algn (x.fr, sc, EXACT);
                fpp_write_xr (op3, x.fr[1]);            /* xr<-val<12:23> */
                }
            break;

        case 003:                                       /* XTA */
            for (i = FPN_NFR_FP; i < FPN_NFR_EP; i++)
                x.fr[i] = 0;                            /* clear FOP2-4 */
            x.fr[1] = fpp_read_xr (op3);                /* get XR value */
            x.fr[0] = (x.fr[1] & 04000)? 07777: 0;
            x.exp = 027;                                /* standard exp */
            if (!(fpp_sta & FPS_DP)) {                  /* fp or ep? */
                fpp_norm (&x, EXACT);                   /* normalize */
                }
            fpp_copy (&fpp_ac, &x);                     /* result to AC */
            if (fpp_sta & FPS_DP)                       /* dp skips exp */
                fpp_ac.exp = x.exp;                     /*  so force copy */
            fpp_opa = fpp_xra + op3;
            break;

        case 004:                                       /* NOP */
            break;

        case 005:                                       /* STARTE */
            if (!(fpp_sta & FPS_EP)) {
                fpp_sta = (fpp_sta | FPS_EP) & ~FPS_DP;
                for (i = FPN_NFR_FP; i < FPN_NFR_EP; i++)
                    fpp_ac.fr[i] = 0;                   /* clear FAC2-4 */
                }
            break;

        case 010:                                       /* LDX */
            wd = fpp_ad15 (0);                          /* load XR immed */
            fpp_write_xr (op3, wd);
            fpp_opa = fpp_xra + op3;
            break;

        case 011:                                       /* ADDX */
            wd = fpp_ad15 (0);
            wd = wd + fpp_read_xr (op3);                /* add to XR immed */
            fpp_write_xr (op3, wd);                     /* trims to 12b */
            fpp_opa = fpp_xra + op3;
            break;

        default:
            return stop_inst;
            }                                           /* end case subop */
        break;

    case 001:                                           /* FLDA */
        ea = fpp_1wd_dir (ir);
        fpp_read_op (ea, &fpp_ac);
        break;

    case 002:
        ea = fpp_2wd_dir (ir);
        fpp_read_op (ea, &fpp_ac);
        if (fpp_sta & FPS_DP)
            fpp_opa = ea + 1;
        else fpp_opa = ea + 2;
        break;

    case 003:
        ea = fpp_indir (ir);
        fpp_read_op (ea, &fpp_ac);
        break;

    case 004:                                           /* jumps and sets */
        ad = fpp_ad15 (op3);                            /* get 15b address */
        switch (op2) {                                  /* case on subop */

        case 000: case 001: case 002: case 003:         /* cond jump */
        case 004: case 005: case 006: case 007:
            if (fpp_cond_met (op2))                     /* br if cond */
                fpp_fpc = ad;
            break;

        case 010:                                       /* SETX */
            fpp_xra = ad;
            break;

        case 011:                                       /* SETB */
            fpp_bra = ad;
            break;

        case 012:                                       /* JSA */
            fpp_write (ad, 01030 + (fpp_fpc >> 12));    /* save return */
            fpp_write (ad + 1, fpp_fpc);                /* trims to 12b */
            fpp_fpc = (ad + 2) & ADDRMASK;
            fpp_opa = fpp_fpc - 1;
            break;

        case 013:                                       /* JSR */
            fpp_write (fpp_bra + 1, 01030 + (fpp_fpc >> 12));
            fpp_write (fpp_bra + 2, fpp_fpc);           /* trims to 12b */
            fpp_opa = fpp_fpc = ad;
            break;

        default:
            return stop_inst;
            }                                           /* end case subop */
        break;

    case 005:                                           /* FADD */
        ea = fpp_1wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&fpp_ac, &x, 0);
        break;
        
    case 006:
        ea = fpp_2wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&fpp_ac, &x, 0);
        break;

    case 007:
        ea = fpp_indir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&fpp_ac, &x, 0);
        break;

    case 010: {                                         /* JNX */
        uint32 xrn = op2 & 07;
        ad = fpp_ad15 (op3);                            /* get 15b addr */
        wd = fpp_read_xr (xrn);                         /* read xr */
        if (op2 & 010) {                                /* inc? */
            wd = (wd + 1) & 07777;
            fpp_write_xr (xrn, wd);                     /* ++xr */
            }
        if (wd != 0)                                    /* xr != 0? */
            fpp_fpc = ad;                               /* jump */
        break;
        }
    case 011:                                           /* FSUB */
        ea = fpp_1wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&fpp_ac, &x, 1);
        break;
        
    case 012:
        ea = fpp_2wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&fpp_ac, &x, 1);
        break;

    case 013:
        ea = fpp_indir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&fpp_ac, &x, 1);
        break;

    case 014:                                           /* TRAP3 */
    case 020:                                           /* TRAP4 */
        fpp_opa = fpp_ad15 (op3);
        fpp_dump_apt (fpp_apta, FPS_TRPX);
        break;

    case 015:                                           /* FDIV */
        ea = fpp_1wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_div (&fpp_ac, &x);
        break;
        
    case 016:
        ea = fpp_2wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_div (&fpp_ac, &x);
        break;

    case 017:
        ea = fpp_indir (ir);
        fpp_read_op (ea, &x);
        fpp_div (&fpp_ac, &x);
        break;

    case 021:                                           /* FMUL */
        ea = fpp_1wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_mul (&fpp_ac, &x);
        break;
        
    case 022:
        ea = fpp_2wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_mul (&fpp_ac, &x);
        break;

    case 023:
        ea = fpp_indir (ir);
        fpp_read_op (ea, &x);
        fpp_mul (&fpp_ac, &x);
        break;

    case 024:                                           /* LTR */
        fpp_copy (&fpp_ac, (fpp_cond_met (op2 & 07)? &fpp_one: &fpp_zero));
        break;

    case 025:                                           /* FADDM */
        fpp_sta |= FPS_XXXM;
        ea = fpp_1wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&x, &fpp_ac, 0);
        fpp_write_op (ea, &x);                          /* store result */
        break;
        
    case 026:
        fpp_sta |= FPS_XXXM;
        ea = fpp_2wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&x, &fpp_ac, 0);
        fpp_write_op (ea, &x);                          /* store result */
        break;

    case 027:
        fpp_sta |= FPS_XXXM;
        ea = fpp_indir (ir);
        fpp_read_op (ea, &x);
        fpp_add (&x, &fpp_ac, 0);
        fpp_write_op (ea, &x);                          /* store result */
        break;

    case 030:                                           /* IMUL/LEA */
        ea = fpp_2wd_dir (ir);                          /* 2-word direct */
        if (fpp_sta & FPS_DP) {                         /* dp? */
            fpp_read_op (ea, &x);                       /* IMUL */
            fpp_imul (&fpp_ac, &x);
            }
        else {                                          /* LEA */
            fpp_sta = (fpp_sta | FPS_DP) & ~FPS_EP;     /* set dp */
            fpp_ac.fr[0] = (ea >> 12) & 07;
            fpp_ac.fr[1] = ea & 07777;
            }
        break;

    case 031:                                           /* FSTA */
        ea = fpp_1wd_dir (ir);
        fpp_write_op (ea, &fpp_ac);
        break;

    case 032:
        ea = fpp_2wd_dir (ir);
        fpp_write_op (ea, &fpp_ac);
        break;

    case 033:
        ea = fpp_indir (ir);
        fpp_write_op (ea, &fpp_ac);
        break;

    case 034:                                           /* IMULI/LEAI */
        ea = fpp_indir (ir);                            /* 1-word indir */
        if (fpp_sta & FPS_DP) {                         /* dp? */
            fpp_read_op (ea, &x);                       /* IMUL */
            fpp_imul (&fpp_ac, &x);
            }
        else {                                          /* LEA */
            fpp_sta = (fpp_sta | FPS_DP) & ~FPS_EP;     /* set dp */
            fpp_ac.fr[0] = (ea >> 12) & 07;
            fpp_ac.fr[1] = ea & 07777;
            fpp_opa = ea;
            }
        break;

    case 035:                                           /* FMULM */
        fpp_sta |= FPS_XXXM;
        ea = fpp_1wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_mul (&x, &fpp_ac);
        fpp_write_op (ea, &x);                          /* store result */
        break;
        
    case 036:
        fpp_sta |= FPS_XXXM;
        ea = fpp_2wd_dir (ir);
        fpp_read_op (ea, &x);
        fpp_mul (&x, &fpp_ac);
        fpp_write_op (ea, &x);                          /* store result */
        break;

    case 037:
        fpp_sta |= FPS_XXXM;
        ea = fpp_indir (ir);
        fpp_read_op (ea, &x);
        fpp_mul (&x, &fpp_ac);
        fpp_write_op (ea, &x);                          /* store result */
        break;
        }                                               /* end sw op+mode */

    if (fpp_ssf) {
        fpp_dump_apt (fpp_apta, FPS_HLTX);              /* dump APT */
        fpp_ssf = 0;
        }

    if (sim_interval)
        sim_interval = sim_interval - 1;
    } while ((sim_interval > 0) && 
             ((fpp_sta & (FPS_RUN|FPS_PAUSE|FPS_LOCK)) == (FPS_RUN|FPS_LOCK)));
if ((fpp_sta & (FPS_RUN|FPS_PAUSE)) == FPS_RUN)
    sim_activate (uptr, 1);
fpp_ac.exp &= 07777;                                    /* mask AC exp */
return SCPE_OK;
}

/* Address decoding routines */

uint32 fpp_1wd_dir (uint32 ir)
{
uint32 ad; 

ad = fpp_bra + ((ir & 0177) * 3);                       /* base + 3*7b off */
if (fpp_sta & FPS_DP)                                   /* dp? skip exp */
    ad = ad + 1;
ad = ad & ADDRMASK;
if (fpp_sta & FPS_DP)
    fpp_opa = ad + 1;
else fpp_opa = ad + 2;
return ad;
}

uint32 fpp_2wd_dir (uint32 ir)
{
uint32 ad;

ad = fpp_ad15 (ir);                                     /* get 15b addr */
return fpp_adxr (ir, ad);                               /* do indexing */
}

uint32 fpp_indir (uint32 ir)
{
uint32 ad, wd1, wd2;

ad = fpp_bra + ((ir & 07) * 3);                         /* base + 3*3b off */
wd1 = fpp_read (ad + 1);                                /* bp+off points to */
wd2 = fpp_read (ad + 2);
ad = ((wd1 & 07) << 12) | wd2;                          /* indirect ptr */

ad = fpp_adxr (ir, ad);                                 /* do indexing */
if (fpp_sta & FPS_DP)
    fpp_opa = ad + 1;
else fpp_opa = ad + 2;
return ad;
}

uint32 fpp_ad15 (uint32 hi)
{
uint32 ad;

ad = ((hi & 07) << 12) | fpp_read (fpp_fpc);            /* 15b addr */
fpp_fpc = (fpp_fpc + 1) & ADDRMASK;                     /* incr FPC */
return ad;                                              /* return addr */
}

uint32 fpp_adxr (uint32 ir, uint32 base_ad)
{
uint32 xr, wd;

xr = (ir >> 3) & 07;
wd = fpp_read_xr (xr);                                  /* get xr */
if (ir & 0100) {                                        /* increment? */
    wd = (wd + 1) & 07777;                              /* inc, rewrite */
    fpp_write_xr (xr, wd);
    }
if (xr != 0) {                                          /* indexed? */
    if (fpp_sta & FPS_EP)
        wd = wd * 6;                                    /* scale by len */
    else if (fpp_sta & FPS_DP)
        wd = wd * 2;
    else wd = wd * 3;
    return (base_ad + wd) & ADDRMASK;                   /* return index */
    }
else return base_ad & ADDRMASK;                         /* return addr */
}

/* Computation routines */

/* Fraction/floating add */

void fpp_add (FPN *a, FPN *b, uint32 sub)
{
FPN x, y, z;
uint32 c, ediff;

fpp_zcopy (&x, a);                                      /* copy opnds */
fpp_zcopy (&y, b);
if (sub)                                                /* subtract? */
    fpp_fr_neg (y.fr, EXACT);                           /* neg B, exact */
if (fpp_sta & FPS_DP) {                                 /* dp? */
    uint32 cout = fpp_fr_add (z.fr, x.fr, y.fr, EXTEND);/* z = a + b */
    uint32 zsign = z.fr[0] & FPN_FRSIGN;
    cout = (cout? 04000: 0);                            /* make sign bit */
    /* overflow is indicated when signs are equal and overflow does not
       match the result sign bit */
    fpp_copy (a, &z);                                   /* result is z */
    if (!((x.fr[0] ^ y.fr[0]) & FPN_FRSIGN) && (cout != zsign)) {
        fpp_copy (a, &z);                               /* copy out result */
        fpp_dump_apt (fpp_apta, FPS_IOVX);              /* int ovf? */
        return;
        }
    }
else {                                                  /* fp or ep */
    if (fpp_fr_test (b->fr, 0, EXACT) == 0)             /* B == 0? */
        z = x;                                          /* result is A */
    else if (fpp_fr_test (a->fr, 0, EXACT) == 0)        /* A == 0? */
        z = y;                                          /* result is B */
    else {                                              /* fp or ep */
        if (x.exp < y.exp) {                            /* |a| < |b|? */
            z = x;                                      /* exchange ops */
            x = y;
            y = z;
            }
        ediff = x.exp - y.exp;                          /* exp diff */
        if (ediff <= (uint32) ((fpp_sta & FPS_EP)? 59: 24)) { /* any add? */
            z.exp = x.exp;                              /* result exp */
            if (ediff != 0)                             /* any align? */
                fpp_fr_algn (y.fr, ediff, EXTEND);      /* align, 60b */
            c = fpp_fr_add (z.fr, x.fr, y.fr, EXTEND);  /* add fractions */
            if ((((x.fr[0] ^ y.fr[0]) & FPN_FRSIGN) == 0) && /* same signs? */
                (c ||                                   /* carry out? */
                ((~x.fr[0] & z.fr[0] & FPN_FRSIGN)))) { /* + to - change? */
                fpp_fr_rsh1 (z.fr, c << 11, EXTEND);    /* rsh, insert cout */
                z.exp = z.exp + 1;                      /* incr exp */
                }                                       /* end same signs */
            }                                           /* end in range */
            else z = x;                                 /* ovrshift */
        }                                               /* end ops != 0 */
    if (fpp_norm (&z, EXTEND))                          /* norm, !exact? */
        fpp_round (&z);                                 /* round */
    fpp_copy (a, &z);                                   /* copy out */
    fpp_test_xp (&z);                                   /* ovf, unf? */
    }                                                   /* end else */
return;
}

/* Fraction/floating multiply */

void fpp_mul (FPN *a, FPN *b)
{
FPN x, y, z;

fpp_zcopy (&x, a);                                      /* copy opnds */
fpp_zcopy (&y, b);
if ((fpp_fr_test(y.fr, 0, EXACT-1) == 0) && (y.fr[EXACT-1] < 2)) {
    y.exp = 0;
    y.fr[EXACT-1] = 0;
}
if (fpp_sta & FPS_DP)                                   /* dp? */
    fpp_fr_mul (z.fr, x.fr, y.fr, TRUE);                /* mult frac */
else {                                                  /* fp or ep */
    fpp_norm (&x, EXACT);
    fpp_norm (&y, EXACT);
    z.exp = x.exp + y.exp;                              /* add exp */
    fpp_fr_mul (z.fr, x.fr, y.fr, TRUE);                /* mult frac */
    if (fpp_norm (&z, EXTEND))                          /* norm, !exact? */
        fpp_round (&z);                                 /* round */
    fpp_copy (a, &z);
    if (z.exp > 2047)
        fpp_dump_apt (fpp_apta, FPS_FOVX);              /* trap */
    return;
    }
fpp_copy (a, &z);                                       /* result is z */
return;
}

/* Fraction/floating divide */

void fpp_div (FPN *a, FPN *b)
{
FPN x, y, z;

if (fpp_fr_test (b->fr, 0, EXACT) == 0) {               /* divisor 0? */
    fpp_dump_apt (fpp_apta, FPS_DVZX);                  /* error */
    return;
    }
if (fpp_fr_test (a->fr, 0, EXACT) == 0)                 /* dividend 0? */
    return;                                             /* quotient is 0 */
fpp_zcopy (&x, a);                                      /* copy opnds */
fpp_zcopy (&y, b);
if (fpp_sta & FPS_DP) {                                 /* dp? */
    if (fpp_fr_div (z.fr, x.fr, y.fr)) {                /* fr div, ovflo? */
        fpp_dump_apt (fpp_apta, FPS_IOVX);              /* error */
        return;
        }
    fpp_copy (a, &z);                                   /* result is z */
    }
else {                                                  /* fp or ep */
    fpp_norm (&y, EXACT);                               /* norm divisor */
    if (fpp_fr_test (x.fr, 04000, EXACT) == 0) {        /* divd 1.000...? */
        x.fr[0] = 06000;                                /* fix */
        x.exp = x.exp + 1;
        }
    z.exp = x.exp - y.exp;                              /* calc exp */
    if (fpp_fr_div (z.fr, x.fr, y.fr)) {                /* fr div, ovflo? */
        uint32 cin = (a->fr[0] ^ b->fr[0]) & FPN_FRSIGN;
        fpp_fr_rsh1 (z.fr, cin, EXTEND);                /* rsh, insert sign */
        z.exp = z.exp + 1;                              /* incr exp */
        }
    if (fpp_norm (&z, EXTEND))                          /* norm, !exact? */
        fpp_round (&z);                                 /* round */
    fpp_copy (a, &z);
    if (z.exp > 2048) {                                /* underflow? */
        if (fpp_cmd & FPC_UNFX) {                       /* trap? */
            fpp_dump_apt (fpp_apta, FPS_UNF);
            return;
            }
        }
    }
return;
}

/* Integer multiply - returns true if overflow */

t_bool fpp_imul (FPN *a, FPN *b)
{
uint32 sext;
FPN x, y, z;

fpp_zcopy (&x, a);                                      /* copy args */
fpp_zcopy (&y, b);
fpp_fr_mul (z.fr, x.fr, y.fr, FALSE);                   /* mult fracs */
a->fr[0] = z.fr[1];                                     /* low 24b */
a->fr[1] = z.fr[2];
if ((a->fr[0] == 0) && (a->fr[1] == 0))                 /* fpp zeroes exp */
    a->exp = 0;                                         /* even in dp mode */
sext = (z.fr[2] & FPN_FRSIGN)? 07777: 0;
if (((z.fr[0] | z.fr[1] | sext) != 0) &&                /* hi 25b == 0 */
    ((z.fr[0] & z.fr[1] & sext) != 07777)) {            /* or 777777774? */
    fpp_dump_apt (fpp_apta, FPS_IOVX);
    return TRUE;
    }
return FALSE;
}

/* Auxiliary floating point routines */

t_bool fpp_cond_met (uint32 cond)
{
switch (cond) {

    case 0:
        return (fpp_fr_test (fpp_ac.fr, 0, EXACT) == 0);

    case 1:
        return (fpp_fr_test (fpp_ac.fr, 0, EXACT) >= 0);

    case 2:
        return (fpp_fr_test (fpp_ac.fr, 0, EXACT) <= 0);

    case 3:
        return 1;

    case 4:
        return (fpp_fr_test (fpp_ac.fr, 0, EXACT) != 0);

    case 5:
        return (fpp_fr_test (fpp_ac.fr, 0, EXACT) < 0);

    case 6:
        return (fpp_fr_test (fpp_ac.fr, 0, EXACT) > 0);

    case 7:
        return (fpp_ac.exp > 027);
    }
return 0;
}

/* Normalization - returns TRUE if rounding possible, FALSE if exact */

t_bool fpp_norm (FPN *a, uint32 cnt)
{
if (fpp_fr_test (a->fr, 0, cnt) == 0) {                 /* zero? */
    a->exp = 0;                                         /* clean exp */
    return FALSE;                                       /* don't round */
    }
while (((a->fr[0] == 0) && !(a->fr[1] & 04000)) ||      /* lead 13b same? */
       ((a->fr[0] == 07777) && (a->fr[1] & 04000))) {
    fpp_fr_lsh12 (a->fr, cnt);                          /* move word */
    a->exp = a->exp - 12;
    }
while (((a->fr[0] ^ (a->fr[0] << 1)) & FPN_FRSIGN) == 0) { /* until norm */
    fpp_fr_lsh1 (a->fr, cnt);                           /* shift 1b */
    a->exp = a->exp - 1;
    }
if (fpp_fr_test (a->fr, 04000, EXACT) == 0) {           /* 4000...0000? */
    a->fr[0] = 06000;                                   /* chg to 6000... */
    a->exp = a->exp + 1;                                /* with exp+1 */
    return FALSE;                                       /* don't round */
    }
return TRUE;
}

/* Exact fp number copy */

void fpp_copy (FPN *a, FPN *b)
{
uint32 i;

if (!(fpp_sta & FPS_DP))
    a->exp = b->exp;
for (i = 0; i < EXACT; i++)
    a->fr[i] = b->fr[i];
return;
}

/* Zero extended fp number copy (60b) */

void fpp_zcopy (FPN *a, FPN *b)
{
uint32 i;

a->exp = b->exp;
for (i = 0; i < FPN_NFR_EP; i++) {
    if ((i < FPN_NFR_FP) || (fpp_sta & FPS_EP))
        a->fr[i] = b->fr[i];
    else a->fr[i] = 0;
    }
a->fr[i++] = 0;
a->fr[i] = 0;
return;
}

/* Test exp for overflow or underflow, returns TRUE on trap */

t_bool fpp_test_xp (FPN *a)
{
if (a->exp > 2047) {                                /* overflow? */
    fpp_dump_apt (fpp_apta, FPS_FOVX);              /* trap */
    return TRUE;
    }
if (a->exp < -2048) {                               /* underflow? */
    if (fpp_cmd & FPC_UNFX) {                       /* trap? */
        fpp_dump_apt (fpp_apta, FPS_UNF);
        return TRUE;
        }
    fpp_copy (a, &fpp_zero);                        /* flush to 0 */
    }
return FALSE;
}

/* Round dp/fp value */

void fpp_round (FPN *a)
{
int32 i;
uint32 cin, afr0_sign;

if (fpp_sta & FPS_EP)                               /* ep? */
    return;                                         /* don't round */
afr0_sign = a->fr[0] & FPN_FRSIGN;                  /* save input sign */
cin = afr0_sign? 03777: 04000;
for (i = FPN_NFR_FP; i >= 0; i--) {                 /* 3 words */
   a->fr[i] = a->fr[i] + cin;                       /* add in carry */
   cin = (a->fr[i] >> 12) & 1;
   a->fr[i] = a->fr[i] & 07777;
   }
if (!(fpp_sta & FPS_DP) &&                          /* fp? */
    (afr0_sign ^ (a->fr[0] & FPN_FRSIGN))) {        /* sign change? */
    fpp_fr_rsh1 (a->fr, afr0_sign, EXACT);          /* rsh, insert sign */
    a->exp = a->exp + 1;
    }
return;
}

/* N-precision integer routines */

/* Fraction add/sub */

uint32 fpp_fr_add (uint32 *c, uint32 *a, uint32 *b, uint32 cnt)

{
uint32 i, cin;

for (i = cnt, cin = 0; i > 0; i--) {
    c[i - 1] = a[i - 1] + b[i - 1] + cin;
    cin = (c[i - 1] >> 12) & 1;
    c[i - 1] = c[i - 1] & 07777;
    }
return cin;
}

void fpp_fr_sub (uint32 *c, uint32 *a, uint32 *b, uint32 cnt)
{
uint32 i, cin;

for (i = cnt, cin = 0; i > 0; i--) {
    c[i - 1] = a[i - 1] - b[i - 1] - cin;
    cin = (c[i - 1] >> 12) & 1;
    c[i - 1] = c[i - 1] & 07777;
    }
return;
}

/* Fraction multiply - always develop 60b, multiply is
   either 24b*24b or 60b*60b
   
   This is a signed multiply.  The shift in for signed multiply is
   technically ALU_N XOR ALU_V.  This can be simplified as follows:

   a-sign   c-sign  result-sign     cout    overflow    N XOR V = shift in

   0        0       0               0       0           0
   0        0       1               0       1           0
   0        1       0               1       0           0
   0        1       1               0       0           1
   1        0       0               1       0           0
   1        0       1               0       0           1
   1        1       0               1       1           1
   1        1       1               1       0           1

   If a-sign == c-sign, shift-in = a-sign
   If a-sign != c-sign, shift-in = result-sign
   */

void fpp_fr_mul (uint32 *c, uint32 *a, uint32 *b, t_bool fix)
{
uint32 i, cnt, lo, wc, fill, b_sign;

b_sign = b[0] & FPN_FRSIGN;                         /* remember b's sign */

fpp_fr_fill (c, 0, FPN_NFR_MDS);                    /* clr answer */
if (fpp_sta & FPS_EP)                               /* ep? */
    lo = FPN_NFR_EP;                                /* low order mpyr word */
else  
    lo = FPN_NFR_FP;                                /* low order mpyr word */

if (fix)
    fpp_fr_algn (a, 12, FPN_NFR_MDS + 1);           /* fill left with sign */
wc = 2;                                             /* 3 words at start */
fill = 0;
cnt = lo * 12;                                      /* total steps */
for (i = 0; i < cnt; i++) { 
    if ((i % 12) == 0) {
        wc++;                                       /* do another word */
        lo--;                                       /* and next mpyr word */
        fpp_fr_algn (c, 24, wc + 1);
        c[wc] = 0;
        c[0] = c[1] = fill;                         /* propagate sign */
        }
    if (b[lo] & FPN_FRSIGN)                         /* mpyr bit set? */
        fpp_fr_add(c, a, c, wc);
    fill = ((c[0] & FPN_FRSIGN) ? 07777 : 0);       /* remember sign */
    fpp_fr_lsh1 (c, wc);                            /* shift the result */
    fpp_fr_lsh1 (b + lo, 1);                        /* shift mpcd */
    
    }

if (!fix)                                           /* imul shifts result */
    fpp_fr_rsh1 (c, c[0] & FPN_FRSIGN, EXACT + 1);  /* result is 1 wd right */
if (b_sign) {                                       /* if mpyr was negative */
    if (fix)
        fpp_fr_lsh12 (a, FPN_NFR_MDS+1);            /* restore a */
    fpp_fr_sub (c, c, a, EXACT);                    /* adjust result */
    fpp_fr_sub (c, c, a, EXACT);
    }

return;
}

/* Fraction divide */

t_bool fpp_fr_div (uint32 *c, uint32 *a, uint32 *b)
{
uint32 i, old_c, lo, cnt, sign, b_sign, addsub, limit;
/* Number of words processed by each divide step */
static uint32 limits[7] = {6, 6, 5, 4, 3, 3, 2};

fpp_fr_fill (c, 0, FPN_NFR_MDS);                    /* clr answer */
sign = (a[0] ^ b[0]) & FPN_FRSIGN;                  /* sign of result */
b_sign = (b[0] & FPN_FRSIGN);
if (a[0] & FPN_FRSIGN)                              /* |a| */
    fpp_fr_neg (a, EXACT);
if (fpp_sta & FPS_EP)                               /* ep? 6 words */
    lo = FPN_NFR_EP-1;
else lo = FPN_NFR_FP-1;                             /* fp, dp? 3 words */
cnt = (lo + 1) * 12;
addsub = 04000;                                     /* setup first op */
for (i = 0; i < cnt; i++) {                         /* loop */
    limit = limits[i / 12];                         /* how many wds this time */
    fpp_fr_lsh1 (c, FPN_NFR_MDS);                   /* shift quotient */
    if (addsub ^ b_sign)                            /* diff signs, subtr */
        fpp_fr_sub (a, a, b, limit);                /* divd - divr */
    else
        fpp_fr_add (a, a, b, limit);                /* restore */
    if (!(a[0] & FPN_FRSIGN)) {
        c[lo] |= 1;                                 /* set quo bit */
        addsub = 04000;                             /* sign for nxt loop */
        }
    else addsub = 0;
    fpp_fr_lsh1 (a, limit);                         /* shift dividend */
    }
old_c = c[0];                                       /* save ho quo */
if (sign)                                           /* expect neg ans? */
    fpp_fr_neg (c, EXTEND);                         /* -quo */
if (old_c & FPN_FRSIGN)                             /* sign set before */
    return TRUE;                                    /* neg? */
return FALSE;
}

/* Negate - 24b or 60b */

uint32 fpp_fr_neg (uint32 *a, uint32 cnt)
{
uint32 i, cin;

for (i = cnt, cin = 1; i > 0; i--) {
    a[i - 1] = (~a[i - 1] + cin) & 07777;
    cin = (cin != 0 && a[i - 1] == 0);
    }
return cin;
}

/* Test (compare to x'0...0) - 24b or 60b */

int32 fpp_fr_test (uint32 *a, uint32 v0, uint32 cnt)
{
uint32 i;

if (a[0] != v0)
    return (a[0] & FPN_FRSIGN)? -1: +1;
for (i = 1; i < cnt; i++) {
    if (a[i] != 0)
        return (a[0] & FPN_FRSIGN)? -1: +1;
    }
return 0;
}

/* Fraction compare - 24b or 60b */

int32 fpp_fr_cmp (uint32 *a, uint32 *b, uint32 cnt)
{
uint32 i;

if ((a[0] ^ b[0]) & FPN_FRSIGN)
    return (b[0] & FPN_FRSIGN)? +1: -1;
for (i = 0; i < cnt; i++) {
    if (a[i] > b[i])
        return (b[0] & FPN_FRSIGN)? +1: -1;
    if (a[i] < b[i])
        return (b[0] & FPN_FRSIGN)? -1: +1;
    }
return 0;
}

/* Fraction fill */

void fpp_fr_fill (uint32 *a, uint32 v, uint32 cnt)
{
uint32 i;

for (i = 0; i < cnt; i++)
    a[i] = v;
return;
}

/* Left shift n (unsigned) */

void fpp_fr_lshn (uint32 *a, uint32 sc, uint32 cnt)
{
uint32 i;

if (sc >= (cnt * 12)) {                             /* out of range? */
    fpp_fr_fill (a, 0, cnt);
    return;
    }
while (sc >= 12) {                                  /* word shift? */
    fpp_fr_lsh12 (a, cnt);
    sc = sc - 12;
    }
if (sc == 0)                                        /* any more? */
    return;
for (i = 1; i < cnt; i++)                           /* bit shift */
    a[i - 1] = ((a[i - 1] << sc) | (a[i] >> (12 - sc))) & 07777;
a[cnt - 1] = (a[cnt - 1] << sc) & 07777;
return;
}

/* Left shift 12b (unsigned) */

void fpp_fr_lsh12 (uint32 *a, uint32 cnt)
{
uint32 i;

for (i = 1; i < cnt; i++)
    a[i - 1] = a[i];
a[cnt - 1] = 0;
return;
}

/* Left shift 1b (unsigned) */

void fpp_fr_lsh1 (uint32 *a, uint32 cnt)
{
uint32 i;

for (i = 1; i < cnt; i++)
    a[i - 1] = ((a[i - 1] << 1) | (a[i] >> 11)) & 07777;
a[cnt - 1] = (a[cnt - 1] << 1) & 07777;
return;
}

/* Right shift 1b, with shift in */

void fpp_fr_rsh1 (uint32 *a, uint32 sign, uint32 cnt)
{
uint32 i;

for (i = cnt - 1; i > 0; i--)
    a[i] = ((a[i] >> 1) | (a[i - 1] << 11)) & 07777;
a[0] = (a[0] >> 1) | sign;
return;
}

/* Right shift n (signed) */

void fpp_fr_algn (uint32 *a, uint32 sc, uint32 cnt)
{
uint32 i, sign;

sign = (a[0] & FPN_FRSIGN)? 07777: 0;
if (sc >= (cnt * 12)) {                             /* out of range? */
    fpp_fr_fill (a, sign, cnt);
    return;
    }
while (sc >= 12) {
    for (i = cnt - 1; i > 0; i--)
        a[i] = a[i - 1];
    a[0] = sign;
    sc = sc - 12;
    }
if (sc == 0)
    return;
for (i = cnt - 1; i > 0; i--)
    a[i] = ((a[i] >> sc) | (a[i - 1] << (12 - sc))) & 07777;
a[0] = ((a[0] >> sc) | (sign << (12 - sc))) & 07777;
return;
}

/* Read/write routines */

void fpp_read_op (uint32 ea, FPN *a)
{
uint32 i;

if (!(fpp_sta & FPS_DP)) {
    a->exp = fpp_read (ea++);
    a->exp = SEXT12 (a->exp);
    }
for (i = 0; i < EXACT; i++)
    a->fr[i] = fpp_read (ea + i);
return;
}

void fpp_write_op (uint32 ea, FPN *a)
{
uint32 i;

fpp_opa = ea + 2;
if (!(fpp_sta & FPS_DP))
    fpp_write (ea++, a->exp);
for (i = 0; i < EXACT; i++)
    fpp_write (ea + i, a->fr[i]);
return;
}

uint32 fpp_read (uint32 ea)
{
ea = ea & ADDRMASK;
if (fpp_cmd & FPC_FIXF)
    ea = fpp_aptsvf | (ea & 07777);
return M[ea];
}

void fpp_write (uint32 ea, uint32 val)
{
ea = ea & ADDRMASK;
if (fpp_cmd & FPC_FIXF)
    ea = fpp_aptsvf | (ea & 07777);
if (MEM_ADDR_OK (ea))
    M[ea] = val & 07777;
return;
}

uint32 apt_read (uint32 ea)
{
ea = ea & ADDRMASK;
return M[ea];
}

void apt_write (uint32 ea, uint32 val)
{
ea = ea & ADDRMASK;
if (MEM_ADDR_OK (ea))
    M[ea] = val & 07777;
return;
}

/* Utility routines */

void fpp_load_apt (uint32 ad)
{
uint32 wd0, i;

wd0 = apt_read (ad++);
fpp_fpc = ((wd0 & 07) << 12) | apt_read (ad++);
if (FPC_GETFAST (fpp_cmd) != 017) {
    fpp_xra = ((wd0 & 00070) << 9) | apt_read (ad++);
    fpp_bra = ((wd0 & 00700) << 6) | apt_read (ad++);
    fpp_opa = ((wd0 & 07000) << 3) | apt_read (ad++);
    fpp_ac.exp = apt_read (ad++);
    for (i = 0; i < EXACT; i++)
        fpp_ac.fr[i] = apt_read (ad++);
    }
fpp_aptsvf = (ad - 1) & 070000;
fpp_sta |= FPS_RUN;
return;
}

void fpp_dump_apt (uint32 ad, uint32 sta)
{
uint32 wd0, i;

wd0 = (fpp_fpc >> 12) & 07;
if (FPC_GETFAST (fpp_cmd) != 017)
    wd0 = wd0 |
        ((fpp_opa >> 3) & 07000) |
        ((fpp_bra >> 6) & 00700) |
        ((fpp_xra >> 9) & 00070);
apt_write (ad++, wd0);
apt_write (ad++, fpp_fpc);
if (FPC_GETFAST (fpp_cmd) != 017) {
    apt_write (ad++, fpp_xra);
    apt_write (ad++, fpp_bra);
    apt_write (ad++, fpp_opa);
    apt_write (ad++, fpp_ac.exp);
    for (i = 0; i < EXACT; i++)
        apt_write (ad++, fpp_ac.fr[i]);
    }
fpp_sta = (fpp_sta | sta) & ~FPS_RUN;
fpp_flag = 1;
if (fpp_cmd & FPC_IE)
    int_req |= INT_FPP;
return;
}

/* Reset routine */

t_stat fpp_reset (DEVICE *dptr)
{
sim_cancel (&fpp_unit);
fpp_flag = 0;
fpp_last_lockbit = 0;
int_req &= ~INT_FPP;
if (sim_switches & SWMASK ('P')) {
    fpp_apta = 0;
    fpp_aptsvf = 0;
    fpp_fpc = 0;
    fpp_bra = 0;
    fpp_xra = 0;
    fpp_opa = 0;
    fpp_ac = fpp_zero;
    fpp_ssf = 0;
    fpp_sta = 0;
    fpp_cmd = 0;
    }
else {
    fpp_sta &= ~(FPS_DP|FPS_EP|FPS_TRPX|FPS_DVZX|FPS_IOVX|FPS_FOVX|FPS_UNF);
    fpp_cmd &= (FPC_DP|FPC_UNFX|FPC_IE);
    }

return SCPE_OK;
}
