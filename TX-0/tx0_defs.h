/*************************************************************************
 *                                                                       *
 * $Id: tx0_defs.h 2059 2009-02-23 05:59:14Z hharte $                    *
 *                                                                       *
 * Copyright (c) 2009 Howard M. Harte.                                   *
 * Based on pdp1_defs.h, Copyright (c) 1993-2006, Robert M. Supnik       *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * of Howard M. Harte.                                                   *
 *                                                                       *
 * Module Description:                                                   *
 *     cpu TX-0 Central Processor                                        *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

#ifndef PDP1_DEFS_H_
#define PDP1_DEFS_H_   0

#include "sim_defs.h"

/* Rename of global PC variable to avoid namespace conflicts on some platforms */

#define PC PC_Global

/* Simulator stop codes */
#define STOP_RSRV       1                               /* must be 1 */
#define STOP_HALT       2                               /* HALT */
#define STOP_IBKPT      3                               /* breakpoint */

/* Memory */
#define ASIZE           16                              /* address bits */
#define MAXMEMSIZE      (1u << ASIZE)                   /* max mem size */
#define AMASK           (MAXMEMSIZE - 1)                /* address mask */
#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)

/* Architectural constants */
#define SIGN            0400000                         /* sign */
#define DMASK           0777777                         /* data mask */
#define YMASK           0017777                         /* "Y" Mask for address calculation (13 bits) */

/* I/O status flags */
#define IOS_V_LPN       17                              /* light pen */
#define IOS_V_PETR      16                              /* paper tape reader */
#define IOS_V_TTO       15                              /* typewriter out */
#define IOS_V_TTI       14                              /* typewriter in */
#define IOS_V_PTP       13                              /* paper tape punch */
#define IOS_V_DRM       12                              /* drum */
#define IOS_V_SQB       11                              /* sequence break */
#define IOS_V_PNT       3                               /* print done */
#define IOS_V_SPC       2                               /* space done */
#define IOS_V_DCS       1                               /* data comm sys */
#define IOS_V_DRP       0                               /* parallel drum busy */

#define IOS_LPN         (1 << IOS_V_LPN)
#define IOS_PETR        (1 << IOS_V_PETR)
#define IOS_TTO         (1 << IOS_V_TTO)
#define IOS_TTI         (1 << IOS_V_TTI)
#define IOS_PTP         (1 << IOS_V_PTP)
#define IOS_DRM         (1 << IOS_V_DRM)
#define IOS_SQB         (1 << IOS_V_SQB)
#define IOS_PNT         (1 << IOS_V_PNT)
#define IOS_SPC         (1 << IOS_V_SPC)
#define IOS_DCS         (1 << IOS_V_DCS)
#define IOS_DRP         (1 << IOS_V_DRP)

#define UNIT_V_MODE     (UNIT_V_UF + 0)
#define UNIT_MODE       (3 << UNIT_V_MODE)
#define UNIT_MODE_READIN       (3 << UNIT_V_MODE)
#define UNIT_MODE_TEST  (1 << UNIT_V_MODE)


#endif
