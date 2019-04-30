/* vax_cmode.c: VAX compatibility mode

   Copyright (c) 2004-2016, Robert M Supnik

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

   On a full VAX, this module implements PDP-11 compatibility mode.
   On a subset VAX, this module forces a fault if REI attempts to set PSL<cm>.

   14-Jul-16    RMS     Updated PSL check (found by EVKAE 6.2)
   28-May-08    RMS     Inlined physical memory routines
   25-Jan-08    RMS     Fixed declaration (Mark Pizzolato)
   03-May-06    RMS     Fixed omission of SXT
                        Fixed order of operand fetching in XOR
   24-Aug-04    RMS     Cloned from PDP-11 CPU

   In compatibility mode, the Istream prefetch mechanism is not used.  The
   prefetcher will be explicitly resynchronized through intexc on any exit
   from compatibility mode.
*/

#include "vax_defs.h"

#if defined (CMPM_VAX)

#define RdMemB(a)       Read (a, L_BYTE, RA)
#define RdMemMB(a)      Read (a, L_BYTE, WA)
#define WrMemB(d,a)     Write (a, d, L_BYTE, WA)
#define BRANCH_F(x)     CMODE_JUMP ((PC + (((x) + (x)) & BMASK)) & WMASK)
#define BRANCH_B(x)     CMODE_JUMP ((PC + (((x) + (x)) | 0177400)) & WMASK)
#define CC_XOR_NV(x)    ((((x) & CC_N) != 0) ^ (((x) & CC_V) != 0))
#define CC_XOR_NC(x)    ((((x) & CC_N) != 0) ^ (((x) & CC_C) != 0))

int32 GeteaB (int32 spec);
int32 GeteaW (int32 spec);
int32 RdMemW (int32 a);
int32 RdMemMW (int32 a);
void WrMemW (int32 d, int32 a);
int32 RdRegB (int32 rn);
int32 RdRegW (int32 rn);
void WrRegB (int32 val, int32 rn);
void WrRegW (int32 val, int32 rn);

/* Validate PSL for compatibility mode */

t_bool BadCmPSL (int32 newpsl)
{
if ((newpsl & (PSL_FPD|PSL_IS|PSL_CUR|PSL_PRV|PSL_IPL|PSW_DV|PSW_FU|PSW_IV)) !=
    ((USER << PSL_V_CUR) | (USER << PSL_V_PRV)))
    return TRUE;
else return FALSE;
}

/* Compatibility mode execution */

int32 op_cmode (int32 cc)
{
int32 IR, srcspec, dstspec, srcreg, dstreg, ea;
int32 i, t, src, src2, dst, sign, oc;
int32 acc = ACC_MASK (USER);

PC = PC & WMASK;                                        /* PC must be 16b */
if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {  /* breakpoint? */
    ABORT (STOP_IBKPT);                                 /* stop simulation */
    }
sim_interval = sim_interval - 1;                        /* count instr */

IR = RdMemW (PC);                                       /* fetch instruction */
PC = (PC + 2) & WMASK;                                  /* incr PC, mod 65k */
srcspec = (IR >> 6) & 077;                              /* src, dst specs */
dstspec = IR & 077;
srcreg = (srcspec <= 07);                               /* src, dst = rmode? */
dstreg = (dstspec <= 07);
switch ((IR >> 12) & 017) {                             /* decode IR<15:12> */

/* Opcode 0: no operands, specials, branches, JSR, SOPs */

    case 000:                                           /* 00xxxx */
        switch ((IR >> 6) & 077) {                      /* decode IR<11:6> */
        case 000:                                       /* 0000xx */
            switch (IR) {                               /* decode IR<5:0> */
            case 3:                                     /* BPT */
                CMODE_FAULT (CMODE_BPT);
                break;

            case 4:                                     /* IOT */
                CMODE_FAULT (CMODE_IOT);
                break;

            case 2:                                     /* RTI */
            case 6:                                     /* RTT */
                src = RdMemW (R[6] & WMASK);            /* new PC */
                src2 = RdMemW ((R[6] + 2) & WMASK);     /* new PSW */
                R[6] = (R[6] + 4) & WMASK;
                cc = src2 & CC_MASK;                    /* update cc, T */
                if (src2 & PSW_T)
                    PSL = PSL | PSW_T;
                else PSL = PSL & ~PSW_T;
                CMODE_JUMP (src);                       /* update PC */
                break;

            default:                                    /* undefined */
                CMODE_FAULT (CMODE_RSVI);
                break;
                }                                       /* end switch IR */
            break;                                      /* end case 0000xx */

        case 001:                                       /* JMP */
            if (dstreg)                                 /* mode 0 illegal */
                CMODE_FAULT (CMODE_ILLI);
            else {
                CMODE_JUMP (GeteaW (dstspec));
                }
            break;

        case 002:                                       /* 0002xx */
            if (IR < 000210) {                          /* RTS */
                dstspec = dstspec & 07;
                if (dstspec != 7) {                     /* PC <- r */
                   CMODE_JUMP (RdRegW (dstspec));
                   }
                dst = RdMemW (R[6]);                    /* t <- (sp)+ */
                R[6] = (R[6] + 2) & WMASK;
                WrRegW (dst, dstspec);                  /* r <- t */
                break;                                  /* end if RTS */
                }
            if (IR < 000240) {                          /* [210:237] */
                CMODE_FAULT (CMODE_RSVI);
                break;
                }
            if (IR < 000260)                            /* clear CC */
                cc = cc & ~(IR & CC_MASK);
            else cc = cc | (IR & CC_MASK);              /* set CC */
            break;

        case 003:                                       /* SWAB */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = ((src & BMASK) << 8) | ((src >> 8) & BMASK);
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_B ((dst & BMASK));
            break;

        case 004: case 005:                             /* BR */
            BRANCH_F (IR);
            break;

        case 006: case 007:                             /* BR */
            BRANCH_B (IR);
            break;

        case 010: case 011:                             /* BNE */
            if ((cc & CC_Z) == 0) {
                BRANCH_F (IR);
                } 
            break;

        case 012: case 013:                             /* BNE */
            if ((cc & CC_Z) == 0) {
                BRANCH_B (IR);
                }
            break;

        case 014: case 015:                             /* BEQ */
            if (cc & CC_Z) {
                BRANCH_F (IR);
                } 
            break;

        case 016: case 017:                             /* BEQ */
            if (cc & CC_Z) {
                BRANCH_B (IR);
                }
            break;

        case 020: case 021:                             /* BGE */
            if (CC_XOR_NV (cc) == 0) {
                BRANCH_F (IR);
                } 
            break;

        case 022: case 023:                             /* BGE */
            if (CC_XOR_NV (cc) == 0) {
                BRANCH_B (IR);
                }
            break;

        case 024: case 025:                             /* BLT */
            if (CC_XOR_NV (cc)) {
                BRANCH_F (IR);
                }
            break;

        case 026: case 027:                             /* BLT */
            if (CC_XOR_NV (cc)) {
                BRANCH_B (IR);
                }
            break;

        case 030: case 031:                             /* BGT */
            if (((cc & CC_Z) || CC_XOR_NV (cc)) == 0) {
                BRANCH_F (IR);
                } 
            break;

        case 032: case 033:                             /* BGT */
            if (((cc & CC_Z) || CC_XOR_NV (cc)) == 0) {
                BRANCH_B (IR);
                }
            break;

        case 034: case 035:                             /* BLE */
            if ((cc & CC_Z) || CC_XOR_NV (cc)) {
                BRANCH_F (IR);
                } 
            break;

        case 036: case 037:                             /* BLE */
            if ((cc & CC_Z) || CC_XOR_NV (cc)) {
                BRANCH_B (IR);
                }
            break;

        case 040: case 041: case 042: case 043:         /* JSR */
        case 044: case 045: case 046: case 047:
            if (dstreg) {                               /* mode 0 illegal */
                CMODE_FAULT (CMODE_ILLI);
                }
            else {
                srcspec = srcspec & 07;                 /* get reg num */
                dst = GeteaW (dstspec);                 /* get dst addr */
                src = RdRegW (srcspec);                 /* get src reg */
                WrMemW (src, (R[6] - 2) & WMASK);       /* -(sp) <- r */
                R[6] = (R[6] - 2) & WMASK;
                if (srcspec != 7)                       /* r <- PC */
                    WrRegW (PC, srcspec);
                CMODE_JUMP (dst);                       /* PC <- dst */
                }
            break;                                      /* end JSR */

        case 050:                                       /* CLR */
            if (dstreg)
                WrRegW (0, dstspec);
            else WrMemW (0, GeteaW (dstspec));
            cc = CC_Z;
            break;

        case 051:                                       /* COM */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = src ^ WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            cc = cc | CC_C;
            break;

        case 052:                                       /* INC */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (src + 1) & WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZP_W (dst);
            if (dst == 0100000)
                cc = cc | CC_V;
            break;

        case 053:                                       /* DEC */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (src - 1) & WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZP_W (dst);
            if (dst == 077777)
                cc = cc | CC_V;
            break;

        case 054:                                       /* NEG */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (-src) & WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            if (dst == 0100000)
                cc = cc | CC_V;
            if (dst)
                cc = cc | CC_C;
            break;

        case 055:                                       /* ADC */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (src + (cc & CC_C)) & WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            if ((src == 077777) && (dst == 0100000))
                cc = cc | CC_V;
            if ((src == 0177777) && (dst == 0))
                cc = cc | CC_C;
            break;

        case 056:                                       /* SBC */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (src - (cc & CC_C)) & WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            if ((src == 0100000) && (dst == 077777))
                cc = cc | CC_V;
            if ((src == 0) && (dst == 0177777))
                cc = cc | CC_C;
            break;

        case 057:                                       /* TST */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemW (GeteaW (dstspec));
            CC_IIZZ_W (src);
            break;

        case 060:                                       /* ROR */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (src >> 1) | ((cc & CC_C)? WSIGN: 0);
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            if (src & 1)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 061:                                       /* ROL */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = ((src << 1) | ((cc & CC_C)? 1: 0)) & WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            if (src & WSIGN)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 062:                                       /* ASR */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (src & WSIGN) | (src >> 1);
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            if (src & 1)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 063:                                       /* ASL */
            if (dstreg)
                src = RdRegW (dstspec);
            else src = RdMemMW (ea = GeteaW (dstspec));
            dst = (src << 1) & WMASK;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZZ_W (dst);
            if (src & WSIGN)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 065:                                       /* MFPI */
            if (dstreg)                                 /* "mov dst,-(sp)" */
                dst = RdRegW (dstspec);
            else dst = RdMemW (GeteaW (dstspec));
            WrMemW (dst, (R[6] - 2) & WMASK);
            R[6] = (R[6] - 2) & WMASK;
            CC_IIZP_W (dst);
            break;

        case 066:                                       /* MTPI */
            dst = RdMemW (R[6] & WMASK);                /* "mov (sp)+,dst" */
            R[6] = (R[6] + 2) & WMASK;
            recq[recqptr++] = RQ_REC (AIN|RW, 6);
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, (GeteaW (dstspec) & WMASK));
            CC_IIZP_W (dst);
            break;

        case 067:                                       /* SXT */
            dst = (cc & CC_N)? 0177777: 0;
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, GeteaW (dstspec));
            CC_IIZP_W (dst);
            break;

        default:                                        /* undefined */
            CMODE_FAULT (CMODE_RSVI);
            break;
            }                                           /* end switch SOPs */
        break;                                          /* end case 000 */

/* Opcodes 01 - 06: double operand word instructions

   Compatibility mode requires source address decode, source fetch,
   dest address decode, dest fetch/store.

   Add: v = [sign (src) = sign (src2)] and [sign (src) != sign (result)]
   Cmp: v = [sign (src) != sign (src2)] and [sign (src2) = sign (result)]
*/

    case 001:                                           /* MOV */
        if (srcreg)
            src = RdRegW (srcspec);
        else src = RdMemW (GeteaW (srcspec));
        if (dstreg)
            WrRegW (src, dstspec);
        else WrMemW (src, GeteaW (dstspec));
        CC_IIZP_W (src);
        break;

    case 002:                                           /* CMP */
        if (srcreg)
            src = RdRegW (srcspec);
        else src = RdMemW (GeteaW (srcspec));
        if (dstreg)
            src2 = RdRegW (dstspec);
        else src2 = RdMemW (GeteaW (dstspec));
        dst = (src - src2) & WMASK;
        CC_IIZZ_W (dst);
        if (((src ^ src2) & (~src2 ^ dst)) & WSIGN)
            cc = cc | CC_V;
        if (src < src2)
            cc = cc | CC_C;
        break;

    case 003:                                           /* BIT */
        if (srcreg)
            src = RdRegW (srcspec);
        else src = RdMemW (GeteaW (srcspec));
        if (dstreg)
            src2 = RdRegW (dstspec);
        else src2 = RdMemW (GeteaW (dstspec));
        dst = src2 & src;
        CC_IIZP_W (dst);
        break;

    case 004:                                           /* BIC */
        if (srcreg)
            src = RdRegW (srcspec);
        else src = RdMemW (GeteaW (srcspec));
        if (dstreg)
            src2 = RdRegW (dstspec);
        else src2 = RdMemMW (ea = GeteaW (dstspec));
        dst = src2 & ~src;
        if (dstreg)
            WrRegW (dst, dstspec);
        else WrMemW (dst, ea);
        CC_IIZP_W (dst);
        break;

    case 005:                                           /* BIS */
        if (srcreg)
            src = RdRegW (srcspec);
        else src = RdMemW (GeteaW (srcspec));
        if (dstreg)
            src2 = RdRegW (dstspec);
        else src2 = RdMemMW (ea = GeteaW (dstspec));
        dst = src2 | src;
        if (dstreg)
            WrRegW (dst, dstspec);
        else WrMemW (dst, ea);
        CC_IIZP_W (dst);
        break;

    case 006:                                           /* ADD */
        if (srcreg)
            src = RdRegW (srcspec);
        else src = RdMemW (GeteaW (srcspec));
        if (dstreg)
            src2 = RdRegW (dstspec);
        else src2 = RdMemMW (ea = GeteaW (dstspec));
        dst = (src2 + src) & WMASK;
        if (dstreg)
            WrRegW (dst, dstspec);
        else WrMemW (dst, ea);
        CC_ADD_W (dst, src, src2);
        break;

/* Opcode 07: EIS, FIS (not implemented), CIS

   Notes:
   - MUL carry: C is set if the (signed) result doesn't fit in 16 bits.
   - Divide has three error cases:
        1. Divide by zero.
        2. Divide largest negative number by -1.
        3. (Signed) quotient doesn't fit in 16 bits.
     Cases 1 and 2 must be tested in advance, to avoid C runtime errors.
   - ASHx left: overflow if the bits shifted out do not equal the sign
     of the result (convert shift out to 1/0, xor against sign).
   - ASHx right: if right shift sign extends, then the shift and
     conditional or of shifted -1 is redundant.  If right shift zero
     extends, then the shift and conditional or does sign extension.
*/

    case 007:                                           /* EIS */
        srcspec = srcspec & 07;                         /* get src reg */
        switch ((IR >> 9) & 07)  {                      /* decode IR<11:9> */

        case 0:                                         /* MUL */
            if (dstreg)                                 /* get src2 */
                src2 = RdRegW (dstspec);
            else src2 = RdMemW (GeteaW (dstspec));
            src = RdRegW (srcspec);                     /* get src */
            if (src2 & WSIGN)                           /* sext src, src2 */
                src2 = src2 | ~WMASK;
            if (src & WSIGN)
                src = src | ~WMASK;
            dst = src * src2;                           /* multiply */
            WrRegW ((dst >> 16) & WMASK, srcspec);      /* high 16b */
            WrRegW (dst & WMASK, srcspec | 1);          /* low 16b */
            CC_IIZZ_L (dst & LMASK);
            if ((dst > 077777) || (dst < -0100000))
                cc = cc | CC_C;
            break;

        case 1:                                         /* DIV */
            if (dstreg)                                 /* get src2 */
                src2 = RdRegW (dstspec);
            else src2 = RdMemW (GeteaW (dstspec));
            t = RdRegW (srcspec);
            src = (((uint32) t) << 16) | RdRegW (srcspec | 1);
            if (src2 == 0) {                            /* div by 0? */
                cc = CC_V | CC_C;                       /* set cc's */
                break;                                  /* done */
                }
            if (((uint32)src == LSIGN) && ((uint32)src2 == WMASK)) {    /* -2^31 / -1? */
                cc = CC_V;                              /* overflow */
                break;                                  /* done */
                }
            if (src2 & WSIGN)                           /* sext src, src2 */
                src2 = src2 | ~WMASK;
            if (t & WSIGN)
                src = src | ~LMASK;
            dst = src / src2;                           /* divide */
            if ((dst > 077777) || (dst < -0100000)) {   /* out of range? */
                cc = CC_V;                              /* overflow */
                break;
                }
            CC_IIZZ_W (dst & WMASK);                    /* set cc's */
            WrRegW (dst & WMASK, srcspec);              /* quotient */
            WrRegW ((src - (src2 * dst)) & WMASK, srcspec | 1);
            break;

        case 2:                                         /* ASH */
            if (dstreg)                                 /* get src2 */
                src2 = RdRegW (dstspec);
            else src2 = RdMemW (GeteaW (dstspec));
            src2 = src2 & 077;
            src = RdRegW (srcspec);                     /* get src */
            if ((sign = ((src & WSIGN)? 1: 0)))
                src = src | ~WMASK;
            if (src2 == 0) {                            /* [0] */
                dst = src;                              /* result */
                oc = 0;                                 /* last bit out */
                }
            else if (src2 <= 15) {                      /* [1,15] */
                dst = src << src2;
                i = (src >> (16 - src2)) & WMASK;
                oc = (i & 1)? CC_C: 0;
                if ((dst & WSIGN)? (i != WMASK): (i != 0))
                    oc = oc | CC_V;
                }
            else if (src2 <= 31) {                      /* [16,31] */
                dst = 0;
                oc = ((src << (src2 - 16)) & 1)? CC_C: 0;
                if (src)
                    oc = oc | CC_V;
                }
            else if (src2 == 32) {                      /* [32] = -32 */
                dst = -sign;
                oc = sign? CC_C: 0;
                }
            else {                                      /* [33,63] = -31,-1 */
                dst = (src >> (64 - src2)) | (-sign << (src2 - 32));
                oc = ((src >> (63 - src2)) & 1)? CC_C: 0;
                }
            WrRegW (dst = dst & WMASK, srcspec);        /* result */
            CC_IIZZ_W (dst);
            cc = cc | oc;
            break;

        case 3:                                         /* ASHC */
            if (dstreg)                                 /* get src2 */
                src2 = RdRegW (dstspec);
            else src2 = RdMemW (GeteaW (dstspec));
            src2 = src2 & 077;
            t = RdRegW (srcspec);
            src = (((uint32) t) << 16) | RdRegW (srcspec | 1);
            sign = (t & WSIGN)? 1: 0;                   /* get src sign */
            if (src2 == 0) {                            /* [0] */
                dst = src;                              /* result */
                oc = 0;                                 /* last bit out */
                }
            else if (src2 <= 31) {                      /* [1,31] */
                dst = ((uint32) src) << src2;
                i = ((src >> (32 - src2)) | (-sign << src2)) & LMASK;
                oc = (i & 1)? CC_C: 0;
                if ((dst & LSIGN)? ((uint32)i != LMASK): (i != 0))
                    oc = oc | CC_V;
                }
            else if (src2 == 32) {                      /* [32] = -32 */
                dst = -sign;
                oc = sign? CC_C: 0;
                }
            else {                                      /* [33,63] = -31,-1 */
                dst = (src >> (64 - src2)) | (-sign << (src2 - 32));
                oc = ((src >> (63 - src2)) & 1)? CC_C: 0;
                }
            WrRegW ((dst >> 16) & WMASK, srcspec);      /* high result */
            WrRegW (dst & WMASK, srcspec | 1);          /* low result */
            CC_IIZZ_L (dst & LMASK);
            cc = cc | oc;
            break;

        case 4:                                         /* XOR */
            src = RdRegW (srcspec);                     /* get src */
            if (dstreg)                                 /* get dst */
                src2 = RdRegW (dstspec);
            else src2 = RdMemMW (ea = GeteaW (dstspec));
            dst = src2 ^ src;
            if (dstreg)                                 /* result */
                WrRegW (dst, dstspec);
            else WrMemW (dst, ea);
            CC_IIZP_W (dst);
            break;

        case 7:                                         /* SOB */
            dst = (RdRegW (srcspec) - 1) & WMASK;       /* decr reg */
            WrRegW (dst, srcspec);                      /* result */
            if (dst != 0) {                             /* br if zero */
                CMODE_JUMP ((PC - dstspec - dstspec) & WMASK);
                }
            break;

        default:
            CMODE_FAULT (CMODE_RSVI);                   /* end switch EIS */
            }
        break;                                          /* end case 007 */

/* Opcode 10: branches, traps, SOPs */

    case 010:
        switch ((IR >> 6) & 077) {                      /* decode IR<11:6> */
        case 000: case 001:                             /* BPL */
            if ((cc & CC_N) == 0) {
                BRANCH_F (IR);
                } 
            break;

        case 002: case 003:                             /* BPL */
            if ((cc & CC_N) == 0) {
                BRANCH_B (IR);
                }
            break;

        case 004: case 005:                             /* BMI */
            if (cc & CC_N) {
                BRANCH_F (IR);
                } 
            break;

        case 006: case 007:                             /* BMI */
            if (cc & CC_N) {
                BRANCH_B (IR);
                }
            break;

        case 010: case 011:                             /* BHI */
            if ((cc & (CC_C | CC_Z)) == 0) {
                BRANCH_F (IR);
                } 
            break;

        case 012: case 013:                             /* BHI */
            if ((cc & (CC_C | CC_Z)) == 0) {
                BRANCH_B (IR);
                }
            break;

        case 014: case 015:                             /* BLOS */
            if (cc & (CC_C | CC_Z)) {
                BRANCH_F (IR);
                } 
            break;

        case 016: case 017:                             /* BLOS */
            if (cc & (CC_C | CC_Z)) {
                BRANCH_B (IR);
                }
            break;

        case 020: case 021:                             /* BVC */
            if ((cc & CC_V) == 0) {
                BRANCH_F (IR);
                } 
            break;

        case 022: case 023:                             /* BVC */
            if ((cc & CC_V) == 0) {
                BRANCH_B (IR);
                }
            break;

        case 024: case 025:                             /* BVS */
            if (cc & CC_V) {
                BRANCH_F (IR);
                } 
            break;

        case 026: case 027:                             /* BVS */
            if (cc & CC_V) {
                BRANCH_B (IR);
                }
            break;

        case 030: case 031:                             /* BCC */
            if ((cc & CC_C) == 0) {
                BRANCH_F (IR);
                } 
            break;

        case 032: case 033:                             /* BCC */
            if ((cc & CC_C) == 0) {
                BRANCH_B (IR); 
                }
            break;

        case 034: case 035:                             /* BCS */
            if (cc & CC_C) {
                BRANCH_F (IR);
                } 
            break;

        case 036: case 037:                             /* BCS */
            if (cc & CC_C) {
                BRANCH_B (IR);
                }
            break;

        case 040: case 041: case 042: case 043:         /* EMT */
            CMODE_FAULT (CMODE_EMT);
            break;

        case 044: case 045: case 046: case 047:         /* TRAP */
            CMODE_FAULT (CMODE_TRAP);
            break;

        case 050:                                       /* CLRB */
            if (dstreg)
                WrRegB (0, dstspec);
            else WrMemB (0, GeteaB (dstspec));
            cc = CC_Z;
            break;

        case 051:                                       /* COMB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = src ^ BMASK;
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            cc = cc | CC_C;
            break;

        case 052:                                       /* INCB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (src + 1) & BMASK;
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZP_B (dst);
            if (dst == 0200)
                cc = cc | CC_V;
            break;

        case 053:                                       /* DECB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (src - 1) & BMASK;
            if (dstreg) 
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZP_B (dst);
            if (dst == 0177)
                cc = cc | CC_V;
            break;

        case 054:                                       /* NEGB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (-src) & BMASK;
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            if (dst == 0200)
                cc = cc | CC_V;
            if (dst)
                cc = cc | CC_C;
            break;

        case 055:                                       /* ADCB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (src + (cc & CC_C)) & BMASK;
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            if ((src == 0177) && (dst == 0200))
                cc = cc | CC_V;
            if ((src == 0377) && (dst == 0))
                cc = cc | CC_C;
            break;

        case 056:                                       /* SBCB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (src - (cc & CC_C)) & BMASK;
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            if ((src == 0200) && (dst == 0177))
                cc = cc | CC_V;
            if ((src == 0) && (dst == 0377))
                cc = cc | CC_C;
            break;

        case 057:                                       /* TSTB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemB (GeteaB (dstspec));
            CC_IIZZ_B (src);
            break;

        case 060:                                       /* RORB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (src >> 1) | ((cc & CC_C)? BSIGN: 0);
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            if (src & 1)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 061:                                       /* ROLB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = ((src << 1) | ((cc & CC_C)? 1: 0)) & BMASK;
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            if (src & BSIGN)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 062:                                       /* ASRB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (src >> 1) | (src & BSIGN);
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            if (src & 1)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 063:                                       /* ASLB */
            if (dstreg)
                src = RdRegB (dstspec);
            else src = RdMemMB (ea = GeteaB (dstspec));
            dst = (src << 1) & BMASK;
            if (dstreg)
                WrRegB (dst, dstspec);
            else WrMemB (dst, ea);
            CC_IIZZ_B (dst);
            if (src & BSIGN)
                cc = cc | CC_C;
            if (CC_XOR_NC (cc))
                cc = cc | CC_V;
            break;

        case 065:                                       /* MFPD */
            if (dstreg)                                 /* "mov dst,-(sp)" */
                dst = RdRegW (dstspec);
            else dst = RdMemW (GeteaW (dstspec));
            WrMemW (dst, (R[6] - 2) & WMASK);
            R[6] = (R[6] - 2) & WMASK;
            CC_IIZP_W (dst);
            break;

        case 066:                                       /* MTPD */
            dst = RdMemW (R[6] & WMASK);                /* "mov (sp)+,dst" */
            R[6] = (R[6] + 2) & WMASK;
            recq[recqptr++] = RQ_REC (AIN|RW, 6);
            if (dstreg)
                WrRegW (dst, dstspec);
            else WrMemW (dst, (GeteaW (dstspec) & WMASK));
            CC_IIZP_W (dst);
            break;

        default:
            CMODE_FAULT (CMODE_RSVI);
            break; }                                    /* end switch SOPs */
        break;                                          /* end case 010 */

/* Opcodes 11 - 16: double operand byte instructions

   Cmp: v = [sign (src) != sign (src2)] and [sign (src2) = sign (result)]
   Sub: v = [sign (src) != sign (src2)] and [sign (src) = sign (result)]
*/

    case 011:                                           /* MOVB */
        if (srcreg)
            src = RdRegB (srcspec);
        else src = RdMemB (GeteaB (srcspec));
        if (dstreg)
            WrRegW ((src & BSIGN)? (0xFF00 | src): src, dstspec);
        else WrMemB (src, GeteaB (dstspec));
        CC_IIZP_B (src);
        break;

    case 012:                                           /* CMPB */
        if (srcreg)
            src = RdRegB (srcspec);
        else src = RdMemB (GeteaB (srcspec));
        if (dstreg)
            src2 = RdRegB (dstspec);
        else src2 = RdMemB (GeteaB (dstspec));
        dst = (src - src2) & BMASK;
        CC_IIZZ_B (dst);
        if (((src ^ src2) & (~src2 ^ dst)) & BSIGN)
            cc = cc | CC_V;
        if (src < src2)
            cc = cc | CC_C;
        break;

    case 013:                                           /* BITB */
        if (srcreg)
            src = RdRegB (srcspec);
        else src = RdMemB (GeteaB (srcspec));
        if (dstreg)
            src2 = RdRegB (dstspec);
        else src2 = RdMemB (GeteaB (dstspec));
        dst = src2 & src;
        CC_IIZP_B (dst);
        break;

    case 014:                                           /* BICB */
        if (srcreg)
            src = RdRegB (srcspec);
        else src = RdMemB (GeteaB (srcspec));
        if (dstreg)
            src2 = RdRegB (dstspec);
        else src2 = RdMemMB (ea = GeteaB (dstspec));
        dst = src2 & ~src;
        if (dstreg)
            WrRegB (dst, dstspec);
        else WrMemB (dst, ea);
        CC_IIZP_B (dst);
        break;

    case 015:                                           /* BISB */
        if (srcreg)
            src = RdRegB (srcspec);
        else src = RdMemB (GeteaB (srcspec));
        if (dstreg)
            src2 = RdRegB (dstspec);
        else src2 = RdMemMB (ea = GeteaB (dstspec));
        dst = src2 | src;
        if (dstreg)
            WrRegB (dst, dstspec);
        else WrMemB (dst, ea);
        CC_IIZP_B (dst);
        break;

    case 016:                                           /* SUB */
        if (srcreg)
            src = RdRegW (srcspec);
        else src = RdMemW (GeteaW (srcspec));
        if (dstreg)
            src2 = RdRegW (dstspec);
        else src2 = RdMemMW (ea = GeteaW (dstspec));
        dst = (src2 - src) & WMASK;
        if (dstreg)
            WrRegW (dst, dstspec);
        else WrMemW (dst, ea);
        CC_IIZZ_W (dst);
        if (((src ^ src2) & (~src ^ dst)) & WSIGN)
            cc = cc | CC_V;
        if (src2 < src)
            cc = cc | CC_C;
        break;

    default:
        CMODE_FAULT (CMODE_RSVI);
        break;
        }                                               /* end switch op */

return cc;
}

/* Effective address calculations

   Inputs:
        spec    =       specifier <5:0>
   Outputs:
        ea      =       effective address
*/

int32 GeteaW (int32 spec)
{
int32 adr, reg;

reg = spec & 07;                                        /* register number */
switch (spec >> 3) {                                    /* decode spec<5:3> */

    default:                                            /* can't get here */
    case 1:                                             /* (R) */
        if (reg == 7)
            return (PC & WMASK);
        else return (R[reg] & WMASK);

    case 2:                                             /* (R)+ */
        if (reg == 7)
            PC = ((adr = PC) + 2) & WMASK;
        else {
            R[reg] = ((adr = R[reg]) + 2) & WMASK;
            recq[recqptr++] = RQ_REC (AIN|RW, reg);
            }
        return adr;

    case 3:                                             /* @(R)+ */
        if (reg == 7)
            PC = ((adr = PC) + 2) & WMASK;
        else {
            R[reg] = ((adr = R[reg]) + 2) & WMASK;
            recq[recqptr++] = RQ_REC (AIN|RW, reg);
            }
        return RdMemW (adr);

    case 4:                                             /* -(R) */
        if (reg == 7)
            adr = PC = (PC - 2) & WMASK;
        else {
            adr = R[reg] = (R[reg] - 2) & WMASK;
            recq[recqptr++] = RQ_REC (ADC|RW, reg);
            }
        return adr;

    case 5:                                             /* @-(R) */
        if (reg == 7)
            adr = PC = (PC - 2) & WMASK;
        else {
            adr = R[reg] = (R[reg] - 2) & WMASK;
            recq[recqptr++] = RQ_REC (ADC|RW, reg);
            }
        return RdMemW (adr);

    case 6:                                             /* d(r) */
        adr = RdMemW (PC);
        PC = (PC + 2) & WMASK;
        if (reg == 7)
            return ((PC + adr) & WMASK);
        else return ((R[reg] + adr) & WMASK);

    case 7:                                             /* @d(R) */
        adr = RdMemW (PC);
        PC = (PC + 2) & WMASK;
        if (reg == 7)
            adr = (PC + adr) & WMASK;
        else adr = (R[reg] + adr) & WMASK;
        return RdMemW (adr);
        }                                               /* end switch */
}

int32 GeteaB (int32 spec)
{
int32 adr, reg;

reg = spec & 07;                                        /* reg number */
switch (spec >> 3) {                                    /* decode spec<5:3> */

    default:                                            /* can't get here */
    case 1:                                             /* (R) */
        if (reg == 7)
            return (PC & WMASK);
        else return (R[reg] & WMASK);

    case 2:                                             /* (R)+ */
        if (reg == 7)
            PC = ((adr = PC) + 2) & WMASK;
        else if (reg == 6) {
            R[reg] = ((adr = R[reg]) + 2) & WMASK;
            recq[recqptr++] = RQ_REC (AIN|RW, reg);
            }
        else {
            R[reg] = ((adr = R[reg]) + 1) & WMASK;
            recq[recqptr++] = RQ_REC (AIN|RB, reg);
            }
        return adr;

    case 3:                                             /* @(R)+ */
        if (reg == 7)
            PC = ((adr = PC) + 2) & WMASK;
        else {
            R[reg] = ((adr = R[reg]) + 2) & WMASK;
            recq[recqptr++] = RQ_REC (AIN|RW, reg);
            }
        return RdMemW (adr);

    case 4:                                             /* -(R) */
        if (reg == 7)
            adr = PC = (PC - 2) & WMASK;
        else if (reg == 6) {
            adr = R[reg] = (R[reg] - 2) & WMASK;
            recq[recqptr++] = RQ_REC (ADC|RW, reg);
            }
        else {
            adr = R[reg] = (R[reg] - 1) & WMASK;
            recq[recqptr++] = RQ_REC (ADC|RB, reg);
            }
        return adr;

    case 5:                                             /* @-(R) */
        if (reg == 7)
            adr = PC = (PC - 2) & WMASK;
        else {
            adr = R[reg] = (R[reg] - 2) & WMASK;
            recq[recqptr++] = RQ_REC (ADC|RW, reg);
            }
        return RdMemW (adr);

    case 6:                                             /* d(r) */
        adr = RdMemW (PC);
        PC = (PC + 2) & WMASK;
        if (reg == 7)
            return ((PC + adr) & WMASK);
        else return ((R[reg] + adr) & WMASK);

    case 7:                                             /* @d(R) */
        adr = RdMemW (PC);
        PC = (PC + 2) & WMASK;
        if (reg == 7)
            adr = (PC + adr) & WMASK;
        else adr = (R[reg] + adr) & WMASK;
        return RdMemW (adr);
        }                                               /* end switch */
}

/* Memory and register access routines */

int32 RdMemW (int32 a)
{
int32 acc = ACC_MASK (USER);

if (a & 1)
    CMODE_FAULT (CMODE_ODD);
return Read (a, L_WORD, RA);
}

int32 RdMemMW (int32 a)
{
int32 acc = ACC_MASK (USER);

if (a & 1)
    CMODE_FAULT (CMODE_ODD);
return Read (a, L_WORD, WA);
}

void WrMemW (int32 d, int32 a)
{
int32 acc = ACC_MASK (USER);

if (a & 1)
    CMODE_FAULT (CMODE_ODD);
Write (a, d, L_WORD, WA);
return;
}

int32 RdRegB (int32 rn)
{
if (rn == 7)
    return (PC & BMASK);
else return (R[rn] & BMASK);
}

int32 RdRegW (int32 rn)
{
if (rn == 7)
    return (PC & WMASK);
else return (R[rn] & WMASK);
}

void WrRegB (int32 val, int32 rn)
{
if (rn == 7) {
    CMODE_JUMP ((PC & ~BMASK) | val);
    }
else R[rn] = (R[rn] & ~BMASK) | val;
return;
}

void WrRegW (int32 val, int32 rn)
{
if (rn == 7) {
    CMODE_JUMP (val);
    }
else R[rn] = val;
return;
}

#else

/* Subset VAX

   Never legal to set CM in PSL
   Should never get to instruction execution
*/

t_bool BadCmPSL (int32 newpsl)
{
return TRUE;                                            /* always bad */
}

int32 op_cmode (int32 cc)
{
RSVD_INST_FAULT(0);
return cc;
}

#endif
