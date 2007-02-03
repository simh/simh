/* hp2100_cpu0.c: HP 1000 unimplemented instruction set stubs

   Copyright (c) 2006, J. David Bryan

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

   CPU0         Unimplemented firmware option instructions

   01-Dec-06    JDB     Removed and implemented "cpu_sis".
   26-Sep-06    JDB     Created

   This file contains template simulations for the firmware options that have
   not yet been implemented.  When a given firmware option is implemented, it
   should be moved out of this file and into another (or its own, depending on
   complexity).

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
        (92851-90001, Mar-1981)
   - Macro/1000 Reference Manual (92059-90001, Dec-1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.
*/

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"


t_stat cpu_rte_ema (uint32 IR, uint32 intrq);               /* RTE-4 EMA */
t_stat cpu_rte_vma (uint32 IR, uint32 intrq);               /* RTE-6 VMA */
t_stat cpu_rte_os (uint32 IR, uint32 intrq, uint32 iotrap); /* RTE-6 OS */
t_stat cpu_ds  (uint32 IR, uint32 intrq);               /* Distributed System */
t_stat cpu_vis (uint32 IR, uint32 intrq);               /* Vector Instruction Set */
t_stat cpu_signal (uint32 IR, uint32 intrq);            /* SIGNAL/1000 Instructions */


/* RTE-IV Extended Memory Area Instructions

   The RTE-IV operating system (HP product number 92067A) introduced the
   Extended Memory Area (EMA) instructions.  EMA provided a mappable data area
   up to one megaword in size.  These three instructions accelerated data
   accesses to variables stored in EMA partitions.  Support was limited to
   E/F-Series machines; M-Series machines used software equivalents.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    92067A  92067A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     .EMIO    105240   EMA I/O
     MMAP     105241   Map physical to logical memory
     [test]   105242   [self test]
     .EMAP    105257   Resolve array element address

   Notes:

     1. RTE-IV EMA and RTE-6 VMA instructions share the same address space, so a
        given machine can run one or the other, but not both.

   Additional references:
    - RTE-IVB Programmer's Reference Manual (92068-90004, Dec-1983).
    - RTE-IVB Technical Specifications (92068-90013, Jan-1980).
*/

static const OP_PAT op_ema[16] = {
  OP_A,    OP_AKK,    OP_N,    OP_N,                    /* .EMIO  MMAP   [test]  ---   */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_A                     /*  ---    ---    ---   .EMAP  */
  };

t_stat cpu_rte_ema (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

if ((cpu_unit.flags & UNIT_EMA) == 0)                   /* EMA option installed? */
    return stop_inst;

entry = IR & 017;                                       /* mask to entry point */

if (op_ema[entry] != OP_N)
    if (reason = cpu_ops (op_ema[entry], op, intrq))    /* get instruction operands */
        return reason;

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others undefined */
        reason = stop_inst;
        }

return reason;
}


/* RTE-6/VM Virtual Memory Area Instructions

   RTE-6/VM (product number 92084A) introduced Virtual Memory Area (VMA)
   instructions -- a superset of the RTE-IV EMA instructions.  Different
   microcode was supplied with the operating system that replaced the microcode
   used with RTE-IV.  Microcode was limited to the E/F-Series, and the M-Series
   used software equivalents.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    92084A  92084A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     .PMAP    105240   Map VMA page into map register
     $LOC     105241   Load on call
     [test]   105242   [self test]
     .SWP     105243   [Swap A and B registers]
     .STAS    105244   [STA B; LDA SP]
     .LDAS    105245   [LDA SP]
     .MYAD    105246   [NOP in microcode]
     .UMPY    105247   [Unsigned multiply and add]

     .IMAP    105250   Integer element resolve address and map
     .IMAR    105251   Integer element resolve address
     .JMAP    105252   Double integer element resolve address and map
     .JMAR    105253   Double integer element resolve address
     .LPXR    105254   Map pointer in P+1 plus offset in P+2
     .LPX     105255   Map pointer in A/B plus offset in P+1
     .LBPR    105256   Map pointer in P+1
     .LBP     105257   Map pointer in A/B registers

   Notes:

     1. The opcodes 105243-247 are undocumented and do not appear to be used in
        any HP software.

     2. The opcode list in the CE Handbook incorrectly shows 105246 as ".MYAD -
        multiply 2 signed integers."  The microcode listing shows that this
        instruction was deleted, and the opcode is now a NOP.

     3. RTE-IV EMA and RTE-6 VMA instructions shared the same address space, so
        a given machine could run one or the other, but not both.

   Additional references:
    - RTE-6/VM VMA/EMA Microcode Source (92084-18828, revision 3).
    - RTE-6/VM Technical Specifications (92084-90015, Apr-1983).
    - M/E/F-Series Computer Systems CE Handbook (5950-3767, Jul-1984).
*/

static const OP_PAT op_vma[16] = {
  OP_N,    OP_KKKAKK, OP_N,    OP_N,                    /* .PMAP  $LOC   [test] .SWAP  */
  OP_N,    OP_N,      OP_N,    OP_K,                    /* .STAS  .LDAS  .MYAD  .UMPY  */
  OP_A,    OP_A,      OP_A,    OP_A,                    /* .IMAP  .IMAR  .JMAP  .JMAR  */
  OP_FF,   OP_F,      OP_F,    OP_N                     /* .LPXR  .LPX   .LBPR  .LBP   */
  };

t_stat cpu_rte_vma (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

if ((cpu_unit.flags & UNIT_VMAOS) == 0)                 /* VMA/OS option installed? */
    return cpu_rte_ema (IR, intrq);                     /* try EMA */

entry = IR & 017;                                       /* mask to entry point */

if (op_vma[entry] != OP_N)
    if (reason = cpu_ops (op_vma[entry], op, intrq))    /* get instruction operands */
        return reason;

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others undefined */
        reason = stop_inst;
        }

return reason;
}


/* RTE-6/VM Operating System Instructions

   The OS instructions were added to acccelerate certain time-consuming
   operations of the RTE-6/VM operating system, HP product number 92084A.
   Microcode was available for the E- and F-Series; the M-Series used software
   equivalents.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A    92084A  92084A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     $LIBR    105340   Enter privileged/reentrant library routine
     $LIBX    105341   Exit privileged/reentrant library routine
     .TICK    105342   TBG tick interrupt handler
     .TNAM    105343   Find ID segment that matches name
     .STIO    105344   Configure I/O instructions
     .FNW     105345   Find word with user increment
     .IRT     105346   Interrupt return processing
     .LLS     105347   Linked list search

     .SIP     105350   Skip if interrupt pending
     .YLD     105351   .SIP completion return point
     .CPM     105352   Compare words LT/EQ/GT
     .ETEQ    105353   Set up EQT pointers in base page
     .ENTN    105354   Transfer parameter addresses (utility)
     [test]   105355   [self test]
     .ENTC    105356   Transfer parameter addresses (priv/reent)
     .DSPI    105357   Set display indicator

   Opcodes 105354-105357 are "dual use" instructions that take different
   actions, depending on whether they are executed from a trap cell during an
   interrupt.  When executed from a trap cell, they have these actions:

     Instr.  1000-E/F   Description
     ------  --------  ----------------------------------------------
     [dma]    105354   DCPC channel interrupt processing
     [dms]    105355   DMS/MP/PE interrupt processing
     [dev]    105356   Standard device interrupt processing
     [tbg]    105357   TBG interrupt processing

   Notes:

     1. The microcode differentiates between interrupt processing and normal
        execution of the "dual use" instructions by testing the CPU flag.
        Interrupt vectoring sets the flag; a normal instruction fetch clears it.
        Under simulation, interrupt vectoring is indicated by the value of the
        "iotrap" parameter.

     2. The operand patterns for .ENTN and .ENTC normally would be coded as
        "OP_A", as each takes a single address as a parameter.  However, because
        they might also be executed from a trap cell, we cannot assume that P+1
        is an address, or we might cause a DM abort when trying to access
        memory.  Therefore, "OP_A" handling is done within each routine, once
        the type of use is determined.

   Additional references:
    - RTE-6/VM O/S Microcode Source (92084-18831, revision 6).
    - RTE-6/VM Technical Specifications (92084-90015, Apr-1983).
*/

static const OP_PAT op_os[16] = {
  OP_A,    OP_A,    OP_N,    OP_N,                      /* $LIBR  $LIBX  .TICK  .TNAM  */
  OP_A,    OP_K,    OP_A,    OP_KK,                     /* .STIO  .FNW   .IRT   .LLS   */
  OP_N,    OP_CC,   OP_KK,   OP_N,                      /* .SIP   .YLD   .CPM   .ETEQ  */
  OP_N,    OP_N,    OP_N,    OP_N                       /* .ENTN  [test] .ENTC  .DSPI  */
  };

t_stat cpu_rte_os (uint32 IR, uint32 intrq, uint32 iotrap)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

if ((cpu_unit.flags & UNIT_VMAOS) == 0)                 /* VMA/OS option installed? */
    return stop_inst;

entry = IR & 017;                                       /* mask to entry point */

if (op_os[entry] != OP_N)  
    if (reason = cpu_ops (op_os[entry], op, intrq))     /* get instruction operands */
        return reason;

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others undefined */
        reason = stop_inst;
        }

return reason;
}


/* Distributed System

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
   12665A Hardwired Serial Data Interface Card and the 12620A RTE Privileged
   Interrupt Fence.  These are required for DS/1000.

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

t_stat cpu_ds (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

if ((cpu_unit.flags & UNIT_DS) == 0)                    /* DS option installed? */
    return stop_inst;

entry = IR & 017;                                       /* mask to entry point */

if (op_ds[entry] != OP_N)
    if (reason = cpu_ops (op_ds[entry], op, intrq))     /* get instruction operands */
        return reason;

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others undefined */
        reason = stop_inst;
        }

return reason;
}


/* Vector Instruction Set

   The VIS provides instructions that operate on one-dimensional arrays of
   floating-point values.  Both single- and double-precision operations are
   supported.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A    12824A

   The routines are mapped to instruction codes as follows:

        Single-Precision        Double-Precision
     Instr.  Opcode  Subcod  Instr.  Opcode  Subcod  Description
     ------  ------  ------  ------  ------  ------  -----------------------------
     VADD    101460  000000  DVADD   105460  004002  Vector add
     VSUB    101460  000020  DVSUB   105460  004022  Vector subtract
     VMPY    101460  000040  DVMPY   105460  004042  Vector multiply
     VDIV    101460  000060  DVDIV   105460  004062  Vector divide
     VSAD    101460  000400  DVSAD   105460  004402  Scalar-vector add
     VSSB    101460  000420  DVSSB   105460  004422  Scalar-vector subtract
     VSMY    101460  000440  DVSMY   105460  004442  Scalar-vector multiply
     VSDV    101460  000460  DVSDV   105460  004462  Scalar-vector divide
     VPIV    101461  0xxxxx  DVPIV   105461  0xxxxx  Vector pivot
     VABS    101462  0xxxxx  DVABS   105462  0xxxxx  Vector absolute value
     VSUM    101463  0xxxxx  DVSUM   105463  0xxxxx  Vector sum
     VNRM    101464  0xxxxx  DVNRM   105464  0xxxxx  Vector norm
     VDOT    101465  0xxxxx  DVDOT   105465  0xxxxx  Vector dot product
     VMAX    101466  0xxxxx  DVMAX   105466  0xxxxx  Vector maximum value
     VMAB    101467  0xxxxx  DVMAB   105467  0xxxxx  Vector maximum absolute value
     VMIN    101470  0xxxxx  DVMIN   105470  0xxxxx  Vector minimum value
     VMIB    101471  0xxxxx  DVMIB   105471  0xxxxx  Vector minimum absolute value
     VMOV    101472  0xxxxx  DVMOV   105472  0xxxxx  Vector move
     VSWP    101473  0xxxxx  DVSWP   105473  0xxxxx  Vector swap
     .ERES   101474    --     --       --      --    Resolve array element address
     .ESEG   101475    --     --       --      --    Load MSEG maps
     .VSET   101476    --     --       --      --    Vector setup
     [test]    --      --     --     105477    --    [self test]

   Instructions use IR bit 11 to select single- or double-precision format.  The
   double-precision instruction names begin with "D" (e.g., DVADD vs. VADD).
   Most VIS instructions are two words in length, with a sub-opcode immediately
   following the primary opcode.

   Notes:

     1. The .VECT (101460) and .DVCT (105460) opcodes preface a single- or
        double-precision arithmetic operation that is determined by the
        sub-opcode value.  The remainder of the dual-precision sub-opcode values
        are "don't care," except for requiring a zero in bit 15.

     2. The VIS uses the hardware FPP of the F-Series.  FPP malfunctions are
        detected by the VIS firmware and are indicated by a memory-protect
        violation and setting the overflow flag.  Under simulation,
        malfunctions cannot occur.

     3. VIS ROM data are available from Bitsavers.

   Additional references:
    - 12824A Vector Instruction Set User's Manual (12824-90001, Jun-1979).
*/

static const OP_PAT op_vis[16] = {
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---  */
  OP_N,    OP_N,      OP_N,    OP_N                     /*  ---    ---    ---    ---  */
  };

t_stat cpu_vis (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

if ((cpu_unit.flags & UNIT_VIS) == 0)                   /* VIS option installed? */
    return stop_inst;

entry = IR & 017;                                       /* mask to entry point */

if (op_vis[entry] != OP_N)
    if (reason = cpu_ops (op_vis[entry], op, intrq))    /* get instruction operands */
        return reason;

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others undefined */
        reason = stop_inst;
        }

return reason;
}


/* SIGNAL/1000 Instructions

   The SIGNAL/1000 instructions provide fast Fourier transforms and complex
   arithmetic.  They utilize the F-Series floating-point processor and the
   Vector Instruction Set.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A    92835A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-F  Description
     ------  ------  ----------------------------------------------
     BITRV   105600  Bit reversal
     BTRFY   105601  Butterfly algorithm
     UNSCR   105602  Unscramble for phasor MPY
     PRSCR   105603  Unscramble for phasor MPY
     BITR1   105604  Swap two elements in array (alternate format)
     BTRF1   105605  Butterfly algorithm (alternate format)
     .CADD   105606  Complex number addition
     .CSUB   105607  Complex number subtraction
     .CMPY   105610  Complex number multiplication
     .CDIV   105611  Complex number division
     CONJG   105612  Complex conjugate
     ..CCM   105613  Complex complement
     AIMAG   105614  Return imaginary part
     CMPLX   105615  Form complex number
     [nop]   105616  [no operation]
     [test]  105617  [self test]

   Notes:

     1. SIGNAL/1000 ROM data are available from Bitsavers.

   Additional references (documents unavailable):
    - HP Signal/1000 User Reference and Installation Manual (92835-90002).
*/

static const OP_PAT op_signal[16] = {
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_N,                    /*  ---    ---    ---    ---   */
  OP_N,    OP_N,      OP_N,    OP_N                     /*  ---    ---    ---    ---   */
  };

t_stat cpu_signal (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
uint32 entry;

if ((cpu_unit.flags & UNIT_SIGNAL) == 0)                /* SIGNAL option installed? */
    return stop_inst;

entry = IR & 017;                                       /* mask to entry point */

if (op_signal[entry] != OP_N)
    if (reason = cpu_ops (op_signal[entry], op, intrq)) /* get instruction operands */
        return reason;

switch (entry) {                                        /* decode IR<3:0> */

    default:                                            /* others undefined */
        reason = stop_inst;
        }

return reason;
}
