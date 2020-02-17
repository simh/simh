/* hp2100_cpu0.c: HP 2100/1000 UIG dispatcher, user microcode, and unimplemented instruction stubs

   Copyright (c) 2006-2018, J. David Bryan

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

   CPU0         UIG dispatcher, user microcode, and unimplemented firmware options

   02-Aug-18    JDB     Moved UIG dispatcher from hp2100_cpu1.c
   25-Jul-18    JDB     Use cpu_configuration instead of cpu_unit.flags for tests
   01-Aug-17    JDB     Changed .FLUN and self-tests to test for unimplemented stops
   09-May-12    JDB     Separated assignments from conditional expressions
   04-Nov-10    JDB     Removed DS note regarding PIF card (is now implemented)
   18-Sep-08    JDB     .FLUN and self-tests for VIS and SIGNAL are NOP if not present
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
                        Added "user microcode" dispatcher for unclaimed instructions
   26-Feb-08    HV      Removed and implemented "cpu_vis" and "cpu_signal"
   22-Nov-07    JDB     Removed and implemented "cpu_rte_ema"
   12-Nov-07    JDB     Removed and implemented "cpu_rte_vma" and "cpu_rte_os"
   01-Dec-06    JDB     Removed and implemented "cpu_sis".
   26-Sep-06    JDB     Created

   Primary references:
     - HP 1000 M/E/F-Series Computers Technical Reference Handbook
          (5955-0282, March 1980)
     - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
          (92851-90001, March 1981)
     - Macro/1000 Reference Manual
          (92059-90001, December 1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.


   This module contains the User Instruction Group (a.k.a. "Macro") dispatcher
   for the 2100 and 1000 (21MX) CPUs.  The UIG simulators reside in separate
   modules, due to the large number of firmware options available for these
   machines.  Unit flags indicate which options are present in the current
   system.

   It also contains a user-microprogram dispatcher to allow simulation of
   site-specific firmware.  All UIG instructions unclaimed by installed firmware
   options are directed here and may be simulated by writing the appropriate
   code.

   The module also contains template simulations for the firmware options that
   have not yet been implemented.  When a given firmware option is implemented,
   it should be moved out of this file and into another (or its own, depending
   on complexity).

   Finally, this module provides generalized instruction operand processing.

   The 2100 and 1000 machines were microprogrammable; the 2116/15/14 machines
   were not.  Both user- and HP-written microprograms were supported.  The
   microcode address space of the 2100 encompassed four modules of 256 words
   each.  The 1000 M-series expanded that to sixteen modules, and the 1000
   E/F-series expanded that still further to sixty-four modules.  Each CPU had
   its own microinstruction set, although the micromachines of the various 1000
   models were similar internally.

   The UIG instructions were divided into ranges assigned to HP firmware
   options, reserved for future HP use, and reserved for user microprograms.
   User microprograms could occupy any range not already used on a given
   machine, but in practice, some effort was made to avoid the HP-reserved
   ranges.

   User microprogram simulation is supported by routing any UIG instruction not
   allocated to an installed firmware option to a user-firmware dispatcher.
   Site-specific microprograms may be simulated there.  In the absence of such a
   simulation, an unimplemented instruction stop will occur.

   Regarding option instruction sets, there was some commonality across CPU
   types.  EAU instructions were identical across all models, and the floating
   point set was the same on the 2100 and 1000.  Other options implemented
   proper instruction supersets (e.g., the Fast FORTRAN Processor from 2100 to
   1000-M to 1000-E to 1000-F) or functional equivalence with differing code
   points (the 2000 I/O Processor from 2100 to 1000, and the extended-precision
   floating-point instructions from 1000-E to 1000-F).

   The 2100 decoded the EAU and UIG sets separately in hardware and supported
   only the UIG 0 code points.  Bits 7-4 of a UIG instruction decoded one of
   sixteen entry points in the lowest-numbered module after module 0.  Those
   entry points could be used directly (as for the floating-point instructions),
   or additional decoding based on bits 3-0 could be implemented.

   The 1000 generalized the instruction decoding to a series of microcoded
   jumps, based on the bits in the instruction.  Bits 15-8 indicated the group
   of the current instruction: EAU (200, 201, 202, 210, and 211), UIG 0 (212),
   or UIG 1 (203 and 213).  UIG 0, UIG 1, and some EAU instructions were decoded
   further by selecting one of sixteen modules within the group via bits 7-4.
   Finally, each UIG module decoded up to sixteen instruction entry points via
   bits 3-0.  Jump tables for all firmware options were contained in the base
   set, so modules needed only to be concerned with decoding their individual
   entry points within the module.

   While the 2100 and 1000 hardware decoded these instruction sets differently,
   the decoding mechanism of the simulation follows that of the 1000 E/F-series.
   Where needed, CPU type- or model-specific behavior is simulated.

   The design of the 1000 microinstruction set was such that executing an
   instruction for which no microcode was present (e.g., executing a FFP
   instruction when the FFP firmware was not installed) resulted in a NOP.
   Under simulation, such execution causes an unimplemented instruction stop if
   "STOP (cpu_ss_unimpl)" is non-zero and a no-operation otherwise.
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"



/* UIG 0

   The first User Instruction Group (UIG) encodes firmware options for the 2100
   and 1000.  Instruction codes 105000-105377 are assigned to microcode options
   as follows:

     Instructions   Option Name                  2100   1000-M  1000-E  1000-F
     -------------  --------------------------  ------  ------  ------  ------
     105000-105362  2000 I/O Processor           opt      -       -       -
     105000-105137  Floating Point               opt     std     std     std
     105200-105237  Fast FORTRAN Processor       opt     opt     opt     std
     105240-105257  RTE-IVA/B Extended Memory     -       -      opt     opt
     105240-105257  RTE-6/VM Virtual Memory       -       -      opt     opt
     105300-105317  Distributed System            -       -      opt     opt
     105320-105337  Double Integer                -       -      opt      -
     105320-105337  Scientific Instruction Set    -       -       -      std
     105340-105357  RTE-6/VM Operating System     -       -      opt     opt

   If the 2100 IOP is installed, the only valid UIG instructions are IOP
   instructions, as the IOP used the full 2100 microcode addressing space.  The
   IOP dispatcher remaps the 2100 codes to 1000 codes for execution.

   The F-Series moved the three-word extended real instructions from the FFP
   range to the base floating-point range and added four-word double real and
   two-word double integer instructions.  The double integer instructions
   occupied some of the vacated extended real instruction codes in the FFP, with
   the rest assigned to the floating-point range.  Consequently, many
   instruction codes for the F-Series are different from the E-Series.

   Implementation notes:

    1. Product 93585A, available from the "Specials" group, added double integer
       microcode to the E-Series.  The instruction codes were different from
       those in the F-Series to avoid conflicting with the E-Series FFP.

    2. To run the double-integer instructions diagnostic in the absence of
       64-bit integer support (and therefore of F-Series simulation), a special
       DBI dispatcher may be enabled by defining ENABLE_DIAG during compilation.
       This dispatcher will remap the F-Series DBI instructions to the E-Series
       codes, so that the F-Series diagnostic may be run.  Because several of
       the F-Series DBI instruction codes replace M/E-Series FFP codes, this
       dispatcher will only operate if FFP is disabled.

       Note that enabling the dispatcher will produce non-standard FP behavior.
       For example, any code in the range 105000-105017 normally would execute a
       FAD instruction.  With the dispatcher enabled, 105014 would execute a
       .DAD, while the other codes would execute a FAD.  Therefore, ENABLE_DIAG
       should only be used to run the diagnostic and is not intended for general
       use.

    3. Any instruction not claimed by an installed option will be sent to the
       user microcode dispatcher.
*/

t_stat cpu_uig_0 (uint32 intrq, t_bool int_ack)
{
const CPU_OPTION_SET cpu_2100_iop = CPU_2100 | CPU_IOP;

if ((cpu_configuration & cpu_2100_iop) == cpu_2100_iop) /* if the CPU is a 2100 with IOP firmware installed */
    return cpu_iop (intrq);                             /*   then dispatch to the IOP executor */

#if !defined (HAVE_INT64) && defined (ENABLE_DIAG)      /* special DBI diagnostic dispatcher */

if ((cpu_configuration & (CPU_FFP | CPU_DBI)) == CPU_DBI)   /* if FFP is absent and DBI is present */
    switch (IR & 0377) {                                    /*   then remap the F-series codes to the E-series */
        case 0014:                                      /* .DAD 105014 */
            return cpu_dbi (0105321u);

        case 0034:                                      /* .DSB 105034 */
            return cpu_dbi (0105327u);

        case 0054:                                      /* .DMP 105054 */
            return cpu_dbi (0105322u);

        case 0074:                                      /* .DDI 105074 */
            return cpu_dbi (0105325u);

        case 0114:                                      /* .DSBR 105114 */
            return cpu_dbi (0105334u);

        case 0134:                                      /* .DDIR 105134 */
            return cpu_dbi (0105326u);

        case 0203:                                      /* .DNG 105203 */
            return cpu_dbi (0105323u);

        case 0204:                                      /* .DCO 105204 */
            return cpu_dbi (0105324u);

        case 0210:                                      /* .DIN 105210 */
            return cpu_dbi (0105330u);

        case 0211:                                      /* .DDE 105211 */
            return cpu_dbi (0105331u);

        case 0212:                                      /* .DIS 105212 */
            return cpu_dbi (0105332u);

        case 0213:                                      /* .DDS 105213 */
            return cpu_dbi (0105333u);
        }                                               /* otherwise, continue */

#endif                                                  /* end of special DBI dispatcher */


switch ((IR >> 4) & 017) {                              /* decode IR<7:4> */

    case 000:                                           /* 105000-105017 */
    case 001:                                           /* 105020-105037 */
    case 002:                                           /* 105040-105057 */
    case 003:                                           /* 105060-105077 */
    case 004:                                           /* 105100-105117 */
    case 005:                                           /* 105120-105137 */
        if (cpu_configuration & CPU_FP)                 /* FP option installed? */
#if defined (HAVE_INT64)                                /* int64 support available */
            return cpu_fpp (IR);                        /* Floating Point Processor */
#else                                                   /* int64 support unavailable */
            return cpu_fp ();                           /* Firmware Floating Point */
#endif                                                  /* end of int64 support */
        else
            break;

    case 010:                                           /* 105200-105217 */
    case 011:                                           /* 105220-105237 */
        if (cpu_configuration & CPU_FFP)                /* FFP option installed? */
            return cpu_ffp (intrq);                     /* Fast FORTRAN Processor */
        else
            break;

    case 012:                                           /* 105240-105257 */
        if (cpu_configuration & CPU_VMAOS)              /* VMA/OS option installed? */
            return cpu_rte_vma ();                      /* RTE-6 VMA */
        else if (cpu_configuration & CPU_EMA)           /* EMA option installed? */
            return cpu_rte_ema ();                      /* RTE-4 EMA */
        else
            break;

    case 014:                                           /* 105300-105317 */
        if (cpu_configuration & CPU_DS)                 /* DS option installed? */
            return cpu_ds ();                           /* Distributed System */
        else
            break;

    case 015:                                           /* 105320-105337 */
#if defined (HAVE_INT64)                                /* int64 support available */
        if (cpu_configuration & CPU_1000_F)             /* F-series? */
            return cpu_sis (IR);                        /* Scientific Instruction is standard */
        else                                            /* M/E-series */
#endif                                                  /* end of int64 support */
        if (cpu_configuration & CPU_DBI)                /* DBI option installed? */
            return cpu_dbi (IR);                        /* Double integer */
        else
            break;

    case 016:                                           /* 105340-105357 */
        if (cpu_configuration & CPU_VMAOS)              /* VMA/OS option installed? */
            return cpu_rte_os (int_ack);                /* RTE-6 OS */
        else
            break;
    }

return cpu_user ();                                     /* try user microcode */
}


/* UIG 1

   The second User Instruction Group (UIG) encodes firmware options for the
   1000.  Instruction codes 101400-101777 and 105400-105777 are assigned to
   microcode options as follows ("x" is "1" or "5" below):

     Instructions   Option Name                   1000-M  1000-E  1000-F
     -------------  ----------------------------  ------  ------  ------
     10x400-10x437  2000 IOP                       opt     opt      -
     10x460-10x477  2000 IOP                       opt     opt      -
     10x460-10x477  Vector Instruction Set          -       -      opt
     10x520-10x537  Distributed System             opt      -       -
     10x600-10x617  SIGNAL/1000 Instruction Set     -       -      opt
     10x700-10x737  Dynamic Mapping System         opt     opt     std
     10x740-10x777  Extended Instruction Group     std     std     std

   Only 1000 systems execute these instructions.

   Implementation notes:

    1. The Distributed System (DS) microcode was mapped to different instruction
       ranges for the M-Series and the E/F-Series.  The sequence of instructions
       was identical, though, so we remap the former range to the latter before
       dispatching.

    2. Any instruction not claimed by an installed option will be sent to the
       user microcode dispatcher.
*/

t_stat cpu_uig_1 (uint32 intrq)
{
if (!(cpu_configuration & CPU_1000))                    /* if the CPU is not a 1000 */
    return STOP (cpu_ss_unimpl);                        /*   the the instruction is unimplemented */

switch ((IR >> 4) & 017) {                              /* decode IR<7:4> */

    case 000:                                           /* 105400-105417 */
    case 001:                                           /* 105420-105437 */
        if (cpu_configuration & CPU_IOP)                /* IOP option installed? */
            return cpu_iop (intrq);                     /* 2000 I/O Processor */
        else
            break;

    case 003:                                           /* 105460-105477 */
#if defined (HAVE_INT64)                                /* int64 support available */
        if (cpu_configuration & CPU_VIS)                /* VIS option installed? */
            return cpu_vis ();                          /* Vector Instruction Set */
        else
#endif                                                  /* end of int64 support */
        if (cpu_configuration & CPU_IOP)                /* IOP option installed? */
            return cpu_iop (intrq);                     /* 2000 I/O Processor */
        else
            break;

    case 005:                                           /* 105520-105537 */
        if (cpu_configuration & CPU_DS) {               /* DS option installed? */
            IR = IR ^ 0000620;                          /* remap to 105300-105317 */
            return cpu_ds ();                           /* Distributed System */
            }
        else
            break;

#if defined (HAVE_INT64)                                /* int64 support available */
    case 010:                                           /* 105600-105617 */
        if (cpu_configuration & CPU_SIGNAL)             /* SIGNAL option installed? */
            return cpu_signal ();                       /* SIGNAL/1000 Instructions */
        else
            break;
#endif                                                  /* end of int64 support */

    case 014:                                           /* 105700-105717 */
    case 015:                                           /* 105720-105737 */
        if (cpu_configuration & CPU_DMS)                /* DMS option installed? */
            return cpu_dms (intrq);                     /* Dynamic Mapping System */
        else
            break;

    case 016:                                           /* 105740-105757 */
    case 017:                                           /* 105760-105777 */
        return cpu_eig (IR, intrq);                     /* Extended Instruction Group */
    }

return cpu_user ();                                     /* try user microcode */
}


/* Distributed System.

   Distributed System firmware was provided with the HP 91740A DS/1000 product
   for use with the HP 12771A (12665A) Serial Interface and 12773A Modem
   Interface system interconnection kits.  Firmware permitted high-speed
   transfers with minimum impact to the processor.  The advent of the
   "intelligent" 12794A and 12825A HDLC cards, the 12793A and 12834A Bisync
   cards, and the 91750A DS-1000/IV software obviated the need for CPU firmware,
   as essentially the firmware was moved onto the I/O cards.

   Primary documentation for the DS instructions has not been located.  However,
   examination of the DS/1000 sources reveals that two instruction were used by
   the DVA65 Serial Interface driver (91740-18071) and placed in the trap cells
   of the communications interfaces.  Presumably they handled interrupts from
   the cards.

   Implementation of the DS instructions will also require simulation of the
   12665A Hardwired Serial Data Interface Card.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A    91740A  91740B  91740B

   The routines are mapped to instruction codes as follows:

     Instr.  1000-M  1000-E/F  Description
     ------  ------  --------  ----------------------------------------------
             105520   105300   "Open loop" (trap cell handler)
             105521   105301   "Closed loop" (trap cell handler)
             105522   105302   [unknown]
     [test]  105524   105304   [self test]
               --     105310   7974 boot loader ROM extension

   Notes:

     1. The E/F-Series opcodes were moved from 105340-357 to 105300-317 at
        revision 1813.

     2. DS/1000 ROM data are available from Bitsavers.

   Additional references (documents unavailable):
    - HP 91740A M-Series Distributed System (DS/1000) Firmware Installation
                Manual (91740-90007).
    - HP 91740B Distributed System (DS/1000) Firmware Installation Manual
                (91740-90009).
*/

static const OP_PAT op_ds[16] = {
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N                     /*  ---    ---    ---    ---  */
  };

t_stat cpu_ds (void)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

entry = IR & 017;                                       /* mask to entry point */

if (op_ds [entry] != OP_N) {
    reason = cpu_ops (op_ds[entry], op);                /* get instruction operands */
    if (reason != SCPE_OK)                              /* did the evaluation fail? */
        return reason;                                  /* return the reason for failure */
    }

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
    }

return reason;
}


/* User firmware dispatcher.

   All UIG instructions unclaimed by installed firmware options are directed
   here.  User- or site-specific firmware may be simulated by dispatching to the
   appropriate simulator routine.  Unimplemented instructions should return
   "STOP (cpu_ss_unimpl)" to cause a simulator stop if enabled.

   Implementation notes:

    1. This routine may be passed any opcode in the ranges 101400-101737 and
       105000-105737.  The 10x740-777 range is dedicated to the EIG instructions
       and is unavailable for user microprograms.

    2. HP operating systems and subsystems depend on the following instructions
       to execute as NOP and return success if the corresponding firmware is not
       installed:

         105226  --  Fast FORTRAN Processor .FLUN instruction
         105355  --  RTE-6/VM OS self-test instruction
         105477  --  Vector Instruction Set self-test
         105617  --  SIGNAL/1000 self-test

       These instructions are executed to determine firmware configuration
       dynamically.  If you use any of these opcodes for your own use, be aware
       that certain HP programs may fail.

    3. User microprograms occupied one or more firmware modules, each containing
       16 potential instruction entry points.  A skeleton dispatcher for the 32
       possible modules is implemented below, along with a sample module.
*/

static t_stat cpu_user_20 (void);                       /* [0] Module 20 user microprograms stub */


t_stat cpu_user (void)
{
t_stat reason = SCPE_OK;

if (cpu_configuration & CPU_211X)                       /* 2116/15/14 CPU? */
    return STOP (cpu_ss_unimpl);                        /* user microprograms not supported */

switch ((IR >> 4) & 037) {                              /* decode IR<8:4> */

/*  case 000:                                           ** 105000-105017 */
/*      return cpu_user_00 ();                          ** uncomment to handle instruction */

/*  case 001:                                           ** 105020-105037 */
/*      return cpu_user_01 ();                          ** uncomment to handle instruction */

/*  case 0nn:                                           ** other cases as needed */
/*      return cpu_user_nn ();                          ** uncomment to handle instruction */

    case 020:                                           /* 10x400-10x417 */
        return cpu_user_20 ();                          /* call sample dispatcher */

/*  case 021:                                           ** 10x420-10x437 */
/*      return cpu_user_21 ();                          ** uncomment to handle instruction */

/*  case 0nn:                                           ** other cases as needed */
/*      return cpu_user_nn ();                          ** uncomment to handle instruction */

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
    }

return reason;
}


/* Example user microprogram simulator.

   User- or site-specific firmware may be simulated by writing the appropriate
   code below.  Unimplemented instructions should return "STOP (cpu_ss_unimpl)"
   to cause a simulator stop if enabled.

   For information on the operand patterns used in the "op_user" array, see the
   comments preceding the "cpu_ops" routine in "hp2100_cpu1.c" and the "operand
   processing encoding" constants in "hp2100_cpu1.h".
*/

static const OP_PAT op_user_20[16] = {
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N                     /*  ---    ---    ---    ---  */
  };

static t_stat cpu_user_20 (void)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

entry = IR & 017;                                       /* mask to entry point */

if (op_user_20 [entry] != OP_N) {
    reason = cpu_ops (op_user_20 [entry], op);          /* get instruction operands */
    if (reason != SCPE_OK)                              /* did the evaluation fail? */
        return reason;                                  /* return the reason for failure */
    }

switch (entry) {                                        /* decode IR<4:0> */

    case 000:                                           /* 10x400 */
/*      break;                                          ** uncomment to handle instruction */

    case 001:                                           /* 10x401 */
/*      break;                                          ** uncomment to handle instruction */

/*  case 0nn:                                           ** other cases as needed */
/*      break;                                          ** uncomment to handle instruction */

    default:                                            /* others unimplemented */
        reason = STOP (cpu_ss_unimpl);
    }

return reason;
}


/* Read a multiple-precision operand value */

OP ReadOp (HP_WORD va, OPSIZE precision)
{
OP operand;
uint32 i;

if (precision == in_s)
    operand.word = ReadW (va);                          /* read single integer */

else if (precision == in_d)
    operand.dword = ReadW (va) << 16 |                  /* read double integer */
                    ReadW ((va + 1) & LA_MASK);         /* merge high and low words */

else
    for (i = 0; i < (uint32) precision; i++) {          /* read fp 2 to 5 words */
        operand.fpk[i] = ReadW (va);
        va = (va + 1) & LA_MASK;
        }
return operand;
}

/* Write a multiple-precision operand value */

void WriteOp (HP_WORD va, OP operand, OPSIZE precision)
{
uint32 i;

if (precision == in_s)
    WriteW (va, operand.word);                          /* write single integer */

else if (precision == in_d) {
    WriteW (va, UPPER_WORD (operand.dword));                /* write double integer */
    WriteW (va + 1 & LA_MASK, LOWER_WORD (operand.dword));  /* high word, then low word */
    }

else
    for (i = 0; i < (uint32) precision; i++) {          /* write fp 2 to 5 words */
        WriteW (va, operand.fpk[i]);
        va = (va + 1) & LA_MASK;
        }
return;
}


/* Get instruction operands.

   Operands for a given instruction are specifed by an "operand pattern"
   consisting of flags indicating the types and storage methods.  The pattern
   directs how each operand is to be retrieved and whether the operand value or
   address is returned in the operand array.

   Typically, a microcode simulation handler will define an OP_PAT array, with
   each element containing an operand pattern corresponding to the simulated
   instruction.  Operand patterns are defined in the header file accompanying
   this source file.  After calling this function with the appropriate operand
   pattern and a pointer to an array of OPs, operands are decoded and stored
   sequentially in the array.

   The following operand encodings are defined:

      Code   Operand Description                         Example    Return
     ------  ----------------------------------------  -----------  ------------
     OP_NUL  No operand present                           [inst]    None

     OP_IAR  Integer constant in A register                LDA I    Value of I
                                                          [inst]
                                                           ...
                                                        I  DEC 0

     OP_JAB  Double integer constant in A/B registers      DLD J    Value of J
                                                          [inst]
                                                           ...
                                                        J  DEC 0,0

     OP_FAB  2-word FP constant in A/B registers           DLD F    Value of F
                                                          [inst]
                                                           ...
                                                        F  DEC 0.0

     OP_CON  Inline 1-word constant                       [inst]    Value of C
                                                        C  DEC 0
                                                           ...

     OP_VAR  Inline 1-word variable                       [inst]    Address of V
                                                        V  BSS 1
                                                           ...

     OP_ADR  Inline address                               [inst]    Address of A
                                                           DEF A
                                                           ...
                                                        A  EQU *

     OP_ADK  Address of integer constant                  [inst]    Value of K
                                                           DEF K
                                                           ...
                                                        K  DEC 0

     OP_ADD  Address of double integer constant           [inst]    Value of D
                                                           DEF D
                                                           ...
                                                        D  DEC 0,0

     OP_ADF  Address of 2-word FP constant                [inst]    Value of F
                                                           DEF F
                                                           ...
                                                        F  DEC 0.0

     OP_ADX  Address of 3-word FP constant                [inst]    Value of X
                                                           DEF X
                                                           ...
                                                        X  DEX 0.0

     OP_ADT  Address of 4-word FP constant                [inst]    Value of T
                                                           DEF T
                                                           ...
                                                        T  DEY 0.0

     OP_ADE  Address of 5-word FP constant                [inst]    Value of E
                                                           DEF E
                                                           ...
                                                        E  DEC 0,0,0,0,0

   Address operands, i.e., those having a DEF to the operand, will be resolved
   to direct addresses.  If an interrupt is pending and more than three levels
   of indirection are used, the routine returns without completing operand
   retrieval (the instruction will be retried after interrupt servicing).
   Addresses are always resolved in the current DMS map.

   An operand pattern consists of one or more operand encodings, corresponding
   to the operands required by a given instruction.  Values are returned in
   sequence to the operand array.
*/

t_stat cpu_ops (OP_PAT pattern, OPS op)
{
OP_PAT  flags;
uint32  i;
t_stat  reason = SCPE_OK;

for (i = 0; i < OP_N_F; i++) {
    flags = pattern & OP_M_FLAGS;                       /* get operand pattern */

    if (flags >= OP_ADR) {                              /* address operand? */
        MR = ReadW (PR);                                /* get the pointer */

        reason = cpu_resolve_indirects (TRUE);          /* resolve indirects */

        if (reason != SCPE_OK)                          /* resolution failed? */
            return reason;
        }

    switch (flags) {
        case OP_NUL:                                    /* null operand */
            return reason;                              /* no more, so quit */

        case OP_IAR:                                    /* int in A */
            (*op++).word = AR;                          /* get one-word value */
            break;

        case OP_JAB:                                    /* dbl-int in A/B */
            (*op++).dword = (AR << 16) | BR;            /* get two-word value */
            break;

        case OP_FAB:                                    /* 2-word FP in A/B */
            (*op).fpk[0] = AR;                          /* get high FP word */
            (*op++).fpk[1] = BR;                        /* get low FP word */
            break;

        case OP_CON:                                    /* inline constant operand */
            *op++ = ReadOp (PR, in_s);                  /* get value */
            break;

        case OP_VAR:                                    /* inline variable operand */
            (*op++).word = PR;                          /* get pointer to variable */
            break;

        case OP_ADR:                                    /* inline address operand */
            (*op++).word = MR;                          /* get address (set by "resolve" above) */
            break;

        case OP_ADK:                                    /* address of int constant */
            *op++ = ReadOp (MR, in_s);                  /* get value */
            break;

        case OP_ADD:                                    /* address of dbl-int constant */
            *op++ = ReadOp (MR, in_d);                  /* get value */
            break;

        case OP_ADF:                                    /* address of 2-word FP const */
            *op++ = ReadOp (MR, fp_f);                  /* get value */
            break;

        case OP_ADX:                                    /* address of 3-word FP const */
            *op++ = ReadOp (MR, fp_x);                  /* get value */
            break;

        case OP_ADT:                                    /* address of 4-word FP const */
            *op++ = ReadOp (MR, fp_t);                  /* get value */
            break;

        case OP_ADE:                                    /* address of 5-word FP const */
            *op++ = ReadOp (MR, fp_e);                  /* get value */
            break;

        default:
            return SCPE_IERR;                           /* not implemented */
        }

    if (flags >= OP_CON)                                /* operand after instruction? */
        PR = (PR + 1) & LA_MASK;                        /* yes, so bump to next */
    pattern = pattern >> OP_N_FLAGS;                    /* move next pattern into place */
    }
return reason;
}
