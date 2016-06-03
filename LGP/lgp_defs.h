/* lgp_defs.h: LGP simulator definitions 

   Copyright (c) 2004-2010, Robert M. Supnik

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
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   22-May-10    RMS     Added check for 64b definitions
*/

#ifndef LGP_DEFS_H_
#define LGP_DEFS_H_    0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "LGP-30 does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_STOP       1                               /* STOP */
#define STOP_IBKPT      2                               /* breakpoint */
#define STOP_OVF        3                               /* overflow */
#define STOP_NXDEV      4                               /* non-existent device */
#define STOP_STALL      5                               /* IO stall */

/* Memory */

#define MEMSIZE         4096                            /* memory size */
#define AMASK           0xFFF                           /* addr mask */
#define NTK_30          64
#define NSC_30          64
#define SCMASK_30       0x03F                           /* sector mask */
#define NTK_21          32
#define NSC_21          128
#define SCMASK_21       0x07F
#define RPM             4000                            /* rev/minutes */
#define WPS             ((NSC_30 * RPM) / 60)           /* words/second */

/* Architectural constants */

#define SIGN            0x80000000                      /* sign */
#define DMASK           0xFFFFFFFF                      /* data mask */
#define MMASK           0xFFFFFFFE                      /* memory mask */

/* Instruction format */

#define I_M_OP          0xF                             /* opcode */
#define I_V_OP          16
#define I_OP            (I_M_OP << I_V_OP)
#define I_GETOP(x)      (((x) >> I_V_OP) & I_M_OP)
#define I_M_EA          AMASK                           /* address */
#define I_V_EA          2
#define I_EA            (I_M_EA << I_V_EA)
#define I_GETEA(x)      (((x) >> I_V_EA) & I_M_EA)
#define I_M_TK          0x3F                            /* LGP-30 char */
#define I_V_TK          8                               /* LGP-21 device */
#define I_GETTK(x)      (((x) >> I_V_TK) & I_M_TK)

/* Unit flags */

#define UNIT_V_LGP21    (UNIT_V_UF + 0)
#define UNIT_V_MANI     (UNIT_V_UF + 1)
#define UNIT_V_INPT     (UNIT_V_UF + 2)
#define UNIT_V_OUTPT    (UNIT_V_UF + 3)
#define UNIT_V_IN4B     (UNIT_V_UF + 4)
#define UNIT_V_TTSS_D   (UNIT_V_UF + 5)
#define UNIT_V_LGPH_D   (UNIT_V_UF + 6)
#define UNIT_V_FLEX_D   (UNIT_V_UF + 7)                 /* Flex default */
#define UNIT_V_FLEX     (UNIT_V_UF + 8)                 /* Flex format */
#define UNIT_V_NOCS     (UNIT_V_UF + 9)                 /* ignore cond stop */
#define UNIT_LGP21      (1u << UNIT_V_LGP21)
#define UNIT_MANI       (1u << UNIT_V_MANI)
#define UNIT_INPT       (1u << UNIT_V_INPT)
#define UNIT_OUTPT      (1u << UNIT_V_OUTPT)
#define UNIT_IN4B       (1u << UNIT_V_IN4B)
#define UNIT_TTSS_D     (1u << UNIT_V_TTSS_D)
#define UNIT_LGPH_D     (1u << UNIT_V_LGPH_D)
#define UNIT_FLEX_D     (1u << UNIT_V_FLEX_D)
#define UNIT_FLEX       (1u << UNIT_V_FLEX)
#define UNIT_NOCS       (1u << UNIT_V_NOCS)
#define Q_LGP21         (cpu_unit.flags & UNIT_LGP21)
#define Q_MANI          (cpu_unit.flags & UNIT_MANI)
#define Q_INPT          (cpu_unit.flags & UNIT_INPT)
#define Q_OUTPT         (cpu_unit.flags & UNIT_OUTPT)
#define Q_IN4B          (cpu_unit.flags & UNIT_IN4B)

/* IO return */

#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* stop on error */

/* Significant characters */

#define FLEX_LC         0x04
#define FLEX_UC         0x08
#define FLEX_CR         0x10
#define FLEX_BS         0x14
#define FLEX_CSTOP      0x20
#define FLEX_DEL        0x3F

/* LGP-21 device assignments */

#define DEV_PT          0
#define DEV_TT          2
#define DEV_MASK        0x1F
#define DEV_SHIFT       62

/* Instructions */

enum opcodes {
    OP_Z, OP_B, OP_Y, OP_R,
    OP_I, OP_D, OP_N, OP_M,
    OP_P, OP_E, OP_U, OP_T,
    OP_H, OP_C, OP_A, OP_S
    };

/* Prototypes */

uint32 Read (uint32 ea);
void Write (uint32 ea, uint32 dat);

/* Translation Tables */

extern const int32 flex_to_ascii[128];
extern const int32 ascii_to_flex[128];

#endif
