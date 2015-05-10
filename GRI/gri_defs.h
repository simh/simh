/* gri_defs.h: GRI-909 simulator definitions 

   Copyright (c) 2001-2015, Robert M. Supnik

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

   22-May-10    RMS     Added check for 64b definitions
   12-Jan-08    RMS     Added GRI-99 support
   25-Apr-03    RMS     Revised for extended file support
   19-Sep-02    RMS     Fixed declarations in gdev structure

   There are several discrepancies between the original GRI-909 Reference
   Manual of 1969 and the only surviving code sample, the MIT Crystal Growing
   System of 1972.  These discrepancies were clarified by later documentation:

   1. Ref Manual documents two GR's at codes 26-27; MITCS documents six GR's
      at 30-35.  Answer: 6 GR's, 26-27 were used for character compares.
   2. Ref Manual documents only unsigned overflow (carry) for arithmetic
      operator; MITCS uses both unsigned overflow (AOV) and signed overflow
      (SOV).  Answer: signed and unsigned.
   3. Ref Manual documents a ROM-subroutine multiply operator and mentions
      but does not document a "fast multiply"; MITCS uses an extended
      arithmetic operator with multiply, divide, and shift.  Answer: EAO
      is a package of ROM subroutines with just four functions: multiply,
      divide, arithmetic right shift, and normalize.
   4. Is SOV testable even if the FOA is not ADD?  Answer: AOV and SOV are
      calculated regardless of the function.
   5. How does the EAO handle divide overflow?  Answer: set link.
*/

#ifndef GRI_DEFS_H_
#define GRI_DEFS_H_    0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "GRI does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_DEV        1                               /* must be 1 */
#define STOP_HALT       2                               /* HALT */
#define STOP_IBKPT      3                               /* breakpoint */
#define STOP_ILLINT     4                               /* illegal intr */

/* Memory */

#define MAXMEMSIZE      32768                           /* max memory size */
#define AMASK           077777                          /* logical addr mask */
#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)

/* Architectural constants */

#define SIGN            0100000                         /* sign */
#define INDEX           0100000                         /* indexed (GRI-99) */
#define DMASK           0177777                         /* data mask */
#define CBIT            (DMASK + 1)                     /* carry bit */

/* Instruction format */

#define I_M_SRC         077                             /* source */
#define I_V_SRC         10
#define I_GETSRC(x)     (((x) >> I_V_SRC) & I_M_SRC)
#define I_M_OP          017                             /* operator */
#define I_V_OP          6
#define I_GETOP(x)      (((x) >> I_V_OP) & I_M_OP)
#define I_M_DST         077                             /* destination */
#define I_V_DST         0
#define I_GETDST(x)     (((x) >> I_V_DST) & I_M_DST)
#define SF_V_REASON     1                               /* SF reason */

/* IO return */

#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* stop on error */

/* Operators */

#define U_ZERO          000                             /* zero */
#define U_IR            001                             /* instruction reg */
#define U_FSK           002                             /* func out/skip */
#define U_TRP           003                             /* trap */
#define U_ISR           004                             /* intr status */
#define U_MA            005                             /* mem addr */
#define U_MEM           006                             /* mem data */
#define U_SC            007                             /* seq counter */
#define U_SWR           010                             /* switch register */
#define U_AX            011                             /* arith in 1 */
#define U_AY            012                             /* arith in 2 */
#define U_AO            013                             /* arith out */
#define U_EAO           014                             /* ext arith */
#define U_MSR           017                             /* machine status */
#define U_XR            022                             /* GRI-99: idx reg */
#define U_GTRP          023                             /* GRI-99: alt trap */
#define U_BSW           024                             /* byte swap */
#define U_BPK           025                             /* byte pack */
#define U_BCP1          026                             /* byte compare 1 */
#define U_BCP2          027                             /* byte compare 2 */
#define U_GR            030                             /* hex general regs */
#define U_CDR           055                             /* card reader */
#define U_CADR          057
#define U_DWC           066                             /* disk */
#define U_DCA           067
#define U_DISK          070
#define U_LPR           071                             /* line printer */
#define U_CAS           074                             /* casette */
#define U_RTC           075                             /* clock */
#define U_HS            076                             /* paper tape */
#define U_TTY           077                             /* console */

struct gdev {
    uint32      (*Src)(uint32);                         /* source */
    t_stat      (*Dst)(uint32, uint32);                 /* dest */
    t_stat      (*FO)(uint32);                          /* func out */
    uint32      (*SF)(uint32);                          /* skip func */
};

/* Trap (jump) */

#define TRP_DIR         00                              /* direct */
#define TRP_DEF         01                              /* defer */

/* Interrupt status */

#define ISR_OFF         01                              /* int off */
#define ISR_ON          02                              /* int on */

/* Bus modifiers */

#define BUS_COM         002                             /* complement */
#define BUS_FNC         014                             /* function mask */
#define BUS_P1          004                             /* + 1 */
#define BUS_L1          010                             /* rotate left */
#define BUS_R1          014                             /* rotate right */

/* Memory address modes */

#define MEM_MOD         03
#define MEM_DIR         00                              /* direct */
#define MEM_DEF         01                              /* defer */
#define MEM_IMM         02                              /* immediate */
#define MEM_IDF         03                              /* immediate defer */

/* Arithmetic unit */

#define FO_V_FOA        8                               /* arith func */
#define FO_M_FOA        03
#define OP_GET_FOA(x)   (((x) >> (FO_V_FOA - I_V_OP)) & FO_M_FOA)
#define AO_ADD          00                              /* add */
#define AO_AND          01                              /* and */
#define AO_XOR          02                              /* xor */
#define AO_IOR          03                              /* or */
#define EAO_MUL         01                              /* multiply */
#define EAO_DIV         02                              /* divide */
#define EAO_ARS         03                              /* arith rshft */
#define EAO_NORM        04                              /* normalize */

/* Machine status */

#define MSR_V_BOV       15                              /* bus carry */
#define MSR_V_L         14                              /* bus link */
#define MSR_V_FOA       8                               /* arith func */
#define MSR_M_FOA       03
#define MSR_V_SOV       1                               /* arith ovflo */
#define MSR_V_AOV       0                               /* arith carry */
#define MSR_BOV         (1u << MSR_V_BOV)
#define MSR_L           (1u << MSR_V_L)
#define MSR_FOA         (MSR_M_FOA << MSR_V_FOA)
#define MSR_SOV         (1u << MSR_V_SOV)
#define MSR_AOV         (1u << MSR_V_AOV)
#define MSR_GET_FOA(x)  (((x) >> MSR_V_FOA) & MSR_M_FOA)
#define MSR_PUT_FOA(x,n) (((x) & ~(MSR_M_FOA << MSR_V_FOA)) | \
                        (((n) & MSR_M_FOA) << MSR_V_FOA))
#define MSR_RW          (MSR_BOV|MSR_L|MSR_FOA|MSR_SOV|MSR_AOV)

/* Real time clock */

#define RTC_OFF         001                             /* off */
#define RTC_ON          002                             /* clock on */
#define RTC_OV          010                             /* clock flag */
#define RTC_CTR         0103                            /* counter */

/* Terminal */

#define TTY_ORDY        002                             /* output flag */
#define TTY_IRDY        010                             /* input flag */

/* Paper tape */

#define PT_STRT         001                             /* start reader */
#define PT_ORDY         002                             /* output flag */
#define PT_IRDY         010                             /* input flag */

/* Interrupt masks (ISR) */

#define INT_V_TTO       0                               /* console out */
#define INT_V_TTI       1                               /* console in */
#define INT_V_HSP       2                               /* paper tape punch */
#define INT_V_HSR       3                               /* paper tape reader */
#define INT_V_LPR       5                               /* line printer */
#define INT_V_CDR       7                               /* card reader */
#define INT_V_CASW      9                               /* casette */
#define INT_V_CASR      10
#define INT_V_RTC       11                              /* clock */
#define INT_V_DISK      14                              /* disk */
#define INT_V_NODEF     16                              /* nodefer */
#define INT_V_ON        17                              /* enable */
#define INT_TTO         (1u << INT_V_TTO)
#define INT_TTI         (1u << INT_V_TTI)
#define INT_HSP         (1u << INT_V_HSP)
#define INT_HSR         (1u << INT_V_HSR)
#define INT_LPR         (1u << INT_V_LPR)
#define INT_CDR         (1u << INT_V_CDR)
#define INT_CASW        (1u << INT_V_CAS1)
#define INT_CASR        (1u << INT_V_CAS2)
#define INT_RTC         (1u << INT_V_RTC)
#define INT_DISK        (1u << INT_V_DISK)
#define INT_NODEF       (1u << INT_V_NODEF)
#define INT_ON          (1u << INT_V_ON)
#define INT_PENDING     (INT_ON | INT_NODEF)

/* Vectors */

#define VEC_BKP         0000                            /* breakpoint */
#define VEC_TTO         0011                            /* console out */
#define VEC_TTI         0014                            /* console in */
#define VEC_HSP         0017                            /* paper tape punch */
#define VEC_HSR         0022                            /* paper tape reader */
#define VEC_LPR         0033                            /* line printer */
#define VEC_CDR         0033                            /* card reader */
#define VEC_CASW        0044                            /* casette */
#define VEC_CASR        0047
#define VEC_DISK        0055                            /* disk */
#define VEC_RTC         0100                            /* clock */

#endif
