/* 3b2_400_mau.c: AT&T 3B2 Model 400 Math Acceleration Unit (WE32106 MAU)
   Header

   Copyright (c) 2019, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.

   ---------------------------------------------------------------------

   This file is part of a simulation of the WE32106 Math Acceleration
   Unit. The WE32106 MAU is an IEEE-754 compabitle floating point
   hardware math accelerator that was available as an optional
   component on the AT&T 3B2/310 and 3B2/400, and a standard component
   on the 3B2/500, 3B2/600, and 3B2/1000.

   Portions of this code are derived from the SoftFloat 2c library by
   John R. Hauser. Functions derived from SoftFloat 2c are clearly
   marked in the comments.

   Legal Notice
   ============

   SoftFloat was written by John R. Hauser.  Release 2c of SoftFloat
   was made possible in part by the International Computer Science
   Institute, located at Suite 600, 1947 Center Street, Berkeley,
   California 94704.  Funding was partially provided by the National
   Science Foundation under grant MIP-9311980.  The original version
   of this code was written as part of a project to build a
   fixed-point vector processor in collaboration with the University
   of California at Berkeley, overseen by Profs. Nelson Morgan and
   John Wawrzynek.

   THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable
   effort has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS
   THAT WILL AT TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS
   SOFTWARE IS RESTRICTED TO PERSONS AND ORGANIZATIONS WHO CAN AND
   WILL TOLERATE ALL LOSSES, COSTS, OR OTHER PROBLEMS THEY INCUR DUE
   TO THE SOFTWARE WITHOUT RECOMPENSE FROM JOHN HAUSER OR THE
   INTERNATIONAL COMPUTER SCIENCE INSTITUTE, AND WHO FURTHERMORE
   EFFECTIVELY INDEMNIFY JOHN HAUSER AND THE INTERNATIONAL COMPUTER
   SCIENCE INSTITUTE (possibly via similar legal notice) AGAINST ALL
   LOSSES, COSTS, OR OTHER PROBLEMS INCURRED BY THEIR CUSTOMERS AND
   CLIENTS DUE TO THE SOFTWARE, OR INCURRED BY ANYONE DUE TO A
   DERIVATIVE WORK THEY CREATE USING ANY PART OF THE SOFTWARE.

   The following are expressly permitted, even for commercial
   purposes:

   (1) distribution of SoftFloat in whole or in part, as long as this
   and other legal notices remain and are prominent, and provided also
   that, for a partial distribution, prominent notice is given that it
   is a subset of the original; and

   (2) inclusion or use of SoftFloat in whole or in part in a
   derivative work, provided that the use restrictions above are met
   and the minimal documentation requirements stated in the source
   code are satisfied.
   ---------------------------------------------------------------------

   Data Types
   ==========

   The WE32106 MAU stores values using IEEE-754 1985 types, plus a
   non-standard Decimal type.

   - Decimal Type - 18 BCD digits long. Each digit is 4 bits wide.
     Sign is encoded in byte 0.

    3322 2222 2222 1111 1111 1100 0000 0000
    1098 7654 3210 9876 5432 1098 7654 3210
   +-------------------+----+----+----+----+
   |       unused      | D18| D17| D16| D15|  High Word
   +----+----+----+----+----+----+----+----+
   | D14| D13| D12| D11| D10| D09| D08| D07|  Middle Word
   +----+----+----+----+----+----+----+----+
   | D06| D05| D04| D03| D02| D01| D00|sign|  Low Word
   +----+----+----+----+----+----+----+----+

   Sign: 0: Positive Infinity   10: Positive Number
         1: Negative Infinity   11: Negative Number
         2: Positive NaN        12: Positive Number
         3: Negative NaN        13: Negative Number
         4-9: Trapping NaN      14-15: Positive Number

   - Extended Precision (80-bit) - exponent biased by 16383

   3 322222222221111 1 111110000000000
   1 098765432109876 5 432109876543210
   +-----------------+-+---------------+
   |      unused     |S|   Exponent    |  High Word
   +-+---------------+-+---------------+
   |J|      Fraction (high word)       |  Middle Word
   +-+---------------------------------+
   |        Fraction (low word)        |  Low Word
   +-----------------------------------+


   - Double Precision (64-bit) - exponent biased by 1023

   3 3222222222 211111111110000000000
   1 0987654321 098765432109876543210
   +-+----------+---------------------+
   |S| Exponent |   Fraction (high)   |    High Word
   +-+----------+---------------------+
   |           Fraction (low)         |    Low Word
   +----------------------------------+


   - Single Precision (32-bit) - exponent biased by 127

   3 32222222 22211111111110000000000
   1 09876543 21098765432109876543210
   +-+--------+-----------------------+
   |S|  Exp   |       Fraction        |
   +-+--------+-----------------------+

*/

#ifndef _3B2_400_MAU_H_
#define _3B2_400_MAU_H_

#include "sim_defs.h"

#define SRC_LEN_INVALID   0
#define SRC_LEN_SINGLE    1
#define SRC_LEN_DOUBLE    2
#define SRC_LEN_TRIPLE    3

#define MAU_ASR_RC_SHIFT  22

#define MAU_ASR_PR        0x20u        /* Partial Remainder        */
#define MAU_ASR_QS        0x40u        /* Divide By Zero Sticky    */
#define MAU_ASR_US        0x80u        /* Underflow Sticky         */
#define MAU_ASR_OS        0x100u       /* Overflow Sticky          */
#define MAU_ASR_IS        0x200u       /* Invalid Operation Sticky */
#define MAU_ASR_PM        0x400u       /* Inexact Mask             */
#define MAU_ASR_QM        0x800u       /* Divide by Zero Mask      */
#define MAU_ASR_UM        0x1000u      /* Underflow Mask           */
#define MAU_ASR_OM        0x2000u      /* Overflow Mask            */
#define MAU_ASR_IM        0x4000u      /* Invalid Operation Mask   */

#define MAU_ASR_UO        0x10000u     /* Unordered                */
#define MAU_ASR_CSC       0x20000u     /* Context Switch Control   */
#define MAU_ASR_PS        0x40000u     /* Inexact Sticky           */
#define MAU_ASR_IO        0x80000u     /* Integer Overflow         */
#define MAU_ASR_Z         0x100000u    /* Zero Flag                */
#define MAU_ASR_N         0x200000u    /* Negative Flag            */
#define MAU_ASR_RC        0x400000u    /* Round Control            */

#define MAU_ASR_NTNC      0x1000000u   /* Nontrapping NaN Control  */
#define MAU_ASR_ECP       0x2000000u   /* Exception Condition      */

#define MAU_ASR_RA        0x80000000u  /* Result Available         */

#define MAU_RC_RN         0            /* Round toward Nearest     */
#define MAU_RC_RP         1            /* Round toward Plus Infin. */
#define MAU_RC_RM         2            /* Round toward Neg. Infin. */
#define MAU_RC_RZ         3            /* Round toward Zero        */

#define SFP_SIGN(V)  (((V) >> 31) & 1)
#define SFP_EXP(V)   (((V) >> 23) & 0xff)
#define SFP_FRAC(V)  ((V) & 0x7fffff)

#define DFP_SIGN(V)  (((V) >> 63) & 1)
#define DFP_EXP(V)   (((V) >> 52) & 0x7ff)
#define DFP_FRAC(V)  ((V) & 0xfffffffffffffull)

#define XFP_SIGN(V)  (((V)->sign_exp >> 15) & 1)
#define XFP_EXP(V)   ((V)->sign_exp & 0x7fff)
#define XFP_FRAC(V)  ((V)->frac)
    
#define XFP_IS_NORMAL(V)          ((V)->frac & 0x8000000000000000ull)

#define DEFAULT_XFP_NAN_SIGN_EXP  0xffff
#define DEFAULT_XFP_NAN_FRAC      0xc000000000000000ull

#define SFP_IS_TRAPPING_NAN(V)   (((((V) >> 22) & 0x1ff) == 0x1fe) &&  \
                                  ((V) & 0x3fffff))
#define DFP_IS_TRAPPING_NAN(V)   (((((V) >> 51) & 0xfff) == 0xffe) &&  \
                                  ((V) & 0x7ffffffffffffull))
#define XFP_IS_NAN(V)             ((((V)->sign_exp & 0x7fff) == 0x7fff) && \
                                   (t_uint64)((V)->frac << 1))
#define XFP_IS_TRAPPING_NAN(V)   ((((V)->sign_exp) & 0x7fff) &&        \
                                  ((((V)->frac) & ~(0x4000000000000000ull)) << 1) && \
                                  (((V)->frac) == ((V)->frac & ~(0x4000000000000000ull))))
#define PACK_DFP(SIGN,EXP,FRAC) ((((t_uint64)(SIGN))<<63) +      \
                                 (((t_uint64)(EXP))<<52) +       \
                                 ((t_uint64)(FRAC)))
#define PACK_SFP(SIGN,EXP,FRAC) (((uint32)(SIGN)<<31) +  \
                                 ((uint32)(EXP)<<23) +   \
                                 ((uint32)(FRAC)))
#define PACK_XFP(SIGN,EXP,FRAC,V)    do {               \
        (V)->frac = (FRAC);                             \
        (V)->sign_exp = ((uint16)(SIGN) << 15) + (EXP); \
        (V)->s = FALSE;                                 \
    } while (0)

#define PACK_XFP_S(SIGN,EXP,FRAC,S,V)   do {             \
        (V)->frac = (FRAC);                              \
        (V)->sign_exp = ((uint16)(SIGN) << 15) + (EXP);  \
        (V)->s = (S) != 0;                               \
    } while (0)

#define MAU_RM        ((RM)((mau_state.asr >> 22) & 3))

typedef enum {
    M_ADD   = 0x02,
    M_SUB   = 0x03,
    M_DIV   = 0x04,
    M_REM   = 0x05,
    M_MUL   = 0x06,
    M_MOVE  = 0x07,
    M_RDASR = 0x08,
    M_WRASR = 0x09,
    M_CMP   = 0x0a,
    M_CMPE  = 0x0b,
    M_ABS   = 0x0c,
    M_SQRT  = 0x0d,
    M_RTOI  = 0x0e,
    M_FTOI  = 0x0f,
    M_ITOF  = 0x10,
    M_DTOF  = 0x11,
    M_FTOD  = 0x12,
    M_NOP   = 0x13,
    M_EROF  = 0x14,
    M_NEG   = 0x17,
    M_LDR   = 0x18,
    M_CMPS  = 0x1a,
    M_CMPES = 0x1b
} mau_opcodes;

typedef enum {
    M_OP3_F0_SINGLE,
    M_OP3_F1_SINGLE,
    M_OP3_F2_SINGLE,
    M_OP3_F3_SINGLE,
    M_OP3_F0_DOUBLE,
    M_OP3_F1_DOUBLE,
    M_OP3_F2_DOUBLE,
    M_OP3_F3_DOUBLE,
    M_OP3_F0_TRIPLE,
    M_OP3_F1_TRIPLE,
    M_OP3_F2_TRIPLE,
    M_OP3_F3_TRIPLE,
    M_OP3_MEM_SINGLE,
    M_OP3_MEM_DOUBLE,
    M_OP3_MEM_TRIPLE,
    M_OP3_NONE
} op3_spec;

/* Specifier bytes for Operands 1 and 2 */
typedef enum {
    M_OP_F0,
    M_OP_F1,
    M_OP_F2,
    M_OP_F3,
    M_OP_MEM_SINGLE,
    M_OP_MEM_DOUBLE,
    M_OP_MEM_TRIPLE,
    M_OP_NONE
} op_spec;

/*
 * 128-bit value
 */
typedef struct {
    t_uint64 low;
    t_uint64 high;
} t_mau_128;

/*
 * Not-a-Number Type
 */
typedef struct {
    t_bool sign;
    t_uint64 high;
    t_uint64 low;
} T_NAN;

/*
 * Extended Precision (80 bits).
 *
 * Note that an undocumented feature of the WE32106 requires the use
 * of uint32 rather than uint16 for the sign and exponent components
 * of the struct. Although bits 80-95 are "unused", several
 * diagnostics actually expect these bits to be moved and preserved on
 * word transfers. They are ignored and discarded by math routines,
 * however.
 *
 * The 's' field holds the Sticky bit used by rounding.
 */
typedef struct {
    uint32 sign_exp;  /* Sign and Exponent */
    t_uint64 frac;    /* Fraction/Significand/Mantissa */
    t_bool s;         /* Sticky bit */
} XFP;

typedef struct {
    uint32 h;
    t_uint64 l;
} DEC;

/*
 * Supported rounding modes.
 */
typedef enum {
    ROUND_NEAREST,
    ROUND_PLUS_INF,
    ROUND_MINUS_INF,
    ROUND_ZERO
} RM;

/*
 * Double Precision (64 bits)
 */
typedef t_uint64 DFP;

/*
 * Single Precision (32 bits)
 */
typedef uint32 SFP;

/*
 * MAU state
 */

typedef struct {
    uint32   cmd;
    /* Exception  */
    uint32   exception;
    /* Status register */
    uint32   asr;
    t_bool   trapping_nan;
    /* Generate a Non-Trapping NaN */
    t_bool   ntnan;
    /* Source (from broadcast) */
    uint32   src;
    /* Destination (from broadcast) */
    uint32   dst;
    uint8    opcode;
    uint8    op1;
    uint8    op2;
    uint8    op3;
    /* Data Register */
    XFP      dr;
    /* Operand Registers */
    XFP      f0;
    XFP      f1;
    XFP      f2;
    XFP      f3;
} MAU_STATE;

t_stat mau_reset(DEVICE *dptr);
t_stat mau_attach(UNIT *uptr, CONST char *cptr);
t_stat mau_detach(UNIT *uptr);
t_stat mau_broadcast(uint32 cmd, uint32 src, uint32 dst);
CONST char *mau_description(DEVICE *dptr);
t_stat mau_broadcast(uint32 cmd, uint32 src, uint32 dst);

#endif /* _3B2_400_MAU_H_ */
