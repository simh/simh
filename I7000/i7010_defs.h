/* i7010_defs.h: IBM 7010 simulator definitions

   Copyright (c) 2006-2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sim_defs.h"                           /* simulator defns */

#include "i7000_defs.h"

/* Memory */
#define AMASK  0x1ffff
#define BBIT   0x80000000
#define MEM_ADDR_OK(x)  ((uint32)((x) & AMASK) < MEMSIZE)
extern uint8            M[MAXMEMSIZE];
#define WM      0200    /* Word mark in memory */

/* Issue a command to a channel */
int chan_cmd(uint16 dev, uint16 cmd, uint32 addr);

/* Opcodes */                   /* I     A     B   */
#define OP_A    CHR_A           /* Aab */ /* NSI   ALW   BLB */
#define OP_S    CHR_S           /* Sab */ /* NSI   ALW   BLB */
#define OP_ZA   CHR_QUEST       /* ?ab */ /* NSI   ALW   BLB */
#define OP_ZS   CHR_EXPL        /* !ab */ /* NSI   ALW   BLB */
#define OP_M    CHR_QUOT        /* @ab */ /* NSI   ALA   BLB */
#define OP_D    CHR_RPARN       /* %ab */ /* NSI   ALA   10-quotient */
#define OP_SAR  CHR_G           /* Gcd */       /* NSI    *     *    C */
                                /* A B E F T*/ /* 061, 062, 065, 066 */
#define OP_SWM  CHR_COM         /* ,ab */ /* NSI   A-1   B-1 */
#define OP_CWM  CHR_LPARN       /* sqab*/ /* NSI   A-1   B-1 */
#define OP_CS   CHR_SLSH        /* /ib */ /* NSI/B B     bbb00-1/NSIB */
#define OP_H    CHR_DOT         /* .i */ /* NSI/B BI  NSIB  */
#define OP_NOP  CHR_N           /* Nxxx^ */     /* NSI   *     *   */
#define OP_MOV  CHR_D           /* Dabd */      /* NSI  */
                                /* B A    8     4   2   1 */
                                /* 1pos   r-l   scan      */
                                /* awm          wm  Z   N */
                                /* bwm    */
                                /* a|bwm  */
                                /* a|bwm  l-r */
                                /* arm    */
                                /* agm|awm */
                                /* arm|agm|arm  */
#define OP_MSZ  CHR_Z           /* Zab */       /* NSI   ALA   B+1 */
#define OP_C    CHR_C           /* Cab */ /* NSI   ALW   BLW */
#define OP_T    CHR_T           /* Tabd */      /* NSI   ALW   last addr */
                                /* 1 <, 2 ==, 3 <=, 4 >, 5 <>, 6 =>, 7 any, b end */
#define OP_E    CHR_E           /* Eab */ /* NSI   ALA  ? */
#define OP_B    CHR_J           /* Jid */ /* NSIB  BI   NSIB */
                                /* blank   jump */
                                /* Z   Arith overflow */
                                /* 9   Carriage 9 CH1 */
                                /* !   Carriage 9 CH2 */
                                /* R   Carriage Busy CH1 */
                                /* L   Carriage Busy CH2 */
                                /* @   Cariage Overflow 12 CH 1 */
                                /* sq  Cariage Overflow 12 CH 2 */
                                /* S   Equal */
                                /* U   High */
                                /* T   Low */
                                /* /   High or Low */
                                /* W   Divide overflow */
                                /* Q   Inq req ch 1 */
                                /* *   Inq req ch 2 */
                                /* 1   Overlap in Proc Ch 1 */
                                /* 2   Overlap in Proc Ch 2 */
                                /* 3   Overlap in Proc Ch 1 */
                                /* 4   Overlap in Proc Ch 2 */
                                /* K   Tape indicator */
                                /* V   Zero Balence */
                                /* Y   Branch Exp over */
                                /* X   Branch Exp under */
                                /* M   Branch Bin Card 1 */
                                /* (   Branch Bin Card 2 */
#define OP_IO1 CHR_R            /* Rid */ /* NSIB    BI    NSIB */
#define OP_IO2 CHR_X            /* Xid */ /* NSIB    BI    NSIB */
#define OP_IO3 CHR_3            /* 3id */ /* NSIB    BI    NSIB */
#define OP_IO4 CHR_1            /* 1id */ /* NSIB    BI    NSIB */
                                /* B-Wrong len, A-No Trans, 8-Condition,
                                   4-Data Check, 2-Busy, 1- not Ready */
#define OP_BCE CHR_B            /* Bibd */ /* NSIB    BI    B-1/NSIB */
#define OP_BBE CHR_W            /* Wibd */ /* NSIB    BI    B-1/NSIB */
#define OP_BWE CHR_V            /* Vibd */ /* NSIB    BI    B-1/NSIB */

#define OP_RD  CHR_M            /* Mxbd */
#define OP_RDW CHR_L            /* Lxbd */
#define OP_CC1 CHR_F            /* Fd */
#define OP_CC2 CHR_2            /* 2d */
#define OP_SSF1 CHR_K           /* Kd */
#define OP_SSF2 CHR_4           /* 4d */
#define OP_UC  CHR_U            /* Uxd */
#define OP_PRI CHR_Y            /* Yd */ /* E- enter, X leave */
                                        /* 21 - enable prot? */
                                        /* 11 - disable prot? */
#define OP_STS CHR_DOL          /* $ad */ /* S- store, R- restore */
                                          /* E - stchan1, G stchan2 */
                                          /* 1 - rschan1, 4 rschan4*/
                                          /* 47 - set low? */
                                          /* 72 - set high? */
#define OP_FP  CHR_EQ           /* =ad */ /* R - Floating Reset Add */
                                          /* L - Floating store */
                                          /* A - Floating add */
                                          /* S - Floating sub */
                                          /* M - Floating mul */
                                          /* D - Floating div */

/* Flags for chan_io_status. */
#define IO_CHS_NORDY 0001       /* Unit not Ready */
#define IO_CHS_BUSY  0002       /* Unit or channel Busy */
#define IO_CHS_CHECK 0004       /* Data check */
#define IO_CHS_COND  0010       /* Condition */
#define IO_CHS_NOTR  0020       /* No transfer */
#define IO_CHS_WRL   0040       /* Wrong length */
#define IO_CHS_DONE  0100       /* Device done */
#define IO_CHS_OVER  0200       /* Channel busy on overlap processing */
