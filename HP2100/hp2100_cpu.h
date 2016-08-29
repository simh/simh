/* hp2100_cpu.h: HP 2100 CPU definitions

   Copyright (c) 2005-2016, Robert M. Supnik

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

   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Mar-13    JDB     Added declarations for the MP abort handler and CPU registers
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   03-Jan-10    RMS     Changed declarations of mp_control, mp_mefvv, for VMS compiler
   15-Jul-08    JDB     Rearranged declarations with hp2100_cpu.c and hp2100_defs.h
   26-Jun-08    JDB     Added mp_control to CPU state externals
   24-Apr-08    JDB     Added calc_defer() prototype
   20-Apr-08    JDB     Added DEB_VIS and DEB_SIG debug flags
   26-Nov-07    JDB     Added extern sim_deb, cpu_dev, DEB flags for debug printouts
   05-Nov-07    JDB     Added extern intaddr, mp_viol, mp_mevff, calc_int, dev_ctl,
                        ReadIO, WriteIO for RTE-6/VM microcode support
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

#ifndef HP2100_CPU_H_
#define HP2100_CPU_H_  0

#include <setjmp.h>


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
#define UNIT_V_VIS      (UNIT_V_UF + 12)                /* VIS installed */
#define UNIT_V_SIGNAL   (UNIT_V_UF + 13)                /* SIGNAL/1000 installed */
/* Future microcode expansion; reuse flags bottom-up if needed */
#define UNIT_V_DS       (UNIT_V_UF + 14)                /* DS installed */

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

/* Debug flags */

#define DEB_OS          (1 << 0)                        /* RTE-6/VM OS firmware non-TBG processing */
#define DEB_OSTBG       (1 << 1)                        /* RTE-6/VM OS firmware TBG processing */
#define DEB_VMA         (1 << 2)                        /* RTE-6/VM VMA firmware instructions */
#define DEB_EMA         (1 << 3)                        /* RTE-6/VM EMA firmware instructions */
#define DEB_VIS         (1 << 4)                        /* E/F-Series VIS firmware instructions */
#define DEB_SIG         (1 << 5)                        /* F-Series SIGNAL/1000 firmware instructions */

/* PC queue. */

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = (uint16) err_PC

/* Memory reference instructions */

#define I_IA            0100000                         /* indirect address */
#define I_AB            0004000                         /* A/B select */
#define I_CP            0002000                         /* current page */
#define I_DISP          0001777                         /* page displacement */
#define I_PAGENO        0076000                         /* page number */

/* Other instructions */

#define I_NMRMASK       0172000                         /* non-mrf opcode */
#define I_ASKP          0002000                         /* alter/skip */
#define I_IO            0102000                         /* I/O */
#define I_CTL           0004000                         /* CTL on/off */
#define I_HC            0001000                         /* hold/clear */
#define I_DEVMASK       0000077                         /* device select code mask */
#define I_GETIOOP(x)    (((x) >> 6) & 07)               /* I/O sub op */

/* Instruction masks */

#define I_MRG           0074000                         /* MRG instructions */
#define I_MRG_I         (I_MRG | I_IA)                  /* MRG indirect instruction group */
#define I_JSB           0014000                         /* JSB instruction */
#define I_JSB_I         (I_JSB | I_IA)                  /* JSB,I instruction */
#define I_JMP           0024000                         /* JMP instruction */
#define I_ISZ           0034000                         /* ISZ instruction */

#define I_IOG           0107700                         /* I/O group instruction */
#define I_SFS           0102300                         /* SFS instruction */
#define I_STF           0102100                         /* STF instruction */

/* Memory management */

#define VA_N_OFF        10                              /* offset width */
#define VA_M_OFF        ((1 << VA_N_OFF) - 1)           /* offset mask */
#define VA_GETOFF(x)    ((x) & VA_M_OFF)
#define VA_N_PAG        (VA_N_SIZE - VA_N_OFF)          /* page width */
#define VA_V_PAG        (VA_N_OFF)                      /* page offset */
#define VA_M_PAG        ((1 << VA_N_PAG) - 1)           /* page mask */
#define VA_GETPAG(x)    (((x) >> VA_V_PAG) & VA_M_PAG)

/* Maps */

#define MAP_NUM         4                               /* num maps */
#define MAP_LNT         (1 << VA_N_PAG)                 /* map length */
#define MAP_MASK        ((MAP_NUM * MAP_LNT) - 1)
#define SMAP            0                               /* system map */
#define UMAP            (SMAP + MAP_LNT)                /* user map */
#define PAMAP           (UMAP + MAP_LNT)                /* port A map */
#define PBMAP           (PAMAP + MAP_LNT)               /* port B map */

/* DMS map entries */

#define MAP_V_RPR       15                              /* read prot */
#define MAP_V_WPR       14                              /* write prot */
#define RDPROT          (1 << MAP_V_RPR)                /* read access check */
#define WRPROT          (1 << MAP_V_WPR)                /* write access check */
#define NOPROT          0                               /* no access check */
#define MAP_RSVD        0036000                         /* reserved bits */
#define MAP_N_PAG       (PA_N_SIZE - VA_N_OFF)          /* page width */
#define MAP_V_PAG       (VA_N_OFF)
#define MAP_M_PAG       ((1 << MAP_N_PAG) - 1)
#define MAP_GETPAG(x)   (((x) & MAP_M_PAG) << MAP_V_PAG)

/* MEM status register */

#define MST_ENBI        0100000                         /* MEM enabled at interrupt */
#define MST_UMPI        0040000                         /* User map selected at inerrupt */
#define MST_ENB         0020000                         /* MEM enabled currently */
#define MST_UMP         0010000                         /* User map selected currently */
#define MST_PRO         0004000                         /* Protected mode enabled currently */
#define MST_FLT         0002000                         /* Base page portion mapped */
#define MST_FENCE       0001777                         /* Base page fence */

/* MEM violation register */

#define MVI_V_RPR       15                              /* must be same as */
#define MVI_V_WPR       14                              /* MAP_V_xPR */
#define MVI_RPR         (1 << MVI_V_RPR)                /* rd viol */
#define MVI_WPR         (1 << MVI_V_WPR)                /* wr viol */
#define MVI_BPG         0020000                         /* base page viol */
#define MVI_PRV         0010000                         /* priv viol */
#define MVI_MEB         0000200                         /* me bus enb @ viol */
#define MVI_MEM         0000100                         /* mem enb @ viol */
#define MVI_UMP         0000040                         /* usr map @ viol */
#define MVI_PAG         0000037                         /* pag sel */

/* CPU registers */

#define AR              ABREG[0]                        /* A = reg 0 */
#define BR              ABREG[1]                        /* B = reg 1 */

extern uint16 ABREG[2];                                 /* A/B regs (use AR/BR) */
extern uint32 PR;                                       /* P register */
extern uint32 SR;                                       /* S register */
extern uint32 MR;                                       /* M register */
extern uint32 TR;                                       /* T register */
extern uint32 XR;                                       /* X register */
extern uint32 YR;                                       /* Y register */
extern uint32 E;                                        /* E register */
extern uint32 O;                                        /* O register */

/* CPU state */

extern uint32    err_PC;
extern uint32    dms_enb;
extern uint32    dms_ump;
extern uint32    dms_sr;
extern uint32    dms_vr;
extern FLIP_FLOP mp_control;
extern uint32    mp_fence;
extern uint32    mp_viol;
extern FLIP_FLOP mp_mevff;
extern uint32    iop_sp;
extern t_bool    ion_defer;
extern uint32    intaddr;
extern uint16    pcq [PCQ_SIZE];
extern uint32    pcq_p;
extern uint32    stop_inst;
extern UNIT      cpu_unit;
extern DEVICE    cpu_dev;
extern REG       cpu_reg [];
extern jmp_buf   save_env;


/* CPU functions */

#define MP_ABORT(va)    longjmp (save_env, (va))

extern t_stat resolve    (uint32 MA, uint32 *addr, uint32 irq);
extern uint16 ReadPW     (uint32 pa);
extern uint8  ReadB      (uint32 va);
extern uint8  ReadBA     (uint32 va);
extern uint16 ReadW      (uint32 va);
extern uint16 ReadWA     (uint32 va);
extern uint16 ReadIO     (uint32 va, uint32 map);
extern void   WritePW    (uint32 pa, uint32 dat);
extern void   WriteB     (uint32 va, uint32 dat);
extern void   WriteBA    (uint32 va, uint32 dat);
extern void   WriteW     (uint32 va, uint32 dat);
extern void   WriteWA    (uint32 va, uint32 dat);
extern void   WriteIO    (uint32 va, uint32 dat, uint32 map);
extern t_stat iogrp      (uint32 ir, uint32 iotrap);
extern uint32 calc_int   (void);
extern t_bool calc_defer (void);
extern void   mp_dms_jmp (uint32 va, uint32 plb);
extern uint16 dms_rmap   (uint32 mapi);
extern void   dms_wmap   (uint32 mapi, uint32 dat);
extern void   dms_viol   (uint32 va, uint32 st);
extern uint32 dms_upd_vr (uint32 va);
extern uint32 dms_upd_sr (void);

#endif
