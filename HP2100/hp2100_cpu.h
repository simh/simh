/* hp2100_cpu.h: HP 2100 CPU declarations

   Copyright (c) 2005-2016, Robert M. Supnik
   Copyright (c) 2017-2018, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   22-Feb-18    JDB     Reworked "cpu_ibl" into "cpu_copy_loader"
                        Cleaned up IBL definitions, added loader structure
   22-Jul-17    JDB     Renamed "intaddr" to CIR; added IR
   14-Jul-17    JDB     Removed calc_defer() prototype
   11-Jul-17    JDB     Moved "ibl_copy" and renamed to "cpu_ibl"
   10-Jul-17    JDB     Renamed the global routine "iogrp" to "cpu_iog"
   07-Jul-17    JDB     Changed "iotrap" from uint32 to t_bool
   07-Jun-17    JDB     Added maximum instruction length for sim_emax definition
   06-Jun-17    JDB     Added instruction group decoding macros
   04-Apr-17    JDB     Added "cpu_configuration" for symbolic ex/dep validation
   08-Mar-17    JDB     Added "cpu_speed" for TBG service access
   15-Feb-17    JDB     Deleted unneeded guard macro definition
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



#include <setjmp.h>



/* Memory access macros.

   These macros provide simplified function call sequences for memory reads and
   writes by the CPU.  They supply the correct access classification.  The
   following macro routines are provided:

     Name     Action
     -------  ------------------------------------------------------------
     ReadF    Read an instruction word using the current map
     ReadW    Read a data word using the current map
     ReadWA   Read a data word using the alternate map
     ReadS    Read a data word using the system map
     ReadU    Read a data word using the user map

     ReadB    Read a data byte using the current map
     ReadBA   Read a data byte using the alternate map

     WriteW   Write a data word using the current map
     WriteWA  Write a data word using the alternate map
     WriteS   Write a data word using the system map
     WriteU   Write a data word using the user map

     WriteB   Write a data byte using the current map
     WriteBA  Write a data byte using the alternate map

   The MP_ABORT macro performs a "longjmp" to the memory protect handler in the
   instruction execution loop.  The parameter is the address of the violation.
   The conditions that initiate a MP abort must be tested explicitly before
   calling MP_ABORT.
*/

#define ReadF(a)            mem_read (&cpu_dev, Fetch, a)
#define ReadW(a)            mem_read (&cpu_dev, Data, a)
#define ReadWA(a)           mem_read (&cpu_dev, Data_Alternate, a)
#define ReadS(a)            mem_read (&cpu_dev, Data_System, a)
#define ReadU(a)            mem_read (&cpu_dev, Data_User, a)

#define ReadB(a)            mem_read_byte (&cpu_dev, Data, a)
#define ReadBA(a)           mem_read_byte (&cpu_dev, Data_Alternate, a)

#define WriteW(a,v)         mem_write (&cpu_dev, Data, a, v)
#define WriteWA(a,v)        mem_write (&cpu_dev, Data_Alternate, a, v)
#define WriteS(a,v)         mem_write (&cpu_dev, Data_System, a, v)
#define WriteU(a,v)         mem_write (&cpu_dev, Data_User, a, v)

#define WriteB(a,v)         mem_write_byte (&cpu_dev, Data, a, v)
#define WriteBA(a,v)        mem_write_byte (&cpu_dev, Data_Alternate, a, v)

#define MP_ABORT(va)        longjmp (save_env, (va))


/* CPU tracing flags */

#define DEBUG_NOOS          (1u << 0)           /* configure RTE-6/VM not to use OS firmware */

#define TRACE_INSTR         (1u << 1)           /* trace instruction executions */
#define TRACE_DATA          (1u << 2)           /* trace memory data accesses */
#define TRACE_FETCH         (1u << 3)           /* trace memory instruction fetches */
#define TRACE_REG           (1u << 4)           /* trace register values */
#define TRACE_OPND          (1u << 5)           /* trace instruction operands */
#define TRACE_EXEC          (1u << 6)           /* trace matching instruction execution states */
#define TRACE_SR            (1u << 7)           /* trace service requests received */

#define TRACE_ALL           ~DEBUG_NOOS         /* trace everything */

#define DMS_FORMAT          "%c %04o %05o  %06o  "      /* map | physical page | logical address | value format */
#define REGA_FORMAT         "%c **** %05o  %06o  "      /* protection | fence | S register format for working registers */
#define REGB_FORMAT         "%c **** *****  ******  "   /* protection format for MP/MEM registers */
#define OPND_FORMAT         "* **** %05o  %06o  "       /* address | data format for operands */
#define EXEC_FORMAT         "********************  "    /* null format for EXEC separation */


/* CPU stop flags */

#define SS_INHIBIT          (t_stat) (~0u)              /* inhibit stops for the first instruction executed */

#define STOP(s)             ((s) & ~cpu_ss_inhibit)     /* stop if the condition is enabled and not inhibited */


/* Supported breakpoint switches */

#define BP_EXEC             (SWMASK ('E'))      /* an execution breakpoint */
#define BP_ENONE            (SWMASK ('N'))      /* an execution breakpoint when mapping is off */
#define BP_ESYS             (SWMASK ('S'))      /* an execution breakpoint in the system map */
#define BP_EUSER            (SWMASK ('U'))      /* an execution breakpoint in the user map */

#define BP_SUPPORTED        (BP_EXEC | BP_ENONE | BP_ESYS | BP_EUSER)


/* CPU unit flags.

      31  30  29  28  27  26  25  24  23  22  21  20  19  18  17  16
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | R | - | G | V | O | E | D | F | M | I | P | U |   CPU model   |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                                     | f | t | series|
                                                     +---+---+---+---+

   Where:

     R = reserved
     G = SIGNAL/1000 firmware is present
     V = Vector Instruction Set firmware is present
     O = RTE-6/VM VMA and OS firmware is present
     E = RTE-IV EMA firmware is present
     D = Double Integer firmware is present
     F = Fast FORTRAN Processor firmware is present
     M = Dynamic Mapping System firmware is present
     I = 2000 I/O Processor firmware is present
     P = Floating Point hardware or firmware is present
     U = Extended Arithmetic Unit is present
     f = CPU family
     t = CPU type

   CPU Models:

     0 0 00 = HP 2116
     0 0 01 = HP 2115
     0 0 10 = HP 2114
     0 0 11 = unused

     0 1 00 = HP 2100
     0 1 01 = unused
     0 1 10 = unused
     0 1 11 = unused

     1 0 00 = HP 1000 M-Series
     1 0 01 = HP 1000 E-Series
     1 0 10 = HP 1000 F-Series
     1 0 11 = unused

     1 1 00 = unused (1000 A-Series)
     1 1 01 = unused (1000 A-Series)
     1 1 10 = unused (1000 A-Series)
     1 1 11 = unused (1000 A-Series)

*/

#define CPU_V_SERIES        0
#define CPU_V_TYPE          2
#define CPU_V_FAMILY        3

#define FAMILY_21XX         (0 << CPU_V_FAMILY)
#define FAMILY_1000         (1 << CPU_V_FAMILY)

#define TYPE_211X           (0 << CPU_V_TYPE)       /* 2114, 2115, 2116 */
#define TYPE_2100           (1 << CPU_V_TYPE)       /* 2100A, 2100S */
#define TYPE_1000MEF        (0 << CPU_V_TYPE)       /* 1000-M, 1000-E, 1000-F */
#define TYPE_1000AL         (1 << CPU_V_TYPE)       /* 1000-L, A600, A700, A900, A990 */

#define SERIES_16           (0 << CPU_V_SERIES)     /* 211X */
#define SERIES_15           (1 << CPU_V_SERIES)     /* 211X */
#define SERIES_14           (2 << CPU_V_SERIES)     /* 211X */
#define SERIES_00           (0 << CPU_V_SERIES)     /* 2100 */
#define SERIES_M            (0 << CPU_V_SERIES)     /* 1000 */
#define SERIES_E            (1 << CPU_V_SERIES)     /* 1000 */
#define SERIES_F            (2 << CPU_V_SERIES)     /* 1000 */


/* CPU unit flags */

#define UNIT_M_CPU          017                     /* CPU model mask  [3:0] */
#define UNIT_M_TYPE         014                     /* CPU type mask   [3:2] */
#define UNIT_M_FAMILY       010                     /* CPU family mask [3:3] */

#define UNIT_V_CPU          (UNIT_V_UF + 0)         /* CPU model bits 0-3 */
#define UNIT_V_EAU          (UNIT_V_UF + 4)         /* EAU installed */
#define UNIT_V_FP           (UNIT_V_UF + 5)         /* FP installed */
#define UNIT_V_IOP          (UNIT_V_UF + 6)         /* IOP installed */
#define UNIT_V_DMS          (UNIT_V_UF + 7)         /* DMS installed */
#define UNIT_V_FFP          (UNIT_V_UF + 8)         /* FFP installed */
#define UNIT_V_DBI          (UNIT_V_UF + 9)         /* DBI installed */
#define UNIT_V_EMA          (UNIT_V_UF + 10)        /* RTE-4 EMA installed */
#define UNIT_V_VMAOS        (UNIT_V_UF + 11)        /* RTE-6 VMA/OS installed */
#define UNIT_V_VIS          (UNIT_V_UF + 12)        /* VIS installed */
#define UNIT_V_SIGNAL       (UNIT_V_UF + 13)        /* SIGNAL/1000 installed */
/* Future microcode expansion; reuse flags bottom-up if needed */
#define UNIT_V_DS           (UNIT_V_UF + 14)        /* DS installed */


/* Unit models */

#define UNIT_MODEL_MASK     (UNIT_M_CPU << UNIT_V_CPU)

#define UNIT_2116           ((FAMILY_21XX | TYPE_211X    | SERIES_16) << UNIT_V_CPU)
#define UNIT_2115           ((FAMILY_21XX | TYPE_211X    | SERIES_15) << UNIT_V_CPU)
#define UNIT_2114           ((FAMILY_21XX | TYPE_211X    | SERIES_14) << UNIT_V_CPU)
#define UNIT_2100           ((FAMILY_21XX | TYPE_2100    | SERIES_00) << UNIT_V_CPU)
#define UNIT_1000_M         ((FAMILY_1000 | TYPE_1000MEF | SERIES_M)  << UNIT_V_CPU)
#define UNIT_1000_E         ((FAMILY_1000 | TYPE_1000MEF | SERIES_E)  << UNIT_V_CPU)
#define UNIT_1000_F         ((FAMILY_1000 | TYPE_1000MEF | SERIES_F)  << UNIT_V_CPU)


/* Unit types */

#define UNIT_TYPE_MASK      (UNIT_M_TYPE << UNIT_V_CPU)

#define UNIT_TYPE_211X      ((FAMILY_21XX | TYPE_211X)    << UNIT_V_CPU)
#define UNIT_TYPE_2100      ((FAMILY_21XX | TYPE_2100)    << UNIT_V_CPU)
#define UNIT_TYPE_1000      ((FAMILY_1000 | TYPE_1000MEF) << UNIT_V_CPU)


/* Unit families */

#define UNIT_FAMILY_MASK    (UNIT_M_FAMILY << UNIT_V_CPU)

#define UNIT_FAMILY_21XX    (FAMILY_21XX << UNIT_V_CPU)
#define UNIT_FAMILY_1000    (FAMILY_1000 << UNIT_V_CPU)


/* Unit accessors */

#define UNIT_CPU_MODEL      (cpu_unit.flags & UNIT_MODEL_MASK)
#define UNIT_CPU_TYPE       (cpu_unit.flags & UNIT_TYPE_MASK)
#define UNIT_CPU_FAMILY     (cpu_unit.flags & UNIT_FAMILY_MASK)

#define CPU_MODEL_INDEX     (UNIT_CPU_MODEL >> UNIT_V_CPU)


/* Unit features */

#define UNIT_EAU            (1u << UNIT_V_EAU)
#define UNIT_FP             (1u << UNIT_V_FP)
#define UNIT_IOP            (1u << UNIT_V_IOP)
#define UNIT_DMS            (1u << UNIT_V_DMS)
#define UNIT_FFP            (1u << UNIT_V_FFP)
#define UNIT_DBI            (1u << UNIT_V_DBI)
#define UNIT_EMA            (1u << UNIT_V_EMA)
#define UNIT_VMAOS          (1u << UNIT_V_VMAOS)
#define UNIT_VIS            (1u << UNIT_V_VIS)
#define UNIT_DS             (1u << UNIT_V_DS)
#define UNIT_SIGNAL         (1u << UNIT_V_SIGNAL)

#define UNIT_EMA_VMA        (UNIT_EMA | UNIT_VMAOS)

#define UNIT_OPTS           (UNIT_EAU | UNIT_FP    | UNIT_IOP | \
                             UNIT_DMS | UNIT_FFP   | UNIT_DBI | \
                             UNIT_EMA | UNIT_VMAOS | \
                             UNIT_VIS | UNIT_DS    | UNIT_SIGNAL)

/* "Pseudo-option" flags used only for option testing; never set into UNIT structure. */

#define UNIT_V_PFAIL        (UNIT_V_UF - 1)                 /* Power fail installed */
#define UNIT_V_DMA          (UNIT_V_UF - 2)                 /* DMA installed */
#define UNIT_V_MP           (UNIT_V_UF - 3)                 /* Memory protect installed */

#define UNIT_PFAIL          (1 << UNIT_V_PFAIL)
#define UNIT_DMA            (1 << UNIT_V_DMA)
#define UNIT_MP             (1 << UNIT_V_MP)

#define UNIT_NONE           0                               /* no options */


/* CPU configuration model flags */

#define CPU_MASK            D16_MASK

#define CPU_2116            (1u << (FAMILY_21XX | TYPE_211X    | SERIES_16))
#define CPU_2115            (1u << (FAMILY_21XX | TYPE_211X    | SERIES_15))
#define CPU_2114            (1u << (FAMILY_21XX | TYPE_211X    | SERIES_14))
#define CPU_2100            (1u << (FAMILY_21XX | TYPE_2100    | SERIES_00))
#define CPU_1000_M          (1u << (FAMILY_1000 | TYPE_1000MEF | SERIES_M))
#define CPU_1000_E          (1u << (FAMILY_1000 | TYPE_1000MEF | SERIES_E))
#define CPU_1000_F          (1u << (FAMILY_1000 | TYPE_1000MEF | SERIES_F))

#define CPU_1000            (CPU_1000_M | CPU_1000_E | CPU_1000_F)

#define CPU_ALL             (CPU_2116   | CPU_2115   | CPU_2114 |  \
                             CPU_2100   |                          \
                             CPU_1000_M | CPU_1000_E | CPU_1000_F)

/* PC queue. */

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq [pcq_p = (pcq_p - 1) & PCQ_MASK] = (uint16) err_PC


/* Maximum instruction length.

   This value is the length in words of the longest machine instruction.  It is
   used to set "sim_emax", which, in turn, is used to allocate the "sim_eval"
   array.  This array holds the words of a machine instruction to be formatted
   and printed or to be parsed and stored.

   The longest instruction in the 21xx/1000 family is the [D]VPIV (vector pivot)
   instruction in the Vector Instruction Set.
*/

#define MAX_INSTR_LENGTH    10


/* Instruction group decoding.

   The HP 21xx/1000 instruction set consists of five groups: the Memory
   Reference Group (MRG), the Shift-Rotate Group (SRG), the Alter-Skip Group
   (ASG), the I/O Group (IOG), and the Macro Group (MAC).  Group membership is
   determined by a multi-level decoding of bits 15-10, as follows:

      Bits
     15-10   Group
     ------  -----
     xnnnx    MRG
     00000    SRG
     00001    ASG
     10001    IOG
     10000    MAC

   Where:

     x = 0 or 1
     n = any collective value other than 0

   The MAC group is subdivided into the Extended Arithmetic Group (EAG), the
   first User Instruction Group (UIG-0), and the second User Instruction Group
   (UIG-1).  Decoding is by bits 11-8, as follows (note that bit 10 = 0 for the
   MAC group):

     Bits
     11-8  Group
     ----  -----
     0000  EAG
     0001  EAG
     0010  EAG
     0011  UIG-1
     1000  EAG
     1001  EAG
     1010  UIG-0
     1011  UIG-1

   Bits 7-4 further decode the UIG instruction feature group.
*/

#define GROUP_MASK          0172000u            /* instruction group mask */

#define MRG                 0070000u            /* Memory Reference Group indicator */
#define SRG                 0000000u            /* Shift-Rotate Group indicator */
#define ASG                 0002000u            /* Alter-Skip Group indicator */
#define IOG                 0102000u            /* I/O Group indicator */

#define MRGOP(v)            (((v) & MRG) != 0)          /* MRG membership test */
#define SRGOP(v)            (((v) & GROUP_MASK) == SRG) /* SRG membership test */
#define ASGOP(v)            (((v) & GROUP_MASK) == ASG) /* ASG membership test */
#define IOGOP(v)            (((v) & GROUP_MASK) == IOG) /* IOG membership test */

#define SRG_CLE             0000040u            /* SRG CLE opcode */
#define SRG_SLx             0000010u            /* SRG SLA/SLB opcode */
#define SRG_NOP             0000000u            /* SRG no-operation opcode */

#define SRG1_DE_MASK        0001000u            /* SRG disable/enable first micro-op field bit */
#define SRG1_MASK           0001700u
#define SRG1_SHIFT          6

#define SRG2_DE_MASK        0000020u            /* SRG disable/enable second micro-op field bit */
#define SRG2_MASK           0000027u
#define SRG2_SHIFT          0

#define SRG1(u)             (((u) & SRG1_MASK) >> SRG1_SHIFT)
#define SRG2(u)             (((u) & SRG2_MASK) >> SRG2_SHIFT)

#define UIG_MASK            0000360u            /* UIG feature group mask */
#define UIG_SHIFT           4                   /* UIG feature group alignment shift */

#define UIG(i)              (((i) & UIG_MASK) >> UIG_SHIFT)

#define UIG_0_MASK          0177400u            /* UIG-0 opcode mask */
#define UIG_0_RANGE         0105000u            /* UIG-0 opcode range */

#define UIG_1_MASK          0173400u            /* UIG-1 opcode mask */
#define UIG_1_RANGE         0101400u            /* UIG-1 opcode range */

#define UIG_0_OP(i)         (((i) & UIG_0_MASK) == UIG_0_RANGE) /* UIG-0 membership test */
#define UIG_1_OP(i)         (((i) & UIG_1_MASK) == UIG_1_RANGE) /* UIG-1 membership test */

#define RTE_IRQ_RANGE       0105354             /* RTE-6/VM interrupt request instructions range */


/* Memory Reference Group instructions */

#define I_IA                0100000u            /* indirect address */
#define I_AB                0004000u            /* A/B select */
#define I_CP                0002000u            /* current page */
#define I_DISP              0001777u            /* page displacement */
#define I_PAGENO            0076000u            /* page number */

/* Alter/Skip Group instructions */

#define I_CMx               0001000u            /* CMA/B */
#define I_CLx               0000400u            /* CLA/B */
#define I_CME               0000200u            /* CME */
#define I_CLE               0000100u            /* CLE */
#define I_SEZ               0000040u            /* SEZ */
#define I_SSx               0000020u            /* SSA/B */
#define I_SLx               0000010u            /* SLA/B */
#define I_INx               0000004u            /* INA/B */
#define I_SZx               0000002u            /* SZA/B */
#define I_RSS               0000001u            /* RSS */

#define I_SSx_SLx_RSS       (I_SSx | I_SLx | I_RSS)         /* a special case */
#define I_ALL_SKIPS         (I_SEZ | I_SZx | I_SSx_SLx_RSS) /* another special case */

/* Shift/Rotate Group micro-ops */

#define I_xLS               0000000u            /* ALS/BLS */
#define I_xRS               0000001u            /* ARS/BRS */
#define I_RxL               0000002u            /* RAL/RBL */
#define I_RxR               0000003u            /* RAR/RBR */
#define I_xLR               0000004u            /* ALR/BLR */
#define I_ERx               0000005u            /* ERA/ERB */
#define I_ELx               0000006u            /* ELA/ELB */
#define I_xLF               0000007u            /* ALF/BLF */

#define SRG_DIS             0000000u
#define SRG1_EN             0000010u
#define SRG2_EN             0000020u

/* Other instructions */

#define I_NOP           0000000u                        /* no operation */
#define I_NMRMASK       0172000u                        /* non-mrf opcode */
#define I_ASKP          0002000u                        /* alter/skip */
#define I_IO            0102000u                        /* I/O */
#define I_CTL           0004000u                        /* CTL on/off */
#define I_HC            0001000u                        /* hold/clear */
#define I_DEVMASK       0000077u                        /* device select code mask */
#define I_GETIOOP(x)    (((x) >> 6) & 07u)              /* I/O sub op */

/* Instruction masks */

#define I_MRG           0074000u                        /* MRG instructions */
#define I_MRG_I         (I_MRG | I_IA)                  /* MRG indirect instruction group */
#define I_JSB           0014000u                        /* JSB instruction */
#define I_JSB_I         (I_JSB | I_IA)                  /* JSB,I instruction */
#define I_JMP           0024000u                        /* JMP instruction */
#define I_ISZ           0034000u                        /* ISZ instruction */

#define I_IOG           0107700u                        /* I/O group instruction */
#define I_SFS           0102300u                        /* SFS instruction */
#define I_STF           0102100u                        /* STF instruction */


/* Initial Binary Loader.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | ROM # | -   - |      select code      | -   -   -   -   -   - |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define IBL_WIDTH           6                           /* loader ROM address width */
#define IBL_MASK            ((1u << IBL_WIDTH) - 1)     /* loader ROM address mask (2 ** 6 - 1) */
#define IBL_MAX             ((1u << IBL_WIDTH) - 1)     /* loader ROM address maximum (2 ** 6 - 1) */
#define IBL_SIZE            (IBL_MAX + 1)               /* loader ROM size in words */

#define IBL_START           0                           /* ROM array index of the program start */
#define IBL_DMA             (IBL_MAX - 1)               /* ROM array index of the DMA configuration word */
#define IBL_FWA             (IBL_MAX - 0)               /* ROM array index of the negative starting address */
#define IBL_NA              (IBL_MAX + 1)               /* "not-applicable" ROM array index */

#define IBL_S_CLEAR         0000000u                    /* cpu_copy_loader mask to clear the S register */
#define IBL_S_NOCLEAR       0177777u                    /* cpu_copy_loader mask to preserve the S register */
#define IBL_S_NOSET         0000000u                    /* cpu_copy_loader mask to preserve the S register */

#define IBL_ROM_MASK        0140000u                    /* ROM socket selector mask */
#define IBL_SC_MASK         0007700u                    /* device select code mask */
#define IBL_USER_MASK       ~(IBL_ROM_MASK | IBL_SC_MASK)

#define IBL_ROM_SHIFT       14
#define IBL_SC_SHIFT        6

#define IBL_ROM(s)          (((s) & IBL_ROM_MASK) >> IBL_ROM_SHIFT)
#define IBL_SC(s)           (((s) & IBL_SC_MASK)  >> IBL_SC_SHIFT)

#define IBL_TO_SC(c)        ((c) << IBL_SC_SHIFT & IBL_SC_MASK)


typedef struct {
    uint32       start_index;                   /* the array index of the start of the program */
    uint32       dma_index;                     /* the array index of the DMA configuration word */
    uint32       fwa_index;                     /* the array index of the negative starting address */
    MEMORY_WORD  loader [IBL_SIZE];             /* the 64-word bootstrap loader program */
    } BOOT_LOADER;

typedef BOOT_LOADER LOADER_ARRAY [2];           /* array (21xx, 1000) of bootstrap loaders */


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

#define MAP_PAGE(r)     ((r) & MAP_M_PAG)               /* extract the page number from a map register */
#define TO_PAGE(n)      ((n) << MAP_V_PAG)              /* position the page number in a physical address */

/* MEM status register */

#define MST_ENBI        0100000u                        /* MEM enabled at interrupt */
#define MST_UMPI        0040000u                        /* User map selected at interrupt */
#define MST_ENB         0020000u                        /* MEM enabled currently */
#define MST_UMP         0010000u                        /* User map selected currently */
#define MST_PRO         0004000u                        /* Protected mode enabled currently */
#define MST_FLT         0002000u                        /* Base page portion mapped */
#define MST_FENCE       0001777u                        /* Base page fence */

/* MEM violation register */

#define MVI_V_RPR       15                              /* must be same as */
#define MVI_V_WPR       14                              /* MAP_V_xPR */
#define MVI_RPR         (1 << MVI_V_RPR)                /* rd viol */
#define MVI_WPR         (1 << MVI_V_WPR)                /* wr viol */
#define MVI_BPG         0020000u                        /* base page viol */
#define MVI_PRV         0010000u                        /* priv viol */
#define MVI_MEB         0000200u                        /* me bus enb @ viol */
#define MVI_MEM         0000100u                        /* mem enb @ viol */
#define MVI_UMP         0000040u                        /* usr map @ viol */
#define MVI_PAG         0000037u                        /* pag sel */

/* CPU registers */

#define AR              ABREG [0]               /* A = reg 0 */
#define BR              ABREG [1]               /* B = reg 1 */

extern HP_WORD ABREG [2];                       /* A/B regs (use AR/BR) */
extern HP_WORD PR;                              /* P register */
extern HP_WORD SR;                              /* S register */
extern HP_WORD MR;                              /* M register */
extern HP_WORD TR;                              /* T register */
extern HP_WORD XR;                              /* X register */
extern HP_WORD YR;                              /* Y register */
extern uint32  E;                               /* E register */
extern uint32  O;                               /* O register */

extern HP_WORD IR;                              /* Instruction Register */
extern HP_WORD CIR;                             /* Central Interrupt Register */


/* CPU state */

extern HP_WORD   err_PC;
extern uint32    dms_enb;
extern uint32    dms_ump;
extern HP_WORD   dms_sr;
extern FLIP_FLOP mp_control;
extern HP_WORD   mp_fence;
extern HP_WORD   mp_viol;
extern FLIP_FLOP mp_mevff;
extern HP_WORD   iop_sp;
extern t_bool    ion_defer;
extern uint16    pcq [PCQ_SIZE];
extern uint32    pcq_p;
extern UNIT      cpu_unit;
extern DEVICE    cpu_dev;
extern jmp_buf   save_env;
extern t_bool    mp_mem_changed;

extern t_stat    cpu_ss_unimpl;                 /* status return for unimplemented instruction execution */
extern t_stat    cpu_ss_undef;                  /* status return for undefined instruction execution */
extern t_stat    cpu_ss_unsc;                   /* status return for I/O to an unassigned select code */
extern t_stat    cpu_ss_ioerr;                  /* status return for an unreported I/O error */
extern t_stat    cpu_ss_indir;                  /* status return for indirect loop execution */
extern t_stat    cpu_ss_inhibit;                /* simulation stop inhibition mask */
extern UNIT      *cpu_ioerr_uptr;               /* pointer to a unit with an unreported I/O error */

extern uint32    cpu_configuration;             /* the current CPU option set and model */
extern uint32    cpu_speed;                     /* the CPU speed, expressed as a multiplier of a real machine */
extern t_bool    is_1000;                       /* TRUE if the CPU is a 1000 M/E/F-Series */


/* CPU global SCP support routines declared in scp.h

extern t_stat sim_instr (void);
*/


/* CPU global SCP support routines */

extern void cpu_post_cmd (t_bool from_scp);


/* CPU global utility routines */

extern t_stat  cpu_copy_loader (const LOADER_ARRAY boot, uint32 sc, HP_WORD sr_clear, HP_WORD sr_set);
extern t_stat  cpu_iog         (HP_WORD IR, t_bool iotrap);
extern uint32  calc_int        (void);
extern t_stat  resolve         (HP_WORD MA, HP_WORD *address, uint32 irq);


/* Memory global utility routines */

extern HP_WORD mem_read       (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address);
extern void    mem_write      (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address, HP_WORD value);
extern uint8   mem_read_byte  (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address);
extern void    mem_write_byte (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address, uint8 value);

extern HP_WORD mem_fast_read  (HP_WORD address, uint32 map);
extern HP_WORD mem_examine    (uint32 address);
extern void    mem_deposit    (uint32 address, HP_WORD value);


/* Memory Expansion Unit global utility routines */

extern uint16  dms_rmap   (uint32 mapi);
extern void    dms_wmap   (uint32 mapi, uint32 dat);
extern void    dms_viol   (uint32 va, HP_WORD st);
extern HP_WORD dms_upd_vr (uint32 va);
extern HP_WORD dms_upd_sr (void);


/* Memory Protect global utility routines */

extern void mp_dms_jmp (uint32 va, uint32 plb);
