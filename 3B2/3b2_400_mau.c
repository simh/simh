/* 3b2_400_mau.c: AT&T 3B2 Model 400 Math Acceleration Unit (WE32106 MAU)
   Implementation

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
*/

#include <math.h>

#include "3b2_defs.h"
#include "3b2_400_mau.h"

#define   MAU_ID   0        /* Coprocessor ID of MAU */

#define   TININESS_BEFORE_ROUNDING   TRUE

/* Static function declarations */
static SIM_INLINE void mau_case_div_zero(XFP *op1, XFP *op2, XFP *result);

static SIM_INLINE void mau_exc(uint32 flag, uint32 mask);
static SIM_INLINE void abort_on_fault();
static SIM_INLINE void mau_decode(uint32 cmd, uint32 src, uint32 dst);
static SIM_INLINE t_bool le_128(t_uint64 a0, t_uint64 a1, t_uint64 b0, t_uint64 b1);
static SIM_INLINE t_bool eq_128(t_uint64 a0, t_uint64 a1, t_uint64 b0, t_uint64 b1);
static SIM_INLINE t_bool lt_128(t_uint64 a0, t_uint64 a1, t_uint64 b0, t_uint64 b1);
static uint8 leading_zeros(uint32 val);
static uint8 leading_zeros_64(t_int64 val);
static void shift_right_32_jamming(uint32 val, int16 count, uint32 *result);
static void shift_right_64_jamming(t_uint64 val, int16 count, t_uint64 *result);
static void shift_right_extra_64_jamming(t_uint64 val_a, t_uint64 val_b, int16 count,
                                         t_uint64 *r_a, t_uint64 *r_b);
static void shift_right_128_jamming(t_uint64 val_a, t_uint64 val_b, int16 count,
                                    t_uint64 *r_a, t_uint64 *r_b);
static void short_shift_left_128(t_uint64 val_a, t_uint64 val_b, int16 count,
                                 t_uint64 *r_a, t_uint64 *r_b);
static void shift_right_128(t_uint64 val_a, t_uint64 val_b, int16 count,
                            t_uint64 *r_a, t_uint64 *r_b);
static void add_128(t_uint64 a0, t_uint64 a1,
                    t_uint64 b0, t_uint64 b1,
                    t_uint64 *r_low, t_uint64 *r_high);
static void sub_128(t_uint64 a0, t_uint64 a1,
                    t_uint64 b0, t_uint64 b1,
                    t_uint64 *r_low, t_uint64 *r_high);
static void mul_64_to_128(t_uint64 a, t_uint64 b, t_uint64 *r_low, t_uint64 *r_high);
static void mul_64_by_shifted_32_to_128(t_uint64 a, uint32 b, t_mau_128 *result);
static t_uint64 estimate_div_128_to_64(t_uint64 a0, t_uint64 a1, t_uint64 b);
static uint32 estimate_sqrt_32(int16 a_exp, uint32 a);

static uint32 round_pack_int(t_bool sign, t_uint64 frac, RM rounding_mode);
static t_int64 round_pack_int64(t_bool sign,
                                t_uint64 abs_0, t_uint64 abs_1,
                                RM rounding_mode);

static SFP round_pack_sfp(t_bool sign, int16 exp,
                          uint32 frac, RM rounding_mode);
static DFP round_pack_dfp(t_bool sign, int16 exp, t_uint64 frac,
                          t_bool xfp_sticky, RM rounding_mode);
static void round_pack_xfp(t_bool sign, int32 exp,
                           t_uint64 frac_a, t_uint64 frac_b,
                           RM rounding_mode, XFP *result);
static void propagate_xfp_nan(XFP *a, XFP *b, XFP *result);
static void propagate_xfp_nan_128(XFP* a, XFP* b, t_mau_128* result);
static void normalize_round_pack_xfp(t_bool sign, int32 exp,
                                     t_uint64 frac_0, t_uint64 frac_1,
                                     RM rounding_mode, XFP *result);
static void normalize_sfp_subnormal(uint32 in_frac, int16 *out_exp, uint32 *out_frac);
static void normalize_dfp_subnormal(t_uint64 in_frac, int16 *out_exp, t_uint64 *out_frac);
static void normalize_xfp_subnormal(t_uint64 in_frac, int32 *out_exp, t_uint64 *out_frac);

static T_NAN sfp_to_common_nan(SFP val);
static T_NAN dfp_to_common_nan(DFP val);
static T_NAN xfp_to_common_nan(XFP *val);
static SFP common_nan_to_sfp(T_NAN nan);
static DFP common_nan_to_dfp(T_NAN nan);
static void common_nan_to_xfp(T_NAN nan, XFP *result);

static void sfp_to_xfp(SFP val, XFP *result);
static void dfp_to_xfp(DFP val, XFP *result);
static SFP xfp_to_sfp(XFP *val, RM rounding_mode);
static DFP xfp_to_dfp(XFP *val, RM rounding_mode);

static uint32 xfp_eq(XFP *a, XFP *b);
static uint32 xfp_lt(XFP *a, XFP *b);

static void xfp_cmp(XFP *a, XFP *b);
static void xfp_cmpe(XFP *a, XFP *b);
static void xfp_cmps(XFP *a, XFP *b);
static void xfp_cmpes(XFP *a, XFP *b);
static void xfp_add(XFP *a, XFP *b, XFP *result, RM rounding_mode);
static void xfp_sub(XFP *a, XFP *b, XFP *result, RM rounding_mode);
static void xfp_mul(XFP *a, XFP *b, XFP *result, RM rounding_mode);
static void xfp_div(XFP *a, XFP *b, XFP *result, RM rounding_mode);
static void xfp_sqrt(XFP *a, XFP *result, RM rounding_mode);
static void xfp_remainder(XFP *a, XFP *b, XFP *result, RM rounding_mode);

static void load_src_op(uint8 op, XFP *xfp);
static void load_op1_decimal(DEC *d);
static void store_op3_int(uint32 val);
static void store_op3_decimal(DEC *d);
static void store_op3(XFP *xfp);

static void mau_rdasr();
static void mau_wrasr();
static void mau_move();
static void mau_cmp();
static void mau_cmps();
static void mau_cmpe();
static void mau_cmpes();
static void mau_ldr();
static void mau_erof();
static void mau_rtoi();
static void mau_ftoi();
static void mau_dtof();
static void mau_ftod();
static void mau_add();
static void mau_sub();
static void mau_mul();
static void mau_div();
static void mau_neg();
static void mau_abs();
static void mau_sqrt();
static void mau_itof();
static void mau_remainder();

static void mau_execute();

UNIT mau_unit = { UDATA(NULL, 0, 0) };

MAU_STATE mau_state;

BITFIELD asr_bits[] = {
    BITNCF(5),
    BIT(PR),
    BIT(QS),
    BIT(US),
    BIT(OS),
    BIT(IS),
    BIT(PM),
    BIT(QM),
    BIT(UM),
    BIT(OM),
    BIT(IM),
    BITNCF(1),
    BIT(UO),
    BIT(CSC),
    BIT(PS),
    BIT(IO),
    BIT(Z),
    BIT(N),
    BITFFMT(RC,2,%d),
    BIT(NTNC),
    BIT(ECP),
    BITNCF(5),
    BIT(RA),
    ENDBITS
};

REG mau_reg[] = {
    { HRDATAD  (CMD, mau_state.cmd,       32, "Command Word")  },
    { HRDATADF (ASR, mau_state.asr,       32, "ASR", asr_bits) },
    { HRDATAD  (OPCODE, mau_state.opcode, 8,  "Opcode")        },
    { HRDATAD  (OP1, mau_state.op1,       8,  "Operand 1")     },
    { HRDATAD  (OP2, mau_state.op2,       8,  "Operand 2")     },
    { HRDATAD  (OP3, mau_state.op3,       8,  "Operand 3")     },
    { NULL }
};

MTAB mau_mod[] = {
    { UNIT_EXHALT, UNIT_EXHALT, "Halt on Exception", "EXHALT",
      NULL, NULL, NULL, "Enables Halt on floating point exceptions" },
    { UNIT_EXHALT, 0, "No halt on Exception", "NOEXHALT",
      NULL, NULL, NULL, "Disables Halt on floating point exceptions" },
    { 0 }
};

static DEBTAB mau_debug[] = {
    { "DECODE", DECODE_DBG,   "Decode"        },
    { "TRACE",  TRACE_DBG,    "Call Trace"    },
    { NULL }
};

DEVICE mau_dev = {
    "MAU",                          /* name */
    &mau_unit,                      /* units */
    mau_reg,                        /* registers */
    mau_mod,                        /* modifiers */
    1,                              /* #units */
    16,                             /* address radix */
    32,                             /* address width */
    1,                              /* address incr. */
    16,                             /* data radix */
    8,                              /* data width */
    NULL,                           /* examine routine */
    NULL,                           /* deposit routine */
    &mau_reset,                     /* reset routine */
    NULL,                           /* boot routine */
    NULL,                           /* attach routine */
    NULL,                           /* detach routine */
    NULL,                           /* context */
    DEV_DISABLE|DEV_DIS|DEV_DEBUG,  /* flags */
    0,                              /* debug control flags */
    mau_debug,                      /* debug flag names */
    NULL,                           /* memory size change */
    NULL,                           /* logical name */
    NULL,                           /* help routine */
    NULL,                           /* attach help routine */
    NULL,                           /* help context */
    &mau_description                /* device description */
};

XFP INF = {
    0x7fff,
    0x0000000000000000ull,
    0
};

XFP TRAPPING_NAN = {
    0x7fff,
    0x7fffffffffffffffull,
    0
};

/* Generated Non-Trapping NaN
 * p. 2-8 "When the MAU generates a nontrapping NaN, J+fraction
 * contains all 1s.  The MAU never generates a trapping NaN."
 */
XFP GEN_NONTRAPPING_NAN = {
    0x7fff,
    0xffffffffffffffffull,
    0
};

CONST char *mau_op_names[32] = {
    "0x00",  "0x01",  "ADD",  "SUB",   "DIV",  "REM",  "MUL",  "MOVE",    /* 00-07 */
    "RDASR", "WRASR", "CMP",  "CMPE",  "ABS",  "SQRT", "RTOI", "FTOI",    /* 08-0F */
    "ITOF",  "DTOF",  "FTOD", "NOP",   "EROF", "0x15", "0x16", "NEG",     /* 10-17 */
    "LDR",   "0x19",  "CMPS", "CMPES", "0x1C", "0x1D", "0x1E", "0x1F"     /* 18-1F */
};

CONST char *src_op_names[8] = {
    "F0", "F1", "F2", "F3",
    "MEM S", "MEM D", "MEM X", "N/A"
};

CONST char *dst_op_names[16] = {
    "F0 S", "F1 S", "F2 S", "F3 S",
    "F0 D", "F1 D", "F2 D", "F3 D",
    "F0 X", "F1 X", "F2 X", "F3 X",
    "MEM S", "MEM D", "MEM X", "N/A"
};

/*
 * Special Cases
 * -------------
 *
 * The handling of combinations of special input values is specified
 * in the "WE32106 Math Acceleration Unit Information Manual"
 * pp. 5-3--5-5.
 *
 * Each of these "special case" routines can be called by math
 * functions based on a combination of the input values.
 *
 * (At the moment, only divide-by-zero is explicitly called out here
 * as a special case)
 */

static SIM_INLINE void mau_case_div_zero(XFP *op1, XFP *op2, XFP *result)
{
    mau_state.asr |= MAU_ASR_QS;

    if (mau_state.asr & MAU_ASR_QM) {
        mau_state.asr |= MAU_ASR_ECP;
        PACK_XFP(0, 0x7fff, 0x8000000000000000ull, result);
    } else {
        if (XFP_SIGN(op1) ^ XFP_SIGN(op2)) {
            PACK_XFP(1, INF.sign_exp, INF.frac, result);
        } else {
            PACK_XFP(0, INF.sign_exp, INF.frac, result);
        }
    }
}

static SIM_INLINE void mau_exc(uint32 flag, uint32 mask)
{
    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [mau_exc] asr=%08x flag=%08x mask=%08x\n",
              R[NUM_PC], mau_state.asr, flag, mask);

    mau_state.asr |= flag;

    /*
     * page 2-14: NTNC bit is checked if an Invalid Operation
     * exception occurs while the Invalid Operation Mask bit is
     * clear. If NTNC is set to 1, an exception occurs and bit 9
     * (IS) is set. If NTNC is set to 0, no exception occurs,
     * and a nontraping NaN is generated.
     */
    if (flag == MAU_ASR_IS && (mau_state.asr & MAU_ASR_IM) == 0) {
        if (mau_state.asr & MAU_ASR_NTNC) {
            mau_state.asr |= MAU_ASR_ECP;
        } else {
            mau_state.ntnan = TRUE;
        }
        return;
    }

    if (mau_state.asr & mask) {
        mau_state.asr |= MAU_ASR_ECP;
    }
}

/*
 * Returns true if an exceptional condition is present.
 */
static SIM_INLINE t_bool mau_exception_present()
{

    return mau_state.asr & MAU_ASR_ECP &&
        (((mau_state.asr & MAU_ASR_IS) && ((mau_state.asr & MAU_ASR_IM) ||
                                           (mau_state.asr & MAU_ASR_NTNC))) ||
         ((mau_state.asr & MAU_ASR_US) && (mau_state.asr & MAU_ASR_UM)) ||
         ((mau_state.asr & MAU_ASR_OS) && (mau_state.asr & MAU_ASR_OM)) ||
         ((mau_state.asr & MAU_ASR_PS) && (mau_state.asr & MAU_ASR_PM)) ||
         ((mau_state.asr & MAU_ASR_QS) && (mau_state.asr & MAU_ASR_QM)));
}

static SIM_INLINE void abort_on_fault()
{
    switch(mau_state.opcode) {
    case M_NOP:
    case M_RDASR:
    case M_WRASR:
    case M_EROF:
    case M_LDR:
        return;
    default:
        /*
         * Integer overflow is non-maskable in the MAU, but generates an Integer
         * Overflow exception to be handled by the WE32100 CPU (if not masked
         * in the CPU's PSW).
         */
        if ((mau_state.asr & MAU_ASR_IO) && (R[NUM_PSW] & PSW_OE_MASK)) {
            if (mau_unit.flags & UNIT_EXHALT) {
                stop_reason = STOP_EX;
            }
            sim_debug(TRACE_DBG, &mau_dev,
                      "[%08x] [abort_on_fault] Aborting on un-maskable overflow fault. ASR=%08x\n",
                      R[NUM_PC], mau_state.asr);
            cpu_abort(NORMAL_EXCEPTION, INTEGER_OVERFLOW);
        }

        /* Otherwise, check for other exceptions. */
        if (mau_exception_present()) {
            if (mau_unit.flags & UNIT_EXHALT) {
                stop_reason = STOP_EX;
            }
            sim_debug(TRACE_DBG, &mau_dev,
                      "[%08x] [abort_on_fault] Aborting on ECP fault. ASR=%08x\n",
                      R[NUM_PC], mau_state.asr);
            cpu_abort(NORMAL_EXCEPTION, EXTERNAL_MEMORY_FAULT);
        }

        break;
    }
}

/*
 * Clears N and Z flags in the ASR if appropriate.
 */
static void clear_asr()
{
    mau_state.ntnan = FALSE;

    switch(mau_state.opcode) {
    case M_NOP:
    case M_RDASR:
    case M_WRASR:
    case M_EROF:
        return;
    default:
        mau_state.asr &= ~(MAU_ASR_Z|MAU_ASR_N|MAU_ASR_ECP);
        break;
    }
}

/*
 * Returns true if the 'nz' flags should be set.
 *
 * Note: There is an undocumented feature of the WE32106 expressed
 * here. If an exception has occured, the Z and N flags are not to be
 * set!
 */
static t_bool set_nz()
{

    switch(mau_state.opcode) {
    case M_NOP:
    case M_RDASR:
    case M_WRASR:
    case M_EROF:
        return FALSE;
    default:
        return (mau_state.asr & MAU_ASR_ECP) == 0;
    }
}

t_stat mau_reset(DEVICE *dptr)
{
    memset(&mau_state, 0, sizeof(MAU_STATE));
    return SCPE_OK;
}

/*************************************************************************
 * Utility Functions
 ************************************************************************/

/*
 * Compare two 128-bit values a and b. Rturns true if a <= b
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static SIM_INLINE t_bool le_128(t_uint64 a0, t_uint64 a1, t_uint64 b0, t_uint64 b1)
{
    return (a0 < b0) || ((a0 == b0) && (a1 <= b1));
}

/*
 * Compare two 128-bit values a and b. Returns true if a = b
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static SIM_INLINE t_bool eq_128(t_uint64 a0, t_uint64 a1, t_uint64 b0, t_uint64 b1)
{
    return (a0 == b0) && (a1 == b1);
}

/*
 * Compare two 128-bit values a and b. Returns true if a < b
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static SIM_INLINE t_bool lt_128(t_uint64 a0, t_uint64 a1, t_uint64 b0, t_uint64 b1)
{
    return (a0 < b0) || ((a0 == b0) && (a1 < b1));
}

/*
 * Return the number of leading binary zeros in an unsigned 32-bit
 * value.
 *
 * Algorithm couresty of "Hacker's Delight" by Henry S. Warren.
 */
static uint8 leading_zeros(uint32 val)
{
    unsigned n = 0;

    if (val <= 0x0000ffff) {
        n += 16;
        val <<= 16;
    }
    if (val <= 0x00ffffff) {
        n += 8;
        val <<= 8;
    }
    if (val <= 0x0fffffff) {
        n += 4;
        val <<= 4;
    }
    if (val <= 0x3fffffff) {
        n += 2;
        val <<= 2;
    }
    if (val <= 0x7fffffff) {
        n++;
    }

    return n;
}

/*
 * Return the number of leading binary zeros in a signed 64-bit
 * value.
 */
static uint8 leading_zeros_64(t_int64 val)
{
    uint8 n = 0;

    if (val == 0) {
        return 64;
    }

    while (1) {
        if (val < 0) break;

        n++;

        val <<= 1;
    }

    return n;
}

/*
 * Shift a 32-bit unsigned value, 'val', right by 'count' bits. If any
 * non-zero bits are shifted off, they are "jammed" into the least
 * significant bit of the result by setting the least significant bit
 * to 1.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void shift_right_32_jamming(uint32 val, int16 count, uint32 *result)
{
    uint32 tmp;

    if (count == 0) {
        tmp = val;
    } else if (count < 32) {
        tmp = (val >> count) | ((val << ((-count) & 31)) != 0);
    } else {
        tmp = (val != 0);
    }

    *result = tmp;
}

/*
 * Shift a 64-bit unsigned value, 'val', right by 'count' bits. If any
 * non-zero bits are shifted off, they are "jammed" into the least
 * significant bit of the result by setting the least significant bit
 * to 1.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void shift_right_64_jamming(t_uint64 val, int16 count, t_uint64 *result)
{
    t_uint64 tmp;

    if (count == 0) {
        tmp = val;
    } else if (count < 64) {
        tmp = (val >> count) | ((val << ((-count) & 63)) != 0);
    } else {
        tmp = (val != 0);
    }

    *result = tmp;
}

/*
 * Shifts the 128-bit value formed by concatenating val_a and val_b
 * right by 64 _plus_ the number of bits given in 'count'.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void shift_right_extra_64_jamming(t_uint64 val_a, t_uint64 val_b, int16 count,
                                         t_uint64 *r_a, t_uint64 *r_b)
{
    t_uint64 a, b;
    int8 neg_count = (-count) & 63;

    if (count == 0) {
        b = val_b;
        a = val_a;
    } else if (count < 64) {
        b = (val_a << neg_count) | (val_b != 0);
        a = val_a >> count;
    } else {
        if (count == 64) {
            b = val_a | (val_b != 0);
        } else {
            b = ((val_a | val_b) != 0);
        }
        a = 0;
    }

    *r_a = a;
    *r_b = b;
}

/*
 * Shift the 128-bit value formed by val_a and val_b right by
 * 64 plus the number of bits given in count.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void shift_right_128_jamming(t_uint64 val_a, t_uint64 val_b, int16 count,
                                    t_uint64 *r_a, t_uint64 *r_b)
{
    t_uint64 tmp_a, tmp_b;
    int8 neg_count = (-count) & 63;

    if (count == 0) {
        tmp_a = val_a;
        tmp_b = val_b;
    } else if (count < 64) {
        tmp_a = (val_a >> count);
        tmp_b = (val_a << neg_count) | (val_b != 0);
    } else {
        if (count == 64) {
            tmp_b = val_a | (val_b != 0);
        } else {
            tmp_b = ((val_a | val_b) != 0);
        }
        tmp_a = 0;
    }

    *r_a = tmp_a;
    *r_b = tmp_b;
}

/*
 * Shifts the 128-bit value formed by val_a and val_b left by the
 * number of bits given in count.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void short_shift_left_128(t_uint64 val_a, t_uint64 val_b, int16 count,
                                 t_uint64 *r_a, t_uint64 *r_b)
{
    *r_b = val_b << count;
    if (count == 0) {
        *r_a = val_a;
    } else {
        *r_a = (val_a << count) | (val_b >> ((-count) & 63));
    }
}

/*
 * Shifts the 128-bit value formed by val_a and val_b right by the
 * number of bits given ihn 'count'. Any bits shifted off are lost.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void shift_right_128(t_uint64 val_a, t_uint64 val_b, int16 count,
                            t_uint64 *r_a, t_uint64 *r_b)
{
    t_uint64 tmp_a, tmp_b;
    int8 neg_count;

    neg_count = (- count) & 63;

    if (count == 0) {
        tmp_a = val_a;
        tmp_b = val_b;
    } else if (count < 64) {
        tmp_a = val_a >> count;
        tmp_b = (val_a << neg_count) | (val_b >> count);
    } else {
        tmp_a = 0;
        tmp_b = (count < 128) ? (val_a >> (count & 63)) : 0;
    }

    *r_a = tmp_a;
    *r_b = tmp_b;
}

/*
 * Add two 128-bit values.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void add_128(t_uint64 a0, t_uint64 a1,
                    t_uint64 b0, t_uint64 b1,
                    t_uint64 *r_low, t_uint64 *r_high)
{
    t_uint64 tmp;

    tmp = a1 + b1;
    *r_high = tmp;
    *r_low = a0 + b0 + (tmp < a1);
}

/*
 * Subract two 128-bit values.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void sub_128(t_uint64 a0, t_uint64 a1,
                    t_uint64 b0, t_uint64 b1,
                    t_uint64 *r_low, t_uint64 *r_high)
{
    *r_high = a1 - b1;
    *r_low = a0 - b0 - (a1 < b1);
}

/*
 * Multiplies a by b to obtain a 128-bit product. The product is
 * broken into two 64-bit pieces which are stored at r_low and r_high.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void mul_64_to_128(t_uint64 a, t_uint64 b, t_uint64 *r_low, t_uint64 *r_high)
{
    uint32 a_high, a_low, b_high, b_low;
    t_uint64 rl, rm_a, rm_b, rh;

    a_low = (uint32)a;
    a_high = a >> 32;

    b_low = (uint32)b;
    b_high = b >> 32;

    rh = ((t_uint64) a_low) * b_low;
    rm_a = ((t_uint64) a_low) * b_high;
    rm_b = ((t_uint64) a_high) * b_low;
    rl = ((t_uint64) a_high) * b_high;

    rm_a += rm_b;

    rl += (((t_uint64)(rm_a < rm_b)) << 32) + (rm_a >> 32);
    rm_a <<= 32;
    rh += rm_a;
    rl += (rh < rm_a);

    *r_high = rh;
    *r_low = rl;
}

/*
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void mul_64_by_shifted_32_to_128(t_uint64 a, uint32 b, t_mau_128 *result)
{
    t_uint64 mid;

    mid = (t_uint64)(uint32) a * b;
    result->low = mid << 32;
    result->high = (t_uint64)(uint32)(a >> 32) * b + (mid >> 32);
}

/*
 * Returns an approximation of the 64-bit integer value obtained by
 * dividing 'b' into the 128-bit value 'a0' and 'a1'.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static t_uint64 estimate_div_128_to_64(t_uint64 a0, t_uint64 a1, t_uint64 b)
{
    t_uint64 b0, b1;
    t_uint64 rem0, rem1, term0, term1;
    t_uint64 z;

    if (b <= a0) {
        return 0xffffffffffffffffull;
    }

    b0 = b >> 32;
    z = (b0 << 32 <= a0) ? 0xffffffff00000000ull : (a0 / b0) << 32;

    mul_64_to_128( b, z, &term0, &term1 );

    sub_128( a0, a1, term0, term1, &rem0, &rem1 );

    while (((int64_t)rem0) < 0) {
        z -= 0x100000000ull;
        b1 = b << 32;
        add_128(rem0, rem1, b0, b1, &rem0, &rem1);
    }

    rem0 = (rem0 << 32) | (rem1 >> 32);
    z |= (b0<<32 <= rem0) ? 0xffffffff : rem0 / b0;

    return z;
}

/*
 * Returns an approximation of the square root of the 32-bit
 * value 'a'.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static uint32 estimate_sqrt_32(int16 a_exp, uint32 a)
{
    static const uint16 sqrt_odd_adjust[] = {
        0x0004, 0x0022, 0x005D, 0x00B1, 0x011D, 0x019F, 0x0236, 0x02E0,
        0x039C, 0x0468, 0x0545, 0x0631, 0x072B, 0x0832, 0x0946, 0x0A67
    };

    static const uint16 sqrt_even_adjust[] = {
        0x0A2D, 0x08AF, 0x075A, 0x0629, 0x051A, 0x0429, 0x0356, 0x029E,
        0x0200, 0x0179, 0x0109, 0x00AF, 0x0068, 0x0034, 0x0012, 0x0002
    };

    int8 index;
    uint32 z;

    index = (a >> 27) & 0xf;

    if (a_exp & 1) {
        z = 0x4000 + (a >> 17) - sqrt_odd_adjust[index];
        z = ((a / z) << 14) + (z << 15);
        a >>= 1;
    }
    else {
        z = 0x8000 + (a >> 17) - sqrt_even_adjust[index];
        z = a / z + z;
        z = (0x20000 <= z) ? 0xFFFF8000 : ( z<<15 );
        if ( z <= a ) return (uint32) (((int32) a) >> 1);
    }

    return ((uint32) ((((t_uint64) a )<<31 ) / z)) + (z >> 1);
}

static uint32 approx_recip_sqrt_32(uint32 oddExpA, uint32 a)
{
    int index;
    uint16 eps, r0;
    uint32 ESqrR0;
    uint32 sigma0;
    uint32 r;
    uint32 sqrSigma0;

    static const uint16 softfloat_approxRecipSqrt_1k0s[16] = {
        0xB4C9, 0xFFAB, 0xAA7D, 0xF11C, 0xA1C5, 0xE4C7, 0x9A43, 0xDA29,
        0x93B5, 0xD0E5, 0x8DED, 0xC8B7, 0x88C6, 0xC16D, 0x8424, 0xBAE1
    };
    static const uint16 softfloat_approxRecipSqrt_1k1s[16] = {
        0xA5A5, 0xEA42, 0x8C21, 0xC62D, 0x788F, 0xAA7F, 0x6928, 0x94B6,
        0x5CC7, 0x8335, 0x52A6, 0x74E2, 0x4A3E, 0x68FE, 0x432B, 0x5EFD
    };

    index = (a>>27 & 0xE) + oddExpA;
    eps = (uint16) (a>>12);
    r0 = softfloat_approxRecipSqrt_1k0s[index]
             - ((softfloat_approxRecipSqrt_1k1s[index] * (uint32) eps)
                    >>20);
    ESqrR0 = (uint32) r0 * r0;
    if ( ! oddExpA ) ESqrR0 <<= 1;
    sigma0 = ~(uint32) (((uint32) ESqrR0 * (t_uint64) a)>>23);
    r = ((uint32) r0<<16) + ((r0 * (t_uint64) sigma0)>>25);
    sqrSigma0 = ((t_uint64) sigma0 * sigma0)>>32;
    r += ((uint32) ((r>>1) + (r>>3) - ((uint32) r0<<14))
              * (t_uint64) sqrSigma0)
             >>48;
    if ( ! (r & 0x80000000) ) r = 0x80000000;
    return r;
}

/*
 * Return the properly rounded 32-bit integer corresponding to 'sign'
 * and 'frac'.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static uint32 round_pack_int(t_bool sign, t_uint64 frac, RM rounding_mode)
{
    int8 round_increment, round_bits;
    int32 result;

    round_increment = 0x40;

    if (!(rounding_mode == ROUND_NEAREST)) {
        if (rounding_mode == ROUND_ZERO) {
            round_increment = 0;
        } else {
            round_increment = 0x7f;
            if (sign) {
                if (rounding_mode == ROUND_PLUS_INF) {
                    round_increment = 0;
                }
            } else {
                if (rounding_mode == ROUND_MINUS_INF) {
                    round_increment = 0;
                }
            }
        }
    }

    round_bits = frac & 0x7f;
    frac = (frac + round_increment) >> 7;
    frac &= ~((t_uint64)((round_bits ^ 0x40) == 0) &
              (t_uint64)(rounding_mode == ROUND_NEAREST));

    result = (int32)frac;

    if (sign) {
        result = -result;
    }

    if ((frac >> 32) || (result && ((result < 0) ^ sign))) {
        mau_exc(MAU_ASR_IO, MAU_ASR_OM);  /* Integer overflow */
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);  /* Inexact */
        return sign ? (int32) 0x80000000 : 0x7fffffff;
    }

    if (round_bits) {
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);
    }

    return result;
}

/*
 * Return the properly rounded 64-bit integer corresponding to 'sign'
 * and 'frac'.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static t_int64 round_pack_int64(t_bool sign,
                                t_uint64 abs_0, t_uint64 abs_1,
                                RM rounding_mode)
{
    t_bool increment;
    int64_t z;

    increment = (t_int64)abs_1 < 0;

    if (rounding_mode != ROUND_NEAREST) {
        if (rounding_mode == ROUND_ZERO) {
            increment = 0;
        } else {
            if (sign) {
                increment = (rounding_mode == ROUND_MINUS_INF) && abs_1;
            } else {
                increment = (rounding_mode == ROUND_PLUS_INF) && abs_1;
            }
        }
    }

    if (increment) {
        ++abs_0;
        if (abs_0 == 0) {
            /* Overflow */
            mau_exc(MAU_ASR_OS, MAU_ASR_OM);
            return sign ? 0x8000000000000000ull : 0x7fffffffffffffffull;
        }
        abs_0 &= ~((t_uint64)((abs_1 << 1) == 0) &
                   (t_uint64)(rounding_mode == ROUND_NEAREST));
    }

    z = abs_0;
    if (sign) {
        z = -z;
    }
    if (z && ((z < 0) ^ sign)) {
        /* Overflow */
        mau_exc(MAU_ASR_OS, MAU_ASR_OM);
        return sign ? 0x8000000000000000ull : 0x7fffffffffffffffull;
    }

    if (abs_1) {
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);
    }

    return z;
}

/*
 * Return a properly rounded 32-bit floating point value, given a sign
 * bit, exponent, fractional part, and a rounding mode.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static SFP round_pack_sfp(t_bool sign, int16 exp, uint32 frac, RM rounding_mode)
{
    int8 round_increment, round_bits;
    uint8 is_tiny;

    is_tiny = 0;
    round_increment = 0x40;

    if (rounding_mode != ROUND_NEAREST) {
        if (rounding_mode == ROUND_ZERO) {
            round_increment = 0;
        } else {
            if (sign) {
                if (rounding_mode == ROUND_PLUS_INF) {
                    round_increment = 0;
                }
            } else {
                if (rounding_mode == ROUND_MINUS_INF) {
                    round_increment = 0;
                }
            }
        }
    }

    round_bits = frac & 0x7f;

    if (0xfd <= (uint16) exp) {
        if ((0xfd < exp) ||
            (exp == 0xfd && (int32)(frac + round_increment) < 0)) {
            mau_exc(MAU_ASR_OS, MAU_ASR_OM);
            mau_exc(MAU_ASR_PS, MAU_ASR_PM);
            return PACK_SFP(sign, 0xff, 0) - (round_increment == 0);
        }
        if (exp < 0) {
            is_tiny = (TININESS_BEFORE_ROUNDING ||
                       ((exp < -1) ||
                        (frac + round_increment < 0x80000000)));
            shift_right_32_jamming(frac, -exp, &frac);
            exp = 0;
            round_bits = frac & 0x7f;
            if (is_tiny && round_bits) {
                mau_exc(MAU_ASR_US, MAU_ASR_UM);
            }
        }
    }

    if (round_bits) {
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);
    }

    frac = (frac + round_increment) >> 7;
    frac &= ~((t_uint64)((round_bits ^ 0x40) == 0) &
              (t_uint64)(rounding_mode == ROUND_NEAREST));
    if (frac == 0) {
        exp = 0;
    }

    return PACK_SFP(sign, exp, frac);
}

/*
 * Return a properly rounded 64-bit floating point value, given a sign
 * bit, exponent, fractional part, and a rounding mode.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static DFP round_pack_dfp(t_bool sign, int16 exp, t_uint64 frac,
                          t_bool xfp_sticky, RM rounding_mode)
{
    int16 round_increment, round_bits;
    t_bool lsb, round, sticky;
    uint8 is_tiny;

    is_tiny = 0;
    round_increment = 0;

    if (rounding_mode != ROUND_NEAREST) {
        if (rounding_mode == ROUND_ZERO) {
            round_increment = 0;
        } else {
            round_increment = 0x7ff;
            if (sign) {
                if (rounding_mode == ROUND_PLUS_INF) {
                    round_increment = 0;
                }
            } else {
                if (rounding_mode == ROUND_MINUS_INF) {
                    round_increment = 0;
                }
            }
        }
    }

    round_bits = frac & 0x7ff;

    if (0x7fd <= (uint16) exp) {
        if (exp < 0) {
            is_tiny = (TININESS_BEFORE_ROUNDING ||
                       (exp < -1) ||
                       ((frac + round_increment) < 0x8000000000000000ull));
            shift_right_64_jamming(frac, -exp, &frac);
            exp = 0;
            round_bits = frac & 0x7ff;
            if (is_tiny && round_bits) {
                mau_exc(MAU_ASR_US, MAU_ASR_UM);
            }
        } else if (0x7fd < exp) {
            mau_exc(MAU_ASR_OS, MAU_ASR_OM);
            mau_exc(MAU_ASR_PS, MAU_ASR_PM);
            return (PACK_DFP(sign, 0x7ff, 0) - (round_increment == 0));
        }
    }

    if (round_bits) {
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);
    }

    if (rounding_mode == ROUND_NEAREST) {
        frac >>= 11;
        lsb = (frac & 1) != 0;
        round = (round_bits & 0x400) != 0;
        sticky = ((round_bits & 0x3ff) != 0) | xfp_sticky;
        if (round & (sticky || lsb)) {
            frac++;
            if (frac == 0) {
                exp++;
            }
        }
    } else {
        frac = (frac + round_increment) >> 11;
        lsb = !((t_bool)(round_bits ^ 0x200));
        frac &= ~((t_uint64)lsb);
    }

    return PACK_DFP(sign, exp, frac);
}

/*
 * Return a properly rounded 80-bit floating point value, given a sign
 * bit, exponent, fractional part, and a rounding mode.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void round_pack_xfp(t_bool sign, int32 exp,
                           t_uint64 frac_a, t_uint64 frac_b,
                           RM rounding_mode, XFP *result)
{
    uint8 is_tiny;
    t_int64 round_mask;

    if (0x7ffd <= (uint32)(exp - 1)) {
        if (0x7ffe < exp) {
            round_mask = 0;
            mau_exc(MAU_ASR_OS, MAU_ASR_OM);
            mau_exc(MAU_ASR_PS, MAU_ASR_PM);
            if ((rounding_mode == ROUND_ZERO) ||
                (sign && (rounding_mode == ROUND_PLUS_INF)) ||
                (!sign && (rounding_mode == ROUND_MINUS_INF))) {
                PACK_XFP(sign, 0x7ffe, ~round_mask, result);
                return;
            }
            PACK_XFP(sign, 0x7fff, 0x8000000000000000ull, result);
            return;
        }
        if (exp <= 0) {
            is_tiny = (TININESS_BEFORE_ROUNDING ||
                       (exp < 0) ||
                       (frac_a < 0xffffffffffffffffull));
            shift_right_extra_64_jamming(frac_a, frac_b, (int16)(1 - exp), &frac_a, &frac_b);
            exp = 0;
            if (is_tiny && frac_b) {
                mau_exc(MAU_ASR_US, MAU_ASR_UM);
            }
            if (frac_b) {
                mau_exc(MAU_ASR_PS, MAU_ASR_PM);
            }
            PACK_XFP(sign, exp, frac_a, result);
            return;
        }
    }
    if (frac_b) {
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);
    }
    if (frac_a == 0) {
        exp = 0;
    }
    PACK_XFP_S(sign, exp, frac_a, frac_b, result);
}

/*
 * Given two 80-bit floating point values 'a' and 'b', one of which is
 * a NaN, return the appropriate NaN result.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void propagate_xfp_nan(XFP *a, XFP *b, XFP *result)
{
    uint8 a_is_nan, a_is_signaling_nan;
    uint8 b_is_nan, b_is_signaling_nan;

    a_is_nan = XFP_IS_NAN(a);
    a_is_signaling_nan = XFP_IS_TRAPPING_NAN(a);
    b_is_nan = XFP_IS_NAN(b);
    b_is_signaling_nan = XFP_IS_TRAPPING_NAN(b);

    a->frac |= 0xc000000000000000ull;
    b->frac |= 0xc000000000000000ull;

    if (a_is_signaling_nan | b_is_signaling_nan) {
        mau_exc(MAU_ASR_IS, MAU_ASR_IM);
    }

    if (a_is_nan) {
        if (a_is_signaling_nan & b_is_nan) {
            result->sign_exp = b->sign_exp;
            result->frac = b->frac;
        } else {
            result->sign_exp = a->sign_exp;
            result->frac = a->frac;
        }
    } else {
        result->sign_exp = b->sign_exp;
        result->frac = b->frac;
    }
}

/*
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void propagate_xfp_nan_128(XFP* a, XFP* b, t_mau_128* result)
{
    t_bool is_sig_nan_a, is_sig_nan_b;
    t_uint64 non_frac_a_low, non_frac_b_low;
    uint16 mag_a, mag_b;

    is_sig_nan_a = XFP_IS_TRAPPING_NAN(a);
    is_sig_nan_b = XFP_IS_TRAPPING_NAN(b);

    non_frac_a_low = a->frac & 0xC000000000000000ull;
    non_frac_b_low = b->frac & 0xC000000000000000ull;

    if (is_sig_nan_a | is_sig_nan_b) {
        /* Invalid */
        mau_exc(MAU_ASR_IS, MAU_ASR_IM);
        if (is_sig_nan_a) {
            if (is_sig_nan_b) goto return_larger_mag;
            if (XFP_IS_NAN(b)) goto return_b;
            goto return_a;
        } else {
            if (XFP_IS_NAN(a)) goto return_a;
            goto return_b;
        }
    }

 return_larger_mag:
    mag_a = a->frac & 0x7fff;
    mag_b = b->frac & 0x7fff;
    if (mag_a < mag_b) goto return_b;
    if (mag_b < mag_a) goto return_a;
    if (a->frac < b->frac) goto return_b;
    if (b->frac < a->frac) goto return_a;
    if (a->sign_exp < b->sign_exp) goto return_a;
 return_b:
    result->high = b->sign_exp;
    result->low = non_frac_b_low;
    return;
 return_a:
    result->high = a->sign_exp;
    result->low = non_frac_a_low;
    return;
}

/*
 * Normalize and round an extended-precision floating point value.
 *
 * Partially derived from the SoftFloat 2c package (see copyright
 * notice above)
 */
static void normalize_round_pack_xfp(t_bool sign, int32 exp,
                                     t_uint64 frac_0, t_uint64 frac_1,
                                     RM rounding_mode, XFP *result)
{
    int8 shift_count;

    if (frac_0 == 0) {
        frac_0 = frac_1;
        frac_1 = 0;
        exp -= 64;
    }

    shift_count = leading_zeros_64(frac_0);
    short_shift_left_128(frac_0, frac_1, shift_count, &frac_0, &frac_1);
    exp -= shift_count;

    round_pack_xfp(sign, exp, frac_0, frac_1, rounding_mode, result);
}


/*
 * Normalize the subnormal 80-bit floating point value represented by
 * the denormalized input fractional comonent.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void normalize_sfp_subnormal(uint32 in_frac, int16 *out_exp, uint32 *out_frac)
{
    int8 shift_count;

    shift_count = leading_zeros(in_frac) - 8;

    if (shift_count < 0) {
        /* There was invalid input, there's nothing we can do. */
        *out_frac = in_frac;
        *out_exp = 0;
        return;
    }

    *out_frac = in_frac << shift_count;
    *out_exp = (uint16)(1 - shift_count);
}

/*
 * Normalize the subnormal 64-bit floating point value represented by
 * the denormalized input fractional comonent.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void normalize_dfp_subnormal(t_uint64 in_frac, int16 *out_exp, t_uint64 *out_frac)
{
    int8 shift_count;

    shift_count = leading_zeros_64(in_frac) - 11;

    if (shift_count < 0) {
        /* There was invalid input, there's nothing we can do. */
        *out_frac = in_frac;
        *out_exp = 0;
        return;
    }

    *out_frac = in_frac << shift_count;
    *out_exp = 1 - shift_count;
}

/*
 * Normalize the subnormal 32-bit floating point value represented by
 * the denormalized input fractional comonent.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void normalize_xfp_subnormal(t_uint64 in_frac, int32 *out_exp, t_uint64 *out_frac)
{
    int8 shift_count;

    shift_count = leading_zeros_64(in_frac);
    *out_frac = in_frac << shift_count;
    *out_exp = 1 - shift_count;
}

/*
 * Returns the result of converting the 32-bit floating point NaN
 * value to the canonincal NaN format.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static T_NAN sfp_to_common_nan(SFP val)
{
    T_NAN nan = {0};

    if (SFP_IS_TRAPPING_NAN(val)) {
        mau_state.trapping_nan = TRUE;
    }

    nan.sign = val >> 31;
    nan.low = 0;
    nan.high = ((t_uint64) val) << 41;

    return nan;
}

/*
 * Returns the result of converting the 64-bit floating point NaN
 * value to the canonincal NaN format.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static T_NAN dfp_to_common_nan(DFP val)
{
    T_NAN nan = {0};

    if (DFP_IS_TRAPPING_NAN(val)) {
        mau_state.trapping_nan = TRUE;
    }

    nan.sign = (val >> 63) & 1;
    nan.low = 0;
    nan.high = (t_uint64)(val << 12);

    return nan;
}

/*
 * Returns the result of converting the 80-bit floating point NaN
 * value to the canonincal NaN format.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static T_NAN xfp_to_common_nan(XFP *val)
{
    T_NAN nan = {0};

    if (XFP_IS_TRAPPING_NAN(val)) {
        mau_state.trapping_nan = TRUE;
    }

    nan.sign = val->sign_exp >> 15;
    nan.low = 0;
    nan.high = val->frac << 1;

    return nan;
}

/*
 * Returns the result of converting a canonical NAN format value to a
 * 32-bit floating point format.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static SFP common_nan_to_sfp(T_NAN nan)
{
    return ((((uint32)nan.sign) << 31)
            | 0x7fc00000
            | (nan.high >> 41));
}

/*
 * Returns the result of converting a canonical NAN format value to a
 * 64-bit floating point format.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static DFP common_nan_to_dfp(T_NAN nan)
{
    return ((((t_uint64)nan.sign) << 63)
            | 0x7ff8000000000000ull
            | (nan.high >> 12));
}

/*
 * Returns the result of converting a canonical NAN format value to an
 * 80-bit floating point format.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void common_nan_to_xfp(T_NAN nan, XFP *result)
{
    result->frac = 0xc000000000000000ull | (nan.high >> 1);
    result->sign_exp = (((uint16)nan.sign) << 15) | 0x7fff;
}

/*
 * Convert a 32-bit floating point value to an 80-bit floating point
 * value.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void sfp_to_xfp(SFP val, XFP *result)
{
    t_bool sign;
    int16 exp;
    uint32 frac;

    sign = SFP_SIGN(val);
    exp = SFP_EXP(val);
    frac = SFP_FRAC(val);

    if (exp == 0xff) {
        if (frac) {
            common_nan_to_xfp(sfp_to_common_nan(val), result);
            return;
        }
    }

    if (exp == 0) {
        if (frac == 0) {
            PACK_XFP(sign, 0, 0, result);
            return;
        }
        normalize_sfp_subnormal(frac, &exp, &frac);
    }

    frac |= 0x800000;

    PACK_XFP(sign, exp + 0x3f80, ((t_uint64) frac) << 40, result);
}

/*
 * Convert a 64-bit floating point value to an 80-bit floating point value.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
void dfp_to_xfp(DFP val, XFP *result)
{
    t_bool sign;
    int16 exp;
    t_uint64 frac;

    sign = DFP_SIGN(val);
    exp = DFP_EXP(val);
    frac = DFP_FRAC(val);

    if (exp == 0x7ff) {
        if (sign) {
            common_nan_to_xfp(dfp_to_common_nan(val), result);
        }

        PACK_XFP(sign, 0xff, 0, result);
        return;
    }
    if (exp == 0) {
        if (frac == 0) {
            PACK_XFP(sign, 0, 0, result);
            return;
        }
        normalize_dfp_subnormal(frac, &exp, &frac);
    }

    PACK_XFP(sign,
             exp + 0x3c00,
             0x8000000000000000ull | (frac << 11),
             result);
}

/*
 * Convert an 80-bit floating point value to a 32-bit floating point
 * value.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static SFP xfp_to_sfp(XFP *val, RM rounding_mode)
{
    t_bool sign;
    int32 exp;
    t_uint64 frac;
    uint32 dst_frac;

    sign = XFP_SIGN(val);
    exp = XFP_EXP(val);
    frac = XFP_FRAC(val);

    if (exp == 0x7fff) {
        if ((t_uint64)(frac << 1)) {
            return common_nan_to_sfp(xfp_to_common_nan(val));
        }
        return PACK_SFP(sign, 0xff, 0);
    }

    shift_right_64_jamming(frac, 33, &frac);

    dst_frac = (uint32)frac;

    if (exp || frac) {
        exp -= 0x3f81;
    }

    return round_pack_sfp(sign, exp, dst_frac, rounding_mode);
}

/*
 * Convert an 80-bit floating point value to a 64-bit floating point
 * value.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static DFP xfp_to_dfp(XFP *val, RM rounding_mode)
{
    t_bool sign;
    int32 exp;
    t_uint64 frac;

    sign = XFP_SIGN(val);
    exp = XFP_EXP(val);
    frac = XFP_FRAC(val);

    sim_debug(TRACE_DBG, &mau_dev,
              "[xfp_to_dfp] input=%04x%016llx input_exp=%04x  packed_exp=%04x\n",
              val->sign_exp, val->frac, (uint16)exp, (uint16)(exp - 0x3c01));

    if (exp == 0x7fff) {
        if ((t_uint64)(frac << 1)) {
            return common_nan_to_dfp(xfp_to_common_nan(val));
        }
        return PACK_DFP(sign, 0x7ff, 0);
    }

    if (exp || frac) {
        exp -= 0x3c01;
    }

    return round_pack_dfp(sign, exp, frac, val->s, rounding_mode);
}

/*****************************************************************************
 * Comparison Functions
 ****************************************************************************/

/*
 * Returns true if the two 80-bit floating point values are equal.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static uint32 xfp_eq(XFP *a, XFP *b)
{
    if (((XFP_EXP(a) == 0x7fff) && (t_uint64)(XFP_FRAC(a) << 1)) ||
        ((XFP_EXP(b) == 0x7fff) && (t_uint64)(XFP_FRAC(b) << 1))) {

        /* Check for NAN and raise invalid exception */
        if (XFP_IS_TRAPPING_NAN(a) || XFP_IS_TRAPPING_NAN(b)) {
            mau_exc(MAU_ASR_IS, MAU_ASR_IM);
        }

        return 0;
    }

    return ((a->frac == b->frac) &&
            ((a->sign_exp == b->sign_exp) ||
             ((a->frac == 0) && ((uint16)((a->sign_exp|b->sign_exp) << 1) == 0))));
}

/*
 * Returns true if the 80-bit floating point value 'a' is less than
 * the 80-bit floating point value 'b'.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static uint32 xfp_lt(XFP *a, XFP *b)
{
    uint32 a_sign, b_sign;

    if (((XFP_EXP(a) == 0x7fff) && (t_uint64)(XFP_FRAC(a) << 1)) ||
        ((XFP_EXP(b) == 0x7fff) && (t_uint64)(XFP_FRAC(b) << 1))) {
        return 0;
    }

    a_sign = XFP_SIGN(a);
    b_sign = XFP_SIGN(b);

    if (a_sign != b_sign) {
        return(a_sign &&
               ((((uint16)((a->sign_exp|b->sign_exp) << 1)) | a->frac | b->frac) != 0));
    }

    if (a_sign) {
        return (b->sign_exp < a->sign_exp) || ((b->sign_exp == a->sign_exp) && (b->frac < a->frac));
    } else {
        return (a->sign_exp < b->sign_exp) || ((a->sign_exp == b->sign_exp) && (a->frac < b->frac));
    }
}

/*****************************************************************************
 * Conversion Functions
 ****************************************************************************/

/*
 * Convert a 32-bit signed integer value to an IEEE-754 extended
 * precion (80-bit) floating point value.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
void mau_int_to_xfp(int32 val, XFP *result)
{
    int32 shift_width;
    t_bool sign;
    uint32 abs_val;
    uint16 sign_exp = 0;
    t_uint64 frac = 0;

    if (val) {
        sign = (val < 0);
        abs_val = (uint32)(sign ? -val : val);
        shift_width = leading_zeros(abs_val);
        sign_exp = (sign << 15) | (0x401e - shift_width);
        frac = (t_uint64) (abs_val << shift_width) << 32;
    }

    result->sign_exp = sign_exp;
    result->frac = frac;

    if (sign_exp & 0x8000) {
        mau_state.asr |= MAU_ASR_N;
    }

    if ((sign_exp & 0x7fff) == 0 && frac == 0) {
        mau_state.asr |= MAU_ASR_Z;
    }
}

/*
 * Convert a floating point value to a 64-bit integer.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
t_int64 xfp_to_int64(XFP *val, RM rounding_mode)
{
    t_bool sign;
    int32 exp, shift_count;
    t_uint64 frac, frac_extra;

    sign = XFP_SIGN(val);
    exp = XFP_EXP(val);
    frac = XFP_FRAC(val);
    shift_count = 0x403e - exp;
    if (shift_count <= 0) {
        if (shift_count) {
            mau_exc(MAU_ASR_IS, MAU_ASR_IM);
            if (!sign || ((exp == 0x7fff) && (frac != 0x8000000000000000ull))) {
                return 0x7fffffffffffffffull;
            }
            return 0x8000000000000000ull;
        }
        frac_extra = 0;
    } else {
        shift_right_extra_64_jamming(frac, 0, shift_count, &frac, &frac_extra);
    }

    return round_pack_int64(sign, frac, frac_extra, rounding_mode);
}

void mau_int64_to_xfp(t_uint64 val, XFP *result)
{
    t_bool sign;
    t_uint64 abs;
    int8 shift_count;

    if (val == 0) {
        PACK_XFP(0, 0, 0, result);
        return;
    }

    sign = (val & 0x8000000000000000ull) != 0ull;
    abs = val & 0x7fffffffffffffffull;
    shift_count = leading_zeros_64(abs);
    PACK_XFP(sign, 0x403e - shift_count, abs << shift_count, result);
}

/*
 * Convert a float value to a decimal value.
 */
void xfp_to_decimal(XFP *a, DEC *d, RM rounding_mode)
{
    t_int64 tmp;
    int i;
    t_bool sign;
    uint16 digits[19] = {0};

    tmp = xfp_to_int64(a, rounding_mode);

    if (tmp < 0) {
        sign = 0xb;
    } else {
        sign = 0xa;
    }

    for (i = 0; i < 19; i++) {
        digits[i] = tmp % 10;
        tmp /= 10;
    }

    d->l = sign;
    d->l |= (t_uint64)digits[0] << 4;
    d->l |= (t_uint64)digits[1] << 8;
    d->l |= (t_uint64)digits[2] << 12;
    d->l |= (t_uint64)digits[3] << 16;
    d->l |= (t_uint64)digits[4] << 20;
    d->l |= (t_uint64)digits[5] << 24;
    d->l |= (t_uint64)digits[6] << 28;
    d->l |= (t_uint64)digits[7] << 32;
    d->l |= (t_uint64)digits[8] << 36;
    d->l |= (t_uint64)digits[9] << 40;
    d->l |= (t_uint64)digits[10] << 44;
    d->l |= (t_uint64)digits[11] << 48;
    d->l |= (t_uint64)digits[12] << 52;
    d->l |= (t_uint64)digits[13] << 56;
    d->l |= (t_uint64)digits[14] << 60;
    d->h = (uint32)digits[15];
    d->h |= (uint32)digits[15] << 4;
    d->h |= (uint32)digits[15] << 8;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [xfp_to_decimal] "
              "Digits: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d 0x%x\n",
              R[NUM_PC],
              digits[17], digits[16], digits[15], digits[14], digits[13], digits[12],
              digits[11], digits[10], digits[9], digits[8], digits[7], digits[6],
              digits[5], digits[4], digits[3], digits[2], digits[1], digits[0],
              sign);
}

/*
 * Convert a decimal value to a float value.
 */
void mau_decimal_to_xfp(DEC *d, XFP *a)
{
    int i;
    t_bool sign;
    uint16 digits[18] = {0};
    t_uint64 multiplier = 1;
    t_uint64 tmp;
    t_int64 signed_tmp;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [mau_decimal_to_xfp] DEC input: %08x %08x %08x\n",
              R[NUM_PC], d->h, (uint32)(d->l >> 32), (uint32)(d->l));

    sign = (d->l) & 15;
    digits[0] = (d->l >> 4) & 15;
    digits[1] = (d->l >> 8) & 15;
    digits[2] = (d->l >> 12) & 15;
    digits[3] = (d->l >> 16) & 15;
    digits[4] = (d->l >> 20) & 15;
    digits[5] = (d->l >> 24) & 15;
    digits[6] = (d->l >> 28) & 15;
    digits[7] = (d->l >> 32) & 15;
    digits[8] = (d->l >> 36) & 15;
    digits[9] = (d->l >> 40) & 15;
    digits[10] = (d->l >> 44) & 15;
    digits[11] = (d->l >> 48) & 15;
    digits[12] = (d->l >> 52) & 15;
    digits[13] = (d->l >> 56) & 15;
    digits[14] = (d->l >> 60) & 15;
    digits[15] = (d->h) & 15;
    digits[16] = (d->h >> 4) & 15;
    digits[17] = (d->h >> 8) & 15;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [mau_decimal_to_xfp] "
              "Digits: %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d 0x%x\n",
              R[NUM_PC],
              digits[17], digits[16], digits[15], digits[14], digits[13], digits[12],
              digits[11], digits[10], digits[9], digits[8], digits[7], digits[6],
              digits[5], digits[4], digits[3], digits[2], digits[1], digits[0],
              sign);

    tmp = 0;

    for (i = 0; i < 18; i++) {
        tmp += digits[i] * multiplier;
        multiplier *= 10;
    }

    switch (sign) {
    case 0xd:
    case 0xb:
        /* Negative number */
        signed_tmp = -((t_int64) tmp);
        break;
        /* TODO: HANDLE NAN AND INFINITY */
    default:
        signed_tmp = (t_int64) tmp;
    }

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [mau_decimal_to_xfp] tmp val = %lld\n",
              R[NUM_PC], signed_tmp);

    mau_int64_to_xfp((t_uint64) signed_tmp, a);

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [mau_decimal_to_xfp] XFP = %04x%016llx\n",
              R[NUM_PC], a->sign_exp, a->frac);

}

/*
 * Convert a floating point value to a 32-bit integer.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
uint32 xfp_to_int(XFP *val, RM rounding_mode)
{
    t_bool sign;
    int32 exp, shift_count;
    t_uint64 frac;

    sign = XFP_SIGN(val);
    exp = XFP_EXP(val);
    frac = XFP_FRAC(val);

    if ((exp == 0x7fff) && (t_uint64)(frac << 1)) {
        sign = 0;
    }

    shift_count = 0x4037 - exp;

    if (shift_count <= 0) {
        shift_count = 1;
    }

    shift_right_64_jamming(frac, shift_count, &frac);

    return round_pack_int(sign, frac, rounding_mode);
}

/*
 * Round an 80-bit extended precission floating-point value
 * to an integer.
 *
 * Derived from the SoftFloat 2c library (see copyright notice above)
 */
void mau_round_xfp_to_int(XFP *val, XFP *result, RM rounding_mode)
{
    t_bool sign;
    int32 exp;
    t_uint64 last_bit_mask, round_bits_mask;

    exp = XFP_EXP(val);

    if (0x403e <= exp) {
        if ((exp == 0x7fff) && (t_uint64)(XFP_FRAC(val) << 1)) {
            propagate_xfp_nan(val, val, result);
            return;
        }
        result->sign_exp = val->sign_exp;
        result->frac = val->frac;
        return;
    }
    if (exp < 0x3ff) {
        if ((exp == 0) && ((t_uint64)(XFP_FRAC(val) << 1) == 0)) {
            result->sign_exp = val->sign_exp;
            result->frac = val->frac;
            return;
        }
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);
        sign = XFP_SIGN(val);
        switch (rounding_mode) {
        case ROUND_NEAREST:
            if (exp == 0x3ffe && (t_uint64)(XFP_FRAC(val) << 1)) {
                PACK_XFP(sign, 0x3fff, 0x8000000000000000ull, result);
                return;
            }
            break;
        case ROUND_MINUS_INF:
            if (sign) {
                PACK_XFP(1, 0x3fff, 0x8000000000000000ull, result);
            } else {
                PACK_XFP(0, 0, 0, result);
            }
            return;
        case ROUND_PLUS_INF:
            if (sign) {
                PACK_XFP(1, 0, 0, result);
            } else {
                PACK_XFP(0, 0x3fff, 0x8000000000000000ull, result);
            }
            return;
        default:
            /* Do nothing */
            break;
        }
        PACK_XFP(sign, 0, 0, result);
        return;
    }

    last_bit_mask = 1;
    last_bit_mask <<= 0x403e - exp;
    round_bits_mask = last_bit_mask - 1;

    result->sign_exp = val->sign_exp;
    result->frac = val->frac;

    if (rounding_mode == ROUND_NEAREST) {
        result->frac += last_bit_mask >> 1;
        if ((result->frac & round_bits_mask) == 0) {
            result->frac &= ~last_bit_mask;
        }
    } else if (rounding_mode != ROUND_ZERO) {
        if (XFP_SIGN(result) ^ (rounding_mode == ROUND_PLUS_INF)) {
            result->frac += round_bits_mask;
        }
    }

    result->frac &= ~round_bits_mask;
    if (result->frac == 0) {
        ++result->sign_exp;
        result->frac = 0x8000000000000000ull;
    }

    if (result->frac != val->frac) {
        mau_exc(MAU_ASR_PS, MAU_ASR_PM);
    }
}

/*****************************************************************************
 * Math Functions
 ****************************************************************************/

/*
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void xfp_add_fracs(XFP *a, XFP *b, t_bool sign, XFP *result, RM rounding_mode)
{
    int32 a_exp, b_exp, r_exp;
    t_uint64 a_frac, b_frac, r_frac_0, r_frac_1;
    int32 exp_diff;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [ADD_FRACS] a=%04x%016llx  b=%04x%016llx\n",
              R[NUM_PC],
              a->sign_exp, a->frac,
              b->sign_exp, b->frac);

    a_exp = XFP_EXP(a);
    a_frac = XFP_FRAC(a);
    b_exp = XFP_EXP(b);
    b_frac = XFP_FRAC(b);

    exp_diff = a_exp - b_exp;
    if (0 < exp_diff) {
        if (a_exp == 0x7fff) {
            if ((t_uint64) (a_frac << 1)) {
                propagate_xfp_nan(a, b, result);
                return;
            }
            result->sign_exp = a->sign_exp;
            result->frac = a->frac;
            return;
        }
        if (b_exp == 0) {
            --exp_diff;
        }
        shift_right_extra_64_jamming(b_frac, 0, exp_diff, &b_frac, &r_frac_1);
        r_exp = a_exp;
    } else if (exp_diff < 0) {
        if (b_exp == 0x7fff) {
            if ((t_uint64) (b_frac << 1)) {
                propagate_xfp_nan(a, b, result);
                return;
            }
            PACK_XFP(sign, 0x7fff, 0x8000000000000000ull, result);
            return;
        }
        if (a_exp == 0) {
            ++exp_diff;
        }

        shift_right_extra_64_jamming(a_frac, 0, -exp_diff, &a_frac, &r_frac_1);
        r_exp = b_exp;
    } else {
        if (a_exp == 0x7fff) {
            if ((t_uint64)((a_frac | b_frac) << 1)) {
                propagate_xfp_nan(a, b, result);
                return;
            }
            result->sign_exp = a->sign_exp;
            result->frac = a->frac;
            return;
        }
        r_frac_1 = 0;
        r_frac_0 = a_frac + b_frac;
        if (a_exp == 0) {
            normalize_xfp_subnormal(r_frac_0, &r_exp, &r_frac_0);

            round_pack_xfp(sign, r_exp, r_frac_0, r_frac_1, rounding_mode, result);
            return;
        }
        r_exp = a_exp;
        shift_right_extra_64_jamming(r_frac_0, r_frac_1, 1, &r_frac_0, &r_frac_1);
        r_frac_0 |= 0x8000000000000000ull;
        ++r_exp;
        round_pack_xfp(sign, r_exp, r_frac_0, r_frac_1, rounding_mode, result);
        return;
    }
    r_frac_0 = a_frac + b_frac;
    if (((t_int64) r_frac_0) < 0) {
        round_pack_xfp(sign, r_exp, r_frac_0, r_frac_1, rounding_mode, result);
        return;
    }
    shift_right_extra_64_jamming(r_frac_0, r_frac_1, 1, &r_frac_0, &r_frac_1);
    r_frac_0 |= 0x8000000000000000ull;
    ++r_exp;
    round_pack_xfp(sign, r_exp, r_frac_0, r_frac_1, rounding_mode, result);
    return;
}

/*
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void xfp_sub_fracs(XFP *a, XFP *b, t_bool sign, XFP *result, RM rounding_mode)
{
    int32 a_exp, b_exp, r_exp;
    t_uint64 a_frac, b_frac, r_frac_0, r_frac_1;
    int32 exp_diff;

    a_exp = XFP_EXP(a);
    a_frac = XFP_FRAC(a);
    b_exp = XFP_EXP(b);
    b_frac = XFP_FRAC(b);
    exp_diff = a_exp - b_exp;

    if (0 < exp_diff) {
        /* aExpBigger */
        if (a_exp == 0x7fff) {
            if ((t_uint64)(a_frac << 1)) {
                propagate_xfp_nan(a, b, result);
                return;
            }
            result->sign_exp = a->sign_exp;
            result->frac = a->frac;
            return;
        }
        if (b_exp == 0) {
            --exp_diff;
        }
        shift_right_128_jamming(b_frac, 0, exp_diff, &b_frac, &r_frac_1);
        /* aBigger */
        sub_128(a_frac, 0, b_frac, r_frac_1, &r_frac_0, &r_frac_1);
        r_exp = a_exp;
        /* normalizeRoundAndPack */
        normalize_round_pack_xfp(sign, r_exp, r_frac_0, r_frac_1, rounding_mode, result);
        return;
    }
    if (exp_diff < 0) {
        /* bExpBigger */
        if (b_exp == 0x7fff) {
            if ((t_uint64)(b_frac << 1)) {
                propagate_xfp_nan(a, b, result);
                return;
            }
            PACK_XFP(sign ? 0 : 1, 0x7fff, 0x8000000000000000ull, result);
            return;
        }
        if (a_exp == 0) {
            ++exp_diff;
        }
        shift_right_128_jamming(a_frac, 0, -exp_diff, &a_frac, &r_frac_1);
        /* bBigger */
        sub_128(b_frac, 0, a_frac, r_frac_1, &r_frac_0, &r_frac_1);
        r_exp = b_exp;
        sign = sign ? 0 : 1;
        /* normalizeRoundAndPack */
        normalize_round_pack_xfp(sign, r_exp,
                                 r_frac_0, r_frac_1,
                                 rounding_mode, result);
        return;
    }
    if (a_exp == 0x7fff) {
        if ((t_uint64)((a_frac | b_frac) << 1)) {
            propagate_xfp_nan(a, b, result);
            return;
        }
        mau_exc(MAU_ASR_IS, MAU_ASR_IM); /* Invalid */
        result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
        result->frac = DEFAULT_XFP_NAN_FRAC;
        return;
    }
    if (a_exp == 0) {
        a_exp = 1;
        b_exp = 1;
    }
    r_frac_1 = 0;
    if (b_frac < a_frac) {
        /* aBigger */
        sub_128(a_frac, 0, b_frac, r_frac_1, &r_frac_0, &r_frac_1);
        r_exp = a_exp;
        /* normalizeRoundAndPack */
        normalize_round_pack_xfp(sign, r_exp,
                                 r_frac_0, r_frac_1,
                                 rounding_mode, result);
        return;
    }
    if (a_frac < b_frac) {
        /* bBigger */
        sub_128(b_frac, 0, a_frac, r_frac_1, &r_frac_0, &r_frac_1);
        r_exp = b_exp;
        sign ^= 1;

        /* normalizeRoundAndPack */
        normalize_round_pack_xfp(sign, r_exp,
                                 r_frac_0, r_frac_1,
                                 rounding_mode, result);
        return;
    }

    PACK_XFP(rounding_mode == ROUND_MINUS_INF, 0, 0, result);
}

/*************************************************************************
 *
 * MAU-specific functions
 *
 *************************************************************************/

/*
 * Set condition flags based on comparison of the two values A and B.
 *
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void xfp_cmp(XFP *a, XFP *b)
{
    mau_state.asr &= ~(MAU_ASR_N|MAU_ASR_Z|MAU_ASR_UO);

    /* Page 5-9:
     *
     * "An invalid operation exception condition exists if either or
     * both source operands are trapping NaNs. If the exception is
     * masked then the UO flag would be set. However, if this
     * exception is enabled, and, if Op1 is a trapping NaN, it is
     * converted to double-extended precision and stored in DR. Else,
     * Op2 (converted to double-extended precision, if necessary) is
     * stored in DR."
     */

    if (XFP_IS_NAN(a) || XFP_IS_NAN(b)) {
        if ((mau_state.asr & MAU_ASR_IM) == 0) {
            mau_state.asr |= MAU_ASR_UO;
        } else if (XFP_IS_NAN(a)) {
            mau_state.dr.sign_exp = a->sign_exp;
            mau_state.dr.frac = a->frac;
        } else {
            mau_state.dr.sign_exp = b->sign_exp;
            mau_state.dr.frac = b->frac;
        }
        return;
    }

    if (xfp_lt(a, b)) {
        mau_state.asr |= MAU_ASR_N;
    }

    if (xfp_eq(a, b)) {
        mau_state.asr |= MAU_ASR_Z;
    }
}

static void xfp_cmpe(XFP *a, XFP *b)
{
    mau_state.asr &= ~(MAU_ASR_N|MAU_ASR_Z|MAU_ASR_UO);

    /* Page 5-10:
     *
     * "When two unordered values are compared, then, in additon to
     * the response specified below, the invalid operation exception
     * sticky flag (ASR<IS> = 1) is set and the trap invoked if the
     * invalid operation exceptionis enabled.""
     */

    if ((XFP_IS_NAN(a) || XFP_IS_NAN(b)) && (mau_state.asr & MAU_ASR_IM)) {
        mau_state.asr |= MAU_ASR_UO;
        return;
    }

    if (xfp_lt(a, b)) {
        mau_state.asr |= MAU_ASR_N;
    }

    if (xfp_eq(a, b)) {
        mau_state.asr |= MAU_ASR_Z;
    }
}

static void xfp_cmps(XFP *a, XFP *b)
{
    mau_state.asr &= ~(MAU_ASR_N|MAU_ASR_Z|MAU_ASR_UO);

    if (XFP_IS_NAN(a) || XFP_IS_NAN(b)) {
        if ((mau_state.asr & MAU_ASR_IM) == 0) {
            mau_state.asr |= MAU_ASR_UO;
        } else if (XFP_IS_NAN(a)) {
            mau_state.dr.sign_exp = a->sign_exp;
            mau_state.dr.frac = a->frac;
        } else {
            mau_state.dr.sign_exp = b->sign_exp;
            mau_state.dr.frac = b->frac;
        }
        return;
    }

    if (xfp_lt(a, b)) {
        mau_state.asr |= MAU_ASR_Z;
    } else if (xfp_eq(a, b)) {
        mau_state.asr |= MAU_ASR_N;
    }
}

static void xfp_cmpes(XFP *a, XFP *b)
{
    mau_state.asr &= ~(MAU_ASR_N|MAU_ASR_Z|MAU_ASR_UO);

    if ((XFP_IS_NAN(a) || XFP_IS_NAN(b)) && (mau_state.asr & MAU_ASR_IM)) {
        mau_state.asr |= MAU_ASR_UO;
        return;
    }

    if (xfp_lt(a, b)) {
        mau_state.asr |= MAU_ASR_Z;
    }

    if (xfp_eq(a, b)) {
        mau_state.asr |= MAU_ASR_N;
    }
}

static void xfp_add(XFP *a, XFP *b, XFP *result, RM rounding_mode)
{
    uint32 a_sign, b_sign;

    a_sign = XFP_SIGN(a);
    b_sign = XFP_SIGN(b);

    if (a_sign == b_sign) {
        xfp_add_fracs(a, b, a_sign, result, rounding_mode);
    } else {
        xfp_sub_fracs(a, b, a_sign, result, rounding_mode);
    }
}

static void xfp_sub(XFP *a, XFP *b, XFP *result, RM rounding_mode)
{
    uint32 a_sign, b_sign;

    a_sign = XFP_SIGN(a);
    b_sign = XFP_SIGN(b);

    if (a_sign == b_sign) {
        xfp_sub_fracs(a, b, a_sign, result, rounding_mode);
    } else {
        xfp_add_fracs(a, b, a_sign, result, rounding_mode);
    }
}

/*
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void xfp_mul(XFP *a, XFP *b, XFP *result, RM rounding_mode)
{
    uint32 a_sign, b_sign, r_sign;
    int32 a_exp, b_exp, r_exp;
    t_uint64 a_frac, b_frac, r_frac_0, r_frac_1;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [MUL] op1=%04x%016llx  op2=%04x%016llx\n",
              R[NUM_PC],
              a->sign_exp, a->frac,
              b->sign_exp, b->frac);

    a_sign = XFP_SIGN(a);
    a_exp = XFP_EXP(a);
    a_frac = XFP_FRAC(a);
    b_sign = XFP_SIGN(b);
    b_exp = XFP_EXP(b);
    b_frac = XFP_FRAC(b);

    r_sign = a_sign ^ b_sign;

    if (a_exp == 0x7fff) {
        if ((t_uint64)(a_frac << 1) || ((b_exp == 0x7fff) && (t_uint64)(b_frac << 1))) {
            propagate_xfp_nan(a, b, result);
            return;
        }
        if ((b_exp | b_frac) == 0) {
            /* invalid */
            mau_exc(MAU_ASR_IS, MAU_ASR_IM);
            result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
            result->frac = DEFAULT_XFP_NAN_FRAC;
            return;
        }
        PACK_XFP(r_sign, 0x7fff, 0x8000000000000000ull, result);
        return;
    }

    if (b_exp == 0x7fff) {
        if ((t_uint64)(b_frac << 1)) {
            propagate_xfp_nan(a, b, result);
            return;
        }
        if ((a_exp | a_frac) == 0) {
            /* invalid */
            mau_exc(MAU_ASR_IS, MAU_ASR_IM);
            result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
            result->frac = DEFAULT_XFP_NAN_FRAC;
            return;
        }
        PACK_XFP(r_sign, 0x7fff, 0x8000000000000000ull, result);
        return;
    }

    if (a_exp == 0) {
        if (a_frac == 0) {
            PACK_XFP(r_sign, 0, 0, result);
            return;
        }
        normalize_xfp_subnormal(a_frac, &a_exp, &a_frac);
    }

    if (b_exp == 0) {
        if (b_frac == 0) {
            PACK_XFP(r_sign, 0, 0, result);
            return;
        }
        normalize_xfp_subnormal(b_frac, &b_exp, &b_frac);
    }

    r_exp = a_exp + b_exp - 0x3ffe;
    mul_64_to_128(a_frac, b_frac, &r_frac_0, &r_frac_1);
    if (0 < (t_int64)r_frac_0) {
        short_shift_left_128(r_frac_0, r_frac_1, 1,
                             &r_frac_0, &r_frac_1);
        --r_exp;
    }

    round_pack_xfp(r_sign, r_exp, r_frac_0,
                   r_frac_1, rounding_mode, result);
}

/*
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void xfp_div(XFP *a, XFP *b, XFP *result, RM rounding_mode)
{
    t_bool a_sign, b_sign, r_sign;
    int32 a_exp, b_exp, r_exp;
    t_uint64 a_frac, b_frac, r_frac0, r_frac1;
    t_uint64 rem0, rem1, rem2, term0, term1, term2;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [DIV] op1=%04x%016llx op2=%04x%016llx\n",
              R[NUM_PC], b->sign_exp, b->frac, a->sign_exp, a->frac);

    a_sign = XFP_SIGN(a);
    a_exp = XFP_EXP(a);
    a_frac = XFP_FRAC(a);

    b_sign = XFP_SIGN(b);
    b_exp = XFP_EXP(b);
    b_frac = XFP_FRAC(b);

    r_sign = a_sign ^ b_sign;

    if (a_exp == 0x7fff) {
        if ((t_uint64)(a_frac << 1)) {
            propagate_xfp_nan(a, b, result);
            return;
        }

        if (b_exp == 0x7fff) {
            if ((t_uint64)(b_frac << 1)) {
                propagate_xfp_nan(a, b, result);
                return;
            }
            /* Invalid */
            mau_exc(MAU_ASR_IS, MAU_ASR_IM);
            result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
            result->frac = DEFAULT_XFP_NAN_FRAC;
            return;
        }

        PACK_XFP(r_sign, 0x7fff, 0x8000000000000000ull, result);
        return;
    }

    if (b_exp == 0x7fff) {
        if ((t_uint64) (b_frac << 1)) {
            propagate_xfp_nan(a, b, result);
            return;
        }

        PACK_XFP(r_sign, 0, 0, result);
        return;
    }

    if (b_exp == 0) {
        if (b_frac == 0) {
            if ((a_exp | b_frac) == 0) {
                /* Invalid */
                mau_exc(MAU_ASR_IS, MAU_ASR_IM);
                result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
                result->frac = DEFAULT_XFP_NAN_FRAC;
                return;
            }
            /* Divide by zero - SPECIAL CASE 4 */
            sim_debug(TRACE_DBG, &mau_dev,
                      "[%08x] [DIV] Divide by zero detected.\n", R[NUM_PC]);
            mau_case_div_zero(a, b, result);
            return;
        }
        normalize_xfp_subnormal(b_frac, &b_exp, &b_frac);
    }

    if (a_exp == 0) {
        if (a_frac == 0) {
            PACK_XFP(r_sign, 0, 0, result);
            return;
        }
        normalize_xfp_subnormal(a_frac, &a_exp, &a_frac);
    }

    r_exp = a_exp - b_exp + 0x3ffe;
    rem1 = 0;
    if (b_frac <= a_frac) {
        shift_right_128(a_frac, 0, 1, &a_frac, &rem1);
        ++r_exp;
    }

    r_frac0 = estimate_div_128_to_64(a_frac, rem1, b_frac);
    mul_64_to_128(b_frac, r_frac0, &term0, &term1);
    sub_128(a_frac, rem1, term0, term1, &rem0, &rem1);

    while ((t_int64) rem0 < 0) {
        --r_frac0;
        add_128(rem0, rem1, 0, b_frac, &rem0, &rem1);
    }

    r_frac1 = estimate_div_128_to_64(rem1, 0, b_frac);
    if ((t_uint64)(r_frac1 << 1) <= 8) {
        mul_64_to_128(b_frac, r_frac1, &term1, &term2);
        sub_128(rem1, 0, term1, term2, &rem1, &rem2);
        while ((t_int64) rem1 < 0) {
            --r_frac1;
            add_128(rem1, rem2, 0, b_frac, &rem1, &rem2);
        }
        r_frac1 |= ((rem1 | rem2) != 0);
    }

    round_pack_xfp(r_sign, r_exp, r_frac0, r_frac1, rounding_mode, result);
}

/*
 * Derived from the SoftFloat 2c package (see copyright notice above)
 */
static void xfp_sqrt(XFP *a, XFP *result, RM rounding_mode)
{
    XFP zero = {0, 0, 0};
    t_bool a_sign;
    int32 a_exp, norm_exp, r_exp;
    uint32 a_frac_32, sqrt_recip_32, r_frac_32;
    t_uint64 a_frac, norm_frac, q, x64, z_frac, z_frac_extra;
    t_mau_128 nan_128, rem, y, term;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [SQRT] op1=%04x%016llx\n",
              R[NUM_PC], a->sign_exp, a->frac);

    a_sign = XFP_SIGN(a);
    a_exp  = XFP_EXP(a);
    a_frac  = XFP_FRAC(a);

    if (a_exp == 0x7fff) {
        if ( a_frac & 0x7fffffffffffffffull ) {
            propagate_xfp_nan_128(a, &zero, &nan_128);
            result->sign_exp = (uint32) nan_128.high;
            result->frac = nan_128.low;
            return;
        }
        if ( ! a_sign ) {
            result->sign_exp = a->sign_exp;
            result->frac = a->frac;
        }
        /* Invalid */
        mau_exc(MAU_ASR_IS, MAU_ASR_IM);
        result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
        result->frac = DEFAULT_XFP_NAN_FRAC;
        return;
    }

    if (a_sign) {
        if (!a_frac) {
            PACK_XFP(a_sign, 0, 0, result);
            return;
        }
        mau_exc(MAU_ASR_IS, MAU_ASR_IM);
        result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
        result->frac = DEFAULT_XFP_NAN_FRAC;
        return;
    }

    if (!a_exp) {
        a_exp = 1;
    }

    if (!(a_frac & 0x8000000000000000ull)) {
        if (!a_frac) {
            PACK_XFP(a_sign, 0, 0, result);
            return;
        }
        normalize_xfp_subnormal(a->frac, &norm_exp, &norm_frac);
        a_exp += norm_exp;
        a_frac = norm_frac;
    }

    /*
     * r_frac_32 is guaranteed to be a lower bound on the square root of
     * a_frac_32, which makes r_frac_32 also a lower bound on the square
     * root of `a_frac'.
     */
    r_exp = ((a_exp - 0x3FFF) >> 1) + 0x3FFF;
    a_exp &= 1;
    a_frac_32 = a_frac >> 32;
    sqrt_recip_32 = approx_recip_sqrt_32(a_exp, a_frac_32);
    r_frac_32 = ((t_uint64) a_frac_32 * sqrt_recip_32) >> 32;

    if (a_exp) {
        r_frac_32 >>= 1;
        short_shift_left_128(0, a_frac, 61, &rem.high, &rem.low);
    } else {
        short_shift_left_128(0, a_frac, 62, &rem.high, &rem.low);
    }

    rem.high -= (t_uint64) r_frac_32 * r_frac_32;

    q = ((uint32) (rem.high >> 2) * (t_uint64) sqrt_recip_32) >> 32;
    x64 = (t_uint64) r_frac_32 << 32;
    z_frac = x64 + (q<<3);
    short_shift_left_128(rem.high, rem.low, 29, &y.high, &y.low);

    /* Repeating this loop is a rare occurrence. */
    while(1) {
        mul_64_by_shifted_32_to_128(x64 + z_frac, (uint32) q, &term);
        sub_128(y.high, y.low, term.high, term.low, &rem.high, &rem.low);
        if (!(rem.high & 0x8000000000000000ull)) {
            break;
        }
        --q;
        z_frac -= 1<<3;
    }

    q = (((rem.high>>2) * sqrt_recip_32)>>32) + 2;
    x64 = z_frac;
    z_frac = (z_frac<<1) + (q>>25);
    z_frac_extra = (t_uint64) (q<<39);

    if ( (q & 0xffffff) <= 2 ) {
        q &= ~(t_uint64) 0xffff;
        z_frac_extra = (t_uint64) (q<<39);
        mul_64_by_shifted_32_to_128(x64 + (q >> 27), (uint32) q, &term);
        x64 = (uint32) (q<<5) * (t_uint64) (uint32) q;
        add_128(term.high, term.low, 0, x64, &term.high, &term.low);
        short_shift_left_128(rem.high, rem.low, 28, &rem.high, &rem.low);
        sub_128(rem.high, rem.low, term.high, term.low, &rem.high, &rem.low);
        if (rem.high & 0x8000000000000000ull) {
            if (!z_frac_extra ) {
                --z_frac;
            }
            --z_frac_extra;
        } else {
            if (rem.high | rem.low) {
                z_frac_extra |= 1;
            }
        }
    }

    round_pack_xfp(0, r_exp, z_frac, z_frac_extra, rounding_mode,result);
    return;
}

static void xfp_remainder(XFP *a, XFP *b, XFP *result, RM rounding_mode)
{
    uint32 a_sign, r_sign;
    int32 a_exp, b_exp, exp_diff;
    t_uint64 a_frac_0, a_frac_1, b_frac;
    t_uint64 q, term_0, term_1, alt_a_frac_0, alt_a_frac_1;

    a_sign = XFP_SIGN(a);
    a_exp = XFP_EXP(a);
    a_frac_0 = XFP_FRAC(a);
    b_exp = XFP_EXP(b);
    b_frac = XFP_FRAC(b);

    if (a_exp == 0x7fff) {
        if ((t_uint64)(a_frac_0 << 1) ||
            ((b_exp == 0x7fff) && (t_uint64)(b_frac << 1))) {
            propagate_xfp_nan(a, b, result);
            return;
        }
        /* invalid */
        mau_exc(MAU_ASR_IS, MAU_ASR_IM);
        result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
        result->frac = DEFAULT_XFP_NAN_FRAC;
        return;
    }

    if (b_exp == 0x7fff) {
        if ((t_uint64)(b_frac << 1)) {
            propagate_xfp_nan(a, b, result);
        }
        result->sign_exp = a->sign_exp;
        result->frac = a->frac;
        return;
    }

    if (b_exp == 0) {
        if (b_frac == 0) {
            /* invalid */
            mau_exc(MAU_ASR_IS, MAU_ASR_IM);
            result->sign_exp = DEFAULT_XFP_NAN_SIGN_EXP;
            result->frac = DEFAULT_XFP_NAN_FRAC;
            return;
        }
        normalize_xfp_subnormal(b_frac, &b_exp, &b_frac);
    }

    if (a_exp == 0) {
        if ((t_uint64)(a_frac_0 << 1) == 0) {
            result->sign_exp = a->sign_exp;
            result->frac = a->frac;
            return;
        }
        normalize_xfp_subnormal(a_frac_0, &a_exp, &a_frac_0);
    }

    b_frac |= 0x8000000000000000ull;
    r_sign = a_sign;
    exp_diff = a_exp - b_exp;
    a_frac_1 = 0;
    if (exp_diff < 0) {
        if (exp_diff < -1) {
            result->sign_exp = a->sign_exp;
            result->frac = a->frac;
            return;
        }
        shift_right_128(a_frac_0, 0, 1, &a_frac_0, &a_frac_1);
        exp_diff = 0;
    }

    q = (b_frac <= a_frac_0);

    if (q) {
        a_frac_0 -= b_frac;
    }

    exp_diff -= 64;

    while (0 < exp_diff) {
        q = estimate_div_128_to_64(a_frac_0, a_frac_1, b_frac);
        q = (2 < q) ? q - 2 : 0;
        mul_64_to_128(b_frac, q, &term_0, &term_1);
        sub_128(a_frac_0, a_frac_1, term_0, term_1, &a_frac_0, &a_frac_1);
        short_shift_left_128(a_frac_0, a_frac_1, 62, &a_frac_0, &a_frac_1);
        exp_diff -= 62;
    }

    exp_diff += 64;

    if (0 < exp_diff) {
        q = estimate_div_128_to_64(a_frac_0, a_frac_1, b_frac);
        q = (2 < q) ? q - 2 : 0;
        q >>= 64 - exp_diff;
        mul_64_to_128(b_frac, q << (64 - exp_diff), &term_0, &term_1);
        sub_128(a_frac_0, a_frac_1, term_0, term_1, &a_frac_0, &a_frac_1);
        short_shift_left_128(0, b_frac, 64 - exp_diff, &term_0, &term_1);
        while (le_128(term_0, term_1, a_frac_0, a_frac_1)) {
            ++q;
            sub_128(a_frac_0, a_frac_1, term_0, term_1, &a_frac_0, &a_frac_1);
        }
    } else {
        term_0 = b_frac;
        term_1 = 0;
    }

    sub_128(term_0, term_1, a_frac_0, a_frac_1, &alt_a_frac_0, &alt_a_frac_1);

    if (lt_128(alt_a_frac_0, alt_a_frac_1, a_frac_0, a_frac_1) ||
        (eq_128(alt_a_frac_0, alt_a_frac_1, a_frac_0, a_frac_1) &&
         (q & 1))) {
        a_frac_0 = alt_a_frac_0;
        a_frac_1 = alt_a_frac_1;
        r_sign = r_sign ? 0 : 1;
    }

    normalize_round_pack_xfp(r_sign, b_exp + exp_diff,
                             a_frac_0, a_frac_1,
                             rounding_mode, result);
}

/*
 * Load an extended precision 80-bit IEE-754 floating point value from
 * memory or register, based on the operand's specification.
 */
static void load_src_op(uint8 op, XFP *xfp)
{
    DFP dfp;
    SFP sfp;

    switch (op) {
    case M_OP_F0:
        xfp->sign_exp = mau_state.f0.sign_exp;
        xfp->frac = mau_state.f0.frac;
        break;
    case M_OP_F1:
        xfp->sign_exp = mau_state.f1.sign_exp;
        xfp->frac = mau_state.f1.frac;
        break;
    case M_OP_F2:
        xfp->sign_exp = mau_state.f2.sign_exp;
        xfp->frac = mau_state.f2.frac;
        break;
    case M_OP_F3:
        xfp->sign_exp = mau_state.f3.sign_exp;
        xfp->frac = mau_state.f3.frac;
        break;
    case M_OP_MEM_SINGLE:
        sfp = read_w(mau_state.src, ACC_AF);
        sfp_to_xfp(sfp, xfp);
        break;
    case M_OP_MEM_DOUBLE:
        dfp = (t_uint64) read_w(mau_state.src + 4, ACC_AF);
        dfp |= ((t_uint64) read_w(mau_state.src, ACC_AF)) << 32;
        sim_debug(TRACE_DBG, &mau_dev,
                  "[load_src_op][DOUBLE] Loaded %016llx\n",
                  dfp);
        dfp_to_xfp(dfp, xfp);
        sim_debug(TRACE_DBG, &mau_dev,
                  "[load_src_op][DOUBLE] Expanded To %04x%016llx\n",
                  xfp->sign_exp, xfp->frac);
        break;
    case M_OP_MEM_TRIPLE:
        xfp->frac = (t_uint64) read_w(mau_state.src + 8, ACC_AF);
        xfp->frac |= ((t_uint64) read_w(mau_state.src + 4, ACC_AF)) << 32;
        xfp->sign_exp = (uint32) read_w(mau_state.src, ACC_AF);
        break;
    default:
        break;
    }
}

/*
 * Load OP1 as a DEC value.
 */
static void load_op1_decimal(DEC *d)
{
    uint32 low, mid, high;

    switch (mau_state.op1) {
    case M_OP_MEM_TRIPLE:
        low = read_w(mau_state.src + 8, ACC_AF);
        mid = read_w(mau_state.src + 4, ACC_AF);
        high = read_w(mau_state.src, ACC_AF);
        d->l = low;
        d->l |= ((t_uint64) mid << 32);
        d->h = high;
        break;
    default:
        /* Invalid */
        mau_exc(MAU_ASR_IS, MAU_ASR_IM);
        break;
    }
}

static void store_op3_int(uint32 val)
{
    switch(mau_state.op3) {
    case M_OP3_F0_SINGLE:
        mau_state.f0.sign_exp = 0;
        mau_state.f0.frac = (t_uint64)val;
        break;
    case M_OP3_F1_SINGLE:
        mau_state.f1.sign_exp = 0;
        mau_state.f1.frac = (t_uint64)val;
        break;
    case M_OP3_F2_SINGLE:
        mau_state.f2.sign_exp = 0;
        mau_state.f2.frac = (t_uint64)val;
        break;
    case M_OP3_F3_SINGLE:
        mau_state.f3.sign_exp = 0;
        mau_state.f3.frac = (t_uint64)val;
        break;
    case M_OP3_MEM_SINGLE:
        write_w(mau_state.dst, val);
        break;
    default:
        /* Indeterminate output, unsupported */
        break;
    }

    mau_state.dr.sign_exp = 0;
    mau_state.dr.frac = (t_uint64)val;
}

static void store_op3_decimal(DEC *d)
{

    switch(mau_state.op3) {
    case M_OP3_MEM_TRIPLE:
        write_w(mau_state.dst, d->h);
        write_w(mau_state.dst + 4, (uint32)((t_uint64)d->l >> 32));
        write_w(mau_state.dst + 8, (uint32)d->l);
        break;
    default:
        /* Unsupported */
        return;
    }

    mau_state.dr.sign_exp = d->h;
    mau_state.dr.frac = ((t_uint64)d->l >> 32) | (t_uint64)d->l;
}

static void store_op3_reg(XFP *xfp, XFP *reg)
{
    DFP dfp;
    SFP sfp;
    XFP xfp_r;

    if (mau_state.ntnan) {
        reg->sign_exp = GEN_NONTRAPPING_NAN.sign_exp;
        reg->frac = GEN_NONTRAPPING_NAN.frac;
    } else {
        switch(mau_state.op3) {
        case M_OP3_F0_SINGLE:
        case M_OP3_F1_SINGLE:
        case M_OP3_F2_SINGLE:
        case M_OP3_F3_SINGLE:
            sfp = xfp_to_sfp(xfp, MAU_RM);
            sfp_to_xfp(sfp, &xfp_r);
            reg->sign_exp = xfp_r.sign_exp;
            reg->frac = xfp_r.frac;
            reg->s = xfp_r.s;
            break;
        case M_OP3_F0_DOUBLE:
        case M_OP3_F1_DOUBLE:
        case M_OP3_F2_DOUBLE:
        case M_OP3_F3_DOUBLE:
            dfp = xfp_to_dfp(xfp, MAU_RM);
            dfp_to_xfp(dfp, &xfp_r);
            reg->sign_exp = xfp_r.sign_exp;
            reg->frac = xfp_r.frac;
            reg->s = xfp_r.s;
            break;
        case M_OP3_F0_TRIPLE:
        case M_OP3_F1_TRIPLE:
        case M_OP3_F2_TRIPLE:
        case M_OP3_F3_TRIPLE:
            reg->sign_exp = xfp->sign_exp;
            reg->frac = xfp->frac;
            reg->s = xfp->s;
            break;
        }
    }
    if (set_nz()) {
        if (XFP_SIGN(xfp)) {
            mau_state.asr |= MAU_ASR_N;
        }
        if (XFP_EXP(xfp) == 0 && XFP_FRAC(xfp) == 0) {
            mau_state.asr |= MAU_ASR_Z;
        }
    }
}

static void store_op3(XFP *xfp)
{
    DFP dfp;
    SFP sfp;
    t_bool store_dr = FALSE;

    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [store_op3] op3=%04x%016llx\n",
              R[NUM_PC],
              xfp->sign_exp,
              xfp->frac);

    switch (mau_state.opcode) {
    case M_ADD:
    case M_SUB:
    case M_MUL:
    case M_DIV:
        store_dr = TRUE;
        break;
    default:
        break;
    }

    switch (mau_state.op3) {
    case M_OP3_F0_SINGLE:
    case M_OP3_F0_DOUBLE:
    case M_OP3_F0_TRIPLE:
        store_op3_reg(xfp, &mau_state.f0);
        break;
    case M_OP3_F1_SINGLE:
    case M_OP3_F1_DOUBLE:
    case M_OP3_F1_TRIPLE:
        store_op3_reg(xfp, &mau_state.f1);
        break;
    case M_OP3_F2_SINGLE:
    case M_OP3_F2_DOUBLE:
    case M_OP3_F2_TRIPLE:
        store_op3_reg(xfp, &mau_state.f2);
        break;
    case M_OP3_F3_SINGLE:
    case M_OP3_F3_DOUBLE:
    case M_OP3_F3_TRIPLE:
        store_op3_reg(xfp, &mau_state.f3);
        break;
    case M_OP3_MEM_SINGLE:
        if (mau_state.ntnan) {
            sfp = xfp_to_sfp(&GEN_NONTRAPPING_NAN, MAU_RM);
        } else {
            sfp = xfp_to_sfp(xfp, MAU_RM);
        }
        if (set_nz()) {
            if (SFP_SIGN(sfp)) {
                mau_state.asr |= MAU_ASR_N;
            }
            if (SFP_EXP(sfp) == 0 && SFP_FRAC(sfp) == 0) {
                mau_state.asr |= MAU_ASR_Z;
            }
        }
        write_w(mau_state.dst, (uint32)sfp);
        break;
    case M_OP3_MEM_DOUBLE:
        if (mau_state.ntnan) {
            dfp = xfp_to_dfp(&GEN_NONTRAPPING_NAN, MAU_RM);
        } else {
            dfp = xfp_to_dfp(xfp, MAU_RM);
        }
        if (store_dr) {
            mau_state.dr.sign_exp = ((uint16)(DFP_SIGN(dfp)) << 15) | (uint16)(DFP_EXP(dfp));
            mau_state.dr.frac = (t_uint64)(DFP_FRAC(dfp));
            if (DFP_EXP(dfp)) {
                /* If the number is normalized, add the implicit
                   normalized bit 52 */
                mau_state.dr.frac |= ((t_uint64)1 << 52);
            }
        }
        if (set_nz()) {
            if (DFP_SIGN(dfp)) {
                mau_state.asr |= MAU_ASR_N;
            }
            if (DFP_EXP(dfp) == 0 && DFP_FRAC(dfp) == 0) {
                mau_state.asr |= MAU_ASR_Z;
            }
        }
        write_w(mau_state.dst, (uint32)(dfp >> 32));
        write_w(mau_state.dst + 4, (uint32)(dfp));
        break;
    case M_OP3_MEM_TRIPLE:
        if (mau_state.ntnan) {
            write_w(mau_state.dst, (uint32)(GEN_NONTRAPPING_NAN.sign_exp));
            write_w(mau_state.dst + 4, (uint32)(GEN_NONTRAPPING_NAN.frac >> 32));
            write_w(mau_state.dst + 8, (uint32)(GEN_NONTRAPPING_NAN.frac));
        } else {
            write_w(mau_state.dst, (uint32)(xfp->sign_exp));
            write_w(mau_state.dst + 4, (uint32)(xfp->frac >> 32));
            write_w(mau_state.dst + 8, (uint32)(xfp->frac));
        }
        if (set_nz()) {
            if (XFP_SIGN(xfp)) {
                mau_state.asr |= MAU_ASR_N;
            }
            if (XFP_EXP(xfp) == 0 && XFP_FRAC(xfp) == 0) {
                mau_state.asr |= MAU_ASR_Z;
            }
        }
        break;
    default:
        sim_debug(TRACE_DBG, &mau_dev,
                  "[store_op3] WARNING: Unhandled destination: %02x\n", mau_state.op3);
        break;
    }
}

/*************************************************************************
 *
 * MAU instruction impelementations
 *
 *************************************************************************/

static void mau_rdasr()
{
    switch (mau_state.op3) {
        /* Handled */
    case M_OP3_MEM_SINGLE:
        write_w(mau_state.dst, mau_state.asr);
        break;
    case M_OP3_MEM_DOUBLE:
        write_w(mau_state.dst, mau_state.asr);
        write_w(mau_state.dst + 4, mau_state.asr);
        break;
    case M_OP3_MEM_TRIPLE:
        write_w(mau_state.dst, mau_state.asr);
        write_w(mau_state.dst + 4, mau_state.asr);
        write_w(mau_state.dst + 8, mau_state.asr);
        break;
        /* Unhandled */
    default:
        sim_debug(TRACE_DBG, &mau_dev,
                  "[%08x] [mau_rdasr] WARNING: Unhandled source: %02x\n",
                  R[NUM_PC], mau_state.op3);
        break;
    }
}

static void mau_wrasr()
{
    switch (mau_state.op1) {
        /* Handled */
    case M_OP_MEM_SINGLE:
        mau_state.asr = read_w(mau_state.src, ACC_AF);
        sim_debug(TRACE_DBG, &mau_dev,
                  "[%08x] [WRASR] Writing ASR with: %08x\n",
                  R[NUM_PC], mau_state.asr);
        break;
    default:
        sim_debug(TRACE_DBG, &mau_dev,
                  "[%08x] [mau_wrasr] WARNING: Unhandled source: %02x\n",
                  R[NUM_PC],
                  mau_state.op3);
        break;
    }
}

/*
 * OP3 = OP1
 */
static void mau_move()
{
    XFP xfp = {0};

    load_src_op(mau_state.op1, &xfp);
    store_op3(&xfp);
}

static void mau_cmp()
{
    XFP a, b;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_cmp(&a, &b);
}

static void mau_cmps()
{
    XFP a, b;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_cmps(&a, &b);
}

static void mau_cmpe()
{
    XFP a, b;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_cmpe(&a, &b);
}

static void mau_cmpes()
{
    XFP a, b;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_cmpes(&a, &b);
}

static void mau_ldr()
{
    XFP xfp;

    load_src_op(mau_state.op1, &xfp);
    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [LDR] Loading DR with %04x%016llx\n",
              R[NUM_PC], xfp.sign_exp, xfp.frac);
    mau_state.dr.sign_exp = xfp.sign_exp;
    mau_state.dr.frac = xfp.frac;
}

static void mau_erof()
{
    DFP dfp;
    SFP sfp;

    switch (mau_state.op3) {
    case M_OP3_F0_SINGLE:
    case M_OP3_F0_DOUBLE:
    case M_OP3_F0_TRIPLE:
        mau_state.f0.sign_exp = mau_state.dr.sign_exp;
        mau_state.f0.frac = mau_state.dr.frac;
        return;
    case M_OP3_F1_SINGLE:
    case M_OP3_F1_DOUBLE:
    case M_OP3_F1_TRIPLE:
        mau_state.f1.sign_exp = mau_state.dr.sign_exp;
        mau_state.f1.frac = mau_state.dr.frac;
        return;
    case M_OP3_F2_SINGLE:
    case M_OP3_F2_DOUBLE:
    case M_OP3_F2_TRIPLE:
        mau_state.f2.sign_exp = mau_state.dr.sign_exp;
        mau_state.f2.frac = mau_state.dr.frac;
        return;
    case M_OP3_F3_SINGLE:
    case M_OP3_F3_DOUBLE:
    case M_OP3_F3_TRIPLE:
        mau_state.f3.sign_exp = mau_state.dr.sign_exp;
        mau_state.f3.frac = mau_state.dr.frac;
        return;
    case M_OP3_MEM_SINGLE:
        sfp = xfp_to_sfp(&(mau_state.dr), MAU_RM);
        write_w(mau_state.dst, (uint32)sfp);
        return;
    case M_OP3_MEM_DOUBLE:
        dfp = xfp_to_dfp(&(mau_state.dr), MAU_RM);
        write_w(mau_state.dst + 4, (uint32)(dfp >> 32));
        write_w(mau_state.dst, (uint32)(dfp));
        return;
    case M_OP3_MEM_TRIPLE:
        write_w(mau_state.dst, (uint32)(mau_state.dr.sign_exp));
        write_w(mau_state.dst + 4, (uint32)(mau_state.dr.frac >> 32));
        write_w(mau_state.dst + 8, (uint32)(mau_state.dr.frac));
        return;
    default:
        sim_debug(TRACE_DBG, &mau_dev,
                  "[mau_erof] WARNING: Unhandled destination: %02x\n", mau_state.op3);
        return;
    }
}


static void mau_rtoi()
{
    XFP a, result;

    load_src_op(mau_state.op1, &a);
    mau_round_xfp_to_int(&a, &result, MAU_RM);
    store_op3(&result);
}

static void mau_ftoi()
{
    XFP a;
    uint32 result;

    load_src_op(mau_state.op1, &a);
    result = xfp_to_int(&a, MAU_RM);
    store_op3_int(result);
}

static void mau_dtof()
{
    DEC d;
    XFP result;

    load_op1_decimal(&d);
    mau_decimal_to_xfp(&d, &result);
    store_op3(&result);
}

static void mau_ftod()
{
    XFP a;
    DEC d;

    load_src_op(mau_state.op1, &a);
    xfp_to_decimal(&a, &d, MAU_RM);
    store_op3_decimal(&d);
}

static void mau_add()
{
    XFP a, b, result;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_add(&a, &b, &result, MAU_RM);
    store_op3(&result);
}

/*
 * OP3 = OP2 - OP1
 */
static void mau_sub()
{
    XFP a, b, result;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_sub(&b, &a, &result, MAU_RM);
    store_op3(&result);
}

/*
 * OP3 = OP1 * OP2
 */
static void mau_mul()
{
    XFP a, b, result;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_mul(&b, &a, &result, MAU_RM);
    store_op3(&result);
}

/*
 * OP3 = OP1 / OP2
 */
static void mau_div()
{
    XFP a, b, result;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    sim_debug(TRACE_DBG, &mau_dev,
              "[%08x] [DIV OP2/OP1] OP2=0x%04x%016llx OP1=0x%04x%016llx\n",
              R[NUM_PC],
              b.sign_exp, b.frac,
              a.sign_exp, a.frac);
    xfp_div(&b, &a, &result, MAU_RM);
    store_op3(&result);
}

static void mau_neg()
{
    XFP a, result;

    load_src_op(mau_state.op1, &a);
    result.sign_exp = a.sign_exp;
    result.frac = a.frac;
    result.sign_exp ^= 0x8000;
    store_op3(&result);
}

static void mau_abs()
{
    XFP a, result;

    load_src_op(mau_state.op1, &a);
    result.sign_exp = a.sign_exp;
    result.frac = a.frac;
    result.sign_exp &= 0x7fff;
    store_op3(&result);
}

/*
 * OP3 = sqrt(OP1)
 */
static void mau_sqrt()
{
    XFP a, result;

    load_src_op(mau_state.op1, &a);
    xfp_sqrt(&a, &result, MAU_RM);
    store_op3(&result);
}

/*
 * OP3 = float(OP1)
 *
 * If the source operand is more than one word wide, only the last
 * word is converted.
 */
static void mau_itof()
{
    XFP xfp;
    int32 val = 0;

    mau_state.asr &= ~(MAU_ASR_N|MAU_ASR_Z);

    switch(mau_state.op1) {
    case M_OP_F0:
    case M_OP_F1:
    case M_OP_F2:
    case M_OP_F3:
        mau_exc(MAU_ASR_IS, MAU_ASR_IM);
        return;
    case M_OP_MEM_SINGLE:
        val = read_w(mau_state.src, ACC_AF);
        break;
    case M_OP_MEM_DOUBLE:
        val = read_w(mau_state.src + 4, ACC_AF);
        break;
    case M_OP_MEM_TRIPLE:
        val = read_w(mau_state.src + 8, ACC_AF);
        break;
    default:
        break;
    }
    /* Convert */
    mau_int_to_xfp(val, &xfp);

    store_op3(&xfp);
}

/*
 * OP3 = REMAINDER(b/a)
 */
static void mau_remainder()
{
    XFP a, b, result;

    load_src_op(mau_state.op1, &a);
    load_src_op(mau_state.op2, &b);
    xfp_remainder(&b, &a, &result, MAU_RM);
    store_op3(&result);
}

/*
 * Decode the command word into its corresponding parts. Both src and
 * dst are optional depending on the WE32100 operand, and may be set
 * to any value if not used.
 */
static SIM_INLINE void mau_decode(uint32 cmd, uint32 src, uint32 dst)
{
    mau_state.cmd = cmd;
    mau_state.src = src;
    mau_state.dst = dst;
    mau_state.opcode = (uint8) ((cmd & 0x7c00) >> 10);
    mau_state.op1 = (uint8) ((cmd & 0x0380) >> 7);
    mau_state.op2 = (uint8) ((cmd & 0x0070) >> 4);
    mau_state.op3 = (uint8) (cmd & 0x000f);
    sim_debug(DECODE_DBG, &mau_dev,
              "opcode=%s (%02x) op1=%s op2=%s op3=%s\n",
              mau_op_names[mau_state.opcode],
              mau_state.opcode,
              src_op_names[mau_state.op1 & 0x7],
              src_op_names[mau_state.op2 & 0x7],
              dst_op_names[mau_state.op3 & 0xf]);
}

/*
 * Handle a command.
 */
static void mau_execute()
{
    clear_asr();

    switch(mau_state.opcode) {
    case M_NOP:
        /* Do nothing */
        break;
    case M_ADD:
        mau_add();
        break;
    case M_SUB:
        mau_sub();
        break;
    case M_MUL:
        mau_mul();
        break;
    case M_DIV:
        mau_div();
        break;
    case M_RDASR:
        mau_rdasr();
        break;
    case M_WRASR:
        mau_wrasr();
        break;
    case M_MOVE:
        mau_move();
        break;
    case M_LDR:
        mau_ldr();
        break;
    case M_ITOF:
        mau_itof();
        break;
    case M_EROF:
        mau_erof();
        break;
    case M_RTOI:
        mau_rtoi();
        break;
    case M_FTOI:
        mau_ftoi();
        break;
    case M_CMP:
        mau_cmp();
        break;
    case M_CMPS:
        mau_cmps();
        break;
    case M_CMPE:
        mau_cmpe();
        break;
    case M_CMPES:
        mau_cmpes();
        break;
    case M_REM:
        mau_remainder();
        break;
    case M_NEG:
        mau_neg();
        break;
    case M_ABS:
        mau_abs();
        break;
    case M_SQRT:
        mau_sqrt();
        break;
    case M_FTOD:
        mau_ftod();
        break;
    case M_DTOF:
        mau_dtof();
        break;
    default:
        sim_debug(TRACE_DBG, &mau_dev,
                  "[execute] unhandled opcode %s [0x%02x]\n",
                  mau_op_names[mau_state.opcode],
                  mau_state.opcode);
        break;
    }

    /* If an error has occured, abort */
    abort_on_fault();

    /* Copy the N, Z, V and C (from PS) flags over to the CPU's PSW */
    R[NUM_PSW] &= ~(MAU_ASR_N|MAU_ASR_Z|MAU_ASR_IO|MAU_ASR_PS);
    R[NUM_PSW] |= (mau_state.asr & (MAU_ASR_N|MAU_ASR_Z|MAU_ASR_IO|MAU_ASR_PS));

    /* Set the RA and CSC flags in the ASR */
    mau_state.asr |= MAU_ASR_RA;
    if (mau_state.opcode != M_RDASR && mau_state.opcode != M_LDR) {
        mau_state.asr |= MAU_ASR_CSC;
    }
}

/*
 * Receive a broadcast from the CPU, and potentially handle it.
 */
t_stat mau_broadcast(uint32 cmd, uint32 src, uint32 dst)
{
    uint8 id = (uint8) ((cmd & 0xff000000) >> 24);

    /* If the MAU isn't attached, or if this message isn't for us,
     * return SCPE_NXM. Otherwise, decode and act on the command. */
    if (id != MAU_ID) {
        sim_debug(DECODE_DBG, &mau_dev,
                  "[broadcast] Message for coprocessor id %d is not for MAU (%d)\n",
                  id, MAU_ID);
        return SCPE_NXM;
    } else if (mau_dev.flags & DEV_DIS) {
        sim_debug(DECODE_DBG, &mau_dev,
                  "[broadcast] Message for MAU, but MAU is not attached.\n");
        return SCPE_NOATT;
    } else {
        mau_decode(cmd, src, dst);
        mau_execute();
        return SCPE_OK;
    }
}

CONST char *mau_description(DEVICE *dptr)
{
    return "WE32106";
}
