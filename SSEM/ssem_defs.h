/* ssem_defs.h: Manchester University SSEM (Small Scale Experimental Machine)
                          simulator definitions 

   Based on the SIMH package written by Robert M Supnik
 
   Copyright (c) 2006-2013 Gerardo Ospina

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

   This is not a supported product, but the author welcomes bug reports and fixes.
   Mail to ngospina@gmail.com
*/

#ifndef _SSEM_DEFS_H_
#define _SSEM_DEFS_H_    0

#include "sim_defs.h"                                   /* simulator defns */

/* Simulator stop codes */

#define STOP_STOP       1                               /* STOP */
#define STOP_IBKPT      2                               /* breakpoint */

/* Memory */

#define MEMSIZE         32                              /* memory size */
#define AMASK           0x1F                            /* addr mask */

/* Architectural constants */

#define MMASK           0xFFFFFFFF                      /* memory mask */
#define IMASK           0x0000E01F                      /* instruction mask */
#define UMASK           0xFFFF1FE0                      /* unused bits mask */   
#define SMASK           0x80000000                      /* sign mask */   

/* Instruction format */

#define I_M_OP          0x07                             /* opcode */
#define I_V_OP          13
#define I_OP            (I_M_OP << I_V_OP)
#define I_GETOP(x)      (((x) >> I_V_OP) & I_M_OP)
#define I_M_EA          AMASK                           /* address */
#define I_V_EA          0
#define I_EA            (I_M_EA)
#define I_GETEA(x)      ((x) & I_M_EA)

/* Unit flags */

#define UNIT_V_SSEM    (UNIT_V_UF + 0)
#define UNIT_SSEM      (1u << UNIT_V_SSEM)

/* Instructions */

enum opcodes {
    OP_JUMP_INDIRECT, OP_JUMP_INDIRECT_RELATIVE, OP_LOAD_NEGATED, OP_STORE,
    OP_SUBSTRACT, OP_UNDOCUMENTED, OP_TEST, OP_STOP
    };

/* Prototypes */

uint32 Read (uint32 ea);
void Write (uint32 ea, uint32 dat);

#endif
