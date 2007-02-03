/* hp2100_cpu.h: HP 2100 CPU definitions

   Copyright (c) 2005-2006, Robert M. Supnik

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

   16-Dec-06    JDB     Added UNIT_2115 and UNIT_2114
   16-Oct-06    JDB     Moved ReadF to hp2100_cpu1.c
   26-Sep-06    JDB     Added CPU externs for microcode simulators
   16-Aug-06    JDB     Added UNIT_EMA for future RTE-4 EMA microcode
                        Added UNIT_VMA for future RTE-6 VMA and OS microcode
                        Added UNIT_1000_F for future F-Series support
   09-Aug-06    JDB     Added UNIT_DBI for double integer microcode
   21-Jan-05    JDB     Reorganized CPU option flags
   14-Jan-05    RMS     Cloned from hp2100_cpu.c

   CPU models are broken down into family, type, and series to facilitate option
   validation.  Bit 3 encodes the family, bit 2 encodes the type, and bits 1:0
   encode the series within the type.
*/

#ifndef _HP2100_CPU_H_
#define _HP2100_CPU_H_  0

/* CPU model definition flags */

#define CPU_V_SERIES    0
#define CPU_V_TYPE      2
#define CPU_V_FAMILY    3

#define FAMILY_21XX     (0 << CPU_V_FAMILY)
#define FAMILY_1000     (1 << CPU_V_FAMILY)

#define TYPE_211X       (0 << CPU_V_TYPE)               /* 2114, 2115, 2116 */
#define TYPE_2100       (1 << CPU_V_TYPE)               /* 2100A, 2100S */
#define TYPE_1000MEF    (0 << CPU_V_TYPE)               /* 1000-M, 1000-E, 1000-F */
#define TYPE_1000AL     (1 << CPU_V_TYPE)               /* 1000-L, A600, A700, A900, A990 */

#define SERIES_16       (0 << CPU_V_SERIES)             /* 211X */
#define SERIES_15       (1 << CPU_V_SERIES)             /* 211X */
#define SERIES_14       (2 << CPU_V_SERIES)             /* 211X */
#define SERIES_00       (0 << CPU_V_SERIES)             /* 2100 */
#define SERIES_M        (0 << CPU_V_SERIES)             /* 1000 */
#define SERIES_E        (1 << CPU_V_SERIES)             /* 1000 */
#define SERIES_F        (2 << CPU_V_SERIES)             /* 1000 */

/* CPU unit flags */

#define UNIT_M_CPU      017                             /* CPU model mask  [3:0] */
#define UNIT_M_TYPE     014                             /* CPU type mask   [3:2] */
#define UNIT_M_FAMILY   010                             /* CPU family mask [3:3] */

#define UNIT_V_CPU      (UNIT_V_UF + 0)                 /* CPU model bits 0-3 */
#define UNIT_V_EAU      (UNIT_V_UF + 4)                 /* EAU installed */
#define UNIT_V_FP       (UNIT_V_UF + 5)                 /* FP installed */
#define UNIT_V_IOP      (UNIT_V_UF + 6)                 /* IOP installed */
#define UNIT_V_DMS      (UNIT_V_UF + 7)                 /* DMS installed */
#define UNIT_V_FFP      (UNIT_V_UF + 8)                 /* FFP installed */
#define UNIT_V_DBI      (UNIT_V_UF + 9)                 /* DBI installed */
#define UNIT_V_EMA      (UNIT_V_UF + 10)                /* RTE-4 EMA installed */
#define UNIT_V_VMAOS    (UNIT_V_UF + 11)                /* RTE-6 VMA/OS installed */
/* Future microcode expansion; reuse flags bottom-up if needed */
#define UNIT_V_VIS      (UNIT_V_UF + 12)                /* VIS installed */
#define UNIT_V_DS       (UNIT_V_UF + 13)                /* DS installed */
#define UNIT_V_SIGNAL   (UNIT_V_UF + 14)                /* SIGNAL/1000 installed */

/* Unit models */

#define UNIT_MODEL_MASK (UNIT_M_CPU << UNIT_V_CPU)

#define UNIT_2116       ((FAMILY_21XX | TYPE_211X    | SERIES_16) << UNIT_V_CPU)
#define UNIT_2115       ((FAMILY_21XX | TYPE_211X    | SERIES_15) << UNIT_V_CPU)
#define UNIT_2114       ((FAMILY_21XX | TYPE_211X    | SERIES_14) << UNIT_V_CPU)
#define UNIT_2100       ((FAMILY_21XX | TYPE_2100    | SERIES_00) << UNIT_V_CPU)
#define UNIT_1000_M     ((FAMILY_1000 | TYPE_1000MEF | SERIES_M)  << UNIT_V_CPU)
#define UNIT_1000_E     ((FAMILY_1000 | TYPE_1000MEF | SERIES_E)  << UNIT_V_CPU)
#define UNIT_1000_F     ((FAMILY_1000 | TYPE_1000MEF | SERIES_F)  << UNIT_V_CPU)

/* Unit types */

#define UNIT_TYPE_MASK  (UNIT_M_TYPE << UNIT_V_CPU)

#define UNIT_TYPE_211X  ((FAMILY_21XX | TYPE_211X)    << UNIT_V_CPU)
#define UNIT_TYPE_2100  ((FAMILY_21XX | TYPE_2100)    << UNIT_V_CPU)
#define UNIT_TYPE_1000  ((FAMILY_1000 | TYPE_1000MEF) << UNIT_V_CPU)

/* Unit families */

#define UNIT_FAMILY_MASK    (UNIT_M_FAMILY << UNIT_V_CPU)

#define UNIT_FAMILY_21XX    (FAMILY_21XX << UNIT_V_CPU)
#define UNIT_FAMILY_1000    (FAMILY_1000 << UNIT_V_CPU)

/* Unit accessors */

#define UNIT_CPU_MODEL  (cpu_unit.flags & UNIT_MODEL_MASK)
#define UNIT_CPU_TYPE   (cpu_unit.flags & UNIT_TYPE_MASK)
#define UNIT_CPU_FAMILY (cpu_unit.flags & UNIT_FAMILY_MASK)

#define CPU_MODEL_INDEX (UNIT_CPU_MODEL >> UNIT_V_CPU)

/* Unit features */

#define UNIT_EAU        (1 << UNIT_V_EAU)
#define UNIT_FP         (1 << UNIT_V_FP)
#define UNIT_IOP        (1 << UNIT_V_IOP)
#define UNIT_DMS        (1 << UNIT_V_DMS)
#define UNIT_FFP        (1 << UNIT_V_FFP)
#define UNIT_DBI        (1 << UNIT_V_DBI)
#define UNIT_EMA        (1 << UNIT_V_EMA)
#define UNIT_VMAOS      (1 << UNIT_V_VMAOS)
#define UNIT_VIS        (1 << UNIT_V_VIS)
#define UNIT_DS         (1 << UNIT_V_DS)
#define UNIT_SIGNAL     (1 << UNIT_V_SIGNAL)

#define UNIT_EMA_VMA    (UNIT_EMA | UNIT_VMAOS)

#define UNIT_OPTS       (UNIT_EAU | UNIT_FP    | UNIT_IOP | \
                         UNIT_DMS | UNIT_FFP   | UNIT_DBI | \
                         UNIT_EMA | UNIT_VMAOS | \
                         UNIT_VIS | UNIT_DS    | UNIT_SIGNAL)

/* "Pseudo-option" flags used only for option testing; never set into UNIT structure. */

#define UNIT_V_PFAIL    (UNIT_V_UF - 1)                 /* Power fail installed */
#define UNIT_V_DMA      (UNIT_V_UF - 2)                 /* DMA installed */
#define UNIT_V_MP       (UNIT_V_UF - 3)                 /* Memory protect installed */

#define UNIT_PFAIL      (1 << UNIT_V_PFAIL)
#define UNIT_DMA        (1 << UNIT_V_DMA)
#define UNIT_MP         (1 << UNIT_V_MP)

#define UNIT_NONE       0                               /* no options */


/* PC queue. */

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = err_PC

/* CPU registers */

extern uint16 ABREG[2];                                 /* A/B regs (use AR/BR) */
extern uint32 PC;                                       /* P register */
extern uint32 SR;                                       /* S register */
extern uint32 MR;                                       /* M register */
extern uint32 TR;                                       /* T register */
extern uint32 XR;                                       /* X register */
extern uint32 YR;                                       /* Y register */
extern uint32 E;                                        /* E register */
extern uint32 O;                                        /* O register */

/* CPU state */

extern uint32 err_PC;
extern uint32 dms_enb;
extern uint32 dms_ump;
extern uint32 dms_sr;
extern uint32 dms_vr;
extern uint32 mp_fence;
extern uint32 iop_sp;
extern uint32 ion_defer;
extern uint16 pcq[PCQ_SIZE];
extern uint32 pcq_p;
extern uint32 stop_inst;
extern UNIT cpu_unit;

/* CPU functions */

t_stat resolve (uint32 MA, uint32 *addr, uint32 irq);
uint8 ReadB (uint32 addr);
uint8 ReadBA (uint32 addr);
uint16 ReadW (uint32 addr);
uint16 ReadWA (uint32 addr);
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
