/* hp2100_cpu.h: HP 2100 CPU declarations

   Copyright (c) 2005-2016, Robert M. Supnik
   Copyright (c) 2017-2019, J. David Bryan

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

   23-Jan-19    JDB     Removed fmt_ab global declaration; now local to hp2100_cpu5.c
                        removed cpu_user_20 global declaration
   02-Aug-18    JDB     Removed cpu_ema_* helper routine global declarations
   20-Jul-18    JDB     Incorporated declarations and changelog from hp2100_cpu1.h
   10-Jul-18    JDB     Moved IBL info to hp2100_io.h
   14-Jun-18    JDB     Renamed MST_PRO to MST_PROT
   05-Jun-18    JDB     Revised I/O model
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
   26-Jan-17    JDB     Removed debug parameters from cpu_ema_* routines
   17-Jan-17    JDB     Removed register print encoding constants (now redundant)
   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Mar-13    JDB     Added declarations for the MP abort handler and CPU registers
                        Added externs for microcode helper functions
   14-Mar-13    MP      Changed guard macro name to avoid reserved namespace
   03-Jan-10    RMS     Changed declarations of mp_control, mp_mefvv, for VMS compiler
   11-Sep-08    JDB     Moved microcode function prototypes here
   15-Jul-08    JDB     Rearranged declarations with hp2100_cpu.c and hp2100_defs.h
   26-Jun-08    JDB     Added mp_control to CPU state externals
   30-Apr-08    JDB     Corrected OP_AFF to OP_AAFF for SIGNAL/1000
                        Removed unused operand patterns
   24-Apr-08    JDB     Added calc_defer() prototype
   20-Apr-08    JDB     Added DEB_VIS and DEB_SIG debug flags
   23-Feb-08    HV      Added more OP_* for SIGNAL/1000 and VIS
   28-Nov-07    JDB     Added fprint_ops, fprint_regs for debug printouts
   26-Nov-07    JDB     Added extern sim_deb, cpu_dev, DEB flags for debug printouts
   05-Nov-07    JDB     Added extern intaddr, mp_viol, mp_mevff, calc_int, dev_ctl,
                        ReadIO, WriteIO for RTE-6/VM microcode support
   19-Oct-07    JDB     Revised OP_KKKAKK operand profile to OP_CCCACC for $LOC
   16-Dec-06    JDB     Added UNIT_2115 and UNIT_2114
   16-Oct-06    JDB     Moved ReadF to hp2100_cpu1.c
                        Generalized operands for F-Series FP types
   26-Sep-06    JDB     Added CPU externs for microcode simulators
                        Split from hp2100_cpu1.c
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


/* CPU private tracing flags.

   Private flags are allocated in descending order to avoid conflicts
   with global flags that are allocated in ascending order.
*/

#define DEBUG_NOOS          (1u << 31)          /* configure RTE-6/VM not to use OS firmware */

#define TRACE_INSTR         (1u << 30)          /* trace instruction executions */
#define TRACE_DATA          (1u << 29)          /* trace memory data accesses */
#define TRACE_FETCH         (1u << 28)          /* trace memory instruction fetches */
#define TRACE_REG           (1u << 27)          /* trace register values */
#define TRACE_OPND          (1u << 26)          /* trace instruction operands */
#define TRACE_EXEC          (1u << 25)          /* trace matching instruction execution states */
#define TRACE_SR            (1u << 24)          /* trace DMA service requests received */

#define TRACE_ALL           ~DEBUG_NOOS         /* trace everything */

#define REGA_FORMAT         "%c **** %05o  %06o  "      /* protection | fence | S register format for working registers */
#define REGB_FORMAT         "%c **** *****  ******  "   /* protection format for MP/MEM registers */
#define OPND_FORMAT         "* **** %05o  %06o  "       /* address | data format for operands */
#define EXEC_FORMAT         "********************  "    /* null format for EXEC separation */
#define DMS_FORMAT          "%c %04o %05o  %06o  "      /* map | physical page | logical address | value format */


/* CPU stop flags */

#define SS_INHIBIT          (t_stat) (~0u)              /* inhibit stops for the first instruction executed */

#define STOP(s)             ((s) & ~cpu_ss_inhibit)     /* stop if the condition is enabled and not inhibited */


/* Supported breakpoint switches */

#define BP_EXEC             (SWMASK ('E'))      /* an execution breakpoint */
#define BP_ENONE            (SWMASK ('N'))      /* an execution breakpoint when mapping is off */
#define BP_ESYS             (SWMASK ('S'))      /* an execution breakpoint in the system map */
#define BP_EUSER            (SWMASK ('U'))      /* an execution breakpoint in the user map */

#define BP_SUPPORTED        (BP_EXEC | BP_ENONE | BP_ESYS | BP_EUSER)


/* PC queue */

#define PCQ_SIZE        64                      /* must be 2 ** n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq [pcq_p = (pcq_p - 1) & PCQ_MASK] = (uint16) err_PR


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

#define IOG_MASK            0001700u
#define IOG_SHIFT           6
#define IOG_OP(v)           ((IO_GROUP_OP) (((v) & IOG_MASK) >> IOG_SHIFT))

#define UIG_MASK            0000360u            /* UIG feature group mask */
#define UIG_SHIFT           4                   /* UIG feature group alignment shift */

#define UIG(i)              (((i) & UIG_MASK) >> UIG_SHIFT)

#define UIG_0_MASK          0177400u            /* UIG-0 opcode mask */
#define UIG_0_RANGE         0105000u            /* UIG-0 opcode range */

#define UIG_1_MASK          0173400u            /* UIG-1 opcode mask */
#define UIG_1_RANGE         0101400u            /* UIG-1 opcode range */

#define UIG_0_OP(i)         (((i) & UIG_0_MASK) == UIG_0_RANGE) /* UIG-0 membership test */
#define UIG_1_OP(i)         (((i) & UIG_1_MASK) == UIG_1_RANGE) /* UIG-1 membership test */

#define RTE_IRQ_RANGE       0105354u            /* RTE-6/VM interrupt request instructions range */


/* Memory Reference Group instruction register fields */

#define IR_IND              0100000u            /* indirect address bit */
#define IR_CP               0002000u            /* current page bit */
#define IR_OFFSET           0001777u            /* page offset */

#define MR_PAGE             0076000u            /* page number bits within a memory address */


/* I/O Group instruction register fields */

#define IR_HCF              0001000u            /* hold/clear flag bit */


/* General instruction register fields */

#define AB_MASK             0004000u            /* A or B register select bit */
#define AB_SHIFT            11

#define AB_SELECT(i)        (((i) & AB_MASK) >> AB_SHIFT)


/* Instruction operand processing encoding */

/* Base operand types.  Note that all address encodings must be grouped together
   after OP_ADR.
*/

#define OP_NUL          0                       /* no operand */
#define OP_IAR          1                       /* 1-word int in A reg */
#define OP_JAB          2                       /* 2-word int in A/B regs */
#define OP_FAB          3                       /* 2-word FP const in A/B regs */
#define OP_CON          4                       /* inline 1-word constant */
#define OP_VAR          5                       /* inline 1-word variable */

#define OP_ADR          6                       /* inline address */
#define OP_ADK          7                       /* addr of 1-word int const */
#define OP_ADD          8                       /* addr of 2-word int const */
#define OP_ADF          9                       /* addr of 2-word FP const */
#define OP_ADX         10                       /* addr of 3-word FP const */
#define OP_ADT         11                       /* addr of 4-word FP const */
#define OP_ADE         12                       /* addr of 5-word FP const */

#define OP_N_FLAGS      4                       /* number of bits needed for flags */
#define OP_M_FLAGS      ((1 << OP_N_FLAGS) - 1) /* mask for flag bits */

#define OP_N_F          (8 * sizeof (uint32) / OP_N_FLAGS)  /* max number of op fields */

#define OP_V_F1         (0 * OP_N_FLAGS)        /* 1st operand field */
#define OP_V_F2         (1 * OP_N_FLAGS)        /* 2nd operand field */
#define OP_V_F3         (2 * OP_N_FLAGS)        /* 3rd operand field */
#define OP_V_F4         (3 * OP_N_FLAGS)        /* 4th operand field */
#define OP_V_F5         (4 * OP_N_FLAGS)        /* 5th operand field */
#define OP_V_F6         (5 * OP_N_FLAGS)        /* 6th operand field */
#define OP_V_F7         (6 * OP_N_FLAGS)        /* 7th operand field */
#define OP_V_F8         (7 * OP_N_FLAGS)        /* 8th operand field */

/* Operand processing patterns */

#define OP_N            (OP_NUL << OP_V_F1)
#define OP_I            (OP_IAR << OP_V_F1)
#define OP_J            (OP_JAB << OP_V_F1)
#define OP_R            (OP_FAB << OP_V_F1)
#define OP_C            (OP_CON << OP_V_F1)
#define OP_V            (OP_VAR << OP_V_F1)
#define OP_A            (OP_ADR << OP_V_F1)
#define OP_K            (OP_ADK << OP_V_F1)
#define OP_D            (OP_ADD << OP_V_F1)
#define OP_X            (OP_ADX << OP_V_F1)
#define OP_T            (OP_ADT << OP_V_F1)
#define OP_E            (OP_ADE << OP_V_F1)

#define OP_IA           ((OP_IAR << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_JA           ((OP_JAB << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_JD           ((OP_JAB << OP_V_F1) | (OP_ADD << OP_V_F2))
#define OP_RC           ((OP_FAB << OP_V_F1) | (OP_CON << OP_V_F2))
#define OP_RK           ((OP_FAB << OP_V_F1) | (OP_ADK << OP_V_F2))
#define OP_RF           ((OP_FAB << OP_V_F1) | (OP_ADF << OP_V_F2))
#define OP_CV           ((OP_CON << OP_V_F1) | (OP_VAR << OP_V_F2))
#define OP_AC           ((OP_ADR << OP_V_F1) | (OP_CON << OP_V_F2))
#define OP_AA           ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_AK           ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2))
#define OP_AX           ((OP_ADR << OP_V_F1) | (OP_ADX << OP_V_F2))
#define OP_AT           ((OP_ADR << OP_V_F1) | (OP_ADT << OP_V_F2))
#define OP_KV           ((OP_ADK << OP_V_F1) | (OP_VAR << OP_V_F2))
#define OP_KA           ((OP_ADK << OP_V_F1) | (OP_ADR << OP_V_F2))
#define OP_KK           ((OP_ADK << OP_V_F1) | (OP_ADK << OP_V_F2))

#define OP_IIF          ((OP_IAR << OP_V_F1) | (OP_IAR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3))

#define OP_IAT          ((OP_IAR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADT << OP_V_F3))

#define OP_CVA          ((OP_CON << OP_V_F1) | (OP_VAR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3))

#define OP_AAA          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3))

#define OP_AAF          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3))

#define OP_AAX          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADX << OP_V_F3))

#define OP_AAT          ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADT << OP_V_F3))

#define OP_AKA          ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADR << OP_V_F3))

#define OP_AKK          ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADK << OP_V_F3))

#define OP_AXX          ((OP_ADR << OP_V_F1) | (OP_ADX << OP_V_F2) | \
                         (OP_ADX << OP_V_F3))

#define OP_ATT          ((OP_ADR << OP_V_F1) | (OP_ADT << OP_V_F2) | \
                         (OP_ADT << OP_V_F3))

#define OP_AEE          ((OP_ADR << OP_V_F1) | (OP_ADE << OP_V_F2) | \
                         (OP_ADE << OP_V_F3))

#define OP_AAXX         ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADX << OP_V_F3) | (OP_ADX << OP_V_F4))

#define OP_AAFF         ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3) | (OP_ADF << OP_V_F4))

#define OP_AAKK         ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADK << OP_V_F4))

#define OP_KKKK         ((OP_ADK << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADK << OP_V_F4))

#define OP_AAAKK        ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADK << OP_V_F4) | \
                         (OP_ADK << OP_V_F5))

#define OP_AKAKK        ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADK << OP_V_F4) | \
                         (OP_ADK << OP_V_F5))

#define OP_AAACCC       ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_CON << OP_V_F4) | \
                         (OP_CON << OP_V_F5) | (OP_CON << OP_V_F6))

#define OP_AAFFKK       ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADF << OP_V_F3) | (OP_ADF << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADK << OP_V_F6))

#define OP_AAKAKK       ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADK << OP_V_F6))

#define OP_CATAKK       ((OP_CON << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADT << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADK << OP_V_F6))

#define OP_CCCACC       ((OP_CON << OP_V_F1) | (OP_CON << OP_V_F2) | \
                         (OP_CON << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_CON << OP_V_F5) | (OP_CON << OP_V_F6))

#define OP_AAAFFKK      ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADF << OP_V_F4) | \
                         (OP_ADF << OP_V_F5) | (OP_ADK << OP_V_F6) | \
                         (OP_ADK << OP_V_F7))

#define OP_AKAKAKK      ((OP_ADR << OP_V_F1) | (OP_ADK << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_ADK << OP_V_F4) | \
                         (OP_ADR << OP_V_F5) | (OP_ADK << OP_V_F6) | \
                         (OP_ADK << OP_V_F7))

#define OP_AAKAKAKK     ((OP_ADR << OP_V_F1) | (OP_ADR << OP_V_F2) | \
                         (OP_ADK << OP_V_F3) | (OP_ADR << OP_V_F4) | \
                         (OP_ADK << OP_V_F5) | (OP_ADR << OP_V_F6) | \
                         (OP_ADK << OP_V_F7) | (OP_ADK << OP_V_F8))

#define OP_CCACACCA     ((OP_CON << OP_V_F1) | (OP_CON << OP_V_F2) | \
                         (OP_ADR << OP_V_F3) | (OP_CON << OP_V_F4) | \
                         (OP_ADR << OP_V_F5) | (OP_CON << OP_V_F6) | \
                         (OP_CON << OP_V_F7) | (OP_ADR << OP_V_F8))


/* Operand precisions (compatible with F-Series FPP):

    - S = 1-word integer
    - D = 2-word integer
    - F = 2-word single-precision floating-point
    - X = 3-word extended-precision floating-point
    - T = 4-word double-precision floating-point
    - E = 5-word expanded-exponent floating-point
    - A = null operand (operand is in the FPP accumulator)

   5-word floating-point numbers are supported by the F-Series Floating-Point
   Processor hardware, but the instruction codes are not documented.


   Implementation notes:

    1. The enumeration rdering is important, as we depend on the "fp"
       type values to reflect the number of words needed (2-5).
*/

typedef enum {                                  /* operand precision */
    in_s,                                       /*   1-word integer */
    in_d,                                       /*   2-word integer */
    fp_f,                                       /*   2-word single-precision floating-point */
    fp_x,                                       /*   3-word extended-precision floating-point */
    fp_t,                                       /*   4-word double-precision floating-point */
    fp_e,                                       /*   5-word expanded-exponent floating-point */
    fp_a                                        /*   null operand (operand is in FPP accumulator) */
    } OPSIZE;


/* Conversion from operand size to word count */

#define TO_COUNT(s)     ((s == fp_a) ? 0 : (uint32) (s + (s < fp_f)))


/* HP in-memory representation of a packed floating-point number.
   Actual value will use two, three, four, or five words, as needed.
*/

typedef HP_WORD FPK [5];


/* Operand processing types.

   NOTE: Microsoft VC++ 6.0 does not support the C99 standard, so we cannot
   initialize unions by arbitrary variant ("designated initializers").
   Therefore, we follow the C90 form of initializing via the first named
   variant.  The FPK variant must appear first in the OP structure, as we define
   a number of FPK constants in other modules.
*/

typedef union {                                 /* general operand */
    FPK     fpk;                                /* floating-point value */
    HP_WORD word;                               /* 16-bit integer */
    uint32  dword;                              /* 32-bit integer */
    } OP;

typedef OP OPS[OP_N_F];                         /* operand array */

typedef uint32 OP_PAT;                          /* operand pattern */


/* Microcode abort reasons */

typedef enum {                                  /* Abort reason passed via "longjmp" */
    Initialized = 0,
    Memory_Protect,
    Interrupt,
    Indirect_Loop
    } MICRO_ABORT;


/* CPU global register declarations */

#define AR              ABREG [0]               /* A register = memory location 0 */
#define BR              ABREG [1]               /* B register = memory location 1 */

extern HP_WORD ABREG [2];                       /* A and B registers (use AR/BR to reference) */
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
extern HP_WORD SPR;                             /* Stack Pointer Register (F-register for 2100) */


/* CPU global state */

extern DEVICE    cpu_dev;                       /* CPU device structure */

extern HP_WORD   err_PR;                        /* P register error value */
extern FLIP_FLOP cpu_interrupt_enable;          /* interrupt enable flip-flop */
extern uint16    pcq [PCQ_SIZE];                /* PC queue (must be 16-bits wide for REG array entry) */
extern uint32    pcq_p;                         /* PC queue pointer */

extern t_stat    cpu_ss_unimpl;                 /* status return for unimplemented instruction execution */
extern t_stat    cpu_ss_undef;                  /* status return for undefined instruction execution */
extern t_stat    cpu_ss_unsc;                   /* status return for I/O to an unassigned select code */
extern t_stat    cpu_ss_indir;                  /* status return for indirect loop execution */
extern t_stat    cpu_ss_inhibit;                /* simulation stop inhibition mask */
extern t_stat    cpu_ss_ioerr;                  /* status return for an unreported I/O error */
extern UNIT      *cpu_ioerr_uptr;               /* pointer to a unit with an unreported I/O error */

extern uint32    cpu_speed;                     /* the CPU speed, expressed as a multiplier of a real machine */
extern uint32    cpu_pending_interrupt;         /* the select code of a pending interrupt or zero if none */


/* Microcode dispatcher functions (grouped by cpu module number) */

extern t_stat cpu_uig_0   (uint32 intrq, t_bool int_ack);   /* [0] UIG group 0 dispatcher */
extern t_stat cpu_uig_1   (uint32 intrq);                   /* [0] UIG group 1 dispatcher */
extern t_stat cpu_ds      (void);                           /* [0] Distributed System stub */
extern t_stat cpu_user    (void);                           /* [0] User firmware dispatcher */

extern t_stat cpu_eau (void);                           /* [1] EAU group simulator */
extern t_stat cpu_iop (uint32 intrq);                   /* [1] 2000 I/O Processor */

#if !defined (HAVE_INT64)                               /* int64 support unavailable */
extern t_stat cpu_fp  (void);                           /* [1] Firmware Floating Point */
#endif

extern t_stat cpu_dms (uint32 intrq);                   /* [2] Dynamic mapping system */
extern t_stat cpu_eig (HP_WORD IR, uint32 intrq);       /* [2] Extended instruction group */

extern t_stat cpu_ffp (uint32 intrq);                   /* [3] Fast FORTRAN Processor */
extern t_stat cpu_dbi (HP_WORD IR);                     /* [3] Double-Integer instructions */

#if defined (HAVE_INT64)                                /* int64 support available */
extern t_stat cpu_fpp (HP_WORD IR);                     /* [4] Floating Point Processor */
extern t_stat cpu_sis (HP_WORD IR);                     /* [4] Scientific Instruction Set */
#endif

extern t_stat cpu_rte_ema (void);                       /* [5] RTE-IV EMA */
extern t_stat cpu_signal  (void);                       /* [5] SIGNAL/1000 Instructions */

#if defined (HAVE_INT64)                                /* int64 support available */
extern t_stat cpu_vis (void);                           /* [5] Vector Instruction Set */
#endif

extern t_stat cpu_rte_os (t_bool int_ack);              /* [6] RTE-6 OS */

extern t_stat cpu_rte_vma (void);                       /* [7] RTE-6 VMA */


/* Microcode helper functions */

extern OP     ReadOp  (HP_WORD va, OPSIZE precision);               /* generalized operand read */
extern void   WriteOp (HP_WORD va, OP operand, OPSIZE precision);   /* generalized operand write */
extern t_stat cpu_ops (OP_PAT pattern, OPS op);                     /* operand processor */


/* CPU global SCP support routine declarations declared in scp.h

extern t_stat sim_instr (void);
*/


/* CPU global SCP support routine declarations */

extern void cpu_post_cmd (t_bool from_scp);


/* CPU global utility routine declarations */

extern t_stat cpu_iog               (HP_WORD instruction);
extern t_stat cpu_resolve_indirects (t_bool is_interruptible);
extern void   cpu_microcode_abort   (MICRO_ABORT abort_reason);


/* I/O subsystem global utility routine declarations */

extern uint32 io_poll_interrupts (FLIP_FLOP interrupt_system);
