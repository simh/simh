/* s100_cpu.h

   Copyright (c) 2025 Patrick A. Linstruth

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   PETER SCHORN BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Patrick Linstruth shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Patrick Linstruth.

   History:
   11/07/25 Initial version

*/

#ifndef _S100_CPU_H
#define _S100_CPU_H

#include "sim_defs.h"

#define UNIT_CPU_V_VERBOSE      (UNIT_V_UF+0)               /* Enable verbose messagesto */
#define UNIT_CPU_VERBOSE        (1 << UNIT_CPU_V_VERBOSE)

/* CPU chip types */
typedef enum {
    CHIP_TYPE_8080 = 0,
    CHIP_TYPE_Z80,
    NUM_CHIP_TYPE,      /* must be last */
} ChipType;

typedef struct {
    DEVICE   *dev;
    REG      **pc_reg;
    ChipType *chiptype;
    t_stat   (*instr)(void);
    t_value  (*pc_val)(void);
    t_stat   (*parse_sym)(const char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw);
    int32    (*dasm)(char *S, const uint32 *val, const int32 addr);
    t_bool   (*isc)(t_addr **ret_addrs);
    t_stat   (*help)(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
} CPU;

extern void cpu_set_chiptype(ChipType type);
extern char * cpu_get_chipname(ChipType type);

#endif

