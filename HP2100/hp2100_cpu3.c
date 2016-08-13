/* hp2100_cpu3.c: HP 2100/1000 FFP/DBI instructions

   Copyright (c) 2005-2016, J. David Bryan

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

   CPU3         Fast FORTRAN and Double Integer instructions

   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   09-May-12    JDB     Separated assignments from conditional expressions
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   05-Aug-08    JDB     Updated mp_dms_jmp calling sequence
   27-Feb-08    JDB     Added DBI self-test instruction
   23-Oct-07    JDB     Fixed unsigned-divide bug in .DDI
   17-Oct-07    JDB     Fixed unsigned-multiply bug in .DMP
   16-Oct-06    JDB     Calls FPP for extended-precision math
   12-Oct-06    JDB     Altered DBLE, DDINT for F-Series FFP compatibility
   26-Sep-06    JDB     Moved from hp2100_cpu1.c to simplify extensions
   09-Aug-06    JDB     Added double-integer instruction set
   18-Feb-05    JDB     Add 2100/21MX Fast FORTRAN Processor instructions

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
        (92851-90001, Mar-1981)
   - Macro/1000 Reference Manual (92059-90001, Dec-1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.
*/

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"

#if defined (HAVE_INT64)                                /* int64 support available */
#include "hp2100_fp1.h"
#else                                                   /* int64 support unavailable */
#include "hp2100_fp.h"
#endif                                                  /* end of int64 support */


/* Fast FORTRAN Processor.

   The Fast FORTRAN Processor (FFP) is a set of FORTRAN language accelerators
   and extended-precision (three-word) floating point routines.  Although the
   FFP is an option for the 2100 and later CPUs, each implements the FFP in a
   slightly different form.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A    12907A  12977B  13306B   std

   The instruction codes are mapped to routines as follows:

     Instr.   2100  1000-M 1000-E 1000-F    Instr.   2100  1000-M 1000-E 1000-F
     ------  ------ ------ ------ ------    ------  ------ ------ ------ ------
     105200    --   [nop]  [nop]  [test]    105220  .XFER  .XFER  .XFER  .XFER
     105201   DBLE   DBLE   DBLE   DBLE     105221  .GOTO  .GOTO  .GOTO  .GOTO
     105202   SNGL   SNGL   SNGL   SNGL     105222  ..MAP  ..MAP  ..MAP  ..MAP
     105203  .XMPY  .XMPY  .XMPY  .DNG      105223  .ENTR  .ENTR  .ENTR  .ENTR
     105204  .XDIV  .XDIV  .XDIV  .DCO      105224  .ENTP  .ENTP  .ENTP  .ENTP
     105205  .DFER  .DFER  .DFER  .DFER     105225    --   .PWR2  .PWR2  .PWR2
     105206    --   .XPAK  .XPAK  .XPAK     105226    --   .FLUN  .FLUN  .FLUN
     105207    --    XADD   XADD  .BLE      105227  $SETP  $SETP  $SETP  $SETP

     105210    --    XSUB   XSUB  .DIN      105230    --   .PACK  .PACK  .PACK
     105211    --    XMPY   XMPY  .DDE      105231    --     --   .CFER  .CFER
     105212    --    XDIV   XDIV  .DIS      105232    --     --     --   ..FCM
     105213  .XADD  .XADD  .XADD  .DDS      105233    --     --     --   ..TCM
     105214  .XSUB  .XSUB  .XSUB  .NGL      105234    --     --     --     --
     105215    --   .XCOM  .XCOM  .XCOM     105235    --     --     --     --
     105216    --   ..DCM  ..DCM  ..DCM     105236    --     --     --     --
     105217    --   DDINT  DDINT  DDINT     105237    --     --     --     --

   The F-Series maps different instructions to several of the standard FFP
   opcodes.  We first look for these and dispatch them appropriately before
   falling into the handler for the common instructions.

   The math functions use the F-Series FPP for implementation.  The FPP requires
   that the host compiler support 64-bit integers.  Therefore, if 64-bit
   integers are not available, the math instructions of the FFP are disabled.
   We allow this partial implementation as an aid in running systems generated
   for the FFP.  Most system programs did not use the math instructions, but
   almost all use .ENTR.  Supporting the latter even on systems that do not
   support the former still allows such systems to boot.

   Implementation notes:

    1. The "$SETP" instruction is sometimes listed as ".SETP" in the
       documentation.

    2. Extended-precision arithmetic routines (e.g., .XMPY) exist on the 1000-F,
       but they are assigned instruction codes in the single-precision
       floating-point module range.  They are replaced by several double integer
       instructions, which we dispatch to the double integer handler.

    3. The software implementation of ..MAP supports 1-, 2-, or 3-dimensional
       arrays, designated by setting A = -1, 0, and +1, respectively.  The
       firmware implementation supports only 2- and 3-dimensional access.

    4. The documentation for ..MAP for the 2100 FFP shows A = 0 or -1 for two or
       three dimensions, respectively, but the 1000 FFP shows A = 0 or +1.  The
       firmware actually only checks the LSB of A.

    5. The .DFER and .XFER implementations for the 2100 FFP return X+4 and Y+4
       in the A and B registers, whereas the 1000 FFP returns X+3 and Y+3.

    6. The .XFER implementation for the 2100 FFP returns to P+2, whereas the
       1000 implementation returns to P+1.

    7. The firmware implementations of DBLE, .BLE, and DDINT clear the overflow
       flag.  The software implementations do not change overflow.

    8. The M/E-Series FFP arithmetic instructions (.XADD, etc.) return negative
       infinity on negative overflow and positive infinity on positive overflow.
       The equivalent F-Series instructions return positive infinity on both.

    9. The protected memory lower bound for the .GOTO instruction is 2.

   Additional references:
    - DOS/RTE Relocatable Library Reference Manual (24998-90001, Oct-1981)
    - Implementing the HP 2100 Fast FORTRAN Processor (12907-90010, Nov-1974)
*/

static const OP_PAT op_ffp_f[32] = {                    /* patterns for F-Series only */
  OP_N,    OP_AAF,  OP_AX,   OP_N,                      /* [tst]  DBLE   SNGL   .DNG  */
  OP_N,    OP_AA,   OP_A,    OP_AAF,                    /* .DCO   .DFER  .XPAK  .BLE  */
  OP_N,    OP_N,    OP_N,    OP_N,                      /* .DIN   .DDE   .DIS   .DDS  */
  OP_AT,   OP_A,    OP_A,    OP_AAX,                    /* .NGL   .XCOM  ..DCM  DDINT */
  OP_N,    OP_AK,   OP_KKKK, OP_A,                      /* .XFER  .GOTO  ..MAP  .ENTR */
  OP_A,    OP_RK,   OP_R,    OP_K,                      /* .ENTP  .PWR2  .FLUN  $SETP */
  OP_RC,   OP_AA,   OP_R,    OP_A,                      /* .PACK  .CFER  ..FCM  ..TCM */
  OP_N,    OP_N,    OP_N,    OP_N                       /*  ---    ---    ---    ---  */
  };

static const OP_PAT op_ffp_e[32] = {                    /* patterns for 2100/M/E-Series */
  OP_N,    OP_AAF,  OP_AX,   OP_AXX,                    /* [nop]  DBLE   SNGL   .XMPY */
  OP_AXX,  OP_AA,   OP_A,    OP_AAXX,                   /* .XDIV  .DFER  .XPAK  XADD  */
  OP_AAXX, OP_AAXX, OP_AAXX, OP_AXX,                    /* XSUB   XMPY   XDIV   .XADD */
  OP_AXX,  OP_A,    OP_A,    OP_AAX,                    /* .XSUB  .XCOM  ..DCM  DDINT */
  OP_N,    OP_AK,   OP_KKKK, OP_A,                      /* .XFER  .GOTO  ..MAP  .ENTR */
  OP_A,    OP_RK,   OP_R,    OP_K,                      /* .ENTP  .PWR2  .FLUN  $SETP */
  OP_RC,   OP_AA,   OP_N,    OP_N,                      /* .PACK  .CFER   ---    ---  */
  OP_N,    OP_N,    OP_N,    OP_N                       /*  ---    ---    ---    ---  */
  };

t_stat cpu_ffp (uint32 IR, uint32 intrq)
{
OP fpop;
OPS op, op2;
uint32 entry;
uint32 j, sa, sb, sc, da, dc, ra, MA;
int32 expon;
t_stat reason = SCPE_OK;

#if defined (HAVE_INT64)                                /* int64 support available */

int32 i;

#endif                                                  /* end of int64 support */

entry = IR & 037;                                       /* mask to entry point */

if (UNIT_CPU_MODEL != UNIT_1000_F) {                    /* 2100/M/E-Series? */
    if (op_ffp_e [entry] != OP_N) {
        reason = cpu_ops (op_ffp_e [entry], op, intrq); /* get instruction operands */

        if (reason != SCPE_OK)                          /* evaluation failed? */
            return reason;                              /* return reason for failure */
        }
    }

#if defined (HAVE_INT64)                                /* int64 support available */

else {                                                  /* F-Series */
    if (op_ffp_f [entry] != OP_N) {
        reason = cpu_ops (op_ffp_f [entry], op, intrq); /* get instruction operands */

        if (reason != SCPE_OK)                          /* evaluation failed? */
            return reason;                              /* return reason for failure */
        }

    switch (entry) {                                    /* decode IR<4:0> */

        case 000:                                       /* [tst] 105200 (OP_N) */
            XR = 4;                                     /* firmware revision */
            SR = 0102077;                               /* test passed code */
            AR = 0;                                     /* test clears A/B */
            BR = 0;
            PR = (PR + 1) & VAMASK;                     /* P+2 return for firmware w/DBI */
            return reason;

        case 003:                                       /* .DNG 105203 (OP_N) */
            return cpu_dbi (0105323, intrq);            /* remap to double int handler */

        case 004:                                       /* .DCO 105204 (OP_N) */
            return cpu_dbi (0105324, intrq);            /* remap to double int handler */

        case 007:                                       /* .BLE 105207 (OP_AAF) */
            O = fp_cvt (&op[2], fp_f, fp_t);            /* convert value and clear overflow */
            WriteOp (op[1].word, op[2], fp_t);          /* write double-precision value */
            return reason;

        case 010:                                       /* .DIN 105210 (OP_N) */
            return cpu_dbi (0105330, intrq);            /* remap to double int handler */

        case 011:                                       /* .DDE 105211 (OP_N) */
            return cpu_dbi (0105331, intrq);            /* remap to double int handler */

        case 012:                                       /* .DIS 105212 (OP_N) */
            return cpu_dbi (0105332, intrq);            /* remap to double int handler */

        case 013:                                       /* .DDS 105213 (OP_N) */
            return cpu_dbi (0105333, intrq);            /* remap to double int handler */

        case 014:                                       /* .NGL 105214 (OP_AT) */
            O = fp_cvt (&op[1], fp_t, fp_f);            /* convert value */
            AR = op[1].fpk[0];                          /* move MSB to A */
            BR = op[1].fpk[1];                          /* move LSB to B */
            return reason;

        case 032:                                       /* ..FCM 105232 (OP_R) */
            O = fp_pcom (&op[0], fp_f);                 /* complement value */
            AR = op[0].fpk[0];                          /* return result */
            BR = op[0].fpk[1];                          /* to A/B registers */
            return reason;

        case 033:                                       /* ..TCM 105233 (OP_A) */
            fpop = ReadOp (op[0].word, fp_t);           /* read 4-word value */
            O = fp_pcom (&fpop, fp_t);                  /* complement it */
            WriteOp (op[0].word, fpop, fp_t);           /* write 4-word value */
            return reason;
        }                                               /* fall thru if not special to F */
    }

#endif                                                  /* end of int64 support */

switch (entry) {                                        /* decode IR<4:0> */

/* FFP module 1 */

    case 000:                                           /* [nop] 105200 (OP_N) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 M/E-series */
            return stop_inst;                           /* trap if not */
        break;

#if defined (HAVE_INT64)                                /* int64 support available */

    case 001:                                           /* DBLE 105201 (OP_AAF) */
        O = fp_cvt (&op[2], fp_f, fp_x);                /* convert value and clear overflow */
        WriteOp (op[1].word, op[2], fp_x);              /* write extended-precision value */
        break;

    case 002:                                           /* SNGL 105202 (OP_AX) */
        O = fp_cvt (&op[1], fp_x, fp_f);                /* convert value */
        AR = op[1].fpk[0];                              /* move MSB to A */
        BR = op[1].fpk[1];                              /* move LSB to B */
        break;

    case 003:                                           /* .XMPY 105203 (OP_AXX) */
        i = 0;                                          /* params start at op[0] */
        goto XMPY;                                      /* process as XMPY */

    case 004:                                           /* .XDIV 105204 (OP_AXX) */
        i = 0;                                          /* params start at op[0] */
        goto XDIV;                                      /* process as XDIV */

#endif                                                  /* end of int64 support */

    case 005:                                           /* .DFER 105205 (OP_AA) */
        BR = op[0].word;                                /* get destination address */
        AR = op[1].word;                                /* get source address */
        goto XFER;                                      /* do transfer */

#if defined (HAVE_INT64)                                /* int64 support available */

    case 006:                                           /* .XPAK 105206 (OP_A) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 */
            return stop_inst;                           /* trap if not */

        if (intrq) {                                    /* interrupt pending? */
            PR = err_PC;                                /* restart instruction */
            break;
            }

        fpop = ReadOp (op[0].word, fp_x);               /* read unpacked */
        O = fp_nrpack (&fpop, fpop, (int16) AR, fp_x);  /* nrm/rnd/pack mantissa, exponent */
        WriteOp (op[0].word, fpop, fp_x);               /* write result */
        break;

    case 007:                                           /* XADD 105207 (OP_AAXX) */
        i = 1;                                          /* params start at op[1] */
    XADD:                                               /* enter here from .XADD */
        if (intrq) {                                    /* interrupt pending? */
            PR = err_PC;                                /* restart instruction */
            break;
            }

        O = fp_exec (001, &fpop, op[i + 1], op[i + 2]); /* three-word add */
        WriteOp (op[i].word, fpop, fp_x);               /* write sum */
        break;

    case 010:                                           /* XSUB 105210 (OP_AAXX) */
        i = 1;                                          /* params start at op[1] */
    XSUB:                                               /* enter here from .XSUB */
        if (intrq) {                                    /* interrupt pending? */
            PR = err_PC;                                /* restart instruction */
            break;
            }

        O = fp_exec (021, &fpop, op[i + 1], op[i + 2]); /* three-word subtract */
        WriteOp (op[i].word, fpop, fp_x);               /* write difference */
        break;

    case 011:                                           /* XMPY 105211 (OP_AAXX) */
        i = 1;                                          /* params start at op[1] */
    XMPY:                                               /* enter here from .XMPY */
        if (intrq) {                                    /* interrupt pending? */
            PR = err_PC;                                /* restart instruction */
            break;
            }

        O = fp_exec (041, &fpop, op[i + 1], op[i + 2]); /* three-word multiply */
        WriteOp (op[i].word, fpop, fp_x);               /* write product */
        break;

    case 012:                                           /* XDIV 105212 (OP_AAXX) */
        i = 1;                                          /* params start at op[1] */
     XDIV:                                              /* enter here from .XDIV */
        if (intrq) {                                    /* interrupt pending? */
            PR = err_PC;                                /* restart instruction */
            break;
            }

        O = fp_exec (061, &fpop, op[i + 1], op[i + 2]); /* three-word divide */
        WriteOp (op[i].word, fpop, fp_x);               /* write quotient */
        break;

    case 013:                                           /* .XADD 105213 (OP_AXX) */
        i = 0;                                          /* params start at op[0] */
        goto XADD;                                      /* process as XADD */

    case 014:                                           /* .XSUB 105214 (OP_AXX) */
        i = 0;                                          /* params start at op[0] */
        goto XSUB;                                      /* process as XSUB */

    case 015:                                           /* .XCOM 105215 (OP_A) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 */
            return stop_inst;                           /* trap if not */

        fpop = ReadOp (op[0].word, fp_x);               /* read unpacked */
        AR = fp_ucom (&fpop, fp_x);                     /* complement and rtn exp adj */
        WriteOp (op[0].word, fpop, fp_x);               /* write result */
        break;

    case 016:                                           /* ..DCM 105216 (OP_A) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 */
            return stop_inst;                           /* trap if not */

        if (intrq) {                                    /* interrupt pending? */
            PR = err_PC;                                /* restart instruction */
            break;
            }

        fpop = ReadOp (op[0].word, fp_x);               /* read operand */
        O = fp_pcom (&fpop, fp_x);                      /* complement (can't ovf neg) */
        WriteOp (op[0].word, fpop, fp_x);               /* write result */
        break;

    case 017:                                           /* DDINT 105217 (OP_AAX) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 */
            return stop_inst;                           /* trap if not */

        if (intrq) {                                    /* interrupt pending? */
            PR = err_PC;                                /* restart instruction */
            break;
            }

        O = fp_trun (&fpop, op[2], fp_x);               /* truncate operand (can't ovf) */
        WriteOp (op[1].word, fpop, fp_x);               /* write result */
        break;

#endif                                                  /* end of int64 support */

/* FFP module 2 */

    case 020:                                           /* .XFER 105220 (OP_N) */
        if (UNIT_CPU_TYPE == UNIT_TYPE_2100)
            PR = (PR + 1) & VAMASK;                     /* 2100 .XFER returns to P+2 */
    XFER:                                               /* enter here from .DFER */
        sc = 3;                                         /* set count for 3-wd xfer */
        goto CFER;                                      /* do transfer */

    case 021:                                           /* .GOTO 105221 (OP_AK) */
        if ((int16) op[1].word < 1)                     /* index < 1? */
            op[1].word = 1;                             /* reset min */

        sa = PR + op[1].word - 1;                       /* point to jump target */
        if (sa >= op[0].word)                           /* must be <= last target */
            sa = op[0].word - 1;

        da = ReadW (sa);                                /* get jump target */
        reason = resolve (da, &MA, intrq);              /* resolve indirects */
        if (reason != SCPE_OK) {                        /* resolution failed? */
            PR = err_PC;                                /* irq restarts instruction */
            break;
            }

        mp_dms_jmp (MA, 2);                             /* validate jump addr */
        PCQ_ENTRY;                                      /* record last P */
        PR = MA;                                        /* jump */
        BR = op[0].word;                                /* (for 2100 FFP compat) */
        break;

    case 022:                                           /* ..MAP 105222 (OP_KKKK) */
        op[1].word = op[1].word - 1;                    /* decrement 1st subscr */

        if ((AR & 1) == 0)                              /* 2-dim access? */
            op[1].word = op[1].word +                   /* compute element offset */
                         (op[2].word - 1) * op[3].word;
        else {                                          /* 3-dim access */
            reason = cpu_ops (OP_KK, op2, intrq);       /* get 1st, 2nd ranges */
            if (reason != SCPE_OK) {                    /* evaluation failed? */
                PR = err_PC;                            /* irq restarts instruction */
                break;
                }
            op[1].word = op[1].word +                   /* offset */
                         ((op[3].word - 1) * op2[1].word +
                          op[2].word - 1) * op2[0].word;
            }

        AR = (op[0].word + op[1].word * BR) & DMASK;    /* return element address */
        break;

    case 023:                                           /* .ENTR 105223 (OP_A) */
        MA = PR - 3;                                    /* get addr of entry point */
    ENTR:                                               /* enter here from .ENTP */
        da = op[0].word;                                /* get addr of 1st formal */
        dc = MA - da;                                   /* get count of formals */
        sa = ReadW (MA);                                /* get addr of return point */
        ra = ReadW (sa++);                              /* get rtn, ptr to 1st actual */
        WriteW (MA, ra);                                /* stuff rtn into caller's ent */
        sc = ra - sa;                                   /* get count of actuals */
        if (sc > dc)                                    /* use min (actuals, formals) */
            sc = dc;

        for (j = 0; j < sc; j++) {
            MA = ReadW (sa++);                          /* get addr of actual */
            reason = resolve (MA, &MA, intrq);          /* resolve indirect */
            if (reason != SCPE_OK) {                    /* resolution failed? */
                PR = err_PC;                            /* irq restarts instruction */
                break;
                }
            WriteW (da++, MA);                          /* put addr into formal */
            }

        AR = (uint16) ra;                               /* return address */
        BR = (uint16) da;                               /* addr of 1st unused formal */
        break;

    case 024:                                           /* .ENTP 105224 (OP_A) */
        MA = PR - 5;                                    /* get addr of entry point */
        goto ENTR;

    case 025:                                           /* .PWR2 105225 (OP_RK) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 */
            return stop_inst;                           /* trap if not */

        fp_unpack (&fpop, &expon, op[0], fp_f);         /* unpack value */
        expon = expon + (int16) (op[1].word);           /* multiply by 2**n */
        fp_pack (&fpop, fpop, expon, fp_f);             /* repack value */
        AR = fpop.fpk[0];                               /* return result */
        BR = fpop.fpk[1];                               /* to A/B registers */
        break;

    case 026:                                           /* .FLUN 105226 (OP_R) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 */
            return stop_inst;                           /* trap if not */

        fp_unpack (&fpop, &expon, op[0], fp_f);         /* unpack value */
        AR = (int16) expon;                             /* return expon to A */
        BR = fpop.fpk[1];                               /* and low mant to B */
        break;

    case 027:                                           /* $SETP 105227 (OP_K) */
        j = sa = AR;                                    /* save initial value */
        sb = BR;                                        /* save initial address */
        AR = 0;                                         /* AR will return = 0 */
        BR = BR & VAMASK;                               /* addr must be direct */

        do {
            WriteW (BR, j);                             /* write value to address */
            j = (j + 1) & DMASK;                        /* incr value */
            BR = (BR + 1) & VAMASK;                     /* incr address */
            op[0].word = op[0].word - 1;                /* decr count */
            if (op[0].word && intrq) {                  /* more and intr? */
                AR = (uint16) sa;                       /* restore A */
                BR = (uint16) sb;                       /* restore B */
                PR = err_PC;                            /* restart instruction */
                break;
                }
            }
        while (op[0].word != 0);                        /* loop until count exhausted */
        break;

    case 030:                                           /* .PACK 105230 (OP_RC) */
        if (UNIT_CPU_TYPE != UNIT_TYPE_1000)            /* must be 1000 */
            return stop_inst;                           /* trap if not */

        O = fp_nrpack (&fpop, op[0],                    /* nrm/rnd/pack value */
                       (int16) (op[1].word), fp_f);
        AR = fpop.fpk[0];                               /* return result */
        BR = fpop.fpk[1];                               /* to A/B registers */
        break;

    case 031:                                           /* .CFER 105231 (OP_AA) */
        if ((UNIT_CPU_MODEL != UNIT_1000_E) &&          /* must be 1000 E-series */
            (UNIT_CPU_MODEL != UNIT_1000_F))            /* or 1000 F-series */
            return stop_inst;                           /* trap if not */

        BR = op[0].word;                                /* get destination address */
        AR = op[1].word;                                /* get source address */
        sc = 4;                                         /* set for 4-wd xfer */
    CFER:                                               /* enter here from .XFER */
        for (j = 0; j < sc; j++) {                      /* xfer loop */
            WriteW (BR, ReadW (AR));                    /* transfer word */
            AR = (AR + 1) & VAMASK;                     /* bump source addr */
            BR = (BR + 1) & VAMASK;                     /* bump destination addr */
            }

        E = 0;                                          /* routine clears E */

        if (UNIT_CPU_TYPE == UNIT_TYPE_2100) {          /* 2100 (and .DFER/.XFER)? */
            AR = (AR + 1) & VAMASK;                     /* 2100 FFP returns X+4, Y+4 */
            BR = (BR + 1) & VAMASK;
            }
        break;

    default:                                            /* others undefined */
        reason = stop_inst;
        }

return reason;
}


/* Double-Integer Instructions.

   The double-integer instructions were added to the HP instruction set at
   revision 1920 of the 1000-F.  They were immediately adopted in a number of HP
   software products, most notably the RTE file management package (FMP)
   routines.  As these routines are used in nearly every RTE program, F-Series
   programs were almost always a few hundred bytes smaller than their M- and
   E-Series counterparts.  This became significant as RTE continued to grow in
   size, and some customer programs ran out of address space on E-Series
   machines.

   While HP never added double-integer instructions to the standard E-Series, a
   product from the HP "specials group," HP 93585A, provided microcoded
   replacements for the E-Series.  This could provide just enough address-space
   savings to allow programs to load in E-Series systems, in addition to
   accelerating these common operations.

   There was no equivalent M-Series microcode, due to the limited micromachine
   address space on that system.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    93585A   std

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E   1000-F   Description
     ------  ------   ------  -----------------------------------------
     [test]  105320     --    [self test]
     .DAD    105321   105014  Double integer add
     .DMP    105322   105054  Double integer multiply
     .DNG    105323   105203  Double integer negate
     .DCO    105324   105204  Double integer compare
     .DDI    105325   105074  Double integer divide
     .DDIR   105326   105134  Double integer divide (reversed)
     .DSB    105327   105034  Double integer subtract
     .DIN    105330   105210  Double integer increment
     .DDE    105331   105211  Double integer decrement
     .DIS    105332   105212  Double integer increment and skip if zero
     .DDS    105333   105213  Double integer decrement and skip if zero
     .DSBR   105334   105114  Double integer subtraction (reversed)

   On the F-Series, the double-integer instruction codes are split among the
   floating-point processor and the Fast FORTRAN Processor ranges.  They are
   dispatched from those respective simulators for processing here.

   Implementation notes:

    1. Opcodes 105335-105337 are NOPs in the microcode.  They generate
       unimplemented instructions stops under simulation.

    2. This is an implementation of Revision 2 of the microcode, which was
       released as ROM part numbers 93585-80003, 93585-80005, and 93585-80001
       (Revision 1 substituted -80002 for -80005).

    3. The F-Series firmware executes .DMP and .DDI/.DDIR by floating the 32-bit
       double integer to a 48-bit extended-precision number, calling the FPP to
       execute the extended-precision multiply/divide, and then fixing the
       product to a 32-bit double integer.  We simulate these directly with 64-
       or 32-bit integer arithmetic.

   Additional references:
    - 93585A Microcode Source (93585-18002 Rev. 2005)
    - 93585A Double Integer Instructions Installation and Reference Manual
             (93585-90007)
*/

static const OP_PAT op_dbi[16] = {
  OP_N,    OP_JD,   OP_JD,   OP_J,                      /* [test] .DAD   .DMP   .DNG  */
  OP_JD,   OP_JD,   OP_JD,   OP_JD,                     /* .DCO   .DDI   .DDIR  .DSB  */
  OP_J,    OP_J,    OP_A,    OP_A,                      /* .DIN   .DDE   .DIS   .DDS  */
  OP_JD,   OP_N,    OP_N,    OP_N                       /* .DSBR   ---    ---    ---  */
  };

t_stat cpu_dbi (uint32 IR, uint32 intrq)
{
OP din;
OPS op;
uint32 entry, t;
t_stat reason = SCPE_OK;

entry = IR & 017;                                       /* mask to entry point */

if (op_dbi[entry] != OP_N) {
    reason = cpu_ops (op_dbi [entry], op, intrq);       /* get instruction operands */
    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

    case 000:                                           /* [test] 105320 (OP_N) */
        XR = 2;                                         /* set revision */
        BR = 0377;                                      /* side effect of microcode */
        SR = 0102077;                                   /* set "pass" code */
        PR = (PR + 1) & VAMASK;                         /* return to P+1 */
        t = (AR << 16) | BR;                            /* set t for return */
        break;

    case 001:                                           /* .DAD 105321 (OP_JD) */
        t = op[0].dword + op[1].dword;                  /* add values */
        E = E | (t < op[0].dword);                      /* carry if result smaller */
        O = (((~op[0].dword ^ op[1].dword) &            /* overflow if sign wrong */
              (op[0].dword ^ t) & SIGN32) != 0);
        break;

    case 002:                                           /* .DMP 105322 (OP_JD) */
        {

#if defined (HAVE_INT64)                                /* int64 support available */

            t_int64 t64;

            t64 = (t_int64) INT32 (op[0].dword) *       /* multiply signed values */
                  (t_int64) INT32 (op[1].dword);
            O = ((t64 < -(t_int64) 0x80000000) ||       /* overflow if out of range */
                 (t64 >  (t_int64) 0x7FFFFFFF));
            if (O)
                t = ~SIGN32;                            /* if overflow, rtn max pos */
            else
                t = (uint32) (t64 & DMASK32);           /* else lower 32 bits of result */

#else                                                   /* int64 support unavailable */

            uint32 sign, xu, yu, rh, rl;

            sign = ((int32) op[0].dword < 0) ^          /* save sign of result */
                   ((int32) op[1].dword < 0);

            xu = (uint32) abs ((int32) op[0].dword);    /* make operands pos */
            yu = (uint32) abs ((int32) op[1].dword);

            if ((xu & 0xFFFF0000) == 0 &&               /* 16 x 16 multiply? */
                (yu & 0xFFFF0000) == 0) {
                t = xu * yu;                            /* do it */
                O = 0;                                  /* can't overflow */
                }

            else if ((xu & 0xFFFF0000) != 0 &&          /* 32 x 32 multiply? */
                     (yu & 0xFFFF0000) != 0)
                O = 1;                                  /* always overflows */

            else {                                      /* 16 x 32 or 32 x 16 */
                rl = (xu & 0xFFFF) * (yu & 0xFFFF);     /* form 1st partial product */

                if ((xu & 0xFFFF0000) == 0)
                    rh = xu * (yu >> 16) + (rl >> 16);  /* 16 x 32 2nd partial */
                else
                    rh = (xu >> 16) * yu + (rl >> 16);  /* 32 x 16 2nd partial */

                O = (rh > 0x7FFF + sign);               /* check for out of range */
                if (O == 0)
                    t = (rh << 16) | (rl & 0xFFFF);     /* combine partials */
                }

            if (O)                                      /* if overflow occurred */
                t = ~SIGN32;                            /*   then return the largest positive number */
            else if (sign)                              /* otherwise if the result is negative */
                t = ~t + 1;                             /*   then return the twos complement (set if O = 0 above) */

#endif                                                  /* end of int64 support */

        }
        break;

    case 003:                                           /* .DNG 105323 (OP_J) */
        t = ~op[0].dword + 1;                           /* negate value */
        O = (op[0].dword == SIGN32);                    /* overflow if max neg */
        if (op[0].dword == 0)                           /* borrow if result zero */
            E = 1;
        break;

    case 004:                                           /* .DCO 105324 (OP_JD) */
        t = op[0].dword;                                /* copy for later store */
        if ((int32) op[0].dword < (int32) op[1].dword)
            PR = (PR + 1) & VAMASK;                     /* < rtns to P+2 */
        else if ((int32) op[0].dword > (int32) op[1].dword)
            PR = (PR + 2) & VAMASK;                     /* > rtns to P+3 */
        break;                                          /* = rtns to P+1 */

    case 005:                                           /* .DDI 105325 (OP_JD) */
    DDI:
        O = ((op[1].dword == 0) ||                      /* overflow if div 0 */
             ((op[0].dword == SIGN32) &&                /*   or max neg div -1 */
              ((int32) op[1].dword == -1)));
        if (O)
            t = ~SIGN32;                                /* rtn max pos for ovf */
        else
            t = (uint32) (INT32 (op[0].dword) /         /* else return quotient */
                          INT32 (op[1].dword));
        break;

    case 006:                                           /* .DDIR 105326 (OP_JD) */
        t = op[0].dword;                                /* swap operands */
        op[0].dword = op[1].dword;
        op[1].dword = t;
        goto DDI;                                       /* continue at .DDI */

    case 007:                                           /* .DSB 105327 (OP_JD) */
    DSB:
        t = op[0].dword - op[1].dword;                  /* subtract values */
        E = E | (op[0].dword < op[1].dword);            /* borrow if minu < subtr */
        O = (((op[0].dword ^ op[1].dword) &             /* overflow if sign wrong */
              (op[0].dword ^ t) & SIGN32) != 0);
        break;

    case 010:                                           /* .DIN 105330 (OP_J) */
        t = op[0].dword + 1;                            /* increment value */
        O = (t == SIGN32);                              /* overflow if sign flipped */
        if (t == 0)
            E = 1;                                      /* carry if result zero */
        break;

    case 011:                                           /* .DDE 105331 (OP_J) */
        t = op[0].dword - 1;                            /* decrement value */
        O = (t == ~SIGN32);                             /* overflow if sign flipped */
        if ((int32) t == -1)
            E = 1;                                      /* borrow if result -1 */
        break;

    case 012:                                           /* .DIS 105332 (OP_A) */
        din = ReadOp (op[0].word, in_d);                /* get value */
        t = din.dword = din.dword + 1;                  /* increment value */
        WriteOp (op[0].word, din, in_d);                /* store it back */
        if (t == 0)
            PR = (PR + 1) & VAMASK;                     /* skip if result zero */
        break;

    case 013:                                           /* .DDS 105333 (OP_A) */
        din = ReadOp (op[0].word, in_d);                /* get value */
        t = din.dword = din.dword - 1;                  /* decrement value */
        WriteOp (op[0].word, din, in_d);                /* write it back */
        if (t == 0)
            PR = (PR + 1) & VAMASK;                     /* skip if result zero */
        break;

    case 014:                                           /* .DSBR 105334 (OP_JD) */
        t = op[0].dword;                                /* swap operands */
        op[0].dword = op[1].dword;
        op[1].dword = t;
        goto DSB;                                       /* continue at .DSB */

    default:                                            /* others undefined */
        t = (AR << 16) | BR;                            /* set t for NOP */
        reason = stop_inst;
        }

if (reason == SCPE_OK) {                                /* if return OK */
    AR = (t >> 16) & DMASK;                             /*   break result */
    BR = t & DMASK;                                     /*   into A and B */
    }

return reason;
}
