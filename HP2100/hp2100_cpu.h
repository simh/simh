/* hp2100_cpu.h: HP 2100 CPU definitions

   Copyright (c) 2005, Robert M. Supnik

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

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   21-Jan-05    JDB     Reorganized CPU option flags
   14-Jan-05    RMS     Cloned from hp2100_cpu.c

   CPU models are broken down into type and series to facilitate option
   validation.  Bits 3:2 encode the type, and bits 1:0 encode the series
   within the type.
*/

#ifndef _HP2100_CPU_H_
#define _HP2100_CPU_H_  0

#define CPU_V_SERIES    0
#define CPU_V_TYPE      2

#define TYPE_211X       0                               /* 2114, 2115, 2116 */
#define TYPE_2100       1                               /* 2100A, 2100S */
#define TYPE_21MX       2                               /* 21MX-M, 21MX-E, 21MX-F */
#define TYPE_1000A      3                               /* A600, A700, A900, A990 */

#define CPU_2116        (TYPE_211X << CPU_V_TYPE | 0)
#define CPU_2100        (TYPE_2100 << CPU_V_TYPE | 0)
#define CPU_21MX_M      (TYPE_21MX << CPU_V_TYPE | 0)
#define CPU_21MX_E      (TYPE_21MX << CPU_V_TYPE | 1)

#define UNIT_V_CPU      (UNIT_V_UF + 0)                 /* CPU model bits 0-3 */
#define UNIT_M_CPU      017                             /* CPU model mask */
#define UNIT_M_TYPE     014                             /* CPU type mask */
#define UNIT_V_EAU      (UNIT_V_UF + 4)                 /* EAU installed */
#define UNIT_V_FP       (UNIT_V_UF + 5)                 /* FP installed */
#define UNIT_V_IOP      (UNIT_V_UF + 6)                 /* IOP installed */
#define UNIT_V_DMS      (UNIT_V_UF + 7)                 /* DMS installed */
#define UNIT_V_FFP      (UNIT_V_UF + 8)                 /* FFP installed */

#define UNIT_CPU_MASK   (UNIT_M_CPU << UNIT_V_CPU)
#define UNIT_2116       (CPU_2116 << UNIT_V_CPU)
#define UNIT_2100       (CPU_2100 << UNIT_V_CPU)
#define UNIT_21MX_M     (CPU_21MX_M << UNIT_V_CPU)
#define UNIT_21MX_E     (CPU_21MX_E << UNIT_V_CPU)

#define UNIT_TYPE_MASK  (UNIT_M_TYPE << UNIT_V_CPU)
#define UNIT_TYPE_211X  ((TYPE_211X << CPU_V_TYPE) << UNIT_V_CPU)
#define UNIT_TYPE_2100  ((TYPE_2100 << CPU_V_TYPE) << UNIT_V_CPU)
#define UNIT_TYPE_21MX  ((TYPE_21MX << CPU_V_TYPE) << UNIT_V_CPU)

#define UNIT_CPU_MODEL  (cpu_unit.flags & UNIT_CPU_MASK)
#define UNIT_CPU_TYPE   (cpu_unit.flags & UNIT_TYPE_MASK)
#define CPU_TYPE        (UNIT_CPU_TYPE >> (UNIT_V_CPU + CPU_V_TYPE))

#define UNIT_EAU        (1 << UNIT_V_EAU)
#define UNIT_FP         (1 << UNIT_V_FP)
#define UNIT_IOP        (1 << UNIT_V_IOP)
#define UNIT_DMS        (1 << UNIT_V_DMS)
#define UNIT_FFP        (1 << UNIT_V_FFP)

#define UNIT_OPTS       (UNIT_EAU | UNIT_FP | UNIT_IOP | UNIT_DMS | UNIT_FFP)

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = err_PC

t_stat resolve (uint32 MA, uint32 *addr, uint32 irq);
uint8 ReadB (uint32 addr);
uint8 ReadBA (uint32 addr);
uint16 ReadW (uint32 addr);
uint16 ReadWA (uint32 addr);
uint32 ReadF (uint32 addr);
void WriteB (uint32 addr, uint32 dat);
void WriteBA (uint32 addr, uint32 dat);
void WriteW (uint32 addr, uint32 dat);
void WriteWA (uint32 addr, uint32 dat);
t_stat iogrp (uint32 ir, uint32 iotrap);
void mp_dms_jmp (uint32 va);
uint16 dms_rmap (uint32 mapi);
void dms_wmap (uint32 mapi, uint32 dat);
void dms_viol (uint32 va, uint32 st);
uint32 dms_upd_sr (void);

#endif
