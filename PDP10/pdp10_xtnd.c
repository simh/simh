/* pdp10_xtnd.c: PDP-10 extended instruction simulator

   Copyright (c) 1993-2016, Robert M Supnik

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

   05-Nov-16    RMS     Fixed last digit error in CVTBDT (Pascal Parent)
   12-May-01    RMS     Fixed compiler warning in xlate

   Instructions handled in this module:

        MOVSLJ          move string left justified
        MOVSO           move string offset
        MOVST           move string translated
        MOVSRJ          move string right justified
        CMPSL           compare string, skip on less
        CMPSE           compare string, skip on equal
        CMPSLE          compare string, skip on less or equal
        CMPSGE          compare string, skip on greater or equal
        CMPSN           compare string, skip on unequal
        CMPSG           compare string, skip on greater
        CVTDBO          convert decimal to binary offset
        CVTDBT          convert decimal to binary translated
        CVTBDO          convert binary to decimal offset
        CVTBDT          convert binary to decimal translated
        EDIT            edit

   The PDP-10 extended instructions deal with non-binary data types,
   particularly byte strings and decimal strings.  (In the KL10, the
   extended instructions include G floating support as well.)  They
   are very complicated microcoded subroutines that can potentially
   run for a very long time.  Accordingly, the instructions must test
   for interrupts as well as page faults, and be prepared to restart
   from either.

   In general, the simulator attempts to keep the AC block up to date,
   so that page fails and interrupts can be taken directly at any point.
   If the AC block is not up to date, memory accessibility must be tested
   before the actual read or write is done.

   The extended instruction routine returns a status code as follows:

        XT_NOSK         no skip completion
        XT_SKIP         skip completion
        XT_MUUO         invalid extended instruction
*/

#include "pdp10_defs.h"
#include <setjmp.h>

#define MM_XSRC         (pflgs & XSRC_PXCT)
#define MM_XDST         (pflgs & XDST_PXCT)
#define MM_EA_XSRC      ((pflgs & EA_PXCT) && MM_XSRC)
#define MM_EA_XDST      ((pflgs & EA_PXCT) && MM_XDST)

#define XT_CMPSL        001                             /* opcodes */
#define XT_CMPSE        002
#define XT_CMPSLE       003
#define XT_EDIT         004
#define XT_CMPSGE       005
#define XT_CMPSN        006
#define XT_CMPSG        007
#define XT_CVTDBO       010
#define XT_CVTDBT       011
#define XT_CVTBDO       012
#define XT_CVTBDT       013
#define XT_MOVSO        014
#define XT_MOVST        015
#define XT_MOVSLJ       016
#define XT_MOVSRJ       017

/* Translation control */

#define XT_LFLG         INT64_C(0400000000000)          /* L flag */
#define XT_SFLG         INT64_C(0400000000000)          /* S flag */
#define XT_NFLG         INT64_C(0200000000000)          /* N flag */
#define XT_MFLG         INT64_C(0100000000000)          /* M flag */

/* Translation table */

#define XT_V_CODE       15                              /* translation op */
#define XT_M_CODE       07
#define XT_BYMASK       07777                           /* byte mask */
#define XT_DGMASK       017                             /* digit mask */
#define XT_GETCODE(x)   ((int32) (((x) >> XT_V_CODE) & XT_M_CODE))

/* AC masks */

#define XLNTMASK        INT64_C(0000777777777)          /* length */
#define XFLGMASK        INT64_C(0700000000000)          /* flags */
#define XT_MBZ          INT64_C(0777000000000)          /* must be zero */
#define XT_MBZE         INT64_C(0047777000000)          /* must be zero, edit */

/* Register change log */

#define XT_N_RLOG       5                               /* entry width */
#define XT_M_RLOG       ((1 << XT_N_RLOG) - 1)          /* entry mask */
#define XT_O_RLOG       1                               /* entry offset */
#define XT_INSRLOG(x,v) v = ((v << XT_N_RLOG) | (((x) + XT_O_RLOG) & XT_M_RLOG))
#define XT_REMRLOG(x,v) x = (v & XT_M_RLOG) - XT_O_RLOG; \
                        v = v >> XT_N_RLOG

/* Edit */

#define ED_V_PBYN       30                              /* pattern byte # */
#define ED_M_PBYN       03
#define ED_PBYNO        INT64_C(0040000000000)          /* overflow bit */
#define ED_GETPBYN(x)   ((int32) (((x) >> ED_V_PBYN) & ED_M_PBYN))
#define ED_V_POPC       6                               /* pattern byte opcode */
#define ED_M_PAT        0777                            /* pattern byte mask */
#define ED_M_NUM        0077                            /* number for msg, etc */
#define ED_PBYTE(x,y)   ((int32) (((x) >> (27 - (ED_GETPBYN (y) * 9))) & ED_M_PAT))
#define ED_STOP         0000                            /* stop */
#define ED_SELECT       0001                            /* select source */
#define ED_SIGST        0002                            /* start significance */
#define ED_FLDSEP       0003                            /* field separator */
#define ED_EXCHMD       0004                            /* exchange mark, dst */
#define ED_MESSAG       0100                            /* message */
#define ED_SKPM         0500                            /* skip if M */
#define ED_SKPN         0600                            /* skip if N */
#define ED_SKPA         0700                            /* skip always */

extern int32 rlog;
extern jmp_buf save_env;

extern d10 Read (int32 ea, int32 prv);
extern void Write (int32 ea, d10 val, int32 prv);
extern a10 calc_ea (d10 inst, int32 prv);
extern int32 test_int (void);
d10 incbp (d10 bp);
d10 incloadbp (int32 ac, int32 pflgs);
void incstorebp (d10 val, int32 ac, int32 pflgs);
d10 xlate (d10 by, a10 tblad, d10 *xflgs, int32 pflgs);
void filldst (d10 fill, int32 ac, d10 cnt, int32 pflgs);

static const d10 pwrs10[23][2] = {
{           INT64_C(0),           INT64_C(0),},
{           INT64_C(0),           INT64_C(1),},
{           INT64_C(0),          INT64_C(10),},
{           INT64_C(0),         INT64_C(100),},
{           INT64_C(0),        INT64_C(1000),},
{           INT64_C(0),       INT64_C(10000),},
{           INT64_C(0),      INT64_C(100000),},
{           INT64_C(0),     INT64_C(1000000),},
{           INT64_C(0),    INT64_C(10000000),},
{           INT64_C(0),   INT64_C(100000000),},
{           INT64_C(0),  INT64_C(1000000000),},
{           INT64_C(0), INT64_C(10000000000),},
{           INT64_C(2), INT64_C(31280523264),},
{          INT64_C(29),  INT64_C(3567587328),},
{         INT64_C(291),  INT64_C(1316134912),},
{        INT64_C(2910), INT64_C(13161349120),},
{       INT64_C(29103), INT64_C(28534276096),},
{      INT64_C(291038), INT64_C(10464854016),},
{     INT64_C(2910383),  INT64_C(1569325056),},
{    INT64_C(29103830), INT64_C(15693250560),},
{   INT64_C(291038304), INT64_C(19493552128),},
{  INT64_C(2910383045), INT64_C(23136829440),},
{ INT64_C(29103830456), INT64_C(25209864192),},
 };

int xtend (int32 ac, int32 ea, int32 pflgs)
{
d10 b1, b2, ppi; 
d10 xinst, xoff = 0, digit, f1, f2, rs[2];
d10 xflgs = 0;
a10 e1 = 0, entad;
int32 p1 = ADDAC (ac, 1);
int32 p3 = ADDAC (ac, 3);
int32 p4 = ADDAC (ac, 4);
int32 flg, i, s2 = 0, t, pp, pat, xop, xac, ret;

xinst = Read (ea, MM_OPND);                             /* get extended instr */
xop = GET_OP (xinst);                                   /* get opcode */
xac = GET_AC (xinst);                                   /* get AC */
if (xac || (xop == 0) || (xop > XT_MOVSRJ))
    return XT_MUUO;
rlog = 0;                                               /* clear log */
switch (xop) {                                          /* case on opcode */

/* String compares - checked against KS10 ucode
   If both strings are zero length, they are considered equal.
   Both source and destination lengths are MBZ checked.

        AC      =       source1 length
        AC + 1  =       source1 byte pointer
        AC + 3  =       source2 length
        AC + 4  =       source2 byte pointer
*/

    case XT_CMPSL:                                      /* CMPSL */
    case XT_CMPSE:                                      /* CMPSE */
    case XT_CMPSLE:                                     /* CMPSLE */
    case XT_CMPSGE:                                     /* CMPSGE */
    case XT_CMPSN:                                      /* CMPSN */
    case XT_CMPSG:                                      /* CMPSG */
        if ((AC(ac) | AC(p3)) & XT_MBZ)                 /* check length MBZ */
            return XT_MUUO;
        f1 = Read (ADDA (ea, 1), MM_OPND) & bytemask[GET_S (AC(p1))];
        f2 = Read (ADDA (ea, 2), MM_OPND) & bytemask[GET_S (AC(p4))];
        b1 = b2 = 0;
        for (flg = 0; (AC(ac) | AC(p3)) && (b1 == b2); flg++) {
            if (flg && (t = test_int ()))
                ABORT (t);
            rlog = 0;                                   /* clear log */
            if (AC(ac))                                 /* src1 */
                b1 = incloadbp (p1, pflgs);
            else b1 = f1;
            if (AC(p3))                                 /* src2 */
                b2 = incloadbp (p4, pflgs);
            else b2 = f2;
            if (AC(ac))
                AC(ac) = (AC(ac) - 1) & XLNTMASK;
            if (AC(p3))
                AC(p3) = (AC(p3) - 1) & XLNTMASK;
            }
        switch (xop) {
        case XT_CMPSL:
            return (b1 < b2)? XT_SKIP: XT_NOSK;
        case XT_CMPSE:
            return (b1 == b2)? XT_SKIP: XT_NOSK;
        case XT_CMPSLE:
            return (b1 <= b2)? XT_SKIP: XT_NOSK;
        case XT_CMPSGE:
            return (b1 >= b2)? XT_SKIP: XT_NOSK;
        case XT_CMPSN:
            return (b1 != b2)? XT_SKIP: XT_NOSK;
        case XT_CMPSG:
            return (b1 > b2)? XT_SKIP: XT_NOSK;
            }
        
        return XT_MUUO;

/* Convert binary to decimal instructions - checked against KS10 ucode
   There are no MBZ tests.

        AC'AC + 1 =     double precision integer source
        AC + 3  =       flags and destination length
        AC + 4  =       destination byte pointer
*/

    case XT_CVTBDO:                                     /* CVTBDO */
    case XT_CVTBDT:                                     /* CVTBDT */
        e1 = calc_ea (xinst, MM_EA);                    /* get ext inst addr */
        if (xop == XT_CVTBDO)                           /* offset? */
            xoff = (e1 & RSIGN)? (e1 | LMASK): e1;      /* get offset */
        rs[0] = AC(ac);                                 /* get src opnd */
        rs[1] = CLRS (AC(p1));
        if (!TSTF (F_FPD)) {                            /* set up done yet? */
            if (TSTS (AC(ac))) {                        /* get abs value */
                DMOVN (rs);
                }
            for (i = 22; i > 1; i--) {                  /* find field width */
                if (DCMPGE (rs, pwrs10[i]))
                    break;
                }
            if (i > (AC(p3) & XLNTMASK))
                return XT_NOSK;
            if ((i < (AC(p3) & XLNTMASK)) && (AC(p3) & XT_LFLG)) {
                f1 = Read (ADDA (ea, 1), MM_OPND);
                filldst (f1, p3, (AC(p3) & XLNTMASK) - i, pflgs);
                }
            else AC(p3) = (AC(p3) & XFLGMASK) | i;
            if (TSTS (AC(ac)))
                AC(p3) = AC(p3) | XT_MFLG;
            if (AC(ac) | AC(p1))
                AC(p3) = AC(p3) | XT_NFLG;
            AC(ac) = rs[0];                             /* update state */
            AC(p1) = rs[1];
            SETF (F_FPD);                               /* mark set up done */
            }

/* Now do actual binary to decimal conversion */

        for (flg = 0; AC(p3) & XLNTMASK; flg++) {
            if (flg && (t = test_int ()))
                ABORT (t);
            rlog = 0;                                   /* clear log */
            i = (int32) AC(p3) & XLNTMASK;              /* get length */
            if (i > 22)                                 /* put in range */
                i = 22;
            for (digit = 0; (digit < 10) && DCMPGE (rs, pwrs10[i]); digit++) {
                rs[0] = rs[0] - pwrs10[i][0] - (rs[1] < pwrs10[i][1]);
                rs[1] = (rs[1] - pwrs10[i][1]) & MMASK;
                }
            if (xop == XT_CVTBDO)                       /* offset? */
                digit = (digit + xoff) & DMASK;
            else {                                      /* translate */
                f1 = Read (e1 + (int32) digit, MM_OPND);/* get xlation */
                if ((i == 1) && (AC(p3) & XT_MFLG))     /* last digit, minus? */
                    f1 = f1 >> 18;                      /* use left */
                digit = f1 & RMASK;
                }
            incstorebp (digit, p4, pflgs);              /* store digit */
            AC(ac) = rs[0];                             /* mem access ok */
            AC(p1) = rs[1];                             /* update state */
            AC(p3) = (AC(p3) & XFLGMASK) | ((AC(p3) - 1) & XLNTMASK);
            }
        CLRF (F_FPD);                                   /* clear FPD */
        return XT_SKIP;

/* Convert decimal to binary instructions - checked against KS10 ucode
   There are no MBZ tests.

        AC      =       flags and source length
        AC + 1  =       source byte pointer
        AC + 3'AC + 4 = double precision integer result
*/

    case XT_CVTDBT:                                     /* CVTDBT */
    case XT_CVTDBO:                                     /* CVTDBO */
        e1 = calc_ea (xinst, MM_EA);                    /* get ext inst addr */
        if ((AC(ac) & XT_SFLG) == 0)                    /* !S? clr res */
            AC(p3) = AC(p4) = 0;
        else AC(p4) = CLRS (AC(p4));                    /* clear low sign */
        if (xop == XT_CVTDBO) {                         /* offset? */
            xoff = (e1 & RSIGN)? (e1 | LMASK): e1;      /* get offset */
            AC(ac) = AC(ac) | XT_SFLG;                  /* set S flag */
            }
        xflgs = AC(ac) & XFLGMASK;                      /* get xlation flags */
        for (flg = 0; AC(ac) & XLNTMASK; flg++) {
            if (flg && (t = test_int ()))
                ABORT (t);
            rlog = 0;                                   /* clear log */
            b1 = incloadbp (p1, pflgs);                 /* get byte */
            if (xop == XT_CVTDBO)
                b1 = (b1 + xoff) & DMASK;
            else {
                b1 = xlate (b1, e1, &xflgs, MM_OPND);
                if (b1 < 0) {                           /* terminated? */
                    AC(ac) = xflgs | ((AC(ac) - 1) & XLNTMASK);
                    if (TSTS (AC(p3)))
                        AC(p4) = SETS (AC(p4));  
                    return XT_NOSK;
                    }
                if (xflgs & XT_SFLG)
                    b1 = b1 & XT_DGMASK;
                else b1 = 0;
                }
            AC(ac) = xflgs | ((AC(ac) - 1) & XLNTMASK);
            if ((b1 < 0) || (b1 > 9)) {                 /* bad digit? done */
                if (TSTS (AC(p3)))
                    AC(p4) = SETS (AC(p4));      
                return XT_NOSK;
                }
            AC(p4) = (AC(p4) * 10) + b1;                /* base * 10 + digit */
            AC(p3) = ((AC(p3) * 10) + (AC(p4) >> 35)) & DMASK;
            AC(p4) = AC(p4) & MMASK;
            }
        if (AC(ac) & XT_MFLG) {
            AC(p4) = -AC(p4) & MMASK;
            AC(p3) = (~AC(p3) + (AC(p4) == 0)) & DMASK;
            }
        if (TSTS (AC(p3)))
            AC(p4) = SETS (AC(p4));      
        return XT_SKIP;

/* String move instructions - checked against KS10 ucode
   Only the destination length is MBZ checked.

        AC      =       flags (MOVST only) and source length
        AC + 1  =       source byte pointer
        AC + 3  =       destination length
        AC + 4  =       destination byte pointer
*/

    case XT_MOVSO:                                      /* MOVSO */
    case XT_MOVST:                                      /* MOVST */
    case XT_MOVSRJ:                                     /* MOVSRJ */
    case XT_MOVSLJ:                                     /* MOVSLJ */
        if (AC(p3) & XT_MBZ)                            /* test dst lnt MBZ */
            return XT_MUUO;
        f1 = Read (ADDA (ea, 1), MM_OPND);              /* get fill */
        switch (xop) {                                  /* case on instr */

        case XT_MOVSO:                                  /* MOVSO */
            AC(ac) = AC(ac) & XLNTMASK;                 /* trim src length */
            xoff = calc_ea (xinst, MM_EA);              /* get offset */
            if (xoff & RSIGN)                           /* sign extend 18b */
                xoff = xoff | LMASK;
            s2 = GET_S (AC(p4));                        /* get dst byte size */
            break;

        case XT_MOVST:                                  /* MOVST */
            e1 = calc_ea (xinst, MM_EA);                /* get xlate tbl addr */
            break;

        case XT_MOVSRJ:                                 /* MOVSRJ */
            AC(ac) = AC(ac) & XLNTMASK;                 /* trim src length */
            if (AC(p3) == 0)
                return (AC(ac)? XT_NOSK: XT_SKIP);
            if (AC(ac) > AC(p3)) {                      /* adv src ptr */
                for (flg = 0; AC(ac) > AC(p3); flg++) {
                    if (flg && (t = test_int ()))
                        ABORT (t);
                    AC(p1) = incbp (AC(p1));
                    AC(ac) = (AC(ac) - 1) & XLNTMASK;
                    }
                }
            else if (AC(ac) < AC(p3))
                filldst (f1, p3, AC(p3) - AC(ac), pflgs);
            break;

        case XT_MOVSLJ:                                 /* MOVSLJ */
            AC(ac) = AC(ac) & XLNTMASK;                 /* trim src length */
            break;
            }                                           /* end case xop */

        xflgs = AC(ac) & XFLGMASK;                      /* get xlation flags */
        if (AC(p3) == 0)
            return (AC(ac)? XT_NOSK: XT_SKIP);
        for (flg = 0; AC(p3) & XLNTMASK; flg++) {
            if (flg && (t = test_int ()))
                ABORT (t);
            rlog = 0;                                   /* clear log */
            if (AC(ac) & XLNTMASK) {                    /* any source? */
                b1 = incloadbp (p1, pflgs);             /* src byte */
                if (xop == XT_MOVSO) {                  /* offset? */
                    b1 = (b1 + xoff) & DMASK;           /* test fit */
                    if (b1 & ~bytemask[s2]) {
                        AC(ac) = xflgs | ((AC(ac) - 1) & XLNTMASK);
                        return XT_NOSK;
                        }
                    }
                else if (xop == XT_MOVST) {             /* translate? */
                    b1 = xlate (b1, e1, &xflgs, MM_OPND);
                    if (b1 < 0) {                       /* upd flags in AC */
                        AC(ac) = xflgs | ((AC(ac) - 1) & XLNTMASK);
                        return XT_NOSK;
                        }
                    if (xflgs & XT_SFLG)
                        b1 = b1 & XT_BYMASK;
                    else b1 = -1;
                    }   
                }
            else b1 = f1;
            if (b1 >= 0) {                              /* valid byte? */
                incstorebp (b1, p4, pflgs);             /* store byte */
                AC(p3) = (AC(p3) - 1) & XLNTMASK;       /* update state */
                }
            if (AC(ac) & XLNTMASK)
                AC(ac) = xflgs | ((AC(ac) - 1) & XLNTMASK);
            }
        return (AC(ac) & XLNTMASK)? XT_NOSK: XT_SKIP;

/* Edit - checked against KS10 ucode
   Only the flags/pattern pointer word is MBZ checked.

        AC      =       flags, pattern pointer
        AC + 1  =       source byte pointer
        AC + 3  =       mark address
        AC + 4  =       destination byte pointer
*/

    case XT_EDIT:                                       /* EDIT */
        if (AC(ac) & XT_MBZE)                           /* check pattern MBZ */
            return XT_MUUO;
        xflgs = AC(ac) & XFLGMASK;                      /* get xlation flags */
        e1 = calc_ea (xinst, MM_EA);                    /* get xlate tbl addr */
        for (ppi = 1, ret = -1, flg = 0; ret < 0; flg++, ppi = 1) {
            if (flg && (t = test_int ()))
                ABORT (t);
            rlog = 0;                                   /* clear log */
            pp = (int32) AC(ac) & AMASK;                /* get pattern ptr */
            b1 = Read (pp, MM_OPND);                    /* get pattern word */
            pat = ED_PBYTE (b1, AC(ac));                /* get pattern byte */
            switch ((pat < 0100)? pat: ((pat >> ED_V_POPC) + 0100)) {

            case ED_STOP:                               /* stop */
                ret = XT_SKIP;                          /* exit loop */
                break;

            case ED_SELECT:                             /* select source */
                b1 = incloadbp (p1, pflgs);             /* get src */
                entad = (e1 + ((int32) b1 >> 1)) & AMASK;
                f1 = ((Read (entad, MM_OPND) >> ((b1 & 1)? 0: 18)) & RMASK);
                i = XT_GETCODE (f1);
                if (i & 2)
                    xflgs = (i & 1)? xflgs | XT_MFLG: xflgs & ~XT_MFLG;
                switch (i) {

                case 00: case 02: case 03:
                    if (xflgs & XT_SFLG)
                        f1 = f1 & XT_BYMASK;
                    else {
                        f1 = Read (INCA (ea), MM_OPND);
                        if (f1 == 0)
                            break;
                        }
                    incstorebp (f1, p4, pflgs);
                    break;

                case 01:
                    ret = XT_NOSK;                      /* exit loop */
                    break;

                case 04: case 06: case 07:
                    xflgs = xflgs | XT_NFLG;
                    f1 = f1 & XT_BYMASK;
                    if ((xflgs & XT_SFLG) == 0) {
                        f2 = Read (ADDA (ea, 2), MM_OPND);
                        Write ((a10) AC(p3), AC(p4), MM_OPND);
                        if (f2)
                            incstorebp (f2, p4, pflgs);
                        xflgs = xflgs | XT_SFLG;
                        }
                    incstorebp (f1, p4, pflgs);
                    break;

                case 05:
                    xflgs = xflgs | XT_NFLG;
                    ret = XT_NOSK;                      /* exit loop */
                    break;
                    }                                   /* end case xlate op */
                break;

            case ED_SIGST:                              /* start significance */
                if ((xflgs & XT_SFLG) == 0) {
                    f2 = Read (ADDA (ea, 2), MM_OPND);
                    Write ((a10) AC(p3), AC(p4), MM_OPND);
                    if (f2)
                        incstorebp (f2, p4, pflgs);
                    xflgs = xflgs | XT_SFLG;
                    }
                break;

            case ED_FLDSEP:                             /* separate fields */
                xflgs = 0;
                break;

            case ED_EXCHMD:                             /* exchange */
                f2 = Read ((int32) (AC(p3) & AMASK), MM_OPND);
                Write ((int32) (AC(p3) & AMASK), AC(p4), MM_OPND);
                AC(p4) = f2;
                break;

            case (0100 + (ED_MESSAG >> ED_V_POPC)):     /* message */
                if (xflgs & XT_SFLG)
                    f1 = Read (ea + (pat & ED_M_NUM) + 1, MM_OPND);
                else {
                    f1 = Read (ea + 1, MM_OPND);
                    if (f1 == 0)
                        break;
                    }
                incstorebp (f1, p4, pflgs);
                break;

            case (0100 + (ED_SKPM >> ED_V_POPC)):       /* skip on M */
                if (xflgs & XT_MFLG)
                    ppi = (pat & ED_M_NUM) + 2;
                break;

            case (0100 + (ED_SKPN >> ED_V_POPC)):       /* skip on N */
                if (xflgs & XT_NFLG)
                    ppi = (pat & ED_M_NUM) + 2;
                break;

            case (0100 + (ED_SKPA >> ED_V_POPC)):       /* skip always */
                ppi = (pat & ED_M_NUM) + 2;
                break;

            default:                                    /* NOP or undefined */
                break;
                }                                       /* end case pttrn op */
            AC(ac) = AC(ac) + ((ppi & ED_M_PBYN) << ED_V_PBYN);
            AC(ac) = AC(ac) + (ppi >> 2) + ((AC(ac) & ED_PBYNO)? 1: 0);
            AC(ac) = xflgs | (AC(ac) & ~(XT_MBZE | XFLGMASK));
            }
        return ret;
        }                                               /* end case xop */
return XT_MUUO;
}

/* Supporting subroutines */

/* Increment byte pointer, register version */

d10 incbp (d10 bp)
{
int32 p, s;

p = GET_P (bp);                                         /* get P and S */
s = GET_S (bp);
p = p - s;                                              /* adv P */
if (p < 0) {                                            /* end of word? */
    bp = (bp & LMASK) | (INCR (bp));                    /* increment addr */
    p = (36 - s) & 077;                                 /* reset P */
    }
bp = PUT_P (bp, p);                                     /* store new P */
return bp;
}

/* Increment and load byte, extended version - uses register log */

d10 incloadbp (int32 ac, int32 pflgs)
{
a10 ba;
d10 bp, wd;
int32 p, s;

bp = AC(ac) = incbp (AC(ac));                           /* increment bp */
XT_INSRLOG (ac, rlog);                                  /* log change */
p = GET_P (bp);                                         /* get P and S */
s = GET_S (bp);
ba = calc_ea (bp, MM_EA_XSRC);                          /* calc bp eff addr */
wd = Read (ba, MM_XSRC);                                /* read word */
wd = (wd >> p) & bytemask[s];                           /* get byte */
return wd;
}

/* Increment and deposit byte, extended version - uses register log */

void incstorebp (d10 val, int32 ac, int32 pflgs)
{
a10 ba;
d10 bp, wd, mask;
int32 p, s;

bp = AC(ac) = incbp (AC(ac));                           /* increment bp */
XT_INSRLOG (ac, rlog);                                  /* log change */
p = GET_P (bp);                                         /* get P and S */
s = GET_S (bp);
ba = calc_ea (bp, MM_EA_XDST);                          /* calc bp eff addr */
wd = Read (ba, MM_XDST);                                /* read, write test */
mask = bytemask[s] << p;                                /* shift mask, val */
val = val << p;
wd = (wd & ~mask) | (val & mask);                       /* insert byte */
Write (ba, wd & DMASK, MM_XDST);
return;
}

/* Translate byte

   Arguments
        by      =       byte to translate
        tblad   =       virtual address of translation table
        *xflgs  =       pointer to word containing translation flags
        prv     =       previous mode flag for table lookup
   Returns
        xby     =       >= 0, translated byte
                        < 0, terminate translation
*/

d10 xlate (d10 by, a10 tblad, d10 *xflgs, int32 prv)
{
a10 ea;
int32 tcode;
d10 tblent;

ea = (tblad + ((int32) by >> 1)) & AMASK;
tblent = ((Read (ea, prv) >> ((by & 1)? 0: 18)) & RMASK);
tcode = XT_GETCODE (tblent);                            /* get xlate code */
switch (tcode) {

    case 00:
        return (*xflgs & XT_SFLG)? tblent: by;

    case 01:
        break;

    case 02:
        *xflgs = *xflgs & ~XT_MFLG;
        return (*xflgs & XT_SFLG)? tblent: by;

    case 03:
        *xflgs = *xflgs | XT_MFLG;
        return (*xflgs & XT_SFLG)? tblent: by;

    case 04:
        *xflgs = *xflgs | XT_SFLG | XT_NFLG;
        return tblent;

    case 05:
        *xflgs = *xflgs | XT_NFLG;
        break;

    case 06:
        *xflgs = (*xflgs | XT_SFLG | XT_NFLG) & ~XT_MFLG;
        return tblent;

    case 07:
        *xflgs = *xflgs | XT_SFLG | XT_NFLG | XT_MFLG;
        return tblent;
        }                                               /* end case  */

return -1;
}

/* Fill out the destination string

   Arguments:
        fill    =       fill
        ac      =       2 word AC block (length, byte pointer)
        cnt     =       fill count
        pflgs   =       PXCT flags
*/

void filldst (d10 fill, int32 ac, d10 cnt, int32 pflgs)
{
int32 i, t;
int32 p1 = ADDA (ac, 1);

for (i = 0; i < cnt; i++) {
    if (i && (t = test_int ()))
        ABORT (t);
    rlog = 0;                                           /* clear log */ 
    incstorebp (fill, p1, pflgs);
    AC(ac) = (AC(ac) & XFLGMASK) | ((AC(ac) - 1) & XLNTMASK);
    }
rlog = 0;
return;
}

/* Clean up after page fault

   Arguments:
        logv    =       register change log

   For each register in logv, decrement the register's contents as
   though it were a byte pointer.  Note that the KS10 does <not>
   do a full decrement calculation but merely adds S to P.
*/

void xtcln (int32 logv)
{
int32 p, reg;

while (logv) {
    XT_REMRLOG (reg, logv);                             /* get next reg */
    if ((reg >= 0) && (reg < AC_NUM)) {
        p = GET_P (AC(reg)) + GET_S (AC(reg));          /* get p + s */
        AC(reg) = PUT_P (AC(reg), p);                   /* p <- p + s */
        }
    }
return;
}
